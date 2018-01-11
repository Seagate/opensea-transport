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
// \file ata_legacy_cmds.c   Implementation for ATA Spec command functions that are old/obsolete from ATA specs prior to ATA-ATAPI7. Also contains CHS versions of some other commands.
//                     The intention of the file is to be generic & not OS specific

#include "ata_helper.h"
#include "ata_helper_func.h"

/////////////////////////////////////////////////////////////////////////////////
/// Obsolete ATA Commands. These commands are from specs prior to ATA-ATAPI 7 ///
/////////////////////////////////////////////////////////////////////////////////

//TODO: commands that do not include retries. Currently only retries versions are implemented.

int ata_Legacy_Format_Track(tDevice *device, uint8_t feature, uint8_t sectorCount, uint8_t sectorNumber, uint8_t cylinderLow, uint8_t cylinderHigh, uint8_t *ptrData, uint32_t dataSize, eAtaProtocol protocol, bool lbaMode)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
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
    ataCommandOptions.tfr.SectorCount = sectorCount;
    ataCommandOptions.tfr.ErrorFeature = feature;
    ataCommandOptions.commadProtocol = protocol;
    ataCommandOptions.tfr.CommandStatus = ATA_FORMAT_TRACK;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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

int ata_Legacy_Recalibrate(tDevice *device, bool chsMode)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
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
    ataCommandOptions.tfr.CommandStatus = ATA_RECALIBRATE;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
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

int ata_Read_DMA_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLowExt = 0;
    ataCommandOptions.tfr.CylinderHighExt = 0;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
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
        ataCommandOptions.tfr.CommandStatus = ATA_READ_DMA_RETRY;//0xC8
    }
    

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Read DMA (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read DMA (CHS)", ret);
    }

    return ret;
}

int ata_Read_Multiple_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
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
        ataCommandOptions.tfr.CommandStatus = ATA_READ_MULTIPLE;//0xC4
    }
                                             //now set the multiple count setting for the SAT builder so that this command can actually work...and we need to set this as a power of 2, whereas the device info is a number of logical sectors
    uint16_t multipleLogicalSectors = device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock;
    while (ataCommandOptions.multipleCount <= 7 && multipleLogicalSectors > 0)
    {
        multipleLogicalSectors = multipleLogicalSectors >> 1;//divide by 2
        ataCommandOptions.multipleCount++;
    }

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Read Multiple (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read Multiple (CHS)", ret);
    }

    return ret;
}

int ata_Set_Max_Address_CHS(tDevice *device, uint16_t newMaxCylinder, uint8_t newMaxHead, uint8_t newMaxSector, bool volitileValue)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MAX;
    ataCommandOptions.tfr.ErrorFeature = (uint8_t)HPA_SET_MAX_ADDRESS;
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.SectorNumber = newMaxSector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(newMaxCylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(newMaxCylinder);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= newMaxHead;
    if (volitileValue)
    {
        ataCommandOptions.tfr.SectorCount |= BIT0;
    }
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Set Max (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Set Max (CHS)", ret);
    }
    return ret;
}

int ata_Set_Max_Address_Ext_CHS(tDevice *device, uint16_t newMaxCylinder, uint8_t newMaxHead, uint8_t newMaxSector, bool volatileValue)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataCommandOptions));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.commandType = ATA_CMD_TYPE_EXTENDED_TASKFILE;
    ataCommandOptions.dataSize = 0; //non-data command
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.tfr.CommandStatus = ATA_SET_MAX_EXT;
    ataCommandOptions.tfr.SectorNumber = newMaxSector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(newMaxCylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(newMaxCylinder);
    ataCommandOptions.tfr.SectorNumberExt = 0;
    ataCommandOptions.tfr.CylinderLowExt = 0;
    ataCommandOptions.tfr.CylinderHighExt = 0;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= newMaxHead;
    if (volatileValue)
    {
        ataCommandOptions.tfr.SectorCount |= BIT0;
    }
    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Set Native Max Address Ext (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Set Native Max Address Ext (CHS)", ret);
    }
    return ret;
}

int ata_Read_Sectors_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint16_t sectorCount, uint32_t dataSize, bool extendedCmd)
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(sectorCount);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
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

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Read Sectors (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read Sectors (CHS)", ret);
    }

    return ret;
}

int ata_Read_Verify_Sectors_CHS(tDevice *device, bool extendedCmd, uint16_t numberOfSectors, uint16_t cylinder, uint8_t head, uint8_t sector)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ptrData = NULL;
    ataCommandOptions.dataSize = 0;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_NO_DATA;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Read Verify Sectors (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read Verify Sectors (CHS)", ret);
    }

    return ret;
}

int ata_Write_DMA_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool fua)
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= head;
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
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_DMA_RETRY;
    }


    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write DMA (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write DMA (CHS)", ret);
    }

    return ret;
}

int ata_Write_Multiple_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool fua)
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
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.SectorCount = M_Byte0(dataSize / device->drive_info.deviceBlockSize);
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
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
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_MULTIPLE;
    }

                                             //now set the multiple count setting for the SAT builder so that this command can actually work...and we need to set this as a power of 2, whereas the device info is a number of logical sectors
    uint16_t multipleLogicalSectors = device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock;
    while (ataCommandOptions.multipleCount <= 7 && multipleLogicalSectors > 0)
    {
        multipleLogicalSectors = multipleLogicalSectors >> 1;//divide by 2
        ataCommandOptions.multipleCount++;
    }

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Multiple (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Multiple (CHS)", ret);
    }

    return ret;
}

