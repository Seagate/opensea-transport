// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file csmi_legacy_pt_cdb_helper.c
// \brief Defines the constants, structures, & functions to help with legacy CSMI ATA passthrough CDB implementation

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "csmi_legacy_pt_cdb_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

// This is an E0 command, much like SAT to issue a generic command through STP by making the drive look like SCSI...This
// is likely not even used since SAT does this... From section 7.1 of CSMI spec
#define CSMI_ATA_PASSTHROUGH_OP_CODE 0xE0
#define CSMI_PROTOCOL_NON_DATA       0
#define CSMI_PROTOCOL_PIO_IN         1
#define CSMI_PROTOCOL_PIO_OUT        2
#define CSMI_PROTOCOL_DMA_IN         3
#define CSMI_PROTOCOL_DMA_OUT        4
#define CSMI_PROTOCOL_PACKET_IN      5
#define CSMI_PROTOCOL_PACKET_OUT     6
#define CSMI_PROTOCOL_DMA_QUEUED_IN  7
#define CSMI_PROTOCOL_DMA_QUEUED_OUT 8

eReturnValues build_CSMI_Passthrough_CDB(uint8_t cdb[CSMI_PASSTHROUGH_CDB_LENGTH], ataPassthroughCommand* ataPtCmd)
{
    eReturnValues ret = BAD_PARAMETER;
    DISABLE_NONNULL_COMPARE
    if (cdb != M_NULLPTR && ataPtCmd != M_NULLPTR)
    {
        ret                 = SUCCESS;
        cdb[OPERATION_CODE] = CSMI_ATA_PASSTHROUGH_OP_CODE;
        switch (ataPtCmd->commadProtocol)
        {
        case ATA_PROTOCOL_PIO:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_PIO_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_PIO_OUT;
            }
            break;
        case ATA_PROTOCOL_DMA:
        case ATA_PROTOCOL_UDMA:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_DMA_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_DMA_OUT;
            }
            break;
        case ATA_PROTOCOL_NO_DATA:
            cdb[1] = CSMI_PROTOCOL_NON_DATA;
            break;
        case ATA_PROTOCOL_DMA_QUE:
        case ATA_PROTOCOL_DMA_FPDMA:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_DMA_QUEUED_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_DMA_QUEUED_OUT;
            }
            break;
        case ATA_PROTOCOL_PACKET:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_PACKET_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_PACKET_OUT;
            }
            break;
        default:
            return OS_COMMAND_NOT_AVAILABLE;
        }
        // check if the command is doing a transfer number in words...otherwise set blocks
        if (ataPtCmd->ataTransferBlocks == ATA_PT_NUMBER_OF_BYTES)
        {
            if ((ataPtCmd->dataSize / sizeof(uint16_t)) > UINT16_MAX)
            {
                return OS_COMMAND_NOT_AVAILABLE;
            }
            cdb[14] = M_Byte1(ataPtCmd->dataSize / sizeof(uint16_t));
            cdb[15] = M_Byte0(ataPtCmd->dataSize / sizeof(uint16_t));
        }
        else if (ataPtCmd->ataTransferBlocks == ATA_PT_NO_DATA_TRANSFER)
        {
            cdb[14] = 0;
            cdb[15] = 0;
        }
        // TODO: If a rare case occurs and this is used + the rare case of a non 512B logical sector size, we may need
        // to change from the blocks bit below...this combination is highly unlikely to be found though and SAT can
        // conver this as necessary.
        else
        {
            cdb[1] |= BIT7; // set the blocks bit
            // set block count (OLD spec says this is in terms of 512...might be different for new 4k drives
            // though...not like this is even used though)
            cdb[14] = M_Byte1(ataPtCmd->dataSize / LEGACY_DRIVE_SEC_SIZE);
            cdb[15] = M_Byte0(ataPtCmd->dataSize / LEGACY_DRIVE_SEC_SIZE);
        }
        // set registers
        if (ataPtCmd->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
            ataPtCmd->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
        {
            if (ataPtCmd->tfr.aux1 || ataPtCmd->tfr.aux2 || ataPtCmd->tfr.aux3 || ataPtCmd->tfr.aux4 ||
                ataPtCmd->tfr.icc)
            {
                return OS_COMMAND_NOT_AVAILABLE;
            }
            cdb[3] = ataPtCmd->tfr.Feature48;
            cdb[5] = ataPtCmd->tfr.SectorCount48;
            cdb[7] = ataPtCmd->tfr.LbaHi48;
            cdb[8] = ataPtCmd->tfr.LbaMid48;
            cdb[9] = ataPtCmd->tfr.LbaLow48;
        }
        else
        {
            cdb[3] = 0;
            cdb[5] = 0;
            cdb[7] = 0;
            cdb[8] = 0;
            cdb[9] = 0;
        }
        cdb[2]  = ataPtCmd->tfr.CommandStatus;
        cdb[4]  = ataPtCmd->tfr.ErrorFeature;
        cdb[6]  = ataPtCmd->tfr.SectorCount;
        cdb[10] = ataPtCmd->tfr.LbaHi;
        cdb[11] = ataPtCmd->tfr.LbaMid;
        cdb[12] = ataPtCmd->tfr.LbaLow;
        cdb[13] = ataPtCmd->tfr.DeviceHead;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}

