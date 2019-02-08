//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file nvme_cmds.c   Implementation for NVM Express command functions
//                     The intention of the file is to be generic & not OS specific

#if !defined(DISABLE_NVME_PASSTHROUGH)
#include "platform_helper.h"

#include "nvme_helper.h"
#include "nvme_helper_func.h"
#include "common_public.h"

int nvme_Cmd(tDevice *device, nvmeCmdCtx * cmdCtx)
{
    int ret = UNKNOWN;
    cmdCtx->device = device;
    //check the opcode bits for data direction and the data direction and make sure they match!
    //If they don't match, return an error for BAD_PARAMETER. Most OS passthroughs parse the op code to figure this out
    //The enum helps us confirm we know what the command sender is intending to do and it will get done correctly!
    uint8_t opcode = M_Byte0(cmdCtx->cmd.dwords.cdw0);
    if (cmdCtx->commandType == NVM_ADMIN_CMD)
    {
        opcode = cmdCtx->cmd.adminCmd.opcode;
    }
    else if (cmdCtx->commandType == NVM_CMD)
    {
        opcode = cmdCtx->cmd.nvmCmd.opcode;
    }
    switch (M_GETBITRANGE(opcode, 1, 0))
    {
    case 0://no data
        if (cmdCtx->commandDirection != XFER_NO_DATA)
        {
            return BAD_PARAMETER;
        }
        break;
    case 1://host to controller (out)
        if (cmdCtx->commandDirection != XFER_DATA_OUT)
        {
            return BAD_PARAMETER;
        }
        break;
    case 2://controller to host (in)
        if (cmdCtx->commandDirection != XFER_DATA_IN)
        {
            return BAD_PARAMETER;
        }
        break;
    case 3://bidirectional
    default://handles bidirectional...
        if (cmdCtx->commandDirection != XFER_DATA_IN_OUT && cmdCtx->commandDirection != XFER_DATA_OUT_IN)
        {
            return BAD_PARAMETER;
        }
        break;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_NVMe_Cmd_Verbose(cmdCtx);
    }
#if defined (_DEBUG)
    //This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a command.
    //This was very important for debugging windows issues, which is why I have this ifdef in place for debug builds. - TJE
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != NULL)
#else
    //Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != NULL && cmdCtx->commandDirection == XFER_DATA_OUT)
#endif
    {
        printf("\t  Data Buffer being sent:\n");
        print_Data_Buffer(cmdCtx->ptrData, cmdCtx->dataSize, true);
        printf("\n");
    }
    ret = send_NVMe_IO(cmdCtx);
    if (cmdCtx->commandCompletionData.dw3Valid)
    {
        device->drive_info.lastNVMeResult.lastNVMeStatus = cmdCtx->commandCompletionData.statusAndCID;
        if (ret != OS_PASSTHROUGH_FAILURE && ret != OS_COMMAND_NOT_AVAILABLE && ret != OS_COMMAND_BLOCKED)
        {
            ret = check_NVMe_Status(cmdCtx->commandCompletionData.statusAndCID);
        }
    }
    else
    {
        //didn't get a status for one reason or another, so clear out anything that may have been left behind from a previous command.
        device->drive_info.lastNVMeResult.lastNVMeStatus = 0;
    }
    if (cmdCtx->commandCompletionData.dw0Valid)
    {
        device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = cmdCtx->commandCompletionData.commandSpecific;
    }
    else
    {
        //didn't get a status for one reason or another, so clear out anything that may have been left behind from a previous command.
        device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = 0;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_NVMe_Cmd_Result_Verbose(cmdCtx);
    }
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
#if defined (_DEBUG)
    //This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a command.
    //This was very important for debugging windows issues, which is why I have this ifdef in place for debug builds. - TJE
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != NULL)
#else
    //Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != NULL && cmdCtx->commandDirection == XFER_DATA_IN)
#endif
    {
        printf("\t  Data Buffer being returned:\n");
        print_Data_Buffer(cmdCtx->ptrData, cmdCtx->dataSize, true);
        printf("\n");
    }
    return ret;
}