int ata_Legacy_Write_Sectors_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize, bool extendedCmd, bool retires)
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

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Sectors (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Sectors (CHS)", ret);
    }

    return ret;
}

int ata_Legacy_Seek_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ptrData = NULL;
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Seek (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Seek (CHS)", ret);
    }
    return ret;
}

int ata_Legacy_Seek(tDevice *device, uint32_t lba)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ptrData = NULL;
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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Seek\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Seek", ret);
    }
    return ret;
}

int ata_Legacy_Read_Long_CHS(tDevice *device, bool retires, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
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

    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_RETRY;//0x22
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_NORETRY;//0x23
    }

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Read Long (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read Long (CHS)", ret);
    }

    return ret;
}

int ata_Legacy_Read_Long(tDevice *device, bool retires, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
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

    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_RETRY;//0x22
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_READ_LONG_NORETRY;//0x23
    }

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Read Long\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Read Long", ret);
    }

    return ret;
}

int ata_Legacy_Write_Long_CHS(tDevice *device, bool retires, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
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

    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_RETRY;//0x32
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_NORETRY;//0x33
    }

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Long (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Long (CHS)", ret);
    }

    return ret;
}

int ata_Legacy_Write_Long(tDevice *device, bool retires, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
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

    if (retires)
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_RETRY;//0x32
    }
    else
    {
        ataCommandOptions.tfr.CommandStatus = ATA_WRITE_LONG_NORETRY;//0x33
    }

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Long\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Long", ret);
    }

    return ret;
}

int ata_Legacy_Write_Same_CHS(tDevice *device, uint8_t subcommand, uint8_t numberOfSectorsToWrite, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;//or number of bytes?
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (subcommand == 0x22)
    {
        ataCommandOptions.tfr.SectorCount = numberOfSectorsToWrite;
        ataCommandOptions.tfr.SectorNumber = sector;
        ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
        ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
        ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    }
    ataCommandOptions.tfr.CommandStatus = ATA_LEGACY_WRITE_SAME;//0xE9

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Same (CHS), subcommand %" PRIX8"h\n", subcommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Same (CHS)", ret);
    }

    return ret;
}

int ata_Legacy_Write_Same(tDevice *device, uint8_t subcommand, uint8_t numberOfSectorsToWrite, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;//or number of bytes?
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (subcommand == 0x22)
    {
        ataCommandOptions.tfr.SectorCount = numberOfSectorsToWrite;
        ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
        ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
        ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
        ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);//set the high 4 bits for the LBA (24:28)
    }
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;//LBA mode bit
    ataCommandOptions.tfr.CommandStatus = ATA_LEGACY_WRITE_SAME;//0xE9

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Same, subcommand %" PRIX8"h\n", subcommand);
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Same", ret);
    }

    return ret;
}

int ata_Legacy_Write_Verify_CHS(tDevice *device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;//or number of bytes?
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.SectorCount = dataSize / LEGACY_DRIVE_SEC_SIZE;
    ataCommandOptions.tfr.SectorNumber = sector;
    ataCommandOptions.tfr.CylinderLow = M_Byte0(cylinder);
    ataCommandOptions.tfr.CylinderHigh = M_Byte1(cylinder);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble0(head);
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECTV_RETRY;//0x3C

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Verify (CHS)\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Verify (CHS)", ret);
    }

    return ret;
}

int ata_Legacy_Write_Verify(tDevice *device, uint32_t lba, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    memset(&ataCommandOptions, 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandType = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.commandDirection = XFER_DATA_OUT;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;//this must be used since the transfer is always 1 sector to the drive. The sector count says how many sectors to write the data sent to
    ataCommandOptions.ataTransferBlocks = ATA_PT_512B_BLOCKS;//or number of bytes?
    ataCommandOptions.ptrData = ptrData;
    ataCommandOptions.dataSize = dataSize;
    ataCommandOptions.commadProtocol = ATA_PROTOCOL_PIO;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    ataCommandOptions.tfr.SectorCount = dataSize / LEGACY_DRIVE_SEC_SIZE;
    ataCommandOptions.tfr.LbaLow = M_Byte0(lba);
    ataCommandOptions.tfr.LbaMid = M_Byte1(lba);
    ataCommandOptions.tfr.LbaHi = M_Byte2(lba);
    ataCommandOptions.tfr.DeviceHead |= M_Nibble6(lba);//set the high 4 bits for the LBA (24:28)
    ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;//LBA mode bit
    ataCommandOptions.tfr.CommandStatus = ATA_WRITE_SECTV_RETRY;//0x3C

    if (ptrData == NULL)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Write Verify\n");
    }

    ret = ata_Passthrough_Command(device, &ataCommandOptions);

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Write Verify", ret);
    }

    return ret;
}

int ata_Legacy_Identify_Device_DMA(tDevice *device, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    ataPassthroughCommand identify;
    memset(&identify, 0, sizeof(ataPassthroughCommand));
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
    identify.ptrData = ptrData;
    identify.dataSize = dataSize;

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        printf("Sending ATA Identify DMA command\n");
    }
    ret = ata_Passthrough_Command(device, &identify);

    if (ret == SUCCESS && ptrData != (uint8_t*)&device->drive_info.IdentifyData.ata.Word000)
    {
        //copy the data to the device structure so that it's not (as) stale
        memcpy(&device->drive_info.IdentifyData.ata.Word000, ptrData, sizeof(tAtaIdentifyData));
    }

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

    if (VERBOSITY_COMMAND_NAMES <= g_verbosity)
    {
        print_Return_Enum("Identify DMA", ret);
    }
    return ret;
}