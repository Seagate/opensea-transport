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
// \file ata_legacy_cmds.c   Implementation for ATA Spec command functions that are old/obsolete from ATA specs prior to
// ATA-ATAPI7. Also contains CHS versions of some other commands.
//                     The intention of the file is to be generic & not OS specific

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper.h"
#include "ata_helper_func.h"

/////////////////////////////////////////////////////////////////////////////////
/// Obsolete ATA Commands. These commands are from specs prior to ATA-ATAPI 7 ///
/////////////////////////////////////////////////////////////////////////////////

// This file only contains commands that include retries today, but many old commands have a no-retries version.
// These are not implemented as we want the retries. Can add these definitions if needed in the future.

eReturnValues ata_Legacy_Format_Track(tDevice*     device,
                                      uint8_t      feature,
                                      uint8_t      sectorCount,
                                      uint8_t      sectorNumber,
                                      uint8_t      cylinderLow,
                                      uint8_t      cylinderHigh,
                                      uint8_t*     ptrData,
                                      uint32_t     dataSize,
                                      eAtaProtocol protocol,
                                      bool         lbaMode)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions;
    safe_memset(&ataCommandOptions, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));
    ataCommandOptions.commandDirection         = XFER_NO_DATA;
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_NO_DATA;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NO_DATA_TRANSFER;
    if (dataSize > 0 && protocol != ATA_PROTOCOL_NO_DATA)
    {
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    }
    ataCommandOptions.ptrData        = ptrData;
    ataCommandOptions.dataSize       = dataSize;
    ataCommandOptions.commandType    = ATA_CMD_TYPE_TASKFILE;
    ataCommandOptions.tfr.LbaLow     = sectorNumber;
    ataCommandOptions.tfr.LbaMid     = cylinderLow;
    ataCommandOptions.tfr.LbaHi      = cylinderHigh;
    ataCommandOptions.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    if (lbaMode)
    {
        ataCommandOptions.tfr.DeviceHead |= LBA_MODE_BIT;
    }
    if (device->drive_info.ata_Options.isDevice1)
    {
        ataCommandOptions.tfr.DeviceHead |= DEVICE_SELECT_BIT;
    }
    ataCommandOptions.tfr.SectorCount   = sectorCount;
    ataCommandOptions.tfr.ErrorFeature  = feature;
    ataCommandOptions.commadProtocol    = protocol;
    ataCommandOptions.tfr.CommandStatus = ATA_FORMAT_TRACK_CMD;
    ataCommandOptions.needRTFRs         = true; // vendor specific, but better to have it than not have it.

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

eReturnValues ata_Legacy_Recalibrate(tDevice* device, uint8_t lowCmdNibble, bool chsMode)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_RECALIBRATE_CMD, false, false);
    if (chsMode)
    {
        set_ata_pt_CHS(&ataCommandOptions, 0, 0, 1);
    }
    else
    {
        set_ata_pt_LBA_28(&ataCommandOptions, UINT32_C(0));
    }
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

eReturnValues ata_Legacy_Read_DMA_CHS(tDevice*               device,
                                      uint16_t               cylinder,
                                      uint8_t                head,
                                      uint8_t                sector,
                                      uint8_t*               ptrData,
                                      M_ATTR_UNUSED uint16_t sectorCount,
                                      uint32_t               dataSize,
                                      bool                   extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_dma_in_cmd(
        device, extendedCmd ? ATA_READ_DMA_EXT : ATA_READ_DMA_RETRY_CMD, extendedCmd,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, extendedCmd), ptrData,
        dataSize);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Read_Multiple_CHS(tDevice*               device,
                                           uint16_t               cylinder,
                                           uint8_t                head,
                                           uint8_t                sector,
                                           uint8_t*               ptrData,
                                           M_ATTR_UNUSED uint16_t sectorCount,
                                           uint32_t               dataSize,
                                           bool                   extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_in_cmd(
        device, extendedCmd ? ATA_READ_READ_MULTIPLE_EXT : ATA_READ_MULTIPLE_CMD, extendedCmd,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, extendedCmd), ptrData,
        dataSize);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);
    set_ata_pt_multipleCount(&ataCommandOptions, device);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Set_Max_Address_CHS(tDevice* device,
                                             uint16_t newMaxCylinder,
                                             uint8_t  newMaxHead,
                                             uint8_t  newMaxSector,
                                             bool     volatileValue)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_SET_MAX, false, false);
    set_ata_pt_CHS(&ataCommandOptions, newMaxCylinder, newMaxHead, newMaxSector);
    if (volatileValue)
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

eReturnValues ata_Legacy_Set_Max_Address_Ext_CHS(tDevice* device,
                                                 uint16_t newMaxCylinder,
                                                 uint8_t  newMaxHead,
                                                 uint8_t  newMaxSector,
                                                 bool     volatileValue)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_SET_MAX_EXT, true, false);
    set_ata_pt_CHS(&ataCommandOptions, newMaxCylinder, newMaxHead, newMaxSector);
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

