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
// \file ata_legacy_cmds.c   Implementation for ATA Spec command functions that are old/obsolete from ATA specs prior to ATA-ATAPI7. Also contains CHS versions of some other commands.
//                     The intention of the file is to be generic & not OS specific

#include "common_types.h"
#include "type_conversion.h"
#include "string_utils.h"
#include "bit_manip.h"
#include "code_attributes.h"
#include "math_utils.h"
#include "error_translation.h"
#include "io_utils.h"
#include "memory_safety.h"

#include "ata_helper.h"
#include "ata_helper_func.h"

/////////////////////////////////////////////////////////////////////////////////
/// Obsolete ATA Commands. These commands are from specs prior to ATA-ATAPI 7 ///
/////////////////////////////////////////////////////////////////////////////////

//This file only contains commands that include retries today, but many old commands have a no-retries version.
//These are not implemented as we want the retries. Can add these definitions if needed in the future.

eReturnValues ata_Legacy_Format_Track(tDevice *device, uint8_t feature, uint8_t sectorCount, uint8_t sectorNumber, uint8_t cylinderLow, uint8_t cylinderHigh, uint8_t *ptrData, uint32_t dataSize, eAtaProtocol protocol, bool lbaMode)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    if (dataSize > 0 && protocol != ATA_PROTOCOL_NO_DATA)
    {
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    }
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.LbaLow = sectorNumber;
    ataCommandOptions.tfr.LbaMid = cylinderLow;
    ataCommandOptions.tfr.LbaHi = cylinderHigh;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (lbaMode)
    {
        ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.SectorCount = sectorCount;
    ataCommandOptions.tfr.ErrorFeature = feature;
    ataCommandOptions.commadProtocol = protocol;
    ataCommandOptions.tfr.CommandStatus = ATA_FORMAT_TRACK_CMD;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (lbaMode)
        {
            printf("Sending ATA Format Track\n");
        }
        else
        {
            printf("Sending ATA Format Track (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (lbaMode)
        {
            print_Return_Enum("Format Track", ret);
        }
        else
        {
            print_Return_Enum("Format Track (CHS)", ret);
        }
    }
    return ret;
}

eReturnValues ata_Legacy_Recalibrate(tDevice *device, uint8_t lowCmdNibble, bool chsMode)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (chsMode)
    {
        ataCommandOptions.tfr.SectorNumber = 1;
        ataCommandOptions.tfr.CylinderLow = 0;
        ataCommandOptions.tfr.CylinderHigh = 0;
    }
    else
    {
        ataCommandOptions.tfr.LbaLow = 0;
        ataCommandOptions.tfr.LbaMid = 0;
        ataCommandOptions.tfr.LbaHi = 0;
        ataCommandOptions.tfr.DeviceHead |= BIT6;//LBA mode
    }
    ataCommandOptions.tfr.SectorCount = 0;
    ataCommandOptions.tfr.ErrorFeature = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.CommandStatus = ATA_RECALIBRATE_CMD;
    ataCommandOptions.tfr.CommandStatus |= M_Nibble0(lowCmdNibble);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (chsMode)
        {
            printf("Sending ATA Recalibrate (CHS)\n");
        }
        else
        {
            printf("Sending ATA Recalibrate\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (chsMode)
        {
            print_Return_Enum("Recalibrate (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Recalibrate", ret);
        }
    }
    return ret;
}

eReturnValues ata_Legacy_Read_DMA_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);

    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_DMA_EXT;//0x25
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_DMA_RETRY_CMD;//0xC8
    }


    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read DMA Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Read DMA (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read DMA Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Read DMA (CHS)", ret);
        }
    }

    return ret;
}