int nvme_Abort_Command(tDevice *device, uint16_t commandIdentifier, uint16_t submissionQueueIdentifier)
{
    int ret = SUCCESS;
    nvmeCmdCtx adminCommand;
    memset(&adminCommand, 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ABORT_CMD;
    adminCommand.cmd.adminCmd.cdw10 = M_WordsTo4ByteValue(commandIdentifier, submissionQueueIdentifier);
    adminCommand.commandDirection = XFER_NO_DATA;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Abort Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 3h = The number of concurrently outstanding Abort commands has exceeded the limit indicated in the Identify Controller data structure
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Abort", ret);
    }
    return ret;
}

int nvme_Asynchronous_Event_Request(tDevice *device, uint8_t *logPageIdentifier, uint8_t *asynchronousEventInformation, uint8_t *asynchronousEventType)
{
    int ret = SUCCESS;
    nvmeCmdCtx adminCommand;
    memset(&adminCommand, 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ASYNC_EVENT;
    adminCommand.commandDirection = XFER_NO_DATA;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Asynchronous Event Request Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 5h = The number of concurrently outstanding Asynchronous Event Request commands has been exceeded.
    //TODO: Figure out how to get the completion queue DWORD0 information out...is this the same as DWORD0 going in?
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Asynchronous Event Request", ret);
    }
    return ret;
}

int nvme_Device_Self_Test(tDevice *device, uint32_t nsid, uint8_t selfTestCode)
{
    int ret = SUCCESS;
    nvmeCmdCtx adminCommand;
    memset(&adminCommand, 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_DEVICE_SELF_TEST;
    adminCommand.commandDirection = XFER_NO_DATA;
    adminCommand.cmd.adminCmd.nsid = nsid;
    adminCommand.cmd.adminCmd.cdw10 = selfTestCode;//lowest 4 bits. All others are reserved
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Device Self Test Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //Command specific return codes:
    // 1Dh = The controller or NVM subsystem already has a device self-test operation in process
    //TODO: we should try putting this at a different level in case this is returned on some other command to inform that something is happening
    if (adminCommand.commandCompletionData.dw3Valid)
    {
        if (M_GETBITRANGE(adminCommand.commandCompletionData.statusAndCID, 27, 25) == NVME_SCT_COMMAND_SPECIFIC_STATUS
            && M_GETBITRANGE(adminCommand.commandCompletionData.statusAndCID, 24, 17) == 0x1D)
        {
            ret = IN_PROGRESS;
        }
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Device Self Test", ret);
    }
    return ret;
}

int nvme_Security_Send(tDevice *device, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t nvmeSecuritySpecificField, uint8_t *ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    nvmeCmdCtx adminCommand;
    memset(&adminCommand, 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SECURITY_SEND;
    adminCommand.commandDirection = XFER_DATA_OUT;
    adminCommand.cmd.adminCmd.addr = (uint64_t)ptrData;//TODO: does this need a cast?
    adminCommand.ptrData = ptrData;
    adminCommand.dataSize = dataLength;
    adminCommand.cmd.adminCmd.cdw10 = M_BytesTo4ByteValue(securityProtocol, M_Word1(securityProtocolSpecific), M_Word0(securityProtocolSpecific), nvmeSecuritySpecificField);
    adminCommand.cmd.adminCmd.cdw11 = dataLength;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Security Send Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Send", ret);
    }
    return ret;
}

int nvme_Security_Receive(tDevice *device, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t nvmeSecuritySpecificField, uint8_t *ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    nvmeCmdCtx adminCommand;
    memset(&adminCommand, 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SECURITY_RECV;
    adminCommand.commandDirection = XFER_DATA_IN;
    adminCommand.cmd.adminCmd.addr = (uint64_t)ptrData;//TODO: does this need a cast?
    adminCommand.ptrData = ptrData;
    adminCommand.dataSize = dataLength;
    adminCommand.cmd.adminCmd.cdw10 = M_BytesTo4ByteValue(securityProtocol, M_Word1(securityProtocolSpecific), M_Word0(securityProtocolSpecific), nvmeSecuritySpecificField);
    adminCommand.cmd.adminCmd.cdw11 = dataLength;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Security Receive Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Receive", ret);
    }
    return ret;
}

int nvme_Write_Uncorrectable(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks)
{
    int ret = SUCCESS;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_WRITE_UNCOR;
    nvmCommand.commandDirection = XFER_NO_DATA;
    nvmCommand.ptrData = NULL;
    nvmCommand.dataSize = 0;
    nvmCommand.cmd.nvmCmd.cdw10 = M_DoubleWord0(startingLBA);//lba
    nvmCommand.cmd.nvmCmd.cdw11 = M_DoubleWord1(startingLBA);//lba
    nvmCommand.cmd.nvmCmd.cdw12 = numberOfLogicalBlocks;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Write Uncorrectable Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Uncorrectable", ret);
    }
    return ret;
}

