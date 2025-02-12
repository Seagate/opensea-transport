// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2019-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file asmedia_nvme_helper.h
// \brief Defines the functions for ASMedia NVMe-USB pass-through

// All code in this file is from a ASMedia USB to NVMe product specification for pass-through NVMe commands.
// This code should only be used on products that are known to use this pass-through interface.

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "asmedia_nvme_helper.h"
#include "scsi_helper_func.h" //for ability to send a SCSI IO

// This builds the CDB that can be used to read a log or read identify data.
// This will return OS_COMMAND_NOT_AVAILABLE if it is given a command that is unable to be sent using this pass-through
// method. NVMe passthrough command. Examples only show identify and get log page. CDB Example (get log page): 0 - E6h
// opcode 1 - NVMe opcode - get log page 2 - reserved 3 - DW10, byte 0 (bits 7:0) - log identifier 4 - Reserved 5 -
// Reserved 6 - Num DWords (MSB) 7 - Num DWords (LSB) 8 - Log page offset (MSB) 9 - Log page offset 10 - Log page offset
// 11 - Log page offset
// 12 - Log page offset
// 13 - Log page offset
// 14 - Log page offset
// 15 - Log page offset (LSB)

// CDB Example (identify):
// 0 - E6h opcode
// 1 - NVMe opcode - identify
// 2 - reserved
// 3 - DW10, byte 0 (bits 7:0) - CNS
// 4 - 15 are reserved
static eReturnValues build_Basic_Passthrough_CDB(nvmeCmdCtx* nvmCmd, uint8_t* cdb)
{
    eReturnValues ret = SUCCESS;
    if (!nvmCmd || !cdb)
    {
        return BAD_PARAMETER;
    }
    if (nvmCmd->commandType != NVM_ADMIN_CMD)
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    switch (nvmCmd->cmd.adminCmd.opcode)
    {
    case NVME_ADMIN_CMD_IDENTIFY:
        if (nvmCmd->dataSize % 4096)
        {
            return OS_COMMAND_NOT_AVAILABLE;
        }
        // Need to make sure no reserved or other newer fields are set such as CNTID, NVMSETID, or UUID Index since
        // those cannot be sent.
        if (nvmCmd->cmd.adminCmd.cdw11 > 0 || nvmCmd->cmd.adminCmd.cdw12 > 0 || nvmCmd->cmd.adminCmd.cdw13 > 0 ||
            nvmCmd->cmd.adminCmd.cdw14 > 0 || nvmCmd->cmd.adminCmd.cdw15 > 0 || nvmCmd->cmd.adminCmd.cdw2 > 0 ||
            nvmCmd->cmd.adminCmd.cdw3 > 0 || nvmCmd->cmd.adminCmd.cdw10 > UINT8_MAX)
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            // can send this command, so set it up and send it
            safe_memset(cdb, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE, 0, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE);
            cdb[OPERATION_CODE]                 = ASMEDIA_NVME_PASSTHROUGH_OP;
            cdb[ASMEDIA_NVME_PT_NVME_OP_OFFSET] = nvmCmd->cmd.adminCmd.opcode;
            cdb[2]                              = RESERVED;
            cdb[3]                              = M_Byte0(nvmCmd->cmd.adminCmd.cdw10);
            cdb[4]                              = RESERVED;
            cdb[5]                              = RESERVED;
            cdb[6]                              = RESERVED;
            cdb[7]                              = RESERVED;
            cdb[8]                              = RESERVED;
            cdb[9]                              = RESERVED;
            cdb[10]                             = RESERVED;
            cdb[11]                             = RESERVED;
            cdb[12]                             = RESERVED;
            cdb[13]                             = RESERVED;
            cdb[14]                             = RESERVED;
            cdb[15]                             = RESERVED;
        }
        break;
    case NVME_ADMIN_CMD_GET_LOG_PAGE:
        // Need to filter out fields that cannot be set in this pass-through command before proceeding
        if (nvmCmd->cmd.adminCmd.cdw11 > 0 || nvmCmd->cmd.adminCmd.cdw14 > 0 || nvmCmd->cmd.adminCmd.cdw15 > 0 ||
            nvmCmd->cmd.adminCmd.cdw2 > 0 || nvmCmd->cmd.adminCmd.cdw3 > 0 ||
            get_8bit_range_uint32(nvmCmd->cmd.adminCmd.cdw10, 15, 8) > 0)
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            // can send this command, so set it up and send it
            safe_memset(cdb, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE, 0, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE);
            cdb[OPERATION_CODE]                 = ASMEDIA_NVME_PASSTHROUGH_OP;
            cdb[ASMEDIA_NVME_PT_NVME_OP_OFFSET] = nvmCmd->cmd.adminCmd.opcode;
            cdb[2]                              = RESERVED;
            cdb[3]                              = M_Byte0(nvmCmd->cmd.adminCmd.cdw10);
            cdb[4]                              = RESERVED;
            cdb[5]                              = RESERVED;
            cdb[6]                              = M_Byte3(nvmCmd->cmd.adminCmd.cdw10);
            cdb[7]                              = M_Byte2(nvmCmd->cmd.adminCmd.cdw10);
            cdb[8]                              = M_Byte3(nvmCmd->cmd.adminCmd.cdw13);
            cdb[9]                              = M_Byte2(nvmCmd->cmd.adminCmd.cdw13);
            cdb[10]                             = M_Byte1(nvmCmd->cmd.adminCmd.cdw13);
            cdb[11]                             = M_Byte0(nvmCmd->cmd.adminCmd.cdw13);
            cdb[12]                             = M_Byte3(nvmCmd->cmd.adminCmd.cdw12);
            cdb[13]                             = M_Byte2(nvmCmd->cmd.adminCmd.cdw12);
            cdb[14]                             = M_Byte1(nvmCmd->cmd.adminCmd.cdw12);
            cdb[15]                             = M_Byte0(nvmCmd->cmd.adminCmd.cdw12);
        }
        break;
    default:
        ret = OS_COMMAND_NOT_AVAILABLE;
        break;
    }
    return ret;
}

