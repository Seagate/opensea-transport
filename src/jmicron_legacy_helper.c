// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file jmicron_legacy_helper.c   Implementation for JMicron Legacy USB to ATA Pass-through CDBs

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
#include "jmicron_legacy_helper.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"

M_NONNULL_PARAM_LIST(1)
M_PARAM_RO(1)
static M_INLINE bool is_Smart_Return_Status_Command(ataPassthroughCommand* ataCommandOptions)
{
    bool isSmartReturnStatus = false;
    DISABLE_NONNULL_COMPARE
    if (ataCommandOptions != M_NULLPTR)
    {
        if (ataCommandOptions->tfr.CommandStatus == ATA_SMART_CMD)
        {
            if (ataCommandOptions->tfr.ErrorFeature == ATA_SMART_RTSMART)
            {
                isSmartReturnStatus = true;
            }
        }
    }
    RESTORE_NONNULL_COMPARE
    return isSmartReturnStatus;
}

eReturnValues build_JMicron_Legacy_PT_CDB(uint8_t cdb[JM_PROLIFIC_CDB_LEN], ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = SUCCESS;
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    safe_memset(cdb, JM_PROLIFIC_CDB_LEN, 0, JM_PROLIFIC_CDB_LEN);
    cdb[OPERATION_CODE] = JM_ATA_PT_OPCODE;
    switch (ataCommandOptions->commandDirection)
    {
    case XFER_NO_DATA:
        cdb[1] = JM_DIR_NODATA;
        break;
    case XFER_DATA_IN:
        cdb[1] = JM_DIR_DATAIN;
        break;
    case XFER_DATA_OUT:
        cdb[1] = JM_DIR_DATAOUT;
        break;
    default:
        return BAD_PARAMETER;
    }
    cdb[2] = RESERVED;
    if (ataCommandOptions->dataSize > UINT16_MAX)
    {
        return BAD_PARAMETER;
    }
    cdb[3]  = M_Byte1(ataCommandOptions->dataSize);
    cdb[4]  = M_Byte0(ataCommandOptions->dataSize);
    cdb[5]  = ataCommandOptions->tfr.ErrorFeature;
    cdb[6]  = ataCommandOptions->tfr.SectorCount;
    cdb[7]  = ataCommandOptions->tfr.LbaLow;
    cdb[8]  = ataCommandOptions->tfr.LbaMid;
    cdb[9]  = ataCommandOptions->tfr.LbaHi;
    cdb[10] = ataCommandOptions->tfr.DeviceHead; // seems to select between drive 0 and 1 in some cases. Using
                                                 // device-head register instead since this seems more appropriate - TJE
    cdb[11] = ataCommandOptions->tfr.CommandStatus;
    // set prolific signature always whether we send it is a different story
    cdb[12] = M_Byte1(JM_PROLIFIC_SIGNATURE);
    cdb[13] = M_Byte0(JM_PROLIFIC_SIGNATURE);
    return ret;
}

eReturnValues read_Adapter_Register(tDevice*                 device,
                                    eJMicronAdapterRegisters jmregister,
                                    uint8_t*                 ptrData,
                                    uint32_t                 dataSize)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, JM_PROLIFIC_CDB_LEN);
    cdb[OPERATION_CODE] = JM_ATA_PT_OPCODE;
    cdb[1]              = JM_DIR_DATAIN;
    cdb[2]              = RESERVED;
    cdb[3]              = M_Byte1(dataSize);
    cdb[4]              = M_Byte0(dataSize);
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte1(M_STATIC_CAST(uint16_t, jmregister));
    cdb[7]              = M_Byte0(M_STATIC_CAST(uint16_t, jmregister));
    cdb[8]              = RESERVED;
    cdb[9]              = RESERVED;
    cdb[10]             = RESERVED;
    cdb[11]             = JM_READ_ADAPTER_REGISTERS;
    // set prolific signature always whether we send it is a different story
    cdb[12] = M_Byte1(JM_PROLIFIC_SIGNATURE);
    cdb[13] = M_Byte0(JM_PROLIFIC_SIGNATURE);
    // now send the CDB
    return scsi_Send_Cdb(device, cdb,
                         device->drive_info.passThroughHacks.passthroughType == ATA_PASSTHROUGH_JMICRON_PROLIFIC
                             ? M_STATIC_CAST(eCDBLen, JM_PROLIFIC_CDB_LEN)
                             : M_STATIC_CAST(eCDBLen, JM_CDB_LEN),
                         ptrData, dataSize, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0);
}

eReturnValues set_JM_Dev(tDevice* device)
{
    eReturnValues ret    = SUCCESS;
    uint8_t       jmport = UINT8_C(0);
    ret                  = read_Adapter_Register(device, JM_REG_CONNECTED_PORTS, &jmport, sizeof(jmport));
    if (ret == SUCCESS)
    {
        switch (M_STATIC_CAST(eJMicronConnectedPorts, jmport))
        {
        case JM_PORT_DEV0:
            device->drive_info.passThroughHacks.ataPTHacks.jmPTDevSet = true;
            break;
        case JM_PORT_DEV1:
            device->drive_info.passThroughHacks.ataPTHacks.jmPTDevSet = true;
            device->drive_info.ata_Options.isDevice1                  = true;
            break;
        case JM_PORT_BOTH:
            ret = FAILURE; // don't know how to set the correct port for the command to route for this handle.
                           // Might be possible to compare identify data to MN in inquiry in some cases and use
                           // values from identify data to set which device it is as a workaround - TJE
            break;
        }
    }
    return ret;
}

