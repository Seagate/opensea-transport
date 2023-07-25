//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file ata_cmds.c   Implementation for ATA Spec command functions
//                     The intention of the file is to be generic & not OS specific

#include "common_public.h"
#include "ata_helper_func.h"
#include "scsi_helper_func.h"
#include "sat_helper_func.h"
#include "ti_legacy_helper.h"
#include "nec_legacy_helper.h"
#include "prolific_legacy_helper.h"
#include "cypress_legacy_helper.h"
#include "psp_legacy_helper.h"
#include "csmi_legacy_pt_cdb_helper.h"

int ata_Passthrough_Command(tDevice *device, ataPassthroughCommand  *ataCommandOptions)
{
    int ret = UNKNOWN;
    switch (device->drive_info.passThroughHacks.passthroughType)
    {
    case ATA_PASSTHROUGH_PSP:
        ret = send_PSP_Legacy_Passthrough_Command(device, ataCommandOptions);
        break;
    case ATA_PASSTHROUGH_CYPRESS:
        ret = send_Cypress_Legacy_Passthrough_Command(device, ataCommandOptions);
        break;
    case ATA_PASSTHROUGH_PROLIFIC:
        ret = send_Prolific_Legacy_Passthrough_Command(device, ataCommandOptions);
        break;
    case ATA_PASSTHROUGH_TI:
        ret = send_TI_Legacy_Passthrough_Command(device, ataCommandOptions);
        break;
    case ATA_PASSTHROUGH_NEC:
        ret = send_NEC_Legacy_Passthrough_Command(device, ataCommandOptions);
        break;
    case ATA_PASSTHROUGH_SAT:
        ret = send_SAT_Passthrough_Command(device, ataCommandOptions);
        break;
    case ATA_PASSTHROUGH_CSMI:
        ret = send_CSMI_Legacy_ATA_Passthrough(device, ataCommandOptions);
        break;
    default:
        ret = BAD_PARAMETER;
        break;
    }
    return ret;
}

int ata_Soft_Reset(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand softReset;
    memset(&softReset, 0, sizeof(ataPassthroughCommand));
    softReset.commadProtocol = ATA_PROTOCOL_SOFT_RESET;
    softReset.commandType = ATA_CMD_TYPE_TASKFILE;
    softReset.commandDirection = XFER_NO_DATA;
    softReset.ptrData = NULL;
    
    ret = ata_Passthrough_Command(device, &softReset);

    return ret;
}

int ata_Hard_Reset(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand hardReset;
    memset(&hardReset, 0, sizeof(ataPassthroughCommand));
    hardReset.commadProtocol = ATA_PROTOCOL_HARD_RESET;
    hardReset.commandType = ATA_CMD_TYPE_TASKFILE;
    hardReset.commandDirection = XFER_NO_DATA;
    hardReset.ptrData = NULL;

    ret = ata_Passthrough_Command(device, &hardReset);

    return ret;
}

int ata_Identify(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand identify;
    memset(&identify, 0, sizeof(ataPassthroughCommand));
    identify.commandType = ATA_CMD_TYPE_TASKFILE;
    identify.commandDirection = XFER_DATA_IN;
    identify.commadProtocol = ATA_PROTOCOL_PIO;
    identify.tfr.SectorCount = 1;//some controllers have issues when this isn't set to 1
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        identify.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    identify.tfr.CommandStatus = ATA_IDENTIFY;
    identify.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    identify.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    identify.ptrData = ptrData;
    identify.dataSize = dataSize;

    if (device->drive_info.ata_Options.isDevice1)
    {
        identify.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Identify command\n");
    }
    ret = ata_Passthrough_Command(device, &identify);

    if (ret == SUCCESS && ptrData != C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000))
    {
        //copy the data to the device structure so that it's not (as) stale
        memcpy(&device->drive_info.IdentifyData.ata.Word000, ptrData, sizeof(tAtaIdentifyData));
    }

#if defined (__BIG_ENDIAN__)
    if(ptrData == (uint8_t*)&device->drive_info.IdentifyData.ata.Word000)
    {
        byte_Swap_ID_Data_Buffer(&device->drive_info.IdentifyData.ata.Word000);
    }
#endif

    if (ret == SUCCESS)
    {
        if (ptrData[510] == ATA_CHECKSUM_VALIDITY_INDICATOR)
        {
            //we got data, so validate the checksum
            uint32_t invalidSec = 0;
            if (!is_Checksum_Valid(ptrData, LEGACY_DRIVE_SEC_SIZE, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
                if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
                {
                    printf("Warning: Identify Checksum is invalid\n");
                }
            }
        }
        else
        {
            //Don't do anything. This device doesn't use a checksum.
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Identify", ret);
    }
    return ret;
}

int ata_Sanitize_Command(tDevice *device, eATASanitizeFeature sanitizeFeature, uint64_t lba, uint16_t sectorCount)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataSanitizeCmd;
    memset(&ataSanitizeCmd, 0, sizeof(ataPassthroughCommand));
    ataSanitizeCmd.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataSanitizeCmd.commandDirection = XFER_NO_DATA;
    ataSanitizeCmd.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataSanitizeCmd.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataSanitizeCmd.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataSanitizeCmd.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataSanitizeCmd.tfr.CommandStatus = ATA_SANITIZE;
    ataSanitizeCmd.tfr.SectorCount = M_Byte0(sectorCount);
    ataSanitizeCmd.tfr.SectorCount48 = M_Byte1(sectorCount);
    ataSanitizeCmd.tfr.ErrorFeature = M_Byte0(C_CAST(uint16_t, sanitizeFeature));
    ataSanitizeCmd.tfr.Feature48 = M_Byte1(C_CAST(uint16_t, sanitizeFeature));
    ataSanitizeCmd.tfr.LbaLow = M_Byte0(lba);
    ataSanitizeCmd.tfr.LbaMid = M_Byte1(lba);
    ataSanitizeCmd.tfr.LbaHi = M_Byte2(lba);
    ataSanitizeCmd.tfr.LbaLow48 = M_Byte3(lba);
    ataSanitizeCmd.tfr.LbaMid48 = M_Byte4(lba);
    ataSanitizeCmd.tfr.LbaHi48 = M_Byte5(lba);

    if (device->drive_info.ata_Options.isDevice1)
    {
        ataSanitizeCmd.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Sanitize command - ");
        switch (sanitizeFeature)
        {
        case ATA_SANITIZE_STATUS:
            printf("Status\n");
            break;
        case ATA_SANITIZE_CRYPTO_SCRAMBLE:
            printf("Crypto Scramble\n");
            break;
        case ATA_SANITIZE_BLOCK_ERASE:
            printf("Block Erase\n");
            break;
        case ATA_SANITIZE_OVERWRITE_ERASE:
            printf("Overwrite Erase\n");
            break;
        case ATA_SANITIZE_FREEZE_LOCK:
            printf("Freeze Lock\n");
            break;
        case ATA_SANITIZE_ANTI_FREEZE_LOCK:
            printf("Anti Freeze Lock\n");
            break;
        default:
            printf("Unknown\n");
            break;
        }
    }
    
    ret = ata_Passthrough_Command(device, &ataSanitizeCmd);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        switch (sanitizeFeature)
        {
        case ATA_SANITIZE_CRYPTO_SCRAMBLE:
            print_Return_Enum("Sanitize - Crypto Scramble", ret);
            break;
        case ATA_SANITIZE_BLOCK_ERASE:
            print_Return_Enum("Sanitize - Block Erase", ret);
            break;
        case ATA_SANITIZE_OVERWRITE_ERASE:
            print_Return_Enum("Sanitize - Overwrite", ret);
            break;
        case ATA_SANITIZE_FREEZE_LOCK:
            print_Return_Enum("Sanitize - Freeze Lock", ret);
            break;
        case ATA_SANITIZE_STATUS:
            print_Return_Enum("Sanitize - Status", ret);
            break;
        case ATA_SANITIZE_ANTI_FREEZE_LOCK:
            print_Return_Enum("Sanitize - Anti Freeze Lock", ret);
            break;
        default:
            print_Return_Enum("Sanitize - Unknown", ret);
        }
    }
    return ret;
}

int ata_Sanitize_Status(tDevice *device, bool clearFailureMode)
{
    uint16_t statusCount = 0;
    if (clearFailureMode)
    {
        statusCount |= BIT0;
    }
    return ata_Sanitize_Command(device, ATA_SANITIZE_STATUS, 0, statusCount);
}

int ata_Sanitize_Crypto_Scramble(tDevice *device, bool failureModeBit, bool znr)
{
    uint16_t cryptoCount = 0;
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

int ata_Sanitize_Block_Erase(tDevice *device, bool failureModeBit, bool znr)
{
    uint16_t blockEraseCount = 0;
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

int ata_Sanitize_Overwrite_Erase(tDevice *device, bool failureModeBit, bool invertBetweenPasses, uint8_t numberOfPasses, uint32_t overwritePattern, bool znr, bool definitiveEndingPattern)
{
    uint16_t overwriteCount = 0;
    uint64_t overwriteLBA = overwritePattern;
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

int ata_Sanitize_Freeze_Lock(tDevice *device)
{
    return ata_Sanitize_Command(device, ATA_SANITIZE_FREEZE_LOCK, ATA_SANITIZE_FREEZE_LOCK_LBA, RESERVED);
}

int ata_Sanitize_Anti_Freeze_Lock(tDevice *device)
{
    return ata_Sanitize_Command(device, ATA_SANITIZE_ANTI_FREEZE_LOCK, ATA_SANITIZE_ANTI_FREEZE_LOCK_LBA, RESERVED);
}

int ata_Read_Log_Ext(tDevice *device, uint8_t logAddress, uint16_t pageNumber, uint8_t *ptrData, uint32_t dataSize, bool useDMA, uint16_t featureRegister )
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Read Log Ext DMA command");
        }
        else
        {
            printf("Sending ATA Read Log Ext command");
        }
        printf(" - Log %02" PRIX8 "h, Page %" PRIu16 ", Count %" PRIu32 "\n", logAddress, pageNumber, (dataSize / LEGACY_DRIVE_SEC_SIZE));
    }

    //zap it
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }
    else if (dataSize < LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
    // Must be at 512 boundary
    else if (dataSize % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }

    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    if (!useDMA)
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO; 
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LOG_EXT;
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LOG_EXT_DMA;
        //set the DMA protocol to use based off the supported DMA modes of the drive
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;

    // Will the ptrData be the correct size for the log?
    ataCommandOptions.tfr.LbaLow = logAddress;
    ataCommandOptions.tfr.LbaMid = M_Byte0(pageNumber);
    ataCommandOptions.tfr.LbaHi = RESERVED;
    ataCommandOptions.tfr.LbaLow48 = RESERVED;
    ataCommandOptions.tfr.LbaMid48 = M_Byte1(pageNumber);
    ataCommandOptions.tfr.LbaHi48 = RESERVED;
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(featureRegister);
    ataCommandOptions.tfr.Feature48 = M_Byte1(featureRegister);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        uint32_t invalidSec = 0;
        switch (logAddress)
        {
        case ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG:
        case ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG:
        case ATA_LOG_DELAYED_LBA_LOG:
        case ATA_LOG_NCQ_COMMAND_ERROR_LOG:
        case ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG:
            //we got data, so validate the checksum
            if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
            }
            break;
        default:
            //don't do anything since not all logs have checksums to validate
            break;
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Read Log Ext DMA", ret);
        }
        else
        {
            print_Return_Enum("Read Log Ext", ret);
        }
    }
    return ret;
}