eReturnValues send_ASMedia_Basic_NVMe_Passthrough_Cmd(nvmeCmdCtx* nvmCmd)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE);
    ret = build_Basic_Passthrough_CDB(nvmCmd, cdb);
    if (ret == SUCCESS)
    {
        ret = scsi_Send_Cdb(nvmCmd->device, cdb, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE, nvmCmd->ptrData, nvmCmd->dataSize,
                            XFER_DATA_IN, M_NULLPTR, 0, 15);
        nvmCmd->commandCompletionData.dw0Valid = false;
        nvmCmd->commandCompletionData.dw1Valid = false;
        nvmCmd->commandCompletionData.dw2Valid = false;
        nvmCmd->commandCompletionData.dw3Valid = false;
    }
    return ret;
}

static eReturnValues build_ASMedia_Packet_Command_CDB(uint8_t*                 cdb,
                                                      eDataTransferDirection*  cdbDataDirection,
                                                      eASM_NVMPacket_Operation asmOperation,
                                                      uint8_t                  parameter1,
                                                      nvmeCmdCtx*              nvmCmd,
                                                      uint8_t*                 dataPtr,
                                                      uint32_t                 dataSize)
{
    eReturnValues ret = SUCCESS;

    if (!cdb || !cdbDataDirection)
    {
        return BAD_PARAMETER;
    }

    safe_memset(cdb, ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE, 0, ASMEDIA_NVME_PACKET_CDB_SIZE);

    // Before validating other parameters, need to know which operation is being requested.
    switch (asmOperation)
    {
    case ASMEDIA_NVMP_OP_POWER_DOWN_NVME:
        /* Fall-through */
    case ASMEDIA_NVMP_OP_RESET_BRIDGE:
        /* Fall-through */
    case ASMEDIA_NVMP_OP_CONTROL_LED:
        /* Fall-through */
    case ASMEDIA_NVMP_OP_RELINK_USB:
        cdb[OPERATION_CODE]                         = ASMEDIA_NVME_PACKET_WRITE_OP;
        cdb[1]                                      = ASMEDIA_NVME_PACKET_SIGNATURE;
        cdb[ASMEDIA_NVME_PACKET_OPERATION_OFFSET]   = C_CAST(uint8_t, asmOperation);
        cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = parameter1;
        // cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = parameter2;//parameter 2 is unused for these operations
        *cdbDataDirection = XFER_NO_DATA;
        break;
    case ASMEDIA_NVMP_OP_GET_BRIDGE_INFO:
        cdb[OPERATION_CODE]                         = ASMEDIA_NVME_PACKET_READ_OP;
        cdb[1]                                      = ASMEDIA_NVME_PACKET_SIGNATURE;
        cdb[ASMEDIA_NVME_PACKET_OPERATION_OFFSET]   = C_CAST(uint8_t, asmOperation);
        cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = parameter1;
        // cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = parameter2;//parameter 2 is unused for this operation
        // set allocation length to 40h
        cdb[10]           = M_Byte3(0x40);
        cdb[11]           = M_Byte2(0x40);
        cdb[12]           = M_Byte1(0x40);
        cdb[13]           = M_Byte0(0x40);
        *cdbDataDirection = XFER_DATA_IN;
        break;
    case ASMEDIA_NVMP_OP_SEND_ADMIN_IO_NVM_CMD:
        if (!nvmCmd || !dataPtr || dataSize < 64)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            cdb[OPERATION_CODE]                       = ASMEDIA_NVME_PACKET_WRITE_OP;
            cdb[1]                                    = ASMEDIA_NVME_PACKET_SIGNATURE;
            cdb[ASMEDIA_NVME_PACKET_OPERATION_OFFSET] = C_CAST(uint8_t, asmOperation);
            // ignore input parameter 1 value as we can look at the nvmCmd structure to set it properly for this command
            // setup transfer length as 64B since that's the size of the command in NVMe and that is what the spec shows
            // it is looking for.
            cdb[10] = M_Byte3(ASM_NVMP_DWORDS_DATA_PACKET_SIZE);
            cdb[11] = M_Byte2(ASM_NVMP_DWORDS_DATA_PACKET_SIZE);
            cdb[12] = M_Byte1(ASM_NVMP_DWORDS_DATA_PACKET_SIZE);
            cdb[13] = M_Byte0(ASM_NVMP_DWORDS_DATA_PACKET_SIZE);

            // set param 1 for command type
            if (nvmCmd->commandType == NVM_ADMIN_CMD)
            {
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = ASM_NVMP_SEND_CMD_ADMIN;
            }
            else
            {
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = ASM_NVMP_SEND_CMD_IO;
            }

            // set param 2 for data direction
            switch (nvmCmd->commandDirection)
            {
            case XFER_DATA_IN:
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_DATA_IN;
                if (nvmCmd->dataSize == 0)
                {
                    cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
                }
                break;
            case XFER_DATA_OUT:
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_DATA_OUT;
                if (nvmCmd->dataSize == 0)
                {
                    cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
                }
                break;
            case XFER_NO_DATA:
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
                break;
            default:
                return OS_COMMAND_NOT_AVAILABLE;
            }

            *cdbDataDirection = XFER_DATA_OUT;

            // finally, setup the data buffer with the NVM command DWORDS for the USB adapter to send to the device.
            // setup each dword in the buffer
            safe_memset(dataPtr, dataSize, 0, dataSize);
            if (nvmCmd->commandType == NVM_ADMIN_CMD)
            {
                // CDW0
                dataPtr[0] = nvmCmd->cmd.adminCmd.opcode;
                // NOTE: bytes 1, 2, 3 hold fused bits, prp vs sgl, and CID. None of these are filled in for now...-TJE
                // NSID
                dataPtr[4] = M_Byte0(nvmCmd->cmd.adminCmd.nsid);
                dataPtr[5] = M_Byte1(nvmCmd->cmd.adminCmd.nsid);
                dataPtr[6] = M_Byte2(nvmCmd->cmd.adminCmd.nsid);
                dataPtr[7] = M_Byte3(nvmCmd->cmd.adminCmd.nsid);
                // CDW2
                dataPtr[8]  = M_Byte0(nvmCmd->cmd.adminCmd.cdw2);
                dataPtr[9]  = M_Byte1(nvmCmd->cmd.adminCmd.cdw2);
                dataPtr[10] = M_Byte2(nvmCmd->cmd.adminCmd.cdw2);
                dataPtr[11] = M_Byte3(nvmCmd->cmd.adminCmd.cdw2);
                // CDW3
                dataPtr[12] = M_Byte0(nvmCmd->cmd.adminCmd.cdw3);
                dataPtr[13] = M_Byte1(nvmCmd->cmd.adminCmd.cdw3);
                dataPtr[14] = M_Byte2(nvmCmd->cmd.adminCmd.cdw3);
                dataPtr[15] = M_Byte3(nvmCmd->cmd.adminCmd.cdw3);
                // metadata ptr is reserved!
                // data ptr is not needed as it will be changed by the USB adapter
                // CDW10
                dataPtr[40] = M_Byte0(nvmCmd->cmd.adminCmd.cdw10);
                dataPtr[41] = M_Byte1(nvmCmd->cmd.adminCmd.cdw10);
                dataPtr[42] = M_Byte2(nvmCmd->cmd.adminCmd.cdw10);
                dataPtr[43] = M_Byte3(nvmCmd->cmd.adminCmd.cdw10);
                // CDW11
                dataPtr[44] = M_Byte0(nvmCmd->cmd.adminCmd.cdw11);
                dataPtr[45] = M_Byte1(nvmCmd->cmd.adminCmd.cdw11);
                dataPtr[46] = M_Byte2(nvmCmd->cmd.adminCmd.cdw11);
                dataPtr[47] = M_Byte3(nvmCmd->cmd.adminCmd.cdw11);
                // CDW12
                dataPtr[48] = M_Byte0(nvmCmd->cmd.adminCmd.cdw12);
                dataPtr[49] = M_Byte1(nvmCmd->cmd.adminCmd.cdw12);
                dataPtr[50] = M_Byte2(nvmCmd->cmd.adminCmd.cdw12);
                dataPtr[51] = M_Byte3(nvmCmd->cmd.adminCmd.cdw12);
                // CDW13
                dataPtr[52] = M_Byte0(nvmCmd->cmd.adminCmd.cdw13);
                dataPtr[53] = M_Byte1(nvmCmd->cmd.adminCmd.cdw13);
                dataPtr[54] = M_Byte2(nvmCmd->cmd.adminCmd.cdw13);
                dataPtr[55] = M_Byte3(nvmCmd->cmd.adminCmd.cdw13);
                // CDW14
                dataPtr[56] = M_Byte0(nvmCmd->cmd.adminCmd.cdw14);
                dataPtr[57] = M_Byte1(nvmCmd->cmd.adminCmd.cdw14);
                dataPtr[58] = M_Byte2(nvmCmd->cmd.adminCmd.cdw14);
                dataPtr[59] = M_Byte3(nvmCmd->cmd.adminCmd.cdw14);
                // CDW15
                dataPtr[60] = M_Byte0(nvmCmd->cmd.adminCmd.cdw15);
                dataPtr[61] = M_Byte1(nvmCmd->cmd.adminCmd.cdw15);
                dataPtr[62] = M_Byte2(nvmCmd->cmd.adminCmd.cdw15);
                dataPtr[63] = M_Byte3(nvmCmd->cmd.adminCmd.cdw15);
            }
            else
            {
                // CDW0
                dataPtr[0] = nvmCmd->cmd.nvmCmd.opcode;
                // NOTE: bytes 1, 2, 3 hold fused bits, prp vs sgl, and CID. None of these are filled in for now...-TJE
                // NSID
                dataPtr[4] = M_Byte0(nvmCmd->cmd.nvmCmd.nsid);
                dataPtr[5] = M_Byte1(nvmCmd->cmd.nvmCmd.nsid);
                dataPtr[6] = M_Byte2(nvmCmd->cmd.nvmCmd.nsid);
                dataPtr[7] = M_Byte3(nvmCmd->cmd.nvmCmd.nsid);
                // CDW2
                dataPtr[8]  = M_Byte0(nvmCmd->cmd.nvmCmd.cdw2);
                dataPtr[9]  = M_Byte1(nvmCmd->cmd.nvmCmd.cdw2);
                dataPtr[10] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw2);
                dataPtr[11] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw2);
                // CDW3
                dataPtr[12] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw3);
                dataPtr[13] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw3);
                dataPtr[14] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw3);
                dataPtr[15] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw3);
                // metadata ptr is reserved!
                // data ptr is not needed as it will be changed by the USB adapter
                // CDW10
                dataPtr[40] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw10);
                dataPtr[41] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw10);
                dataPtr[42] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw10);
                dataPtr[43] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw10);
                // CDW11
                dataPtr[44] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw11);
                dataPtr[45] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw11);
                dataPtr[46] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw11);
                dataPtr[47] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw11);
                // CDW12
                dataPtr[48] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw12);
                dataPtr[49] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw12);
                dataPtr[50] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw12);
                dataPtr[51] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw12);
                // CDW13
                dataPtr[52] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw13);
                dataPtr[53] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw13);
                dataPtr[54] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw13);
                dataPtr[55] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw13);
                // CDW14
                dataPtr[56] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw14);
                dataPtr[57] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw14);
                dataPtr[58] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw14);
                dataPtr[59] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw14);
                // CDW15
                dataPtr[60] = M_Byte0(nvmCmd->cmd.nvmCmd.cdw15);
                dataPtr[61] = M_Byte1(nvmCmd->cmd.nvmCmd.cdw15);
                dataPtr[62] = M_Byte2(nvmCmd->cmd.nvmCmd.cdw15);
                dataPtr[63] = M_Byte3(nvmCmd->cmd.nvmCmd.cdw15);
            }
        }
        break;
    case ASMEDIA_NVMP_OP_DATA_PHASE:
    {
        uint32_t allocationLength                 = dataSize;
        cdb[OPERATION_CODE]                       = ASMEDIA_NVME_PACKET_READ_OP;
        cdb[1]                                    = ASMEDIA_NVME_PACKET_SIGNATURE;
        cdb[ASMEDIA_NVME_PACKET_OPERATION_OFFSET] = C_CAST(uint8_t, asmOperation);

        // set param 1 for command type
        if (nvmCmd->commandType == NVM_ADMIN_CMD)
        {
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = ASM_NVMP_SEND_CMD_ADMIN;
        }
        else
        {
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = ASM_NVMP_SEND_CMD_IO;
        }

        // set param 2 for data direction
        switch (nvmCmd->commandDirection)
        {
        case XFER_DATA_IN:
            *cdbDataDirection                           = XFER_DATA_IN;
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_DATA_IN;
            if (nvmCmd->dataSize == 0)
            {
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
                allocationLength                            = 0;
                *cdbDataDirection                           = XFER_NO_DATA;
            }
            break;
        case XFER_DATA_OUT:
            cdb[OPERATION_CODE] =
                ASMEDIA_NVME_PACKET_WRITE_OP; // change to a write opcode if sending data to the device.
            *cdbDataDirection                           = XFER_DATA_OUT;
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_DATA_OUT;
            if (nvmCmd->dataSize == 0)
            {
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
                allocationLength                            = 0;
                *cdbDataDirection                           = XFER_NO_DATA;
            }
            break;
        case XFER_NO_DATA:
            *cdbDataDirection                           = XFER_NO_DATA;
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
            allocationLength                            = 0;
            break;
        default:
            return OS_COMMAND_NOT_AVAILABLE;
        }

        // Transfers should be 512B aligned
        cdb[10] = M_Byte3(allocationLength);
        cdb[11] = M_Byte2(allocationLength);
        cdb[12] = M_Byte1(allocationLength);
        cdb[13] = M_Byte0(allocationLength);
    }
    break;
    case ASMEDIA_NVMP_OP_GET_NVM_COMPLETION:
        cdb[OPERATION_CODE]                       = ASMEDIA_NVME_PACKET_READ_OP;
        cdb[1]                                    = ASMEDIA_NVME_PACKET_SIGNATURE;
        cdb[ASMEDIA_NVME_PACKET_OPERATION_OFFSET] = C_CAST(uint8_t, asmOperation);

        // set allocation length to 10h
        cdb[10] = M_Byte3(ASM_NVMP_RESPONSE_DATA_SIZE);
        cdb[11] = M_Byte2(ASM_NVMP_RESPONSE_DATA_SIZE);
        cdb[12] = M_Byte1(ASM_NVMP_RESPONSE_DATA_SIZE);
        cdb[13] = M_Byte0(ASM_NVMP_RESPONSE_DATA_SIZE);

        // set param 1 for command type
        if (nvmCmd->commandType == NVM_ADMIN_CMD)
        {
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = ASM_NVMP_SEND_CMD_ADMIN;
        }
        else
        {
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET] = ASM_NVMP_SEND_CMD_IO;
        }

        // set param 2 for data direction
        switch (nvmCmd->commandDirection)
        {
        case XFER_DATA_IN:
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_DATA_IN;
            if (nvmCmd->dataSize == 0)
            {
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
            }
            break;
        case XFER_DATA_OUT:
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_DATA_OUT;
            if (nvmCmd->dataSize == 0)
            {
                cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
            }
            break;
        case XFER_NO_DATA:
            cdb[ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET] = ASM_NVMP_NON_DATA;
            break;
        default:
            return OS_COMMAND_NOT_AVAILABLE;
        }
        *cdbDataDirection = XFER_DATA_IN;
        break;
    default:
        ret = BAD_PARAMETER;
        break;
    }

    return ret;
}