eReturnValues ata_Legacy_Read_Multiple_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
	uint16_t multipleLogicalSectors = 0;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_READ_MULTIPLE_EXT;//0x29
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_MULTIPLE_CMD;//0xC4
    }
    //now set the multiple count setting for the SAT builder so that this command can actually work...and we need to set this as a power of 2, whereas the device info is a number of logical sectors
    multipleLogicalSectors = device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock;
    while (ataCommandOptions.multipleCount <= 7 && multipleLogicalSectors > 0)
    {
        multipleLogicalSectors = multipleLogicalSectors >> 1;//divide by 2
        ataCommandOptions.multipleCount++;
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read Multiple Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Read Multiple (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read Multiple Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Read Multiple (CHS)", ret);
        }
    }

    return ret;
}

eReturnValues ata_Legacy_Set_Max_Address_CHS(tDevice *device, uint16_t newMaxCylinder, uint8_t newMaxHead, uint8_t newMaxSector, bool volitileValue)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataCommandOptions), 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MAX;
    ataCommandOptions.tfr.ErrorFeature = C_CAST(uint8_t, HPA_SET_MAX_ADDRESS);
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.SectorNumber = newMaxSector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(newMaxCylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(newMaxCylinder);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= newMaxHead;
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
        printf("Sending ATA Set Max (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Max (CHS)", ret);
    }
    return ret;
}

eReturnValues ata_Legacy_Set_Max_Address_Ext_CHS(tDevice *device, uint16_t newMaxCylinder, uint8_t newMaxHead, uint8_t newMaxSector, bool volatileValue)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataCommandOptions), 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MAX_EXT;
    ataCommandOptions.tfr.SectorNumber = newMaxSector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(newMaxCylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(newMaxCylinder);
    ataCommandOptions.tfr.SectorNumberExt = 0;
    ataCommandOptions.tfr.CylinderLowExt = 0;
    ataCommandOptions.tfr.CylinderHighExt = 0;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(newMaxHead);
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
        printf("Sending ATA Set Native Max Address Ext (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Native Max Address Ext (CHS)", ret);
    }
    return ret;
}

eReturnValues ata_Legacy_Read_Sectors_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_SECT_EXT;//0x24
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(sectorCount);
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_SECT;//0x20
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read Sectors Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Read Sectors (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read Sectors Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Read Sectors (CHS)", ret);
        }
    }

    return ret;
}

eReturnValues ata_Legacy_Read_Verify_Sectors_CHS(tDevice *device, bool extendedCmd, uint16_t numberOfSectors, uint16_t cylinder, uint8_t head, uint8_t sector)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (extendedCmd)
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_VERIFY_EXT;
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(numberOfSectors);
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_READ_VERIFY_RETRY;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Read Verify Sectors Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Read Verify Sectors (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Read Verify Sectors Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Read Verify Sectors (CHS)", ret);
        }
    }

    return ret;
}

eReturnValues ata_Legacy_Read_Verify_Sectors_No_Retry_CHS(tDevice *device, uint16_t numberOfSectors, uint16_t cylinder, uint8_t head, uint8_t sector)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_READ_VERIFY_NORETRY;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read Verify Sectors - No Retry (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Verify Sectors - No Retry (CHS)", ret);
    }

    return ret;
}