int ata_Write_Log_Ext(tDevice *device, uint8_t logAddress, uint16_t pageNumber, \
                              uint8_t *ptrData, uint32_t dataSize, bool useDMA, bool forceRTFRs )
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Write Log Ext DMA command");
        }
        else
        {
            printf("Sending ATA Write Log Ext command");
        }
        printf(" - Log %02" PRIX8 "h, Page %" PRIu16 ", Count %" PRIu32 "\n", logAddress, pageNumber, (dataSize / LEGACY_DRIVE_SEC_SIZE));
    }

    //zap it
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }
    else if (dataSize < LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }
    // Must be at 512 boundary
    else if (dataSize % LEGACY_DRIVE_SEC_SIZE)
    {
        return BAD_PARAMETER;
    }

    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.forceCheckConditionBit = forceRTFRs;
    if (!useDMA)
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO; 
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LOG_EXT_CMD;
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LOG_EXT_DMA;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;

    // Will the ptrData be the correct size for the log?
    ataCommandOptions.tfr.LbaLow = logAddress;
    ataCommandOptions.tfr.LbaMid = M_Byte0(pageNumber);
    ataCommandOptions.tfr.LbaHi = RESERVED;
    ataCommandOptions.tfr.LbaLow48 = RESERVED;
    ataCommandOptions.tfr.LbaMid48 = M_Byte1(pageNumber);
    ataCommandOptions.tfr.LbaHi48 = RESERVED;
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);

    ataCommandOptions.tfr.ErrorFeature = 0; // ?? ATA Spec says Log specific
    ataCommandOptions.tfr.Feature48 = 0;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Write Log Ext DMA", ret);
        }
        else
        {
            print_Return_Enum("Write Log Ext", ret);
        }
    }
    return ret;
}

int ata_SMART_Command(tDevice *device, uint8_t feature, uint8_t lbaLo, uint8_t *ptrData, uint32_t dataSize, uint32_t timeout, bool forceRTFRs, uint8_t countReg)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA SMART command - ");
    }
    //zap it
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.forceCheckConditionBit = forceRTFRs;
    switch (feature)
    {
    case ATA_SMART_READ_LOG:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_READ_LOG)
        {
            printf("Read Log - Log %02" PRIX8 "h, Count %" PRIu32 "\n", lbaLo, (dataSize / LEGACY_DRIVE_SEC_SIZE));
        }
        M_FALLTHROUGH
    case ATA_SMART_RDATTR_THRESH:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_RDATTR_THRESH)
        {
            printf("Read Thresholds\n");
        }
        M_FALLTHROUGH
    case ATA_SMART_READ_DATA:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_READ_DATA)
        {
            printf("Read Data\n");
        }
        ataCommandOptions.commandDirection = XFER_DATA_IN;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        break;
    case ATA_SMART_WRITE_LOG:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_WRITE_LOG)
        {
            printf("Write Log - Log %02" PRIX8 "h, Count %" PRIu32 "\n", lbaLo, (dataSize / LEGACY_DRIVE_SEC_SIZE));
        }
        ataCommandOptions.commandDirection = XFER_DATA_OUT;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        break;
    case ATA_SMART_SW_AUTOSAVE:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_SW_AUTOSAVE)
        {
            printf("Attribute Autosave\n");
        }
        M_FALLTHROUGH
    case ATA_SMART_SAVE_ATTRVALUE:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_SAVE_ATTRVALUE)
        {
            printf("Save Attributes\n");
        }
        M_FALLTHROUGH
    case ATA_SMART_ENABLE:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_ENABLE)
        {
            printf("Enable Operations\n");
        }
        M_FALLTHROUGH
    case ATA_SMART_EXEC_OFFLINE_IMM:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_EXEC_OFFLINE_IMM)
        {
            printf("Offline Immediate - test %02" PRIX8 "h\n", lbaLo);
        }
        M_FALLTHROUGH
    case ATA_SMART_RTSMART:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_RTSMART)
        {
            printf("Return Status\n");
        }
        M_FALLTHROUGH
    case ATA_SMART_DISABLE:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity && feature == ATA_SMART_DISABLE)
        {
            printf("Disable Operations\n");
        }
        M_FALLTHROUGH
    default:
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        break;
    }

    // just sanity sake
    if (ataCommandOptions.commandDirection != XFER_NO_DATA)
    {
        if (!ptrData)
        {
            return BAD_PARAMETER;
        }
        else if (dataSize < LEGACY_DRIVE_SEC_SIZE)
        {
            return BAD_PARAMETER;
        }
    }

    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;

    ataCommandOptions.tfr.CommandStatus = ATA_SMART_CMD;
    ataCommandOptions.tfr.LbaLow = lbaLo;
    ataCommandOptions.tfr.LbaMid = ATA_SMART_SIG_MID;
    ataCommandOptions.tfr.LbaHi = ATA_SMART_SIG_HI;

    if(ataCommandOptions.commadProtocol == ATA_PROTOCOL_NO_DATA)
    {
        ataCommandOptions.tfr.SectorCount = countReg;
    }
    else
    {
        ataCommandOptions.tfr.SectorCount = C_CAST(uint8_t, dataSize / LEGACY_DRIVE_SEC_SIZE);
    }

    ataCommandOptions.tfr.ErrorFeature = feature;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.timeout = timeout;
    //special case to try and get around some weird failures in Windows
    if (ataCommandOptions.commandDirection == XFER_DATA_IN)
    {
        //make sure the buffer is cleared to zero
        memset(ptrData, 0, dataSize);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        switch (feature)
        {
        case ATA_SMART_READ_DATA:
            print_Return_Enum("SMART Read Data", ret);
            break;
        case ATA_SMART_RDATTR_THRESH:
            print_Return_Enum("SMART Read Thresholds", ret);
            break;
        case ATA_SMART_SW_AUTOSAVE:
            print_Return_Enum("SMART Attribute Autosave", ret);
            break;
        case ATA_SMART_SAVE_ATTRVALUE:
            print_Return_Enum("SMART Save Attribute", ret);
            break;
        case ATA_SMART_EXEC_OFFLINE_IMM:
            print_Return_Enum("SMART Offline Immediate", ret);
            break;
        case ATA_SMART_READ_LOG:
            print_Return_Enum("SMART Read Log", ret);
            break;
        case ATA_SMART_WRITE_LOG:
            print_Return_Enum("SMART Write Log", ret);
            break;
        case ATA_SMART_ENABLE:
            print_Return_Enum("SMART Enable Operations", ret);
            break;
        case ATA_SMART_DISABLE:
            print_Return_Enum("SMART Disable Operations", ret);
            break;
        case ATA_SMART_RTSMART:
            print_Return_Enum("SMART Return Status", ret);
            break;
        default:
            print_Return_Enum("SMART", ret);
        }
    }
    return ret;


}

int ata_SMART_Read_Log(tDevice *device, uint8_t logAddress, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = ata_SMART_Command(device, ATA_SMART_READ_LOG, logAddress, ptrData, dataSize, 15, false, 0);
    if (ret == SUCCESS)
    {
        uint32_t invalidSec = 0;
        switch (logAddress)
        {
        case ATA_LOG_SUMMARY_SMART_ERROR_LOG:
        case ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG:
        case ATA_LOG_SMART_SELF_TEST_LOG:
        case ATA_LOG_SELECTIVE_SELF_TEST_LOG:
            //we got data, so validate the checksum
            if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
                if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
                {
                    printf("Warning: Log Checksum is invalid\n");
                }
            }
            break;
        default:
            //don't do anything since not all logs have checksums to validate
            break;
        }
    }
    return ret;
}
int ata_SMART_Write_Log(tDevice *device, uint8_t logAddress, uint8_t *ptrData, uint32_t dataSize, bool forceRTFRs)
{
    return ata_SMART_Command(device,ATA_SMART_WRITE_LOG, logAddress, ptrData, dataSize, 15, forceRTFRs, 0);
}

int ata_SMART_Offline(tDevice *device, uint8_t subcommand, uint32_t timeout)
{
    return ata_SMART_Command(device, ATA_SMART_EXEC_OFFLINE_IMM, subcommand, NULL, 0, timeout, false, 0);
}

int ata_SMART_Read_Data(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = ata_SMART_Command(device, ATA_SMART_READ_DATA, 0, ptrData, dataSize, 15, false, 0);
    if (ret == SUCCESS)
    {
        uint32_t invalidSec = 0;
        //we got data, so validate the checksum
        if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
        {
            ret = WARN_INVALID_CHECKSUM;
            if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
            {
                printf("Warning: Checksum is invalid\n");
            }
        }
    }
    return ret;
}

int ata_SMART_Return_Status(tDevice *device)
{
    return ata_SMART_Command(device, ATA_SMART_RTSMART, 0, NULL, 0, 15, true, 0);
}

int ata_SMART_Enable_Operations(tDevice *device)
{
    return ata_SMART_Command(device, ATA_SMART_ENABLE, 0, NULL, 0, 15, false, 0);
}

int ata_SMART_Disable_Operations(tDevice *device)
{
    return ata_SMART_Command(device, ATA_SMART_DISABLE, 0, NULL, 0, 15, false, 0);
}

int ata_SMART_Read_Thresholds(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = ata_SMART_Command(device, ATA_SMART_RDATTR_THRESH, 0, ptrData, dataSize, 15, false, 0);
    if (ret == SUCCESS)
    {
        uint32_t invalidSec = 0;
        //we got data, so validate the checksum
        if (!is_Checksum_Valid(ptrData, dataSize, &invalidSec))
        {
            ret = WARN_INVALID_CHECKSUM;
            if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
            {
                printf("Warning: Checksum is invalid\n");
            }
        }
    }
    return ret;
}

int ata_SMART_Save_Attributes(tDevice *device)
{
    return ata_SMART_Command(device, ATA_SMART_SAVE_ATTRVALUE, 0, NULL, 0, 15, false, 0);
}

int ata_SMART_Attribute_Autosave(tDevice *device, bool enable)
{
    if(enable)
    {
        return ata_SMART_Command(device, ATA_SMART_SW_AUTOSAVE, 0, NULL, 0, 15, false, ATA_SMART_ATTRIBUTE_AUTOSAVE_ENABLE_SIG);
    }
    else
    {
        return ata_SMART_Command(device, ATA_SMART_SW_AUTOSAVE, 0, NULL, 0, 15, false, 0);
    }
}