// NOTE: There is currently a bug in this code on the data phase command being rejected for invalid field in CDB.
//       This will debugged once I get a device in hand to figure out what is going wrong.
eReturnValues send_ASM_NVMe_Cmd(nvmeCmdCtx* nvmCmd)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, asmCDB, ASMEDIA_NVME_PACKET_CDB_SIZE);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, asmPayload, ASM_NVMP_DWORDS_DATA_PACKET_SIZE);
    eDataTransferDirection asmCDBDir = 0;
    // if the NVMe command is not doing a multiple of 512B data transfer, we need to allocate local memory, rounded up
    // to 512B boundaries before the command. Then we can copy that back to the smaller buffer after command is
    // complete.
    uint8_t* dataPhasePtr  = M_NULLPTR;
    uint32_t dataPhaseSize = UINT32_C(0);
    bool     localMemory   = false;
    DISABLE_NONNULL_COMPARE
    if (nvmCmd == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    dataPhasePtr  = nvmCmd->ptrData;
    dataPhaseSize = nvmCmd->dataSize;
    if (nvmCmd->ptrData && nvmCmd->dataSize > 0 && nvmCmd->dataSize % UINT32_C(512))
    {
        dataPhaseSize = uint32_round_up_power2(nvmCmd->dataSize, UINT32_C(512));
        dataPhasePtr  = C_CAST(
            uint8_t*, safe_calloc_aligned(dataPhaseSize, sizeof(uint8_t), nvmCmd->device->os_info.minimumAlignment));
        if (dataPhasePtr == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        // if a data-out command, need to copy what is intended to go to the device to the new buffer
        if (nvmCmd->ptrData && nvmCmd->commandDirection == XFER_DATA_OUT && nvmCmd->dataSize > 0)
        {
            safe_memcpy(dataPhasePtr, dataPhaseSize, nvmCmd->ptrData, nvmCmd->dataSize);
        }
        localMemory = true;
    }

    // 1. Build and send CDB/data for the pass-through packet command
    ret = build_ASMedia_Packet_Command_CDB(asmCDB, &asmCDBDir, ASMEDIA_NVMP_OP_SEND_ADMIN_IO_NVM_CMD, 0, nvmCmd,
                                           asmPayload, ASM_NVMP_DWORDS_DATA_PACKET_SIZE);
    if (SUCCESS != ret)
    {
        if (localMemory)
        {
            safe_free_aligned(&dataPhasePtr);
        }
        return ret;
    }
    ret = scsi_Send_Cdb(nvmCmd->device, asmCDB, ASMEDIA_NVME_PACKET_CDB_SIZE, asmPayload,
                        ASM_NVMP_DWORDS_DATA_PACKET_SIZE, asmCDBDir, M_NULLPTR, 0, 15);
    if (SUCCESS != ret)
    {
        if (localMemory)
        {
            safe_free_aligned(&dataPhasePtr);
        }
        return ret;
    }

    // 2. Perform the data phase (this triggers the command to be sent)
    ret = build_ASMedia_Packet_Command_CDB(asmCDB, &asmCDBDir, ASMEDIA_NVMP_OP_DATA_PHASE, 0, nvmCmd, dataPhasePtr,
                                           dataPhaseSize);
    if (SUCCESS != ret)
    {
        if (localMemory)
        {
            safe_free_aligned(&dataPhasePtr);
        }
        return ret;
    }
    eReturnValues sendRet = scsi_Send_Cdb(nvmCmd->device, asmCDB, ASMEDIA_NVME_PACKET_CDB_SIZE, dataPhasePtr,
                                          dataPhaseSize, asmCDBDir, M_NULLPTR, 0, 15);

    if (localMemory)
    {
        // copy back to original smaller buffer from the oversized padded buffer if read command
        if (nvmCmd->ptrData && nvmCmd->commandDirection == XFER_DATA_IN && nvmCmd->dataSize > 0)
        {
            safe_memcpy(nvmCmd->ptrData, nvmCmd->dataSize, dataPhasePtr, nvmCmd->dataSize);
        }
        safe_free_aligned(&dataPhasePtr);
    }

    bool senseDataIsAllWeGot = true;
    if (sendRet != OS_COMMAND_TIMEOUT)
    {
        // 3. get the command completion
        DECLARE_ZERO_INIT_ARRAY(uint8_t, completionData, ASM_NVMP_RESPONSE_DATA_SIZE);
        ret = build_ASMedia_Packet_Command_CDB(asmCDB, &asmCDBDir, ASMEDIA_NVMP_OP_GET_NVM_COMPLETION, 0, nvmCmd,
                                               M_NULLPTR, 0);
        if (SUCCESS == scsi_Send_Cdb(nvmCmd->device, asmCDB, ASMEDIA_NVME_PACKET_CDB_SIZE, completionData,
                                     ASM_NVMP_RESPONSE_DATA_SIZE, asmCDBDir, M_NULLPTR, 0, 15))
        {
            // check for invalid entry by looking for 0xFF at bytes 14 and 15 of returned data
            if (completionData[14] == UINT8_MAX && completionData[15] == UINT8_MAX)
            {
                senseDataIsAllWeGot = true;
            }
            else
            {
                senseDataIsAllWeGot = false;
                // convert to what needs to be passed back up the stack
                nvmCmd->commandCompletionData.commandSpecific =
                    M_BytesTo4ByteValue(completionData[3], completionData[2], completionData[1], completionData[0]);
                nvmCmd->commandCompletionData.dw1Reserved =
                    M_BytesTo4ByteValue(completionData[7], completionData[6], completionData[5], completionData[4]);
                nvmCmd->commandCompletionData.sqIDandHeadPtr =
                    M_BytesTo4ByteValue(completionData[11], completionData[10], completionData[9], completionData[8]);
                nvmCmd->commandCompletionData.statusAndCID =
                    M_BytesTo4ByteValue(completionData[15], completionData[14], completionData[13], completionData[12]);
                nvmCmd->commandCompletionData.dw0Valid = true;
                nvmCmd->commandCompletionData.dw1Valid = true;
                nvmCmd->commandCompletionData.dw2Valid = true;
                nvmCmd->commandCompletionData.dw3Valid = true;
            }
        }
    }
    if (senseDataIsAllWeGot)
    {
        nvmCmd->commandCompletionData.dw0Valid        = false;
        nvmCmd->commandCompletionData.dw1Valid        = false;
        nvmCmd->commandCompletionData.dw2Valid        = false;
        nvmCmd->commandCompletionData.dw3Valid        = false;
        nvmCmd->commandCompletionData.sqIDandHeadPtr  = 0;
        nvmCmd->commandCompletionData.commandSpecific = 0;
        nvmCmd->commandCompletionData.dw1Reserved     = 0;
        nvmCmd->commandCompletionData.statusAndCID    = 0;
        // Didn't get NVMe response, so no judgments beyond sense data translation can be made.
        ret = sendRet;
    }
    return ret;
}

static eReturnValues asm_nvme_Shutdown(tDevice* device, bool withShutdownProcessing)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, ASMEDIA_NVME_PACKET_CDB_SIZE);
    eDataTransferDirection asmCDBDir = XFER_NO_DATA;
    eReturnValues          ret = build_ASMedia_Packet_Command_CDB(&cdb[0], &asmCDBDir, ASMEDIA_NVMP_OP_POWER_DOWN_NVME,
                                                         withShutdownProcessing ? 1 : 0, M_NULLPTR, M_NULLPTR, 0);
    if (ret == SUCCESS)
    {
        // send it
        ret = scsi_Send_Cdb(device, cdb, ASMEDIA_NVME_PACKET_CDB_SIZE, M_NULLPTR, 0, asmCDBDir, M_NULLPTR, 0, 15);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

static eReturnValues asm_nvme_Reset_Bridge(tDevice* device)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, ASMEDIA_NVME_PACKET_CDB_SIZE);
    eDataTransferDirection asmCDBDir = XFER_NO_DATA;
    eReturnValues          ret =
        build_ASMedia_Packet_Command_CDB(&cdb[0], &asmCDBDir, ASMEDIA_NVMP_OP_RESET_BRIDGE, 0, M_NULLPTR, M_NULLPTR, 0);
    if (ret == SUCCESS)
    {
        // send it
        ret = scsi_Send_Cdb(device, cdb, ASMEDIA_NVME_PACKET_CDB_SIZE, M_NULLPTR, 0, asmCDBDir, M_NULLPTR, 0, 15);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

static eReturnValues asm_nvme_Relink_Bridge(tDevice* device, bool normalShutdownBeforeDisconnect)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, ASMEDIA_NVME_PACKET_CDB_SIZE);
    eDataTransferDirection asmCDBDir = XFER_NO_DATA;
    eReturnValues          ret =
        build_ASMedia_Packet_Command_CDB(&cdb[0], &asmCDBDir, ASMEDIA_NVMP_OP_RELINK_USB,
                                         normalShutdownBeforeDisconnect ? 1 : 0, M_NULLPTR, M_NULLPTR, 0);
    if (ret == SUCCESS)
    {
        // send it
        ret = scsi_Send_Cdb(device, cdb, ASMEDIA_NVME_PACKET_CDB_SIZE, M_NULLPTR, 0, asmCDBDir, M_NULLPTR, 0, 15);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

eReturnValues asm_nvme_Reset(tDevice* device)
{
    // shutdown, then reset bridge
    if (SUCCESS == asm_nvme_Shutdown(device, true))
    {
        if (SUCCESS == asm_nvme_Reset_Bridge(device))
        {
            return SUCCESS;
        }
        else
        {
            // something went wrong...try relink before failing
            if (SUCCESS == asm_nvme_Relink_Bridge(device, false))
            {
                return SUCCESS;
            }
            else
            {
                return FAILURE;
            }
        }
    }
    else
    {
        return NOT_SUPPORTED;
    }
}

eReturnValues asm_nvme_Subsystem_Reset(tDevice* device)
{
    // relink USB command
    return asm_nvme_Relink_Bridge(device, true);
}
