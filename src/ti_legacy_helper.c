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
// \file ti_legacy_helper.c   Implementation for TI Legacy USB Pass-through CDBs

#include "ti_legacy_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"

int build_TI_Legacy_CDB(uint8_t cdb[16], ataPassthroughCommand *ataCommandOptions, bool olderOpCode, bool forceMode, uint8_t modeValue)
{
    int ret = SUCCESS;
    if (olderOpCode)
    {
        cdb[OPERATION_CODE] = TI_LEGACY_OPCODE_OLD;
    }
    else
    {
        cdb[OPERATION_CODE] = TI_LEGACY_OPCODE;
    }
    //If PIO, set the PIO bit
    if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_PIO)
    {
        cdb[1] |= BIT7;
    }
    if (forceMode)
    {
        //They asked us to set a specific PIO/UDMA mode
        cdb[1] |= modeValue & 0x07;
    }
    else
    {
        //this is preferred...set the "fastest" bit to let the bridge determine the transfer mode to use.
        cdb[1] |= BIT3;
    }
    cdb[2] = RESERVED;
    cdb[3] = ataCommandOptions->tfr.DeviceHead;
    cdb[4] = ataCommandOptions->tfr.LbaHi;//Cyl High
    cdb[5] = ataCommandOptions->tfr.LbaMid;//Cyl Low
    cdb[6] = ataCommandOptions->tfr.ErrorFeature;
    cdb[7] = ataCommandOptions->tfr.SectorCount;
    cdb[8] = ataCommandOptions->tfr.LbaLow;//sector number
    cdb[9] = ataCommandOptions->tfr.CommandStatus;
    //all remaining bytes are marked reserved in the spec. Comments are thoughts of things to "try" if we ever care enough and need to debug one of these super old products
    cdb[10] = RESERVED;//Cyl High Ext?
    cdb[11] = RESERVED;//Cyl Low Ext?
    cdb[12] = RESERVED;//Feature Ext?
    cdb[13] = RESERVED;//Sector Count Ext?
    cdb[14] = RESERVED;//Sector Number Ext?
    cdb[15] = RESERVED;
    return ret;
}

int send_TI_Legacy_Passthrough_Command(tDevice *device, ataPassthroughCommand *ataCommandOptions)
{
    int ret = UNKNOWN;
    uint8_t *senseData = NULL;//only allocate if the pointer in the ataCommandOptions is NULL
    bool localSenseData = false;
    uint8_t tiCDB[CDB_LEN_16] = { 0 };
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return NOT_SUPPORTED;
    }
    if (!ataCommandOptions->ptrSenseData)
    {
        senseData = (uint8_t*)calloc(SPC3_SENSE_LEN, sizeof(uint8_t));
        if (!senseData)
        {
            return MEMORY_FAILURE;
        }
        localSenseData = true;
        ataCommandOptions->ptrSenseData = senseData;
        ataCommandOptions->senseDataSize = SPC3_SENSE_LEN;
    }

    ret = build_TI_Legacy_CDB(tiCDB, ataCommandOptions, false, false, 0);
    if (ret == SUCCESS)
    {
        //printf out register verbose information
        print_Verbose_ATA_Command_Information(ataCommandOptions);
        //send the CDB
        ret = scsi_Send_Cdb(device, tiCDB, CDB_LEN_16, ataCommandOptions->ptrData, ataCommandOptions->dataSize, ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData, ataCommandOptions->senseDataSize, 0);
        //dummy up RTFRs since the TI documentation doesn't have a way to show them.
        //This is all based on how we interpret the scsi sense data in the scsi level.
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
            ataCommandOptions->rtfr.error = ATA_ERROR_BIT_ABORT;
            break;
        default:
            ataCommandOptions->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            break;
        }
        //print out RTFRs
        print_Verbose_ATA_Command_Result_Information(ataCommandOptions);
    }
    memcpy(&device->drive_info.lastCommandSenseData[0], &ataCommandOptions->ptrSenseData, M_Min(SPC3_SENSE_LEN, ataCommandOptions->senseDataSize));
    memcpy(&device->drive_info.lastCommandRTFRs, &ataCommandOptions->rtfr, sizeof(ataReturnTFRs));
    safe_Free(senseData);
    if (localSenseData)
    {
        ataCommandOptions->ptrSenseData = NULL;
        ataCommandOptions->senseDataSize = 0;
    }
    return ret;
}