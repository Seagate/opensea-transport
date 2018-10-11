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
    print_NVMe_Cmd_Verbose(cmdCtx);
#if defined (_DEBUG)
    //This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a command.
    //This was very important for debugging windows issues, which is why I have this ifdef in place for debug builds. - TJE
    if (VERBOSITY_BUFFERS <= g_verbosity && cmdCtx->ptrData != NULL)
#else
    //Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= g_verbosity && cmdCtx->ptrData != NULL && cmdCtx->commandDirection == XFER_DATA_OUT)
#endif
    {
        printf("\t  Data Buffer being sent:\n");
        print_Data_Buffer(cmdCtx->ptrData, cmdCtx->dataSize, true);
        printf("\n");
    }
    ret = send_NVMe_IO(cmdCtx);
    print_NVMe_Cmd_Result_Verbose(cmdCtx);//This function is currently a stub! Need to fill it in with something useful! We may need to add reading the error log to get something to print out! - TJE
    print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
#if defined (_DEBUG)
    //This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a command.
    //This was very important for debugging windows issues, which is why I have this ifdef in place for debug builds. - TJE
    if (VERBOSITY_BUFFERS <= g_verbosity && cmdCtx->ptrData != NULL)
#else
    //Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= g_verbosity && cmdCtx->ptrData != NULL && cmdCtx->commandDirection == XFER_DATA_IN)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Abort Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 3h = The number of concurrently outstanding Abort commands has exceeded the limit indicated in the Identify Controller data structure
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Asynchronous Event Request Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 5h = The number of concurrently outstanding Asynchronous Event Request commands has been exceeded.
    //TODO: Figure out how to get the completion queue DWORD0 information out...is this the same as DWORD0 going in?
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Device Self Test Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 1Dh = The controller or NVM subsystem already has a device self-test operation in process
    if (adminCommand.result == 0x1D)
    {
        ret = IN_PROGRESS;
    }
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    adminCommand.cmd.adminCmd.dataLen = dataLength;
    adminCommand.dataSize = dataLength;
    adminCommand.cmd.adminCmd.cdw10 = M_BytesTo4ByteValue(securityProtocol, M_Word1(securityProtocolSpecific), M_Word0(securityProtocolSpecific), nvmeSecuritySpecificField);
    adminCommand.cmd.adminCmd.cdw11 = dataLength;
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Security Send Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    adminCommand.cmd.adminCmd.dataLen = dataLength;
    adminCommand.dataSize = dataLength;
    adminCommand.cmd.adminCmd.cdw10 = M_BytesTo4ByteValue(securityProtocol, M_Word1(securityProtocolSpecific), M_Word0(securityProtocolSpecific), nvmeSecuritySpecificField);
    adminCommand.cmd.adminCmd.cdw11 = dataLength;
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Security Receive Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Write Uncorrectable Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Dataset Management Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Flush Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Write Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Read Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read", ret);
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
	ImageDl.cmd.adminCmd.dataLen = numberOfBytes;
	ImageDl.cmd.adminCmd.cdw10 = (numberOfBytes >> 2) - 1; //Since this is, 0 based, number of DWords not Bytes. 
    ImageDl.cmd.adminCmd.cdw11 = bufferOffset >> 2;
    ImageDl.result = 0;
    ImageDl.timeout = 15;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Firmware Image Download Command\n");
    }
    ret = nvme_Cmd(device, &ImageDl);
    if ( (ret != SUCCESS) && (VERBOSITY_QUIET < g_verbosity) )
    {
        //14h is OVERLAPPING RANGE
        printf("\nNVME Firmware Download Command Failed (0x%02X)\n",ImageDl.result);
    }
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Firmware Image Download", ret);
    }
    return ret;
}