int ata_SMART_Auto_Offline(tDevice *device, bool enable)
{
    if (enable)
    {
        return ata_SMART_Command(device, ATA_SMART_AUTO_OFFLINE, 0, NULL, 0, 15, false, ATA_SMART_AUTO_OFFLINE_ENABLE_SIG);
    }
    else
    {
        return ata_SMART_Command(device, ATA_SMART_AUTO_OFFLINE, 0, NULL, 0, 15, false, 0);
    }
}

int ata_Security_Disable_Password(tDevice *device, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE; //28bit command
    ataCommandOptions.dataSize = LEGACY_DRIVE_SEC_SIZE; //spec defines that this command always transfers this much data
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.tfr.CommandStatus = ATA_SECURITY_DISABLE_PASS;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Security Disable Password Command\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Disable Password", ret);
    }
    return ret;
}

int ata_Security_Erase_Prepare(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE; //28bit command
    ataCommandOptions.tfr.CommandStatus = ATA_SECURITY_ERASE_PREP;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Security Erase Prepare Command\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Erase Prepare", ret);
    }

    return ret;
}

int ata_Security_Erase_Unit(tDevice *device, uint8_t *ptrData, uint32_t timeout)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE; //28bit command
    ataCommandOptions.dataSize = LEGACY_DRIVE_SEC_SIZE; //spec defines that this command always transfers this much data
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.tfr.CommandStatus = ATA_SECURITY_ERASE_UNIT_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.timeout = timeout;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Security Erase Unit Command\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Erase Unit", ret);
    }

    return ret;
}

int ata_Security_Set_Password(tDevice *device, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE; //28bit command
    ataCommandOptions.dataSize = LEGACY_DRIVE_SEC_SIZE; //spec defines that this command always transfers this much data
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.tfr.CommandStatus = ATA_SECURITY_SET_PASS;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Security Set Password Command\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Set Password", ret);
    }

    return ret;
}

int ata_Security_Unlock(tDevice *device, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE; //28bit command
    ataCommandOptions.dataSize = LEGACY_DRIVE_SEC_SIZE; //spec defines that this command always transfers this much data
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.tfr.CommandStatus = ATA_SECURITY_UNLOCK_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Security Unlock Command\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Unlock", ret);
    }

    return ret;
}

int ata_Security_Freeze_Lock(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE; //28bit command
    ataCommandOptions.tfr.CommandStatus = ATA_SECURITY_FREEZE_LOCK_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Security Freeze Lock Command\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Erase Freeze Lock", ret);
    }

    return ret;
}

int ata_Accessible_Max_Address_Feature(tDevice *device, uint16_t feature, uint64_t lba, ataReturnTFRs *rtfrs)
{
    int             ret          = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.tfr.CommandStatus = ATA_ACCESSABLE_MAX_ADDR;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(feature);
    ataCommandOptions.tfr.Feature48 = M_Byte1(feature);
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(lba);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Accessible Max Address Command - Feature = 0x%04" PRIX16 ", LBA = %" PRIu64 "\n", feature, lba);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (rtfrs != NULL)
    {
        memcpy(rtfrs, &(ataCommandOptions.rtfr), sizeof(ataReturnTFRs));
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Accessible Max Address Command", ret);
    }

    return ret;
}

int ata_Get_Native_Max_Address_Ext(tDevice *device, uint64_t *nativeMaxLBA)
{
    int             ret   = UNKNOWN;
    ataReturnTFRs rtfrs;
    memset(&rtfrs, 0, sizeof(rtfrs));
    ret = ata_Accessible_Max_Address_Feature(device, AMAC_GET_NATIVE_MAX_ADDRESS, 0, &rtfrs);
    if (ret == SUCCESS && nativeMaxLBA)
    {
        *nativeMaxLBA = M_BytesTo8ByteValue(0, 0, rtfrs.lbaHiExt, rtfrs.lbaMidExt, rtfrs.lbaLowExt, rtfrs.lbaHi, rtfrs.lbaMid, rtfrs.lbaLow);
    }
    return ret;
}

int ata_Set_Accessible_Max_Address_Ext(tDevice *device, uint64_t newMaxLBA)
{
    int ret = UNKNOWN;
    ret = ata_Accessible_Max_Address_Feature(device, AMAC_SET_ACCESSIBLE_MAX_ADDRESS, newMaxLBA, NULL);
    return ret;
}

int ata_Freeze_Accessible_Max_Address_Ext(tDevice *device)
{
    int ret = UNKNOWN;
    ret = ata_Accessible_Max_Address_Feature(device, AMAC_FREEZE_ACCESSIBLE_MAX_ADDRESS, 0, NULL);
    return ret;
}

int ata_Read_Native_Max_Address(tDevice *device, uint64_t *nativeMaxLBA, bool ext)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = NULL;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (ext)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_MAX_ADDRESS_EXT;
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_MAX_ADDRESS;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read Native Max Address");
        if (ext)
        {
            printf(" Ext");
        }
        printf("\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS && nativeMaxLBA)
    {
        if (ext)
        {
            *nativeMaxLBA = M_BytesTo8ByteValue(0, 0, ataCommandOptions.rtfr.lbaHiExt, ataCommandOptions.rtfr.lbaMidExt, ataCommandOptions.rtfr.lbaLowExt, ataCommandOptions.rtfr.lbaHi, ataCommandOptions.rtfr.lbaMid, ataCommandOptions.rtfr.lbaLow);
        }
        else
        {
            *nativeMaxLBA = M_BytesTo4ByteValue(M_Nibble0(ataCommandOptions.rtfr.device), ataCommandOptions.rtfr.lbaHi, ataCommandOptions.rtfr.lbaMid, ataCommandOptions.rtfr.lbaLow);
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (ext)
        {
            print_Return_Enum("Read Native Max Address Ext", ret);
        }
        else
        {
            print_Return_Enum("Read Native Max Address", ret);
        }
    }
    return ret;
}
int ata_Set_Max(tDevice *device, eHPAFeature setMaxFeature, uint32_t newMaxLBA, bool volitileValue, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MAX;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, setMaxFeature);

    switch (setMaxFeature)
    {
    case HPA_SET_MAX_ADDRESS:
    case HPA_SET_MAX_FREEZE_LOCK:
    case HPA_SET_MAX_LOCK:
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        break;
    case HPA_SET_MAX_UNLOCK:
    case HPA_SET_MAX_PASSWORD:
        ataCommandOptions.commandDirection = XFER_DATA_OUT;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        ataCommandOptions.tfr.SectorCount = 1;//spec says NA, but this will help the lower layer just like the identify command
        break;
    default:
        return BAD_PARAMETER;
    }

    ataCommandOptions.tfr.LbaLow = M_Byte0(newMaxLBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(newMaxLBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(newMaxLBA);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(newMaxLBA);
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (volitileValue)
    {
        ataCommandOptions.tfr.SectorCount |= BIT0;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Set Max, LBA = %" PRIu32 ", %s\n", newMaxLBA, (volitileValue ? "Volatile" : "Non-Volatile"));
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Max", ret);
    }
    return ret;
}

int ata_Set_Max_Address(tDevice *device, uint32_t newMaxLBA, bool volitileValue)
{
    return ata_Set_Max(device, HPA_SET_MAX_ADDRESS, newMaxLBA, volitileValue, NULL, 0);
}

int ata_Set_Max_Password(tDevice *device, uint8_t *ptrData, uint32_t dataLength)
{
    return ata_Set_Max(device, HPA_SET_MAX_PASSWORD, 0, false, ptrData, dataLength);
}

int ata_Set_Max_Lock(tDevice *device)
{
    return ata_Set_Max(device, HPA_SET_MAX_LOCK, 0, false, NULL, 0);
}

int ata_Set_Max_Unlock(tDevice *device, uint8_t *ptrData, uint32_t dataLength)
{
    return ata_Set_Max(device, HPA_SET_MAX_UNLOCK, 0, false, ptrData, dataLength);
}

int ata_Set_Max_Freeze_Lock(tDevice *device)
{
    return ata_Set_Max(device, HPA_SET_MAX_FREEZE_LOCK, 0, false, NULL, 0);
}

int ata_Set_Max_Address_Ext(tDevice *device, uint64_t newMaxLBA, bool volatileValue)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MAX_EXT;
    ataCommandOptions.tfr.LbaLow = M_Byte0(newMaxLBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(newMaxLBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(newMaxLBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(newMaxLBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(newMaxLBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(newMaxLBA);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (volatileValue)
    {
        ataCommandOptions.tfr.SectorCount |= BIT0;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Set Native Max Address Ext, LBA = %" PRIu64 ", %s\n", newMaxLBA, (volatileValue ? "Volatile" : "Non-Volatile"));
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Native Max Address Ext", ret);
    }
    return ret;
}

int ata_Download_Microcode(tDevice *device, eDownloadMicrocodeFeatures subCommand, uint16_t blockCount, uint16_t bufferOffset, bool useDMA, uint8_t *pData, uint32_t dataLen, bool firstSegment, bool lastSegment, uint32_t timeoutSeconds)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.fwdlFirstSegment = firstSegment;
    ataCommandOptions.fwdlLastSegment = lastSegment;
    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_DOWNLOAD_MICROCODE_DMA;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_DOWNLOAD_MICROCODE_CMD;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.dataSize = dataLen;
    ataCommandOptions.ptrData = pData;
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, subCommand);
    ataCommandOptions.tfr.SectorCount = M_Byte0(blockCount);
    ataCommandOptions.tfr.LbaLow = M_Byte1(blockCount);
    ataCommandOptions.tfr.LbaMid = M_Byte0(bufferOffset);
    ataCommandOptions.tfr.LbaHi = M_Byte1(bufferOffset);

    if (ataCommandOptions.tfr.LbaLow > 0)
    {
        //change length location to TPSIU so the SATL knows how to calculate the transfer length correctly since it is no longer just the sector count field.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    }

    ataCommandOptions.timeout = timeoutSeconds;
    if (ataCommandOptions.timeout == 0)
    {
        ataCommandOptions.timeout = 30;//using 30 seconds since some firmwares can take a little longer to activate
    }

    //if we are told to use the activate command, we need to set the protocol to non-data, set pdata to NULL, and dataSize to 0
    if (subCommand == ATA_DL_MICROCODE_ACTIVATE)
    {
        ataCommandOptions.dataSize = 0;
        ataCommandOptions.ptrData = NULL;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.tfr.SectorCount = 0;
        ataCommandOptions.tfr.LbaLow = 0;
        ataCommandOptions.tfr.LbaMid = 0;
        ataCommandOptions.tfr.LbaHi = 0;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Download Microcode DMA, subcommand 0x%" PRIX8 "\n", C_CAST(uint8_t, subCommand));
        }
        else
        {
            printf("Sending ATA Download Microcode, subcommand 0x%" PRIX8 "\n", C_CAST(uint8_t, subCommand));
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Download Microcode DMA", ret);
        }
        else
        {
            print_Return_Enum("Download Microcode", ret);
        }
    }
    return ret;
}

int ata_Check_Power_Mode(tDevice *device, uint8_t *powerMode)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.tfr.CommandStatus = ATA_CHECK_POWER_MODE_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (!powerMode)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Check Power Mode\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        *powerMode = ataCommandOptions.rtfr.secCnt;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Check Power Mode", ret);
    }
    return ret;
}

int ata_Configure_Stream(tDevice *device, uint8_t streamID, bool addRemoveStreamBit, bool readWriteStreamBit, uint8_t defaultCCTL, uint16_t allocationUnit)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_CONFIGURE_STREAM;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.SectorCount = M_Byte0(allocationUnit);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(allocationUnit);
    //set default cctl
    ataCommandOptions.tfr.Feature48 = defaultCCTL;
    //set stream ID
    ataCommandOptions.tfr.ErrorFeature = streamID & 0x03;//stream ID is specified by bits 2:0
    //set add/remove stream bit
    if (addRemoveStreamBit)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT7;
    }
    //set the read/Write stream bit (obsolete starting in ATA8/ACS)
    if (readWriteStreamBit)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT6;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Configure Stream\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Configure Stream", ret);
    }
    return ret;
}