eReturnValues get_RTFRs_From_CSMI_Legacy(tDevice* device, ataPassthroughCommand* ataCommandOptions, int commandRet)
{
    // TODO: Whenever a driver is found using this legacy CDB, we need to figure out how RTFRs are returned, IF there is
    // a way that they are returned.
    M_USE_UNUSED(device);
    M_USE_UNUSED(ataCommandOptions);
    M_USE_UNUSED(commandRet);
    return NOT_SUPPORTED;
}

eReturnValues send_CSMI_Legacy_ATA_Passthrough(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, csmiCDB, CSMI_PASSTHROUGH_CDB_LENGTH);
    uint8_t* senseData      = M_NULLPTR; // only allocate if the pointer in the ataCommandOptions is M_NULLPTR
    bool     localSenseData = false;
    if (ataCommandOptions->ptrSenseData == M_NULLPTR)
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
    ret = build_CSMI_Passthrough_CDB(csmiCDB, ataCommandOptions);
    if (ret == SUCCESS)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print verbose tfr info
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // send it
        ret = scsi_Send_Cdb(device, csmiCDB, CSMI_PASSTHROUGH_CDB_LENGTH, ataCommandOptions->ptrData,
                            ataCommandOptions->dataSize, ataCommandOptions->commandDirection,
                            ataCommandOptions->ptrSenseData, ataCommandOptions->senseDataSize, 0);

        // TODO: get the RTFRs if this is even possible...it's not documented
        // ret = get_RTFRs_From_CSMI_Legacy(device, ataCommandOptions, ret);
        // if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        //{
        //     //print RTFRs
        //     print_Verbose_ATA_Command_Result_Information(ataCommandOptions, device);
        // }
        ////set return code
        ////Based on the RTFRs or sense data, generate a return value
        // if (ataCommandOptions->rtfr.status == (ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE))
        //{
        //     ret = SUCCESS;
        // }
        // else if (ataCommandOptions->rtfr.status == ATA_STATUS_BIT_BUSY)
        //{
        //     ret = IN_PROGRESS;
        // }
        // else if (ataCommandOptions->rtfr.status == 0 && ret == SUCCESS)//the IO was successful, however we didn't
        // fill in any tfrs...This should allow us one more chance to dummy up values
        //{
        //     ret = SUCCESS;
        //     ataCommandOptions->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;//just make sure we
        //     set passing status for anything that cares to check rtfrs
        // }
        // else if (ret != NOT_SUPPORTED && ret != IN_PROGRESS)
        //{
        //     ret = FAILURE;
        // }
    }
    // before we get rid of the sense data, copy it back to the last command sense data
    safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0,
                SPC3_SENSE_LEN); // clear before copying over data
    safe_memcpy(&device->drive_info.lastCommandSenseData[0], SPC3_SENSE_LEN, &ataCommandOptions->ptrSenseData,
                M_Min(SPC3_SENSE_LEN, ataCommandOptions->senseDataSize));
    // safe_memcpy(&device->drive_info.lastCommandRTFRs, sizeof(ataReturnTFRs), &ataCommandOptions->rtfr,
    // sizeof(ataReturnTFRs));
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