int nvme_Dataset_Management(tDevice *device, uint8_t numberOfRanges, bool deallocate, bool integralDatasetForWrite, bool integralDatasetForRead, uint8_t *ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_DATA_SET_MANAGEMENT;
    nvmCommand.commandDirection = XFER_DATA_OUT;
    nvmCommand.ptrData = ptrData;
    nvmCommand.dataSize = dataLength;
    nvmCommand.device = device;
    nvmCommand.cmd.nvmCmd.cdw10 = numberOfRanges;//number of ranges
    if (deallocate)
    {
        nvmCommand.cmd.nvmCmd.cdw11 |= BIT2;
    }
    if (integralDatasetForWrite)
    {
        nvmCommand.cmd.nvmCmd.cdw11 |= BIT1;
    }
    if (integralDatasetForRead)
    {
        nvmCommand.cmd.nvmCmd.cdw11 |= BIT0;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Dataset Management Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Dataset Management", ret);
    }
    return ret;
}

int nvme_Flush(tDevice *device)
{
    int ret = SUCCESS;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_FLUSH;
    nvmCommand.commandDirection = XFER_NO_DATA;
    nvmCommand.ptrData = NULL;
    nvmCommand.dataSize = 0;
    nvmCommand.device = device;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Flush Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Flush", ret);
    }
    return ret;
}

int nvme_Write(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks, bool limitedRetry, bool fua, uint8_t protectionInformationField, uint8_t directiveType, uint8_t *ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_WRITE;
    nvmCommand.commandDirection = XFER_DATA_OUT;
    nvmCommand.ptrData = ptrData;
    nvmCommand.dataSize = dataLength;
    nvmCommand.device = device;

    //slba
    nvmCommand.cmd.nvmCmd.cdw10 = M_DoubleWord0(startingLBA);
    nvmCommand.cmd.nvmCmd.cdw11 = M_DoubleWord1(startingLBA);
    nvmCommand.cmd.nvmCmd.cdw12 = numberOfLogicalBlocks;
    if (limitedRetry)
    {
        nvmCommand.cmd.nvmCmd.cdw12 |= BIT31;
    }
    if (fua)
    {
        nvmCommand.cmd.nvmCmd.cdw12 |= BIT30;
    }
    nvmCommand.cmd.nvmCmd.cdw12 |= (uint32_t)(protectionInformationField & 0x0F) << 26;
    nvmCommand.cmd.nvmCmd.cdw12 |= (uint32_t)(directiveType & 0x0F) << 20;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Write Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write", ret);
    }
    return ret;
}

int nvme_Read(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks, bool limitedRetry, bool fua, uint8_t protectionInformationField, uint8_t *ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_READ;
    nvmCommand.commandDirection = XFER_DATA_IN;
    nvmCommand.ptrData = ptrData;
    nvmCommand.dataSize = dataLength;
    nvmCommand.device = device;

    //slba
    nvmCommand.cmd.nvmCmd.cdw10 = M_DoubleWord0(startingLBA);
    nvmCommand.cmd.nvmCmd.cdw11 = M_DoubleWord1(startingLBA);
    nvmCommand.cmd.nvmCmd.cdw12 = numberOfLogicalBlocks;
    if (limitedRetry)
    {
        nvmCommand.cmd.nvmCmd.cdw12 |= BIT31;
    }
    if (fua)
    {
        nvmCommand.cmd.nvmCmd.cdw12 |= BIT30;
    }
    nvmCommand.cmd.nvmCmd.cdw12 |= (uint32_t)(protectionInformationField & 0x0F) << 26;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Read Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read", ret);
    }
    return ret;
}

