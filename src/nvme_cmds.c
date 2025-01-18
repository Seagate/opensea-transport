// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file nvme_cmds.c   Implementation for NVM Express command functions
//                     The intention of the file is to be generic & not OS specific

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "platform_helper.h"

#include "asmedia_nvme_helper.h"
#include "common_public.h"
#include "jmicron_nvme_helper.h"
#include "nvme_helper.h"
#include "nvme_helper_func.h"
#include "realtek_nvme_helper.h"

eReturnValues nvme_Reset(tDevice* device)
{
    switch (device->drive_info.passThroughHacks.passthroughType)
    {
    case NVME_PASSTHROUGH_SYSTEM:
        return os_nvme_Reset(device);
    case NVME_PASSTHROUGH_JMICRON:
        return jm_nvme_Reset(device);
    case NVME_PASSTHROUGH_ASMEDIA:
        return asm_nvme_Reset(device);
    case NVME_PASSTHROUGH_REALTEK:
    case NVME_PASSTHROUGH_ASMEDIA_BASIC:
        return OS_COMMAND_NOT_AVAILABLE;
    default:
        return BAD_PARAMETER;
    }
}

eReturnValues nvme_Subsystem_Reset(tDevice* device)
{
    switch (device->drive_info.passThroughHacks.passthroughType)
    {
    case NVME_PASSTHROUGH_SYSTEM:
        return os_nvme_Subsystem_Reset(device);
    case NVME_PASSTHROUGH_JMICRON:
        return jm_nvme_Subsystem_Reset(device);
    case NVME_PASSTHROUGH_ASMEDIA:
        return asm_nvme_Subsystem_Reset(device);
    case NVME_PASSTHROUGH_REALTEK:
    case NVME_PASSTHROUGH_ASMEDIA_BASIC:
        return OS_COMMAND_NOT_AVAILABLE;
    default:
        return BAD_PARAMETER;
    }
}

eReturnValues nvme_Cmd(tDevice* device, nvmeCmdCtx* cmdCtx)
{
    eReturnValues ret = UNKNOWN;
    cmdCtx->device    = device;
    // check the opcode bits for data direction and the data direction and make sure they match!
    // If they don't match, return an error for BAD_PARAMETER. Most OS passthroughs parse the op code to figure this out
    // The enum helps us confirm we know what the command sender is intending to do and it will get done correctly!
    uint8_t opcode = M_Byte0(cmdCtx->cmd.dwords.cdw0);
    if (cmdCtx->commandType == NVM_ADMIN_CMD)
    {
        opcode = cmdCtx->cmd.adminCmd.opcode;
    }
    else if (cmdCtx->commandType == NVM_CMD)
    {
        opcode = cmdCtx->cmd.nvmCmd.opcode;
#if defined(_DEBUG)
        if (cmdCtx->cmd.nvmCmd.nsid != device->drive_info.namespaceID)
        {
            printf("WARNING: NVM Cmd NSID does not match expected value in tDevice\n");
        }
#endif //_DEBUG
    }
    switch (get_bit_range_uint8(opcode, 1, 0))
    {
    case 0: // no data
        if (cmdCtx->commandDirection != XFER_NO_DATA)
        {
            return BAD_PARAMETER;
        }
        break;
    case 1: // host to controller (out)
        if (cmdCtx->commandDirection != XFER_DATA_OUT)
        {
            return BAD_PARAMETER;
        }
        break;
    case 2: // controller to host (in)
        if (cmdCtx->commandDirection != XFER_DATA_IN)
        {
            return BAD_PARAMETER;
        }
        break;
    case 3:  // bidirectional
    default: // handles bidirectional...
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
    if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_SYSTEM)
    {
#if defined(_DEBUG)
        // This is different for debug because sometimes we need to see if the data buffer actually changed after
        // issuing a command. This was very important for debugging windows issues, which is why I have this ifdef in
        // place for debug builds. - TJE
        if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != M_NULLPTR)
#else
        // Only print the data buffer being sent when it is a data transfer to the drive (data out command)
        if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != M_NULLPTR &&
            cmdCtx->commandDirection == XFER_DATA_OUT)
