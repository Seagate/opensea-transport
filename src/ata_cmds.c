// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file ata_cmds.c   Implementation for ATA Spec command functions
//                     The intention of the file is to be generic & not OS specific

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper_func.h"
#include "common_public.h"
#include "csmi_legacy_pt_cdb_helper.h"
#include "cypress_legacy_helper.h"
#include "jmicron_legacy_helper.h"
#include "nec_legacy_helper.h"
#include "prolific_legacy_helper.h"
#include "psp_legacy_helper.h"
#include "sat_helper_func.h"
#include "scsi_helper_func.h"
#include "sunplus_legacy_helper.h"
#include "ti_legacy_helper.h"

OPENSEA_TRANSPORT_API eReturnValues ata_Passthrough_Command(const tDevice* M_NONNULL               device,
                                                            const ataPassthroughCommand* M_NONNULL ataCommandOptions)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.passThroughHacks.passthroughType)
    {
    case ATA_PASSTHROUGH_PSP:
        ret = send_PSP_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_CYPRESS:
        ret = send_Cypress_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_PROLIFIC:
        ret = send_Prolific_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_TI:
        ret = send_TI_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_NEC:
        ret = send_NEC_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_SAT:
        ret = send_SAT_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_CSMI:
        ret = send_CSMI_Legacy_ATA_Passthrough(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_JMICRON:
        M_FALLTHROUGH;
    case ATA_PASSTHROUGH_JMICRON_PROLIFIC:
        ret = send_JMicron_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    case ATA_PASSTHROUGH_SUNPLUS:
        ret = send_Sunplus_Legacy_Passthrough_Command(device, M_CONST_CAST(ataPassthroughCommand*, ataCommandOptions));
        break;
    default:
        ret = BAD_PARAMETER;
        break;
    }
    return ret;
}

M_PARAM_RO(1) eReturnValues ata_Soft_Reset(const tDevice* M_NONNULL device, uint8_t timeout)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand softReset;
    safe_memset(&softReset, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    softReset.commadProtocol   = ATA_PROTOCOL_SOFT_RESET;
    softReset.commandType      = ATA_CMD_TYPE_TASKFILE;
    softReset.commandDirection = XFER_NO_DATA;
    softReset.ptrData          = M_NULLPTR;
    softReset.timeout          = timeout;
    if (timeout > 14)
    {
        softReset.timeout = 14;
    }

    ret = ata_Passthrough_Command(device, &softReset);

    return ret;
}