int nvme_Compare(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks, bool limitedRetry, bool fua, uint8_t protectionInformationField, uint8_t *ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_COMPARE;
    nvmCommand.commandDirection = XFER_DATA_OUT;
    nvmCommand.ptrData = ptrData;
    nvmCommand.dataSize = dataLength;
    nvmCommand.device = device;

    //slba
    nvmCommand.cmd.nvmCmd.cdw10 = M_DoubleWord0(startingLBA);
    nvmCommand.cmd.nvmCmd.cdw11 = M_DoubleWord1(startingLBA);
    nvmCommand.cmd.nvmCmd.cdw12 = numberOfLogicalBlocks;
    if (limitedRetry)
    {
        nvmCommand.cmd.nvmCmd.cdw12 |= BIT31;
    }
    if (fua)
    {
        nvmCommand.cmd.nvmCmd.cdw12 |= BIT30;
    }
    nvmCommand.cmd.nvmCmd.cdw12 |= (uint32_t)(protectionInformationField & 0x0F) << 26;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Compare Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Compare", ret);
    }
    return ret;
}


int nvme_Firmware_Image_Dl(tDevice *device,\
                            uint32_t bufferOffset,\
                            uint32_t numberOfBytes,\
                            uint8_t *ptrData)
{
    int ret = SUCCESS;
    nvmeCmdCtx ImageDl;
    memset(&ImageDl, 0, sizeof(ImageDl));

	ImageDl.cmd.adminCmd.opcode = NVME_ADMIN_CMD_DOWNLOAD_FW;
    ImageDl.commandType = NVM_ADMIN_CMD;
    ImageDl.commandDirection = XFER_DATA_OUT;
	ImageDl.cmd.adminCmd.addr = (uint64_t)ptrData;
    ImageDl.ptrData = ptrData;
    ImageDl.dataSize = numberOfBytes;
	ImageDl.cmd.adminCmd.cdw10 = (numberOfBytes >> 2) - 1; //Since this is, 0 based, number of DWords not Bytes. 
    ImageDl.cmd.adminCmd.cdw11 = bufferOffset >> 2;
    ImageDl.timeout = 15;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Firmware Image Download Command\n");
    }
    ret = nvme_Cmd(device, &ImageDl);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Firmware Image Download", ret);
    }
    return ret;
}

int nvme_Firmware_Commit(tDevice *device, nvmeFWCommitAction commitAction, uint8_t firmwareSlot)
{
    int ret = BAD_PARAMETER;
    nvmeCmdCtx FirmwareCommit;

    if (firmwareSlot > 7)
    {
        return BAD_PARAMETER;//returning this for now.
    }
    memset(&FirmwareCommit, 0, sizeof(FirmwareCommit));

	FirmwareCommit.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ACTIVATE_FW;
    FirmwareCommit.commandType = NVM_ADMIN_CMD;
    FirmwareCommit.commandDirection = XFER_NO_DATA;
	FirmwareCommit.cmd.adminCmd.cdw10 = (commitAction << 3); // 05:03 Bits CA
    FirmwareCommit.cmd.adminCmd.cdw10 |= (firmwareSlot & 0x07); // 02:00 Bits Firmware Slot
    FirmwareCommit.timeout = 30;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Firmware Commit Command\n");
    }
    ret = nvme_Cmd(device, &FirmwareCommit);

    //removed all the code checking for specific status code since it was not really in the right place.
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Firmware Commit", ret);
    }
    return ret;
}

int nvme_Identify(tDevice *device, uint8_t *ptrData, uint32_t nvmeNamespace, uint32_t cns)
{
	nvmeCmdCtx identify;
    int ret = SUCCESS;
	memset(&identify, 0, sizeof(identify));
	identify.cmd.adminCmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    identify.commandType = NVM_ADMIN_CMD;
    identify.commandDirection = XFER_DATA_IN;
	identify.cmd.adminCmd.nsid = nvmeNamespace;
	identify.cmd.adminCmd.addr = (uint64_t)ptrData;
	identify.cmd.adminCmd.cdw10 = cns;
    identify.timeout = 15;
	identify.ptrData = ptrData;
	identify.dataSize = NVME_IDENTIFY_DATA_LEN;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Identify Command\n");
    }
	ret = nvme_Cmd(device, &identify);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Identify", ret);
    }
    return ret;
}