#endif
        {
            printf("\t  Data Buffer being sent:\n");
            print_Data_Buffer(cmdCtx->ptrData, cmdCtx->dataSize, true);
            printf("\n");
        }
    }
    switch (device->drive_info.passThroughHacks.passthroughType)
    {
    case NVME_PASSTHROUGH_SYSTEM:
        ret = send_NVMe_IO(cmdCtx);
        break;
    case NVME_PASSTHROUGH_JMICRON:
        ret = send_JM_NVMe_Cmd(cmdCtx);
        break;
    case NVME_PASSTHROUGH_ASMEDIA_BASIC:
        ret = send_ASMedia_Basic_NVMe_Passthrough_Cmd(cmdCtx);
        break;
    case NVME_PASSTHROUGH_ASMEDIA:
        ret = send_ASM_NVMe_Cmd(cmdCtx);
        break;
    case NVME_PASSTHROUGH_REALTEK:
        ret = send_Realtek_NVMe_Cmd(cmdCtx);
        break;
    default:
        return BAD_PARAMETER;
    }
    if (cmdCtx->commandCompletionData.dw3Valid)
    {
        device->drive_info.lastNVMeResult.lastNVMeStatus = cmdCtx->commandCompletionData.statusAndCID;
        if (ret != OS_PASSTHROUGH_FAILURE && ret != OS_COMMAND_NOT_AVAILABLE && ret != OS_COMMAND_BLOCKED)
        {
            const nvmeStatus* stat = get_NVMe_Status(cmdCtx->commandCompletionData.statusAndCID);
            if (stat)
            {
                ret = stat->ret;
            }
            else
            {
                ret = UNKNOWN;
            }
        }
    }
    else
    {
        // didn't get a status for one reason or another, so clear out anything that may have been left behind from a
        // previous command.
        device->drive_info.lastNVMeResult.lastNVMeStatus = 0;
    }
    if (cmdCtx->commandCompletionData.dw0Valid)
    {
        device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = cmdCtx->commandCompletionData.commandSpecific;
    }
    else
    {
        // didn't get a status for one reason or another, so clear out anything that may have been left behind from a
        // previous command.
        device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = 0;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_NVMe_Cmd_Result_Verbose(cmdCtx);
    }
    if (device->drive_info.passThroughHacks.passthroughType == NVME_PASSTHROUGH_SYSTEM)
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
        }
#if defined(_DEBUG)
        // This is different for debug because sometimes we need to see if the data buffer actually changed after
        // issuing a command. This was very important for debugging windows issues, which is why I have this ifdef in
        // place for debug builds. - TJE
        if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != M_NULLPTR)
#else
        // Only print the data buffer being sent when it is a data transfer to the drive (data out command)
        if (VERBOSITY_BUFFERS <= device->deviceVerbosity && cmdCtx->ptrData != M_NULLPTR &&
            cmdCtx->commandDirection == XFER_DATA_IN)
#endif
        {
            printf("\t  Data Buffer being returned:\n");
            print_Data_Buffer(cmdCtx->ptrData, cmdCtx->dataSize, true);
            printf("\n");
        }
    }
    return ret;
}

eReturnValues nvme_Abort_Command(tDevice* device, uint16_t commandIdentifier, uint16_t submissionQueueIdentifier)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    adminCommand;
    safe_memset(&adminCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType         = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ABORT_CMD;
    adminCommand.cmd.adminCmd.cdw10  = M_WordsTo4ByteValue(commandIdentifier, submissionQueueIdentifier);
    adminCommand.commandDirection    = XFER_NO_DATA;
    adminCommand.timeout             = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Abort Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    // Command specific return codes:
    //  3h = The number of concurrently outstanding Abort commands has exceeded the limit indicated in the Identify
    //  Controller data structure
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Abort", ret);
    }
    return ret;
}