eReturnValues ata_Read_Verify_Sectors_No_Retry(tDevice *device, uint16_t numberOfSectors, uint64_t LBA)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.LbaLow = M_Byte0(LBA);
    ataCommandOptions.tfr.LbaMid = M_Byte1(LBA);
    ataCommandOptions.tfr.LbaHi = M_Byte2(LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(LBA);//set the high 4 bits for the LBA (24:28)
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.CommandStatus = ATA_READ_VERIFY_NORETRY;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read Verify Sectors - No Retry\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Verify Sectors - No Retry", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_DMA_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool fua)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= head;
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
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    }
    else
    {
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
            printf("Sending ATA Write DMA Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Write DMA (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Write DMA Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Write DMA (CHS)", ret);
        }
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Multiple_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool fua)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
	uint16_t multipleLogicalSectors = 0;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
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
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_MULTIPLE_CMD;
    }

    //now set the multiple count setting for the SAT builder so that this command can actually work...and we need to set this as a power of 2, whereas the device info is a number of logical sectors
    multipleLogicalSectors = device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock;
    while (ataCommandOptions.multipleCount <= 7 && multipleLogicalSectors > 0)
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
            printf("Sending ATA Write Multiple Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Write Multiple (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Write Multiple Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Write Multiple (CHS)", ret);
        }
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Sectors_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    if (extendedCmd) //This is only here to use a different command op code if someone using a newer 48bit LBA drive wants to (for some reason) talk to it with CHS, but the 28bit version of the command isn't supported. -TJE
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECT_EXT;
        ataCommandOptions.tfr.SectorNumberExt = 0;
        ataCommandOptions.tfr.CylinderLowExt = 0;
        ataCommandOptions.tfr.CylinderHighExt = 0;
        ataCommandOptions.tfr.SectorCount48 = M_Byte1(dataSize / device->drive_info.deviceBlockSize);
    }
    else
    {
        ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECT;
    }
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);//head bits are lower nibble, or them into this register
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            printf("Sending ATA Write Sectors Ext (CHS)\n");
        }
        else
        {
            printf("Sending ATA Write Sectors (CHS)\n");
        }
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (extendedCmd)
        {
            print_Return_Enum("Write Sectors Ext (CHS)", ret);
        }
        else
        {
            print_Return_Enum("Write Sectors (CHS)", ret);
        }
    }

    return ret;
}

//Lower nibble of command opcode is allowed on really really old drives.
eReturnValues ata_Legacy_Seek_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t lowCmdNibble)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    ataCommandOptions.tfr.SectorCount = 0;
    ataCommandOptions.tfr.ErrorFeature = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.CommandStatus = ATA_SEEK_CMD;
    ataCommandOptions.tfr.CommandStatus |= M_Nibble0(lowCmdNibble);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Seek (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Seek (CHS)", ret);
    }
    return ret;
}

eReturnValues ata_Legacy_Seek(tDevice *device, uint32_t lba, uint8_t lowCmdNibble)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);
    ataCommandOptions.tfr.SectorCount = 0;
    ataCommandOptions.tfr.ErrorFeature = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.CommandStatus = ATA_SEEK_CMD;
    ataCommandOptions.tfr.CommandStatus |= M_Nibble0(lowCmdNibble);
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Seek\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Seek", ret);
    }
    return ret;
}