eReturnValues ata_Legacy_Read_Sectors_CHS(tDevice*               device,
                                          uint16_t               cylinder,
                                          uint8_t                head,
                                          uint8_t                sector,
                                          uint8_t*               ptrData,
                                          M_ATTR_UNUSED uint16_t sectorCount,
                                          uint32_t               dataSize,
                                          bool                   extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_in_cmd(
        device, extendedCmd ? ATA_READ_SECT_EXT : ATA_READ_SECT, extendedCmd,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, extendedCmd), ptrData,
        dataSize);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Read_Verify_Sectors_CHS(tDevice* device,
                                                 bool     extendedCmd,
                                                 uint16_t numberOfSectors,
                                                 uint16_t cylinder,
                                                 uint8_t  head,
                                                 uint8_t  sector)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, extendedCmd ? ATA_READ_VERIFY_EXT : ATA_READ_VERIFY_RETRY, extendedCmd, false);
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);

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

eReturnValues ata_Legacy_Read_Verify_Sectors_No_Retry_CHS(tDevice* device,
                                                          uint16_t numberOfSectors,
                                                          uint16_t cylinder,
                                                          uint8_t  head,
                                                          uint8_t  sector)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_READ_VERIFY_NORETRY, false, false);
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);

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

eReturnValues ata_Read_Verify_Sectors_No_Retry(tDevice* device, uint16_t numberOfSectors, uint32_t LBA)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_READ_VERIFY_NORETRY, false, false);
    set_ata_pt_LBA_28(&ataCommandOptions, LBA);
    ataCommandOptions.tfr.SectorCount = M_Byte0(numberOfSectors);

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

eReturnValues ata_Legacy_Write_DMA_CHS(tDevice* device,
                                       uint16_t cylinder,
                                       uint8_t  head,
                                       uint8_t  sector,
                                       uint8_t* ptrData,
                                       uint32_t dataSize,
                                       bool     extendedCmd,
                                       bool     fua)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_dma_out_cmd(
        device, extendedCmd ? (fua ? ATA_WRITE_DMA_FUA_EXT : ATA_WRITE_DMA_EXT) : ATA_WRITE_DMA_RETRY_CMD, extendedCmd,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, extendedCmd), ptrData,
        dataSize);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Multiple_CHS(tDevice* device,
                                            uint16_t cylinder,
                                            uint8_t  head,
                                            uint8_t  sector,
                                            uint8_t* ptrData,
                                            uint32_t dataSize,
                                            bool     extendedCmd,
                                            bool     fua)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, extendedCmd ? (fua ? ATA_WRITE_MULTIPLE_FUA_EXT : ATA_WRITE_MULTIPLE_EXT) : ATA_WRITE_MULTIPLE_CMD,
        extendedCmd,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, extendedCmd), ptrData,
        dataSize);
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_multipleCount(&ataCommandOptions, device);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Sectors_CHS(tDevice* device,
                                           uint16_t cylinder,
                                           uint8_t  head,
                                           uint8_t  sector,
                                           uint8_t* ptrData,
                                           uint32_t dataSize,
                                           bool     extendedCmd)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, extendedCmd ? ATA_WRITE_SECT_EXT : ATA_WRITE_SECT, extendedCmd,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, extendedCmd), ptrData,
        dataSize);
    ataCommandOptions.ataTransferBlocks = ATA_PT_LOGICAL_SECTOR_SIZE;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

// Lower nibble of command opcode is allowed on really really old drives.
eReturnValues ata_Legacy_Seek_CHS(tDevice* device,
                                  uint16_t cylinder,
                                  uint8_t  head,
                                  uint8_t  sector,
                                  uint8_t  lowCmdNibble)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_SEEK_CMD, false, false);
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);
    ataCommandOptions.tfr.CommandStatus |= M_Nibble0(lowCmdNibble);

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

eReturnValues ata_Legacy_Seek(tDevice* device, uint32_t lba, uint8_t lowCmdNibble)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_nondata_cmd(device, ATA_SEEK_CMD, false, false);
    set_ata_pt_LBA_28(&ataCommandOptions, lba);
    ataCommandOptions.tfr.CommandStatus |= M_Nibble0(lowCmdNibble);

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