M_PARAM_RO(1) eReturnValues ata_Hard_Reset(const tDevice* M_NONNULL device, uint8_t timeout)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand hardReset;
    safe_memset(&hardReset, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    hardReset.commadProtocol   = ATA_PROTOCOL_HARD_RESET;
    hardReset.commandType      = ATA_CMD_TYPE_TASKFILE;
    hardReset.commandDirection = XFER_NO_DATA;
    hardReset.ptrData          = M_NULLPTR;
    hardReset.timeout          = timeout;
    if (timeout > 14)
    {
        hardReset.timeout = 14;
    }

    ret = ata_Passthrough_Command(device, &hardReset);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Identify(const tDevice* M_NONNULL device,
                                                 uint8_t* M_NONNULL       ptrData,
                                                 uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;
    explicit_zeroes(ptrData, dataSize);
    ataPassthroughCommand identify =
        create_ata_pio_in_cmd(device, ATA_IDENTIFY, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataSize);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Identify command\n");
    ret = ata_Passthrough_Command(device, &identify);

    if (ret == SUCCESS)
    {
        // copy the data to the device structure so that it's not (as) stale
        copy_ata_identify_to_tdevice(M_CONST_CAST(tDevice*, device), ptrData);
    }

    if (ret == SUCCESS)
    {
        if (ptrData[510] == ATA_CHECKSUM_VALIDITY_INDICATOR)
        {
            // we got data, so validate the checksum
            uint32_t invalidSec = UINT32_C(0);
            if (!is_Checksum_Valid(ptrData, LEGACY_DRIVE_SEC_SIZE, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
                print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Warning: Identify Checksum is invalid\n");
            }
        }
        else
        {
            // Don't do anything. This device doesn't use a checksum.
        }
    }

    print_tDevice_Return_Enum(device, "Identify", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Command(const tDevice* M_NONNULL device,
                                                         eATASanitizeFeature      sanitizeFeature,
                                                         uint64_t                 lba,
                                                         uint16_t                 sectorCount)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataSanitizeCmd =
        create_ata_nondata_cmd(device, ATA_SANITIZE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    ataSanitizeCmd.tfr.SectorCount   = M_Byte0(sectorCount);
    ataSanitizeCmd.tfr.SectorCount48 = M_Byte1(sectorCount);
    ataSanitizeCmd.tfr.ErrorFeature  = M_Byte0(M_STATIC_CAST(uint16_t, sanitizeFeature));
    ataSanitizeCmd.tfr.Feature48     = M_Byte1(M_STATIC_CAST(uint16_t, sanitizeFeature));
    set_ata_pt_LBA_48_sig(&ataSanitizeCmd, lba);
    if (sanitizeFeature == ATA_SANITIZE_STATUS)
    {
        // We use the RTFRs from the status to decide what to do.
        // For all others we do not need RTFRs. If an error occurs we should get them anyways -TJE
        ataSanitizeCmd.needRTFRs = true;
    }

    // Determine sanitize feature description string
    const char* sanitizeFeatureStr = "ATA Sanitize Command - Unknown";
    switch (sanitizeFeature)
    {
    case ATA_SANITIZE_STATUS:
        sanitizeFeatureStr = "ATA Sanitize Command - Status";
        break;
    case ATA_SANITIZE_CRYPTO_SCRAMBLE:
        sanitizeFeatureStr = "ATA Sanitize Command - Crypto Scramble";
        break;
    case ATA_SANITIZE_BLOCK_ERASE:
        sanitizeFeatureStr = "ATA Sanitize Command - Block Erase";
        break;
    case ATA_SANITIZE_OVERWRITE_ERASE:
        sanitizeFeatureStr = "ATA Sanitize Command - Overwrite Erase";
        break;
    case ATA_SANITIZE_FREEZE_LOCK:
        sanitizeFeatureStr = "ATA Sanitize Command - Freeze Lock";
        break;
    case ATA_SANITIZE_ANTI_FREEZE_LOCK:
        sanitizeFeatureStr = "ATA Sanitize Command - Anti Freeze Lock";
        break;
    }

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending %s\n", sanitizeFeatureStr);

    ret = ata_Passthrough_Command(device, &ataSanitizeCmd);

    print_tDevice_Return_Enum(device, sanitizeFeatureStr, ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Status(const tDevice* M_NONNULL device, bool clearFailureMode)
{
    uint16_t statusCount = UINT16_C(0);
    if (clearFailureMode)
    {
        statusCount |= BIT0;
    }
    return ata_Sanitize_Command(device, ATA_SANITIZE_STATUS, 0, statusCount);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Crypto_Scramble(const tDevice* M_NONNULL device,
                                                                 bool                     failureModeBit,
                                                                 bool                     znr)
{
    uint16_t cryptoCount = UINT16_C(0);
    if (failureModeBit)
    {
        cryptoCount |= BIT4;
    }
    if (znr)
    {
        cryptoCount |= BIT15;
    }
    return ata_Sanitize_Command(device, ATA_SANITIZE_CRYPTO_SCRAMBLE, ATA_SANITIZE_CRYPTO_LBA, cryptoCount);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Block_Erase(const tDevice* M_NONNULL device,
                                                             bool                     failureModeBit,
                                                             bool                     znr)
{
    uint16_t blockEraseCount = UINT16_C(0);
    if (failureModeBit)
    {
        blockEraseCount |= BIT4;
    }
    if (znr)
    {
        blockEraseCount |= BIT15;
    }
    return ata_Sanitize_Command(device, ATA_SANITIZE_BLOCK_ERASE, ATA_SANITIZE_BLOCK_ERASE_LBA, blockEraseCount);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Overwrite_Erase(const tDevice* M_NONNULL device,
                                                                 bool                     failureModeBit,
                                                                 bool                     invertBetweenPasses,
                                                                 uint8_t                  numberOfPasses,
                                                                 uint32_t                 overwritePattern,
                                                                 bool                     znr,
                                                                 bool                     definitiveEndingPattern)
{
    uint16_t overwriteCount = UINT16_C(0);
    uint64_t overwriteLBA   = overwritePattern;
    overwriteLBA |= (C_CAST(uint64_t, ATA_SANITIZE_OVERWRITE_LBA) << 32);
    if (failureModeBit)
    {
        overwriteCount |= BIT4;
    }
    if (invertBetweenPasses)
    {
        overwriteCount |= BIT7;
    }
    if (znr)
    {
        overwriteCount |= BIT15;
    }
    if (definitiveEndingPattern)
    {
        overwriteCount |= BIT6;
    }
    overwriteCount |= M_Nibble0(numberOfPasses);
    return ata_Sanitize_Command(device, ATA_SANITIZE_OVERWRITE_ERASE, overwriteLBA, overwriteCount);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Freeze_Lock(const tDevice* M_NONNULL device)
{
    return ata_Sanitize_Command(device, ATA_SANITIZE_FREEZE_LOCK, ATA_SANITIZE_FREEZE_LOCK_LBA, RESERVED);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sanitize_Anti_Freeze_Lock(const tDevice* M_NONNULL device)
{
    return ata_Sanitize_Command(device, ATA_SANITIZE_ANTI_FREEZE_LOCK, ATA_SANITIZE_ANTI_FREEZE_LOCK_LBA, RESERVED);
}

OPENSEA_TRANSPORT_API eReturnValues ata_Read_Log_Ext(const tDevice* M_NONNULL device,
                                                     uint8_t                  logAddress,
                                                     uint16_t                 pageNumber,
                                                     uint8_t* M_NONNULL       ptrData,
                                                     uint32_t                 dataSize,
                                                     bool                     useDMA,
                                                     uint16_t                 featureRegister)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    // Determine log read command variant
    const char* readLogName = useDMA ? "Read Log Ext DMA" : "Read Log Ext";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA %s - Log %02" PRIX8 "h, Page %" PRIu16 ", Count %" PRIu32 "\n",
                                          readLogName, logAddress, pageNumber, (dataSize / LEGACY_DRIVE_SEC_SIZE));

    if (ptrData == M_NULLPTR || dataSize < LEGACY_DRIVE_SEC_SIZE || dataSize % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }

    ataCommandOptions.commandDirection         = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.commandType              = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_512B_BLOCKS;
    if (useDMA)
    {
        ataCommandOptions =
            create_ata_dma_in_cmd(device, ATA_READ_LOG_EXT_DMA, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    else
    {
        ataCommandOptions =
            create_ata_pio_in_cmd(device, ATA_READ_LOG_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    ataCommandOptions.tfr.LbaLow       = logAddress;
    ataCommandOptions.tfr.LbaMid       = M_Byte0(pageNumber);
    ataCommandOptions.tfr.LbaHi        = RESERVED;
    ataCommandOptions.tfr.LbaLow48     = RESERVED;
    ataCommandOptions.tfr.LbaMid48     = M_Byte1(pageNumber);
    ataCommandOptions.tfr.LbaHi48      = RESERVED;
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(featureRegister);
    ataCommandOptions.tfr.Feature48    = M_Byte1(featureRegister);

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        uint32_t invalidSec = UINT32_C(0);
        switch (logAddress)
        {
        case ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG:
        case ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG:
        case ATA_LOG_DELAYED_LBA_LOG:
        case ATA_LOG_NCQ_COMMAND_ERROR_LOG:
        case ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG:
            // we got data, so validate the checksum
            if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
            }
            break;
        default:
            // don't do anything since not all logs have checksums to validate
            break;
        }
    }

    print_tDevice_Return_Enum(device, readLogName, ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_Log_Ext(const tDevice* M_NONNULL device,
                                                      uint8_t                  logAddress,
                                                      uint16_t                 pageNumber,
                                                      uint8_t* M_NONNULL       ptrData,
                                                      uint32_t                 dataSize,
                                                      bool                     useDMA,
                                                      bool                     forceRTFRs)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    // Determine log write command variant
    const char* writeLogName = useDMA ? "Write Log Ext DMA" : "Write Log Ext";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA %s - Log %02" PRIX8 "h, Page %" PRIu16 ", Count %" PRIu32 "\n",
                                          writeLogName, logAddress, pageNumber, (dataSize / LEGACY_DRIVE_SEC_SIZE));

    if (ptrData == M_NULLPTR || dataSize < LEGACY_DRIVE_SEC_SIZE || dataSize % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }

    if (useDMA)
    {
        ataCommandOptions =
            create_ata_dma_out_cmd(device, ATA_WRITE_LOG_EXT_DMA, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                                   M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    else
    {
        ataCommandOptions =
            create_ata_pio_out_cmd(device, ATA_WRITE_LOG_EXT_CMD, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                                   M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    ataCommandOptions.needRTFRs = forceRTFRs;

    // Will the ptrData be the correct size for the log?
    ataCommandOptions.tfr.LbaLow   = logAddress;
    ataCommandOptions.tfr.LbaMid   = M_Byte0(pageNumber);
    ataCommandOptions.tfr.LbaHi    = RESERVED;
    ataCommandOptions.tfr.LbaLow48 = RESERVED;
    ataCommandOptions.tfr.LbaMid48 = M_Byte1(pageNumber);
    ataCommandOptions.tfr.LbaHi48  = RESERVED;
    ataCommandOptions.tfr.ErrorFeature =
        RESERVED; // ATA Spec says Log specific. No known logs need this at this time-TJE
    ataCommandOptions.tfr.Feature48 = RESERVED;

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeLogName, ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Command(const tDevice* M_NONNULL device,
                                                      uint8_t                  feature,
                                                      uint8_t                  lbaLo,
                                                      uint8_t* M_NULLABLE      ptrData,
                                                      uint32_t                 dataSize,
                                                      uint32_t                 timeout,
                                                      bool                     forceRTFRs,
                                                      uint8_t                  countReg)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    const char* smartFeatureName = "Unknown SMART command";
    switch (feature)
    {
    case ATA_SMART_READ_LOG:
        smartFeatureName = "SMART Read Log";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                              "Sending ATA %s - Log %02" PRIX8 "h, Count %" PRIu32 "\n",
                                              smartFeatureName, lbaLo, (dataSize / LEGACY_DRIVE_SEC_SIZE));
        ataCommandOptions =
            create_ata_pio_in_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
        break;
    case ATA_SMART_RDATTR_THRESH:
        smartFeatureName = "SMART Read Thresholds";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions =
            create_ata_pio_in_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
        break;
    case ATA_SMART_READ_DATA:
        smartFeatureName = "SMART Read Data";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions =
            create_ata_pio_in_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
        break;
    case ATA_SMART_WRITE_LOG:
        smartFeatureName = "SMART Write Log";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                              "Sending ATA %s - Log %02" PRIX8 "h, Count %" PRIu32 "\n",
                                              smartFeatureName, lbaLo, (dataSize / LEGACY_DRIVE_SEC_SIZE));
        ataCommandOptions =
            create_ata_pio_out_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE,
                                   M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
        ataCommandOptions.needRTFRs = forceRTFRs;
        break;
    case ATA_SMART_SW_AUTOSAVE:
        smartFeatureName = "SMART Attribute Autosave";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
        ataCommandOptions.tfr.SectorCount = countReg;
        break;
    case ATA_SMART_SAVE_ATTRVALUE:
        smartFeatureName = "SMART Save Attribute";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
        ataCommandOptions.tfr.SectorCount = countReg;
        break;
    case ATA_SMART_ENABLE:
        smartFeatureName = "SMART Enable Operations";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
        ataCommandOptions.tfr.SectorCount = countReg;
        break;
    case ATA_SMART_EXEC_OFFLINE_IMM:
        smartFeatureName = "SMART Offline Immediate";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                              "Sending ATA %s - test %02" PRIX8 "h\n",
                                              smartFeatureName, lbaLo);
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
        ataCommandOptions.tfr.SectorCount = countReg;
        break;
    case ATA_SMART_RTSMART:
        smartFeatureName = "SMART Return Status";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
        ataCommandOptions.tfr.SectorCount = countReg;
        break;
    case ATA_SMART_DISABLE:
        smartFeatureName = "SMART Disable Operations";
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
        ataCommandOptions.tfr.SectorCount = countReg;
        break;
    default:
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", smartFeatureName);
        if (ptrData != M_NULLPTR)
        {
            if (is_Empty(ptrData, dataSize))
            {
                // assume data in (read)
                ataCommandOptions =
                    create_ata_pio_in_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE,
                                          M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
            }
            else
            {
                ataCommandOptions = create_ata_pio_out_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE,
                                                           M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE),
                                                           ptrData, dataSize);
            }
        }
        else
        {
            ataCommandOptions = create_ata_nondata_cmd(device, ATA_SMART_CMD, ATA_CMD_TYPE_TASKFILE, forceRTFRs);
            ataCommandOptions.tfr.SectorCount = countReg;
        }
        break;
    }

    // just sanity sake
    if (ataCommandOptions.commandDirection != XFER_NO_DATA)
    {
        if (!ptrData || dataSize < LEGACY_DRIVE_SEC_SIZE)
        {
            return BAD_PARAMETER;
        }
    }

    ataCommandOptions.tfr.LbaLow = lbaLo;
    ataCommandOptions.tfr.LbaMid = ATA_SMART_SIG_MID;
    ataCommandOptions.tfr.LbaHi  = ATA_SMART_SIG_HI;

    ataCommandOptions.tfr.ErrorFeature = feature;
    ataCommandOptions.timeout          = timeout;
    // special case to try and get around some weird failures in Windows
    if (ataCommandOptions.commandDirection == XFER_DATA_IN)
    {
        // make sure the buffer is cleared to zero
        explicit_zeroes(ptrData, dataSize);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, smartFeatureName, ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Read_Log(const tDevice* M_NONNULL device,
                                                       uint8_t                  logAddress,
                                                       uint8_t* M_NONNULL       ptrData,
                                                       uint32_t                 dataSize)
{
    explicit_zeroes(ptrData, dataSize);
    eReturnValues ret =
        ata_SMART_Command(device, ATA_SMART_READ_LOG, logAddress, ptrData, dataSize, DEFAULT_COMMAND_TIMEOUT, false, 0);
    if (ret == SUCCESS)
    {
        uint32_t invalidSec = UINT32_C(0);
        switch (logAddress)
        {
        case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
        case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
        case ATA_LOG_SMART_SELF_TEST_LOG:
        case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
            // we got data, so validate the checksum
            if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
                print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Warning: Log Checksum is invalid\n");
            }
            break;
        default:
            // don't do anything since not all logs have checksums to validate
            break;
        }
    }
    return ret;
}
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Write_Log(const tDevice* M_NONNULL device,
                                                        uint8_t                  logAddress,
                                                        uint8_t* M_NONNULL       ptrData,
                                                        uint32_t                 dataSize,
                                                        bool                     forceRTFRs)
{
    return ata_SMART_Command(device, ATA_SMART_WRITE_LOG, logAddress, ptrData, dataSize, DEFAULT_COMMAND_TIMEOUT,
                             forceRTFRs, 0);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Offline(const tDevice* M_NONNULL device,
                                                      uint8_t                  subcommand,
                                                      uint32_t                 timeout)
{
    return ata_SMART_Command(device, ATA_SMART_EXEC_OFFLINE_IMM, subcommand, M_NULLPTR, RESERVED, timeout, false,
                             RESERVED);
}

OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Read_Data(const tDevice* M_NONNULL device,
                                                        uint8_t* M_NONNULL       ptrData,
                                                        uint32_t                 dataSize)
{
    explicit_zeroes(ptrData, dataSize);
    eReturnValues ret = ata_SMART_Command(device, ATA_SMART_READ_DATA, RESERVED, ptrData, dataSize,
                                          DEFAULT_COMMAND_TIMEOUT, false, RESERVED);
    if (ret == SUCCESS)
    {
        uint32_t invalidSec = UINT32_C(0);
        // we got data, so validate the checksum
        if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
        {
            ret = WARN_INVALID_CHECKSUM;
            print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Warning: Checksum is invalid\n");
        }
    }
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Return_Status(const tDevice* M_NONNULL device)
{
    return ata_SMART_Command(device, ATA_SMART_RTSMART, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT, true,
                             RESERVED);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Enable_Operations(const tDevice* M_NONNULL device)
{
    return ata_SMART_Command(device, ATA_SMART_ENABLE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT, false,
                             RESERVED);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Disable_Operations(const tDevice* M_NONNULL device)
{
    return ata_SMART_Command(device, ATA_SMART_DISABLE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT, false,
                             RESERVED);
}

OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Read_Thresholds(const tDevice* M_NONNULL device,
                                                              uint8_t* M_NONNULL       ptrData,
                                                              uint32_t                 dataSize)
{
    explicit_zeroes(ptrData, dataSize);
    eReturnValues ret = ata_SMART_Command(device, ATA_SMART_RDATTR_THRESH, RESERVED, ptrData, dataSize,
                                          DEFAULT_COMMAND_TIMEOUT, false, RESERVED);
    if (ret == SUCCESS)
    {
        uint32_t invalidSec = UINT32_C(0);
        // we got data, so validate the checksum
        if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
        {
            ret = WARN_INVALID_CHECKSUM;
            print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Warning: Checksum is invalid\n");
        }
    }
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Save_Attributes(const tDevice* M_NONNULL device)
{
    return ata_SMART_Command(device, ATA_SMART_SAVE_ATTRVALUE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT,
                             false, RESERVED);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Attribute_Autosave(const tDevice* M_NONNULL device, bool enable)
{
    if (enable)
    {
        return ata_SMART_Command(device, ATA_SMART_SW_AUTOSAVE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT,
                                 false, ATA_SMART_ATTRIBUTE_AUTOSAVE_ENABLE_SIG);
    }
    else
    {
        return ata_SMART_Command(device, ATA_SMART_SW_AUTOSAVE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT,
                                 false, ATA_SMART_ATTRIBUTE_AUTOSAVE_DISABLE_SIG);
    }
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Auto_Offline(const tDevice* M_NONNULL device, bool enable)
{
    if (enable)
    {
        return ata_SMART_Command(device, ATA_SMART_AUTO_OFFLINE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT,
                                 false, ATA_SMART_AUTO_OFFLINE_ENABLE_SIG);
    }
    else
    {
        return ata_SMART_Command(device, ATA_SMART_AUTO_OFFLINE, RESERVED, M_NULLPTR, RESERVED, DEFAULT_COMMAND_TIMEOUT,
                                 false, ATA_SMART_AUTO_OFFLINE_DISABLE_SIG);
    }
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Security_Disable_Password(const tDevice* M_NONNULL device,
                                                                  uint8_t* M_NONNULL       ptrData)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, ATA_SECURITY_DISABLE_PASS, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Security Disable Password Command\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);
    print_tDevice_Return_Enum(device, "Security Disable Password", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Security_Erase_Prepare(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SECURITY_ERASE_PREP, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Security Erase Prepare Command\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Security Erase Prepare", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Security_Erase_Unit(const tDevice* M_NONNULL device,
                                                            uint8_t* M_NONNULL       ptrData,
                                                            uint32_t                 timeout)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, ATA_SECURITY_ERASE_UNIT_CMD, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.timeout = timeout;

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Security Erase Unit Command\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Security Erase Unit", ret);

    return ret;
}

M_PARAM_RO(1)
M_PARAM_RO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Security_Set_Password(const tDevice* M_NONNULL device,
                                                              uint8_t* M_NONNULL       ptrData)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_pio_out_cmd(device, ATA_SECURITY_SET_PASS, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Security Set Password Command\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Security Set Password", ret);

    return ret;
}

M_PARAM_RO(1)
M_PARAM_RO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Security_Unlock(const tDevice* M_NONNULL device, uint8_t* M_NONNULL ptrData)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, ATA_SECURITY_UNLOCK_CMD, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Security Unlock Command\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Security Unlock", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Security_Freeze_Lock(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SECURITY_FREEZE_LOCK_CMD, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Security Freeze Lock Command\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Security Erase Freeze Lock", ret);

    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(4)
OPENSEA_TRANSPORT_API eReturnValues ata_Accessible_Max_Address_Feature(const tDevice* M_NONNULL  device,
                                                                       uint16_t                  feature,
                                                                       uint64_t                  lba,
                                                                       ataReturnTFRs* M_NULLABLE rtfrs,
                                                                       uint16_t                  sectorCount)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_ACCESSABLE_MAX_ADDR, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(feature);
    ataCommandOptions.tfr.Feature48    = M_Byte1(feature);
    set_ata_pt_LBA_48(&ataCommandOptions, lba);
    ataCommandOptions.tfr.SectorCount   = M_Byte0(sectorCount);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    if (feature == 0)
    {
        ataCommandOptions.needRTFRs = true;
    }

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Accessible Max Address Command - Feature = 0x%04" PRIX16 ", LBA = %" PRIu64 ", Count = %" PRIu16 "\n",
                                          feature, lba, sectorCount);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (rtfrs != M_NULLPTR)
    {
        safe_memcpy(rtfrs, sizeof(ataReturnTFRs), &(ataCommandOptions.rtfr), sizeof(ataReturnTFRs));
    }

    print_tDevice_Return_Enum(device, "Accessible Max Address Command", ret);

    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Get_Native_Max_Address_Ext(const tDevice* M_NONNULL device,
                                                                   uint64_t* M_NONNULL      nativeMaxLBA)
{
    eReturnValues ret = UNKNOWN;
    ataReturnTFRs rtfrs;
    safe_memset(&rtfrs, sizeof(rtfrs), 0, sizeof(rtfrs));
    ret = ata_Accessible_Max_Address_Feature(device, AMAC_GET_NATIVE_MAX_ADDRESS, 0, &rtfrs, 0);

    if (ret == SUCCESS && nativeMaxLBA != M_NULLPTR)
    {
        *nativeMaxLBA = get_ata_pt_LBA_48_from_rtfr(&rtfrs);
    }

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Accessible_Max_Address_Ext(const tDevice* M_NONNULL device,
                                                                       uint64_t                 newMaxLBA,
                                                                       bool                     changeId)
{
    return ata_Accessible_Max_Address_Feature(device, AMAC_SET_ACCESSIBLE_MAX_ADDRESS, newMaxLBA, M_NULLPTR,
                                              changeId ? 1 : 0);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Freeze_Accessible_Max_Address_Ext(const tDevice* M_NONNULL device)
{
    return ata_Accessible_Max_Address_Feature(device, AMAC_FREEZE_ACCESSIBLE_MAX_ADDRESS, 0, M_NULLPTR, 0);
}

M_PARAM_RO(1)
M_PARAM_WO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Read_Native_Max_Address(const tDevice* M_NONNULL device,
                                                                uint64_t* M_NONNULL      nativeMaxLBA,
                                                                bool                     ext)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ext ? ATA_READ_MAX_ADDRESS_EXT : ATA_READ_MAX_ADDRESS,
                               ext ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE, true);
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    const char* readMaxCmdName = ext ? "Read Native Max Address Ext" : "Read Native Max Address";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readMaxCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS && nativeMaxLBA != M_NULLPTR)
    {
        if (ext)
        {
            *nativeMaxLBA = get_ata_pt_LBA_48(&ataCommandOptions);
        }
        else
        {
            *nativeMaxLBA = get_ata_pt_LBA_28(&ataCommandOptions);
        }
    }

    print_tDevice_Return_Enum(device, readMaxCmdName, ret);
    return ret;
}
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max(const tDevice* M_NONNULL device,
                                                eHPAFeature              setMaxFeature,
                                                uint32_t                 newMaxLBA,
                                                bool                     volatileValue,
                                                uint8_t* M_NULLABLE      ptrData,
                                                uint32_t                 dataLength)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    switch (setMaxFeature)
    {
    case HPA_SET_MAX_ADDRESS:
    case HPA_SET_MAX_FREEZE_LOCK:
    case HPA_SET_MAX_LOCK:
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_SET_MAX, ATA_CMD_TYPE_TASKFILE, false);
        if (volatileValue)
        {
            ataCommandOptions.tfr.SectorCount |= BIT0;
        }
        break;
    case HPA_SET_MAX_UNLOCK:
    case HPA_SET_MAX_PASSWORD:
        ataCommandOptions = create_ata_pio_out_cmd(device, ATA_SET_MAX, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataLength);
        break;
    default:
        return BAD_PARAMETER;
    }
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, setMaxFeature);
    set_ata_pt_LBA_28(&ataCommandOptions, newMaxLBA);

    // Format message with volatility mode
    const char* volatilityStr = volatileValue ? "Volatile" : "Non-Volatile";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Set Max, LBA = %" PRIu32 ", %s\n",
                                          newMaxLBA, volatilityStr);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Set Max", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Address(const tDevice* M_NONNULL device,
                                                        uint32_t                 newMaxLBA,
                                                        bool                     volatileValue)
{
    return ata_Set_Max(device, HPA_SET_MAX_ADDRESS, newMaxLBA, volatileValue, M_NULLPTR, RESERVED);
}

OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Password(const tDevice* M_NONNULL device,
                                                         uint8_t* M_NONNULL       ptrData,
                                                         uint32_t                 dataLength)
{
    return ata_Set_Max(device, HPA_SET_MAX_PASSWORD, RESERVED, false, ptrData, dataLength);
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Lock(const tDevice* M_NONNULL device)
{
    return ata_Set_Max(device, HPA_SET_MAX_LOCK, RESERVED, false, M_NULLPTR, RESERVED);
}

OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Unlock(const tDevice* M_NONNULL device,
                                                       uint8_t* M_NONNULL       ptrData,
                                                       uint32_t                 dataLength)
{
    return ata_Set_Max(device, HPA_SET_MAX_UNLOCK, RESERVED, false, ptrData, dataLength);
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Freeze_Lock(const tDevice* M_NONNULL device)
{
    return ata_Set_Max(device, HPA_SET_MAX_FREEZE_LOCK, RESERVED, false, M_NULLPTR, RESERVED);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Address_Ext(const tDevice* M_NONNULL device,
                                                            uint64_t                 newMaxLBA,
                                                            bool                     volatileValue)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SET_MAX_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    set_ata_pt_LBA_48(&ataCommandOptions, newMaxLBA);
    if (volatileValue)
    {
        ataCommandOptions.tfr.SectorCount |= BIT0;
    }

    const char* volatilityStr = volatileValue ? "Volatile" : "Non-Volatile";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Set Native Max Address Ext, LBA = %" PRIu64 ", %s\n",
                                          newMaxLBA, volatilityStr);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Set Native Max Address Ext", ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Download_Microcode(const tDevice* M_NONNULL   device,
                                                           eDownloadMicrocodeFeatures subCommand,
                                                           uint16_t                   blockCount,
                                                           uint16_t                   bufferOffset,
                                                           bool                       useDMA,
                                                           uint8_t* M_NULLABLE        pData,
                                                           uint32_t                   dataLen,
                                                           bool                       firstSegment,
                                                           bool                       lastSegment,
                                                           uint32_t                   timeoutSeconds)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    if (subCommand == ATA_DL_MICROCODE_ACTIVATE)
    {
        ataCommandOptions = create_ata_nondata_cmd(
            device, useDMA ? ATA_DOWNLOAD_MICROCODE_DMA : ATA_DOWNLOAD_MICROCODE_CMD, ATA_CMD_TYPE_TASKFILE, true);
        // Set need RTFRs because this can help us understand if the new microcode activated correctly - TJE
        ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, subCommand);
    }
    else
    {
        if (useDMA)
        {
            ataCommandOptions = create_ata_dma_out_cmd(device, ATA_DOWNLOAD_MICROCODE_DMA, ATA_CMD_TYPE_TASKFILE,
                                                       M_Byte0(blockCount), pData, dataLen);
        }
        else
        {
            ataCommandOptions = create_ata_pio_out_cmd(device, ATA_DOWNLOAD_MICROCODE_CMD, ATA_CMD_TYPE_TASKFILE,
                                                       M_Byte0(blockCount), pData, dataLen);
        }
        ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, subCommand);
        ataCommandOptions.tfr.LbaLow       = M_Byte1(blockCount);
        ataCommandOptions.tfr.LbaMid       = M_Byte0(bufferOffset);
        ataCommandOptions.tfr.LbaHi        = M_Byte1(bufferOffset);
        ataCommandOptions.fwdlFirstSegment = firstSegment;
        ataCommandOptions.fwdlLastSegment  = lastSegment;

        if (ataCommandOptions.tfr.LbaLow > 0)
        {
            // change length location to TPSIU so the SATL knows how to calculate the transfer length correctly since it
            // is no longer just the sector count field.
            ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
        }
    }

    ataCommandOptions.timeout = timeoutSeconds;
    if (ataCommandOptions.timeout == 0)
    {
#define DEFAULT_FWDL_TIMEOUT (DEFAULT_COMMAND_TIMEOUT * 2)
        ataCommandOptions.timeout =
            DEFAULT_FWDL_TIMEOUT; // using 30 seconds since some firmwares can take a little longer to activate
    }

    const char* fwdlCmdName = useDMA ? "Download Microcode DMA" : "Download Microcode";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA %s, subcommand 0x%" PRIX8 "\n",
                                          fwdlCmdName, C_CAST(uint8_t, subCommand));

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, fwdlCmdName, ret);
    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Check_Power_Mode(const tDevice* M_NONNULL device, uint8_t* M_NONNULL powerMode)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_CHECK_POWER_MODE_CMD, ATA_CMD_TYPE_TASKFILE, true);

    if (powerMode == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Check Power Mode\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        *powerMode = ataCommandOptions.rtfr.secCnt;
    }

    print_tDevice_Return_Enum(device, "Check Power Mode", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Configure_Stream(const tDevice* M_NONNULL device,
                                                         uint8_t                  streamID,
                                                         bool                     addRemoveStreamBit,
                                                         bool                     readWriteStreamBit,
                                                         uint8_t                  defaultCCTL,
                                                         uint16_t                 allocationUnit)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_CONFIGURE_STREAM, ATA_CMD_TYPE_EXTENDED_TASKFILE, true);
    ataCommandOptions.tfr.SectorCount   = M_Byte0(allocationUnit);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(allocationUnit);
    // set default cctl
    ataCommandOptions.tfr.Feature48 = defaultCCTL;
    // set stream ID
    ataCommandOptions.tfr.ErrorFeature = streamID & 0x03; // stream ID is specified by bits 2:0
    // set add/remove stream bit
    if (addRemoveStreamBit)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT7;
    }
    // set the read/Write stream bit (obsolete starting in ATA8/ACS)
    if (readWriteStreamBit)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT6;
    }
    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Configure Stream\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Configure Stream", ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Data_Set_Management(const tDevice* M_NONNULL device,
                                                            bool                     trimBit,
                                                            uint8_t* M_NONNULL       ptrData,
                                                            uint32_t                 dataSize,
                                                            bool                     xl)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_dma_out_cmd(
        device, xl ? ATA_DATA_SET_MANAGEMENT_XL_CMD : ATA_DATA_SET_MANAGEMENT_CMD, ATA_CMD_TYPE_EXTENDED_TASKFILE,
        M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    // set the TRIM bit if asked
    if (trimBit)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT0;
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* dsmCmdName = xl ? "Data Set Management XL" : "Data Set Management";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA %s\n",
                                          dsmCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, dsmCmdName, ret);

    return ret;
}
M_PARAM_RO(1)
M_PARAM_WO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Execute_Device_Diagnostic(const tDevice* M_NONNULL device,
                                                                  uint8_t* M_NONNULL       diagnosticCode)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_dev_diag_cmd(device, ATA_EXEC_DRV_DIAG, ATA_CMD_TYPE_TASKFILE);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Execute Device Diagnostic\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    // according to the spec, this command should always complete without error
    *diagnosticCode = ataCommandOptions.rtfr.error;

    print_tDevice_Return_Enum(device, "Data Execute Device Diagnostic", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Flush_Cache(const tDevice* M_NONNULL device, bool extendedCommand)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, extendedCommand ? ATA_FLUSH_CACHE_EXT : ATA_FLUSH_CACHE_CMD,
                               extendedCommand ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE, false);
    ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    // Changed from 45 seconds to max command timeout to make sure this has enough time to complete without the system
    // sending a reset. The spec mentions this can take up to 30 minutes, but that is likely a rare condition. It should
    // usually complete faster than that on today's drives - TJE

    const char* flushCacheCmdName = extendedCommand ? "Flush Cache Ext" : "Flush Cache";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", flushCacheCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, flushCacheCmdName, ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Idle(const tDevice* M_NONNULL device, uint8_t standbyTimerPeriod)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_IDLE_CMD, ATA_CMD_TYPE_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount = standbyTimerPeriod;
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Idle, standby timer = %" PRIX8 "h\n",
                                          standbyTimerPeriod);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Idle", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Idle_Immediate(const tDevice* M_NONNULL device, bool unloadFeature)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_IDLE_IMMEDIATE_CMD, ATA_CMD_TYPE_TASKFILE, false);
    if (unloadFeature)
    {
        ataCommandOptions.tfr.ErrorFeature = IDLE_IMMEDIATE_UNLOAD_FEATURE;
        ataCommandOptions.tfr.SectorCount  = 0x00;
        set_ata_pt_LBA_28_sig(&ataCommandOptions, IDLE_IMMEDIATE_UNLOAD_LBA);
        // NOTE: RTFR's set C4h in LBA lo on success. Not currently looking for this -TJE
    }

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Idle Immediate %s\n",
                                          (unloadFeature ? " - Unload" : ""));

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Idle Immediate", ret);
    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Read_Buffer(const tDevice* M_NONNULL device,
                                                    uint8_t* M_NONNULL       ptrData,
                                                    bool                     useDMA)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    explicit_zeroes(ptrData, LEGACY_DRIVE_SEC_SIZE);
    if (useDMA)
    {
        ataCommandOptions =
            create_ata_dma_in_cmd(device, ATA_READ_BUF_DMA, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);
    }
    else
    {
        ataCommandOptions =
            create_ata_pio_in_cmd(device, ATA_READ_BUF, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* readBufName = useDMA ? "Read Buffer DMA" : "Read Buffer";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readBufName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readBufName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Read_DMA(const tDevice* M_NONNULL device,
                                                 uint64_t                 LBA,
                                                 uint8_t* M_NONNULL       ptrData,
                                                 M_ATTR_UNUSED uint16_t   sectorCount,
                                                 uint32_t                 dataSize,
                                                 bool                     extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_dma_read_lba_cmd(
        device, extendedCmd ? ATA_READ_DMA_EXT : ATA_READ_DMA_RETRY_CMD,
        extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), extendedCmd), LBA, ptrData,
        dataSize);

    M_USE_UNUSED(sectorCount);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* readDmaExtName = extendedCmd ? "Read DMA Ext" : "Read DMA";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readDmaExtName);

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readDmaExtName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Read_Multiple(const tDevice* M_NONNULL device,
                                                      uint64_t                 LBA,
                                                      uint8_t* M_NONNULL       ptrData,
                                                      M_ATTR_UNUSED uint16_t   sectorCount,
                                                      uint32_t                 dataSize,
                                                      bool                     extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_read_lba_cmd(
        device, extendedCmd ? ATA_READ_READ_MULTIPLE_EXT : ATA_READ_MULTIPLE_CMD,
        extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), extendedCmd), LBA, ptrData,
        dataSize);
    set_ata_pt_multipleCount(&ataCommandOptions, device);

    M_USE_UNUSED(sectorCount);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* readMultipleCmdName = extendedCmd ? "Read Multiple Ext" : "Read Multiple";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readMultipleCmdName);

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readMultipleCmdName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Read_Sectors(const tDevice* M_NONNULL device,
                                                     uint64_t                 LBA,
                                                     uint8_t* M_NONNULL       ptrData,
                                                     M_ATTR_UNUSED uint16_t   sectorCount,
                                                     uint32_t                 dataSize,
                                                     bool                     extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_read_lba_cmd(
        device, extendedCmd ? ATA_READ_SECT_EXT : ATA_READ_SECT,
        extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), extendedCmd), LBA, ptrData,
        dataSize);

    M_USE_UNUSED(sectorCount);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* readSectorsCmdName = extendedCmd ? "Read Sectors Ext" : "Read Sectors";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readSectorsCmdName);

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readSectorsCmdName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Read_Sectors_No_Retry(const tDevice* M_NONNULL device,
                                                              uint64_t                 LBA,
                                                              uint8_t* M_NONNULL       ptrData,
                                                              uint16_t                 sectorCount,
                                                              uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;
    explicit_zeroes(ptrData, dataSize);
    ataPassthroughCommand ataCommandOptions = create_ata_pio_read_lba_cmd(
        device, ATA_READ_SECT_NORETRY, ATA_CMD_TYPE_TASKFILE, sectorCount, LBA, ptrData, dataSize);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Read Sectors(No Retry)\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Read Sectors(No Retry)", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Read_Stream_Ext(const tDevice* M_NONNULL device,
                                                        bool                     useDMA,
                                                        uint8_t                  streamID,
                                                        bool                     notSequential,
                                                        bool                     readContinuous,
                                                        uint8_t                  commandCCTL,
                                                        uint64_t                 LBA,
                                                        uint8_t* M_NONNULL       ptrData,
                                                        uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    if (useDMA)
    {
        ataCommandOptions = create_ata_dma_in_cmd(
            device, ATA_READ_STREAM_DMA_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE,
            get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), true), ptrData, dataSize);
    }
    else
    {
        ataCommandOptions = create_ata_pio_in_cmd(
            device, ATA_READ_STREAM_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE,
            get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), true), ptrData, dataSize);
    }
    set_ata_pt_LBA_48(&ataCommandOptions, LBA);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    // set the stream ID
    ataCommandOptions.tfr.ErrorFeature = streamID & 0x07;

    if (notSequential)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT5;
    }

    if (readContinuous)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT6;
    }

    ataCommandOptions.tfr.Feature48 = commandCCTL;

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* readStreamName = useDMA ? "Read Stream Ext DMA" : "Read Stream Ext";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readStreamName);

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readStreamName, ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Read_Verify_Sectors(const tDevice* M_NONNULL device,
                                                            bool                     extendedCmd,
                                                            uint16_t                 numberOfSectors,
                                                            uint64_t                 LBA)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, extendedCmd ? ATA_READ_VERIFY_EXT : ATA_READ_VERIFY_RETRY,
                               extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE, false);
    if (extendedCmd)
    {
        set_ata_pt_LBA_48(&ataCommandOptions, LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfSectors);
    }
    else
    {
        set_ata_pt_LBA_28(&ataCommandOptions, M_DoubleWord0(LBA));
    }
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);

    const char* readVerifyCmdName = extendedCmd ? "Read Verify Sectors Ext" : "Read Verify Sectors";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readVerifyCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readVerifyCmdName, ret);

    return ret;
}

