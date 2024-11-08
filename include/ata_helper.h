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

// \file ata_helper.h
// \brief Defines the constants structures to help with ATA Specification

#pragma once

#include "common_public.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define ATA_STATUS_BIT_BUSY         BIT7 // if this is set, all other bits are invalid and not to be used
#define ATA_STATUS_BIT_READY        BIT6
#define ATA_STATUS_BIT_DEVICE_FAULT BIT5 // also called the write fault bit
#define ATA_STATUS_BIT_STREAM_ERROR BIT5
#define ATA_STATUS_BIT_SEEK_COMPLETE                                                                                   \
    BIT4                                         // old/obsolete and unused. Old drives still set it for backwards
                                                 // compatability
#define ATA_STATUS_BIT_SERVICE              BIT4 // DMA Queued commands. Tagged command queuing AFAIK
#define ATA_STATUS_BIT_DEFERRED_WRITE_ERROR BIT4 // write stream commands
#define ATA_STATUS_BIT_DATA_REQUEST         BIT3
#define ATA_STATUS_BIT_CORRECTED_DATA       BIT2 // old/obsolete
#define ATA_STATUS_BIT_ALIGNMENT_ERROR      BIT2
#define ATA_STATUS_BIT_INDEX                BIT1 // old/obsolete. flips state with each drive revolution
#define ATA_STATUS_BIT_SENSE_DATA_AVAILABLE BIT1 // set when sense data is available for the command
#define ATA_STATUS_BIT_CHECK_CONDITION      BIT0 // ATAPI
#define ATA_STATUS_BIT_ERROR                BIT0 // an error occured

#define ATA_ERROR_BIT_BAD_BLOCK             BIT7 // old/obsolete - ATA-1
#define ATA_ERROR_BIT_INTERFACE_CRC         BIT7
#define ATA_ERROR_BIT_UNCORRECTABLE_DATA    BIT6
#define ATA_ERROR_BIT_WRITE_PROTECTED       BIT6 // old/obsolete - removable medium
#define ATA_ERROR_BIT_MEDIA_CHANGE          BIT5
#define ATA_ERROR_BIT_ID_NOT_FOUND          BIT4
#define ATA_ERROR_BIT_MEDIA_CHANGE_REQUEST  BIT3
#define ATA_ERROR_BIT_ABORT                 BIT2
#define ATA_ERROR_BIT_TRACK_ZERO_NOT_FOUND  BIT1 // old/obsolete
#define ATA_ERROR_BIT_END_OF_MEDIA          BIT1 // ATAPI
#define ATA_ERROR_BIT_NO_MEDIA              BIT1 // old/obsolete - removable medium
#define ATA_ERROR_BIT_INSUFFICIENT_LBA_RANGE_ENTRIES_REMAINING                                                         \
    BIT1                                                   // add LBAs to NV cache pinned set command only - ranges
#define ATA_ERROR_BIT_ADDRESS_MARK_NOT_FOUND          BIT0 // old/obsolete
#define ATA_ERROR_BIT_COMMAND_COMPLETION_TIME_OUT     BIT0 // streaming feature
#define ATAPI_ERROR_BIT_ILLEGAL_LENGTH_INDICATOR      BIT0
#define ATAPI_ERROR_BIT_MEDIA_ERROR                   BIT0
#define ATA_ERROR_BIT_ATTEMPTED_PARTIAL_RANGE_REMOVAL BIT0 // remove LBAs from NV cache pinned set command only
#define ATA_ERROR_BIT_INSUFFICIENT_NV_CACHE_SPACE     BIT0 // add LBAs to NV cache pinned set command only

#define ATA_DL_MICROCODE_OFFSET_SAVE                  (0x03)
#define ATA_DL_MICROCODE_SAVE                         (0x07)

#define MAX_28BIT                                     UINT32_C(0xFFFFFFF)

#define LBA_MODE_BIT                                  BIT6 // Set this in the device/head register to set LBA mode.
#define DEVICE_SELECT_BIT                             BIT4 // On PATA, this is to select drive 1. Device/Head register
#define DEVICE_REG_BACKWARDS_COMPATIBLE_BITS          0xA0
    // device/head in ATA & ATA3 say bits 7&5 should be set on every command.
    // New specs mark these obsolete in commands that are from old specs.
    // New commands may use these for other purposes.
    // Device/Head pre-ATA standardization called this the Sector Size, Device, Head register
    // bit7 was defined as the ECC bit. With this set to zero it used CRC
    // bits 6:5 were the sector size. 11b=128B, 00b=256B, 01b=512B, 10b=1024B.
    // With standardization forcing these bits to A0 sets ECC and 512B sector size, which is all that was likely used in
    // the real-world For backwards compatibility with these really old devices, it is recommended to set this register
    // to these values. This is not necessary for SATA drives unless they are aborting commands for no other reason.
    // Setting these bits may not be necessary for most PATA devices that conform to ATA standards

    // This is a basic validity indicator for a given ATA identify word. Checks that it is non-zero and not FFFFh
    OPENSEA_TRANSPORT_API bool is_ATA_Identify_Word_Valid(uint16_t word);
    // This one is a little more advanced as some words specify bit 15 is zero and bit 14 is 1 as a key, so it checks
    // for this in addition to checking non-zero and non FFFFh example: Word 83, 84, 87, 93 (pata), 106, 119, 120, 209
    OPENSEA_TRANSPORT_API bool is_ATA_Identify_Word_Valid_With_Bits_14_And_15(uint16_t word);
    // checks same as is_ATA_Identify_Word_Valid and that bit 0 is cleared to zero in the SATA words (76 - 79)
    OPENSEA_TRANSPORT_API bool is_ATA_Identify_Word_Valid_SATA(uint16_t word);

#define ATA_CHECKSUM_VALIDITY_INDICATOR                       0xA5

#define IDLE_IMMEDIATE_UNLOAD_LBA                             0x0554E4C
#define IDLE_IMMEDIATE_UNLOAD_FEATURE                         0x44

#define WRITE_UNCORRECTABLE_PSEUDO_UNCORRECTABLE_WITH_LOGGING 0x55
#define WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_5AH               0x5A
#define WRITE_UNCORRECTABLE_VENDOR_SPECIFIC_A5H               0xA5
#define WRITE_UNCORRECTABLE_FLAGGED_WITHOUT_LOGGING           0xAA

#define LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS        0x22
#define LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS              0xDD

    typedef enum eDownloadMicrocodeFeaturesEnum
    {
        ATA_DL_MICROCODE_TEMPORARY_IMMEDIATE    = 0x01, // obsolete
        ATA_DL_MICROCODE_OFFSETS_SAVE_IMMEDIATE = 0x03,
        ATA_DL_MICROCODE_SAVE_IMMEDIATE         = 0x07,
        ATA_DL_MICROCODE_OFFSETS_SAVE_FUTURE    = 0x0E, // deferred
        ATA_DL_MICROCODE_ACTIVATE               = 0x0F
    } eDownloadMicrocodeFeatures;

    typedef enum eSMART_featuresEnum
    {
        ATA_SMART_READ_DATA        = 0xD0,
        ATA_SMART_RDATTR_THRESH    = 0xD1,
        ATA_SMART_SW_AUTOSAVE      = 0xD2,
        ATA_SMART_SAVE_ATTRVALUE   = 0xD3,
        ATA_SMART_EXEC_OFFLINE_IMM = 0xD4,
        ATA_SMART_READ_LOG         = 0xD5,
        ATA_SMART_WRITE_LOG        = 0xD6,
        ATA_SMART_WRATTR_THRESH    = 0xD7,
        ATA_SMART_ENABLE           = 0xD8,
        ATA_SMART_DISABLE          = 0xD9,
        ATA_SMART_RTSMART          = 0xDA,
        ATA_SMART_AUTO_OFFLINE     = 0xDB, // NOTE: never officially adopted by ATA spec, but was part of original SMART
                                           // spec by SFF committee (SFF-8035i).
        ATA_SMART_UNKNOWN
    } eSMART_features;

#define ATA_SMART_SIG_MID                                   0x4F
#define ATA_SMART_SIG_HI                                    0xC2

#define ATA_SMART_BAD_SIG_MID                               0xF4
#define ATA_SMART_BAD_SIG_HI                                0x2C

#define ATA_SMART_READ_DATA_SIZE                            512

#define ATA_SMART_VERSION_OFFSET                            0
#define ATA_SMART_BEGIN_ATTRIBUTES                          2   // also true for thresholds
#define ATA_SMART_END_ATTRIBUTES                            362 // also true for thresholds
#define ATA_SMART_ATTRIBUTE_SIZE                            12  // also true for thresholds
#define ATA_SMART_OFFLINE_COLLECTION_STATUS_OFFSET          362
#define ATA_SMART_SELF_TEST_EXECUTION_STATUS_OFFSET         363
#define ATA_SMART_OFFLINE_DATA_COLLECTION_CAPABILITY_OFFSET 367
#define ATA_SMART_SMART_CAPABILITY_OFFSET                   368
#define ATA_SMART_ERROR_LOGGING_CAPABILITY_OFFSET           370
#define ATA_SMART_SHORT_TEST_POLLING_TIME_OFFSET            372
#define ATA_SMART_EXTENDED_POLLING_TIME_BYTE_OFFSET         373 // if FFh, need to use word below
#define ATA_SMART_CONVEYANCE_POLLING_TIME_OFFSET            374
#define ATA_SMART_EXTENDEd_POLLING_TIME_WORD_OFFSET         375

#define ATA_SMART_STATUS_FLAG_PREFAIL_ADVISORY              BIT0
#define ATA_SMART_STATUS_FLAG_ONLINE_DATA_COLLECTION        BIT1
#define ATA_SMART_STATUS_FLAG_PERFORMANCE                   BIT2
#define ATA_SMART_STATUS_FLAG_ERROR_RATE                    BIT3
#define ATA_SMART_STATUS_FLAG_EVENT_COUNT                   BIT4
#define ATA_SMART_STATUS_FLAG_SELF_PRESERVING               BIT5

#define ATA_SMART_ATTRIBUTE_NOMINAL_COMMON_START                                                                       \
    0x64 // most attributes start at 100, but there are occasionally some that don't
