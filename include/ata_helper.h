//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ****************************************************************************************** *****************************************************************************

// \file ata_helper.h
// \brief Defines the constants structures to help with ATA Specification

#pragma once

#include "common_public.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    #define ATA_STATUS_BIT_BUSY BIT7 //if this is set, all other bits are invalid and not to be used
    #define ATA_STATUS_BIT_READY BIT6
    #define ATA_STATUS_BIT_DEVICE_FAULT BIT5 //also called the write fault bit
    #define ATA_STATUS_BIT_SEEK_COMPLETE BIT4 //old/obsolete and unused. Old drives still set it for backwards compatability
    #define ATA_STATUS_BIT_SERVICE BIT4 //DMA Queued commands. Tagged command queuing AFAIK
    #define ATA_STATUS_BIT_DEFERRED_WRITE_ERROR BIT4 //write stream commands
    #define ATA_STATUS_BIT_DATA_REQUEST BIT3
    #define ATA_STATUS_BIT_CORRECTED_DATA BIT2 //old/obsolete
    #define ATA_STATUS_BIT_ALIGNMENT_ERROR BIT2
    #define ATA_STATUS_BIT_INDEX BIT1 //old/obsolete. flips state with each drive revolution
    #define ATA_STATUS_BIT_SENSE_DATA_AVAILABLE BIT1 //set when sense data is available for the command
    #define ATA_STATUS_BIT_ERROR BIT0 //an error occured

    #define ATA_ERROR_BIT_BAD_BLOCK BIT7 //old/obsolete
    #define ATA_ERROR_BIT_INTERFACE_CRC BIT7
    #define ATA_ERROR_BIT_UNCORRECTABLE_DATA BIT6
    #define ATA_ERROR_BIT_MEDIA_CHANGE BIT5
    #define ATA_ERROR_BIT_ID_NOT_FOUND BIT4
    #define ATA_ERROR_BIT_MEDIA_CHANGE_REQUEST BIT3
    #define ATA_ERROR_BIT_ABORT BIT2
    #define ATA_ERROR_BIT_TRACK_ZERO_NOT_FOUND BIT1 //old/obsolete
    #define ATA_ERROR_BIT_END_OF_MEDIA BIT1
    #define ATA_ERROR_BIT_ADDRESS_MARK_NOT_FOUND BIT0 //old/obsolete
    #define ATA_ERROR_BIT_COMMAND_COMPLETION_TIME_OUT BIT0 //streaming feature

    #define ATA_DL_MICROCODE_OFFSET_SAVE		(0x03)
    #define ATA_DL_MICROCODE_SAVE				(0x07)

    #define MAX_28BIT  (0xFFFFFFF)

    #define ATA_CHECKSUM_VALIDITY_INDICATOR 0xA5

    typedef enum _eDownloadMicrocodeFeatures
    {
        ATA_DL_MICROCODE_TEMPORARY_IMMEDIATE        = 0x01,//obsolete
        ATA_DL_MICROCODE_OFFSETS_SAVE_IMMEDIATE     = 0x03,
        ATA_DL_MICROCODE_SAVE_IMMEDIATE             = 0x07,
        ATA_DL_MICROCODE_OFFSETS_SAVE_FUTURE        = 0x0E,//deferred
        ATA_DL_MICROCODE_ACTIVATE                   = 0x0F
    }eDownloadMicrocodeFeatures;

    typedef enum _eSMART_features
    {
        ATA_SMART_READ_DATA         = 0xD0,
        ATA_SMART_RDATTR_THRESH     = 0xD1,
        ATA_SMART_SW_AUTOSAVE       = 0xD2,
        ATA_SMART_SAVE_ATTRVALUE    = 0xD3,
        ATA_SMART_EXEC_OFFLINE_IMM  = 0xD4,
        ATA_SMART_READ_LOG          = 0xD5,
        ATA_SMART_WRITE_LOG         = 0xD6,
        ATA_SMART_WRATTR_THRESH     = 0xD7,
        ATA_SMART_ENABLE            = 0xD8,
        ATA_SMART_DISABLE           = 0xD9,
        ATA_SMART_RTSMART           = 0xDA,
        ATA_SMART_AUTO_OFFLINE      = 0xDB, //NOTE: never officially adopted by ATA spec, but was part of original SMART spec by SFF committee (SFF-8035i).
        ATA_SMART_UNKNOWN
    }eSMART_features;

    #define ATA_SMART_SIG_MID               0x4F
    #define ATA_SMART_SIG_HI                0xC2

    #define ATA_SMART_BAD_SIG_MID               0xF4
    #define ATA_SMART_BAD_SIG_HI                0x2C

    #define ATA_SMART_READ_DATA_SIZE 512

    #define ATA_SMART_VERSION_OFFSET 0
    #define ATA_SMART_BEGIN_ATTRIBUTES 2 //also true for thresholds
    #define ATA_SMART_END_ATTRIBUTES 362 //also true for thresholds
    #define ATA_SMART_ATTRIBUTE_SIZE 12 //also true for thresholds
    #define ATA_SMART_OFFLINE_COLLECTION_STATUS_OFFSET 362
    #define ATA_SMART_SELF_TEST_EXECUTION_STATUS_OFFSET 363
    #define ATA_SMART_OFFLINE_DATA_COLLECTION_CAPABILITY_OFFSET 367
    #define ATA_SMART_SMART_CAPABILITY_OFFSET 368
    #define ATA_SMART_ERROR_LOGGING_CAPABILITY_OFFSET 370
    #define ATA_SMART_SHORT_TEST_POLLING_TIME_OFFSET 372
    #define ATA_SMART_EXTENDED_POLLING_TIME_BYTE_OFFSET 373 //if FFh, need to use word below
    #define ATA_SMART_CONVEYANCE_POLLING_TIME_OFFSET 374
    #define ATA_SMART_EXTENDEd_POLLING_TIME_WORD_OFFSET 375

    #define ATA_SMART_STATUS_FLAG_PREFAIL_ADVISORY BIT0
    #define ATA_SMART_STATUS_FLAG_ONLINE_DATA_COLLECTION BIT1
    #define ATA_SMART_STATUS_FLAG_PERFORMANCE BIT2
    #define ATA_SMART_STATUS_FLAG_ERROR_RATE BIT3
    #define ATA_SMART_STATUS_FLAG_EVENT_COUNT BIT4
    #define ATA_SMART_STATUS_FLAG_SELF_PRESERVING BIT5

    #define ATA_SMART_THRESHOLD_ALWAYS_PASSING 0x00
    #define ATA_SMART_THRESHOLD_MINIMUM 0x01
    #define ATA_SMART_THRESHOLD_MAXIMUM 0xFD
    #define ATA_SMART_THRESHOLD_INVALID 0xFE
    #define ATA_SMART_THRESHOLD_ALWAYS_FAILING 0xFF //Test code purposes only to test system or software's ability to detect SMART trips

    typedef enum _eATA_CMDS {
        ATA_NOP_CMD                     = 0x00,
        ATASET                          = 0x04,
		ATA_DATA_SET_MANAGEMENT_CMD     = 0x06,
        ATA_DATA_SET_MANAGEMENT_XL_CMD  = 0x07,
        ATAPI_RESET                     = 0x08,
        ATA_DEV_RESET                   = 0x08,
        ATA_REQUEST_SENSE_DATA          = 0x0B,
        ATA_RECALIBRATE                 = 0x10,
        ATA_GET_PHYSICAL_ELEMENT_STATUS = 0x12,
        ATA_READ_SECT                   = 0x20,
        ATA_READ_SECT_NORETRY           = 0x21,
        ATA_READ_LONG_RETRY             = 0x22,
        ATA_READ_LONG_NORETRY           = 0x23,
        ATA_READ_SECT_EXT               = 0x24,
        ATA_READ_DMA_EXT                = 0x25,
        ATA_READ_DMA_QUE_EXT            = 0x26,
        ATA_READ_MAX_ADDRESS_EXT        = 0x27,
        ATA_READ_READ_MULTIPLE_EXT      = 0x29,
        ATA_READ_STREAM_DMA_EXT         = 0x2A,
        ATA_READ_STREAM_EXT             = 0x2B,
        ATA_READ_LOG_EXT                = 0x2F,
        ATA_WRITE_SECT                  = 0x30,
        ATA_WRITE_SECT_NORETRY          = 0x31,
        ATA_WRITE_LONG_RETRY            = 0x32,
        ATA_WRITE_LONG_NORETRY          = 0x33,
        ATA_WRITE_SECT_EXT              = 0x34,
        ATA_WRITE_DMA_EXT               = 0x35,
        ATA_WRITE_DMA_QUE_EXT           = 0x36,
        ATA_SET_MAX_EXT                 = 0x37,
        ATA_WRITE_MULTIPLE_EXT          = 0x39,
        ATA_WRITE_STREAM_DMA_EXT        = 0x3A,
        ATA_WRITE_STREAM_EXT            = 0x3B,
        ATA_WRITE_SECTV_RETRY           = 0x3C,
        ATA_WRITE_DMA_FUA_EXT           = 0x3D,
        ATA_WRITE_DMA_QUE_FUA_EXT       = 0x3E,
        ATA_WRITE_LOG_EXT_CMD           = 0x3F,
        ATA_READ_VERIFY_RETRY           = 0x40,
        ATA_READ_VERIFY_NORETRY         = 0x41,
        ATA_READ_VERIFY_EXT             = 0x42,
        ATA_ZEROS_EXT                   = 0x44,
        ATA_WRITE_UNCORRECTABLE_EXT     = 0x45,
        ATA_READ_LOG_EXT_DMA            = 0x47,
        ATA_ZONE_MANAGEMENT_IN          = 0x4A,
        ATA_FORMAT_TRACK                = 0x50,
		ATA_CONFIGURE_STREAM            = 0x51,
        ATA_WRITE_LOG_EXT_DMA           = 0x57,
        ATA_TRUSTED_NON_DATA            = 0x5B,
        ATA_TRUSTED_RECEIVE             = 0x5C,
        ATA_TRUSTED_RECEIVE_DMA         = 0x5D,
        ATA_TRUSTED_SEND                = 0x5E,
        ATA_TRUSTED_SEND_DMA            = 0x5F,
        ATA_READ_FPDMA_QUEUED_CMD       = 0x60, // Added _CMD because FreeBSD had a symbol conflict
        ATA_WRITE_FPDMA_QUEUED_CMD      = 0x61,
        ATA_FPDMA_NON_DATA              = 0x63,
        ATA_SEND_FPDMA                  = 0x64,
        ATA_RECEIVE_FPDMA               = 0x65,
        ATA_SEEK_CMD                    = 0x70,
        ATA_SET_DATE_AND_TIME_EXT       = 0x77,
        ATA_ACCESSABLE_MAX_ADDR         = 0x78,
        ATA_REMOVE_AND_TRUNCATE         = 0x7C,
        ATA_EXEC_DRV_DIAG               = 0x90,
        ATA_INIT_DRV_PARAM              = 0x91,
        ATA_DLND_CODE                   = 0x92,
        ATA_DOWNLOAD_MICROCODE          = 0x92,
        ATA_DOWNLOAD_MICROCODE_DMA      = 0x93,
        ATA_ZONE_MANAGEMENT_OUT         = 0x9F,
        ATAPI_COMMAND                   = 0xA0,
        ATAPI_IDENTIFY                  = 0xA1,
        ATA_SMART                       = 0xB0,
        ATA_DCO                         = 0xB1,
        ATA_SET_SECTOR_CONFIG_EXT       = 0xB2,
        ATA_SANITIZE                    = 0xB4,
        ATA_NV_CACHE                    = 0xB6,
        ATA_READ_MULTIPLE               = 0xC4,
        ATA_WRITE_MULTIPLE              = 0xC5,
        ATA_SET_MULTIPLE                = 0xC6,
        ATA_READ_DMA_QUEUED_CMD         = 0xC7,
        ATA_READ_DMA_RETRY              = 0xC8,
        ATA_READ_DMA_NORETRY            = 0xC9,
        ATA_WRITE_DMA_RETRY             = 0xCA,
        ATA_WRITE_DMA_NORETRY           = 0xCB,
        ATA_WRITE_DMA_QUEUED_CMD        = 0xCC,
        ATA_WRITE_MULTIPLE_FUA_EXT      = 0xCE,
        ATA_GET_MEDIA_STATUS            = 0xDA,
        ATA_ACK_MEDIA_CHANGE            = 0xDB,
        ATA_POST_BOOT                   = 0xDC,
        ATA_PRE_BOOT                    = 0xDD,
        ATA_DOOR_LOCK                   = 0xDE,
        ATA_DOOR_UNLOCK                 = 0xDF,
        ATA_STANDBY_IMMD                = 0xE0,
        ATA_IDLE_IMMEDIATE_CMD          = 0xE1,
        ATA_STANDBY                     = 0xE2,
        ATA_IDLE                        = 0xE3,
        ATA_READ_BUF                    = 0xE4,
        ATA_CHECK_POWER_MODE            = 0xE5,
        ATA_SLEEP_CMD                   = 0xE6,
        ATA_FLUSH_CACHE                 = 0xE7,
        ATA_WRITE_BUF                   = 0xE8,
        ATA_READ_BUF_DMA                = 0xE9,
        ATA_LEGACY_WRITE_SAME           = 0xE9,
        ATA_FLUSH_CACHE_EXT             = 0xEA,
        ATA_WRITE_BUF_DMA               = 0xEB,
        ATA_IDENTIFY                    = 0xEC,
        ATA_MEDIA_EJECT                 = 0xED,
        ATA_IDENTIFY_DMA                = 0xEE,
        ATA_SET_FEATURE                 = 0xEF,
        ATA_SECURITY_SET_PASS           = 0xF1,
        ATA_SECURITY_UNLOCK_CMD         = 0xF2,
        ATA_SECURITY_ERASE_PREP         = 0xF3,
        ATA_SECURITY_ERASE_UNIT_CMD     = 0xF4,
        ATA_SECURITY_FREEZE_LOCK_CMD    = 0xF5,
        ATA_SECURITY_DISABLE_PASS       = 0xF6,
        ATA_LEGACY_TRUSTED_RECEIVE      = 0xF7,
        ATA_READ_MAX_ADDRESS            = 0xF8,
        ATA_SET_MAX                     = 0xF9,
        ATA_LEGACY_TRUSTED_SEND         = 0xFB,
        ATA_SEEK_EXT                    = 0xFC
    } eATA_CMDS;

    typedef enum _eAtaSCTActionCodes {
        SCT_RESERVED,
        SCT_READ_WRITE_LONG,//obsolete in newer standards
        SCT_WRITE_SAME,
        SCT_ERROR_RECOVERY_CONTROL,
        SCT_FEATURE_CONTROL,
        SCT_DATA_TABLES,
        SCT_VENDOR,
        SCT_RESERVED_FOR_SATA,
        //as new things are added to the ATA spec, add them from here
        UNKNOWN_ACTION
    }eAtaSCTActionCodes;

    typedef enum _eSCTRWLMode {
        SCT_RWL_RESERVED,
        SCT_RWL_READ_LONG,
        SCT_RWL_WRITE_LONG
    }eSCTRWLMode;

    typedef enum _eAtaCmdType {
        ATA_CMD_TYPE_UNKNOWN,
        ATA_CMD_TYPE_TASKFILE,
        ATA_CMD_TYPE_EXTENDED_TASKFILE,
        ATA_CMD_TYPE_NON_TASKFILE,
        ATA_CMD_TYPE_SOFT_RESET,
        ATA_CMD_TYPE_HARD_RESET,
        ATA_CMD_TYPE_PACKET
    } eAtaCmdType;

    typedef enum _eAtaProtocol {
        ATA_PROTOCOL_UNKNOWN,      // initial setting
        ATA_PROTOCOL_PIO,          // various, includes r/w
        ATA_PROTOCOL_DMA,          // various, includes r/w
        ATA_PROTOCOL_NO_DATA,      // various (e.g. NOP)
        ATA_PROTOCOL_DEV_RESET,    // device RESET
        ATA_PROTOCOL_DEV_DIAG,     // EXECUTE device DIAGNOSTIC
        ATA_PROTOCOL_DMA_QUE,      // Read/Write DMA Queued (tagged command queuing non-SATA)
        ATA_PROTOCOL_PACKET,       // PACKET
        ATA_PROTOCOL_PACKET_DMA,   // PACKET
        ATA_PROTOCOL_DMA_FPDMA,    // READ/WRITE FPDMA QUEUED
        ATA_PROTOCOL_SOFT_RESET,   // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_HARD_RESET,   // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_RET_INFO,     // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_UDMA,         // Needed for SAT spec. Table 101 (Protocol field)
        ATA_PROTOCOL_MAX_VALUE,    // Error check terminator
    } eAtaProtocol;
    

    typedef struct _ataTFRBlock 
    {
        uint8_t                CommandStatus;
        uint8_t                ErrorFeature;

        uint8_t                LbaLow;
        uint8_t                LbaMid;
        uint8_t                LbaHi;
        uint8_t                DeviceHead;

        uint8_t                LbaLow48;
        uint8_t                LbaMid48;
        uint8_t                LbaHi48;
        uint8_t                Feature48;

        uint8_t                SectorCount;
        uint8_t                SectorCount48;
        uint8_t                icc;
        uint8_t                DeviceControl;
        // Pad it out to 16 bytes
        uint8_t                aux1;
        uint8_t                aux2;
        uint8_t                aux3;
        uint8_t                aux4;
    } ataTFRBlock;

    //This is really for sending a SAT command so that it gets built properly.
    typedef enum _eATAPassthroughTransferBlocks
    {
        ATA_PT_512B_BLOCKS,
        ATA_PT_LOGICAL_SECTOR_SIZE,
        ATA_PT_NUMBER_OF_BYTES,
        ATA_PT_NO_DATA_TRANSFER
    }eATAPassthroughTransferBlocks;

    //This is really for sending a SAT command so that it gets built properly.
    typedef enum _eATAPassthroughLength
    {
        ATA_PT_LEN_NO_DATA,
        ATA_PT_LEN_FEATURES_REGISTER,//bits 7:0
        ATA_PT_LEN_SECTOR_COUNT,//bits 7:0
        ATA_PT_LEN_TPSIU
    }eATAPassthroughLength;