eReturnValues nvme_Asynchronous_Event_Request(tDevice* device,
                                              uint8_t* logPageIdentifier,
                                              uint8_t* asynchronousEventInformation,
                                              uint8_t* asynchronousEventType)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    adminCommand;
    safe_memset(&adminCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType         = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ASYNC_EVENT;
    adminCommand.commandDirection    = XFER_NO_DATA;
    adminCommand.timeout             = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Asynchronous Event Request Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    // Command specific return codes:
    //  5h = The number of concurrently outstanding Asynchronous Event Request commands has been exceeded.

    DISABLE_NONNULL_COMPARE
    if (logPageIdentifier != M_NULLPTR)
    {
        *logPageIdentifier = get_8bit_range_uint32(adminCommand.commandCompletionData.dw0, 23, 16);
    }

    if (asynchronousEventInformation != M_NULLPTR)
    {
        *asynchronousEventInformation = get_8bit_range_uint32(adminCommand.commandCompletionData.dw0, 15, 8);
    }

    if (asynchronousEventType != M_NULLPTR)
    {
        *asynchronousEventType = get_8bit_range_uint32(adminCommand.commandCompletionData.dw0, 2, 0);
    }
    RESTORE_NONNULL_COMPARE

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Asynchronous Event Request", ret);
    }
    return ret;
}

eReturnValues nvme_Device_Self_Test(tDevice* device, uint32_t nsid, uint8_t selfTestCode)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    adminCommand;
    safe_memset(&adminCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType         = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_DEVICE_SELF_TEST;
    adminCommand.commandDirection    = XFER_NO_DATA;
    adminCommand.cmd.adminCmd.nsid   = nsid;
    adminCommand.cmd.adminCmd.cdw10  = selfTestCode; // lowest 4 bits. All others are reserved
    adminCommand.timeout             = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Device Self Test Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);
    // Command specific return codes:
    //  1Dh = The controller or NVM subsystem already has a device self-test operation in process
    if (adminCommand.commandCompletionData.dw3Valid)
    {
        if (get_8bit_range_uint32(adminCommand.commandCompletionData.statusAndCID, 27, 25) ==
                NVME_SCT_COMMAND_SPECIFIC_STATUS &&
            get_8bit_range_uint32(adminCommand.commandCompletionData.statusAndCID, 24, 17) == 0x1D)
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

eReturnValues nvme_Security_Send(tDevice* device,
                                 uint8_t  securityProtocol,
                                 uint16_t securityProtocolSpecific,
                                 uint8_t  nvmeSecuritySpecificField,
                                 uint8_t* ptrData,
                                 uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    adminCommand;
    safe_memset(&adminCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType         = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SECURITY_SEND;
    adminCommand.commandDirection    = XFER_DATA_OUT;
    adminCommand.cmd.adminCmd.addr   = C_CAST(uintptr_t, ptrData);
    adminCommand.ptrData             = ptrData;
    adminCommand.dataSize            = dataLength;
    adminCommand.cmd.adminCmd.cdw10  = M_BytesTo4ByteValue(securityProtocol, M_Byte1(securityProtocolSpecific),
                                                           M_Byte0(securityProtocolSpecific), nvmeSecuritySpecificField);
    adminCommand.cmd.adminCmd.cdw11  = dataLength;
    adminCommand.timeout             = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Security Send Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Send", ret);
    }
    return ret;
}

eReturnValues nvme_Security_Receive(tDevice* device,
                                    uint8_t  securityProtocol,
                                    uint16_t securityProtocolSpecific,
                                    uint8_t  nvmeSecuritySpecificField,
                                    uint8_t* ptrData,
                                    uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    adminCommand;
    safe_memset(&adminCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    adminCommand.commandType         = NVM_ADMIN_CMD;
    adminCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SECURITY_RECV;
    adminCommand.commandDirection    = XFER_DATA_IN;
    adminCommand.cmd.adminCmd.addr   = C_CAST(uintptr_t, ptrData);
    adminCommand.ptrData             = ptrData;
    adminCommand.dataSize            = dataLength;
    adminCommand.cmd.adminCmd.cdw10  = M_BytesTo4ByteValue(securityProtocol, M_Byte1(securityProtocolSpecific),
                                                           M_Byte0(securityProtocolSpecific), nvmeSecuritySpecificField);
    adminCommand.cmd.adminCmd.cdw11  = dataLength;
    adminCommand.timeout             = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Security Receive Command\n");
    }
    ret = nvme_Cmd(device, &adminCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Receive", ret);
    }
    return ret;
}

eReturnValues nvme_Verify(tDevice* device,
                          uint64_t startingLBA,
                          bool     limitedRetry,
                          bool     fua,
                          uint8_t  protectionInformationField,
                          uint16_t numberOfLogicalBlocks)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_VERIFY;
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.commandDirection  = XFER_NO_DATA;
    nvmCommand.ptrData           = M_NULLPTR;
    nvmCommand.dataSize          = 0;
    nvmCommand.cmd.nvmCmd.cdw10  = M_DoubleWord0(startingLBA); // lba
    nvmCommand.cmd.nvmCmd.cdw11  = M_DoubleWord1(startingLBA); // lba
    nvmCommand.cmd.nvmCmd.cdw12  = numberOfLogicalBlocks;
    if (limitedRetry)
    {
        M_SET_BIT(nvmCommand.cmd.nvmCmd.cdw12, 31);
    }
    if (fua)
    {
        M_SET_BIT(nvmCommand.cmd.nvmCmd.cdw12, 30);
    }
    nvmCommand.cmd.nvmCmd.cdw12 |= C_CAST(uint32_t, protectionInformationField & 0x0F) << 26;
    nvmCommand.timeout = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Verify Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify", ret);
    }
    return ret;
}

