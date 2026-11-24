// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2026-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file logitec_legacy_helper.c   Implementation for Logitec Legacy USB Pass-through CDBs

#include "math_utils.h"

#include "logitec_legacy_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

M_PARAM_WO(1)
    M_PARAM_RO(2)
    eReturnValues build_Logitec_Legacy_CDB(uint8_t                          cdb[M_NONNULL_ARRAY LOGITEC_LEGACY_CDB_LEN],
                                       ataPassthroughCommand* M_NONNULL ataCommandOptions)
{
    eReturnValues ret = SUCCESS;
    if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_PIO)
    {
        cdb[LOGITEC_CDB_OFFSET_OPCODE] = LOGITEC_LEGACY_OPCODE;
        cdb[LOGITEC_CDB_OFFSET_1_RESERVED] = RESERVED;
        cdb[LOGITEC_CDB_OFFSET_FEATURES] = ataCommandOptions->tfr.feature;
        cdb[LOGITEC_CDB_OFFSET_SECTOR_COUNT] = ataCommandOptions->tfr.sectorCount;
        cdb[LOGITEC_CDB_OFFSET_LBA_LOW] = ataCommandOptions->tfr.lbaLow;
        cdb[LOGITEC_CDB_OFFSET_LBA_MID] = ataCommandOptions->tfr.lbaMid;
        cdb[LOGITEC_CDB_OFFSET_LBA_HIGH] = ataCommandOptions->tfr.lbaHigh;
        cdb[LOGITEC_CDB_OFFSET_DEVICE] = ataCommandOptions->tfr.device;
        cdb[LOGITEC_CDB_OFFSET_COMMAND] = ataCommandOptions->tfr.Command;
        cdb[LOGITEC_CDB_OFFSET_SIGNATURE] = LOGITEC_LEGACY_SIGNATURE;
    }
    else
    {
        // Note: Uncertain if other protocols are supported or not. May be possible with flipping the reserved byte values, but
        // unknown. For now only allow PIO mode.
        ret = NOT_SUPPORTED;
    }
    return ret;
}

M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_Logitec_Legacy_Passthrough_Command(const tDevice* M_NONNULL         device,
                                                      ataPassthroughCommand* M_NONNULL ataCommandOptions)
{
    eReturnValues ret            = UNKNOWN;
    uint8_t*      senseData      = M_NULLPTR; // only allocate if the pointer in the ataCommandOptions is M_NULLPTR
    bool          localSenseData = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logitecCDB, LOGITEC_LEGACY_CDB_LEN);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return NOT_SUPPORTED;
    }
    if (!ataCommandOptions->ptrSenseData)
    {
        senseData = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), get_Device_IO_Minimum_Alignment(device)));
        if (!senseData)
        {
            return MEMORY_FAILURE;
        }
        localSenseData                   = true;
        ataCommandOptions->ptrSenseData  = senseData;
        ataCommandOptions->senseDataSize = SPC3_SENSE_LEN;
    }

    ret = build_Logitec_Legacy_CDB(logitecCDB, ataCommandOptions);
    if (ret == SUCCESS)
    {
        // print out register verbose information
        print_tDevice_Verbose_ATA_Command_Information(device, VERBOSITY_COMMAND_VERBOSE, ataCommandOptions);
        // send the CDB
        ret = scsi_Send_Cdb(device, logitecCDB, M_STATIC_CAST(eCDBLen, LOGITEC_LEGACY_CDB_LEN), ataCommandOptions->ptrData, ataCommandOptions->dataSize,
                            ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData,
                            ataCommandOptions->senseDataSize, ataCommandOptions->timeout);
        // dummy up RTFRs since the TI documentation doesn't have a way to show them.
        // This is all based on how we interpret the scsi sense data in the scsi level.
        switch (ret)
        {
        case OS_PASSTHROUGH_FAILURE:
            break;
        case IN_PROGRESS:
            ataCommandOptions->rtfr.status = ATA_STATUS_BIT_BUSY;
            break;
        case SUCCESS:
            ataCommandOptions->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
            break;
        case UNKNOWN:
            ataCommandOptions->rtfr.status = 0x00;
            break;
        case ABORTED:
            ataCommandOptions->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_ABORT;
            break;
        default:
            ataCommandOptions->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            break;
        }
        // print out RTFRs
        print_tDevice_Verbose_ATA_Command_Result_Information(device, VERBOSITY_COMMAND_VERBOSE, ataCommandOptions);
    }
    copy_Last_Command_Sense_Data_To_tDevice(M_CONST_CAST(tDevice*, device), ataCommandOptions->ptrSenseData,
                                            M_Min(SPC3_SENSE_LEN, ataCommandOptions->senseDataSize));
    copy_Last_Command_RTFRs_To_tDevice(M_CONST_CAST(tDevice*, device), &ataCommandOptions->rtfr);
    safe_free_aligned(&senseData);
    if (localSenseData)
    {
        ataCommandOptions->ptrSenseData  = M_NULLPTR;
        ataCommandOptions->senseDataSize = 0;
    }
    if (did_ATA_Command_Timeout(device, ataCommandOptions))
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    return ret;
}