M_PARAM_RO(1)
M_PARAM_WO(2)
M_PARAM_WO(3)
M_PARAM_WO(4)
OPENSEA_TRANSPORT_API eReturnValues ata_Request_Sense_Data(const tDevice* M_NONNULL device,
                                                           uint8_t* M_NONNULL       senseKey,
                                                           uint8_t* M_NONNULL       additionalSenseCode,
                                                           uint8_t* M_NONNULL       additionalSenseCodeQualifier)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_REQUEST_SENSE_DATA, ATA_CMD_TYPE_EXTENDED_TASKFILE, true);

    if (senseKey == M_NULLPTR || additionalSenseCode == M_NULLPTR || additionalSenseCodeQualifier == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Request Sense Data\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        *senseKey                     = M_Nibble0(ataCommandOptions.rtfr.lbaHi);
        *additionalSenseCode          = ataCommandOptions.rtfr.lbaMid;
        *additionalSenseCodeQualifier = ataCommandOptions.rtfr.lbaLow;
    }

    print_tDevice_Return_Enum(device, "Request Sense Data", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Date_And_Time(const tDevice* M_NONNULL device, uint64_t timeStamp)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SET_DATE_AND_TIME_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    set_ata_pt_LBA_48_sig(&ataCommandOptions, timeStamp);

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Set Data & Time Ext - time stamp: %016" PRIX64 "h\n",
                                          timeStamp);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Set Data & Time Ext", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Multiple_Mode(const tDevice* M_NONNULL device, uint8_t drqDataBlockCount)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SET_MULTIPLE, ATA_CMD_TYPE_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount = drqDataBlockCount;

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Set Multiple Mode - DRQ block count: %" PRIu8 "\n",
                                          drqDataBlockCount);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Set Multiple Mode", ret);

    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Sleep(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SLEEP_CMD, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Sleep\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Sleep", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Standby(const tDevice* M_NONNULL device, uint8_t standbyTimerPeriod)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_STANDBY_CMD, ATA_CMD_TYPE_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount = standbyTimerPeriod;

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Standby, standby timer - %" PRIX8 "h\n",
                                          standbyTimerPeriod);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Standby", ret);

    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Standby_Immediate(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_STANDBY_IMMD, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Standby Immediate\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Standby Immediate", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Trusted_Non_Data(const tDevice* M_NONNULL device,
                                                         uint8_t                  securityProtocol,
                                                         bool                     trustedSendReceiveBit,
                                                         uint16_t                 securityProtocolSpecific)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_TRUSTED_NON_DATA, ATA_CMD_TYPE_TASKFILE, false);
    ataCommandOptions.tfr.ErrorFeature = securityProtocol;
    ataCommandOptions.tfr.LbaMid       = M_Byte0(securityProtocolSpecific);
    ataCommandOptions.tfr.LbaHi        = M_Byte1(securityProtocolSpecific);
    if (trustedSendReceiveBit)
    {
        ataCommandOptions.tfr.DeviceHead |= BIT0; // LBA bit 24
    }

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Trusted Non-Data - Security Protocol %02" PRIX8 ", Specific: %04" PRIX16 "\n",
                                          securityProtocol, securityProtocolSpecific);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Trusted Non-Data", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Trusted_Receive(const tDevice* M_NONNULL device,
                                                        bool                     useDMA,
                                                        uint8_t                  securityProtocol,
                                                        uint16_t                 securityProtocolSpecific,
                                                        uint8_t* M_NONNULL       ptrData,
                                                        uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    explicit_zeroes(ptrData, dataSize);
    if (useDMA)
    {
        ataCommandOptions =
            create_ata_dma_in_cmd(device, ATA_TRUSTED_RECEIVE_DMA, ATA_CMD_TYPE_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    else
    {
        ataCommandOptions =
            create_ata_pio_in_cmd(device, ATA_TRUSTED_RECEIVE, ATA_CMD_TYPE_TASKFILE,
                                  M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    ataCommandOptions.tfr.ErrorFeature = securityProtocol;
    ataCommandOptions.tfr.LbaLow       = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaMid       = M_Byte0(securityProtocolSpecific);
    ataCommandOptions.tfr.LbaHi        = M_Byte1(securityProtocolSpecific);
    if (ataCommandOptions.tfr.LbaLow > 0)
    {
        // change length location to TPSIU so the SATL knows how to calculate the transfer length correctly since it is
        // no longer just the sector count field.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
        ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;
    }

    // Determine command variant
    const char* commandStr = useDMA ? "ATA Trusted Receive DMA" : "ATA Trusted Receive";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending %s - Security Protocol %02" PRIX8 ", Specific: %04" PRIX16 "\n",
                                          commandStr, securityProtocol, securityProtocolSpecific);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, commandStr, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Trusted_Send(const tDevice* M_NONNULL device,
                                                     bool                     useDMA,
                                                     uint8_t                  securityProtocol,
                                                     uint16_t                 securityProtocolSpecific,
                                                     uint8_t* M_NONNULL       ptrData,
                                                     uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    if (useDMA)
    {
        ataCommandOptions =
            create_ata_dma_out_cmd(device, ATA_TRUSTED_SEND_DMA, ATA_CMD_TYPE_TASKFILE,
                                   M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    else
    {
        ataCommandOptions =
            create_ata_pio_out_cmd(device, ATA_TRUSTED_SEND, ATA_CMD_TYPE_TASKFILE,
                                   M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    }
    ataCommandOptions.tfr.ErrorFeature = securityProtocol;
    ataCommandOptions.tfr.LbaLow       = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaMid       = M_Byte0(securityProtocolSpecific);
    ataCommandOptions.tfr.LbaHi        = M_Byte1(securityProtocolSpecific);
    if (ataCommandOptions.tfr.LbaLow > 0)
    {
        // change length location to TPSIU so the SATL knows how to calculate the transfer length correctly since it is
        // no longer just the sector count field.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
        ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;
    }

    // Determine command variant
    const char* commandStr = useDMA ? "ATA Trusted Send DMA" : "ATA Trusted Send";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending %s - Security Protocol %02" PRIX8 ", Specific: %04" PRIX16 "\n",
                                          commandStr, securityProtocol, securityProtocolSpecific);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, commandStr, ret);

    return ret;
}

M_PARAM_RO(1)
M_PARAM_RO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_Write_Buffer(const tDevice* M_NONNULL device,
                                                     uint8_t* M_NONNULL       ptrData,
                                                     bool                     useDMA)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    if (useDMA)
    {
        ataCommandOptions =
            create_ata_dma_out_cmd(device, ATA_WRITE_BUF_DMA, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);
    }
    else
    {
        ataCommandOptions =
            create_ata_pio_out_cmd(device, ATA_WRITE_BUF, ATA_CMD_TYPE_TASKFILE, 1, ptrData, LEGACY_DRIVE_SEC_SIZE);
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* writeBufName = useDMA ? "Write Buffer DMA" : "Write Buffer";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", writeBufName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeBufName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_DMA(const tDevice* M_NONNULL device,
                                                  uint64_t                 LBA,
                                                  uint8_t* M_NONNULL       ptrData,
                                                  uint32_t                 dataSize,
                                                  bool                     extendedCmd,
                                                  bool                     fua)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_dma_write_lba_cmd(
        device, extendedCmd ? (fua ? ATA_WRITE_DMA_FUA_EXT : ATA_WRITE_DMA_EXT) : ATA_WRITE_DMA_RETRY_CMD,
        extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), extendedCmd), LBA, ptrData,
        dataSize);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* writeDmaExtName = extendedCmd ? "Write DMA Ext" : "Write DMA";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", writeDmaExtName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeDmaExtName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_Multiple(const tDevice* M_NONNULL device,
                                                       uint64_t                 LBA,
                                                       uint8_t* M_NONNULL       ptrData,
                                                       uint32_t                 dataSize,
                                                       bool                     extendedCmd,
                                                       bool                     fua)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_read_lba_cmd(
        device, extendedCmd ? (fua ? ATA_WRITE_MULTIPLE_FUA_EXT : ATA_WRITE_MULTIPLE_EXT) : ATA_WRITE_MULTIPLE_CMD,
        extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), extendedCmd), LBA, ptrData,
        dataSize);
    set_ata_pt_multipleCount(&ataCommandOptions, device);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* writeMultipleCmdName = extendedCmd ? "Write Multiple Ext" : "Write Multiple";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", writeMultipleCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeMultipleCmdName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_Sectors(const tDevice* M_NONNULL device,
                                                      uint64_t                 LBA,
                                                      uint8_t* M_NONNULL       ptrData,
                                                      uint32_t                 dataSize,
                                                      bool                     extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_write_lba_cmd(
        device, extendedCmd ? ATA_WRITE_SECT_EXT : ATA_WRITE_SECT,
        extendedCmd ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), extendedCmd), LBA, ptrData,
        dataSize);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* writeSectorsCmdName = extendedCmd ? "Write Sectors Ext" : "Write Sectors";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", writeSectorsCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeSectorsCmdName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_Sectors_No_Retry(const tDevice* M_NONNULL device,
                                                               uint64_t                 LBA,
                                                               uint8_t* M_NONNULL       ptrData,
                                                               uint32_t                 dataSize)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_write_lba_cmd(
        device, ATA_WRITE_SECT_NORETRY, ATA_CMD_TYPE_TASKFILE,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), false), LBA, ptrData,
        dataSize);

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Write Sectors(No Retry)\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Write Sectors(No Retry)", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_Stream_Ext(const tDevice* M_NONNULL device,
                                                         bool                     useDMA,
                                                         uint8_t                  streamID,
                                                         bool                     flush,
                                                         bool                     writeContinuous,
                                                         uint8_t                  commandCCTL,
                                                         uint64_t                 LBA,
                                                         uint8_t* M_NONNULL       ptrData,
                                                         uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    if (useDMA)
    {
        ataCommandOptions = create_ata_dma_out_cmd(
            device, ATA_WRITE_STREAM_DMA_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE,
            get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), true), ptrData, dataSize);
    }
    else
    {
        ataCommandOptions = create_ata_pio_out_cmd(
            device, ATA_WRITE_STREAM_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE,
            get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), true), ptrData, dataSize);
    }
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_LBA_48(&ataCommandOptions, LBA);
    // set the stream ID
    ataCommandOptions.tfr.ErrorFeature = streamID & 0x07;

    if (flush)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT5;
    }

    if (writeContinuous)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT6;
    }

    ataCommandOptions.tfr.Feature48 = commandCCTL;

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    const char* writeStreamName = useDMA ? "Write Stream Ext DMA" : "Write Stream Ext";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", writeStreamName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeStreamName, ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Write_Uncorrectable(const tDevice* M_NONNULL device,
                                                            uint8_t                  unrecoverableOptions,
                                                            uint16_t                 numberOfSectors,
                                                            uint64_t                 LBA)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_WRITE_UNCORRECTABLE_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    set_ata_pt_LBA_48(&ataCommandOptions, LBA);
    ataCommandOptions.tfr.SectorCount   = M_Byte0(numberOfSectors);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfSectors);
    ataCommandOptions.tfr.ErrorFeature  = unrecoverableOptions;

    if (unrecoverableOptions != WRITE_UNCORRECTABLE_PSEUDO_UNCORRECTABLE_WITH_LOGGING &&
        unrecoverableOptions != WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_5AH &&
        unrecoverableOptions != WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_A5H &&
        unrecoverableOptions != WRITE_UNCORRECTABLE_FLAGGED_WITHOUT_LOGGING)
    {
        return BAD_PARAMETER;
    }

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Write Uncorrectable Ext - %02" PRIX8 "h, LBA = %" PRIu64 ", Count: %" PRIu16 "\n",
                                          unrecoverableOptions, LBA, numberOfSectors);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Write Uncorrectable Ext", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Feature(const tDevice* M_NONNULL device,
                                                         eNVCacheFeatures         feature,
                                                         uint16_t                 count,
                                                         uint64_t                 LBA,
                                                         uint8_t* M_NULLABLE      ptrData,
                                                         uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    const char*           nvCacheFeature = M_NULLPTR;
    switch (feature)
    {
    case NV_SET_NV_CACHE_POWER_MODE:
        nvCacheFeature    = "Non-Volatile Cache - Set NV Cache Power Mode";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        break;
    case NV_RETURN_FROM_NV_CACHE_POWER_MODE:
        nvCacheFeature    = "Non-Volatile Cache - Return from NV Cache Power Mode";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        break;
    case NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET:
        nvCacheFeature = "Non-Volatile Cache - Add LBA(s) to NV Cache Pinned Set";
        ataCommandOptions =
            create_ata_dma_out_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, count, ptrData, dataSize);
        break;
    case NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET:
        nvCacheFeature = "Non-Volatile Cache - Remove LBA(s) from NV Cache Pinned Set";
        if (LBA & BIT0)
        {
            // NOTE: If we need "Number unpinned remaining" change to needing RTFRs
            ataCommandOptions = create_ata_nondata_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        }
        else
        {
            ataCommandOptions =
                create_ata_dma_out_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, count, ptrData, dataSize);
        }
        break;
    case NV_QUERY_NV_CACHE_PINNED_SET:
        nvCacheFeature = "Non-Volatile Cache - Query NV Cache Pinned Set";
        ataCommandOptions =
            create_ata_dma_in_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, count, ptrData, dataSize);
        break;
    case NV_QUERY_NV_CACHE_MISSES:
        nvCacheFeature = "Non-Volatile Cache - Query NV Cache Misses";
        ataCommandOptions =
            create_ata_dma_in_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, count, ptrData, dataSize);
        break;
    case NV_FLUSH_NV_CACHE:
        // NOTE: If we need "Number unpinned remaining" change to needing RTFRs
        nvCacheFeature    = "Non-Volatile Cache - Flush NV Cache";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        break;
    case NV_CACHE_ENABLE:
        nvCacheFeature    = "Non-Volatile Cache - NV Cache Enable";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        break;
    case NV_CACHE_DISABLE:
        nvCacheFeature    = "Non-Volatile Cache - NV Cache Disable";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_NV_CACHE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        break;
    default:
        nvCacheFeature = "Non-Volatile Cache - Unknown NV Cache feature";
        break;
    }
    set_ata_pt_LBA_48_sig(&ataCommandOptions, LBA);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(C_CAST(uint16_t, feature));
    ataCommandOptions.tfr.Feature48    = M_Byte1(C_CAST(uint16_t, feature));

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", nvCacheFeature);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, nvCacheFeature, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Add_LBAs_To_Cache(const tDevice* M_NONNULL device,
                                                                   bool                     populateImmediately,
                                                                   uint8_t* M_NONNULL       ptrData,
                                                                   uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;
    uint64_t      lba = UINT64_C(0);
    if (populateImmediately)
    {
        lba |= BIT0;
    }
    ret = ata_NV_Cache_Feature(device, NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET,
                               C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), lba, ptrData, dataSize);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_NV_Flush_NV_Cache(const tDevice* M_NONNULL device,
                                                          uint32_t                 minNumberOfLogicalBlocks)
{
    return ata_NV_Cache_Feature(device, NV_FLUSH_NV_CACHE, 0, C_CAST(uint64_t, minNumberOfLogicalBlocks), M_NULLPTR, 0);
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Disable(const tDevice* M_NONNULL device)
{
    return ata_NV_Cache_Feature(device, NV_CACHE_DISABLE, 0, 0, M_NULLPTR, 0);
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Enable(const tDevice* M_NONNULL device)
{
    return ata_NV_Cache_Feature(device, NV_CACHE_ENABLE, 0, 0, M_NULLPTR, 0);
}

M_PARAM_RO(1)
M_PARAM_WO(2)
OPENSEA_TRANSPORT_API eReturnValues ata_NV_Query_Misses(const tDevice* M_NONNULL device, uint8_t* M_NONNULL ptrData)
{
    explicit_zeroes(ptrData, 512);
    return ata_NV_Cache_Feature(device, NV_QUERY_NV_CACHE_MISSES, 0x0001, 0, ptrData, 512);
}

OPENSEA_TRANSPORT_API eReturnValues ata_NV_Query_Pinned_Set(const tDevice* M_NONNULL device,
                                                            uint64_t                 dataBlockNumber,
                                                            uint8_t* M_NONNULL       ptrData,
                                                            uint32_t                 dataSize)
{
    explicit_zeroes(ptrData, dataSize);
    return ata_NV_Cache_Feature(device, NV_QUERY_NV_CACHE_PINNED_SET,
                                C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), dataBlockNumber, ptrData, dataSize);
}

OPENSEA_TRANSPORT_API eReturnValues ata_NV_Remove_LBAs_From_Cache(const tDevice* M_NONNULL device,
                                                                  bool                     unpinAll,
                                                                  uint8_t* M_NONNULL       ptrData,
                                                                  uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;

    uint64_t lba = UINT64_C(0);

    if (unpinAll)
    {
        lba |= BIT0;
        ret = ata_NV_Cache_Feature(device, NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET, 0, lba, M_NULLPTR, 0);
    }
    else
    {
        ret = ata_NV_Cache_Feature(device, NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET,
                                   C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), lba, ptrData, dataSize);
    }

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Features(const tDevice* M_NONNULL device,
                                                     uint8_t                  subcommand,
                                                     uint8_t                  subcommandCountField,
                                                     uint8_t                  subcommandLBALo,
                                                     uint8_t                  subcommandLBAMid,
                                                     uint16_t                 subcommandLBAHi)
{
    eReturnValues ret = UNKNOWN;
    // NOTE: Set need RTFRs to true for now since it is not clear which feature may or may not need them...generally
    // this is probably not needed-TJE
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SET_FEATURE, ATA_CMD_TYPE_TASKFILE, true);
    ataCommandOptions.tfr.LbaLow = subcommandLBALo;
    ataCommandOptions.tfr.LbaMid = subcommandLBAMid;
    ataCommandOptions.tfr.LbaHi  = M_Byte0(subcommandLBAHi);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble2(subcommandLBAHi);
    ataCommandOptions.tfr.SectorCount  = subcommandCountField;
    ataCommandOptions.tfr.ErrorFeature = subcommand;

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Set Features - subcommand %02" PRIX8 "h\n",
                                          subcommand);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Set Features", ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Identify_Packet_Device(const tDevice* M_NONNULL device,
                                                               uint8_t* M_NONNULL       ptrData,
                                                               uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;
    explicit_zeroes(ptrData, dataSize);
    ataPassthroughCommand ataCommandOptions =
        create_ata_pio_in_cmd(device, ATAPI_IDENTIFY, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataSize);
    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Identify Packet Device\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        // copy the data to the device structure so that it's not (as) stale
        copy_ata_identify_to_tdevice(M_CONST_CAST(tDevice*, device), ptrData);
    }

    if (ret == SUCCESS)
    {
        if (ptrData[510] == ATA_CHECKSUM_VALIDITY_INDICATOR)
        {
            // we got data, so validate the checksum
            uint32_t invalidSec = UINT32_C(0);
            if (!is_Checksum_Valid(ptrData, LEGACY_DRIVE_SEC_SIZE, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
            }
        }
        else
        {
            // Don't do anything. Device doesn't use a checksum
        }
    }

    print_tDevice_Return_Enum(device, "Identify Packet Device", ret);
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Device_Configuration_Overlay_Feature(const tDevice* M_NONNULL device,
                                                                             eDCOFeatures             dcoFeature,
                                                                             uint8_t* M_NULLABLE      ptrData,
                                                                             uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    const char* dcoFeatureString = M_NULLPTR;
    switch (dcoFeature)
    {
    case DCO_RESTORE:
        dcoFeatureString  = "Device Configuration Overlay Command - Restore";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_DCO, ATA_CMD_TYPE_TASKFILE, false);
        break;
    case DCO_FREEZE_LOCK:
        dcoFeatureString  = "Device Configuration Overlay Command - Freeze Lock";
        ataCommandOptions = create_ata_nondata_cmd(device, ATA_DCO, ATA_CMD_TYPE_TASKFILE, false);
        break;
    case DCO_IDENTIFY:
        dcoFeatureString  = "Device Configuration Overlay Command - Identify";
        ataCommandOptions = create_ata_pio_in_cmd(device, ATA_DCO, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataSize);
        break;
    case DCO_SET:
        dcoFeatureString  = "Device Configuration Overlay Command - Set";
        ataCommandOptions = create_ata_pio_out_cmd(device, ATA_DCO, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataSize);
        break;
    case DCO_IDENTIFY_DMA:
        dcoFeatureString  = "Device Configuration Overlay Command - Identify DMA";
        ataCommandOptions = create_ata_dma_in_cmd(device, ATA_DCO, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataSize);
        break;
    case DCO_SET_DMA:
        dcoFeatureString  = "Device Configuration Overlay Command - Set DMA";
        ataCommandOptions = create_ata_dma_out_cmd(device, ATA_DCO, ATA_CMD_TYPE_TASKFILE, 1, ptrData, dataSize);
        break;
    default:
        dcoFeatureString = "Device Configuration Overlay - Unknown feature";
        break;
    }
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, dcoFeature);

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", dcoFeatureString);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, dcoFeatureString, ret);

    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Restore(const tDevice* M_NONNULL device)
{
    return ata_Device_Configuration_Overlay_Feature(device, DCO_RESTORE, M_NULLPTR, 0);
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Freeze_Lock(const tDevice* M_NONNULL device)
{
    return ata_Device_Configuration_Overlay_Feature(device, DCO_FREEZE_LOCK, M_NULLPTR, 0);
}

OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Identify(const tDevice* M_NONNULL device,
                                                     bool                     useDMA,
                                                     uint8_t* M_NONNULL       ptrData,
                                                     uint32_t                 dataSize)
{
    explicit_zeroes(ptrData, dataSize);
    eReturnValues ret =
        ata_Device_Configuration_Overlay_Feature(device, useDMA ? DCO_IDENTIFY_DMA : DCO_IDENTIFY, ptrData, dataSize);
    if (ret == SUCCESS)
    {
        if (ptrData[510] == ATA_CHECKSUM_VALIDITY_INDICATOR)
        {
            // we got data, so validate the checksum
            uint32_t invalidSec = UINT32_C(0);
            if (!is_Checksum_Valid(ptrData, LEGACY_DRIVE_SEC_SIZE, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
            }
        }
        else
        {
            // don't do anything. Device doesn't use a checksum
        }
    }
    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Set(const tDevice* M_NONNULL device,
                                                bool                     useDMA,
                                                uint8_t* M_NONNULL       ptrData,
                                                uint32_t                 dataSize)
{
    return ata_Device_Configuration_Overlay_Feature(device, useDMA ? DCO_SET_DMA : DCO_SET, ptrData, dataSize);
}

// eReturnValues ata_Packet(const tDevice *device, uint8_t *scsiCDB, bool dmaBit, bool dmaDirBit, uint16_t
// byteCountLimit, uint8_t *ptrData, uint32_t *dataSize)
//{
//     ataPassthroughCommand packetCommand;
//     safe_memset(&packetCommand, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
//     packetCommand.commadProtocol = ATA_CMD_TYPE_PACKET;
//     packetCommand.tfr.CommandStatus = ATAPI_COMMAND;
//     if (dmaBit)
//     {
//         packetCommand.tfr.ErrorFeature |= BIT0;
//     }
//     if (dmaDirBit)
//     {
//         packetCommand.tfr.ErrorFeature |= BIT2;
//     }
// }

M_PARAM_RO(1)
M_NONNULL_IF_NONZERO_PARAM(8, 9)
M_PARAM_WO_SIZE(8, 9)
OPENSEA_TRANSPORT_API eReturnValues ata_ZAC_Management_In(const tDevice* M_NONNULL device,
                                                          eZMAction                action,
                                                          uint8_t                  actionSpecificFeatureExt,
                                                          uint8_t                  actionSpecificFeatureBits,
                                                          uint16_t                 returnPageCount,
                                                          uint64_t                 actionSpecificLBA,
                                                          uint16_t                 actionSpecificAUX,
                                                          uint8_t* M_NULLABLE      ptrData,
                                                          uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    explicit_zeroes(ptrData, dataSize);
    switch (action)
    {
    case ZM_ACTION_REPORT_ZONES:
    case ZM_ACTION_REPORT_REALMS:
    case ZM_ACTION_REPORT_ZONE_DOMAINS:
    case ZM_ACTION_ZONE_ACTIVATE:
    case ZM_ACTION_ZONE_QUERY:
        ataCommandOptions = create_ata_dma_in_cmd(device, ATA_ZONE_MANAGEMENT_IN, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                                                  returnPageCount, ptrData, dataSize);
        break;
    default: // Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
    }
    set_ata_pt_LBA_48(&ataCommandOptions, actionSpecificLBA);
    ataCommandOptions.tfr.Feature48    = actionSpecificFeatureExt;
    ataCommandOptions.tfr.ErrorFeature = M_STATIC_CAST(uint8_t, action) | (actionSpecificFeatureBits & UINT8_C(0xE0));
    if (actionSpecificAUX)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_COMPLETE_TASKFILE;
        ataCommandOptions.tfr.aux4    = M_Byte1(actionSpecificAUX);
        ataCommandOptions.tfr.aux3    = M_Byte0(actionSpecificAUX);
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Zone Management In\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Zone Management In", ret);

    return ret;
}

M_PARAM_RO(1)
M_NONNULL_IF_NONZERO_PARAM(7, 8)
M_PARAM_RO_SIZE(7, 8)
OPENSEA_TRANSPORT_API eReturnValues ata_ZAC_Management_Out(const tDevice* M_NONNULL device,
                                                           eZMAction                action,
                                                           uint8_t                  actionSpecificFeatureExt,
                                                           uint16_t                 pagesToSend_ActionSpecific,
                                                           uint64_t                 actionSpecificLBA,
                                                           uint16_t                 actionSpecificAUX,
                                                           uint8_t* M_NULLABLE      ptrData,
                                                           uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    switch (action)
    {
    case ZM_ACTION_CLOSE_ZONE:
    case ZM_ACTION_FINISH_ZONE:
    case ZM_ACTION_OPEN_ZONE:
    case ZM_ACTION_RESET_WRITE_POINTERS:
    case ZM_ACTION_SEQUENTIALIZE_ZONE:
        ataCommandOptions =
            create_ata_nondata_cmd(device, ATA_ZONE_MANAGEMENT_OUT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        break;
    default: // Need to add new zm actions as they are defined in the spec
        if (ptrData && dataSize > 0)
        {
            ataCommandOptions = create_ata_dma_out_cmd(device, ATA_ZONE_MANAGEMENT_OUT, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                                                       pagesToSend_ActionSpecific, ptrData, dataSize);
        }
        else
        {
            ataCommandOptions =
                create_ata_nondata_cmd(device, ATA_ZONE_MANAGEMENT_OUT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
        }
        break;
    }
    set_ata_pt_LBA_48(&ataCommandOptions, actionSpecificLBA);
    ataCommandOptions.tfr.Feature48    = actionSpecificFeatureExt;
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, action);
    if (actionSpecificAUX)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_COMPLETE_TASKFILE;
        ataCommandOptions.tfr.aux4    = M_Byte1(actionSpecificAUX);
        ataCommandOptions.tfr.aux3    = M_Byte0(actionSpecificAUX);
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Zone Management Out\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Zone Management Out", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Close_Zone_Ext(const tDevice* M_NONNULL device,
                                                       bool                     closeAll,
                                                       uint64_t                 zoneID,
                                                       uint16_t                 zoneCount)
{
    if (closeAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_CLOSE_ZONE, BIT0, zoneCount, 0, 0, M_NULLPTR, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_CLOSE_ZONE, RESERVED, zoneCount, zoneID, 0, M_NULLPTR, 0);
    }
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Finish_Zone_Ext(const tDevice* M_NONNULL device,
                                                        bool                     finishAll,
                                                        uint64_t                 zoneID,
                                                        uint16_t                 zoneCount)
{
    if (finishAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_FINISH_ZONE, BIT0, zoneCount, 0, 0, M_NULLPTR, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_FINISH_ZONE, RESERVED, zoneCount, zoneID, 0, M_NULLPTR, 0);
    }
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Open_Zone_Ext(const tDevice* M_NONNULL device,
                                                      bool                     openAll,
                                                      uint64_t                 zoneID,
                                                      uint16_t                 zoneCount)
{
    if (openAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_OPEN_ZONE, BIT0, zoneCount, 0, 0, M_NULLPTR, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_OPEN_ZONE, RESERVED, zoneCount, zoneID, 0, M_NULLPTR, 0);
    }
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Reset_Write_Pointers_Ext(const tDevice* M_NONNULL device,
                                                                 bool                     resetAll,
                                                                 uint64_t                 zoneID,
                                                                 uint16_t                 zoneCount)
{
    if (resetAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_RESET_WRITE_POINTERS, BIT0, zoneCount, 0, 0, M_NULLPTR, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_RESET_WRITE_POINTERS, RESERVED, zoneCount, zoneID, 0, M_NULLPTR,
                                      0);
    }
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Sequentialize_Zone_Ext(const tDevice* M_NONNULL device,
                                                               bool                     all,
                                                               uint64_t                 zoneID,
                                                               uint16_t                 zoneCount)
{
    if (all)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_SEQUENTIALIZE_ZONE, BIT0, zoneCount, 0, 0, M_NULLPTR, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_SEQUENTIALIZE_ZONE, RESERVED, zoneCount, zoneID, 0, M_NULLPTR,
                                      0);
    }
}

OPENSEA_TRANSPORT_API eReturnValues ata_Report_Zones_Ext(const tDevice* M_NONNULL device,
                                                         eZoneReportingOptions    reportingOptions,
                                                         bool                     partial,
                                                         uint16_t                 returnPageCount,
                                                         uint64_t                 zoneLocator,
                                                         uint8_t* M_NONNULL       ptrData,
                                                         uint32_t                 dataSize)
{
    uint8_t actionSpecificFeatureExt = C_CAST(uint8_t, reportingOptions);
    if (partial)
    {
        actionSpecificFeatureExt |= BIT7;
    }
    return ata_ZAC_Management_In(device, ZM_ACTION_REPORT_ZONES, actionSpecificFeatureExt, 0, returnPageCount,
                                 zoneLocator, 0, ptrData, dataSize);
}

OPENSEA_TRANSPORT_API eReturnValues ata_Report_Realms_Ext(const tDevice* M_NONNULL device,
                                                          eRealmsReportingOptions  reportingOptions,
                                                          uint16_t                 returnPageCount,
                                                          uint64_t                 realmLocator,
                                                          uint8_t* M_NONNULL       ptrData,
                                                          uint32_t                 dataSize)
{
    return ata_ZAC_Management_In(device, ZM_ACTION_REPORT_REALMS, C_CAST(uint8_t, reportingOptions), 0, returnPageCount,
                                 realmLocator, 0, ptrData, dataSize);
}

OPENSEA_TRANSPORT_API eReturnValues ata_Report_Zone_Domains_Ext(const tDevice* M_NONNULL    device,
                                                                eZoneDomainReportingOptions reportingOptions,
                                                                uint16_t                    returnPageCount,
                                                                uint64_t                    zoneDomainLocator,
                                                                uint8_t* M_NONNULL          ptrData,
                                                                uint32_t                    dataSize)
{
    return ata_ZAC_Management_In(device, ZM_ACTION_REPORT_ZONE_DOMAINS, C_CAST(uint8_t, reportingOptions), 0,
                                 returnPageCount, zoneDomainLocator, 0, ptrData, dataSize);
}

OPENSEA_TRANSPORT_API eReturnValues ata_Zone_Activate_Ext(const tDevice* M_NONNULL device,
                                                          bool                     all,
                                                          uint16_t                 returnPageCount,
                                                          uint64_t                 zoneID,
                                                          bool                     numZonesSF,
                                                          uint16_t                 numberOfZones,
                                                          uint8_t                  otherZoneDomainID,
                                                          uint8_t* M_NONNULL       ptrData,
                                                          uint32_t                 dataSize)
{
    uint8_t actionSpecificBits = UINT8_C(0);
    if (all)
    {
        actionSpecificBits |= BIT7;
    }
    if (!numZonesSF)
    {
        // specify number of zones in aux
        actionSpecificBits |= BIT5;
        return ata_ZAC_Management_In(device, ZM_ACTION_ZONE_ACTIVATE, otherZoneDomainID, actionSpecificBits,
                                     returnPageCount, zoneID, numberOfZones, ptrData, dataSize);
    }
    else
    {
        // number of zones was set by set features/identify device data log field last set by set features
        return ata_ZAC_Management_In(device, ZM_ACTION_ZONE_ACTIVATE, otherZoneDomainID, actionSpecificBits,
                                     returnPageCount, zoneID, 0, ptrData, dataSize);
    }
}

OPENSEA_TRANSPORT_API eReturnValues ata_Zone_Query_Ext(const tDevice* M_NONNULL device,
                                                       bool                     all,
                                                       uint16_t                 returnPageCount,
                                                       uint64_t                 zoneID,
                                                       bool                     numZonesSF,
                                                       uint16_t                 numberOfZones,
                                                       uint8_t                  otherZoneDomainID,
                                                       uint8_t* M_NONNULL       ptrData,
                                                       uint32_t                 dataSize)
{
    uint8_t actionSpecificBits = UINT8_C(0);
    if (all)
    {
        actionSpecificBits |= BIT7;
    }
    if (!numZonesSF)
    {
        // specify number of zones in aux
        actionSpecificBits |= BIT5;
        return ata_ZAC_Management_In(device, ZM_ACTION_ZONE_QUERY, otherZoneDomainID, actionSpecificBits,
                                     returnPageCount, zoneID, numberOfZones, ptrData, dataSize);
    }
    else
    {
        // number of zones was set by set features/identify device data log field last set by set features
        return ata_ZAC_Management_In(device, ZM_ACTION_ZONE_QUERY, otherZoneDomainID, actionSpecificBits,
                                     returnPageCount, zoneID, 0, ptrData, dataSize);
    }
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Media_Eject(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_MEDIA_EJECT, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Media Eject\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Media Eject", ret);

    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Get_Media_Status(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_GET_MEDIA_STATUS, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Get Media Status\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Get Media Status", ret);

    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Media_Lock(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_DOOR_LOCK_CMD, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Media Lock\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Media Lock", ret);

    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Media_Unlock(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_DOOR_UNLOCK_CMD, ATA_CMD_TYPE_TASKFILE, false);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Media Unlock\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Media Unlock", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Zeros_Ext(const tDevice* M_NONNULL device,
                                                  uint16_t                 numberOfLogicalSectors,
                                                  uint64_t                 lba,
                                                  bool                     trim)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_ZEROS_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount   = M_Byte0(numberOfLogicalSectors);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfLogicalSectors);
    set_ata_pt_LBA_48(&ataCommandOptions, lba);

    if (trim)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT0;
    }

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES,
                                          "Sending ATA Zeros Ext - LBA %" PRIu64 ", count %" PRIu16 " %s\n",
                                          lba, numberOfLogicalSectors, (trim ? "(TRIM)" : ""));

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Zeros Ext", ret);

    return ret;
}

// Changing to a 5 hour timeout due to new information showing larger capacities taking even longer to complete.
// While it is a long time it is still faster than a full reformat of the drive.
// This time is more than double what is expected, but that leaves room for error in case some drives are taking
// longer than expected.
#define DEFAULT_SET_SECTOR_CONFIG_TIMEOUT (3600 * 5)

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Set_Sector_Configuration_Ext(const tDevice* M_NONNULL device,
                                                                     uint16_t                 commandCheck,
                                                                     uint8_t sectorConfigurationDescriptorIndex)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_SET_SECTOR_CONFIG_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount  = sectorConfigurationDescriptorIndex & 0x07;
    ataCommandOptions.tfr.Feature48    = M_Byte1(commandCheck);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(commandCheck);
    ataCommandOptions.timeout          = DEFAULT_SET_SECTOR_CONFIG_TIMEOUT;
    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Set Sector Configuration Ext\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Set Sector Configuration Ext", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Get_Physical_Element_Status(const tDevice* M_NONNULL device,
                                                                    uint8_t                  filter,
                                                                    uint8_t                  reportType,
                                                                    uint64_t                 startingElement,
                                                                    uint8_t* M_NONNULL       ptrData,
                                                                    uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;
    explicit_zeroes(ptrData, dataSize);
    ataPassthroughCommand ataCommandOptions =
        create_ata_dma_in_cmd(device, ATA_GET_PHYSICAL_ELEMENT_STATUS, ATA_CMD_TYPE_EXTENDED_TASKFILE,
                              M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    ataCommandOptions.tfr.Feature48 =
        C_CAST(uint8_t,
               (filter << 6) | (reportType & 0x0F)); // filter is 2 bits, report type is 4 bits. All others are reserved
    ataCommandOptions.tfr.ErrorFeature = RESERVED;
    set_ata_pt_LBA_48_sig(&ataCommandOptions, startingElement);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Get Physical Element Status\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Get Physical Element Status", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Remove_Element_And_Truncate(const tDevice* M_NONNULL device,
                                                                    uint32_t                 elementIdentifier,
                                                                    uint64_t                 requestedMaxLBA)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_REMOVE_AND_TRUNCATE, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount   = M_Byte0(elementIdentifier);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(elementIdentifier);
    ataCommandOptions.tfr.ErrorFeature  = M_Byte2(elementIdentifier);
    ataCommandOptions.tfr.Feature48     = M_Byte3(elementIdentifier);
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    set_ata_pt_LBA_48(&ataCommandOptions, requestedMaxLBA);

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Remove And Truncate\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Remove And Truncate", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Remove_Element_And_Modify_Zones(const tDevice* M_NONNULL device,
                                                                        uint32_t                 elementIdentifier)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_REMOVE_ELEMENT_AND_MODIFY_ZONES, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    ataCommandOptions.tfr.SectorCount   = M_Byte0(elementIdentifier);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(elementIdentifier);
    ataCommandOptions.tfr.ErrorFeature  = M_Byte2(elementIdentifier);
    ataCommandOptions.tfr.Feature48     = M_Byte3(elementIdentifier);
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Remove And Modify Zones\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Remove And Modify Zones", ret);
    return ret;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Restore_Elements_And_Rebuild(const tDevice* M_NONNULL device)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_RESTORE_AND_REBUILD, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Restore Elements and Rebuild\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Restore Elements and Rebuild", ret);
    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_Mutate_Ext(const tDevice* M_NONNULL device,
                                                   bool                     requestMaximumAccessibleCapacity,
                                                   uint32_t                 requestedConfigurationID)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_MUTATE_EXT, ATA_CMD_TYPE_EXTENDED_TASKFILE, false);
    if (requestMaximumAccessibleCapacity)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT0;
    }
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    ataCommandOptions.tfr.LbaLow   = M_Byte0(requestedConfigurationID);
    ataCommandOptions.tfr.LbaMid   = M_Byte1(requestedConfigurationID);
    ataCommandOptions.tfr.LbaHi    = M_Byte2(requestedConfigurationID);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(requestedConfigurationID);
    ataCommandOptions.tfr.LbaMid48 = RESERVED;
    ataCommandOptions.tfr.LbaHi48  = RESERVED;

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Mutate Ext\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Mutate Ext", ret);
    return ret;
}