int nvme_Get_Features(tDevice *device, nvmeFeaturesCmdOpt * featCmdOpts)
{
    int ret = UNKNOWN; 
	nvmeCmdCtx getFeatures;
    uint32_t dWord10 = 0;
	memset(&getFeatures, 0, sizeof(getFeatures));
	getFeatures.cmd.adminCmd.opcode = NVME_ADMIN_CMD_GET_FEATURES;
    getFeatures.commandType = NVM_ADMIN_CMD;
    getFeatures.commandDirection = XFER_DATA_IN;
    getFeatures.cmd.adminCmd.addr = featCmdOpts->prp1; // TODO: dataLen? 
    getFeatures.cmd.adminCmd.metadata = featCmdOpts->prp2; 
    //getFeatures.dataSize = featCmdOpts.dataSize; //TODO: allow this since a get features could return other data

    dWord10 = featCmdOpts->sel << 8; 
    dWord10 |= featCmdOpts->fid;

    getFeatures.cmd.adminCmd.cdw10 = dWord10;
    getFeatures.cmd.adminCmd.cdw11 = featCmdOpts->featSetGetValue;
    getFeatures.timeout = 15;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Get Features Command\n");
    }

    ret = nvme_Cmd(device, &getFeatures);

    /*if (VERBOSITY_DEFAULT < device->deviceVerbosity)
    {
        printf("\tReturn: cdw10=0x%08X, cdw11=0x%08X result=0x%08X\n",\
                        getFeatures.cmd.adminCmd.cdw10,getFeatures.cmd.adminCmd.cdw11, getFeatures.result);
    }*/

    if (ret == SUCCESS) 
    {
        featCmdOpts->featSetGetValue = getFeatures.commandCompletionData.commandSpecific;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get Features", ret);
    }
    return ret;
}

int nvme_Set_Features(tDevice *device, nvmeFeaturesCmdOpt * featCmdOpts)
{
    int ret = UNKNOWN; 
    nvmeCmdCtx setFeatures;
    uint32_t dWord10 = 0;
    memset(&setFeatures, 0, sizeof(setFeatures));
    setFeatures.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SET_FEATURES;
    setFeatures.commandType = NVM_ADMIN_CMD;
    setFeatures.commandDirection = XFER_DATA_OUT;
    setFeatures.cmd.adminCmd.addr = featCmdOpts->prp1; 
    //setFeatures.dataSize = featCmdOpts.dataSize;// TODO: dataLen? 
    setFeatures.cmd.adminCmd.metadata = featCmdOpts->prp2; 

    dWord10 = featCmdOpts->sv << 31; 
    dWord10 |= featCmdOpts->fid;

    setFeatures.cmd.adminCmd.cdw10 = dWord10;
    setFeatures.cmd.adminCmd.cdw11 = featCmdOpts->featSetGetValue;

    setFeatures.timeout = 15;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Set Features Command\n");
    }

    ret = nvme_Cmd(device, &setFeatures);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Features", ret);
    }
    return ret;
}

int nvme_Sanitize(tDevice *device, bool noDeallocateAfterSanitize, bool invertBetweenOverwritePasses, uint8_t overWritePassCount, bool allowUnrestrictedSanitizeExit, uint8_t sanitizeAction, uint32_t overwritePattern)
{
    int ret = UNKNOWN;
    nvmeCmdCtx nvmCommand;
    memset(&nvmCommand, 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType = NVM_ADMIN_CMD;
    nvmCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SANITIZE;
    nvmCommand.commandDirection = XFER_NO_DATA;

    //set the overwrite pass count first
    nvmCommand.cmd.adminCmd.cdw10 = (uint32_t)(overWritePassCount << 4);
    
    if (noDeallocateAfterSanitize)
    {
        nvmCommand.cmd.adminCmd.cdw10 |= BIT9;
    }
    if (invertBetweenOverwritePasses)
    {
        nvmCommand.cmd.adminCmd.cdw10 |= BIT8;
    }
    if (allowUnrestrictedSanitizeExit)
    {
        nvmCommand.cmd.adminCmd.cdw10 |= BIT3;
    }
    nvmCommand.cmd.adminCmd.cdw10 |= M_GETBITRANGE(sanitizeAction, 2, 0);
    nvmCommand.cmd.adminCmd.cdw11 = overwritePattern;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Sanitize Command\n");
    }

    ret = nvme_Cmd(device, &nvmCommand);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Sanitize", ret);
    }

    return ret;
}

