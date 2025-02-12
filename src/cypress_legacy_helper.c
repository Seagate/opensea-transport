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
// \file cypress_legacy_helper.c   Implementation for Cypress Legacy USB Pass-through CDBs

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
#include "cypress_legacy_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

eReturnValues build_Cypress_Legacy_CDB(uint8_t cdb[16], ataPassthroughCommand* ataCommandOptions)
{
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return NOT_SUPPORTED;
    }
    cdb[OPERATION_CODE] = CYPRESS_SIGNATURE_OPCODE;
    cdb[1]              = CYPRESS_SUBCOMMAND;
    // check if we are sending an identify command
    if (ataCommandOptions->tfr.CommandStatus == ATA_IDENTIFY ||
        ataCommandOptions->tfr.CommandStatus == ATA_IDENTIFY_DMA ||
        ataCommandOptions->tfr.CommandStatus == ATAPI_IDENTIFY)
    {
        cdb[2] |= CYPRESS_IDENTIFY_DATA_BIT;
    }
    if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_DMA || ataCommandOptions->commadProtocol == ATA_PROTOCOL_UDMA)
    {
        cdb[2] |= CYPRESS_UDMA_COMMAND_BIT;
    }
    // set register select to 0xFF for all registers. (we can change this later to set bits based on what registers we
    // actually use)
    cdb[3] = 0xFF;
    // Transfer Block Count (used for multiple commands)
    if (ataCommandOptions->multipleCount)
    {
        cdb[4] = ataCommandOptions->multipleCount;
    }
    // command registers
    cdb[5]  = ataCommandOptions->tfr.DeviceControl;
    cdb[6]  = ataCommandOptions->tfr.ErrorFeature;
    cdb[7]  = ataCommandOptions->tfr.SectorCount;
    cdb[8]  = ataCommandOptions->tfr.LbaLow; // sector num
    cdb[9]  = ataCommandOptions->tfr.LbaMid; // cyl low
    cdb[10] = ataCommandOptions->tfr.LbaHi;  // cyl high
    cdb[11] = ataCommandOptions->tfr.DeviceHead;
    cdb[12] = ataCommandOptions->tfr.CommandStatus;
    return SUCCESS;
}

eReturnValues get_RTFRs_From_Cypress_Legacy(tDevice*               device,
                                            ataPassthroughCommand* ataCommandOptions,
                                            eReturnValues          commandRet)
{
    eReturnValues ret = SUCCESS;
    if (commandRet == OS_PASSTHROUGH_FAILURE)
    {
        return commandRet;
    }
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, returnData, 8);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    cdb[OPERATION_CODE] = CYPRESS_SIGNATURE_OPCODE;
    cdb[1]              = CYPRESS_SUBCOMMAND;
    cdb[2] |= CYPRESS_TASK_FILE_READ_BIT;
    ret = scsi_Send_Cdb(device, cdb, CDB_LEN_16, returnData, 8, XFER_DATA_IN, senseData, SPC3_SENSE_LEN, 0);
    if (ret == SUCCESS)
    {
        // byte 0 has the alternate status
        ataCommandOptions->rtfr.error  = returnData[1];
        ataCommandOptions->rtfr.secCnt = returnData[2];
        ataCommandOptions->rtfr.lbaLow = returnData[3];
        ataCommandOptions->rtfr.lbaMid = returnData[4];
        ataCommandOptions->rtfr.lbaHi  = returnData[5];
        ataCommandOptions->rtfr.device = returnData[6];
        ataCommandOptions->rtfr.status = returnData[7];
    }
    return ret;
}

eReturnValues send_Cypress_Legacy_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cypressCDB, CDB_LEN_16);
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
    ret = build_Cypress_Legacy_CDB(cypressCDB, ataCommandOptions);
    if (ret == SUCCESS)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print verbose tfr info
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // send it
        ret = scsi_Send_Cdb(device, cypressCDB, CDB_LEN_16, ataCommandOptions->ptrData, ataCommandOptions->dataSize,
                            ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData,
                            ataCommandOptions->senseDataSize, 0);
        // get the RTFRs
        ret = get_RTFRs_From_Cypress_Legacy(device, ataCommandOptions, ret);
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