#define ATA_SMART_ATTRIBUTE_WORST_COMMON_START                                                                         \
    0xFD // It's fairly common for a worst-ever to start at this highest possible value then move down as data is
         // collected.
#define ATA_SMART_ATTRIBUTE_MINIMUM        0x01
#define ATA_SMART_ATTRIBUTE_MAXIMUM        0xFD

#define ATA_SMART_THRESHOLD_ALWAYS_PASSING 0x00
#define ATA_SMART_THRESHOLD_MINIMUM        0x01
#define ATA_SMART_THRESHOLD_MAXIMUM        0xFD
#define ATA_SMART_THRESHOLD_INVALID        0xFE
#define ATA_SMART_THRESHOLD_ALWAYS_FAILING                                                                             \
    0xFF // Test code purposes only to test system or software's ability to detect SMART trips

#define ATA_SMART_ATTRIBUTE_AUTOSAVE_ENABLE_SIG  0xF1
#define ATA_SMART_AUTO_OFFLINE_ENABLE_SIG        0xF8

#define ATA_SMART_ATTRIBUTE_AUTOSAVE_DISABLE_SIG 0x00
#define ATA_SMART_AUTO_OFFLINE_DISABLE_SIG       0x00

    typedef enum eATA_CMDSEnum
    {
        ATA_NOP_CMD                          = 0x00,
        ATA_CFA_REQUEST_SENSE                = 0x03,
        ATASET                               = 0x04,
        ATA_DATA_SET_MANAGEMENT_CMD          = 0x06,
        ATA_DATA_SET_MANAGEMENT_XL_CMD       = 0x07,
        ATAPI_RESET                          = 0x08,
        ATA_DEV_RESET                        = 0x08,
        ATA_REQUEST_SENSE_DATA               = 0x0B,
        ATA_RECALIBRATE_CMD                  = 0x10,
        ATA_GET_PHYSICAL_ELEMENT_STATUS      = 0x12,
        ATA_READ_SECT                        = 0x20,
        ATA_READ_SECT_NORETRY                = 0x21,
        ATA_READ_LONG_RETRY_CMD              = 0x22,
        ATA_READ_LONG_NORETRY                = 0x23,
        ATA_READ_SECT_EXT                    = 0x24,
        ATA_READ_DMA_EXT                     = 0x25,
        ATA_READ_DMA_QUE_EXT                 = 0x26,
        ATA_READ_MAX_ADDRESS_EXT             = 0x27,
        ATA_READ_READ_MULTIPLE_EXT           = 0x29,
        ATA_READ_STREAM_DMA_EXT              = 0x2A,
        ATA_READ_STREAM_EXT                  = 0x2B,
        ATA_READ_LOG_EXT                     = 0x2F,
        ATA_WRITE_SECT                       = 0x30,
        ATA_WRITE_SECT_NORETRY               = 0x31,
        ATA_WRITE_LONG_RETRY_CMD             = 0x32,
        ATA_WRITE_LONG_NORETRY               = 0x33,
        ATA_WRITE_SECT_EXT                   = 0x34,
        ATA_WRITE_DMA_EXT                    = 0x35,
        ATA_WRITE_DMA_QUE_EXT                = 0x36,
        ATA_SET_MAX_EXT                      = 0x37,
        ATA_CFA_WRITE_SECTORS_WITHOUT_ERASE  = 0x38,
        ATA_WRITE_MULTIPLE_EXT               = 0x39,
        ATA_WRITE_STREAM_DMA_EXT             = 0x3A,
        ATA_WRITE_STREAM_EXT                 = 0x3B,
        ATA_WRITE_SECTV_RETRY                = 0x3C,
        ATA_WRITE_DMA_FUA_EXT                = 0x3D,
        ATA_WRITE_DMA_QUE_FUA_EXT            = 0x3E,
        ATA_WRITE_LOG_EXT_CMD                = 0x3F,
        ATA_READ_VERIFY_RETRY                = 0x40,
        ATA_READ_VERIFY_NORETRY              = 0x41,
        ATA_READ_VERIFY_EXT                  = 0x42,
        ATA_ZEROS_EXT                        = 0x44,
        ATA_WRITE_UNCORRECTABLE_EXT          = 0x45,
        ATA_READ_LOG_EXT_DMA                 = 0x47,
        ATA_ZONE_MANAGEMENT_IN               = 0x4A,
        ATA_FORMAT_TRACK_CMD                 = 0x50,
        ATA_CONFIGURE_STREAM                 = 0x51,
        ATA_WRITE_LOG_EXT_DMA                = 0x57,
        ATA_TRUSTED_NON_DATA                 = 0x5B,
        ATA_TRUSTED_RECEIVE                  = 0x5C,
        ATA_TRUSTED_RECEIVE_DMA              = 0x5D,
        ATA_TRUSTED_SEND                     = 0x5E,
        ATA_TRUSTED_SEND_DMA                 = 0x5F,
        ATA_READ_FPDMA_QUEUED_CMD            = 0x60, // Added _CMD because FreeBSD had a symbol conflict
        ATA_WRITE_FPDMA_QUEUED_CMD           = 0x61,
        ATA_FPDMA_NON_DATA                   = 0x63,
        ATA_SEND_FPDMA                       = 0x64,
        ATA_RECEIVE_FPDMA                    = 0x65,
        ATA_SEEK_CMD                         = 0x70,
        ATA_SET_DATE_AND_TIME_EXT            = 0x77,
        ATA_ACCESSABLE_MAX_ADDR              = 0x78,
        ATA_REMOVE_AND_TRUNCATE              = 0x7C,
        ATA_RESTORE_AND_REBUILD              = 0x7D,
        ATA_REMOVE_ELEMENT_AND_MODIFY_ZONES  = 0x7E,
        ATA_CFA_TRANSLATE_SECTOR             = 0x87,
        ATA_EXEC_DRV_DIAG                    = 0x90,
        ATA_INIT_DRV_PARAM                   = 0x91,
        ATA_DLND_CODE                        = 0x92,
        ATA_DOWNLOAD_MICROCODE_CMD           = 0x92,
        ATA_DOWNLOAD_MICROCODE_DMA           = 0x93,
        ATA_LEGACY_ALT_STANDBY_IMMEDIATE     = 0x94,
        ATA_LEGACY_ALT_IDLE_IMMEDIATE        = 0x95,
        ATA_LEGACY_ALT_STANDBY               = 0x96,
        ATA_MUTATE_EXT                       = 0x96,
        ATA_LEGACY_ALT_IDLE                  = 0x97,
        ATA_LEGACY_ALT_CHECK_POWER_MODE      = 0x98,
        ATA_LEGACY_ALT_SLEEP                 = 0x99,
        ATA_ZONE_MANAGEMENT_OUT              = 0x9F,
        ATAPI_COMMAND                        = 0xA0,
        ATAPI_IDENTIFY                       = 0xA1,
        ATA_SMART_CMD                        = 0xB0,
        ATA_DCO                              = 0xB1,
        ATA_SET_SECTOR_CONFIG_EXT            = 0xB2,
        ATA_SANITIZE                         = 0xB4,
        ATA_NV_CACHE                         = 0xB6,
        ATA_CFA_EXTENDED_IDENTIFY            = 0xB7, // Feature 0x0001
        ATA_CFA_KEY_MANAGEMENT               = 0xB9,
        ATA_CFA_STREAMING_PERFORMANCE        = 0xBB,
        ATA_CFA_ERASE_SECTORS                = 0xC0,
        ATA_READ_MULTIPLE_CMD                = 0xC4,
        ATA_WRITE_MULTIPLE_CMD               = 0xC5,
        ATA_SET_MULTIPLE                     = 0xC6,
        ATA_READ_DMA_QUEUED_CMD              = 0xC7,
        ATA_READ_DMA_RETRY_CMD               = 0xC8,
        ATA_READ_DMA_NORETRY                 = 0xC9,
        ATA_WRITE_DMA_RETRY_CMD              = 0xCA,
        ATA_WRITE_DMA_NORETRY                = 0xCB,
        ATA_WRITE_DMA_QUEUED_CMD             = 0xCC,
        ATA_WRITE_MULTIPLE_FUA_EXT           = 0xCE,
        ATA_CFA_WRITE_MULTIPLE_WITHOUT_ERASE = 0xCD,
        ATA_GET_MEDIA_STATUS                 = 0xDA,
        ATA_ACK_MEDIA_CHANGE                 = 0xDB,
        ATA_POST_BOOT                        = 0xDC,
        ATA_PRE_BOOT                         = 0xDD,
        ATA_DOOR_LOCK_CMD                    = 0xDE,
        ATA_DOOR_UNLOCK_CMD                  = 0xDF,
        ATA_STANDBY_IMMD                     = 0xE0,
        ATA_IDLE_IMMEDIATE_CMD               = 0xE1,
        ATA_STANDBY_CMD                      = 0xE2,
        ATA_IDLE_CMD                         = 0xE3,
        ATA_READ_BUF                         = 0xE4,
        ATA_CHECK_POWER_MODE_CMD             = 0xE5,
        ATA_SLEEP_CMD                        = 0xE6,
        ATA_FLUSH_CACHE_CMD                  = 0xE7,
        ATA_WRITE_BUF                        = 0xE8,
        ATA_READ_BUF_DMA                     = 0xE9,
        ATA_LEGACY_WRITE_SAME                = 0xE9,
        ATA_FLUSH_CACHE_EXT                  = 0xEA,
        ATA_WRITE_BUF_DMA                    = 0xEB,
        ATA_IDENTIFY                         = 0xEC,
        ATA_MEDIA_EJECT                      = 0xED,
        ATA_IDENTIFY_DMA                     = 0xEE,
        ATA_SET_FEATURE                      = 0xEF,
        ATA_SECURITY_SET_PASS                = 0xF1,
        ATA_SECURITY_UNLOCK_CMD              = 0xF2,
        ATA_SECURITY_ERASE_PREP              = 0xF3,
        ATA_SECURITY_ERASE_UNIT_CMD          = 0xF4,
        ATA_SECURITY_FREEZE_LOCK_CMD         = 0xF5,
        ATA_CFA_WEAR_LEVEL                   = 0xF5,
        ATA_SECURITY_DISABLE_PASS            = 0xF6,
        ATA_LEGACY_TRUSTED_RECEIVE           = 0xF7,
        ATA_READ_MAX_ADDRESS                 = 0xF8,
        ATA_SET_MAX                          = 0xF9,
        ATA_LEGACY_TRUSTED_SEND              = 0xFB,
        ATA_SEEK_EXT                         = 0xFC,
    } eATA_CMDS;

    typedef enum eNCQNonDataSubCommandsEnum
    {
        NCQ_NON_DATA_ABORT_NCQ_QUEUE            = 0x00,
        NCQ_NON_DATA_DEADLINE_HANDLING          = 0x01,
        NCQ_NON_DATA_HYBRID_DEMOTE_BY_SIZE      = 0x02,
        NCQ_NON_DATA_HYBRID_CHANGE_BY_LBA_RANGE = 0x03,
        NCQ_NON_DATA_HYBRID_CONTROL             = 0x04,
        NCQ_NON_DATA_SET_FEATURES               = 0x05,
        NCQ_NON_DATA_ZERO_EXT                   = 0x06,
        NCQ_NON_DATA_ZAC_MANAGEMENT_OUT         = 0x07,
    } eNCQNonDataSubCommands;

    typedef enum eReceiveFPDMASubCommandsEnum
    {
        RECEIVE_FPDMA_RESERVED          = 0x00,
        RECEIVE_FPDMA_READ_LOG_DMA_EXT  = 0x01,
        RECEIVE_FPDMA_ZAC_MANAGEMENT_IN = 0x02,
    } eReceiveFPDMASubCommands;

    typedef enum eSendFPDMASubCommandsEnum
    {
        SEND_FPDMA_DATA_SET_MANAGEMENT    = 0x00,
        SEND_FPDMA_HYBRID_EVICT           = 0x01,
        SEND_FPDMA_WRITE_LOG_DMA_EXT      = 0x02,
        SEND_FPDMA_ZAC_MANAGEMENT_OUT     = 0x03,
        SEND_FPDMA_DATA_SET_MANAGEMENT_XL = 0x04,
    } eSendFPDMASubCommands;

    typedef enum eAtaSCTActionCodesEnum
    {
        SCT_RESERVED               = 0x00,
        SCT_READ_WRITE_LONG        = 0x01, // obsolete in newer standards
        SCT_WRITE_SAME             = 0x02,
        SCT_ERROR_RECOVERY_CONTROL = 0x03,
        SCT_FEATURE_CONTROL        = 0x04,
        SCT_DATA_TABLES            = 0x05,
        SCT_VENDOR                 = 0x06,
        SCT_RESERVED_FOR_SATA      = 0x07,
        // as new things are added to the ATA spec, add them from here
        UNKNOWN_ACTION
    } eAtaSCTActionCodes;

    typedef enum eSCTRWLModeEnum
    {
        SCT_RWL_RESERVED   = 0x00,
        SCT_RWL_READ_LONG  = 0x01,
        SCT_RWL_WRITE_LONG = 0x02,
    } eSCTRWLMode;

    typedef enum eAtaCmdTypeEnum
    {
        ATA_CMD_TYPE_UNKNOWN,
        ATA_CMD_TYPE_TASKFILE,          // 28bit command
        ATA_CMD_TYPE_EXTENDED_TASKFILE, // 48bit command, minus AUX and ICC registers (which usually cannot be passed
                                        // through anyways)
        ATA_CMD_TYPE_COMPLETE_TASKFILE, // Used for commands trying to set ICC or AUX registers as this will create a
                                        // 32B CDB. This is LIKELY not supported as it has yet to be seen "in the wild".
                                        // Avoid trying these commands since 90% of HBAs tested won't allow FPDMA
                                        // protocol to passthrough anyways - TJE
        ATA_CMD_TYPE_SOFT_RESET,
        ATA_CMD_TYPE_HARD_RESET,
        ATA_CMD_TYPE_PACKET
    } eAtaCmdType;

    typedef enum eAtaProtocolEnum
    {
        ATA_PROTOCOL_UNKNOWN,    // initial setting
        ATA_PROTOCOL_PIO,        // various, includes r/w
        ATA_PROTOCOL_DMA,        // various, includes r/w
        ATA_PROTOCOL_NO_DATA,    // various (e.g. NOP)
        ATA_PROTOCOL_DEV_RESET,  // device RESET
        ATA_PROTOCOL_DEV_DIAG,   // EXECUTE device DIAGNOSTIC
        ATA_PROTOCOL_DMA_QUE,    // Read/Write DMA Queued (tagged command queuing non-SATA)
        ATA_PROTOCOL_PACKET,     // PACKET
        ATA_PROTOCOL_PACKET_DMA, // PACKET
        ATA_PROTOCOL_DMA_FPDMA,  // READ/WRITE FPDMA QUEUED
        ATA_PROTOCOL_SOFT_RESET, // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_HARD_RESET, // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_RET_INFO,   // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_UDMA,       // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_MAX_VALUE,  // Error check terminator
    } eAtaProtocol;

    typedef struct s_ataTFRBlock
    {
        uint8_t CommandStatus;
        uint8_t ErrorFeature;
        union
        {
            uint8_t LbaLow;
            uint8_t SectorNumber;
        };
        union
        {
            uint8_t LbaMid;
            uint8_t CylinderLow;
        };
        union
        {
            uint8_t LbaHi;
            uint8_t CylinderHigh;
        };
        uint8_t DeviceHead;

        union
        {
            uint8_t LbaLow48;
            uint8_t SectorNumberExt;
        };
        union
        {
            uint8_t LbaMid48;
            uint8_t CylinderLowExt;
        };
        union
        {
            uint8_t LbaHi48;
            uint8_t CylinderHighExt;
        };
        uint8_t Feature48;

        uint8_t SectorCount;
        uint8_t SectorCount48;
        uint8_t icc;
        uint8_t DeviceControl;
        // Pad it out to 16 bytes
        uint8_t aux1; // bits 7:0
        uint8_t aux2; // bits 15:8
        uint8_t aux3; // bits 23:16
        uint8_t aux4; // bits 31:24
    } ataTFRBlock;

    // This is really for sending a SAT command so that it gets built properly.
    typedef enum eATAPassthroughTransferBlocksEnum
    {
        ATA_PT_512B_BLOCKS,
        ATA_PT_LOGICAL_SECTOR_SIZE,
        ATA_PT_NUMBER_OF_BYTES,
        ATA_PT_NO_DATA_TRANSFER
    } eATAPassthroughTransferBlocks;

    // This is really for sending a SAT command so that it gets built properly.
    typedef enum eATAPassthroughLengthEnum
    {
        ATA_PT_LEN_NO_DATA,
        ATA_PT_LEN_FEATURES_REGISTER, // bits 7:0
        ATA_PT_LEN_SECTOR_COUNT,      // bits 7:0
        ATA_PT_LEN_TPSIU
    } eATAPassthroughLength;

    // \struct typedef struct s_ataPassthroughCommand
    typedef struct s_ataPassthroughCommand
    {
        eAtaCmdType            commandType;
        eDataTransferDirection commandDirection;
        eAtaProtocol           commadProtocol;
        ataTFRBlock            tfr;
        ataReturnTFRs          rtfr; // Should we make it ataTFRBlock for easy copy?
        uint8_t*               ptrData;
        uint32_t               dataSize;
        uint8_t*               ptrSenseData;
        uint8_t                senseDataSize;
        uint32_t               timeout; // timeout override for the lower layers. This is a time in seconds.
        eATAPassthroughTransferBlocks
            ataTransferBlocks; // this is in here for building the SAT command to set the T_Type bits properly depending
                               // on the command we are issuing.
        eATAPassthroughLength
            ataCommandLengthLocation; // this is in here for building the SAT command to set the T_Length field
                                      // properly. This is important for asynchronous commands
        uint8_t multipleCount; // This is the exponent value specifying the number of sectors used in a read/write
                               // multiple command transfer. All other commands should leave this at zero. This ONLY
                               // matters on read/write multiple commands, if this is nonzero on any other command, it
                               // will fail. Only bits 0:2 are valid (SAT limitation)
        bool forceCheckConditionBit; // Set this to force setting the check condition bit on a command. This is here
                                     // because by default,only non-data gets this bit due to some weird chipsets. This
                                     // is an override that can be used in certain commands.
        uint8_t forceCDBSize;  // only set this if you want to force a specific SAT passthrough CDB size (12B, 16B, or
                               // 32B). Bad parameter may be returned if setting registers in a command that cannot be
                               // set in the specified SAT CDB
        bool fwdlFirstSegment; // firmware download unique flag to help low-level OSs (Windows)
        bool fwdlLastSegment;  // firmware download unique flag to help low-level OSs (Windows)
    } ataPassthroughCommand;

