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
// \file ti_legacy_helper.c   Implementation for TI Legacy USB Pass-through CDBs

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper_func.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"
#include "ti_legacy_helper.h"

eReturnValues build_TI_Legacy_CDB(uint8_t                cdb[16],
                                  ataPassthroughCommand* ataCommandOptions,
                                  bool                   olderOpCode,
                                  bool                   forceMode,
                                  uint8_t                modeValue)
{
    eReturnValues ret = SUCCESS;
    if (olderOpCode)
    {
        cdb[OPERATION_CODE] = TI_LEGACY_OPCODE_OLD;
    }
    else
    {
        cdb[OPERATION_CODE] = TI_LEGACY_OPCODE;
    }
    // If PIO, set the PIO bit
    if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_PIO)
    {
        cdb[1] |= BIT7;
    }
    if (forceMode)
    {
        // They asked us to set a specific PIO/UDMA mode
        cdb[1] |= modeValue & 0x07;
    }
    else
    {
        // this is preferred...set the "fastest" bit to let the bridge determine the transfer mode to use.
        cdb[1] |= BIT3;
    }
    cdb[2] = RESERVED;
    cdb[3] = ataCommandOptions->tfr.DeviceHead;
    cdb[4] = ataCommandOptions->tfr.LbaHi;  // Cyl High
    cdb[5] = ataCommandOptions->tfr.LbaMid; // Cyl Low
    cdb[6] = ataCommandOptions->tfr.ErrorFeature;
    cdb[7] = ataCommandOptions->tfr.SectorCount;
    cdb[8] = ataCommandOptions->tfr.LbaLow; // sector number
    cdb[9] = ataCommandOptions->tfr.CommandStatus;
    // all remaining bytes are marked reserved in the spec. Comments are thoughts of things to "try" if we ever care
    // enough and need to debug one of these super old products
    cdb[10] = RESERVED; // Cyl High Ext?
    cdb[11] = RESERVED; // Cyl Low Ext?
    cdb[12] = RESERVED; // Feature Ext?
    cdb[13] = RESERVED; // Sector Count Ext?
    cdb[14] = RESERVED; // Sector Number Ext?
    cdb[15] = RESERVED;
    return ret;
}

eReturnValues send_TI_Legacy_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret            = UNKNOWN;
    uint8_t*      senseData      = M_NULLPTR; // only allocate if the pointer in the ataCommandOptions is M_NULLPTR
    bool          localSenseData = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, tiCDB, CDB_LEN_16);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return NOT_SUPPORTED;
    }
    if (!ataCommandOptions->ptrSenseData)
    {
        senseData = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!senseData)
        {
            return MEMORY_FAILURE;
        }
        localSenseData                   = true;
        ataCommandOptions->ptrSenseData  = senseData;
        ataCommandOptions->senseDataSize = SPC3_SENSE_LEN;
    }

    ret = build_TI_Legacy_CDB(tiCDB, ataCommandOptions, false, false, 0);
    if (ret == SUCCESS)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // printf out register verbose information
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // send the CDB
        ret = scsi_Send_Cdb(device, tiCDB, CDB_LEN_16, ataCommandOptions->ptrData, ataCommandOptions->dataSize,
                            ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData,
                            ataCommandOptions->senseDataSize, 0);
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
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print out RTFRs
            print_Verbose_ATA_Command_Result_Information(ataCommandOptions, device);
        }
    }
    safe_memcpy(&device->drive_info.lastCommandSenseData[0], SPC3_SENSE_LEN, &ataCommandOptions->ptrSenseData,
                M_Min(SPC3_SENSE_LEN, ataCommandOptions->senseDataSize));
    safe_memcpy(&device->drive_info.lastCommandRTFRs, sizeof(ataReturnTFRs), &ataCommandOptions->rtfr,
                sizeof(ataReturnTFRs));
    safe_free_aligned(&senseData);
    if (localSenseData)
    {
        ataCommandOptions->ptrSenseData  = M_NULLPTR;
        ataCommandOptions->senseDataSize = 0;
    }
    if ((device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) > ataCommandOptions->timeout)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    return ret;
}