eReturnValues nvme_Write_Uncorrectable(tDevice* device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_WRITE_UNCOR;
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.commandDirection  = XFER_NO_DATA;
    nvmCommand.ptrData           = M_NULLPTR;
    nvmCommand.dataSize          = 0;
    nvmCommand.cmd.nvmCmd.cdw10  = M_DoubleWord0(startingLBA); // lba
    nvmCommand.cmd.nvmCmd.cdw11  = M_DoubleWord1(startingLBA); // lba
    nvmCommand.cmd.nvmCmd.cdw12  = numberOfLogicalBlocks;
    nvmCommand.timeout           = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Write Uncorrectable Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Uncorrectable", ret);
    }
    return ret;
}

eReturnValues nvme_Dataset_Management(tDevice* device,
                                      uint8_t  numberOfRanges,
                                      bool     deallocate,
                                      bool     integralDatasetForWrite,
                                      bool     integralDatasetForRead,
                                      uint8_t* ptrData,
                                      uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_DATA_SET_MANAGEMENT;
    nvmCommand.commandDirection  = XFER_DATA_OUT;
    nvmCommand.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.ptrData           = ptrData;
    nvmCommand.dataSize          = dataLength;
    nvmCommand.device            = device;
    nvmCommand.cmd.nvmCmd.cdw10  = numberOfRanges; // number of ranges
    nvmCommand.timeout           = 15;
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

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Dataset Management", ret);
    }
    return ret;
}

eReturnValues nvme_Flush(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_FLUSH;
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.commandDirection  = XFER_NO_DATA;
    nvmCommand.ptrData           = M_NULLPTR;
    nvmCommand.dataSize          = 0;
    nvmCommand.device            = device;
    nvmCommand.timeout           = 15;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Flush Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Flush", ret);
    }
    return ret;
}

eReturnValues nvme_Write(tDevice* device,
                         uint64_t startingLBA,
                         uint16_t numberOfLogicalBlocks,
                         bool     limitedRetry,
                         bool     fua,
                         uint8_t  protectionInformationField,
                         uint8_t  directiveType,
                         uint8_t* ptrData,
                         uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_WRITE;
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.commandDirection  = XFER_DATA_OUT;
    nvmCommand.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCommand.ptrData           = ptrData;
    nvmCommand.dataSize          = dataLength;
    nvmCommand.device            = device;
    nvmCommand.timeout           = 15;

    // slba
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
    nvmCommand.cmd.nvmCmd.cdw12 |= C_CAST(uint32_t, protectionInformationField & 0x0F) << 26;
    nvmCommand.cmd.nvmCmd.cdw12 |= C_CAST(uint32_t, directiveType & 0x0F) << 20;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Write Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write", ret);
    }
    return ret;
}

eReturnValues nvme_Read(tDevice* device,
                        uint64_t startingLBA,
                        uint16_t numberOfLogicalBlocks,
                        bool     limitedRetry,
                        bool     fua,
                        uint8_t  protectionInformationField,
                        uint8_t* ptrData,
                        uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_READ;
    nvmCommand.commandDirection  = XFER_DATA_IN;
    nvmCommand.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCommand.ptrData           = ptrData;
    nvmCommand.dataSize          = dataLength;
    nvmCommand.device            = device;
    nvmCommand.timeout           = 15;

    // slba
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
    nvmCommand.cmd.nvmCmd.cdw12 |= C_CAST(uint32_t, protectionInformationField & 0x0F) << 26;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Read Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read", ret);
    }
    return ret;
}

