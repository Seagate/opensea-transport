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
// \file prolific_legacy_helper.c   Implementation for Prolific Legacy USB Pass-through CDBs

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
#include "prolific_legacy_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

eReturnValues build_Prolific_Legacy_Passthrough_CDBs(uint8_t                lowCDB[16],
                                                     uint8_t                hiCDB[16],
                                                     bool*                  highCDBValid,
                                                     ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = SUCCESS;
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        *highCDBValid         = true;
        hiCDB[OPERATION_CODE] = PROLIFIC_EXECUTE_ATA_COMMAND_OPCODE;
        if (ataCommandOptions->commandDirection != XFER_DATA_OUT)
        {
            // set read bit
            hiCDB[1] |= BIT4;
        }
        // Prefix, so leave bits 3:0 set to zero
        hiCDB[2] = RESERVED;
        hiCDB[3] = ataCommandOptions->tfr.Feature48;
        hiCDB[4] = M_Byte1(CHECK_WORD);
        hiCDB[5] = M_Byte0(CHECK_WORD);
        // Length
        hiCDB[6] = M_Byte3(ataCommandOptions->dataSize);
        hiCDB[7] = M_Byte2(ataCommandOptions->dataSize);
        hiCDB[8] = M_Byte1(ataCommandOptions->dataSize);
        hiCDB[9] = M_Byte0(ataCommandOptions->dataSize);
        // more registers
        hiCDB[10] = ataCommandOptions->tfr.SectorCount48;
        hiCDB[11] = ataCommandOptions->tfr.LbaLow48;
        hiCDB[12] = ataCommandOptions->tfr.LbaMid48;
        hiCDB[13] = ataCommandOptions->tfr.LbaHi48;
        // these two may need to be set to zero instead...when debugging this code, check this.
        hiCDB[14] = ataCommandOptions->tfr.DeviceHead;
        hiCDB[15] = ataCommandOptions->tfr.CommandStatus;
    }
    // set the low registers...if this was an ext command, then we will have filled out the ext registers above.
    lowCDB[OPERATION_CODE] = PROLIFIC_EXECUTE_ATA_COMMAND_OPCODE;
    if (ataCommandOptions->commandDirection != XFER_DATA_OUT)
    {
        // set read bit
        lowCDB[1] |= BIT4;
    }
    // set the normal bits
    lowCDB[1] |= 0x05;
    lowCDB[2] = RESERVED;
    lowCDB[3] = ataCommandOptions->tfr.ErrorFeature;
    lowCDB[4] = M_Byte1(CHECK_WORD);
    lowCDB[5] = M_Byte0(CHECK_WORD);
    // Length
    lowCDB[6] = M_Byte3(ataCommandOptions->dataSize);
    lowCDB[7] = M_Byte2(ataCommandOptions->dataSize);
    lowCDB[8] = M_Byte1(ataCommandOptions->dataSize);
    lowCDB[9] = M_Byte0(ataCommandOptions->dataSize);
    // more registers
    lowCDB[10] = ataCommandOptions->tfr.SectorCount;
    lowCDB[11] = ataCommandOptions->tfr.LbaLow;
    lowCDB[12] = ataCommandOptions->tfr.LbaMid;
    lowCDB[13] = ataCommandOptions->tfr.LbaHi;
    lowCDB[14] = ataCommandOptions->tfr.DeviceHead;
    lowCDB[15] = ataCommandOptions->tfr.CommandStatus; // says PIO commands only...need to test this
    return ret;
}

eReturnValues get_RTFRs_From_Prolific_Legacy(tDevice*               device,
                                             ataPassthroughCommand* ataCommandOptions,
                                             eReturnValues          commandRet)
{
    eReturnValues ret = SUCCESS;
    if (commandRet == OS_PASSTHROUGH_FAILURE)
    {
        return commandRet;
    }
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, returnData, 16);
    cdb[OPERATION_CODE] = PROLIFIC_GET_REGISTERS_OPCODE;
    cdb[1]              = RESERVED;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = M_Byte1(CHECK_WORD);
    cdb[5]              = M_Byte0(CHECK_WORD);
    ret = scsi_Send_Cdb(device, cdb, CDB_LEN_6, returnData, 16, XFER_DATA_IN, senseData, SPC3_SENSE_LEN, 0);
    if (ret == SUCCESS)
    {
        ataCommandOptions->rtfr.status    = returnData[0];
        ataCommandOptions->rtfr.error     = returnData[1];
        ataCommandOptions->rtfr.secCnt    = returnData[2];
        ataCommandOptions->rtfr.secCntExt = returnData[3];
        ataCommandOptions->rtfr.lbaLow    = returnData[4];
        ataCommandOptions->rtfr.lbaLowExt = returnData[5];
        ataCommandOptions->rtfr.lbaMid    = returnData[6];
        ataCommandOptions->rtfr.lbaMidExt = returnData[7];
        ataCommandOptions->rtfr.lbaHi     = returnData[8];
        ataCommandOptions->rtfr.lbaHiExt  = returnData[9];
        ataCommandOptions->rtfr.device    = returnData[10];
    }
    return ret;
}

eReturnValues send_Prolific_Legacy_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, prolificLowCDB, CDB_LEN_16);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, prolificHighCDB, CDB_LEN_16);
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
    ret = build_Prolific_Legacy_Passthrough_CDBs(prolificLowCDB, prolificHighCDB, &highCDBValid, ataCommandOptions);
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
            ret = scsi_Send_Cdb(device, prolificHighCDB, CDB_LEN_16, ataCommandOptions->ptrData,
                                ataCommandOptions->dataSize, XFER_NO_DATA, ataCommandOptions->ptrSenseData,
                                ataCommandOptions->senseDataSize, 0);
        }
        // send low CDB
        ret = scsi_Send_Cdb(device, prolificLowCDB, CDB_LEN_16, ataCommandOptions->ptrData, ataCommandOptions->dataSize,
                            ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData,
                            ataCommandOptions->senseDataSize, 0);
        // get the RTFRs
        ret = get_RTFRs_From_Prolific_Legacy(device, ataCommandOptions, ret);
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