int ata_Data_Set_Management(tDevice *device, bool trimBit, uint8_t* ptrData, uint32_t dataSize, bool xl)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    switch (device->drive_info.ata_Options.dmaMode)
    {
    case ATA_DMA_MODE_NO_DMA:
        return BAD_PARAMETER;
    case ATA_DMA_MODE_UDMA:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
        break;
    case ATA_DMA_MODE_MWDMA:
    case ATA_DMA_MODE_DMA:
    default:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
        break;
    }
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.ptrData = ptrData;
    if (xl)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_DATA_SET_MANAGEMENT_XL_CMD;
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_DATA_SET_MANAGEMENT_CMD;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    //set the TRIM bit if asked
    if (trimBit)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT0;
    }
    //set the count registers
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (xl)
        {
            printf("Sending ATA Data Set Management XL\n");
        }
        else
        {
            printf("Sending ATA Data Set Management\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (xl)
        {
            print_Return_Enum("Data Set Management XL", ret);
        }
        else
        {
            print_Return_Enum("Data Set Management", ret);
        }
    }

    return ret;
}
int ata_Execute_Device_Diagnostic(tDevice *device, uint8_t* diagnosticCode)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_DEV_DIAG;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.tfr.CommandStatus = ATA_EXEC_DRV_DIAG;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Execute Device Diagnostic\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    //according to the spec, this command should always complete without error
    *diagnosticCode = ataCommandOptions.rtfr.error;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Data Execute Device Diagnostic", ret);
    }

    return ret;
}

int ata_Flush_Cache(tDevice *device, bool extendedCommand)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    //Changed from 45 seconds to max command timeout to make sure this has enough time to complete without the system sending a reset.
    //The spec mentions this can take up to 30 minutes, but that is likely a rare condition. It should usually complete faster than that on today's drives - TJE

    if (extendedCommand)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_FLUSH_CACHE_EXT;
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_FLUSH_CACHE_CMD;
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCommand)
        {
            printf("Sending ATA Flush Cache Ext\n");
        }
        else
        {
            printf("Sending ATA Flush Cache\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCommand)
        {
            print_Return_Enum("Flush Cache Ext", ret);
        }
        else
        {
            print_Return_Enum("Flush Cache", ret);
        }
    }
    return ret;
}

int ata_Idle(tDevice *device, uint8_t standbyTimerPeriod)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_IDLE_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.SectorCount = standbyTimerPeriod;
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Idle, standby timer = %" PRIX8 "h\n", standbyTimerPeriod);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Idle", ret);
    }
    return ret;
}
int ata_Idle_Immediate(tDevice *device, bool unloadFeature)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_IDLE_IMMEDIATE_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (unloadFeature)
    {
        ataCommandOptions.tfr.ErrorFeature = IDLE_IMMEDIATE_UNLOAD_FEATURE;
        ataCommandOptions.tfr.SectorCount = 0x00;
        ataCommandOptions.tfr.LbaHi = M_Byte2(IDLE_IMMEDIATE_UNLOAD_LBA);
        ataCommandOptions.tfr.LbaMid = M_Byte1(IDLE_IMMEDIATE_UNLOAD_LBA);
        ataCommandOptions.tfr.LbaLow = M_Byte0(IDLE_IMMEDIATE_UNLOAD_LBA);
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Idle Immediate %s\n", (unloadFeature ? " - Unload" : ""));
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Idle Immediate", ret);
    }
    return ret;
}
int ata_Read_Buffer(tDevice *device, uint8_t *ptrData, bool useDMA)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = LEGACY_DRIVE_SEC_SIZE;//defined in the spec that this will only read a 512byte block of data

    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_BUF_DMA;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_BUF;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Read Buffer DMA\n");
        }
        else
        {
            printf("Sending ATA Read Buffer\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Read Buffer DMA", ret);
        }
        else
        {
            print_Return_Enum("Read Buffer", ret);
        }
    }

    return ret;
}
int ata_Read_DMA(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    switch (device->drive_info.ata_Options.dmaMode)
    {
    case ATA_DMA_MODE_NO_DMA:
        return BAD_PARAMETER;
    case ATA_DMA_MODE_UDMA:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
        break;
    case ATA_DMA_MODE_MWDMA:
    case ATA_DMA_MODE_DMA:
    default:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
        break;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_DMA_EXT;//0x25
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_DMA_RETRY_CMD;//0xC8
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read DMA Ext\n");
        }
        else
        {
            printf("Sending ATA Read DMA\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read DMA Ext", ret);
        }
        else
        {
            print_Return_Enum("Read DMA", ret);
        }
    }

    return ret;
}

int ata_Read_Multiple(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_READ_MULTIPLE_EXT;//0x29
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_MULTIPLE_CMD;//0xC4
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    //now set the multiple count setting for the SAT builder so that this command can actually work...and we need to set this as a power of 2, whereas the device info is a number of logical sectors
    uint16_t multipleLogicalSectors = device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock;
    while (ataCommandOptions.multipleCount <= 7 && multipleLogicalSectors > 1)//multipleLogicalSectors should be greater than 1 so that we get the proper 2^X power value for the SAT command.
    {
        multipleLogicalSectors = multipleLogicalSectors >> 1;//divide by 2
        ataCommandOptions.multipleCount++;
    }

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read Multiple Ext\n");
        }
        else
        {
            printf("Sending ATA Read Multiple\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read Multiple Ext", ret);
        }
        else
        {
            print_Return_Enum("Read Multiple", ret);
        }
    }

    return ret;
}

int ata_Read_Sectors(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_SECT_EXT;//0x24
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_SECT;//0x20
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read Sectors Ext\n");
        }
        else
        {
            printf("Sending ATA Read Sectors\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read Sectors Ext", ret);
        }
        else
        {
            print_Return_Enum("Read Sectors", ret);
        }
    }

    return ret;
}

int ata_Read_Sectors_No_Retry(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_READ_SECT_NORETRY;//0x21

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read Sectors(No Retry)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Sectors(No Retry)", ret);
    }

    return ret;
}

int ata_Read_Stream_Ext(tDevice *device, bool useDMA, uint8_t streamID, bool notSequential, bool readContinuous, uint8_t commandCCTL, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_STREAM_DMA_EXT;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_STREAM_EXT;
    }
    //set the stream ID
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
    
    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Read Stream Ext DMA\n");
        }
        else
        {
            printf("Sending ATA Read Stream Ext\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Read Stream Ext DMA", ret);
        }
        else
        {
            print_Return_Enum("Read Stream Ext", ret);
        }
    }

    return ret;
}

int ata_Read_Verify_Sectors(tDevice *device, bool extendedCmd, uint16_t numberOfSectors, uint64_t LBA)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_VERIFY_EXT;
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfSectors);
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_VERIFY_RETRY;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read Verify Sectors Ext\n");
        }
        else
        {
            printf("Sending ATA Read Verify Sectors\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read Verify Sectors Ext", ret);
        }
        else
        {
            print_Return_Enum("Read Verify Sectors", ret);
        }
    }

    return ret;
}

int ata_Request_Sense_Data(tDevice *device, uint8_t *senseKey, uint8_t *additionalSenseCode, uint8_t *additionalSenseCodeQualifier)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_REQUEST_SENSE_DATA;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (!senseKey || !additionalSenseCode || !additionalSenseCodeQualifier)
    {
        return BAD_PARAMETER;
    }
    
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Request Sense Data\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        *senseKey = ataCommandOptions.rtfr.lbaHi & 0x0F;
        *additionalSenseCode = ataCommandOptions.rtfr.lbaMid;
        *additionalSenseCodeQualifier = ataCommandOptions.rtfr.lbaLow;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Request Sense Data", ret);
    }

    return ret;
}

int ata_Set_Date_And_Time(tDevice *device, uint64_t timeStamp)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_DATE_AND_TIME_EXT;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.LbaLow = M_Byte0(timeStamp);
    ataCommandOptions.tfr.LbaMid = M_Byte1(timeStamp);
    ataCommandOptions.tfr.LbaHi = M_Byte2(timeStamp);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(timeStamp);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(timeStamp);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(timeStamp);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Set Data & Time Ext - time stamp: %016" PRIX64 "h\n", timeStamp);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Data & Time Ext", ret);
    }

    return ret;
}

int ata_Set_Multiple_Mode(tDevice *device, uint8_t drqDataBlockCount)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MULTIPLE;
    ataCommandOptions.tfr.SectorCount = drqDataBlockCount;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Set Multiple Mode - DRQ block count: %" PRIu8 "\n", drqDataBlockCount);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Multiple Mode", ret);
    }

    return ret;
}
int ata_Sleep(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_SLEEP_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Sleep\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Sleep", ret);
    }

    return ret;
}

int ata_Standby(tDevice *device, uint8_t standbyTimerPeriod)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_STANDBY_CMD;
    ataCommandOptions.tfr.SectorCount = standbyTimerPeriod;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Standby, standby timer - %" PRIX8 "h\n", standbyTimerPeriod);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Standby", ret);
    }

    return ret;
}

int ata_Standby_Immediate(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_STANDBY_IMMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Standby Immediate\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Standby Immediate", ret);
    }

    return ret;
}

int ata_Trusted_Non_Data(tDevice *device, uint8_t securityProtocol, bool trustedSendReceiveBit, uint16_t securityProtocolSpecific)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_TRUSTED_NON_DATA;
    ataCommandOptions.tfr.ErrorFeature = securityProtocol;
    ataCommandOptions.tfr.LbaMid = M_Byte0(securityProtocolSpecific);
    ataCommandOptions.tfr.LbaHi = M_Byte1(securityProtocolSpecific);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (trustedSendReceiveBit)
    {
        ataCommandOptions.tfr.DeviceHead |= BIT0;//LBA bit 24
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Trusted Non-Data");
        printf(" - Security Protocol %02" PRIX16 ", Specific: %04" PRIX16 "\n", securityProtocol, securityProtocolSpecific);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Trusted Non-Data", ret);
    }

    return ret;
}