int nvme_Firmware_Commit(tDevice *device, nvmeFWCommitAction commitAction, uint8_t firmwareSlot)
{
    int ret = BAD_PARAMETER;
    nvmeCmdCtx FirmwareCommit;

    if ( (firmwareSlot < 1) 
         || (firmwareSlot > 7)
         || (commitAction >= NVME_CA_INVALID) )
    {
        if(VERBOSITY_QUIET < g_verbosity)
        {
            printf("WARN: Possibly invalid parameters for Firmware Commit CMD CA=0x%02X, FS=0x%02X\n",\
                   firmwareSlot, commitAction);
        }
        // return ret; // Not returning in case someone wants to test their drive with bogus params
    }
    memset(&FirmwareCommit, 0, sizeof(FirmwareCommit));

	FirmwareCommit.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ACTIVATE_FW;
    FirmwareCommit.commandType = NVM_ADMIN_CMD;
    FirmwareCommit.commandDirection = XFER_NO_DATA;
	FirmwareCommit.cmd.adminCmd.cdw10 = (commitAction << 3); // 05:03 Bits CA
    FirmwareCommit.cmd.adminCmd.cdw10 |= (firmwareSlot & 0x07); // 02:00 Bits Firmware Slot
    FirmwareCommit.result = 0;
    FirmwareCommit.timeout = 15;
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Firmware Commit Command\n");
    }
    ret = nvme_Cmd(device, &FirmwareCommit);

    if(ret != SUCCESS)
    {
        switch(device->os_info.last_error & 0x00FF)
        {
        case NVME_FW_DL_REQUIRES_SYS_RST:
        case NVME_FW_DL_REQUIRES_NVM_RST:
        case NVME_FW_DL_ON_NEXT_RST:
            ret = SUCCESS;
            break;
        case NVME_FW_DL_INVALID_SLOT:
            if(VERBOSITY_QUIET < g_verbosity)
            {
                printf("INVALID SLOT\n");
            }
        case NVME_FW_DL_INVALID_IMG:
            if(VERBOSITY_QUIET < g_verbosity)
            {
                printf("INVALID IMAGE\n");
            }
        case NVME_FW_DL_MAX_TIME_VIOLATION:
            if(VERBOSITY_QUIET < g_verbosity)
            {
                printf("Err: Firmware Activation Requires Maximum Time Violation\n");
            }
        case NVME_FW_DL_ACT_PROHIBITED:
            if(VERBOSITY_QUIET < g_verbosity)
            {
                printf("ACTIVATION PROHIBITED\n");
            }
        case NVME_FW_DL_OVERLAPPING_RANGE:
            if(VERBOSITY_QUIET < g_verbosity)
            {
                printf("OVERLAPPING RANGE\n");
            }
        default:
            printf("device->os_info.last_error=0x%x\n", device->os_info.last_error);
            break;
        }
    }
        
    if ( (ret != SUCCESS) && (VERBOSITY_QUIET < g_verbosity) )
    {
        printf("\nNVME Firmware Commit Command Failed (0x%02X)\n",FirmwareCommit.result);
        //TODO: add a switch statement for enum nvmeFWCommitRC 
    }
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
	identify.cmd.adminCmd.addr = (unsigned long)ptrData;
	identify.cmd.adminCmd.dataLen = NVME_IDENTIFY_DATA_LEN;
	identify.cmd.adminCmd.cdw10 = cns;
    identify.timeout = 15;
	//Added the following for Windows. 
	identify.ptrData = ptrData;
	identify.dataSize = NVME_IDENTIFY_DATA_LEN;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Identify Command");
    }
	ret = nvme_Cmd(device, &identify);
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
	//getFeatures.cmd.adminCmd.addr = NULL;
	getFeatures.cmd.adminCmd.dataLen = 0;
    getFeatures.cmd.adminCmd.addr = featCmdOpts->prp1; // TODO: dataLen? 
    getFeatures.cmd.adminCmd.metadata = featCmdOpts->prp2; 

    dWord10 = featCmdOpts->sel << 8; 
    dWord10 |= featCmdOpts->fid;

    getFeatures.cmd.adminCmd.cdw10 = dWord10;
    getFeatures.cmd.adminCmd.cdw11 = featCmdOpts->featSetGetValue;
    getFeatures.timeout = 15;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Get Features Command");
    }

    ret = nvme_Cmd(device, &getFeatures);

    /*if (VERBOSITY_DEFAULT < g_verbosity)
    {
        printf("\tReturn: cdw10=0x%08X, cdw11=0x%08X result=0x%08X\n",\
                        getFeatures.cmd.adminCmd.cdw10,getFeatures.cmd.adminCmd.cdw11, getFeatures.result);
    }*/

    if (ret == SUCCESS) 
    {
        featCmdOpts->featSetGetValue = getFeatures.result;
    }
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    //getFeatures.cmd.adminCmd.addr = NULL;
    setFeatures.cmd.adminCmd.dataLen = 0;
    setFeatures.cmd.adminCmd.addr = featCmdOpts->prp1; // TODO: dataLen? 
    setFeatures.cmd.adminCmd.metadata = featCmdOpts->prp2; 

    dWord10 = featCmdOpts->sv << 30; 
    dWord10 |= featCmdOpts->fid;

    setFeatures.cmd.adminCmd.cdw10 = dWord10;
    setFeatures.cmd.adminCmd.cdw11 = featCmdOpts->featSetGetValue;

    setFeatures.timeout = 15;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Set Features Command");
    }

    ret = nvme_Cmd(device, &setFeatures);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Sanitize Command");
    }

    ret = nvme_Cmd(device, &nvmCommand);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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
    getLogPage.cmd.adminCmd.dataLen = getLogPageCmdOpts->dataLen;
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Get Log Page Command");
    }

    ret = nvme_Cmd(device, &getLogPage);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending NVMe Format Command");
    }

    ret = nvme_Cmd(device, &formatCmd);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Format", ret);
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
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Reading PCI Bar Registers\n");
    }
    ret = pci_Read_Bar_Reg( device, barRegs, (uint32_t)dataSize );
    if (ret == SUCCESS) 
    {
        memcpy(ctrlRegs,barRegs,sizeof(nvmeBarCtrlRegisters));
    }
    
    free(barRegs);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("PCI Bar Registers", ret);
    }

    return ret;
}