// \struct typedef struct _ataPassthroughCommand
    typedef struct _ataPassthroughCommand
    {
        eAtaCmdType                     commandType;
        eDataTransferDirection          commandDirection;
        eAtaProtocol                    commadProtocol;
        ataTFRBlock                     tfr;
        ataReturnTFRs                   rtfr; // Should we make it ataTFRBlock for easy copy?
        uint8_t                         *ptrData;
        uint32_t                        dataSize;
        uint8_t                         *ptrSenseData;
        uint8_t                         senseDataSize;
        uint32_t                        timeout;//timeout override for the lower layers. This is a time in seconds.
        eATAPassthroughTransferBlocks   ataTransferBlocks;//this is in here for building the SAT command to set the T_Type bits properly depending on the command we are issuing.
        eATAPassthroughLength           ataCommandLengthLocation;//this is in here for building the SAT command to set the T_Length field properly. This is important for asynchronous commands
        uint8_t                         multipleCount;//This is the exponent value specifying the number of sectors used in a read/write multiple command transfer. All other commands should leave this at zero. This ONLY matters on read/write multiple commands, if this is nonzero on any other command, it will fail. Only bits 0:2 are valid (SAT limitation)
        bool                            forceCheckConditionBit;//Set this to force setting the check condition bit on a command. This is here because by default,only non-data gets this bit due to some weird chipsets. This is an override that can be used in certain commands.
    } ataPassthroughCommand;

    //added these packs to make sure this structure gets interpreted correctly
    // in the code when I point it to a buffer and try and access it.
    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _ataSMARTAttribute
    {
        uint8_t     attributeNumber;
        uint16_t    status;//bit 0 = prefail warranty bit, bit 1 = online collection, bit 2 = performance, bit 3 = error rate, bit 4 = even counter, bit 5 = self preserving
        uint8_t     nominal;
        uint8_t     worstEver;
        uint8_t     rawData[7];//attribute and vendor specific
    #if !defined (__GNUC__)
    }ataSMARTAttribute;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) ataSMARTAttribute;
    #endif

    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _ataSMARTThreshold
    {
        uint8_t      attributeNumber;
        uint8_t      thresholdValue;
        uint8_t      reservedBytes[10];
    #if !defined (__GNUC__)
    }ataSMARTThreshold;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) ataSMARTThreshold;
    #endif

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
    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _ataLogDirectorySector
    {
        //  Log Address | Log Name                                  | Feature Set   | R/W   | Access 
        uint16_t LogDir;                     //  00          | Log directory                             | none          | RO    | GPL,SL
        uint16_t SummarySMARTErrLog;         //  01          | Summary SMART Error Log                   | SMART         | RO    | SL (a)
        uint16_t CompSMARTErrLog;            //  02          | Comprehensive SMART Error Log             | SMART         | RO    | SL (a)
        uint16_t ExtCompSMARTErrLog;         //  03          | Ext. Comprehensive SMART Error Log        | SMART         | RO    | GPL (b)
        uint16_t DeviceStatistics;           //  04          | Device Statistics                         | none          | RO    | GPL, SL
        uint16_t ReservedCFA1;               //  05          |                                           |               |       |   
        uint16_t SMARTSelfTestLog;           //  06          | SMART Self-Test Log                       | SMART         | RO    | SL (a)
        uint16_t ExtSMARTSelfTestLog;        //  07          | Ext. SMART Self-Test Log                  | SMART         | RO    | GLB (b)
        uint16_t PowerConditions;            //  08          | Power Conditions                          | EPC           | RO    | GPL (b)
        uint16_t SelectiveSelfTestLog;       //  09          | Selective Self-Test Log                   | SMART         | R/W   | SL (a)
        uint16_t DeviceStatNotification;     //  0A          | Device Statistics Notification            | DSN           | R/W   | GPL (b)
        uint16_t ReservedCFA2;               //  0B          |                                           |               |       |       
        uint16_t Reserved1;                  //  0C          |                                           |               |       |       
        uint16_t LPSMisAlignLog;             //  0D          | LPS Mis-alignment Log                     | LPS           | RO    | GPL,SL
        uint16_t Reserved2[2];               //  OE..0F      |                                           |               |       |
        uint16_t NCQCmdErrLog;               //  10          | NCQ Command Error Log                     | NCQ           | RO    | GPL (b)
        uint16_t SATAPhyEventCountLog;       //  11          | SATA Phy Event Counters Log               | none          | RO    | GPL (b)
        uint16_t SATANCQQueueManageLog;      //  12          | SATA NCQ Queue Management Log             | NCQ           | RO    | GPL (b)
        uint16_t SATANCQSendRecvLog;         //  13          | SATA NCQ Send & Receive Log               | NCQ           | RO    | GPL (b)
        uint16_t ReservedSATA[4];            //  14..17      | Reserved for Serial ATA                   |               |       |       
        uint16_t LBAStatus;                  //  18          | LBA Status                                | none          | RO    | GPL (b)
        uint16_t Reserved3[7];               //  19..20      | Reserved, 20h is Obsolete                 |               |       |           
        uint16_t WriteStreamErrLog;          //  21          | Write Stream Error Log                    | Streaming     | RO    | GPL (b)
        uint16_t ReadStreamErrLog;           //  22          | Read Stream Error Log                     | Streaming     | RO    | GPL (b)
        uint16_t Obsolete1;                  //  23          |                                           |               |       |       
        uint16_t CurrDevInternalStsDataLog;  //  24          | Current Device Internal Status Data Log   | none          | RO    | GPL (b)
        uint16_t SavedDevInternalStsDataLog; //  25          | Saved Device Internal Status Data Log     | none          | RO    | GPL (b)
        uint16_t Reserved4[10];              //  26..2F      |                                           |               |       |           
        uint16_t IdentifyDeviceData;         //  30          | IDENTIFY DEVICE data                      | none          | RO    | GPL, SL
        uint16_t Reserved5[79];              //  31..7F      |                                           |               |       |        
        uint16_t HostSpecific[32];           //  80..9F      | Host Specific                             | SMART         | R/W   | GPL, SL
        uint16_t DeviceVendorSpecific[64];   //  A0..DF      | Device Vendor Specific                    | SMART         | VS    | GPL, SL
        uint16_t SCTCmdSts;                  //  E0          | SCT Command / Status                      | SCT           | R/W   | GPL, SL
        uint16_t SCTDataXfer;                //  E1          | SCT Data Transfer                         | SCT           | R/W   | GPL, SL
        uint16_t Reserved6[30];              //  E2..FF      |                                           |               |       |           
    #if !defined (__GNUC__)
    }ataLogDirectorySector;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) ataLogDirectorySector;
    #endif

    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _ataPowerConditionsDescriptor
    {
        uint8_t reserved;
        uint8_t powerConditionFlags;
        uint16_t reserved2;
        uint32_t defaultTimerSetting;
        uint32_t savedTimerSetting;
        uint32_t currentTimerSetting;
        uint32_t nomincalRecoveryTimeToPM0;
        uint32_t minimumTimerSetting;
        uint32_t maximumTimerSetting;
        uint8_t reserved3[36];
    #if !defined (__GNUC__)
    }ataPowerConditionsDescriptor;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) ataPowerConditionsDescriptor;
    #endif

    typedef struct _ataSMARTValue {
        ataSMARTAttribute    data;
        bool                 valid;
        ataSMARTThreshold    thresholdData;
        bool                 thresholdDataValid;//new ATA specs no longer support the threshold sector so some drives may not report thresholds
        bool                 isWarrantied;
    } ataSMARTValue;

    typedef struct _ataSMARTLog 
    {
        ataSMARTValue    attributes[256];//attribute numbers 1 - 255 are valid (check valid bit to make sure it's a used attribute)
    } ataSMARTLog;

    typedef enum _eNVCacheFeatures {
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
    }eNVCacheFeatures;

    typedef enum _eDCOFeatures {
        DCO_RESTORE      = 0xC0,
        DCO_FREEZE_LOCK  = 0xC1,
        DCO_IDENTIFY     = 0xC2,
        DCO_SET          = 0xC3,
        DCO_IDENTIFY_DMA = 0xC4,
        DCO_SET_DMA      = 0xC5
    }eDCOFeatures;

    typedef enum _eEPCFeatureSet 
    {
        ENABLE_EPC_NOT_SET = -1,
        ENABLE_EPC = 0x04,
        DISABLE_EPC = 0x05,
    } eEPCFeatureSet;

    #define ATA_IDENTIFY_SANITIZE_INDEX         (59)
