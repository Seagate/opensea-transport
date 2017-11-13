//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

void print_NVMe_Cmd_Ctx (const nvmeCmdCtx * cmdCtx)
{
    printf("Cmd Type: %s\n", cmdCtx->commandType ? "NVME_CMD" : "NVM_ADMIN_CMD");
    printf("Cmd Direction: %d\n",cmdCtx->commandDirection); //TODO: This needs to be a fuction in common_public.h enum-to-string etc. 
    printf("Cmd Data Ptr: %p\n, Size %d\n", cmdCtx->ptrData, cmdCtx->dataSize);
    printf("Cmd timeout %d\n", cmdCtx->timeout);
    printf("Cmd result 0x%02X\n", cmdCtx->result);
    if (cmdCtx->commandType == NVM_ADMIN_CMD)
    {
        printf("\tDopcode=%" PRIu8 " //CDW0\n",cmdCtx->cmd.adminCmd.opcode);
        printf("\tflags=%" PRIu8 " //CDW0\n",cmdCtx->cmd.adminCmd.flags);
        printf("\trsvd1=%" PRIu16 " //CDW0\n",cmdCtx->cmd.adminCmd.rsvd1);
        printf("\tnsid=%" PRIu32 "\n",cmdCtx->cmd.adminCmd.nsid);
        printf("\tcdw2=%" PRIu32 "\n",cmdCtx->cmd.adminCmd.cdw2);
        printf("\tcdw3=%" PRIu32 "\n",cmdCtx->cmd.adminCmd.cdw3);
        printf("\tmetadata=%" PRIu64 "\n",cmdCtx->cmd.adminCmd.metadata);
        printf("\tmetadata_len=%" PRIu32 "\n",cmdCtx->cmd.adminCmd.metadataLen);
        printf("\taddr=%" PRIu64 "\n",cmdCtx->cmd.adminCmd.addr);
        printf("\taddr_len=%" PRIu32 "\n",cmdCtx->cmd.adminCmd.dataLen);
        printf("\tcdw10=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw10);
        printf("\tcdw11=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw11);
        printf("\tcdw12=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw12);
        printf("\tcdw13=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw13);
        printf("\tcdw13=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw13);
        printf("\tcdw14=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw14);
        printf("\tcdw15=0x%08" PRIX32 "\n",cmdCtx->cmd.adminCmd.cdw15);
    }
    //TOOD: Add Non Admin command, which we currently don't use. 
}

int nvme_Cmd(tDevice *device, nvmeCmdCtx * cmdCtx)
{
    int ret = UNKNOWN;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif

    cmdCtx->device = device;

    ret = send_NVMe_IO(cmdCtx);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
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
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 3h = The number of concurrently outstanding Abort commands has exceeded the limit indicated in the Identify Controller data structure
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
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 5h = The number of concurrently outstanding Asynchronous Event Request commands has been exceeded.
    //TODO: Figure out how to get the completion queue DWORD0 information out...is this the same as DWORD0 going in?
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
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    // 1Dh = The controller or NVM subsystem already has a device self-test operation in process
    if (adminCommand.result == 0x1D)
    {
        ret = IN_PROGRESS;
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
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
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
    ret = nvme_Cmd(device, &adminCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
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
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
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
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
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
    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
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

    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
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

    ret = nvme_Cmd(device, &nvmCommand);
    //TODO: Need a function to print out some verbose information for any/all commands (if possible)
    //Command specific return codes:
    return ret;
}

int nvme_Firmware_Image_Dl(tDevice *device,\
                            uint32_t bufferOffset,\
                            uint32_t numberOfBytes,\
                            uint8_t *ptrData)
{
    int ret = SUCCESS;
    nvmeCmdCtx ImageDl;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif


    memset(&ImageDl, 0, sizeof(ImageDl));

	ImageDl.cmd.adminCmd.opcode = NVME_ADMIN_CMD_DOWNLOAD_FW;
    ImageDl.commandType = NVM_ADMIN_CMD;
    ImageDl.commandDirection = XFER_DATA_OUT;
	ImageDl.cmd.adminCmd.addr = (unsigned long)ptrData;
	ImageDl.cmd.adminCmd.dataLen = numberOfBytes;
	ImageDl.cmd.adminCmd.cdw10 = (numberOfBytes >> 2) - 1; //Since this is, 0 based, number of DWords not Bytes. 
    ImageDl.cmd.adminCmd.cdw11 = bufferOffset >> 2;
    ImageDl.result = 0;
    ImageDl.timeout = 15;

#ifdef _DEBUG
    printf("%s: p=%p, sz=%d, cdw10=0x%X, cdw11=0x%X\n",\
           __FUNCTION__,ptrData, numberOfBytes,ImageDl.cmd.adminCmd.cdw10,ImageDl.cmd.adminCmd.cdw11 );
#endif
    
    ret = nvme_Cmd(device, &ImageDl);
    if ( (ret != SUCCESS) && (VERBOSITY_QUIET < g_verbosity) )
    {
        //14h is OVERLAPPING RANGE
        printf("\nNVME Firmware Download Command Failed (0x%02X)\n",ImageDl.result);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Firmware_Commit(tDevice *device, nvmeFWCommitAction commitAction, uint8_t firmwareSlot)
{
    int ret = BAD_PARAMETER;
    nvmeCmdCtx FirmwareCommit;

#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif

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
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Identify(tDevice *device, uint8_t *ptrData, uint32_t nvmeNamespace, uint32_t cns)
{
	nvmeCmdCtx identify;
    int ret = SUCCESS;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
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

	ret = nvme_Cmd(device, &identify);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Get_Features(tDevice *device, nvmeFeaturesCmdOpt * featCmdOpts)
{
    int ret = UNKNOWN; 
	nvmeCmdCtx getFeatures;
    uint32_t dWord10 = 0;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
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
    if (VERBOSITY_DEFAULT < g_verbosity)
    {
        printf("\tCMD: cdw10=0x%08X, cdw11=0x%08X\n",\
               getFeatures.cmd.adminCmd.cdw10,getFeatures.cmd.adminCmd.cdw11);
    }


    ret = nvme_Cmd(device, &getFeatures);

    if (VERBOSITY_DEFAULT < g_verbosity)
    {
        printf("\tReturn: cdw10=0x%08X, cdw11=0x%08X result=0x%08X\n",\
                        getFeatures.cmd.adminCmd.cdw10,getFeatures.cmd.adminCmd.cdw11, getFeatures.result);
    }

    if (ret == SUCCESS) 
    {
        featCmdOpts->featSetGetValue = getFeatures.result;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Set_Features(tDevice *device, nvmeFeaturesCmdOpt * featCmdOpts)
{
    int ret = UNKNOWN; 
    nvmeCmdCtx setFeatures;
    uint32_t dWord10 = 0;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
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

    ret = nvme_Cmd(device, &setFeatures);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
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
    return ret;
}

int nvme_Get_Log_Page(tDevice *device, nvmeGetLogPageCmdOpts * getLogPageCmdOpts)
{
    int ret = UNKNOWN; 
    nvmeCmdCtx getLogPage;
    uint32_t dWord10 = 0;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&getLogPage, 0, sizeof(getLogPage));
    getLogPage.cmd.adminCmd.opcode = NVME_ADMIN_CMD_GET_LOG_PAGE;
    getLogPage.commandType = NVM_ADMIN_CMD;
    getLogPage.commandDirection = XFER_DATA_IN;
    getLogPage.cmd.adminCmd.addr = getLogPageCmdOpts->addr;
    getLogPage.cmd.adminCmd.dataLen = getLogPageCmdOpts->dataLen;
    getLogPage.cmd.adminCmd.nsid = getLogPageCmdOpts->nsid;

    dWord10 = (getLogPageCmdOpts->dataLen / NVME_DWORD_SIZE) - 1;//zero based DWORD value
    dWord10 <<= 16;
    dWord10 |= getLogPageCmdOpts->lid;

    getLogPage.cmd.adminCmd.cdw10 = dWord10;

	getLogPage.ptrData = (uint8_t*)getLogPageCmdOpts->addr;
	getLogPage.dataSize = getLogPageCmdOpts->dataLen;

    getLogPage.timeout = 15;
#ifdef _DEBUG
    printf("%s: p=%p, sz=%d, cdw10=0x%X, nsid=0x%x\n",\
           __FUNCTION__,&getLogPage.cmd.adminCmd.addr,\
           getLogPage.cmd.adminCmd.dataLen,getLogPage.cmd.adminCmd.cdw10,
            getLogPage.cmd.adminCmd.nsid\
            );
#endif
    ret = nvme_Cmd(device, &getLogPage);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Format(tDevice *device, nvmeFormatCmdOpts * formatCmdOpts)
{
    int ret = UNKNOWN; 
    nvmeCmdCtx formatCmd;
    uint32_t dWord10 = 0;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Format NVM - Command Dword 10 = 0x%04x\n",dWord10);
    }

    formatCmd.timeout = 30; //seconds

    ret = nvme_Cmd(device, &formatCmd);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Read_Ctrl_Reg(tDevice *device, nvmeBarCtrlRegisters * ctrlRegs)
{
    int ret = UNKNOWN;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    //For now lets first get the page aligned one & then copy the 
    int dataSize = getpagesize();
    uint8_t * barRegs = calloc(dataSize,sizeof(uint8_t));
    if (!barRegs)
    {
        return MEMORY_FAILURE;
    }
    ret = pci_Read_Bar_Reg( device, barRegs, (uint32_t)dataSize );
    if (ret == SUCCESS) 
    {
        memcpy(ctrlRegs,barRegs,sizeof(nvmeBarCtrlRegisters));
    }
    
    free(barRegs);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

#endif