int nvme_Get_Log_Page(tDevice *device, nvmeGetLogPageCmdOpts * getLogPageCmdOpts)
{
    int ret = UNKNOWN; 
    nvmeCmdCtx getLogPage;
    uint32_t dWord10 = 0;
    uint32_t numDwords = 0;
    memset(&getLogPage, 0, sizeof(getLogPage));
    getLogPage.cmd.adminCmd.opcode = NVME_ADMIN_CMD_GET_LOG_PAGE;
    getLogPage.commandType = NVM_ADMIN_CMD;
    getLogPage.commandDirection = XFER_DATA_IN;
    #if defined(VMK_CROSS_COMP)
    getLogPage.cmd.adminCmd.addr = (uint32_t)getLogPageCmdOpts->addr;
    #else
    getLogPage.cmd.adminCmd.addr = (uint64_t)getLogPageCmdOpts->addr;
    #endif
    getLogPage.dataSize = getLogPageCmdOpts->dataLen;
    getLogPage.ptrData = getLogPageCmdOpts->addr;
    getLogPage.cmd.adminCmd.nsid = getLogPageCmdOpts->nsid;

    numDwords = (getLogPageCmdOpts->dataLen / NVME_DWORD_SIZE) - 1;//zero based DWORD value
    
    dWord10 |= getLogPageCmdOpts->lid;
    dWord10 |= (getLogPageCmdOpts->lsp & 0x0F) << 8;
    dWord10 |= (getLogPageCmdOpts->rae & 0x01) << 15;
    dWord10 |= numDwords << 16;

    getLogPage.cmd.adminCmd.cdw10 = dWord10;
    getLogPage.cmd.adminCmd.cdw11 = numDwords >> 16;

    getLogPage.cmd.adminCmd.cdw12 = (uint32_t)(getLogPageCmdOpts->offset & 0xFFFFFFFF);
    getLogPage.cmd.adminCmd.cdw13 = (uint32_t)(getLogPageCmdOpts->offset >> 32);

	getLogPage.ptrData = (uint8_t*)getLogPageCmdOpts->addr;
	getLogPage.dataSize = getLogPageCmdOpts->dataLen;

    getLogPage.timeout = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Get Log Page Command\n");
    }

    ret = nvme_Cmd(device, &getLogPage);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get Log Page", ret);
    }

    return ret;
}


int nvme_Format(tDevice *device, nvmeFormatCmdOpts * formatCmdOpts)
{
    int ret = UNKNOWN; 
    nvmeCmdCtx formatCmd;
    uint32_t dWord10 = 0;
    //Setting this check so we are not corrupting dWord10 
    // Revise if they change the spec is revised. 
    if (   (formatCmdOpts->pi > 7) 
        || (formatCmdOpts->ses > 7)
        || (formatCmdOpts->lbaf > 0xF)
           ) 
    {
        return BAD_PARAMETER;
    }

    memset(&formatCmd, 0, sizeof(formatCmd));
    formatCmd.cmd.adminCmd.opcode = NVME_ADMIN_CMD_FORMAT_NVM;
    formatCmd.commandType = NVM_ADMIN_CMD;
    formatCmd.commandDirection = XFER_NO_DATA;
    formatCmd.cmd.adminCmd.nsid = formatCmdOpts->nsid;

    //Construct the correct 
    dWord10 = formatCmdOpts->ses << 9; 
    if (formatCmdOpts->pil) 
    {
        dWord10 |= BIT8;
    }
    dWord10 |= formatCmdOpts->pi << 5; 
    if (formatCmdOpts->ms) 
    {
        dWord10 |= BIT4;
    }
    dWord10 |= (formatCmdOpts->lbaf & 0x0F); // just the nibble. 

    formatCmd.cmd.adminCmd.cdw10 = dWord10;

    formatCmd.timeout = 30; //seconds

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Format Command\n");
    }

    ret = nvme_Cmd(device, &formatCmd);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Format", ret);
    }

    return ret;
}

int nvme_Reservation_Report(tDevice *device, bool extendedDataStructure, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    nvmeCmdCtx nvmCmd;
    memset(&nvmCmd, 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_REPORT;
    nvmCmd.cmd.nvmCmd.nsid = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1 = (uint64_t)ptrData;
    nvmCmd.commandDirection = XFER_DATA_IN;
    nvmCmd.commandType = NVM_CMD;
    nvmCmd.dataSize = dataSize;
    nvmCmd.device = device;
    nvmCmd.ptrData = ptrData;
    nvmCmd.timeout = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = (dataSize >> 2) - 1;//convert bytes to a number of dwords (zeros based value)

    if (extendedDataStructure)
    {
        nvmCmd.cmd.nvmCmd.cdw11 |= BIT0;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Reservation Report Command\n");
    }

    ret = nvme_Cmd(device, &nvmCmd);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Reservation Report", ret);
    }

    return ret;
}