#define SMART_ATTRIBUTE_RAW_DATA_BYTE_COUNT UINT8_C(7)
    // clang-format off
    // added these packs to make sure this structure gets interpreted correctly
    //  in the code when I point it to a buffer and try and access it.
    M_PACK_ALIGN_STRUCT(ataSMARTAttribute, 1,
                        uint8_t attributeNumber;
                        uint16_t status; /*bit 0 = prefail warranty bit, bit 1 = online collection, bit 2 = performance,
                                            bit 3 = error rate, bit 4 = even counter, bit 5 = self preserving*/
                        uint8_t nominal;
                        uint8_t worstEver;
                        uint8_t rawData[SMART_ATTRIBUTE_RAW_DATA_BYTE_COUNT]; /*attribute and vendor specific*/
    );
    // clang-format on

    // Static assert to make sure this structure is exactly12B and compiler has not inserted extra padding
    M_STATIC_ASSERT(sizeof(ataSMARTAttribute) == 12, smart_attribute_not_12_bytes);

#define SMART_THRESHOLD_RESERVED_DATA_BYTE_COUNT UINT8_C(10)
    // clang-format off
    M_PACK_ALIGN_STRUCT(ataSMARTThreshold, 1,
                        uint8_t attributeNumber;
                        uint8_t thresholdValue;
                        uint8_t reservedBytes[SMART_THRESHOLD_RESERVED_DATA_BYTE_COUNT];);
    // clang-format on

    // Static assert to make sure this structure is exactly12B and compiler has not inserted extra padding
    M_STATIC_ASSERT(sizeof(ataSMARTThreshold) == 12, smart_threshold_not_12_bytes);