eReturnValues nvme_Compare(tDevice* device,
                           uint64_t startingLBA,
                           uint16_t numberOfLogicalBlocks,
                           bool     limitedRetry,
                           bool     fua,
                           uint8_t  protectionInformationField,
                           uint8_t* ptrData,
                           uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType       = NVM_CMD;
    nvmCommand.cmd.nvmCmd.opcode = NVME_CMD_COMPARE;
    nvmCommand.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCommand.commandDirection  = XFER_DATA_OUT;
    nvmCommand.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCommand.ptrData           = ptrData;
    nvmCommand.dataSize          = dataLength;
    nvmCommand.device            = device;
    nvmCommand.timeout           = 15;

    // slba
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
    nvmCommand.cmd.nvmCmd.cdw12 |= C_CAST(uint32_t, protectionInformationField & 0x0F) << 26;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Compare Command\n");
    }
    ret = nvme_Cmd(device, &nvmCommand);

    // Command specific return codes:
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Compare", ret);
    }
    return ret;
}

eReturnValues nvme_Firmware_Image_Dl(tDevice* device,
                                     uint32_t bufferOffset,
                                     uint32_t numberOfBytes,
                                     uint8_t* ptrData,
                                     bool     firstSegment,
                                     bool     lastSegment,
                                     uint32_t timeoutSeconds)
{
    eReturnValues ret = SUCCESS;
    nvmeCmdCtx    ImageDl;
    safe_memset(&ImageDl, sizeof(ImageDl), 0, sizeof(ImageDl));

    ImageDl.cmd.adminCmd.opcode = NVME_ADMIN_CMD_DOWNLOAD_FW;
    ImageDl.commandType         = NVM_ADMIN_CMD;
    ImageDl.commandDirection    = XFER_DATA_OUT;
    ImageDl.cmd.adminCmd.addr   = C_CAST(uintptr_t, ptrData);
    ImageDl.ptrData             = ptrData;
    ImageDl.dataSize            = numberOfBytes;
    ImageDl.cmd.adminCmd.cdw10  = (numberOfBytes >> 2) - 1; // Since this is, 0 based, number of DWords not Bytes.
    ImageDl.cmd.adminCmd.cdw11  = bufferOffset >> 2;
    ImageDl.timeout             = timeoutSeconds;
    if (ImageDl.timeout == 0)
    {
        ImageDl.timeout = 30; // default to 30 seconds to make sure we have a long enough timeout
    }
    ImageDl.fwdlFirstSegment = firstSegment;
    ImageDl.fwdlLastSegment  = lastSegment;

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

eReturnValues nvme_Firmware_Commit(tDevice*           device,
                                   nvmeFWCommitAction commitAction,
                                   uint8_t            firmwareSlot,
                                   uint32_t           timeoutSeconds)
{
    eReturnValues ret = BAD_PARAMETER;
    nvmeCmdCtx    FirmwareCommit;

    if (firmwareSlot > 7)
    {
        return BAD_PARAMETER; // returning this for now.
    }
    safe_memset(&FirmwareCommit, sizeof(FirmwareCommit), 0, sizeof(FirmwareCommit));

    FirmwareCommit.cmd.adminCmd.opcode = NVME_ADMIN_CMD_ACTIVATE_FW;
    FirmwareCommit.commandType         = NVM_ADMIN_CMD;
    FirmwareCommit.commandDirection    = XFER_NO_DATA;
    FirmwareCommit.cmd.adminCmd.cdw10  = (C_CAST(uint32_t, commitAction) << 3); // 05:03 Bits CA
    FirmwareCommit.cmd.adminCmd.cdw10 |= (firmwareSlot & 0x07);                 // 02:00 Bits Firmware Slot
    FirmwareCommit.timeout = timeoutSeconds;
    if (FirmwareCommit.timeout == 0)
    {
        FirmwareCommit.timeout = 30; // default to 30 seconds since some images may take more time to activate
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending NVMe Firmware Commit Command\n");
    }
    ret = nvme_Cmd(device, &FirmwareCommit);

    // removed all the code checking for specific status code since it was not really in the right place.
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Firmware Commit", ret);
    }
    return ret;
}