int nvme_Reservation_Register(tDevice *device, uint8_t changePersistThroughPowerLossState, bool ignoreExistingKey, uint8_t reservationRegisterAction, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    nvmeCmdCtx nvmCmd;
    memset(&nvmCmd, 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_REGISTER;
    nvmCmd.cmd.nvmCmd.nsid = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1 = (uint64_t)ptrData;
    nvmCmd.commandDirection = XFER_DATA_OUT;
    nvmCmd.commandType = NVM_CMD;
    nvmCmd.dataSize = dataSize;
    nvmCmd.device = device;
    nvmCmd.ptrData = ptrData;
    nvmCmd.timeout = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = changePersistThroughPowerLossState << 30;
    if (ignoreExistingKey)
    {
        nvmCmd.cmd.nvmCmd.cdw10 |= BIT3;
    }
    nvmCmd.cmd.nvmCmd.cdw10 |= M_GETBITRANGE(reservationRegisterAction, 2, 0);


    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Reservation Register Command\n");
    }

    ret = nvme_Cmd(device, &nvmCmd);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Reservation Register", ret);
    }

    return ret;
}

int nvme_Reservation_Acquire(tDevice *device, uint8_t reservationType, bool ignoreExistingKey, uint8_t reservtionAcquireAction, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    nvmeCmdCtx nvmCmd;
    memset(&nvmCmd, 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_ACQUIRE;
    nvmCmd.cmd.nvmCmd.nsid = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1 = (uint64_t)ptrData;
    nvmCmd.commandDirection = XFER_DATA_OUT;
    nvmCmd.commandType = NVM_CMD;
    nvmCmd.dataSize = dataSize;
    nvmCmd.device = device;
    nvmCmd.ptrData = ptrData;
    nvmCmd.timeout = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = reservationType << 8;
    if (ignoreExistingKey)
    {
        nvmCmd.cmd.nvmCmd.cdw10 |= BIT3;
    }
    nvmCmd.cmd.nvmCmd.cdw10 |= M_GETBITRANGE(reservtionAcquireAction, 2, 0);


    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Reservation Acquire Command\n");
    }

    ret = nvme_Cmd(device, &nvmCmd);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Reservation Acquire", ret);
    }

    return ret;
}

int nvme_Reservation_Release(tDevice *device, uint8_t reservationType, bool ignoreExistingKey, uint8_t reservtionReleaseAction, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    nvmeCmdCtx nvmCmd;
    memset(&nvmCmd, 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_RELEASE;
    nvmCmd.cmd.nvmCmd.nsid = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1 = (uint64_t)ptrData;
    nvmCmd.commandDirection = XFER_DATA_OUT;
    nvmCmd.commandType = NVM_CMD;
    nvmCmd.dataSize = dataSize;
    nvmCmd.device = device;
    nvmCmd.ptrData = ptrData;
    nvmCmd.timeout = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = reservationType << 8;
    if (ignoreExistingKey)
    {
        nvmCmd.cmd.nvmCmd.cdw10 |= BIT3;
    }
    nvmCmd.cmd.nvmCmd.cdw10 |= M_GETBITRANGE(reservtionReleaseAction, 2, 0);


    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Reservation Release Command\n");
    }

    ret = nvme_Cmd(device, &nvmCmd);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Reservation Release", ret);
    }

    return ret;
}

int nvme_Read_Ctrl_Reg(tDevice *device, nvmeBarCtrlRegisters * ctrlRegs)
{
    int ret = UNKNOWN;
    //For now lets first get the page aligned one & then copy the 
    int dataSize = getpagesize();
    uint8_t * barRegs = calloc(dataSize,sizeof(uint8_t));
    if (!barRegs)
    {
        return MEMORY_FAILURE;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Reading PCI Bar Registers\n");
    }
    ret = pci_Read_Bar_Reg( device, barRegs, (uint32_t)dataSize );
    if (ret == SUCCESS) 
    {
        memcpy(ctrlRegs,barRegs,sizeof(nvmeBarCtrlRegisters));
    }
    
    free(barRegs);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("PCI Bar Registers", ret);
    }

    return ret;
}

#endif