int ata_Trusted_Receive(tDevice *device, bool useDMA, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_TRUSTED_RECEIVE_DMA;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_TRUSTED_RECEIVE;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.ErrorFeature = securityProtocol;
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaLow = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaMid = M_Byte0(securityProtocolSpecific);
    ataCommandOptions.tfr.LbaHi = M_Byte1(securityProtocolSpecific);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (ataCommandOptions.tfr.LbaLow > 0)
    {
        //change length location to TPSIU so the SATL knows how to calculate the transfer length correctly since it is no longer just the sector count field.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Trusted Receive DMA");
        }
        else
        {
            printf("Sending ATA Trusted Receive");
        }
        printf(" - Security Protocol %02" PRIX16 ", Specific: %04" PRIX16 "\n", securityProtocol, securityProtocolSpecific);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Trusted Receive DMA", ret);
        }
        else
        {
            print_Return_Enum("Trusted Receive", ret);
        }
    }

    return ret;
}
int ata_Trusted_Send(tDevice *device, bool useDMA, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_TRUSTED_SEND_DMA;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_TRUSTED_SEND;
    }
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.ErrorFeature = securityProtocol;
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaLow = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaMid = M_Byte0(securityProtocolSpecific);
    ataCommandOptions.tfr.LbaHi = M_Byte1(securityProtocolSpecific);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (ataCommandOptions.tfr.LbaLow > 0)
    {
        //change length location to TPSIU so the SATL knows how to calculate the transfer length correctly since it is no longer just the sector count field.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Trusted Send DMA");
        }
        else
        {
            printf("Sending ATA Trusted Send");
        }
        printf(" - Security Protocol %02" PRIX16 ", Specific: %04" PRIX16 "\n", securityProtocol, securityProtocolSpecific);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Trusted Send DMA", ret);
        }
        else
        {
            print_Return_Enum("Trusted Send", ret);
        }
    }

    return ret;
}

int ata_Write_Buffer(tDevice *device, uint8_t *ptrData, bool useDMA)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = LEGACY_DRIVE_SEC_SIZE;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_BUF_DMA;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_BUF;
    }

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Write Buffer DMA\n");
        }
        else
        {
            printf("Sending ATA Write Buffer\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Write Buffer DMA", ret);
        }
        else
        {
            print_Return_Enum("Write Buffer", ret);
        }
    }

    return ret;
}

int ata_Write_DMA(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool fua)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    switch (device->drive_info.ata_Options.dmaMode)
    {
    case ATA_DMA_MODE_NO_DMA:
        return BAD_PARAMETER;
    case ATA_DMA_MODE_UDMA:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
        break;
    case ATA_DMA_MODE_MWDMA:
    case ATA_DMA_MODE_DMA:
    default:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
        break;
    }
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        if (fua)
        {
            ataCommandOptions.tfr.CommandStatus = ATA_WRITE_DMA_FUA_EXT;
        }
        else
        {
            ataCommandOptions.tfr.CommandStatus = ATA_WRITE_DMA_EXT;
        }
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_DMA_RETRY_CMD;
    }


    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Write DMA Ext\n");
        }
        else
        {
            printf("Sending ATA Write DMA\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Write DMA Ext", ret);
        }
        else
        {
            print_Return_Enum("Write DMA", ret);
        }
    }

    return ret;
}

int ata_Write_Multiple(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool fua)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        if (fua)
        {
            ataCommandOptions.tfr.CommandStatus = ATA_WRITE_MULTIPLE_FUA_EXT;
        }
        else
        {
            ataCommandOptions.tfr.CommandStatus = ATA_WRITE_MULTIPLE_EXT;
        }
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
        if ((dataSize / device->drive_info.deviceBlockSize) > 0xFFFF)
        {
            ataCommandOptions.tfr.SectorCount = 0;
            ataCommandOptions.tfr.SectorCount48 = 0;
        }
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_MULTIPLE_CMD;
        if ((dataSize / device->drive_info.deviceBlockSize) > 0xFF)
        {
            ataCommandOptions.tfr.SectorCount = 0;
        }
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    //now set the multiple count setting for the SAT builder so that this command can actually work...and we need to set this as a power of 2, whereas the device info is a number of logical sectors
    uint16_t multipleLogicalSectors = device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock;
    while (ataCommandOptions.multipleCount <= 7 && multipleLogicalSectors > 1)//multipleLogicalSectors should be greater than 1 so that we get the proper 2^X power value for the SAT command.
    {
        multipleLogicalSectors = multipleLogicalSectors >> 1;//divide by 2
        ataCommandOptions.multipleCount++;
    }

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Write Multiple Ext\n");
        }
        else
        {
            printf("Sending ATA Write Multiple\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Write Multiple Ext", ret);
        }
        else
        {
            print_Return_Enum("Write Multiple", ret);
        }
    }

    return ret;
}

int ata_Write_Sectors(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECT_EXT;
        ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
        ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
        ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    }
    else
    {
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECT;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Write Sectors Ext\n");
        }
        else
        {
            printf("Sending ATA Write Sectors\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Write Sectors Ext", ret);
        }
        else
        {
            print_Return_Enum("Write Sectors", ret);
        }
    }

    return ret;
}

int ata_Write_Sectors_No_Retry(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECT_NORETRY; //0x31
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Sectors(No Retry)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Sectors(No Retry)", ret);
    }

    return ret;
}

int ata_Write_Stream_Ext(tDevice *device, bool useDMA, uint8_t streamID, bool flush, bool writeContinuous, uint8_t commandCCTL, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (useDMA)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_STREAM_DMA_EXT;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
    }
    else
    {
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_STREAM_EXT;
    }
    //set the stream ID
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

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            printf("Sending ATA Write Stream Ext DMA\n");
        }
        else
        {
            printf("Sending ATA Write Stream Ext\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (useDMA)
        {
            print_Return_Enum("Write Stream Ext DMA", ret);
        }
        else
        {
            print_Return_Enum("Write Stream Ext", ret);
        }
    }

return ret;
}

int ata_Write_Uncorrectable(tDevice *device, uint8_t unrecoverableOptions, uint16_t numberOfSectors, uint64_t LBA)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfSectors);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_UNCORRECTABLE_EXT;

    if (unrecoverableOptions != WRITE_UNCORRECTABLE_PSEUDO_UNCORRECTABLE_WITH_LOGGING && unrecoverableOptions != WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_5AH && unrecoverableOptions != WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_A5H && unrecoverableOptions != WRITE_UNCORRECTABLE_FLAGGED_WITHOUT_LOGGING)
    {
        return BAD_PARAMETER;
    }
    ataCommandOptions.tfr.ErrorFeature = unrecoverableOptions;


    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Uncorrectable Ext - %02" PRIX8 "h, LBA = %" PRIu64 ", Count: %" PRIu16 "\n", unrecoverableOptions, LBA, numberOfSectors);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Uncorrectable Ext", ret);
    }

    return ret;
}

int ata_NV_Cache_Feature(tDevice *device, eNVCacheFeatures feature, uint16_t count, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(LBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(LBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(count);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(count);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(C_CAST(uint16_t, feature));
    ataCommandOptions.tfr.Feature48 = M_Byte1(C_CAST(uint16_t, feature));
    ataCommandOptions.tfr.CommandStatus = ATA_NV_CACHE;

    char* nvCacheFeature = NULL;
    switch (feature)
    {
    case NV_SET_NV_CACHE_POWER_MODE:
        nvCacheFeature = "Set NV Cache Power Mode";
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        break;
    case NV_RETURN_FROM_NV_CACHE_POWER_MODE:
        nvCacheFeature = "Return from NV Cache Power Mode";
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        break;
    case NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET:
        nvCacheFeature = "Add LBA(s) to NV Cache Pinned Set";
        ataCommandOptions.commandDirection = XFER_DATA_OUT;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
        break;
    case NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET:
        nvCacheFeature = "Remove LBA(s) from NV Cache Pinned Set";
        if (LBA & BIT0)
        {
            ataCommandOptions.commandDirection = XFER_NO_DATA;
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
            ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
            ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        }
        else
        {
            ataCommandOptions.commandDirection = XFER_DATA_OUT;
            switch (device->drive_info.ata_Options.dmaMode)
            {
            case ATA_DMA_MODE_NO_DMA:
                return BAD_PARAMETER;
            case ATA_DMA_MODE_UDMA:
                ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
                break;
            case ATA_DMA_MODE_MWDMA:
            case ATA_DMA_MODE_DMA:
            default:
                ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
                break;
            }
        }
        break;
    case NV_QUERY_NV_CACHE_PINNED_SET:
        nvCacheFeature = "Query NV Cache Pinned Set";
        ataCommandOptions.commandDirection = XFER_DATA_IN;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
        break;
    case NV_QUERY_NV_CACHE_MISSES:
        nvCacheFeature = "Query NV Cache Misses";
        ataCommandOptions.commandDirection = XFER_DATA_IN;
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
        break;
    case NV_FLUSH_NV_CACHE:
        nvCacheFeature = "Flush NV Cache";
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        break;
    case NV_CACHE_ENABLE:
        nvCacheFeature = "NV Cache Enable";
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        break;
    case NV_CACHE_DISABLE:
        nvCacheFeature = "NV Cache Disable";
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        break;
    default:
        nvCacheFeature = "Unknown NV Cache feature";
        break;
    }

    if (ataCommandOptions.commandDirection != XFER_NO_DATA)
    {
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Non-Volatile Cache - %s\n", nvCacheFeature);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        switch (feature)
        {
        case NV_SET_NV_CACHE_POWER_MODE:
            print_Return_Enum("Non-Volatile Cache - Set NV Cache Power Mode",ret);
            break;
        case NV_RETURN_FROM_NV_CACHE_POWER_MODE:
            print_Return_Enum("Non-Volatile Cache - Return from NV Cache Power Mode", ret);
            break;
        case NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET:
            print_Return_Enum("Non-Volatile Cache - Add LBA(s) to NV Cache Pinned Set", ret);
            break;
        case NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET:
            print_Return_Enum("Non-Volatile Cache - Remove LBA(s) from NV Cache Pinned Set", ret);
            break;
        case NV_QUERY_NV_CACHE_PINNED_SET:
            print_Return_Enum("Non-Volatile Cache - Query NV Cache Pinned Set", ret);
            break;
        case NV_QUERY_NV_CACHE_MISSES:
            print_Return_Enum("Non-Volatile Cache - Query NV Cache Misses", ret);
            break;
        case NV_FLUSH_NV_CACHE:
            print_Return_Enum("Non-Volatile Cache - Flush NV Cache", ret);
            break;
        case NV_CACHE_ENABLE:
            print_Return_Enum("Non-Volatile Cache - NV Cache Enable", ret);
            break;
        case NV_CACHE_DISABLE:
            print_Return_Enum("Non-Volatile Cache - NV Cache Disable", ret);
            break;
        default:
            print_Return_Enum("Non-Volatile Cache - Unknown NV Cache feature", ret);
            break;
        }
    }

    return ret;
}

int ata_NV_Cache_Add_LBAs_To_Cache(tDevice *device, bool populateImmediately, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    uint64_t lba = 0;
    if (populateImmediately)
    {
        lba |= BIT0;
    }
    ret = ata_NV_Cache_Feature(device, NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET, C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), lba, ptrData, dataSize);
    return ret;
}

int ata_NV_Flush_NV_Cache(tDevice *device, uint32_t minNumberOfLogicalBlocks)
{
    int ret = UNKNOWN;
    ret = ata_NV_Cache_Feature(device, NV_FLUSH_NV_CACHE, 0, C_CAST(uint64_t, minNumberOfLogicalBlocks), NULL, 0);
    return ret;
}

int ata_NV_Cache_Disable(tDevice *device)
{
    int ret = UNKNOWN;
    ret = ata_NV_Cache_Feature(device, NV_CACHE_DISABLE, 0, 0, NULL, 0);
    return ret;
}

int ata_NV_Cache_Enable(tDevice *device)
{
    int ret = UNKNOWN;
    ret = ata_NV_Cache_Feature(device, NV_CACHE_ENABLE, 0, 0, NULL, 0);
    return ret;
}

int ata_NV_Query_Misses(tDevice *device, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    ret = ata_NV_Cache_Feature(device, NV_QUERY_NV_CACHE_MISSES, 0x0001, 0, ptrData, 512);
    return ret;
}

int ata_NV_Query_Pinned_Set(tDevice *device, uint64_t dataBlockNumber, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ret = ata_NV_Cache_Feature(device, NV_QUERY_NV_CACHE_PINNED_SET, C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), dataBlockNumber, ptrData, dataSize);
    return ret;
}