eReturnValues ata_Legacy_Read_Long_CHS(tDevice *device, bool retires, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is some number of bytes
    ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    ataCommandOptions.tfr.SectorCount = 1;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_RETRY_CMD;//0x22
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_NORETRY;//0x23
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read Long (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Long (CHS)", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Read_Long(tDevice *device, bool retires, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_IN;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is some number of bytes
    ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);//set the high 4 bits for the LBA (24:28)
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;//LBA mode bit
    ataCommandOptions.tfr.SectorCount = 1;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_RETRY_CMD;//0x22
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_NORETRY;//0x23
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Read Long\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Long", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Long_CHS(tDevice *device, bool retires, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is some number of bytes
    ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    ataCommandOptions.tfr.SectorCount = 1;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_RETRY_CMD;//0x32
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_NORETRY;//0x33
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Long (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Long (CHS)", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Long(tDevice *device, bool retires, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is some number of bytes
    ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);//set the high 4 bits for the LBA (24:28)
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;//LBA mode bit
    ataCommandOptions.tfr.SectorCount = 1;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_RETRY_CMD;//0x32
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_NORETRY;//0x33
    }

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Long\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Long", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Same_CHS(tDevice *device, uint8_t subcommand, uint8_t numberOfSectorsToWrite, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
    ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.timeout = UINT32_MAX;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS;
        ataCommandOptions.tfr.SectorCount = numberOfSectorsToWrite;
        ataCommandOptions.tfr.SectorNumber = sector;
        ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
        ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
        ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    }
    else if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS;
        ataCommandOptions.tfr.SectorCount = 1;//spec says N/A, but this helps with SAT translators and should be ignored by the drive.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    }
    else
    {
        return BAD_PARAMETER;
    }
    ataCommandOptions.tfr.CommandStatus = ATA_LEGACY_WRITE_SAME;//0xE9

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Same (CHS), subcommand %" PRIX8 "h\n", subcommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same (CHS)", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Same(tDevice *device, uint8_t subcommand, uint8_t numberOfSectorsToWrite, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
    ataCommandOptions.ataTransferBlocks = ATA_PT_NUMBER_OF_BYTES;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.timeout = UINT32_MAX;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS;
        ataCommandOptions.tfr.SectorCount = numberOfSectorsToWrite;
        ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
        ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
        ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);//set the high 4 bits for the LBA (24:28)
    }
    else if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS;
        ataCommandOptions.tfr.SectorCount = 1;//spec says N/A, but this helps with SAT translators and should be ignored by the drive.
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
        ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;
    }
    else
    {
        return BAD_PARAMETER;
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;//LBA mode bit
    ataCommandOptions.tfr.CommandStatus = ATA_LEGACY_WRITE_SAME;//0xE9

    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Same, subcommand %" PRIX8 "h\n", subcommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Verify_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.SectorCount = C_CAST(uint8_t, dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECTV_RETRY;//0x3C
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Verify (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Verify (CHS)", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Write_Verify(tDevice *device, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.SectorCount = C_CAST(uint8_t, dataSize / LEGACY_DRIVE_SEC_SIZE);
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);//set the high 4 bits for the LBA (24:28)
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;//LBA mode bit
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECTV_RETRY;//0x3C
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Write Verify\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Verify", ret);
    }

    return ret;
}

eReturnValues ata_Legacy_Identify_Device_DMA(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand identify;
    safe_memset(&identify, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    identify.commandType = ATA_CMD_TYPE_TASKFILE;
    identify.commandDirection = XFER_DATA_IN;
    switch (device->drive_info.ata_Options.dmaMode)
    {
    case ATA_DMA_MODE_NO_DMA:
        return BAD_PARAMETER;
    case ATA_DMA_MODE_UDMA:
        identify.commadProtocol = ATA_PROTOCOL_UDMA;
        break;
    case ATA_DMA_MODE_MWDMA:
    case ATA_DMA_MODE_DMA:
    default:
        identify.commadProtocol = ATA_PROTOCOL_DMA;
        break;
    }
    identify.tfr.SectorCount = 1;//some controllers have issues when this isn't set to 1
    identify.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    identify.tfr.CommandStatus = ATA_IDENTIFY_DMA;
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
        printf("Sending ATA Identify DMA command\n");
    }
    ret = ata_Passthrough_Command(device, &identify);

    if (ret == SUCCESS && ptrData != C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000))
    {
        //copy the data to the device structure so that it's not (as) stale
        safe_memcpy(&device->drive_info.IdentifyData.ata.Word000, sizeof(tAtaIdentifyData), ptrData, 512);
    }

#if defined (__BIG_ENDIAN__)
    if (ptrData == C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000))
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
            //don't do anything. Device doesn't use a checksum
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Identify DMA", ret);
    }
    return ret;
}

eReturnValues ata_Legacy_Check_Power_Mode(tDevice *device, uint8_t *powerMode)
{
    eReturnValues ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataCommandOptions), 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.ataTransferBlocks = ATA_PT_NO_DATA_TRANSFER;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = M_NULLPTR;
    ataCommandOptions.tfr.CommandStatus = ATA_LEGACY_ALT_CHECK_POWER_MODE;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }

    if (powerMode == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Check Power Mode (Legacy 98h)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (ret == SUCCESS)
    {
        *powerMode = ataCommandOptions.rtfr.secCnt;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Check Power Mode (Legacy 98h)", ret);
    }
    return ret;
}