/////////////////////////////////////////////
/// Asynchronous Commands below this line ///
/////////////////////////////////////////////
M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Non_Data(const tDevice* M_NONNULL device,
                                                     uint8_t                  subCommand /*bits 4:0*/,
                                                     uint16_t                 subCommandSpecificFeature /*bits 11:0*/,
                                                     uint8_t                  subCommandSpecificCount,
                                                     uint8_t                  ncqTag /*bits 5:0*/,
                                                     uint64_t                 lba,
                                                     uint32_t                 auxilary)
{
    eReturnValues ret = UNKNOWN;
    // needing RTFRs is subcommand specific. Setting to true for now-TJE
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_FPDMA_NON_DATA, ATA_CMD_TYPE_EXTENDED_TASKFILE, true);
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA_FPDMA; // this is a non-data NCQ command...due to how SAT CDB
                                                               // builder interprets this, set this value here!
    set_ata_pt_LBA_48(&ataCommandOptions, lba);
    set_ata_pt_aux_icc(&ataCommandOptions, auxilary, 0);
    ataCommandOptions.tfr.DeviceHead =
        clear_uint8_bit(ataCommandOptions.tfr.DeviceHead, 4); // spec says this must be zero
    ataCommandOptions.tfr.Feature48    = M_Byte1(M_STATIC_CAST(uint64_t, subCommandSpecificFeature) << 4);
    ataCommandOptions.tfr.ErrorFeature = M_STATIC_CAST(
        uint8_t, (M_Nibble0(M_STATIC_CAST(uint64_t, subCommandSpecificFeature)) << 4) | M_Nibble0(subCommand));
    ataCommandOptions.tfr.SectorCount48 = subCommandSpecificCount;
    ataCommandOptions.tfr.SectorCount   = M_STATIC_CAST(uint8_t, ncqTag << 3); // shift into bits 7:3

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA NCQ Non Data, Subcommand %u\n", subCommand);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "NCQ Non Data", ret);

    return ret;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Abort_NCQ_Queue(const tDevice* M_NONNULL device,
                                                            uint8_t                  abortType /*bits0:3*/,
                                                            uint8_t                  prio /*bits 1:0*/,
                                                            uint8_t                  ncqTag,
                                                            uint8_t                  tTag)
{
    return ata_NCQ_Non_Data(device, NCQ_NON_DATA_ABORT_NCQ_QUEUE, abortType,
                            C_CAST(uint8_t, C_CAST(uint16_t, prio) << 6), ncqTag, C_CAST(uint32_t, tTag) << 3, 0);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Deadline_Handling(const tDevice* M_NONNULL device,
                                                              bool                     rdnc,
                                                              bool                     wdnc,
                                                              uint8_t                  ncqTag)
{
    uint16_t ft = UINT16_C(0);
    if (rdnc)
    {
        ft |= BIT5;
    }
    if (wdnc)
    {
        ft |= BIT4;
    }
    return ata_NCQ_Non_Data(device, NCQ_NON_DATA_DEADLINE_HANDLING, ft >> 4, RESERVED, ncqTag, RESERVED, 0);
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Set_Features(const tDevice* M_NONNULL   device,
                                                         eATASetFeaturesSubcommands subcommand,
                                                         uint8_t                    subcommandCountField,
                                                         uint8_t                    subcommandLBALo,
                                                         uint8_t                    subcommandLBAMid,
                                                         uint16_t                   subcommandLBAHi,
                                                         uint8_t                    ncqTag)
{
    uint64_t lba = M_BytesTo4ByteValue(M_Nibble0(M_Byte1(subcommandLBAHi)), M_Byte0(subcommandLBAHi), subcommandLBAMid,
                                       subcommandLBALo);
    return ata_NCQ_Non_Data(device, NCQ_NON_DATA_SET_FEATURES, C_CAST(uint16_t, subcommand << 4), subcommandCountField,
                            ncqTag, lba, RESERVED);
}

// ncq zeros ext
M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Zeros_Ext(const tDevice* M_NONNULL device,
                                                      uint16_t                 numberOfLogicalSectors,
                                                      uint64_t                 lba,
                                                      bool                     trim,
                                                      uint8_t                  ncqTag)
{
    return ata_NCQ_Non_Data(device, NCQ_NON_DATA_ZERO_EXT, C_CAST(uint16_t, M_Byte0(numberOfLogicalSectors) << 4),
                            M_Byte1(numberOfLogicalSectors), ncqTag, lba, trim ? BIT1 : 0);
}

// ncq zac management out
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Receive_FPDMA_Queued(const tDevice* M_NONNULL device,
                                                                 uint8_t                  subCommand /*bits 5:0*/,
                                                                 uint16_t                 sectorCount /*ft*/,
                                                                 uint8_t                  prio /*bits 1:0*/,
                                                                 uint8_t                  ncqTag,
                                                                 uint64_t                 lba,
                                                                 uint32_t                 auxilary,
                                                                 uint8_t* M_NONNULL       ptrData,
                                                                 uint32_t                 dataSize)
{
    eReturnValues ret = UNKNOWN;
    explicit_zeroes(ptrData, dataSize);
    ataPassthroughCommand ataCommandOptions =
        create_ata_queued_cmd(device, ATA_RECEIVE_FPDMA, ATA_CMD_TYPE_EXTENDED_TASKFILE, true, ncqTag, XFER_DATA_IN,
                              M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    set_ata_pt_prio_subcmd(&ataCommandOptions, prio, subCommand);
    set_ata_pt_LBA_48(&ataCommandOptions, lba);
    ataCommandOptions.tfr.DeviceHead =
        clear_uint8_bit(ataCommandOptions.tfr.DeviceHead, 4); // spec says this must be zero
    set_ata_pt_aux_icc(&ataCommandOptions, auxilary, 0);
    M_USE_UNUSED(sectorCount);

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Receive FPDMA Queued, Subcommand %u\n", subCommand);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Receive FPDMA Queued", ret);

    return ret;
}

// ncq read log dma ext
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Read_Log_DMA_Ext(const tDevice* M_NONNULL device,
                                                             uint8_t                  logAddress,
                                                             uint16_t                 pageNumber,
                                                             uint8_t* M_NONNULL       ptrData,
                                                             uint32_t                 dataSize,
                                                             uint16_t                 featureRegister,
                                                             uint8_t                  prio /*bits 1:0*/,
                                                             uint8_t                  ncqTag)
{
    uint64_t lba =
        M_BytesTo8ByteValue(0, 0, RESERVED, M_Byte1(pageNumber), RESERVED, RESERVED, M_Byte0(pageNumber), logAddress);
    return ata_NCQ_Receive_FPDMA_Queued(device, UINT8_C(1), C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), prio,
                                        ncqTag, lba, featureRegister, ptrData, dataSize);
}

// ncq ZAC management in
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Send_FPDMA_Queued(const tDevice* M_NONNULL device,
                                                              uint8_t                  subCommand /*bits 5:0*/,
                                                              uint16_t                 sectorCount /*ft*/,
                                                              uint8_t                  prio /*bits 1:0*/,
                                                              uint8_t                  ncqTag,
                                                              uint64_t                 lba,
                                                              uint32_t                 auxilary,
                                                              uint8_t* M_NONNULL       ptrData,
                                                              uint32_t                 dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_queued_cmd(device, ATA_SEND_FPDMA, ATA_CMD_TYPE_EXTENDED_TASKFILE, true, ncqTag, XFER_DATA_OUT,
                              M_STATIC_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), ptrData, dataSize);
    set_ata_pt_prio_subcmd(&ataCommandOptions, prio, subCommand);
    set_ata_pt_LBA_48(&ataCommandOptions, lba);
    ataCommandOptions.tfr.DeviceHead =
        clear_uint8_bit(ataCommandOptions.tfr.DeviceHead, 4); // spec says this must be zero
    set_ata_pt_aux_icc(&ataCommandOptions, auxilary, 0);
    M_USE_UNUSED(sectorCount);

    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Send FPDMA Queued, Subcommand %u\n", subCommand);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Send FPDMA Queued", ret);

    return ret;
}