int ata_NV_Remove_LBAs_From_Cache(tDevice *device, bool unpinAll, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;

    uint64_t lba = 0;

    if (unpinAll)
    {
        lba |= BIT0;
        ret = ata_NV_Cache_Feature(device, NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET, 0, lba, NULL, 0);
    }
    else
    {
        ret = ata_NV_Cache_Feature(device, NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET, C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), lba, ptrData, dataSize);
    }

    return ret;
}
int ata_Set_Features(tDevice *device, uint8_t subcommand, uint8_t subcommandCountField, uint8_t subcommandLBALo, uint8_t subcommandLBAMid, uint16_t subcommandLBAHi)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.LbaLow = subcommandLBALo;
    ataCommandOptions.tfr.LbaMid = subcommandLBAMid;
    ataCommandOptions.tfr.LbaHi = M_Byte0(subcommandLBAHi);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.DeviceHead |= M_Nibble2(subcommandLBAHi);
    ataCommandOptions.tfr.SectorCount = subcommandCountField;
    ataCommandOptions.tfr.ErrorFeature = subcommand;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_FEATURE;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Set Features - subcommand %02" PRIX8 "h\n", subcommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Features", ret);
    }
    return ret;
}

int ata_EPC_Restore_Power_Condition_Settings(tDevice *device, uint8_t powerConditionID, bool defaultBit, bool save)
{
    int ret = UNKNOWN;
    uint8_t lbaLo = 0;//restore power condition subcommand
    if (defaultBit)
    {
        lbaLo |= BIT6;
    }
    if (save)
    {
        lbaLo |= BIT4;
    }
    ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerConditionID, lbaLo, RESERVED, RESERVED);
    return ret;
}

int ata_EPC_Go_To_Power_Condition(tDevice *device, uint8_t powerConditionID, bool delayedEntry, bool holdPowerCondition)
{
    int ret = UNKNOWN;
    uint8_t lbaLo = 1;//go to power condition subcommand
    uint16_t lbaHi = 0;
    if (delayedEntry)
    {
        lbaHi |= BIT9;
    }
    if (holdPowerCondition)
    {
        lbaHi |= BIT8;
    }
    ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerConditionID, lbaLo, RESERVED, lbaHi);
    return ret;
}

int ata_EPC_Set_Power_Condition_Timer(tDevice *device, uint8_t powerConditionID, uint16_t timerValue, bool timerUnits, bool enable, bool save)
{
    int ret = UNKNOWN;
    uint8_t lbaLo = 2;//set power condition timer subcommand
    uint8_t lbaMid = M_Byte0(timerValue);
    uint16_t lbaHi = M_Byte1(timerValue);
    if (save)
    {
        lbaLo |= BIT4;
    }
    if (enable)
    {
        lbaLo |= BIT5;
    }
    if (timerUnits)
    {
        lbaLo |= BIT7;
    }
    ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerConditionID, lbaLo, lbaMid, lbaHi);
    return ret;
}

int ata_EPC_Set_Power_Condition_State(tDevice *device, uint8_t powerConditionID, bool enable, bool save)
{
    int ret = UNKNOWN;
    uint8_t lbaLo = 3;//set power condition state subcommand
    if (save)
    {
        lbaLo |= BIT4;
    }
    if (enable)
    {
        lbaLo |= BIT5;
    }
    ret = ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerConditionID, lbaLo, RESERVED, RESERVED);
    return ret;
}

int ata_EPC_Enable_EPC_Feature_Set(tDevice *device)
{
    return ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, RESERVED, 4, RESERVED, RESERVED);
}

int ata_EPC_Disable_EPC_Feature_Set(tDevice *device)
{
    return ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, RESERVED, 5, RESERVED, RESERVED);
}

int ata_EPC_Set_EPC_Power_Source(tDevice *device, uint8_t powerSource)
{
    return ata_Set_Features(device, SF_EXTENDED_POWER_CONDITIONS, powerSource & 0x02, 6, RESERVED, RESERVED);
}

int ata_Identify_Packet_Device(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.LbaLow = 0;
    ataCommandOptions.tfr.LbaMid = 0;
    ataCommandOptions.tfr.LbaHi = 0;
    ataCommandOptions.tfr.SectorCount = 0;
    ataCommandOptions.tfr.ErrorFeature = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.CommandStatus = ATAPI_IDENTIFY;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Identify Packet Device\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS && ptrData != (uint8_t*)&device->drive_info.IdentifyData.ata.Word000)
    {
        //copy the data to the device structure so that it's not (as) stale
        memcpy(&device->drive_info.IdentifyData.ata.Word000, ptrData, sizeof(tAtaIdentifyData));
    }

#if defined (__BIG_ENDIAN__)
    if(ptrData == (uint8_t*)&device->drive_info.IdentifyData.ata.Word000)
    {
        byte_Swap_ID_Data_Buffer(&device->drive_info.IdentifyData.ata.Word000);
    }
#endif

    if (ret == SUCCESS)
    {
        if (ptrData[510] == ATA_CHECKSUM_VALIDITY_INDICATOR)
        {
            //we got data, so validate the checksum
            uint32_t invalidSec = 0;
            if (!is_Checksum_Valid(ptrData, LEGACY_DRIVE_SEC_SIZE, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
            }
        }
        else
        {
            //Don't do anything. Device doesn't use a checksum
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Identify Packet Device", ret);
    }
    return ret;
}

int ata_Device_Configuration_Overlay_Feature(tDevice *device, eDCOFeatures dcoFeature, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.LbaLow = 0;
    ataCommandOptions.tfr.LbaMid = 0;
    ataCommandOptions.tfr.LbaHi = 0;
    ataCommandOptions.tfr.SectorCount = 0;
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, dcoFeature);
    ataCommandOptions.tfr.CommandStatus = ATA_DCO;

    //default
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;

    char* dcoFeatureString = NULL;
    switch (dcoFeature)
    {
    case DCO_RESTORE:
        dcoFeatureString = "Restore";
        break;
    case DCO_FREEZE_LOCK:
        dcoFeatureString = "Freeze Lock";
        break;
    case DCO_IDENTIFY:
        dcoFeatureString = "Identify";
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.commandDirection = XFER_DATA_IN;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        break;
    case DCO_SET:
        dcoFeatureString = "Set";
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
        ataCommandOptions.commandDirection = XFER_DATA_OUT;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        break;
    case DCO_IDENTIFY_DMA:
        dcoFeatureString = "Identify DMA";
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
        ataCommandOptions.commandDirection = XFER_DATA_IN;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        break;
    case DCO_SET_DMA:
        dcoFeatureString = "Set DMA";
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
        ataCommandOptions.commandDirection = XFER_DATA_OUT;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        break;
    default:
        dcoFeatureString = "Unknown DCO feature";
        break;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Device Configuration Overlay - %s\n", dcoFeatureString);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        switch (dcoFeature)
        {
        case DCO_RESTORE:
            print_Return_Enum("Device Configuration Overlay Command - Restore", ret);
            break;
        case DCO_FREEZE_LOCK:
            print_Return_Enum("Device Configuration Overlay Command - Freeze Lock", ret);
            break;
        case DCO_IDENTIFY:
            print_Return_Enum("Device Configuration Overlay Command - Identify", ret);
            break;
        case DCO_SET:
            print_Return_Enum("Device Configuration Overlay Command - Set", ret);
            break;
        case DCO_IDENTIFY_DMA:
            print_Return_Enum("Device Configuration Overlay Command - Identify DMA", ret);
            break;
        case DCO_SET_DMA:
            print_Return_Enum("Device Configuration Overlay Command - Set DMA", ret);
            break;
        default:
            print_Return_Enum("Device Configuration Overlay - Unknown feature", ret);
            break;
        }
    }
    return ret;
}

int ata_DCO_Restore(tDevice *device)
{
    return ata_Device_Configuration_Overlay_Feature(device, DCO_RESTORE, NULL, 0);
}

int ata_DCO_Freeze_Lock(tDevice *device)
{
    return ata_Device_Configuration_Overlay_Feature(device, DCO_FREEZE_LOCK, NULL, 0);
}

int ata_DCO_Identify(tDevice *device, bool dma, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = ata_Device_Configuration_Overlay_Feature(device, dma ? DCO_IDENTIFY_DMA : DCO_IDENTIFY, ptrData, dataSize);
    if (ret == SUCCESS)
    {
        if (ptrData[510] == ATA_CHECKSUM_VALIDITY_INDICATOR)
        {
            //we got data, so validate the checksum
            uint32_t invalidSec = 0;
            if (!is_Checksum_Valid(ptrData, LEGACY_DRIVE_SEC_SIZE, &invalidSec))
            {
                ret = WARN_INVALID_CHECKSUM;
            }
        }
        else
        {
            //don't do anything. Device doesn't use a checksum
        }
    }
    return ret;
}

int ata_DCO_Set(tDevice *device, bool dma, uint8_t *ptrData, uint32_t dataSize)
{
    return ata_Device_Configuration_Overlay_Feature(device, dma ? DCO_SET_DMA : DCO_SET, ptrData, dataSize);
}

