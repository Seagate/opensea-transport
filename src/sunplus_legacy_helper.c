// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file sunplus_legacy_helper.c   Implementation for sunplus Legacy USB Pass-through CDBs

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
#include "sunplus_legacy_helper.h"

eReturnValues build_Sunplus_Legacy_Passthrough_CDBs(uint8_t                lowCDB[SUNPLUS_PT_CDB_LEN],
                                                    uint8_t                hiCDB[SUNPLUS_PT_CDB_LEN],
                                                    bool*                  highCDBValid,
                                                    ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = SUCCESS;
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        *highCDBValid         = true;
        hiCDB[OPERATION_CODE] = SUNPLUS_PT_COMMAND_OPCODE;
        hiCDB[1]              = RESERVED;
        hiCDB[2]              = SUNPLUS_SUBCOMMAND_SET_48BIT_REGISTERS;
        hiCDB[3]              = RESERVED;
        hiCDB[4]              = RESERVED;
        hiCDB[5]              = ataCommandOptions->tfr.Feature48;
        hiCDB[6]              = ataCommandOptions->tfr.SectorCount48;
        hiCDB[7]              = ataCommandOptions->tfr.LbaLow48;
        hiCDB[8]              = ataCommandOptions->tfr.LbaMid48;
        hiCDB[9]              = ataCommandOptions->tfr.LbaHi48;
        hiCDB[10]             = RESERVED;
        hiCDB[11]             = RESERVED;
    }
    lowCDB[OPERATION_CODE] = SUNPLUS_PT_COMMAND_OPCODE;
    lowCDB[1]              = RESERVED;
    lowCDB[2]              = SUNPLUS_SUBCOMMAND_SEND_ATA_COMMAND;
    switch (ataCommandOptions->commandDirection)
    {
    case XFER_NO_DATA:
        lowCDB[3] = SUNPLUS_XFER_NONE;
        break;
    case XFER_DATA_IN:
        lowCDB[3] = SUNPLUS_XFER_IN;
        break;
    case XFER_DATA_OUT:
        lowCDB[3] = SUNPLUS_XFER_OUT;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return BAD_PARAMETER;
    }
    lowCDB[4]  = ataCommandOptions->dataSize >> 9; // converting to number of 512B blocks
    lowCDB[5]  = ataCommandOptions->tfr.ErrorFeature;
    lowCDB[6]  = ataCommandOptions->tfr.SectorCount;
    lowCDB[7]  = ataCommandOptions->tfr.LbaLow;
    lowCDB[8]  = ataCommandOptions->tfr.LbaMid;
    lowCDB[9]  = ataCommandOptions->tfr.LbaHi;
    lowCDB[10] = ataCommandOptions->tfr.DeviceHead;
    lowCDB[11] = ataCommandOptions->tfr.CommandStatus;
    return ret;
}

#define SUBPLUS_READ_REG_LEN 16
eReturnValues get_RTFRs_From_Sunplus_Legacy(tDevice*               device,
                                            ataPassthroughCommand* ataCommandOptions,
                                            eReturnValues          commandRet)
{
    eReturnValues ret = SUCCESS;
    if (commandRet == OS_PASSTHROUGH_FAILURE)
    {
        return commandRet;
    }
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, SUNPLUS_PT_CDB_LEN);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, returnData, SUBPLUS_READ_REG_LEN);
    cdb[OPERATION_CODE] = SUNPLUS_PT_COMMAND_OPCODE;
    cdb[1]              = RESERVED;
    cdb[2]              = SUNPLUS_SUBCOMMAND_GET_STATUS;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = RESERVED;
    cdb[7]              = RESERVED;
    cdb[8]              = RESERVED;
    cdb[9]              = RESERVED;
    cdb[10]             = RESERVED;
    cdb[11]             = RESERVED;
    ret = scsi_Send_Cdb(device, cdb, SUNPLUS_PT_CDB_LEN, returnData, SUBPLUS_READ_REG_LEN, XFER_DATA_IN, senseData,
                        SPC3_SENSE_LEN, 0);
    if (ret == SUCCESS)
    {
        ataCommandOptions->rtfr.error  = returnData[1];
        ataCommandOptions->rtfr.secCnt = returnData[2];
        ataCommandOptions->rtfr.lbaLow = returnData[3];
        ataCommandOptions->rtfr.lbaMid = returnData[4];
        ataCommandOptions->rtfr.lbaHi  = returnData[5];
        ataCommandOptions->rtfr.device = returnData[6];
        ataCommandOptions->rtfr.status = returnData[7];
        if (ataCommandOptions->commandType != ATA_CMD_TYPE_TASKFILE && ataCommandOptions->needRTFRs)
        {
            ret = WARN_INCOMPLETE_RFTRS;
        }
    }
    return ret;
}

eReturnValues send_Sunplus_Legacy_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, sunplusLowCDB, SUNPLUS_PT_CDB_LEN);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, sunplusHighCDB, SUNPLUS_PT_CDB_LEN);
    bool     highCDBValid   = false;
    uint8_t* senseData      = M_NULLPTR; // only allocate if the pointer in the ataCommandOptions is M_NULLPTR
    bool     localSenseData = false;
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
    // build the command
    ret = build_Sunplus_Legacy_Passthrough_CDBs(sunplusLowCDB, sunplusHighCDB, &highCDBValid, ataCommandOptions);
    if (ret == SUCCESS)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print verbose tfr info
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // if the highCDB is valid, we need to send it first
        if (highCDBValid)
        {
            ret = scsi_Send_Cdb(device, sunplusHighCDB, SUNPLUS_PT_CDB_LEN, ataCommandOptions->ptrData,
                                ataCommandOptions->dataSize, XFER_NO_DATA, ataCommandOptions->ptrSenseData,
                                ataCommandOptions->senseDataSize, 0);
        }
        // send low CDB
        ret = scsi_Send_Cdb(device, sunplusLowCDB, SUNPLUS_PT_CDB_LEN, ataCommandOptions->ptrData,
                            ataCommandOptions->dataSize, ataCommandOptions->commandDirection,
                            ataCommandOptions->ptrSenseData, ataCommandOptions->senseDataSize, 0);
        // get the RTFRs
        ret = get_RTFRs_From_Sunplus_Legacy(device, ataCommandOptions, ret);
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print RTFRs
            print_Verbose_ATA_Command_Result_Information(ataCommandOptions, device);
        }
        // set return code
        // Based on the RTFRs or sense data, generate a return value
        if (ataCommandOptions->rtfr.status == (ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE))
        {
            ret = SUCCESS;
        }
        else if (ataCommandOptions->rtfr.status == ATA_STATUS_BIT_BUSY)
        {
            ret = IN_PROGRESS;
        }
        else if (ataCommandOptions->rtfr.status == 0 &&
                 ret == SUCCESS) // the IO was successful, however we didn't fill in any tfrs...This should allow us one
                                 // more chance to dummy up values
        {
            ret = SUCCESS;
            ataCommandOptions->rtfr.status =
                ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE; // just make sure we set passing status for
                                                                     // anything that cares to check rtfrs
        }
        else if (ret != NOT_SUPPORTED && ret != IN_PROGRESS)
        {
            ret = FAILURE;
        }
    }
    // before we get rid of the sense data, copy it back to the last command sense data
    safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0,
                SPC3_SENSE_LEN); // clear before copying over data
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