// ncq data set management
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Data_Set_Management(const tDevice* M_NONNULL device,
                                                                bool                     trimBit,
                                                                uint8_t* M_NONNULL       ptrData,
                                                                uint32_t                 dataSize,
                                                                uint8_t                  prio /*bits 1:0*/,
                                                                uint8_t                  ncqTag)
{
    uint32_t auxreg = UINT32_C(0); // bits 15:0 represent feature register of the NCQ data set management command.
    if (trimBit)
    {
        auxreg |= BIT0;
    }
    return ata_NCQ_Send_FPDMA_Queued(device, SEND_FPDMA_DATA_SET_MANAGEMENT,
                                     C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), prio, ncqTag, RESERVED, auxreg,
                                     ptrData, dataSize);
}

// ncq write log DMA ext
OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Write_Log_DMA_Ext(const tDevice* M_NONNULL device,
                                                              uint8_t                  logAddress,
                                                              uint16_t                 pageNumber,
                                                              uint8_t* M_NONNULL       ptrData,
                                                              uint32_t                 dataSize,
                                                              uint8_t                  prio /*bits 1:0*/,
                                                              uint8_t                  ncqTag)
{
    uint64_t lba =
        M_BytesTo8ByteValue(0, 0, RESERVED, M_Byte1(pageNumber), RESERVED, RESERVED, M_Byte0(pageNumber), logAddress);
    return ata_NCQ_Send_FPDMA_Queued(device, SEND_FPDMA_WRITE_LOG_DMA_EXT,
                                     C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), prio, ncqTag, lba, RESERVED,
                                     ptrData, dataSize);
}

OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Read_FPDMA_Queued(const tDevice* M_NONNULL device,
                                                              bool                     fua,
                                                              uint64_t                 lba,
                                                              uint8_t* M_NONNULL       ptrData,
                                                              uint32_t                 dataSize,
                                                              uint8_t                  prio,
                                                              uint8_t                  ncqTag,
                                                              uint8_t                  icc)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_queued_lba_cmd(
        device, ATA_READ_FPDMA_QUEUED_CMD, ATA_CMD_TYPE_EXTENDED_TASKFILE, true, ncqTag, XFER_DATA_IN,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), true), lba, ptrData, dataSize);
    set_ata_pt_prio_subcmd(&ataCommandOptions, prio, 0);
    ataCommandOptions.tfr.DeviceHead =
        clear_uint8_bit(ataCommandOptions.tfr.DeviceHead, 4); // spec says this must be zero
    set_ata_pt_aux_icc(&ataCommandOptions, 0, icc);

    if (fua)
    {
        ataCommandOptions.tfr.DeviceHead |= BIT7;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Read FPDMA Queued\n");

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Read FPDMA Queued", ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Write_FPDMA_Queued(const tDevice* M_NONNULL device,
                                                               bool                     fua,
                                                               uint64_t                 lba,
                                                               uint8_t* M_NONNULL       ptrData,
                                                               uint32_t                 dataSize,
                                                               uint8_t                  prio,
                                                               uint8_t                  ncqTag,
                                                               uint8_t                  icc)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_queued_lba_cmd(
        device, ATA_WRITE_FPDMA_QUEUED_CMD, ATA_CMD_TYPE_EXTENDED_TASKFILE, true, ncqTag, XFER_DATA_OUT,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), true), lba, ptrData, dataSize);
    set_ata_pt_prio_subcmd(&ataCommandOptions, prio, 0);
    ataCommandOptions.tfr.DeviceHead =
        clear_uint8_bit(ataCommandOptions.tfr.DeviceHead, 4); // spec says this must be zero
    set_ata_pt_aux_icc(&ataCommandOptions, 0, icc);

    if (fua)
    {
        ataCommandOptions.tfr.DeviceHead |= BIT7;
    }

    print_tDevice_Verbose_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA Write FPDMA Queued\n");

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, "Write FPDMA Queued", ret);

    return ret;
}