//int ata_Packet(tDevice *device, uint8_t *scsiCDB, bool dmaBit, bool dmaDirBit, uint16_t byteCountLimit, uint8_t *ptrData, uint32_t *dataSize)
//{
//    ataPassthroughCommand packetCommand;
//    memset(&packetCommand, 0, sizeof(ataPassthroughCommand));
//    packetCommand.commadProtocol = ATA_CMD_TYPE_PACKET;
//    packetCommand.tfr.CommandStatus = ATAPI_COMMAND;
//    if (dmaBit)
//    {
//        packetCommand.tfr.ErrorFeature |= BIT0;
//    }
//    if (dmaDirBit)
//    {
//        packetCommand.tfr.ErrorFeature |= BIT2;
//    }
//}

int ata_ZAC_Management_In(tDevice *device, eZMAction action, uint8_t actionSpecificFeatureExt, uint16_t returnPageCount, uint64_t actionSpecificLBA, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_ZONE_MANAGEMENT_IN;
    ataCommandOptions.tfr.Feature48 = actionSpecificFeatureExt;
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, action);
    ataCommandOptions.tfr.SectorCount = RESERVED;
    ataCommandOptions.tfr.SectorCount48 = RESERVED;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.LbaLow = M_Byte0(actionSpecificLBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(actionSpecificLBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(actionSpecificLBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(actionSpecificLBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(actionSpecificLBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(actionSpecificLBA);
    switch (action)
    {
    case ZM_ACTION_REPORT_ZONES:
        switch (device->drive_info.ata_Options.dmaMode)
        {
        case ATA_DMA_MODE_NO_DMA:
            return BAD_PARAMETER;
        case ATA_DMA_MODE_UDMA:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
            break;
        case ATA_DMA_MODE_MWDMA:
        case ATA_DMA_MODE_DMA:
        default:
            ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
            break;
        }
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
        ataCommandOptions.commandDirection = XFER_DATA_IN;
        break;
    default://Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
    }

    if (ataCommandOptions.commadProtocol != ATA_PROTOCOL_NO_DATA)
    {
        ataCommandOptions.tfr.SectorCount = M_Byte0(returnPageCount);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(returnPageCount);
    }


    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Zone Management In\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management In", ret);
    }

    return ret;
}

int ata_ZAC_Management_Out(tDevice *device, eZMAction action, uint8_t actionSpecificFeatureExt, uint16_t pagesToSend_ActionSpecific, uint64_t actionSpecificLBA, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_ZONE_MANAGEMENT_OUT;
    ataCommandOptions.tfr.Feature48 = actionSpecificFeatureExt;
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, action);
    ataCommandOptions.tfr.SectorCount = RESERVED;
    ataCommandOptions.tfr.SectorCount48 = RESERVED;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.LbaLow = M_Byte0(actionSpecificLBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(actionSpecificLBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(actionSpecificLBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(actionSpecificLBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(actionSpecificLBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(actionSpecificLBA);
    switch (action)
    {
    case ZM_ACTION_CLOSE_ZONE:
    case ZM_ACTION_FINISH_ZONE:
    case ZM_ACTION_OPEN_ZONE:
    case ZM_ACTION_RESET_WRITE_POINTERS:
        ataCommandOptions.commandDirection = XFER_NO_DATA;
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
        ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
        break;
    case ZM_ACTION_REPORT_ZONES://this is a zone management in command, so return bad parameter
    default://Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
    }

    if (ataCommandOptions.commadProtocol != ATA_PROTOCOL_NO_DATA)
    {
        ataCommandOptions.tfr.SectorCount = M_Byte0(pagesToSend_ActionSpecific);
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(pagesToSend_ActionSpecific);
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Zone Management Out\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);
    
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management Out", ret);
    }

    return ret;
}

int ata_Close_Zone_Ext(tDevice *device, bool closeAll, uint64_t zoneID)
{
    if (closeAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_CLOSE_ZONE, BIT0, RESERVED, 0, NULL, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_CLOSE_ZONE, RESERVED, RESERVED, zoneID, NULL, 0);
    }
}

int ata_Finish_Zone_Ext(tDevice *device, bool finishAll, uint64_t zoneID)
{
    if (finishAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_FINISH_ZONE, BIT0, RESERVED, 0, NULL, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_FINISH_ZONE, RESERVED, RESERVED, zoneID, NULL, 0);
    }
}


int ata_Open_Zone_Ext(tDevice *device, bool openAll, uint64_t zoneID)
{
    if (openAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_OPEN_ZONE, BIT0, RESERVED, 0, NULL, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_OPEN_ZONE, RESERVED, RESERVED, zoneID, NULL, 0);
    }
}

int ata_Report_Zones_Ext(tDevice *device, eZoneReportingOptions reportingOptions, bool partial, uint16_t returnPageCount, uint64_t zoneLocator, uint8_t *ptrData, uint32_t dataSize)
{
    uint8_t actionSpecificFeatureExt = C_CAST(uint8_t, reportingOptions);
    if (partial)
    {
        actionSpecificFeatureExt |= BIT7;
    }
    return ata_ZAC_Management_In(device, ZM_ACTION_REPORT_ZONES, actionSpecificFeatureExt, returnPageCount, zoneLocator, ptrData, dataSize);
}

int ata_Reset_Write_Pointers_Ext(tDevice *device, bool resetAll, uint64_t zoneID)
{
    if (resetAll)
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_RESET_WRITE_POINTERS, BIT0, RESERVED, 0, NULL, 0);
    }
    else
    {
        return ata_ZAC_Management_Out(device, ZM_ACTION_RESET_WRITE_POINTERS, RESERVED, RESERVED, zoneID, NULL, 0);
    }
}

int ata_Media_Eject(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_MEDIA_EJECT;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Media Eject\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Media Eject", ret);
    }

    return ret;
}

int ata_Get_Media_Status(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_GET_MEDIA_STATUS;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Get Media Status\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get Media Status", ret);
    }

    return ret;
}

int ata_Media_Lock(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_DOOR_LOCK_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Media Lock\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Media Lock", ret);
    }

    return ret;
}

int ata_Media_Unlock(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_DOOR_UNLOCK_CMD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Media Unlock\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Media Unlock", ret);
    }

    return ret;
}

int ata_Zeros_Ext(tDevice *device, uint16_t numberOfLogicalSectors, uint64_t lba, bool trim)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_ZEROS_EXT;
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfLogicalSectors);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfLogicalSectors);
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte3(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(lba);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    
    if (trim)
    {
        ataCommandOptions.tfr.ErrorFeature |= BIT0;
    }
    
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Zeros Ext - LBA %" PRIu64 ", count %"PRIu16 " %s\n", lba, numberOfLogicalSectors, (trim ? "(TRIM)" : ""));
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zeros Ext", ret);
    }

    return ret;
}

int ata_Set_Sector_Configuration_Ext(tDevice *device, uint16_t commandCheck, uint8_t sectorConfigurationDescriptorIndex)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_SECTOR_CONFIG_EXT;
    ataCommandOptions.tfr.SectorCount = sectorConfigurationDescriptorIndex & 0x07;
    ataCommandOptions.tfr.Feature48 = M_Byte1(commandCheck);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(commandCheck);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Set Sector Configuration Ext\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Sector Configuration Ext", ret);
    }

    return ret;
}

int ata_Get_Physical_Element_Status(tDevice *device, uint8_t filter, uint8_t reportType, uint64_t startingElement, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    switch (device->drive_info.ata_Options.dmaMode)
    {
    case ATA_DMA_MODE_NO_DMA:
        return BAD_PARAMETER;
    case ATA_DMA_MODE_UDMA:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_UDMA;
        break;
    case ATA_DMA_MODE_MWDMA:
    case ATA_DMA_MODE_DMA:
    default:
        ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA;
        break;
    }
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_GET_PHYSICAL_ELEMENT_STATUS;
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.Feature48 = (filter << 6) | (reportType & 0x0F);//filter is 2 bits, report type is 4 bits. All others are reserved
    ataCommandOptions.tfr.ErrorFeature = RESERVED;
    ataCommandOptions.tfr.LbaLow = M_Byte0(startingElement);
    ataCommandOptions.tfr.LbaMid = M_Byte1(startingElement);
    ataCommandOptions.tfr.LbaHi = M_Byte2(startingElement);
    ataCommandOptions.tfr.LbaLow48 = M_Byte3(startingElement);
    ataCommandOptions.tfr.LbaMid48 = M_Byte4(startingElement);
    ataCommandOptions.tfr.LbaHi48 = M_Byte5(startingElement);
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Get Physical Element Status\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get Physical Element Status", ret);
    }

    return ret;
}

int ata_Remove_Element_And_Truncate(tDevice *device, uint32_t elementIdentifier, uint64_t requestedMaxLBA)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_REMOVE_AND_TRUNCATE;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.SectorCount = M_Byte0(elementIdentifier);
    ataCommandOptions.tfr.SectorCount48 = M_Byte1(elementIdentifier);
    ataCommandOptions.tfr.ErrorFeature = M_Byte2(elementIdentifier);
    ataCommandOptions.tfr.Feature48 = M_Byte3(elementIdentifier);
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    ataCommandOptions.tfr.LbaLow = M_Byte0(requestedMaxLBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(requestedMaxLBA);
    ataCommandOptions.tfr.LbaHi = M_Byte3(requestedMaxLBA);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(requestedMaxLBA);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(requestedMaxLBA);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(requestedMaxLBA);
    
    
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Remove And Truncate\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Remove And Truncate", ret);
    }
    return ret;
}

int ata_Restore_Elements_And_Rebuild(tDevice *device)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_RESTORE_AND_REBUILD;
    if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
    {
        ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }
    ataCommandOptions.tfr.SectorCount = RESERVED;
    ataCommandOptions.tfr.SectorCount48 = RESERVED;
    ataCommandOptions.tfr.ErrorFeature = RESERVED;
    ataCommandOptions.tfr.Feature48 = RESERVED;
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    ataCommandOptions.tfr.LbaLow = RESERVED;
    ataCommandOptions.tfr.LbaMid = RESERVED;
    ataCommandOptions.tfr.LbaHi = RESERVED;
    ataCommandOptions.tfr.LbaLow48 = RESERVED;
    ataCommandOptions.tfr.LbaMid48 = RESERVED;
    ataCommandOptions.tfr.LbaHi48 = RESERVED;


    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Restore Elements and Rebuild\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Restore Elements and Rebuild", ret);
    }
    return ret;
}

/////////////////////////////////////////////
/// Asynchronous Commands below this line ///
/////////////////////////////////////////////