#define ATA_LOG_PAGE_LEN_BYTES                                                                                         \
    UINT16_C(512) // each page of a log is 512 bytes. A given log may be multiple pages long, or multiples of this
                  // value.

    /*
    RO  - Log is read only.
    R/W - Log is read or written.
    VS  - Log is vendor specific thus read/write ability is vendor specific.
    GPL - General Purpose Logging
    SL  - SMART Logging
    (a) - The device shall return command aborted if a GPL feature set (see 4.11) command accesses a log that
    is marked only with SL.
    (b) - The device shall return command aborted if a SMART feature set (see 4.19) command accesses a log that
    is marked only with GPL.
    */
    // clang-format off
                                             /*  Log Address | Log Name                                  | Feature Set   | R/W   | Access */
    M_PACK_ALIGN_STRUCT(ataLogDirectorySector, 1,
        uint16_t LogDir;                     /*  00          | Log directory                             | none          | RO    | GPL,SL */
        uint16_t SummarySMARTErrLog;         /*  01          | Summary SMART Error Log                   | SMART         | RO    | SL (a) */
        uint16_t CompSMARTErrLog;            /*  02          | Comprehensive SMART Error Log             | SMART         | RO    | SL (a) */
        uint16_t ExtCompSMARTErrLog;         /*  03          | Ext. Comprehensive SMART Error Log        | SMART         | RO    | GPL (b)*/
        uint16_t DeviceStatistics;           /*  04          | Device Statistics                         | none          | RO    | GPL, SL*/
        uint16_t ReservedCFA1;               /*  05          |                                           |               |       |        */
        uint16_t SMARTSelfTestLog;           /*  06          | SMART Self-Test Log                       | SMART         | RO    | SL (a) */
        uint16_t ExtSMARTSelfTestLog;        /*  07          | Ext. SMART Self-Test Log                  | SMART         | RO    | GLB (b)*/
        uint16_t PowerConditions;            /*  08          | Power Conditions                          | EPC           | RO    | GPL (b)*/
        uint16_t SelectiveSelfTestLog;       /*  09          | Selective Self-Test Log                   | SMART         | R/W   | SL (a) */
        uint16_t DeviceStatNotification;     /*  0A          | Device Statistics Notification            | DSN           | R/W   | GPL (b)*/
        uint16_t ReservedCFA2;               /*  0B          |                                           |               |       |        */
        uint16_t PendingDefects;             /*  0C          | Pending Defects Log                       |               |       |        */
        uint16_t LPSMisAlignLog;             /*  0D          | LPS Mis-alignment Log                     | LPS           | RO    | GPL,SL */
        uint16_t ReservedZAC;                /*  0E          |                                           |               |       |        */
        uint16_t SenseDataForSuccessfulNCQ;  /*  0F          | Sense data for successful NCQ commands    | NCQ           | RO    | GPL (b)*/
        uint16_t NCQCmdErrLog;               /*  10          | NCQ Command Error Log                     | NCQ           | RO    | GPL (b)*/
        uint16_t SATAPhyEventCountLog;       /*  11          | SATA Phy Event Counters Log               | none          | RO    | GPL (b)*/
        uint16_t SATANCQQueueManageLog;      /*  12          | SATA NCQ Queue Management Log             | NCQ           | RO    | GPL (b)*/
        uint16_t SATANCQSendRecvLog;         /*  13          | SATA NCQ Send & Receive Log               | NCQ           | RO    | GPL (b)*/
        uint16_t HybridInfoLog;              /*  14          | Hybrid Information Log                    | Hybrid Info   | RO    | GPL (b)*/
        uint16_t RebuildAssistLog;           /*  15          | Rebuild Assist Log                        | Rebuild Assist| R/W   | GPL (b)*/
        uint16_t OOBandManagementControl;    /*  16          | Out Of Band Management Control Log        | OOB Management| R/W   | GPL (b)*/
        uint16_t ReservedSATA;               /*  17          | Reserved for Serial ATA                   |               |       |        */
        uint16_t CommandDurationLimits;      /*  18          | Command Duration Limits Log               | CDL           | R/W   | GPL (b)*/
        uint16_t LBAStatus;                  /*  19          | LBA Status                                | none          | RO    | GPL (b)*/
        uint16_t Reserved3[6];               /*  1A..1F      | Reserved                                  |               |       |        */
        uint16_t StreamingPerformance;       /*  20          | Streaming Performance Log (Obsolete)      | Streaming     | RO    | GPL (b)*/
        uint16_t WriteStreamErrLog;          /*  21          | Write Stream Error Log                    | Streaming     | RO    | GPL (b)*/
        uint16_t ReadStreamErrLog;           /*  22          | Read Stream Error Log                     | Streaming     | RO    | GPL (b)*/
        uint16_t DelayedLBALog;              /*  23          | Delayed LBA Log (Obsolete)                |               | RO    |        */
        uint16_t CurrDevInternalStsDataLog;  /*  24          | Current Device Internal Status Data Log   | none          | RO    | GPL (b)*/
        uint16_t SavedDevInternalStsDataLog; /*  25          | Saved Device Internal Status Data Log     | none          | RO    | GPL (b)*/
        uint16_t Reserved4[9];               /*  26..2E      |                                           |               |       |        */
        uint16_t SetSectorConfiguration;     /*  2F          | Set Sector Configuration Log              | none          | RO    | GPL (b)*/
        uint16_t IdentifyDeviceData;         /*  30          | IDENTIFY DEVICE data                      | none          | RO    | GPL, SL*/
        uint16_t Reserved5[17];              /*  31..41      |                                           |               |       |        */
        uint16_t MutateConfigurations;       /*  42          | Mutate Configurations Log                 | User Data Init| RO    | GPL (b)*/
        uint16_t Reserved7[4];               /*  43..46      |                                           |               |       |        */
        uint16_t ConcurrentPositioning;      /*  47          | Concurrent Positioning Ranges Log         | none          | RO    | GPL (b)*/
        uint16_t Reserved8[11];              /*  48..52      |                                           |               |       |        */
        uint16_t SenseDataLog;               /*  53          | Sense Data log                            | Sense Data Rep| RO    | GPL (b)*/
        uint16_t Reserved9[5];               /*  54..58      |                                           |               |       |        */
        uint16_t PowerConsumpotionCtrl;      /*  59          | Power Consumption Control Log             | Power Consumpt| RO    | GPL (b)*/
        uint16_t Reserved10[7];              /*  5A..60      |                                           |               |       |        */
        uint16_t CapacityMNMappingLog;       /*  61          | Capacity Model Number Mapping Log         | none          | RO    | GPL (b)*/
        uint16_t Reserved11[30];             /*  62..7F      |                                           |               |       |        */
        uint16_t HostSpecific[32];           /*  80..9F      | Host Specific                             | SMART         | R/W   | GPL, SL*/
        uint16_t DeviceVendorSpecific[64];   /*  A0..DF      | Device Vendor Specific                    | SMART         | VS    | GPL, SL*/
        uint16_t SCTCmdSts;                  /*  E0          | SCT Command / Status                      | SCT           | R/W   | GPL, SL*/
        uint16_t SCTDataXfer;                /*  E1          | SCT Data Transfer                         | SCT           | R/W   | GPL, SL*/
        uint16_t Reserved6[30];              /*  E2..FF      |                                           |               |       |        */
    );
    // clang-format on

    // Make sure structure above is exactly 512B because if it is updated it is easy to make mistakes and throw this off
    // again.
    M_STATIC_ASSERT(sizeof(ataLogDirectorySector) == ATA_LOG_PAGE_LEN_BYTES, ata_log_directory_is_not_512_bytes);

    static M_INLINE uint32_t get_ATA_Log_Size_From_Directory(uint8_t* ptr,
                                                             uint32_t datalen /*must be 512*/,
                                                             uint8_t  logAddress)
    {
        uint32_t length = UINT32_C(0);
        if (ptr != M_NULLPTR && datalen == ATA_LOG_PAGE_LEN_BYTES)
        {
            // NOTE: All the casts and conversions look messy, but it makes clang-tidy stop warning about implicit
            // conversions -TJE
            length =
                bytes_To_Uint16(ptr[uint32_to_sizet((M_STATIC_CAST(uint32_t, logAddress) * UINT32_C(2)) + UINT32_C(1))],
                                ptr[uint32_to_sizet(M_STATIC_CAST(uint32_t, logAddress) * UINT32_C(2))]) *
                ATA_LOG_PAGE_LEN_BYTES;
        }
        return length;
    }

    // clang-format off
    M_PACK_ALIGN_STRUCT(ataPowerConditionsDescriptor, 1, uint8_t reserved; uint8_t powerConditionFlags;
                        uint16_t reserved2;
                        uint32_t defaultTimerSetting;
                        uint32_t savedTimerSetting;
                        uint32_t currentTimerSetting;
                        uint32_t nomincalRecoveryTimeToPM0;
                        uint32_t minimumTimerSetting;
                        uint32_t maximumTimerSetting;
                        uint8_t  reserved3[36];);
    // clang-format on

    M_STATIC_ASSERT(sizeof(ataPowerConditionsDescriptor) == 64, ata_EPC_descriptor_not_64_bytes);

    typedef struct s_ataSMARTValue
    {
        ataSMARTAttribute data;
        bool              valid;
        ataSMARTThreshold thresholdData;
        bool thresholdDataValid; // new ATA specs no longer support the threshold sector so some drives may not report
                                 // thresholds
        bool isWarrantied;
    } ataSMARTValue;