#define JM_REGISTER_BUF_LEN (16)
eReturnValues get_RTFRs_From_JMicron_Legacy(tDevice*               device,
                                            ataPassthroughCommand* ataCommandOptions,
                                            eReturnValues          commandRet)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, registers, JM_REGISTER_BUF_LEN);
    if (commandRet == OS_PASSTHROUGH_FAILURE)
    {
        return commandRet;
    }
    ret = read_Adapter_Register(
        device, ataCommandOptions->tfr.DeviceHead & DEVICE_SELECT_BIT ? JM_REG_DEV1_RESULTS : JM_REG_DEV0_RESULTS,
        registers, JM_REGISTER_BUF_LEN);
    // now get the RTFRs
    if (ret == SUCCESS)
    {
        ataCommandOptions->rtfr.secCnt = registers[0];
        // 1, 2, 3 unknown
        ataCommandOptions->rtfr.lbaMid = registers[4];
        // 5 unknown
        ataCommandOptions->rtfr.lbaLow = registers[6];
        // 7, 8 unknown
        ataCommandOptions->rtfr.device = registers[9];
        ataCommandOptions->rtfr.lbaHi  = registers[10];
        // 11, 12 unknown
        ataCommandOptions->rtfr.error  = registers[13];
        ataCommandOptions->rtfr.status = registers[14];
    }
    return ret;
}

static M_INLINE bool is_Valid_Smart_Return_Status_For_JMicron_USB(uint8_t status)
{
    bool valid = false;
    switch (status)
    {
    case ATA_SMART_BAD_SIG_HI:
    case ATA_SMART_SIG_HI:
        valid = true;
        break;
    default:
        break;
    }
    return valid;
}

eReturnValues send_JMicron_Legacy_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, jmCDB, JM_PROLIFIC_CDB_LEN);
    uint8_t  cdblen         = device->drive_info.passThroughHacks.passthroughType == ATA_PASSTHROUGH_JMICRON_PROLIFIC
                                  ? JM_PROLIFIC_CDB_LEN
                                  : JM_CDB_LEN;
    uint8_t* senseData      = M_NULLPTR; // only allocate if the pointer in the ataCommandOptions is M_NULLPTR
    bool     localSenseData = false;
    uint8_t  smartStatus    = UINT8_C(0);
    if (device->drive_info.passThroughHacks.ataPTHacks.jmPTDevSet == false)
    {
        // before issuing anything, we need to try setting the device number
        ret = set_JM_Dev(device);
        if (ret != SUCCESS)
        {
            return ret;
        }
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
    if (is_Smart_Return_Status_Command(ataCommandOptions))
    {
        // special case to support smart return status on more bridges.
        // note: this may or maynot work on other non-data SMART commands. Will need to manually test to find out.-TJE
        ataCommandOptions->ptrData          = &smartStatus;
        ataCommandOptions->dataSize         = sizeof(smartStatus);
        ataCommandOptions->commandDirection = XFER_DATA_IN;
    }
    // build the command
    ret = build_JMicron_Legacy_PT_CDB(jmCDB, ataCommandOptions);
    if (ret == SUCCESS)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // print verbose tfr info
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // send it
        ret = scsi_Send_Cdb(device, jmCDB, cdblen, ataCommandOptions->ptrData, ataCommandOptions->dataSize,
                            ataCommandOptions->commandDirection, ataCommandOptions->ptrSenseData,
                            ataCommandOptions->senseDataSize, 0);
        if (is_Smart_Return_Status_Command(ataCommandOptions) &&
            is_Valid_Smart_Return_Status_For_JMicron_USB(smartStatus))
        {
            ataCommandOptions->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
            ataCommandOptions->rtfr.error  = UINT8_C(0); // ensure this is set to zero
            if (smartStatus == ATA_SMART_SIG_HI)
            {
                ataCommandOptions->rtfr.lbaHi  = ATA_SMART_SIG_HI;
                ataCommandOptions->rtfr.lbaMid = ATA_SMART_SIG_MID;
            }
            else
            {
                ataCommandOptions->rtfr.lbaHi  = ATA_SMART_BAD_SIG_HI;
                ataCommandOptions->rtfr.lbaMid = ATA_SMART_BAD_SIG_MID;
            }
            ret = SUCCESS;
        }
        else if (ataCommandOptions->needRTFRs)
        {
            // get the RTFRs
            ret = get_RTFRs_From_JMicron_Legacy(device, ataCommandOptions, ret);
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
                     ret == SUCCESS) // the IO was successful, however we didn't fill in any tfrs...This should allow us
                                     // one more chance to dummy up values
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
    }
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