int ata_NCQ_Non_Data(tDevice *device, uint8_t subCommand /*bits 4:0*/, uint16_t subCommandSpecificFeature /*bits 11:0*/, uint8_t subCommandSpecificCount, uint8_t ncqTag /*bits 5:0*/, uint64_t lba, uint32_t auxilary)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_FPDMA_NON_DATA;
    ataCommandOptions.tfr.Feature48 = M_Byte1(C_CAST(uint64_t, subCommandSpecificFeature) << 4);
    ataCommandOptions.tfr.ErrorFeature = (M_Nibble0(C_CAST(uint64_t, subCommandSpecificFeature)) << 4) | M_Nibble0(subCommand);
    ataCommandOptions.tfr.SectorCount48 = subCommandSpecificCount;
    ataCommandOptions.tfr.SectorCount = ncqTag << 3;//shift into bits 7:3
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte3(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(lba);
    ataCommandOptions.tfr.aux1 = M_Byte0(auxilary);
    ataCommandOptions.tfr.aux2 = M_Byte1(auxilary);
    ataCommandOptions.tfr.aux3 = M_Byte2(auxilary);
    ataCommandOptions.tfr.aux4 = M_Byte3(auxilary);
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA NCQ Non Data, Subcommand %u\n", subCommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("NCQ Non Data", ret);
    }

    return ret;
}

int ata_NCQ_Abort_NCQ_Queue(tDevice *device, uint8_t abortType /*bits0:3*/, uint8_t prio /*bits 1:0*/, uint8_t ncqTag, uint8_t tTag)
{
    return ata_NCQ_Non_Data(device, 0, abortType, C_CAST(uint16_t, prio) << 6, ncqTag, C_CAST(uint64_t, tTag) << 3, 0);
}

int ata_NCQ_Deadline_Handling(tDevice *device, bool rdnc, bool wdnc, uint8_t ncqTag)
{
    uint16_t ft = 0;
    if (rdnc)
    {
        ft |= BIT5;
    }
    if (wdnc)
    {
        ft |= BIT4;
    }
    return ata_NCQ_Non_Data(device, 1, ft >> 4, RESERVED, ncqTag, RESERVED, 0);
}

int ata_NCQ_Set_Features(tDevice *device, eATASetFeaturesSubcommands subcommand, uint8_t subcommandCountField, uint8_t subcommandLBALo, uint8_t subcommandLBAMid, uint16_t subcommandLBAHi, uint8_t ncqTag)
{
    uint64_t lba = M_BytesTo4ByteValue(M_Nibble0(M_Byte1(subcommandLBAHi)), M_Byte0(subcommandLBAHi), subcommandLBAMid, subcommandLBALo);
    return ata_NCQ_Non_Data(device, UINT8_C(5), C_CAST(uint16_t, subcommand << 4), subcommandCountField, ncqTag, lba, RESERVED);
}

//ncq zeros ext
int ata_NCQ_Zeros_Ext(tDevice *device, uint16_t numberOfLogicalSectors, uint64_t lba, bool trim, uint8_t ncqTag)
{
    return ata_NCQ_Non_Data(device, 6, M_Byte0(numberOfLogicalSectors) << 4, M_Byte1(numberOfLogicalSectors), ncqTag, lba, trim ? BIT1 : 0);
}

//ncq zac management out

int ata_NCQ_Receive_FPDMA_Queued(tDevice *device, uint8_t subCommand /*bits 5:0*/, uint16_t sectorCount /*ft*/, uint8_t prio /*bits 1:0*/, uint8_t ncqTag, uint64_t lba, uint32_t auxilary, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = sectorCount * LEGACY_DRIVE_SEC_SIZE;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA_FPDMA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_FEATURES_REGISTER;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_RECEIVE_FPDMA;
    ataCommandOptions.tfr.Feature48 = M_Byte1(sectorCount);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(sectorCount);
    ataCommandOptions.tfr.SectorCount48 = (subCommand & 0x1F) | ((prio & 0x03) << 6);//prio, subcommand
    ataCommandOptions.tfr.SectorCount = ncqTag << 3;//shift into bits 7:3
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte3(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(lba);
    ataCommandOptions.tfr.aux1 = M_Byte0(auxilary);
    ataCommandOptions.tfr.aux2 = M_Byte1(auxilary);
    ataCommandOptions.tfr.aux3 = M_Byte2(auxilary);
    ataCommandOptions.tfr.aux4 = M_Byte3(auxilary);
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (ataCommandOptions.tfr.aux1 ||
        ataCommandOptions.tfr.aux2 ||
        ataCommandOptions.tfr.aux3 ||
        ataCommandOptions.tfr.aux4 ||
        ataCommandOptions.tfr.icc
        )
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_COMPLETE_TASKFILE;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Receive FPDMA Queued, Subcommand %u\n", subCommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Receive FPDMA Queued", ret);
    }

    return ret;
}

//ncq read log dma ext
int ata_NCQ_Read_Log_DMA_Ext(tDevice *device, uint8_t logAddress, uint16_t pageNumber, uint8_t *ptrData, uint32_t dataSize, uint16_t featureRegister, uint8_t prio /*bits 1:0*/, uint8_t ncqTag)
{
    uint64_t lba = M_BytesTo8ByteValue(0, 0, RESERVED, M_Byte1(pageNumber), RESERVED, RESERVED, M_Byte0(pageNumber), logAddress);
    return ata_NCQ_Receive_FPDMA_Queued(device, UINT8_C(1), C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), prio, ncqTag, lba, featureRegister, ptrData);
}

//ncq ZAC management in

int ata_NCQ_Send_FPDMA_Queued(tDevice *device, uint8_t subCommand /*bits 5:0*/, uint16_t sectorCount /*ft*/, uint8_t prio /*bits 1:0*/, uint8_t ncqTag, uint64_t lba, uint32_t auxilary, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = sectorCount * LEGACY_DRIVE_SEC_SIZE;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA_FPDMA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_FEATURES_REGISTER;
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_SEND_FPDMA;
    ataCommandOptions.tfr.Feature48 = M_Byte1(sectorCount);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(sectorCount);
    ataCommandOptions.tfr.SectorCount48 = (subCommand & 0x1F) | ((prio & 0x03) << 6);//prio, subcommand
    ataCommandOptions.tfr.SectorCount = ncqTag << 3;//shift into bits 7:3
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte3(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(lba);
    ataCommandOptions.tfr.aux1 = M_Byte0(auxilary);
    ataCommandOptions.tfr.aux2 = M_Byte1(auxilary);
    ataCommandOptions.tfr.aux3 = M_Byte2(auxilary);
    ataCommandOptions.tfr.aux4 = M_Byte3(auxilary);
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (ataCommandOptions.tfr.aux1 ||
        ataCommandOptions.tfr.aux2 ||
        ataCommandOptions.tfr.aux3 ||
        ataCommandOptions.tfr.aux4 ||
        ataCommandOptions.tfr.icc
        )
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_COMPLETE_TASKFILE;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Send FPDMA Queued, Subcommand %u\n", subCommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Send FPDMA Queued", ret);
    }

    return ret;
}

//ncq data set management
int ata_NCQ_Data_Set_Management(tDevice *device, bool trimBit, uint8_t* ptrData, uint32_t dataSize, uint8_t prio /*bits 1:0*/, uint8_t ncqTag)
{
    uint32_t auxreg = 0;//bits 15:0 represent feature register of the NCQ data set management command.
    if (trimBit)
    {
        auxreg |= BIT0;
    }
    return ata_NCQ_Send_FPDMA_Queued(device, UINT8_C(0), C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), prio, ncqTag, RESERVED, auxreg, ptrData);
}

//ncq write log DMA ext
int ata_NCQ_Write_Log_DMA_Ext(tDevice *device, uint8_t logAddress, uint16_t pageNumber, uint8_t *ptrData, uint32_t dataSize, uint8_t prio /*bits 1:0*/, uint8_t ncqTag)
{
    uint64_t lba = M_BytesTo8ByteValue(0, 0, RESERVED, M_Byte1(pageNumber), RESERVED, RESERVED, M_Byte0(pageNumber), logAddress);
    return ata_NCQ_Send_FPDMA_Queued(device, UINT8_C(2), C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), prio, ncqTag, lba, RESERVED, ptrData);
}

//ncq ZAC management out

int ata_NCQ_Read_FPDMA_Queued(tDevice *device, bool fua, uint64_t lba, uint8_t *ptrData, uint16_t sectorCount, uint8_t prio, uint8_t ncqTag, uint8_t icc)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = sectorCount * device->drive_info.deviceBlockSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA_FPDMA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_FEATURES_REGISTER;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_READ_FPDMA_QUEUED_CMD;
    ataCommandOptions.tfr.Feature48 = M_Byte1(sectorCount);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(sectorCount);
    ataCommandOptions.tfr.SectorCount48 = ((prio & 0x03) << 6);//prio
    ataCommandOptions.tfr.SectorCount = ncqTag << 3;//shift into bits 7:3
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte3(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(lba);
    ataCommandOptions.tfr.aux1 = RESERVED;
    ataCommandOptions.tfr.aux2 = RESERVED;
    ataCommandOptions.tfr.aux3 = RESERVED;
    ataCommandOptions.tfr.aux4 = RESERVED;
    ataCommandOptions.tfr.icc = icc;
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (ataCommandOptions.tfr.aux1 ||
        ataCommandOptions.tfr.aux2 ||
        ataCommandOptions.tfr.aux3 ||
        ataCommandOptions.tfr.aux4 ||
        ataCommandOptions.tfr.icc
        )
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_COMPLETE_TASKFILE;
    }

    if (fua)
    {
        ataCommandOptions.tfr.DeviceHead |= BIT7;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read FPDMA Queued\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read FPDMA Queued", ret);
    }

    return ret;
}

int ata_NCQ_Write_FPDMA_Queued(tDevice *device, bool fua, uint64_t lba, uint8_t *ptrData, uint16_t sectorCount, uint8_t prio, uint8_t ncqTag, uint8_t icc)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = sectorCount * device->drive_info.deviceBlockSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_DMA_FPDMA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_FEATURES_REGISTER;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_FPDMA_QUEUED_CMD;
    ataCommandOptions.tfr.Feature48 = M_Byte1(sectorCount);
    ataCommandOptions.tfr.ErrorFeature = M_Byte0(sectorCount);
    ataCommandOptions.tfr.SectorCount48 = ((prio & 0x03) << 6);//prio
    ataCommandOptions.tfr.SectorCount = ncqTag << 3;//shift into bits 7:3
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte3(lba);
    ataCommandOptions.tfr.LbaLow48 = M_Byte4(lba);
    ataCommandOptions.tfr.LbaMid48 = M_Byte5(lba);
    ataCommandOptions.tfr.LbaHi48 = M_Byte6(lba);
    ataCommandOptions.tfr.aux1 = RESERVED;
    ataCommandOptions.tfr.aux2 = RESERVED;
    ataCommandOptions.tfr.aux3 = RESERVED;
    ataCommandOptions.tfr.aux4 = RESERVED;
    ataCommandOptions.tfr.icc = icc;
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;

    if (ataCommandOptions.tfr.aux1 ||
        ataCommandOptions.tfr.aux2 ||
        ataCommandOptions.tfr.aux3 ||
        ataCommandOptions.tfr.aux4 ||
        ataCommandOptions.tfr.icc
        )
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_COMPLETE_TASKFILE;
    }

    if (fua)
    {
        ataCommandOptions.tfr.DeviceHead |= BIT7;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write FPDMA Queued\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write FPDMA Queued", ret);
    }

    return ret;
}