#define ATA_SMART_LOG_MAX_ATTRIBUTES (256)
    typedef struct s_ataSMARTLog
    {
        ataSMARTValue attributes[ATA_SMART_LOG_MAX_ATTRIBUTES]; // attribute numbers 1 - 255 are valid (check valid bit
                                                                // to make sure it's a used attribute)
    } ataSMARTLog;

    typedef enum eNVCacheFeaturesEnum
    {
        NV_SET_NV_CACHE_POWER_MODE              = 0x0000,
        NV_RETURN_FROM_NV_CACHE_POWER_MODE      = 0x0001,
        NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET      = 0x0010,
        NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET = 0x0011,
        NV_QUERY_NV_CACHE_PINNED_SET            = 0x0012,
        NV_QUERY_NV_CACHE_MISSES                = 0x0013,
        NV_FLUSH_NV_CACHE                       = 0x0014,
        NV_CACHE_ENABLE                         = 0x0015,
        NV_CACHE_DISABLE                        = 0x0016,
        NV_CACHE_UNKNOWN
    } eNVCacheFeatures;

    typedef enum eDCOFeaturesEnum
    {
        DCO_RESTORE      = 0xC0,
        DCO_FREEZE_LOCK  = 0xC1,
        DCO_IDENTIFY     = 0xC2,
        DCO_SET          = 0xC3,
        DCO_IDENTIFY_DMA = 0xC4,
        DCO_SET_DMA      = 0xC5,
    } eDCOFeatures;

    typedef enum eEPCFeatureSetEnum
    {
        ENABLE_EPC_NOT_SET = -1,
        ENABLE_EPC         = 0x04,
        DISABLE_EPC        = 0x05,
    } eEPCFeatureSet;

#define ATA_IDENTIFY_SANITIZE_INDEX (59)
    // If bit 12 of word 59 is set to one the device supports the Sanitize Device feature set.
#define ATA_IDENTIFY_SANITIZE_SUPPORTED (0x1000)
    // If bit 13 of word 59 is set to one, then the device supports the
    // Sanitize Device feature set CRYPTO SCRAMBLE EXT command
#define ATA_IDENTIFY_CRYPTO_SUPPORTED (0x2000)
    // If bit 14 of word 59 is set to one, then the device supports the
    // Sanitize Device feature set OVERWRITE EXT command
#define ATA_IDENTIFY_OVERWRITE_SUPPORTED (0x4000)
    // If bit 15 of word 59 is set to one, then the device supports the
    // Sanitize Device feature set BLOCK ERASE EXT command
#define ATA_IDENTIFY_BLOCK_ERASE_SUPPORTED (0x8000)

#define ATA_SANITIZE_CRYPTO_LBA            (0x43727970)
#define ATA_SANITIZE_BLOCK_ERASE_LBA       (0x426B4572)
#define ATA_SANITIZE_OVERWRITE_LBA         (0x4F57)
#define ATA_SANITIZE_FREEZE_LOCK_LBA       (0x46724C6B)
#define ATA_SANITIZE_ANTI_FREEZE_LOCK_LBA  (0x416E7469)

#define ATA_SANITIZE_CLEAR_OPR_FAILED      (0x01)
#define ATA_SANITIZE_FAILURE_MODE_BIT_SET  (0x10)
#define ATA_SANITIZE_INVERT_PAT_BIT_SET    (0x80)

    typedef enum eATASanitizeFeatureEnum
    {
        ATA_SANITIZE_STATUS           = 0x0000,
        ATA_SANITIZE_CRYPTO_SCRAMBLE  = 0x0011,
        ATA_SANITIZE_BLOCK_ERASE      = 0x0012,
        ATA_SANITIZE_OVERWRITE_ERASE  = 0x0014,
        ATA_SANITIZE_FREEZE_LOCK      = 0x0020,
        ATA_SANITIZE_ANTI_FREEZE_LOCK = 0x0040,
    } eATASanitizeFeature;

    // if the subcommand you are looking for is not in this enum, please add it
    typedef enum eATASetFeaturesSubcommandsEnum
    {
        SF_RESERVED                                             = 0x00,
        SF_ENABLE_8_BIT_DATA_TRANSFERS                          = 0x01, // retired in ATA4. Obsolete in ATA3
        SF_ENABLE_VOLITILE_WRITE_CACHE                          = 0x02,
        SF_SET_TRANSFER_MODE                                    = 0x03,
        SF_ENABLE_ALL_AUTOMATIC_DEFECT_REASSIGNMENT             = 0x04, // Defined in ATA3, obsolete since ATA4
        SF_ENABLE_APM_FEATURE                                   = 0x05,
        SF_ENABLE_PUIS_FEATURE                                  = 0x06,
        SF_PUIS_DEVICE_SPIN_UP                                  = 0x07,
        SF_ADDRESS_OFFSET_RESERVED_BOOT_AREA_METHOD_TECH_REPORT = 0x09, // Defined in ATA5, obsolete in ACS3
        SF_ENABLE_CFA_POWER_MODE1                               = 0x0A,
        SF_ENABLE_WRITE_READ_VERIFY_FEATURE                     = 0x0B,
        SF_ENABLE_DEVICE_LIFE_CONTROL                           = 0x0C,
        SF_CDL_FEATURE                                          = 0x0D,
        SF_ENABLE_SATA_FEATURE                                  = 0x10,
        SF_TLC_SET_CCTL =
            0x20, // set command completion time limit for devices supporting the old time limited commands feature set
        SF_TCL_SET_ERROR_HANDLING =
            0x21, // Sets error handling for devices supporting TLC and read/write continuous mode
        SF_DISABLE_MEDIA_STATUS_NOTIFICATION                        = 0x31, // Defined in ATA4, obsolete in ATA8/ACS
        SF_DISABLE_RETRY                                            = 0x33, // Defined in ATA3, obsolete in ATA5
        SF_ENABLE_FREE_FALL_CONTROL_FEATURE                         = 0x41,
        SF_ENABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT_FEATURE             = 0x42,
        SF_MAXIMUM_HOST_INTERFACE_SECTOR_TIMES                      = 0x43,
        SF_LEGACY_SET_VENDOR_SPECIFIC_ECC_BYTES_FOR_READ_WRITE_LONG = 0x44, // defined in ATA, obsolete in ATA4
        SF_SET_RATE_BASIS                                           = 0x45,
        SF_ZAC_ZONE_ACTIVATION_CONTROL = 0x46, // ZAC2 to set the number of zones. Can affect the zone activate ext
                                               // command or the zone query ext command
        SF_ZAC_UPDATE_UNRESTRICTED_READ_S_WHILE_READING_ZONES = 0x47, // ZAC2 update URSWRZ
        SF_EXTENDED_POWER_CONDITIONS                          = 0x4A,
        SF_SET_CACHE_SEGMENTS                                 = 0x54, // defined in ATA3, obsolete in ATA4
        SF_DISABLE_READ_LOOK_AHEAD_FEATURE                    = 0x55,
        // 56h - 5Ch are vendor unique
        SF_ENABLE_RELEASE_INTERRUPT                           = 0x5D, // TCQ related
        SF_ENABLE_SERVICE_INTERRUPT                           = 0x5E, // TCQ related
        SF_ENABLE_DISABLE_DATA_TRANSFER_AFTER_ERROR_DETECTION = 0x5F, // DDT feature...for some very specific old hosts
        SF_LONG_PHYSICAL_SECTOR_ALIGNMENT_ERROR_REPORTING     = 0x62,
        SF_ENABLE_DISABLE_DSN_FEATURE                         = 0x63,
        SF_DISABLE_REVERTING_TO_POWERON_DEFAULTS              = 0x66,
        SF_CFA_NOP_ACCEPTED_FOR_BACKWARDS_COMPATIBILITY =
            0x69, // Likely defined in old manual if you can find it: SanDisk SDP Series OEM Manual, 1993
        SF_DISABLE_ECC                                            = 0x77, // defined in ATA3, obsolete in ATA4
        SF_DISABLE_8_BIT_DATA_TRANSFERS                           = 0x81, // defined in ATA, obsolete in ATA3
        SF_DISABLE_VOLITILE_WRITE_CACHE                           = 0x82,
        SF_DISABLE_ALL_AUTOMATIC_DEFECT_REASSIGNMENT              = 0x84, // defined in ATA3, obsolete in ATA4
        SF_DISABLE_APM_FEATURE                                    = 0x85,
        SF_DISABLE_PUIS_FEATURE                                   = 0x86,
        SF_ENABLE_ECC                                             = 0x88, // defined in ATA3, obsolete in ATA6
        SF_ADDRESS_OFFSET_RESERVED_BOOT_AREA_METHOD_TECH_REPORT_2 = 0x89, // Defined in ATA5, obsolete in ACS3
        SF_DISABLE_CFA_POWER_MODE_1                               = 0x8A,
        SF_DISABLE_WRITE_READ_VERIFY_FEATURE                      = 0x8B,
        SF_DISABLE_DEVICE_LIFE_CONTROL                            = 0x8C,
        SF_DISABLE_SATA_FEATURE                                   = 0x90,
        SF_ENABLE_MEDIA_STATUS_NOTIFICATION                       = 0x95,
        SF_CFA_NOP_ACCEPTED_FOR_BACKWARDS_COMPATIBILITY_1 =
            0x96, // Likely defined in old manual if you can find it: SanDisk SDP Series OEM Manual, 1993
        SF_CFA_ACCEPTED_FOR_BACKWARDS_COMPATIBILITY =
            0x97, // In one old vendor unique CFA spec, this is set clock speed per sector cnt reg. Valid codes are 0 -
                  // divide by 4, A&B - divide by 2, E&F - divide by 1. I have never found a description of this any
                  // where else and I found this on accident looking for something completely different. - TJE
        SF_ENABLE_RETIRES                                               = 0x99,
        SF_SET_DEVICE_MAXIMUM_AVERAGE_CURRENT                           = 0x9A, // Defined in ATA3, obsolete in ATA4
        SF_ENABLE_READ_LOOK_AHEAD_FEATURE                               = 0xAA,
        SF_SET_MAXIMUM_PREFETCH                                         = 0xAB, // defined in ATA3, obsolete in ATA4
        SF_LEGACY_SET_4_BYTES_ECC_FOR_READ_WRITE_LONG                   = 0xBB,
        SF_DISABLE_FREE_FALL_CONTROL_FEATURE                            = 0xC1,
        SF_DISABLE_AUTOMATIC_ACOUSTIC_MANAGEMENT                        = 0xC2,
        SF_ENABLE_DISABLE_SENSE_DATA_REPORTING_FEATURE                  = 0xC3,
        SF_ENABLE_DISABLE_SENSE_DATA_RETURN_FOR_SUCCESSFUL_NCQ_COMMANDS = 0xC4,
        SF_ENABLE_REVERTING_TO_POWER_ON_DEFAULTS                        = 0xCC,
        // D6-DC are vendor unique
        SF_DISABLE_RELEASE_INTERRUPT                           = 0xDD,
        SF_DISABLE_SERVICE_INTERRUPT                           = 0xDE,
        SF_DISABLE_DISABLE_DATA_TRANSFER_AFTER_ERROR_DETECTION = 0xDF,
        // E0 is vendor unique
        // F0 - FF are reserved for CFA
        SF_UNKNOWN_FEATURE
    } eATASetFeaturesSubcommands;

    typedef enum eWRVModeEnum
    {
        ATA_WRV_MODE_ALL    = 0x00, // mode 0
        ATA_WRV_MODE_65536  = 0x01, // mode 1
        ATA_WRV_MODE_VENDOR = 0x02, // mode 2
        ATA_WRV_MODE_USER   = 0x03  // mode 3
    } eWRVMode;