// If bit 12 of word 59 is set to one the device supports the Sanitize Device feature set.
    #define ATA_IDENTIFY_SANITIZE_SUPPORTED     (0x1000)
// If bit 13 of word 59 is set to one, then the device supports the
// Sanitize Device feature set CRYPTO SCRAMBLE EXT command
    #define ATA_IDENTIFY_CRYPTO_SUPPORTED       (0x2000)
// If bit 14 of word 59 is set to one, then the device supports the
// Sanitize Device feature set OVERWRITE EXT command
    #define ATA_IDENTIFY_OVERWRITE_SUPPORTED    (0x4000)
// If bit 15 of word 59 is set to one, then the device supports the
// Sanitize Device feature set BLOCK ERASE EXT command
    #define ATA_IDENTIFY_BLOCK_ERASE_SUPPORTED  (0x8000)

    #define ATA_SANITIZE_CRYPTO_LBA             (0x43727970)
    #define ATA_SANITIZE_BLOCK_ERASE_LBA        (0x426B4572)
    #define ATA_SANITIZE_OVERWRITE_LBA          (0x4F57)
    #define ATA_SANITIZE_FREEZE_LOCK_LBA        (0x46724C6B)
    #define ATA_SANITIZE_ANTI_FREEZE_LOCK_LBA   (0x416E7469)

    #define ATA_SANITIZE_CLEAR_OPR_FAILED       (0x01)
    #define ATA_SANITIZE_FAILURE_MODE_BIT_SET   (0x10)
    #define ATA_SANITIZE_INVERT_PAT_BIT_SET     (0x80)

   typedef enum _eATASanitizeFeature
   {
       ATA_SANITIZE_STATUS              = 0x0000,
       ATA_SANITIZE_CRYPTO_SCRAMBLE     = 0x0011,
       ATA_SANITIZE_BLOCK_ERASE         = 0x0012,
       ATA_SANITIZE_OVERWRITE_ERASE     = 0x0014,
       ATA_SANITIZE_FREEZE_LOCK         = 0x0020,
       ATA_SANITIZE_ANTI_FREEZE_LOCK    = 0x0040,
   }eATASanitizeFeature;


   //if the subcommand you are looking for is not in this enum, please add it
   typedef enum _eATASetFeaturesSubcommands
   {
       SF_ENABLE_VOLITILE_WRITE_CACHE                     = 0x02,
       SF_SET_TRANSFER_MODE                               = 0x03,
       SF_ENABLE_APM_FEATURE                              = 0x05,
       SF_ENABLE_PUIS_FEATURE                             = 0x06,
       SF_PUIS_DEVICE_SPIN_UP                             = 0x07,
       SF_ENABLE_WRITE_READ_VERIFY_FEATURE                = 0x0B,
       SF_ENABLE_SATA_FEATURE                             = 0x10,
       SF_ENABLE_FREE_FALL_CONTROL_FEATURE                = 0x41,
       SF_MAXIMUM_HOST_INTERFACE_SECTOR_TIMES             = 0x43,
       SF_EXTENDED_POWER_CONDITIONS                       = 0x4A,
       SF_DISABLE_READ_LOOK_AHEAD_FEATURE                 = 0x55,
       SF_LONG_PHYSICAL_SECTOR_ALIGNMENT_ERROR_REPORTING  = 0x62,
       SF_ENABLE_DISABLE_DSN_FEATURE                      = 0x63,
       SF_DISABLE_REVERTING_TO_POWERON_DEFAULTS           = 0x66,
       SF_DISABLE_VOLITILE_WRITE_CACHE                    = 0x82,
       SF_DISABLE_APM_FEATURE                             = 0x85,
       SF_DISABLE_PUIS_FEATURE                            = 0x86,
       SF_DISABLE_WRITE_READ_VERIFY_FEATURE               = 0x8B,
       SF_DISABLE_SATA_FEATURE                            = 0x90,
       SF_ENABLE_READ_LOOK_AHEAD_FEATURE                  = 0xAA,
       SF_DISABLE_FREE_FALL_CONTROL_FEATURE               = 0xC1,
       SF_ENABLE_DISABLE_SENSE_DATA_REPORTING_FEATURE     = 0xC3,
       SF_ENABLE_REVERTING_TO_POWER_ON_DEFAULTS           = 0xCC,
       SF_UNKNOWN_FEATURE
   } eATASetFeaturesSubcommands;

   typedef enum _eEPCSubcommands
   {
       EPC_RESTORE_POWER_CONDITION_SETTINGS = 0x0,
       EPC_GO_TO_POWER_CONDITION = 0x1,
       EPC_SET_POWER_CONDITION_TIMER = 0x2,
       EPC_SET_POWER_CONDITION_STATE = 0x3,
       EPC_ENABLE_EPC_FEATURE_SET = 0x4,
       EPC_DISABLE_EPC_FEATUER_SET = 0x5,
       EPC_SET_EPC_POWER_SOURCE = 0x6,
       EPC_RESERVED_FEATURE
   } eEPCSubcommands;

   typedef enum _eSCTWriteSameFunctions
   {
       WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD              = 0x0001,
       WRITE_SAME_BACKGROUND_USE_SINGLE_LOGICAL_SECTOR      = 0x0002,
       WRITE_SAME_BACKGROUND_USE_MULTIPLE_LOGICAL_SECTORS   = 0x0003,
       WRITE_SAME_FOREGROUND_USE_PATTERN_FIELD              = 0x0101,
       WRITE_SAME_FOREGROUND_USE_SINGLE_LOGICAL_SECTOR      = 0x0102,
       WRITE_SAME_FOREGROUND_USE_MULTIPLE_LOGICAL_SECTORS   = 0x0103,
   }eSCTWriteSameFunctions;

   typedef enum _eHPAFeature
   {
       HPA_SET_MAX_ADDRESS      = 0x00,
       HPA_SET_MAX_PASSWORD     = 0x01,
       HPA_SET_MAX_LOCK         = 0x02,
       HPA_SET_MAX_UNLOCK       = 0x03,
       HPA_SET_MAX_FREEZE_LOCK  = 0x04,
       HPA_SET_MAX_RESERVED
   }eHPAFeature;   

   typedef enum _eATALog
   {
       ATA_LOG_DIRECTORY                                = 0x00,
       ATA_LOG_SUMMARY_SMART_ERROR_LOG                  = 0x01,
       ATA_LOG_COMPREHENSIVE_SMART_ERROR_LOG            = 0x02,
       ATA_LOG_EXTENDED_COMPREHENSIVE_SMART_ERROR_LOG   = 0x03,
       ATA_LOG_DEVICE_STATISTICS                        = 0x04,
       ATA_LOG_SMART_SELF_TEST_LOG                      = 0x06,
       ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG             = 0x07,
       ATA_LOG_POWER_CONDITIONS                         = 0x08,
       ATA_LOG_SELECTIVE_SELF_TEST_LOG                  = 0x09,
       ATA_LOG_DEVICE_STATISTICS_NOTIFICATION           = 0x0A,
       ATA_LOG_PENDING_DEFECTS_LOG                      = 0x0C,
       ATA_LOG_LPS_MISALIGNMENT_LOG                     = 0x0D,
       ATA_LOG_NCQ_COMMAND_ERROR_LOG                    = 0x10,
       ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG              = 0x11,
       ATA_LOG_SATA_NCQ_QUEUE_MANAGEMENT_LOG            = 0x12,
       ATA_LOG_SATA_NCQ_SEND_AND_RECEIVE_LOG            = 0x13,
       ATA_LOG_HYBRID_INFORMATION                       = 0x14,
       ATA_LOG_LBA_STATUS                               = 0x19,
       ATA_LOG_WRITE_STREAM_ERROR_LOG                   = 0x21,
       ATA_LOG_READ_STREAM_ERROR_LOG                    = 0x22,
       ATA_LOG_DELAYED_LBA_LOG                          = 0x23,
       ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG  = 0x24,
       ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG    = 0x25,
       ATA_LOG_SECTOR_CONFIGURATION_LOG                 = 0x2F, //ACS4
       ATA_LOG_IDENTIFY_DEVICE_DATA                     = 0x30,
       ATA_SCT_COMMAND_STATUS                           = 0xE0,
       ATA_SCT_DATA_TRANSFER                            = 0xE1,
   }eATALog;

   typedef enum _eIdentifyDeviceDataLogPage //Log address 30h, ACS-4 Section 9.11
   {
       ATA_ID_DATA_LOG_SUPPORTED_PAGES = 0,
       ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA = 1,
       ATA_ID_DATA_LOG_CAPACITY = 2,
       ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES = 3,
       ATA_ID_DATA_LOG_CURRENT_SETTINGS = 4,
       ATA_ID_DATA_LOG_ATA_STRINGS = 5,
       ATA_ID_DATA_LOG_SECURITY = 6,
       ATA_ID_DATA_LOG_PARALLEL_ATA = 7,
       ATA_ID_DATA_LOG_SERIAL_ATA = 8,
       ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION = 9,
   }eIdentifyDeviceDataLogPage;

	//
   typedef enum _eDeviceStatisticsLog //Log Address 04h, ACS-4 Section 9.5
   {
       ATA_DEVICE_STATS_LOG_LIST, //00h
       ATA_DEVICE_STATS_LOG_GENERAL, //01h
       ATA_DEVICE_STATS_LOG_FREE_FALL, //02h
       ATA_DEVICE_STATS_LOG_ROTATING_MEDIA, //03h
       ATA_DEVICE_STATS_LOG_GEN_ERR, //04h
       ATA_DEVICE_STATS_LOG_TEMP, //05h
       ATA_DEVICE_STATS_LOG_TRANSPORT, //06h
       ATA_DEVICE_STATS_LOG_SSD //07h
       //Add more
   } eDeviceStatisticsLog;

   typedef enum _eSCTDeviceState
   {
       SCT_STATE_ACTIVE_WAITING_FOR_COMMAND                             = 0x00,
       SCT_STATE_STANDBY                                                = 0x01,
       SCT_STATE_SLEEP                                                  = 0x02,
       SCT_STATE_DST_PROCESSING_IN_BACKGROUND                           = 0x03,
       SCT_STATE_SMART_OFFLINE_DATA_COLLECTION_PROCESSING_IN_BACKGROUND = 0x04,
       SCT_STATE_SCT_COMMAND_PROCESSING_IN_BACKGROUND                   = 0x05,
   }eSCTDeviceState;

   typedef enum _eSCTExtendedStatus
   {
       SCT_EXT_STATUS_COMMAND_COMPLETE_NO_ERROR                                                                             = 0x0000,
       SCT_EXT_STATUS_INVALID_FUNCTION_CODE                                                                                 = 0x0001,
       SCT_EXT_STATUS_INPUT_LBA_OUT_OF_RANGE                                                                                = 0x0002,
       SCT_EXT_STATUS_REQUEST_512B_DATA_BLOCK_COUNT_OVERFLOW                                                                = 0x0003,
       SCT_EXT_STATUS_INVALID_FUNCTION_CODE_IN_SCT_ERROR_RECOVERY                                                           = 0x0004,
       SCT_EXT_STATUS_INVALID_SELECTION_CODE_IN_SCT_ERROR_RECOVERY                                                          = 0x0005,
       SCT_EXT_STATUS_HOST_READ_COMMAND_TIMER_IS_LESS_THAN_MINIMUM_VALUE                                                    = 0x0006,
       SCT_EXT_STATUS_HOST_WRITE_COMMAND_TIMER_IS_LESS_THAN_MINIMUM_VALUE                                                   = 0x0007,
       SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_AN_INTERRUPTING_HOST_COMMAND                       = 0x0008,
       SCT_EXT_STATUS_BACKGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_UNRECOVERABLE_ERROR                                = 0x0009,
       SCT_EXT_STATUS_OBSOLETE                                                                                              = 0x000A,
       SCT_EXT_STATUS_SCT_DATA_TRANSFER_COMMAND_WAS_ISSUES_WITHOUT_FIRST_ISSUING_AN_SCT_COMMAND                             = 0x000B,
       SCT_EXT_STATUS_INVALID_FUNCTION_CODE_IN_SCT_FEATURE_CONTROL_COMMAND                                                  = 0x000C,
       SCT_EXT_STATUS_INVALID_FEATURE_CODE_IN_SCT_FEATURE_CONTROL_COMMAND                                                   = 0x000D,
       SCT_EXT_STATUS_INVALID_STATE_VALUE_IN_SCT_FEATURE_CONTROL_COMMAND                                                    = 0x000E,
       SCT_EXT_STATUS_INVALID_OPTION_FLAGS_VALUE_IN_SCT_FEATURE_CONTROL_COMMAND                                             = 0x000F,
       SCT_EXT_STATUS_INVALID_SCT_ACTION_CODE                                                                               = 0x0010,
       SCT_EXT_STATUS_INVALID_TABLE_ID                                                                                      = 0x0011,
       SCT_EXT_STATUS_OPERATION_WAS_TERMINATED_DUE_TO_DEVICE_SECURITY_BEING_LOCKED                                          = 0x0012,
       SCT_EXT_STATUS_INVALID_REVISION_CODE_IN_SCT_DATA                                                                     = 0x0013,
       SCT_EXT_STATUS_FOREGROUND_SCT_OPERATION_WAS_TERMINATED_BECAUSE_OF_UNRECOVERABLE_ERROR                                = 0x0014,
       SCT_EXT_STATUS_MOST_RECENT_NON_SCT_COMMAND_COMPLETED_WITH_ERROR_DUE_TO_ERROR_RECOVERY_READ_OR_WRITE_TIMER_EXPIRING   = 0x0015,
       SCT_EXT_STATUS_SCT_COMMAND_PROCESSING_IN_BACKGROUND                                                                  = 0xFFFF
   }eSCTExtendedStatus;

    #if defined(__cplusplus)
} //extern "C"
    #endif