// Old TCQ commands
OPENSEA_TRANSPORT_API eReturnValues ata_Read_DMA_Queued(const tDevice* M_NONNULL device,
                                                        bool                     ext,
                                                        uint64_t                 lba,
                                                        uint8_t* M_NONNULL       ptrData,
                                                        uint32_t                 dataSize,
                                                        uint8_t                  tag)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_queued_lba_cmd(
        device, ext ? ATA_READ_DMA_QUE_EXT : ATA_READ_DMA_QUEUED_CMD,
        ext ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE, false, tag, XFER_DATA_IN,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), ext), lba, ptrData, dataSize);
    ataCommandOptions.tfr.DeviceHead =
        clear_uint8_bit(ataCommandOptions.tfr.DeviceHead, 4); // spec says this must be zero

    const char* readDmaQueuedCmdName = ext ? "Read DMA Queued Ext" : "Read DMA Queued";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", readDmaQueuedCmdName);

    explicit_zeroes(ptrData, dataSize);
    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, readDmaQueuedCmdName, ret);

    return ret;
}

OPENSEA_TRANSPORT_API eReturnValues ata_Write_DMA_Queued(const tDevice* M_NONNULL device,
                                                         bool                     ext,
                                                         uint64_t                 lba,
                                                         uint8_t* M_NONNULL       ptrData,
                                                         uint32_t                 dataSize,
                                                         uint8_t                  tag)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_queued_lba_cmd(
        device, ext ? ATA_WRITE_DMA_QUE_EXT : ATA_WRITE_DMA_QUEUED_CMD,
        ext ? ATA_CMD_TYPE_EXTENDED_TASKFILE : ATA_CMD_TYPE_TASKFILE, false, tag, XFER_DATA_OUT,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, get_Device_BlockSize(device), ext), lba, ptrData, dataSize);

    const char* writeDmaQueuedCmdName = ext ? "Write DMA Queued Ext" : "Write DMA Queued";
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Sending ATA %s\n", writeDmaQueuedCmdName);

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    print_tDevice_Return_Enum(device, writeDmaQueuedCmdName, ret);

    return ret;
}