#define MAX_WRV_USER_SECTORS UINT32_C(261120)
#define WRV_USER_MULTIPLIER  UINT16_C(1024) // sector count * this = number of sectors being verified in this mode.

    // this is the 7:3 bits of the count register for the SF_SET_TRANSFER_MODE option
    // Bits 2:0 can be used to specify the mode.
    typedef enum eSetTransferModeTransferModesEnum
    {
        SF_TRANSFER_MODE_PIO_DEFAULT               = 0,
        SF_TRANSFER_MODE_PIO_DEFAULT_DISABLE_IORDY = 0,
        SF_TRANSFER_MODE_FLOW_CONTROL              = 1,
        SF_TRANSFER_MODE_SINGLE_WORD_DMA           = 2,
        SF_TRANSFER_MODE_MULTI_WORD_DMA            = 4,
        SF_TRANSFER_MODE_ULTRA_DMA                 = 8,
        SF_TRANSFER_MODE_RESERVED                  = 5
    } eSetTransferModeTransferModes;

    typedef enum eLPSErrorReportingControlEnum
    {
        SF_LPS_DISABLED                                      = 0x00,
        SF_LPS_REPORT_ALIGNMENT_ERROR                        = 0x01,
        SF_LPS_REPORT_ALIGNMENT_ERROR_DATA_CONDITION_UNKNOWN = 0x02,
    } eLPSErrorReportingControl;

    typedef enum eDSNFeatureEnum
    {
        SF_DSN_RESERVED = 0x00,
        SF_DSN_ENABLE   = 0x01,
        SF_DSN_DISABLE  = 0x02,
    } eDSNFeature;

    typedef enum eSATAFeaturesEnum
    {
        SATA_FEATURE_RESERVED                                           = 0x00,
        SATA_FEATURE_NONZERO_BUFFER_OFFSETS                             = 0x01,
        SATA_FEATURE_DMA_SETUP_FIS_AUTO_ACTIVATE                        = 0x02,
        SATA_FEATURE_DEVICE_INITIATED_INTERFACE_POWER_STATE_TRANSITIONS = 0x03,
        SATA_FEATURE_GUARANTEED_IN_ORDER_DATA_DELIVERY                  = 0x04,
        SATA_FEATURE_ASYNCHRONOUS_NOTIFICATION                          = 0x05,
        SATA_FEATURE_SOFTWARE_SETTINGS_PRESERVATION                     = 0x06,
        SATA_FEATURE_DEVICE_AUTOMATIC_PARTIAL_TO_SLUMBER_TRANSITIONS    = 0x07,
        SATA_FEATURE_ENABLE_HARDWARE_FEATURE_CONTROL                    = 0x08,
        SATA_FEATURE_ENABLE_DISABLE_DEVICE_SLEEP                        = 0x09,
        SATA_FEATURE_ENABLE_DISABLE_HYBRID_INFORMATION                  = 0x0A,
        SATA_FEATURE_ENABLE_DISABLE_POWER_DISABLE                       = 0x0B,
    } eSATAFeatures;

    typedef enum eEPCSubcommandsEnum
    {
        EPC_RESTORE_POWER_CONDITION_SETTINGS = 0x0,
        EPC_GO_TO_POWER_CONDITION            = 0x1,
        EPC_SET_POWER_CONDITION_TIMER        = 0x2,
        EPC_SET_POWER_CONDITION_STATE        = 0x3,
        EPC_ENABLE_EPC_FEATURE_SET           = 0x4,
        EPC_DISABLE_EPC_FEATURE_SET          = 0x5,
        EPC_SET_EPC_POWER_SOURCE             = 0x6,
        EPC_RESERVED_FEATURE
    } eEPCSubcommands;

    typedef enum eEPCPowerConditionEnum
    {
        EPC_POWER_CONDITION_STANDBY_Z            = 0x00,
        EPC_POWER_CONDITION_STANDBY_Y            = 0x01,
        EPC_POWER_CONDITION_IDLE_A               = 0x81,
        EPC_POWER_CONDITION_IDLE_B               = 0x82,
        EPC_POWER_CONDITION_IDLE_C               = 0x83,
        EPC_POWER_CONDITION_ALL_POWER_CONDITIONS = 0xFF
    } eEPCPowerCondition;

    typedef enum eSCTWriteSameFunctionsEnum
    {
        WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD            = 0x0001,
        WRITE_SAME_BACKGROUND_USE_SINGLE_LOGICAL_SECTOR    = 0x0002,
        WRITE_SAME_BACKGROUND_USE_MULTIPLE_LOGICAL_SECTORS = 0x0003,
        WRITE_SAME_FOREGROUND_USE_PATTERN_FIELD            = 0x0101,
        WRITE_SAME_FOREGROUND_USE_SINGLE_LOGICAL_SECTOR    = 0x0102,
        WRITE_SAME_FOREGROUND_USE_MULTIPLE_LOGICAL_SECTORS = 0x0103,
    } eSCTWriteSameFunctions;

    typedef enum eHPAFeatureEnum
    {
        HPA_SET_MAX_ADDRESS      = 0x00,
        HPA_SET_MAX_PASSWORD     = 0x01,
        HPA_SET_MAX_LOCK         = 0x02,
        HPA_SET_MAX_UNLOCK       = 0x03,
        HPA_SET_MAX_FREEZE_LOCK  = 0x04,
        HPA_SET_MAX_PASSWORD_DMA = 0x05,
        HPA_SET_MAX_UNLOCK_DMA   = 0x06,
        HPA_SET_MAX_RESERVED
    } eHPAFeature;

    typedef enum eATALogEnum
    {
        ATA_LOG_DIRECTORY                               = 0x00,
        ATA_LOG_SUMMARY_SMART_ERROR_LOG                 = 0x01,
        ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG           = 0x02,
        ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG  = 0x03,
        ATA_LOG_DEVICE_STATISTICS                       = 0x04,
        ATA_LOG_SMART_SELF_TEST_LOG                     = 0x06,
        ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG            = 0x07,
        ATA_LOG_POWER_CONDITIONS                        = 0x08,
        ATA_LOG_SELECTIVE_SELF_TEST_LOG                 = 0x09,
        ATA_LOG_DEVICE_STATISTICS_NOTIFICATION          = 0x0A,
        ATA_LOG_PENDING_DEFECTS_LOG                     = 0x0C,
        ATA_LOG_LPS_MISALIGNMENT_LOG                    = 0x0D,
        ATA_LOG_SENSE_DATA_FOR_SUCCESSFUL_NCQ_COMMANDS  = 0x0F,
        ATA_LOG_NCQ_COMMAND_ERROR_LOG                   = 0x10,
        ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG             = 0x11,
        ATA_LOG_SATA_NCQ_QUEUE_MANAGEMENT_LOG           = 0x12,
        ATA_LOG_SATA_NCQ_SEND_AND_RECEIVE_LOG           = 0x13,
        ATA_LOG_HYBRID_INFORMATION                      = 0x14,
        ATA_LOG_REBUILD_ASSIST                          = 0x15,
        ATA_LOG_OUT_OF_BAND_MANAGEMENT_CONTROL_LOG      = 0x16,
        ATA_LOG_COMMAND_DURATION_LIMITS_LOG             = 0x18,
        ATA_LOG_LBA_STATUS                              = 0x19,
        ATA_LOG_STREAMING_PERFORMANCE                   = 0x20,
        ATA_LOG_WRITE_STREAM_ERROR_LOG                  = 0x21,
        ATA_LOG_READ_STREAM_ERROR_LOG                   = 0x22,
        ATA_LOG_DELAYED_LBA_LOG                         = 0x23,
        ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG = 0x24,
        ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG   = 0x25,
        ATA_LOG_SECTOR_CONFIGURATION_LOG                = 0x2F, // ACS4
        ATA_LOG_IDENTIFY_DEVICE_DATA                    = 0x30,
        ATA_LOG_MUTATE_CONFIGURATIONS                   = 0x42,
        ATA_LOG_CONCURRENT_POSITIONING_RANGES           = 0x47,
        ATA_LOG_SENSE_DATA                              = 0x53,
        ATA_LOG_CAPACITY_MODELNUMBER_MAPPING            = 0x61,
        // 80h - 9F are host specific logs
        ATA_LOG_HOST_SPECIFIC_80H = 0x80,
        ATA_LOG_HOST_SPECIFIC_81H = 0x81,
        ATA_LOG_HOST_SPECIFIC_82H = 0x82,
        ATA_LOG_HOST_SPECIFIC_83H = 0x83,
        ATA_LOG_HOST_SPECIFIC_84H = 0x84,
        ATA_LOG_HOST_SPECIFIC_85H = 0x85,
        ATA_LOG_HOST_SPECIFIC_86H = 0x86,
        ATA_LOG_HOST_SPECIFIC_87H = 0x87,
        ATA_LOG_HOST_SPECIFIC_88H = 0x88,
        ATA_LOG_HOST_SPECIFIC_89H = 0x89,
        ATA_LOG_HOST_SPECIFIC_8AH = 0x8A,
        ATA_LOG_HOST_SPECIFIC_8BH = 0x8B,
        ATA_LOG_HOST_SPECIFIC_8CH = 0x8C,
        ATA_LOG_HOST_SPECIFIC_8DH = 0x8D,
        ATA_LOG_HOST_SPECIFIC_8EH = 0x8E,
        ATA_LOG_HOST_SPECIFIC_8FH = 0x8F,
        ATA_LOG_HOST_SPECIFIC_90H = 0x90,
        ATA_LOG_HOST_SPECIFIC_91H = 0x91,
        ATA_LOG_HOST_SPECIFIC_92H = 0x92,
        ATA_LOG_HOST_SPECIFIC_93H = 0x93,
        ATA_LOG_HOST_SPECIFIC_94H = 0x94,
        ATA_LOG_HOST_SPECIFIC_95H = 0x95,
        ATA_LOG_HOST_SPECIFIC_96H = 0x96,
        ATA_LOG_HOST_SPECIFIC_97H = 0x97,
        ATA_LOG_HOST_SPECIFIC_98H = 0x98,
        ATA_LOG_HOST_SPECIFIC_99H = 0x99,
        ATA_LOG_HOST_SPECIFIC_9AH = 0x9A,
        ATA_LOG_HOST_SPECIFIC_9BH = 0x9B,
        ATA_LOG_HOST_SPECIFIC_9CH = 0x9C,
        ATA_LOG_HOST_SPECIFIC_9DH = 0x9D,
        ATA_LOG_HOST_SPECIFIC_9EH = 0x9E,
        ATA_LOG_HOST_SPECIFIC_9FH = 0x9F,
        // A0-DF are vendor specific logs
        ATA_SCT_COMMAND_STATUS = 0xE0,
        ATA_SCT_DATA_TRANSFER  = 0xE1,
    } eATALog;

    typedef enum eIdentifyDeviceDataLogPageEnum // Log address 30h, ACS-4 Section 9.11
    {
        ATA_ID_DATA_LOG_SUPPORTED_PAGES          = 0x00,
        ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA    = 0x01,
        ATA_ID_DATA_LOG_CAPACITY                 = 0x02,
        ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES   = 0x03,
        ATA_ID_DATA_LOG_CURRENT_SETTINGS         = 0x04,
        ATA_ID_DATA_LOG_ATA_STRINGS              = 0x05,
        ATA_ID_DATA_LOG_SECURITY                 = 0x06,
        ATA_ID_DATA_LOG_PARALLEL_ATA             = 0x07,
        ATA_ID_DATA_LOG_SERIAL_ATA               = 0x08,
        ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION = 0x09,
    } eIdentifyDeviceDataLogPage;