eReturnValues ata_Legacy_Read_Long_CHS(tDevice* device,
                                       bool     retries,
                                       uint16_t cylinder,
                                       uint8_t  head,
                                       uint8_t  sector,
                                       uint8_t* ptrData,
                                       uint32_t dataSize)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_in_cmd(
        device, retries ? ATA_READ_LONG_RETRY_CMD : ATA_READ_LONG_NORETRY, false, 1, ptrData, dataSize);
    // this must be used since the transfer is some number of bytes
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Read_Long(tDevice* device, bool retries, uint32_t lba, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;

    ataPassthroughCommand ataCommandOptions = create_ata_pio_read_lba_cmd(
        device, retries ? ATA_READ_LONG_RETRY_CMD : ATA_READ_LONG_NORETRY, false, 1, lba, ptrData, dataSize);
    // this must be used since the transfer is some number of bytes
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Long_CHS(tDevice* device,
                                        bool     retries,
                                        uint16_t cylinder,
                                        uint8_t  head,
                                        uint8_t  sector,
                                        uint8_t* ptrData,
                                        uint32_t dataSize)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, retries ? ATA_WRITE_LONG_RETRY_CMD : ATA_WRITE_LONG_NORETRY, false, 1, ptrData, dataSize);
    // this must be used since the transfer is some number of bytes
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Long(tDevice* device, bool retries, uint32_t lba, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_write_lba_cmd(
        device, retries ? ATA_WRITE_LONG_RETRY_CMD : ATA_WRITE_LONG_NORETRY, false, 1, lba, ptrData, dataSize);
    // this must be used since the transfer is some number of bytes
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Same_CHS(tDevice* device,
                                        uint8_t  subcommand,
                                        uint8_t  numberOfSectorsToWrite,
                                        uint16_t cylinder,
                                        uint8_t  head,
                                        uint8_t  sector,
                                        uint8_t* ptrData,
                                        uint32_t dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_pio_out_cmd(device, ATA_LEGACY_WRITE_SAME, false, numberOfSectorsToWrite, ptrData, dataSize);
    // this must be used since the transfer is always 1 sector to the drive. The sector count says
    // how many sectors to write the data sent to
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS;
        set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);
    }
    else if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS;
        // spec says N/A, but this helps with SAT translators and should be ignored by the drive.
        ataCommandOptions.tfr.SectorCount = 1;
        // this must be used since the transfer is always 1 sector to the drive. The sector
        // count says how many sectors to write the data sent to
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks        = ATA_PT_512B_BLOCKS;
    }
    else
    {
        return BAD_PARAMETER;
    }

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Same(tDevice* device,
                                    uint8_t  subcommand,
                                    uint8_t  numberOfSectorsToWrite,
                                    uint32_t lba,
                                    uint8_t* ptrData,
                                    uint32_t dataSize)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_pio_out_cmd(device, ATA_LEGACY_WRITE_SAME, false, numberOfSectorsToWrite, ptrData, dataSize);
    // this must be used since the transfer is always 1 sector to the drive. The sector count says
    // how many sectors to write the data sent to
    ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    ataCommandOptions.ataTransferBlocks        = ATA_PT_NUMBER_OF_BYTES;
    if (os_Is_Infinite_Timeout_Supported())
    {
        ataCommandOptions.timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        ataCommandOptions.timeout = MAX_CMD_TIMEOUT_SECONDS;
    }
    if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS;
        set_ata_pt_LBA_28(&ataCommandOptions, lba);
    }
    else if (subcommand == LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS)
    {
        ataCommandOptions.tfr.ErrorFeature = LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS;
        // spec says N/A, but this helps with SAT translators and should be ignored by the drive.
        ataCommandOptions.tfr.SectorCount = 1;
        // this must be used since the transfer is always 1 sector to the drive. The sector
        // count says how many sectors to write the data sent to
        ataCommandOptions.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
        ataCommandOptions.ataTransferBlocks        = ATA_PT_512B_BLOCKS;
    }
    else
    {
        return BAD_PARAMETER;
    }

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Verify_CHS(tDevice* device,
                                          uint16_t cylinder,
                                          uint8_t  head,
                                          uint8_t  sector,
                                          uint8_t* ptrData,
                                          uint32_t dataSize)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_out_cmd(
        device, ATA_WRITE_SECTV_RETRY, false,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, false), ptrData,
        dataSize);
    set_ata_pt_CHS(&ataCommandOptions, cylinder, head, sector);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Write_Verify(tDevice* device, uint32_t lba, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues         ret               = UNKNOWN;
    ataPassthroughCommand ataCommandOptions = create_ata_pio_write_lba_cmd(
        device, ATA_WRITE_SECTV_RETRY, false,
        get_Sector_Count_From_Buffer_Size_For_RW(dataSize, device->drive_info.deviceBlockSize, false), lba, ptrData,
        dataSize);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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

eReturnValues ata_Legacy_Identify_Device_DMA(tDevice* device, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues         ret      = UNKNOWN;
    ataPassthroughCommand identify = create_ata_dma_in_cmd(device, ATA_IDENTIFY_DMA, false, 1, ptrData, dataSize);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending ATA Identify DMA command\n");
    }
    ret = ata_Passthrough_Command(device, &identify);

    if (ret == SUCCESS)
    {
        // copy the data to the device structure so that it's not (as) stale
        copy_ata_identify_to_tdevice(device, ptrData);
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
            // don't do anything. Device doesn't use a checksum
        }
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Identify DMA", ret);
    }
    return ret;
}

eReturnValues ata_Legacy_Check_Power_Mode(tDevice* device, uint8_t* powerMode)
{
    eReturnValues         ret = UNKNOWN;
    ataPassthroughCommand ataCommandOptions =
        create_ata_nondata_cmd(device, ATA_LEGACY_ALT_CHECK_POWER_MODE, false, true);

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
