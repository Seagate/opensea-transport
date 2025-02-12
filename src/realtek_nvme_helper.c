//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file realtek_nvme_helper.h
// \brief Defines the functions for Realtek NVMe-USB pass-through

// All code in this file is from a Realtek USB to NVMe product specification for pass-through nvme commands.
// This code should only be used on products that are known to use this pass-through interface.

#include "realtek_nvme_helper.h"
#include "scsi_helper_func.h" //for ability to send a SCSI IO

eReturnValues build_Realtek_NVMe_CDB_And_Payload(uint8_t*                cdb,
                                                 eDataTransferDirection* cdbDataDirection,
                                                 uint8_t*                dataPtr,
                                                 uint32_t                dataSize,
                                                 eRealtekNVMCMDPhase     phase,
                                                 nvmeCmdCtx*             nvmCmd)
{
    DISABLE_NONNULL_COMPARE
    if (cdb == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    safe_memset(cdb, REALTEK_NVME_CDB_SIZE, 0, REALTEK_NVME_CDB_SIZE);

    switch (phase)
    {
    case REALTEK_PHASE_COMMAND:
        *cdbDataDirection = XFER_DATA_OUT;
        if (dataPtr == M_NULLPTR || dataSize < REALTEK_NVME_CMD_PAYLOAD_LEN || nvmCmd == M_NULLPTR)
        {
            return BAD_PARAMETER;
        }
        else
        {
            safe_memset(dataPtr, REALTEK_NVME_CMD_PAYLOAD_LEN, 0, REALTEK_NVME_CMD_PAYLOAD_LEN);
            cdb[OPERATION_CODE] = REALTEK_NVME_PT_OPCODE_OUT;
            // set length to 0x40 - aka 64 bytes
            cdb[1] = M_Byte0(REALTEK_NVME_CMD_PAYLOAD_LEN); // length
            cdb[2] = M_Byte1(REALTEK_NVME_CMD_PAYLOAD_LEN); // length
            cdb[3] = C_CAST(uint8_t, REALTEK_PHASE_COMMAND);
            if (nvmCmd->commandType == NVM_ADMIN_CMD)
            {
                // set the cmd set field cdb 4-7
                cdb[4] = M_Byte0(REALTEK_NVME_ADMIN_CMD);
                cdb[5] = M_Byte1(REALTEK_NVME_ADMIN_CMD);
                cdb[6] = M_Byte2(REALTEK_NVME_ADMIN_CMD);
                cdb[7] = M_Byte3(REALTEK_NVME_ADMIN_CMD);
            }
            else
            {
                // set the cmd set field cdb 4-7
                cdb[4] = M_Byte0(REALTEK_NVME_IO_CMD);
                cdb[5] = M_Byte1(REALTEK_NVME_IO_CMD);
                cdb[6] = M_Byte2(REALTEK_NVME_IO_CMD);
                cdb[7] = M_Byte3(REALTEK_NVME_IO_CMD);
            }
            // Now setup the remaining command fields.
            // CDW0 is bytes 3:0
            dataPtr[0] = M_Byte0(nvmCmd->cmd.dwords.cdw0);
            dataPtr[1] = M_Byte1(nvmCmd->cmd.dwords.cdw0);
            dataPtr[2] = M_Byte2(nvmCmd->cmd.dwords.cdw0);
            dataPtr[3] = M_Byte3(nvmCmd->cmd.dwords.cdw0);
            // NSID is 7:4
            dataPtr[4] = M_Byte0(nvmCmd->cmd.dwords.cdw1);
            dataPtr[5] = M_Byte1(nvmCmd->cmd.dwords.cdw1);
            dataPtr[6] = M_Byte2(nvmCmd->cmd.dwords.cdw1);
            dataPtr[7] = M_Byte3(nvmCmd->cmd.dwords.cdw1);
            // CDW2 11:8
            dataPtr[8]  = M_Byte0(nvmCmd->cmd.dwords.cdw2);
            dataPtr[9]  = M_Byte1(nvmCmd->cmd.dwords.cdw2);
            dataPtr[10] = M_Byte2(nvmCmd->cmd.dwords.cdw2);
            dataPtr[11] = M_Byte3(nvmCmd->cmd.dwords.cdw2);
            // CDW3 15:12
            dataPtr[12] = M_Byte0(nvmCmd->cmd.dwords.cdw3);
            dataPtr[13] = M_Byte1(nvmCmd->cmd.dwords.cdw3);
            dataPtr[14] = M_Byte2(nvmCmd->cmd.dwords.cdw3);
            dataPtr[15] = M_Byte3(nvmCmd->cmd.dwords.cdw3);
            // metadata ptr is
            dataPtr[16] = M_Byte0(nvmCmd->cmd.dwords.cdw4);
            dataPtr[17] = M_Byte1(nvmCmd->cmd.dwords.cdw4);
            dataPtr[18] = M_Byte2(nvmCmd->cmd.dwords.cdw4);
            dataPtr[19] = M_Byte3(nvmCmd->cmd.dwords.cdw4);
            dataPtr[20] = M_Byte0(nvmCmd->cmd.dwords.cdw5);
            dataPtr[21] = M_Byte1(nvmCmd->cmd.dwords.cdw5);
            dataPtr[22] = M_Byte2(nvmCmd->cmd.dwords.cdw5);
            dataPtr[23] = M_Byte3(nvmCmd->cmd.dwords.cdw5);
            // data ptr is
            dataPtr[24] = M_Byte0(nvmCmd->cmd.dwords.cdw6);
            dataPtr[25] = M_Byte1(nvmCmd->cmd.dwords.cdw6);
            dataPtr[26] = M_Byte2(nvmCmd->cmd.dwords.cdw6);
            dataPtr[27] = M_Byte3(nvmCmd->cmd.dwords.cdw6);
            dataPtr[28] = M_Byte0(nvmCmd->cmd.dwords.cdw7);
            dataPtr[29] = M_Byte1(nvmCmd->cmd.dwords.cdw7);
            dataPtr[30] = M_Byte2(nvmCmd->cmd.dwords.cdw7);
            dataPtr[31] = M_Byte3(nvmCmd->cmd.dwords.cdw7);
            dataPtr[32] = M_Byte0(nvmCmd->cmd.dwords.cdw8);
            dataPtr[33] = M_Byte1(nvmCmd->cmd.dwords.cdw8);
            dataPtr[34] = M_Byte2(nvmCmd->cmd.dwords.cdw8);
            dataPtr[35] = M_Byte3(nvmCmd->cmd.dwords.cdw8);
            dataPtr[36] = M_Byte0(nvmCmd->cmd.dwords.cdw9);
            dataPtr[37] = M_Byte1(nvmCmd->cmd.dwords.cdw9);
            dataPtr[38] = M_Byte2(nvmCmd->cmd.dwords.cdw9);
            dataPtr[39] = M_Byte3(nvmCmd->cmd.dwords.cdw9);
            // CDW10 is 43:40
            dataPtr[40] = M_Byte0(nvmCmd->cmd.dwords.cdw10);
            dataPtr[41] = M_Byte1(nvmCmd->cmd.dwords.cdw10);
            dataPtr[42] = M_Byte2(nvmCmd->cmd.dwords.cdw10);
            dataPtr[43] = M_Byte3(nvmCmd->cmd.dwords.cdw10);
            // CDW11 is 47:44
            dataPtr[44] = M_Byte0(nvmCmd->cmd.dwords.cdw11);
            dataPtr[45] = M_Byte1(nvmCmd->cmd.dwords.cdw11);
            dataPtr[46] = M_Byte2(nvmCmd->cmd.dwords.cdw11);
            dataPtr[47] = M_Byte3(nvmCmd->cmd.dwords.cdw11);
            // CDW12 is 51:48
            dataPtr[48] = M_Byte0(nvmCmd->cmd.dwords.cdw12);
            dataPtr[49] = M_Byte1(nvmCmd->cmd.dwords.cdw12);
            dataPtr[50] = M_Byte2(nvmCmd->cmd.dwords.cdw12);
            dataPtr[51] = M_Byte3(nvmCmd->cmd.dwords.cdw12);
            // CDW13 is 55:52
            dataPtr[52] = M_Byte0(nvmCmd->cmd.dwords.cdw13);
            dataPtr[53] = M_Byte1(nvmCmd->cmd.dwords.cdw13);
            dataPtr[54] = M_Byte2(nvmCmd->cmd.dwords.cdw13);
            dataPtr[55] = M_Byte3(nvmCmd->cmd.dwords.cdw13);
            // CDW14 is 59:56
            dataPtr[56] = M_Byte0(nvmCmd->cmd.dwords.cdw14);
            dataPtr[57] = M_Byte1(nvmCmd->cmd.dwords.cdw14);
            dataPtr[58] = M_Byte2(nvmCmd->cmd.dwords.cdw14);
            dataPtr[59] = M_Byte3(nvmCmd->cmd.dwords.cdw14);
            // CDW15 is 63:60
            dataPtr[60] = M_Byte0(nvmCmd->cmd.dwords.cdw15);
            dataPtr[61] = M_Byte1(nvmCmd->cmd.dwords.cdw15);
            dataPtr[62] = M_Byte2(nvmCmd->cmd.dwords.cdw15);
            dataPtr[63] = M_Byte3(nvmCmd->cmd.dwords.cdw15);
        }
        break;
    case REALTEK_PHASE_DATA:
        if (nvmCmd == M_NULLPTR)
        {
            return BAD_PARAMETER;
        }
        // read uses in, write and non-data use out
        if (nvmCmd->commandDirection == XFER_DATA_IN)
        {
            cdb[OPERATION_CODE] = REALTEK_NVME_PT_OPCODE_IN;
            *cdbDataDirection   = XFER_DATA_IN;
            cdb[6]              = M_Byte0(REALTEK_DATA_IN);
            cdb[7]              = M_Byte1(REALTEK_DATA_IN);
        }
        else
        {
            cdb[OPERATION_CODE] = REALTEK_NVME_PT_OPCODE_OUT;
            *cdbDataDirection   = XFER_DATA_OUT;
            cdb[6]              = M_Byte0(REALTEK_DATA_OUT);
            cdb[7]              = M_Byte1(REALTEK_DATA_OUT);
        }
        // length of transfer can be zero to 256KiB
        cdb[1] = M_Byte0(nvmCmd->dataSize); // length
        cdb[2] = M_Byte1(nvmCmd->dataSize); // length
        cdb[4] = M_Byte2(nvmCmd->dataSize); // length
        cdb[5] = M_Byte3(nvmCmd->dataSize); // length
        if (nvmCmd->dataSize == UINT32_C(0))
        {
            *cdbDataDirection = XFER_NO_DATA;
            cdb[6]            = M_Byte0(REALTEK_NO_DATA);
            cdb[7]            = M_Byte1(REALTEK_NO_DATA);
        }
        cdb[3] = C_CAST(uint8_t, REALTEK_PHASE_DATA);
        break;
    case REALTEK_PHASE_COMPLETION:
        *cdbDataDirection   = XFER_DATA_IN;
        cdb[OPERATION_CODE] = REALTEK_NVME_PT_OPCODE_IN;
        // length must be 16 bytes
        cdb[1] = M_Byte0(REALTEK_NVME_COMPLETION_PAYLOAD_LEN);
        cdb[2] = M_Byte1(REALTEK_NVME_COMPLETION_PAYLOAD_LEN);
        cdb[3] = C_CAST(uint8_t, REALTEK_PHASE_COMPLETION);
        break;
    default:
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    return SUCCESS;
}

eReturnValues send_Realtek_NVMe_Cmd(nvmeCmdCtx* nvmCmd)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, realtekCDB, REALTEK_NVME_CDB_SIZE);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, realtekPayload, REALTEK_NVME_CMD_PAYLOAD_LEN);
    eDataTransferDirection realtekCDBDir = 0;
    DISABLE_NONNULL_COMPARE
    if (nvmCmd == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (nvmCmd->dataSize > REALTEK_NVME_MAX_TRANSFER_SIZE_BYTES || nvmCmd->commandDirection == XFER_DATA_IN_OUT ||
        nvmCmd->commandDirection == XFER_DATA_OUT_IN)
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    // 1. build CDB & data for command to send
    // Send CDB to set the command values that will be used to issue a command.
    ret = build_Realtek_NVMe_CDB_And_Payload(realtekCDB, &realtekCDBDir, realtekPayload, REALTEK_NVME_CMD_PAYLOAD_LEN,
                                             REALTEK_PHASE_COMMAND, nvmCmd);
    if (SUCCESS != ret)
    {
        return ret;
    }
    ret = scsi_Send_Cdb(nvmCmd->device, realtekCDB, REALTEK_NVME_CDB_SIZE, realtekPayload, REALTEK_NVME_CMD_PAYLOAD_LEN,
                        realtekCDBDir, NULL, 0, 15);
    if (SUCCESS != ret)
    {
        return ret;
    }

    // 2. build CDB for data trasfer (including non-data)
    // send CDB for data transfer (this tiggers the actual command to execute)
    safe_memset(realtekCDB, REALTEK_NVME_CDB_SIZE, 0, REALTEK_NVME_CDB_SIZE);
    ret = build_Realtek_NVMe_CDB_And_Payload(realtekCDB, &realtekCDBDir, nvmCmd->ptrData, nvmCmd->dataSize,
                                             REALTEK_PHASE_DATA, nvmCmd);
    if (SUCCESS != ret)
    {
        return ret;
    }
    eReturnValues sendRet = scsi_Send_Cdb(nvmCmd->device, realtekCDB, REALTEK_NVME_CDB_SIZE, nvmCmd->ptrData,
                                          nvmCmd->dataSize, realtekCDBDir, NULL, 0, nvmCmd->timeout);
    // NOTE: do not fail the command or anything else YET.
    // Need to request the response information from the command.
    // TODO: There may be some sense data outputs where the return response info won't work or isn't necessary, but they
    // don't seem documented today. Most likely only for illegal requests.
    if (sendRet != OS_COMMAND_TIMEOUT)
    {
        // 3. build CDB for response info
        // send CDB for response info
        safe_memset(realtekCDB, REALTEK_NVME_CDB_SIZE, 0, REALTEK_NVME_CDB_SIZE);
        safe_memset(realtekPayload, REALTEK_NVME_CMD_PAYLOAD_LEN, 0, REALTEK_NVME_CMD_PAYLOAD_LEN);
        ret = build_Realtek_NVMe_CDB_And_Payload(realtekCDB, &realtekCDBDir, realtekPayload,
                                                 REALTEK_NVME_COMPLETION_PAYLOAD_LEN, REALTEK_PHASE_COMPLETION, nvmCmd);
        if (SUCCESS == scsi_Send_Cdb(nvmCmd->device, realtekCDB, REALTEK_NVME_CDB_SIZE, realtekPayload,
                                     REALTEK_NVME_COMPLETION_PAYLOAD_LEN, realtekCDBDir, NULL, 0, 15))
        {
            nvmCmd->commandCompletionData.dw0Valid = true;
            nvmCmd->commandCompletionData.dw1Valid = true;
            nvmCmd->commandCompletionData.dw2Valid = true;
            nvmCmd->commandCompletionData.dw3Valid = true;
            // This is the correct endianness as it has been validated by trying commands with invalid fields to make
            // sure it is correctly interpretted.
            nvmCmd->commandCompletionData.dw0 =
                M_BytesTo4ByteValue(realtekPayload[3], realtekPayload[2], realtekPayload[1], realtekPayload[0]);
            nvmCmd->commandCompletionData.dw1 =
                M_BytesTo4ByteValue(realtekPayload[7], realtekPayload[6], realtekPayload[5], realtekPayload[4]);
            nvmCmd->commandCompletionData.dw2 =
                M_BytesTo4ByteValue(realtekPayload[11], realtekPayload[10], realtekPayload[9], realtekPayload[8]);
            nvmCmd->commandCompletionData.dw3 =
                M_BytesTo4ByteValue(realtekPayload[15], realtekPayload[14], realtekPayload[13], realtekPayload[12]);
        }
    }
    return ret;
}