// ATA NOP
M_PARAM_RO(1)
M_PARAM_WO(5)
OPENSEA_TRANSPORT_API eReturnValues ata_NOP(const tDevice* M_NONNULL device,
                                            eATANOPFeature           nopMode,
                                            uint8_t                  countToReturn,
                                            uint32_t                 lbaToReturn,
                                            ataReturnTFRs* M_NONNULL returnTFRs)
{
    eReturnValues         ret               = SUCCESS;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_NOP_CMD, ATA_CMD_TYPE_TASKFILE, false);

    ataCommandOptions.tfr.ErrorFeature = M_STATIC_CAST(uint8_t, nopMode);
    ataCommandOptions.tfr.SectorCount  = countToReturn;
    set_ata_pt_LBA_28_sig(&ataCommandOptions, lbaToReturn);
    ataCommandOptions.needRTFRs = true; // This shouldn't be needed since this always triggers an abort, but setting
                                        // just to be sure we try to get them.

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA NOP with Count %02" PRIX8 " and LBA %07" PRIX32 "\n", countToReturn, lbaToReturn);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("ata_NOP: After ata_Passthrough_Command, ataCommandOptions.rtfr.secCnt = 0x%02X\n", ataCommandOptions.rtfr.secCnt);
    }

    if (returnTFRs)
    {
        safe_memcpy(returnTFRs, sizeof(ataReturnTFRs), &ataCommandOptions.rtfr, sizeof(ataReturnTFRs));
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            printf("ata_NOP: After memcpy, returnTFRs->secCnt = 0x%02X\n", returnTFRs->secCnt);
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("NOP", ret);
    }

    return ret;
}