//TODO: This should be calling the get log page function, not being it's own function!
int nvme_Read_Ext_Smt_Log(tDevice *device, EXTENDED_SMART_INFO_T *ExtdSMARTInfo)
{
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
   // EXTENDED_SMART_INFO_T ExtdSMARTInfo;
    fb_log_page_CF          logPageCF;
    int index;
    uint32_t   nsid = 1;
    uint8_t  log_id = 0xC4;
    uint32_t data_len = sizeof(EXTENDED_SMART_INFO_T);
    void* ptr = ExtdSMARTInfo;
	 
    nvmeCmdCtx extSmatLog;
    memset(&extSmatLog, 0, sizeof(extSmatLog));
    int ret = SUCCESS;
    uint32_t  numd = (data_len >> 2) - 1;
    uint16_t  numdu = numd >> 16, numdl = numd & 0xffff;
    
    extSmatLog.cmd.adminCmd.opcode = NVME_ADMIN_CMD_GET_LOG_PAGE;
    extSmatLog.commandType = NVM_ADMIN_CMD;
    extSmatLog.commandDirection = XFER_DATA_IN;
    extSmatLog.cmd.adminCmd.nsid = nsid;
    extSmatLog.cmd.adminCmd.addr = (unsigned long)ptr;
    extSmatLog.cmd.adminCmd.dataLen = data_len;
    extSmatLog.cmd.adminCmd.cdw10 = log_id | (numdl << 16);
    extSmatLog.cmd.adminCmd.cdw11 = numdu;
    extSmatLog.timeout = 15;
	//Added the following for Windows. 
    extSmatLog.ptrData = ptr;
    extSmatLog.dataSize = NVME_IDENTIFY_DATA_LEN;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Reading NVMe Ext SMART Log through duplicate function\n");
    }
    ret = nvme_Cmd(device, &extSmatLog);
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Ext SMART Log", ret);
    }
    return ret;
}

//TODO: The naming scheme of this function doesn't match the rest of the file, and this may be Seagate unique. Will need to investiage how this can be cleaned up.
int pci_Correctble_Err(tDevice *device,uint8_t  opcode, uint32_t  nsid, uint32_t  cdw10, uint32_t cdw11, uint32_t data_len, void *data)
{
    int ret = 0;
    nvmeCmdCtx pciEr;
    memset (&pciEr, 0x00, sizeof(nvmeCmdCtx));
    pciEr.cmd.adminCmd.opcode = opcode;
    pciEr.commandType = NVM_ADMIN_CMD;
    //pciEr.commandDirection = XFER_DATA_IN;
    pciEr.cmd.adminCmd.nsid = nsid;
    pciEr.cmd.adminCmd.addr = (unsigned long)data;
    pciEr.cmd.adminCmd.dataLen = data_len;
    pciEr.cmd.adminCmd.cdw10 = cdw10;
    pciEr.cmd.adminCmd.cdw11 = cdw11;
    pciEr.timeout = 15;
        //Added the following for Windows.
    pciEr.ptrData = data;
    pciEr.dataSize = NVME_IDENTIFY_DATA_LEN;
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("PCI Correctable Error\n");
    }
    ret = nvme_Cmd(device, &pciEr);
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("PCI Correctable Error", ret);
    }
    return ret;
}
#endif