#define ATA_ID_DATA_SUP_PG_LIST_LEN_OFFSET                                                                             \
    UINT16_C(8) // this is the offset in the data where the list length is specified to be read from. The next value is
                // where the list of supported pages begins.
#define ATA_ID_DATA_SUP_PG_LIST_OFFSET                                                                                 \
    UINT16_C(9) // when reading the ID Data log's list of supported pages, this is the offset to start at to find the
                // page numbers that are supported.
#define ATA_ID_DATA_QWORD_VALID_BIT BIT63    // If bit 63 is set, then the qword is valid
#define ATA_ID_DATA_VERSION_1       (0x0001) // to check for at least revision 1 on each page of the log.

    //
    typedef enum eDeviceStatisticsLogEnum // Log Address 04h, ACS-4 Section 9.5
    {
        ATA_DEVICE_STATS_LOG_LIST           = 0x00,
        ATA_DEVICE_STATS_LOG_GENERAL        = 0x01,
        ATA_DEVICE_STATS_LOG_FREE_FALL      = 0x02,
        ATA_DEVICE_STATS_LOG_ROTATING_MEDIA = 0x03,
        ATA_DEVICE_STATS_LOG_GEN_ERR        = 0x04,
        ATA_DEVICE_STATS_LOG_TEMP           = 0x05,
        ATA_DEVICE_STATS_LOG_TRANSPORT      = 0x06,
        ATA_DEVICE_STATS_LOG_SSD            = 0x07,
        ATA_DEVICE_STATS_LOG_ZONED_DEVICE   = 0x08,
        // Add more
        ATA_DEVICE_STATS_LOG_VENDOR_SPECIFIC = 0xFF
    } eDeviceStatisticsLog;
#define ATA_DEV_STATS_SUP_PG_LIST_LEN_OFFSET                                                                           \
    UINT16_C(8) // this is the offset in the data where the list length is specified to be read from. The next value is
                // where the list of supported pages begins.
#define ATA_DEV_STATS_SUP_PG_LIST_OFFSET                                                                               \
    UINT16_C(9) // when reading the device statistics log's list of supported pages, this is the offset to start at to
                // find the page numbers that are supported.
#define ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT  BIT63 // If bit 63 is set, then the qword is valid
#define ATA_DEV_STATS_VALID_VALUE_BIT          BIT62
#define ATA_DEV_STATS_NORMALIZED_STAT_BIT      BIT61
#define ATA_DEV_STATS_SUPPORTS_DSN             BIT60
#define ATA_DEV_STATS_MONITORED_CONDITION_MET  BIT59
#define ATA_DEV_STATS_READ_THEN_INIT_SUPPORTED BIT58
#define ATA_DEV_STATS_VERSION_1                (0x0001) // to check for at least revision 1 on each page of the log.

    typedef enum eSCTDeviceStateEnum
    {
        SCT_STATE_ACTIVE_WAITING_FOR_COMMAND                             = 0x00,
        SCT_STATE_STANDBY                                                = 0x01,
        SCT_STATE_SLEEP                                                  = 0x02,
        SCT_STATE_DST_PROCESSING_IN_BACKGROUND                           = 0x03,
        SCT_STATE_SMART_OFFLINE_DATA_COLLECTION_PROCESSING_IN_BACKGROUND = 0x04,
        SCT_STATE_SCT_COMMAND_PROCESSING_IN_BACKGROUND                   = 0x05,
    } eSCTDeviceState;

    typedef enum eSCTExtendedStatusEnum
    {
        SCT_EXT_STATUS_COMMAND_COMPLETE_NO_ERROR                                                       = 0x0000,
        SCT_EXT_STATUS_INVALID_FUNCTION_CODE                                                           = 0x0001,
        SCT_EXT_STATUS_INPUT_LBA_OUT_OF_RANGE                                                          = 0x0002,
        SCT_EXT_STATUS_REQUEST_512B_DATA_BLOCK_COUNT_OVERFLOW                                          = 0x0003,
        SCT_EXT_STATUS_INVALID_FUNCTION_CODE_IN_SCT_ERROR_RECOVERY                                     = 0x0004,
        SCT_EXT_STATUS_INVALID_SELECTION_CODE_IN_SCT_ERROR_RECOVERY                                    = 0x0005,
        SCT_EXT_STATUS_HOST_READ_COMMAND_TIMER_IS_LESS_THAN_MINIMUM_VALUE                              = 0x0006,
        SCT_EXT_STATUS_HOST_WRITE_COMMAND_TIMER_IS_LESS_THAN_MINIMUM_VALUE                             = 0x0007,
        SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_AN_INTERRUPTING_HOST_COMMAND = 0x0008,
        SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_UNRECOVERABLE_ERROR          = 0x0009,
        SCT_EXT_STATUS_OBSOLETE                                                                        = 0x000A,
        SCT_EXT_STATUS_SCT_DATA_TRANSFER_COMMAND_WAS_ISSUES_WITHOUT_FIRST_ISSUING_AN_SCT_COMMAND       = 0x000B,
        SCT_EXT_STATUS_INVALID_FUNCTION_CODE_IN_SCT_FEATURE_CONTROL_COMMAND                            = 0x000C,
        SCT_EXT_STATUS_INVALID_FEATURE_CODE_IN_SCT_FEATURE_CONTROL_COMMAND                             = 0x000D,
        SCT_EXT_STATUS_INVALID_STATE_VALUE_IN_SCT_FEATURE_CONTROL_COMMAND                              = 0x000E,
        SCT_EXT_STATUS_INVALID_OPTION_FLAGS_VALUE_IN_SCT_FEATURE_CONTROL_COMMAND                       = 0x000F,
        SCT_EXT_STATUS_INVALID_SCT_ACTION_CODE                                                         = 0x0010,
        SCT_EXT_STATUS_INVALID_TABLE_ID                                                                = 0x0011,
        SCT_EXT_STATUS_OPERATION_WAS_TERMINATED_DUE_TO_DEVICE_SECURITY_BEING_LOCKED                    = 0x0012,
        SCT_EXT_STATUS_INVALID_REVISION_CODE_IN_SCT_DATA                                               = 0x0013,
        SCT_EXT_STATUS_FOREGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_UNRECOVERABLE_ERROR          = 0x0014,
        SCT_EXT_STATUS_MOST_RECENT_NON_SCT_COMMAND_COMPLETED_WITH_ERROR_DUE_TO_ERROR_RECOVERY_READ_OR_WRITE_TIMER_EXPIRING =
            0x0015,
        SCT_EXT_STATUS_SCT_COMMAND_PROCESSING_IN_BACKGROUND = 0xFFFF
    } eSCTExtendedStatus;

    typedef enum eSCTFeatureEnum
    {
        SCT_FEATURE_CONTROL_WRITE_CACHE_STATE            = 0x0001,
        SCT_FEATURE_CONTROL_WRITE_CACHE_REORDERING       = 0x0002,
        SCT_FEATURE_CONTROL_SET_HDA_TEMPERATURE_INTERVAL = 0x0003,
        SCT_FEATURE_CONTROL_RESERVED,
        // 0004 & 0005 = reserved for SATA
        // 0006 - CFFF = reserved
        // D000 - FFFF = vendor specifc
        SCT_FEATURE_CONTROL_VENDOR = 0xD000,
    } eSCTFeature;

    typedef enum eSCTFeatureControlFunctionEnum
    {
        SCT_FEATURE_FUNCTION_SET_STATE_AND_OPTIONS  = 0x0001,
        SCT_FEATURE_FUNCTION_RETURN_CURRENT_STATE   = 0x0002,
        SCT_FEATURE_FUNCTION_RETURN_CURRENT_OPTIONS = 0x0003,
    } eSCTFeatureControlFunction;

    typedef enum eAMACCommandEnum // accessible max address configuration
    {
        AMAC_GET_NATIVE_MAX_ADDRESS        = 0x0000,
        AMAC_SET_ACCESSIBLE_MAX_ADDRESS    = 0x0001,
        AMAC_FREEZE_ACCESSIBLE_MAX_ADDRESS = 0x0002,
    } eAMACCommand;

    // Device control register bit definitions
