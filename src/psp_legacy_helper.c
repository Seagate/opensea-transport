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
// \file psp_legacy_helper.c   Implementation for PSP Legacy USB Pass-through CDBs

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
#include "psp_legacy_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

eReturnValues enable_Disable_ATA_Passthrough(tDevice* device, bool enable)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    cdb[CDB_OPERATION_CODE] = PSP_OPCODE;
    if (enable)
    {
        cdb[CDB_1] |= PSP_FUNC_ENABLE_ATA_PASSTHROUGH;
        // set the key
        cdb[CDB_2]  = M_Byte7(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_3]  = M_Byte6(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_4]  = M_Byte5(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_5]  = M_Byte4(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_6]  = M_Byte3(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_7]  = M_Byte2(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_8]  = M_Byte1(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_9]  = M_Byte0(PSP_PASSTHROUGH_ENABLE_KEY);
        cdb[CDB_10] = RESERVED;
        // control byte - set to 0
        cdb[CDB_11] = 0;
    }
    else
    {
        cdb[CDB_1] |= PSP_FUNC_DISABLE_ATA_PASSTHROUGH;
    }
    ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, M_NULLPTR, 0, XFER_NO_DATA, senseData, SPC3_SENSE_LEN, 0);
    return ret;
}

eReturnValues build_PSP_Legacy_CDB(uint8_t* cdb, uint8_t* cdbLen, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = SUCCESS;
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        *cdbLen                 = PSP_EXT_COMMAND_CDB_LEN;
        cdb[CDB_OPERATION_CODE] = PSP_OPCODE;
        switch (ataCommandOptions->commandDirection)
        {
        case XFER_NO_DATA:
            cdb[CDB_1] |= PSP_FUNC_NON_DATA_COMMAND;
            break;
        case XFER_DATA_IN:
            cdb[CDB_1] |= PSP_FUNC_EXT_DATA_IN_COMMAND;
            break;
        case XFER_DATA_OUT:
            cdb[CDB_1] |= PSP_FUNC_EXT_DATA_OUT_COMMAND;
            break;
        default:
            return NOT_SUPPORTED;
        }
        cdb[CDB_2]  = RESERVED;
        cdb[CDB_3]  = ataCommandOptions->tfr.SectorCount48;
        cdb[CDB_4]  = ataCommandOptions->tfr.LbaLow48;
        cdb[CDB_5]  = ataCommandOptions->tfr.LbaMid48;
        cdb[CDB_6]  = ataCommandOptions->tfr.LbaHi48;
        cdb[CDB_7]  = RESERVED;
        cdb[CDB_8]  = ataCommandOptions->tfr.SectorCount;
        cdb[CDB_9]  = ataCommandOptions->tfr.LbaLow;
        cdb[CDB_10] = ataCommandOptions->tfr.LbaMid;
        cdb[CDB_11] = ataCommandOptions->tfr.LbaHi;
        cdb[CDB_12] = ataCommandOptions->tfr.DeviceHead;
        cdb[CDB_13] = ataCommandOptions->tfr.CommandStatus;
        cdb[CDB_14] = M_Byte0(ataCommandOptions->dataSize / LEGACY_DRIVE_SEC_SIZE);
        cdb[CDB_15] = M_Byte1(ataCommandOptions->dataSize / LEGACY_DRIVE_SEC_SIZE);
        cdb[CDB_16] = RESERVED;
        // control byte. Set to 0
        cdb[CDB_17] = 0;
    }
    else // assume 28bit command
    {
        *cdbLen                 = CDB_LEN_12;
        cdb[CDB_OPERATION_CODE] = PSP_OPCODE;
        switch (ataCommandOptions->commandDirection)
        {
        case XFER_NO_DATA:
            cdb[CDB_1] |= PSP_FUNC_NON_DATA_COMMAND;
            break;
        case XFER_DATA_IN:
            cdb[CDB_1] |= PSP_FUNC_DATA_IN_COMMAND;
            break;
        case XFER_DATA_OUT:
            cdb[CDB_1] |= PSP_FUNC_DATA_OUT_COMMAND;
            break;
        default:
            return NOT_SUPPORTED;
        }
        cdb[CDB_2] = ataCommandOptions->tfr.ErrorFeature;
        cdb[CDB_3] = ataCommandOptions->tfr.SectorCount;
        cdb[CDB_4] = ataCommandOptions->tfr.LbaLow;
        cdb[CDB_5] = ataCommandOptions->tfr.LbaMid;
        cdb[CDB_6] = ataCommandOptions->tfr.LbaHi;
        cdb[CDB_7] = ataCommandOptions->tfr.DeviceHead;
        cdb[CDB_8] = ataCommandOptions->tfr.CommandStatus;
        // transfer length
        cdb[CDB_9]  = M_Byte0(ataCommandOptions->dataSize / LEGACY_DRIVE_SEC_SIZE);
        cdb[CDB_10] = RESERVED;
        // control, set to 0
        cdb[CDB_11] = 0;
    }
    return ret;
}

eReturnValues get_RTFRs_From_PSP_Legacy(tDevice*               device,
                                        ataPassthroughCommand* ataCommandOptions,
                                        eReturnValues          commandRet)
{
    eReturnValues ret = SUCCESS;
    if (commandRet == OS_PASSTHROUGH_FAILURE)
    {
        return commandRet;
    }
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, returnData, 14);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    cdb[CDB_OPERATION_CODE] = PSP_OPCODE;
    cdb[CDB_1] |= PSP_FUNC_RETURN_TASK_FILE_REGISTERS;
    // Set the device register in offset 7 for selecting device 0 vs device 1...we won't do that at this time - TJE
    // send the command
    ret = scsi_Send_Cdb(device, cdb, CDB_LEN_16, returnData, 14, XFER_DATA_IN, senseData, SPC3_SENSE_LEN, 0);
    // now get the RTFRs
    if (ret == SUCCESS && returnData[0] > 0)
    {
        if (returnData[1] & BIT0)
        {
            ataCommandOptions->rtfr.secCntExt = returnData[10];
            ataCommandOptions->rtfr.lbaLowExt = returnData[11];
            ataCommandOptions->rtfr.lbaMidExt = returnData[12];
            ataCommandOptions->rtfr.lbaHiExt  = returnData[13];
        }
        ataCommandOptions->rtfr.error  = returnData[2];
        ataCommandOptions->rtfr.secCnt = returnData[3];
        ataCommandOptions->rtfr.lbaLow = returnData[4];
        ataCommandOptions->rtfr.lbaMid = returnData[5];
        ataCommandOptions->rtfr.lbaHi  = returnData[6];
        ataCommandOptions->rtfr.device = returnData[7];
        ataCommandOptions->rtfr.status = returnData[8];
    }
    return ret;
}

eReturnValues send_PSP_Legacy_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret    = UNKNOWN;
    uint8_t       cdbLen = PSP_EXT_COMMAND_CDB_LEN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, pspCDB, PSP_EXT_COMMAND_CDB_LEN);
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
    ret = build_PSP_Legacy_CDB(pspCDB, &cdbLen, ataCommandOptions);
    if (ret == SUCCESS)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print verbose tfr info
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // send it
        ret = scsi_Send_Cdb(device, pspCDB, cdbLen, ataCommandOptions->ptrData, ataCommandOptions->dataSize,
                            ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData,
                            ataCommandOptions->senseDataSize, 0);
        // get the RTFRs
        ret = get_RTFRs_From_PSP_Legacy(device, ataCommandOptions, ret);
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
    safe_memcpy(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &ataCommandOptions->ptrSenseData,
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