eReturnValues nvme_Identify(tDevice* device, uint8_t* ptrData, uint32_t nvmeNamespace, uint32_t cns)
{
    nvmeCmdCtx    identify;
    eReturnValues ret = SUCCESS;
    safe_memset(&identify, sizeof(identify), 0, sizeof(identify));
    identify.cmd.adminCmd.opcode = NVME_ADMIN_CMD_IDENTIFY;
    identify.commandType         = NVM_ADMIN_CMD;
    identify.commandDirection    = XFER_DATA_IN;
    identify.cmd.adminCmd.nsid   = nvmeNamespace;
    identify.cmd.adminCmd.addr   = C_CAST(uintptr_t, ptrData);
    identify.cmd.adminCmd.cdw10  = cns;
    identify.timeout             = 15;
    identify.ptrData             = ptrData;
    identify.dataSize            = NVME_IDENTIFY_DATA_LEN;

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

eReturnValues nvme_Get_Features(tDevice* device, nvmeFeaturesCmdOpt* featCmdOpts)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    getFeatures;
    uint32_t      dWord10 = UINT32_C(0);
    safe_memset(&getFeatures, sizeof(getFeatures), 0, sizeof(getFeatures));
    getFeatures.cmd.adminCmd.opcode = NVME_ADMIN_CMD_GET_FEATURES;
    getFeatures.commandType         = NVM_ADMIN_CMD;
    getFeatures.commandDirection    = XFER_DATA_IN;
    getFeatures.cmd.adminCmd.addr   = C_CAST(uintptr_t, featCmdOpts->dataPtr);
    getFeatures.ptrData             = featCmdOpts->dataPtr;
    getFeatures.dataSize            = featCmdOpts->dataLength;
    getFeatures.cmd.adminCmd.nsid   = featCmdOpts->nsid;

    dWord10 = C_CAST(uint32_t, featCmdOpts->sel) << 8;
    dWord10 |= featCmdOpts->fid;

    getFeatures.cmd.adminCmd.cdw10 = dWord10;
    getFeatures.cmd.adminCmd.cdw11 = featCmdOpts->featSetGetValue;
    getFeatures.timeout            = 15;

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

eReturnValues nvme_Set_Features(tDevice* device, nvmeFeaturesCmdOpt* featCmdOpts)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    setFeatures;
    uint32_t      dWord10 = UINT32_C(0);
    safe_memset(&setFeatures, sizeof(setFeatures), 0, sizeof(setFeatures));
    setFeatures.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SET_FEATURES;
    setFeatures.commandType         = NVM_ADMIN_CMD;
    setFeatures.commandDirection    = XFER_DATA_OUT;
    setFeatures.cmd.adminCmd.addr   = C_CAST(uintptr_t, featCmdOpts->dataPtr);
    setFeatures.ptrData             = featCmdOpts->dataPtr;
    setFeatures.dataSize            = featCmdOpts->dataLength;

    dWord10 = C_CAST(uint32_t, featCmdOpts->sv) << 31;
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

eReturnValues nvme_Sanitize(tDevice* device,
                            bool     noDeallocateAfterSanitize,
                            bool     invertBetweenOverwritePasses,
                            uint8_t  overWritePassCount,
                            bool     allowUnrestrictedSanitizeExit,
                            uint8_t  sanitizeAction,
                            uint32_t overwritePattern)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    nvmCommand;
    safe_memset(&nvmCommand, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCommand.commandType         = NVM_ADMIN_CMD;
    nvmCommand.cmd.adminCmd.opcode = NVME_ADMIN_CMD_SANITIZE;
    nvmCommand.commandDirection    = XFER_NO_DATA;
    nvmCommand.timeout             = 15;

    // set the overwrite pass count first
    nvmCommand.cmd.adminCmd.cdw10 = C_CAST(uint32_t, overWritePassCount << 4);

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
    nvmCommand.cmd.adminCmd.cdw10 |= get_bit_range_uint8(sanitizeAction, 2, 0);
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

eReturnValues nvme_Get_Log_Page(tDevice* device, nvmeGetLogPageCmdOpts* getLogPageCmdOpts)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    getLogPage;
    uint32_t      dWord10   = UINT32_C(0);
    uint32_t      numDwords = UINT32_C(0);
    safe_memset(&getLogPage, sizeof(getLogPage), 0, sizeof(getLogPage));
    getLogPage.cmd.adminCmd.opcode = NVME_ADMIN_CMD_GET_LOG_PAGE;
    getLogPage.commandType         = NVM_ADMIN_CMD;
    getLogPage.commandDirection    = XFER_DATA_IN;
    getLogPage.cmd.adminCmd.addr   = C_CAST(uintptr_t, getLogPageCmdOpts->addr);
    getLogPage.dataSize            = getLogPageCmdOpts->dataLen;
    getLogPage.ptrData             = getLogPageCmdOpts->addr;
    getLogPage.cmd.adminCmd.nsid   = getLogPageCmdOpts->nsid;

    numDwords = (getLogPageCmdOpts->dataLen / NVME_DWORD_SIZE) - UINT32_C(1); // zero based DWORD value

    dWord10 |= getLogPageCmdOpts->lid;
    dWord10 |= (getLogPageCmdOpts->lsp & 0x0F) << 8;
    dWord10 |= (getLogPageCmdOpts->rae & 0x01) << 15;
    dWord10 |= M_Word0(numDwords) << 16;

    getLogPage.cmd.adminCmd.cdw10 = dWord10;
    getLogPage.cmd.adminCmd.cdw11 = M_Word1(numDwords);

    getLogPage.cmd.adminCmd.cdw12 =
        M_DoubleWord0(getLogPageCmdOpts->offset); //  C_CAST(uint32_t, getLogPageCmdOpts->offset & 0xFFFFFFFF);
    getLogPage.cmd.adminCmd.cdw13 =
        M_DoubleWord1(getLogPageCmdOpts->offset); // C_CAST(uint32_t, getLogPageCmdOpts->offset >> 32);

    getLogPage.ptrData  = getLogPageCmdOpts->addr;
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

eReturnValues nvme_Format(tDevice* device, nvmeFormatCmdOpts* formatCmdOpts)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    formatCmd;
    uint32_t      dWord10 = UINT32_C(0);
    // Setting this check so we are not corrupting dWord10
    //  Revise if they change the spec is revised.
    if ((formatCmdOpts->pi > 7) || (formatCmdOpts->ses > 7) || (formatCmdOpts->lbaf > 0xF))
    {
        return BAD_PARAMETER;
    }

    safe_memset(&formatCmd, sizeof(formatCmd), 0, sizeof(formatCmd));
    formatCmd.cmd.adminCmd.opcode = NVME_ADMIN_CMD_FORMAT_NVM;
    formatCmd.commandType         = NVM_ADMIN_CMD;
    formatCmd.commandDirection    = XFER_NO_DATA;
    formatCmd.cmd.adminCmd.nsid   = formatCmdOpts->nsid;

    // Construct the correct
    dWord10 = C_CAST(uint32_t, get_bit_range_uint8(formatCmdOpts->ses, 2, 0)) << 9;
    if (formatCmdOpts->pil)
    {
        dWord10 |= BIT8;
    }
    dWord10 |= C_CAST(uint32_t, get_bit_range_uint8(formatCmdOpts->pi, 2, 0)) << 5;
    if (formatCmdOpts->ms)
    {
        dWord10 |= BIT4;
    }
    dWord10 |= M_Nibble0(formatCmdOpts->lbaf); // just the nibble.

    formatCmd.cmd.adminCmd.cdw10 = dWord10;

    formatCmd.timeout = 30; // seconds

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

eReturnValues nvme_Reservation_Report(tDevice* device, bool extendedDataStructure, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    nvmCmd;
    safe_memset(&nvmCmd, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_REPORT;
    nvmCmd.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCmd.commandDirection  = XFER_DATA_IN;
    nvmCmd.commandType       = NVM_CMD;
    nvmCmd.dataSize          = dataSize;
    nvmCmd.device            = device;
    nvmCmd.ptrData           = ptrData;
    nvmCmd.timeout           = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = (dataSize >> 2) - 1; // convert bytes to a number of dwords (zeros based value)

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

eReturnValues nvme_Reservation_Register(tDevice* device,
                                        uint8_t  changePersistThroughPowerLossState,
                                        bool     ignoreExistingKey,
                                        uint8_t  reservationRegisterAction,
                                        uint8_t* ptrData,
                                        uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    nvmCmd;
    safe_memset(&nvmCmd, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_REGISTER;
    nvmCmd.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCmd.commandDirection  = XFER_DATA_OUT;
    nvmCmd.commandType       = NVM_CMD;
    nvmCmd.dataSize          = dataSize;
    nvmCmd.device            = device;
    nvmCmd.ptrData           = ptrData;
    nvmCmd.timeout           = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = C_CAST(uint32_t, changePersistThroughPowerLossState) << 30;
    if (ignoreExistingKey)
    {
        nvmCmd.cmd.nvmCmd.cdw10 |= BIT3;
    }
    nvmCmd.cmd.nvmCmd.cdw10 |= get_bit_range_uint8(reservationRegisterAction, 2, 0);

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

eReturnValues nvme_Reservation_Acquire(tDevice* device,
                                       uint8_t  reservationType,
                                       bool     ignoreExistingKey,
                                       uint8_t  reservtionAcquireAction,
                                       uint8_t* ptrData,
                                       uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    nvmCmd;
    safe_memset(&nvmCmd, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_ACQUIRE;
    nvmCmd.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCmd.commandDirection  = XFER_DATA_OUT;
    nvmCmd.commandType       = NVM_CMD;
    nvmCmd.dataSize          = dataSize;
    nvmCmd.device            = device;
    nvmCmd.ptrData           = ptrData;
    nvmCmd.timeout           = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = C_CAST(uint32_t, reservationType) << 8;
    if (ignoreExistingKey)
    {
        nvmCmd.cmd.nvmCmd.cdw10 |= BIT3;
    }
    nvmCmd.cmd.nvmCmd.cdw10 |= get_bit_range_uint8(reservtionAcquireAction, 2, 0);

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

eReturnValues nvme_Reservation_Release(tDevice* device,
                                       uint8_t  reservationType,
                                       bool     ignoreExistingKey,
                                       uint8_t  reservtionReleaseAction,
                                       uint8_t* ptrData,
                                       uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    nvmeCmdCtx    nvmCmd;
    safe_memset(&nvmCmd, sizeof(nvmeCmdCtx), 0, sizeof(nvmeCmdCtx));
    nvmCmd.cmd.nvmCmd.opcode = NVME_CMD_RESERVATION_RELEASE;
    nvmCmd.cmd.nvmCmd.nsid   = device->drive_info.namespaceID;
    nvmCmd.cmd.nvmCmd.prp1   = C_CAST(uintptr_t, ptrData);
    nvmCmd.commandDirection  = XFER_DATA_OUT;
    nvmCmd.commandType       = NVM_CMD;
    nvmCmd.dataSize          = dataSize;
    nvmCmd.device            = device;
    nvmCmd.ptrData           = ptrData;
    nvmCmd.timeout           = 15;

    nvmCmd.cmd.nvmCmd.cdw10 = C_CAST(uint32_t, reservationType) << 8;
    if (ignoreExistingKey)
    {
        nvmCmd.cmd.nvmCmd.cdw10 |= BIT3;
    }
    nvmCmd.cmd.nvmCmd.cdw10 |= get_bit_range_uint8(reservtionReleaseAction, 2, 0);

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

eReturnValues nvme_Read_Ctrl_Reg(tDevice* device, nvmeBarCtrlRegisters* ctrlRegs)
{
    eReturnValues ret = UNKNOWN;
    // For now lets first get the page aligned one & then copy the
    size_t dataSize = get_System_Pagesize();
    if (dataSize > 0 && dataSize <= UINT32_MAX)
    {
        uint8_t* barRegs = safe_calloc_aligned(dataSize, sizeof(uint8_t), dataSize);
        if (!barRegs)
        {
            return MEMORY_FAILURE;
        }
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
        {
            printf("Reading PCI Bar Registers\n");
        }
        ret = pci_Read_Bar_Reg(device, barRegs, C_CAST(uint32_t, dataSize));
        if (ret == SUCCESS)
        {
            safe_memcpy(ctrlRegs, sizeof(nvmeBarCtrlRegisters), barRegs, sizeof(nvmeBarCtrlRegisters));
        }

        safe_free_aligned(&barRegs);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("PCI Bar Registers", ret);
    }

    return ret;
}