#define DEVICE_CONTROL_SOFT_RESET BIT2
#define DEVICE_CONTROL_nIEN       BIT3

    // This enum is up to date with ACS4-revision 20
    // Blank lines indicate where undefined/reserved values are located so it could be easy to put those definitions in
    // place as new specs are released
    typedef enum eATAMinorVersionNumberEnum
    {
        ATA_MINOR_VERSION_NOT_REPORTED          = 0x0000,
        ATA_MINOR_VERSION_ATA_1_PRIOR_TO_REV_4  = 0x0001, // ATA (ATA-1) X3T9.2 781D prior to revision 4
        ATA_MINOR_VERSION_ATA_1_PUBLISHED       = 0x0002, // ATA-1 published, ANSI X3.221-1994
        ATA_MINOR_VERSION_ATA_1_REV_4           = 0x0003, // ATA (ATA-1) X3T9.2 781D revision 4
        ATA_MINOR_VERSION_ATA_2_PUBLISHED       = 0x0004, // ATA-2 published, ANSI X3.279-1996
        ATA_MINOR_VERSION_ATA_2_PRIOR_TO_REV_2K = 0x0005, // ATA-2 X3T10 948D prior to revision 2k
        ATA_MINOR_VERSION_ATA_3_REV_1           = 0x0006, // ATA-3 X3T10 2008D revision 1
        ATA_MINOR_VERSION_ATA_2_REV_2K          = 0x0007, // ATA-2 X3T10 948D revision 2k
        ATA_MINOR_VERSION_ATA_3_REV_0           = 0x0008, // ATA-3 X3T10 2008D revision 0
        ATA_MINOR_VERSION_ATA_2_REV_3           = 0x0009, // ATA-2 X3T10 948D revision 3
        ATA_MINOR_VERSION_ATA_3_PUBLISHED       = 0x000A, // ATA-3 published, ANSI X3.298-1997
        ATA_MINOR_VERSION_ATA_3_REV_6           = 0x000B, // ATA-3 X3T10 2008D revision 6
        ATA_MINOR_VERSION_ATA_3_REV_7_AND_7A    = 0x000C, // ATA-3 X3T13 2008D revision 7 & 7a
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV_6     = 0x000D, // ATA/ATAPI-4 X3T13 1153D revision 6
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV_13    = 0x000E, // ATA/ATAPI-4 T13 1153D revision 13
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV7      = 0x000F, // ATA/ATAPI-4 X3T13 1153D revision 7
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV_18    = 0x0010, // ATA/ATAPI-4 T13 1153D revision 18
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV_15    = 0x0011, // ATA/ATAPI-4 T13 1153D revision 15
        ATA_MINOR_VERSION_ATA_ATAPI_4_PUBLISHED = 0x0012, // ATA/ATAPI-4 published, ANSI NCITS 317-1998
        ATA_MINOR_VERSION_ATA_ATAPI_5_REV_3     = 0x0013, // ATA/ATAPI-5 T13 1321D revision 3
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV_14    = 0x0014, // ATA/ATAPI-4 T13 1153D revision 14
        ATA_MINOR_VERSION_ATA_ATAPI_5_REV_1     = 0x0015, // ATA/ATAPI-5 T13 1321D revision 1
        ATA_MINOR_VERSION_ATA_ATAPI_5_PUBLISHED = 0x0016, // ATA/ATAPI-5 published, ANSI INCITS 340-2000
        ATA_MINOR_VERSION_ATA_ATAPI_4_REV_17    = 0x0017, // ATA/ATAPI-4 T13 1153D revision 17
        ATA_MINOR_VERSION_ATA_ATAPI_6_REV_0     = 0x0018, // ATA/ATAPI-6 T13 1410D revision 0
        ATA_MINOR_VERSION_ATA_ATAPI_6_REV_3A    = 0x0019, // ATA/ATAPI-6 T13 1410D revision 3a
        ATA_MINOR_VERSION_ATA_ATAPI_7_REV_1     = 0x001A, // ATA/ATAPI-7 T13 1532D revision 1
        ATA_MINOR_VERSION_ATA_ATAPI_6_REV_2     = 0x001B, // ATA/ATAPI-6 T13 1410D revision 2
        ATA_MINOR_VERSION_ATA_ATAPI_6_REV_1     = 0x001C, // ATA/ATAPI-6 T13 1410D revision 1
        ATA_MINOR_VERSION_ATA_ATAPI_7_RUBLISHED = 0x001D, // ATA/ATAPI-7 published ANSI INCITS 397-2005
        ATA_MINOR_VERSION_ATA_ATAPI_7_REV_0     = 0x001E, // ATA/ATAPI-7 T13 1532D revision 0
        ATA_MINOR_VERSION_ACS3_REV_3B           = 0x001F, // ACS-3 Revision 3b

        ATA_MINOR_VERSION_ATA_ATAPI_7_REV_4A    = 0x0021, // ATA/ATAPI-7 T13 1532D revision 4a
        ATA_MINOR_VERSION_ATA_ATAPI_6_PUBLISHED = 0x0022, // ATA/ATAPI-6 published, ANSI INCITS 361-2002

        ATA_MINOR_VERSION_ATA8_ACS_REV_3C = 0x0027, // ATA8-ACS version 3c
        ATA_MINOR_VERSION_ATA8_ACS_REV_6  = 0x0028, // ATA8-ACS version 6
        ATA_MINOR_VERSION_ATA8_ACS_REV_4  = 0x0029, // ATA8-ACS version 4
        ATA_MINOR_VERSION_ACS5_REV_8      = 0x0030, // ACS-5 version 8
        ATA_MINOR_VERSION_ACS2_REV_2      = 0x0031, // ASC-2 Revision 2

        ATA_MINOR_VERSION_ATA8_ACS_REV_3E = 0x0033, // ATA8-ACS version 3e

        ATA_MINOR_VERSION_ATA8_ACS_REV_4C = 0x0039, // ATA8-ACS version 4c

        ATA_MINOR_VERSION_ATA8_ACS_REV_3F = 0x0042, // ATA8-ACS version 3f

        ATA_MINOR_VERSION_ATA8_ACS_REV_3B = 0x0052, // ATA8-ACS version 3b

        ATA_MINOR_VERSION_ACS4_REV_5 = 0x005E, // ACS-4 Revision 5

        ATA_MINOR_VERSION_ACS3_REV_5 = 0x006D, // ACS-3 Revision 5

        ATA_MINOR_VERSION_ACS6_REV_2 = 0x0073, // ACS-6 Revision 2

        ATA_MINOR_VERSION_ACS_2_PUBLISHED = 0x0082, // ACS-2 published, ANSI INCITS 482-2012

        ATA_MINOR_VERSION_ACS4_PUBLISHED = 0x009C, // ACS-4 published, ANSI, INCITS 529-2018

        ATA_MINOR_VERSION_ATA8_ACS_REV_2D = 0x0107, // ATA8-ACS version 2d

        ATA_MINOR_VERSION_ACS3_PUBLISHED = 0x010A, // ACS-3 published, ANSI INCITS 522-2014

        ATA_MINOR_VERSION_ACS2_REV_3 = 0x0110, // ACS-2 Revision 3

        ATA_MINOR_VERSION_ACS3_REV_4 = 0x011B, // ACS-3 Revision 4

        ATA_MINOR_VERSION_NOT_REPORTED_2 = 0xFFFF
    } eATAMinorVersionNumber;

    typedef enum eZACMinorVersionNumberEnum
    {
        ZAC_MINOR_VERSION_NOT_REPORTED   = 0x0000,
        ZAC_MINOR_VERSION_ZAC_REV_5      = 0x05CF, // ZAC revision 05
        ZAC_MINOR_VERSION_ZAC2_REV_15    = 0x3612, // ZAC2 rev 15
        ZAC_MINOR_VERSION_ZAC2_REV_1B    = 0x7317, // ZAC2 rev 1b
        ZAC_MINOR_VERSION_ZAC_REV_4      = 0xA36C, // ZAC revision 04
        ZAC_MINOR_VERSION_ZAC2_REV12     = 0xB403, // ZAC2 revision 12
        ZAC_MINOR_VERSION_ZAC_REV_1      = 0xB6E8, // ZAC Revision 1
        ZAC_MINOR_VERSION_NOT_REPORTED_2 = 0xFFFF
    } eZACMinorVersionNumber;

    typedef enum eTransportMinorVersionNumberEnum
    {
        TRANSPORT_MINOR_VERSION_NOT_REPORTED              = 0x0000,
        TRANSPORT_MINOR_VERSION_ATA8_AST_D1697_VERSION_0B = 0x0021, // ATA8-AST T13 Project D1697 Version 0b
        TRANSPORT_MINOR_VERSION_ATA8_AST_D1697_VERSION_1  = 0x0051, // ATA8-AST T13 Project D1697 Version 0b
        TRANSPORT_MINOR_VERSION_NOT_REPORTED2             = 0xFFFF
    } eTransportMinorVersionNumber;

#define ATA_MAX_BLOCKS_PER_DRQ_DATA_BLOCKS                UINT8_C(128)

#define ATA_SECURITY_MAX_PW_LENGTH                        UINT8_C(32)
#define ATA_SECURITY_GREATER_THAN_MAX_TIME_VALUE          UINT16_C(255)   // raw ATA identify device value
#define ATA_SECURITY_MAX_TIME_MINUTES                     UINT16_C(508)   // raw minutes value
#define ATA_SECURITY_GREATER_THAN_MAX_EXTENDED_TIME_VALUE UINT16_C(32767) // raw ATA identify device value
#define ATA_SECURITY_MAX_EXTENDED_TIME_MINUTES            UINT16_C(65532) // raw minutes value
#define ATA_SECURITY_TIME_MULTIPLIER                      UINT16_C(2)

    typedef enum eATASecurityStateEnum
    {
        ATA_SEC0 = 0, // powered off, we will never see this
        ATA_SEC1 = 1, // not enabled, locked, or frozen
        ATA_SEC2 = 2, // frozen
        ATA_SEC3 = 3, // powered off, we will never see this
        ATA_SEC4 = 4, // enabled, locked
        ATA_SEC5 = 5, // enabled
        ATA_SEC6 = 6  // enabled, frozen
    } eATASecurityState;

#if defined(__cplusplus)
} // extern "C"
#endif
