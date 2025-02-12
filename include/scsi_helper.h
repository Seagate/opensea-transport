// SPDX-License-Identifier: MPL-2.0

//! \file scsi_helper.h
//! \brief Defines the constants structures to help with SCSI implementation
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "ata_helper.h"
#include "common_public.h"
#include "common_types.h"
#include "memory_safety.h"
#include "type_conversion.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define INQ_RETURN_DATA_LENGTH_SCSI2 (36)
#define INQ_RETURN_DATA_LENGTH       (96)
#define INQ_DATA_T10_VENDOR_ID_LEN   (8) // bytes
#define INQ_DATA_PRODUCT_ID_LEN      (16)
#define INQ_DATA_PRODUCT_REV_LEN     (4)

#define INQ_RESPONSE_FMT_SCSI        (0) // original response format - basically all vendor unique information
#define INQ_RESPONSE_FMT_CCS         (1) // SCSI common command set definition. More or less meets modern requirements
#define INQ_RESPONSE_FMT_CURRENT     (2) // SCSI2 and later all use this format to report inquiry data

#define INQ_MAX_VERSION_DESCRIPTORS  (8)

    typedef enum eSCSIVersionEnum
    {
        SCSI_VERSION_SASI        = 0,
        SCSI_VERSION_NO_STANDARD = 0,
        SCSI_VERSION_SCSI        = 1,
        SCSI_VERSION_SCSI2       = 2,
        SCSI_VERSION_SPC         = 3,
        SCSI_VERSION_SPC_2       = 4,
        SCSI_VERSION_SPC_3       = 5,
        SCSI_VERSION_SPC_4       = 6,
        SCSI_VERSION_SPC_5       = 7,
        // jump past 08-0C as these were used for asni, iso, emca combination value standard identification and are
        // obsolete.
        SCSI_VERSION_SPC_6 = 0x0D,
    } eSCSIVersion;

    typedef enum eCDBLenEnum
    {
        CDB_LEN_6  = 6,
        CDB_LEN_10 = 10,
        CDB_LEN_12 = 12,
        CDB_LEN_16 = 16,
        CDB_LEN_32 = 32,
        // NOTE: Variable length CDBs range between 12 and 260 bytes in length
        CDB_LEN_MAX = 260, // See SAM6. 260 is the defined maximum length.
        CDB_LEN_UNKNOWN
    } eCDBLen;

    // Defined in SPC standards. If a vendor uses an operation code for a vendor unique
    // purpose, all bets are off.
    static M_INLINE eCDBLen get_CDB_Len_From_Operation_Code(uint8_t operationCode)
    {
        eCDBLen len = CDB_LEN_UNKNOWN;
        if (operationCode <= 0x1F)
        {
            len = CDB_LEN_6;
        }
        else if (operationCode >= 0x20 && operationCode <= 0x5F)
        {
            len = CDB_LEN_10;
        }
        else if (operationCode >= 0x80 && operationCode <= 0x9F)
        {
            len = CDB_LEN_12;
        }
        else if (operationCode >= 0xA0 && operationCode <= 0xBF)
        {
            len = CDB_LEN_16;
        }
        // 60h - 7Dh are reserved
        // 7E and 7F are variable length but commonly 32B at this time. Can possible add a lookup based on service
        // action C0h - FFh are vendor unique.
        return len;
    }

    typedef enum eSenseFormatEnum
    {
        SCSI_SENSE_NO_SENSE_DATA   = 0,
        SCSI_SENSE_CUR_INFO_FIXED  = 0x70,
        SCSI_SENSE_DEFER_ERR_FIXED = 0x71,
        SCSI_SENSE_CUR_INFO_DESC   = 0x72,
        SCSI_SENSE_DEFER_ERR_DESC  = 0x73,
        SCSI_SENSE_VENDOR_SPECIFIC = 0x7F,
    } eSenseFormat;

    typedef enum eSenseKeyValuesEnum
    {
        SENSE_KEY_NO_ERROR        = 0x00,
        SENSE_KEY_RECOVERED_ERROR = 0x01,
        SENSE_KEY_NOT_READY       = 0x02,
        SENSE_KEY_MEDIUM_ERROR    = 0x03,
        SENSE_KEY_HARDWARE_ERROR  = 0x04,
        SENSE_KEY_ILLEGAL_REQUEST = 0x05,
        SENSE_KEY_UNIT_ATTENTION  = 0x06,
        SENSE_KEY_DATA_PROTECT    = 0x07,
        SENSE_KEY_BLANK_CHECK     = 0x08,
        SENSE_KEY_VENDOR_SPECIFIC = 0x09,
        SENSE_KEY_COPY_ABORTED    = 0x0A,
        SENSE_KEY_ABORTED_COMMAND = 0x0B,
        SENSE_KEY_EQUAL =
            0x0C, // scsi and scsi 2 defined this as "EQUAL" to say a "search data" command found an equal comparison.
        SENSE_KEY_RESERVED        = 0x0C, // marked obsolete in SPC3 and reserved in later standards
        SENSE_KEY_VOLUME_OVERFLOW = 0x0D,
        SENSE_KEY_MISCOMPARE      = 0x0E,
        SENSE_KEY_COMPLETED       = 0x0F
    } eSenseKeyValues;

    typedef enum eSenseDescriptorTypeEnum
    {
        SENSE_DESCRIPTOR_INFORMATION                        = 0x00,
        SENSE_DESCRIPTOR_COMMAND_SPECIFIC_INFORMATION       = 0x01,
        SENSE_DESCRIPTOR_SENSE_KEY_SPECIFIC                 = 0x02,
        SENSE_DESCRIPTOR_FIELD_REPLACEABLE_UNIT             = 0x03,
        SENSE_DESCRIPTOR_STREAM_COMMANDS                    = 0x04,
        SENSE_DESCRIPTOR_BLOCK_COMMANDS                     = 0x05,
        SENSE_DESCRIPTOR_OSD_OBJECT_IDENTIFICATION          = 0x06,
        SENSE_DESCRIPTOR_OSD_RESPONSE_INTEGRITY_CHECK_VALUE = 0x07,
        SENSE_DESCRIPTOR_OSD_ATTRIBUTE_IDENTIFICATION       = 0x08,
        SENSE_DESCRIPTOR_ATA_STATUS_RETURN                  = 0x09,
        SENSE_DESCRIPTOR_ANOTHER_PROGRESS_INDICATION        = 0x0A,
        SENSE_DESCRIPTOR_USER_DATA_SEGMENT_REFERRAL         = 0x0B,
        SENSE_DESCRIPTOR_FORWAREDED_SENSE_DATA              = 0x0C, // obsolete. Typo fixed in enum value below this one
        SENSE_DESCRIPTOR_FORWARDED_SENSE_DATA               = 0x0C,
        SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE         = 0x0D,
        SENSE_DESCRIPTOR_DEVICE_DESIGNATION                 = 0x0E,
        SENSE_DESCRIPTOR_MICROCODE_ACTIVATION               = 0x0F,
        // 0x10 - 0x7F are reserved
        // 0x80 - 0xFF are vendor specific
    } eSenseDescriptorType;

#define SCSI_SENSE_INFO_VALID_BIT_SET    (0x80)

#define SCSI_SENSE_ADDT_LEN_INDEX        (7)
#define SCSI_DESC_FORMAT_DESC_INDEX      (8)
#define SCSI_DESC_FORMAT_DESC_LEN        (9)
#define SCSI_SENSE_INFO_FIELD_MSB_INDEX  (3)
#define SCSI_FIXED_FORMAT_CMD_INFO_INDEX (8)

#define SCSI_MAX_21_LBA                  UINT32_C(0x001FFFFF) // read/write 6byte commands
#define SCSI_MAX_32_LBA                  UINT32_MAX
#define SCSI_MAX_64_LBA                  UINT64_MAX

    typedef enum eSenseKeySpecificTypeEnum
    {
        SENSE_KEY_SPECIFIC_UNKNOWN,
        SENSE_KEY_SPECIFIC_FIELD_POINTER,
        SENSE_KEY_SPECIFIC_ACTUAL_RETRY_COUNT,
        SENSE_KEY_SPECIFIC_PROGRESS_INDICATION,
        SENSE_KEY_SPECIFIC_SEGMENT_POINTER,
        SENSE_KEY_SPECIFIC_UNIT_ATTENTION_CONDITION_QUEUE_OVERFLOW
    } eSenseKeySpecificType;

    typedef struct s_senseKeySpecificFieldPointer
    {
        bool     cdbOrData; // true = cdb, false = data
        bool     bitPointerValid;
        uint8_t  bitPointer;
        uint16_t fieldPointer;
    } senseKeySpecificFieldPointer;

    typedef struct s_senseKeySpecificActualRetryCount
    {
        uint16_t actualRetryCount;
    } senseKeySpecificActualRetryCount;

    typedef struct s_senseKeySpecificProgressIndication
    {
        uint16_t progressIndication;
    } senseKeySpecificProgressIndication;

    typedef struct s_senseKeySpecificSegmentPointer
    {
        bool     segmentDescriptor;
        bool     bitPointerValid;
        uint8_t  bitPointer;
        uint16_t fieldPointer;
    } senseKeySpecificSegmentPointer;

    typedef struct s_senseKeySpecificUnitAttentionQueueOverflow
    {
        bool overflow;
    } senseKeySpecificUnitAttentionQueueOverflow;

    typedef struct s_senseKeySpecific
    {
        bool senseKeySpecificValid; // Will be set when the sense data contains sense key specific information
        eSenseKeySpecificType type; // use this to parse the correct structure from the union below.
        union
        {
            uint8_t                                    unknownDataType[3];
            senseKeySpecificFieldPointer               field;
            senseKeySpecificActualRetryCount           retryCount;
            senseKeySpecificProgressIndication         progress;
            senseKeySpecificSegmentPointer             segment;
            senseKeySpecificUnitAttentionQueueOverflow unitAttention;
        };
    } senseKeySpecific, *ptrSenseKeySpecific;

    // \struct scsiStatus
    // \param senseKey
    // \param acq
    // \param ascq
    typedef struct s_scsiStatus
    {
        uint8_t format;
        uint8_t senseKey;
        uint8_t asc;
        uint8_t ascq;
        uint8_t fru;
    } scsiStatus;

#define MAX_PROGRESS_INDICATION_DESCRIPTORS  UINT8_C(32)
#define MAX_FORWARDED_SENSE_DATA_DESCRIPTORS UINT8_C(2)

    typedef struct s_senseDataFields
    {
        bool validStructure; // Set to true if the rest of this structure was able to be parsed/filled in. This will
                             // only be false if we do not get 70h - 73h response codes
        bool fixedFormat; // This will tell you if some fields, like information and command-specific information, are
                          // limited to 32bits or not
        bool       deferredError;   // Set to true for response codes 71h & 73h
        scsiStatus scsiStatusCodes; // sense key, asc, ascq, fru
        bool senseDataOverflow; // gets set if the sense buffer is not big enough to return all necessary fields of the
                                // sense data. Request sense command is needed to get all the data.
        bool valid;             // valid bit. Used to know when the information field contains valid information
        bool filemark;          // filemark bit is set (stream commands)
        bool endOfMedia;        // end of media bit is set (stream commands)
        bool illegalLengthIndication; // illegal length indicator bit is set (stream commands or read/write long SBC
                                      // commands)
        union
        {
            uint32_t fixedInformation;
            uint64_t descriptorInformation;
        };
        union
        {
            uint32_t fixedCommandSpecificInformation;
            uint64_t descriptorCommandSpecificInformation;
        };
        senseKeySpecific senseKeySpecificInformation;
        // bools below can be used to know if other fields that are only available in some cases/commands are found.
        // If so, the caller can interpret these themselves.
        // We'll supply the offset in the sense data for them.
        // The offset myst be > 7 to be valid.
        uint8_t osdObjectIdentificationDescriptorOffset;
        uint8_t osdResponseIntegrityCheckValueDescriptorOffset;
        uint8_t osdAttributeIdentificationDescriptorOffset;
        struct s_ataStatusReturnDescriptor
        {
            bool    valid; // must be set for this data to be valid. Means we found this in the sense data.
            bool    extend;
            uint8_t error;
            uint8_t sectorCountExt;
            uint8_t sectorCount;
            uint8_t lbaLowExt;
            uint8_t lbaLow;
            uint8_t lbaMidExt;
            uint8_t lbaMid;
            uint8_t lbaHiExt;
            uint8_t lbaHi;
            uint8_t device;
            uint8_t status;
        } ataStatusReturnDescriptor;
        uint8_t anotherProgressIndicationDescriptorOffset[MAX_PROGRESS_INDICATION_DESCRIPTORS];
        uint8_t userDataSegmentReferralDescriptorOffset;
        uint8_t forwardedSenseDataDescriptorOffset[MAX_FORWARDED_SENSE_DATA_DESCRIPTORS];
        uint8_t deviceDesignationDescriptorOffset;
        struct s_microCodeActivation
        {
            bool     valid;
            uint16_t microcodeActivationTimeSeconds;
        } microCodeActivation;
        // This will be set to true for any descriptors that could not be parsed (vendor unique or not part of the above
        // output) or if the additional sense bytes field of fixed format is non-zero If this happens, the caller should
        // check the sense data buffer themselves for the additional data that they could find useful
        bool    additionalDataAvailable;
        uint8_t additionalDataOffset; // if bool above is set, then this will be set to the offset of the additional
                                      // data that couldn't be parsed
    } senseDataFields;

    typedef senseDataFields*       ptrSenseDataFields;
    typedef const senseDataFields* constPtrSenseDataFields;

    static M_INLINE void safe_free_sensefields(senseDataFields** sensefields)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, sensefields));
    }

    typedef struct s_biDirectionalCommandBuffers
    {
        uint8_t* dataOutBuffer;
        uint32_t dataOutBufferSize;
        uint8_t* dataInBuffer;
        uint32_t dataInBufferSize;
    } biDirectionalCommandBuffers;

// \struct ScsiIoCtx
// \param device file descriptor
// \param cdb SCSI Command block to send
// \param cdbLength length of the perticular cdb being sent
// \param direction is it XFER_DATA_IN (from the drive) XFER_DATA_OUT (to the device)
// \param pdata pointer to the user data to be read/written
// \param dataLength length of the data to be read/written
// \param psense
// \param senseDataSize
// \param timeout
// \param verbose will print some more debug info.
// \param returnStatus return status of the scsi
// \param rtfrs will contain the ATA return tfrs, when requested.
#define SCSI_IO_CTX_MAX_CDB_LEN 64
    typedef struct s_ScsiIoCtx
    {
        tDevice*               device;
        uint8_t                cdb[SCSI_IO_CTX_MAX_CDB_LEN]; // 64 just so if we ever get there.
        uint8_t                cdbLength;
        eDataTransferDirection direction;
        uint8_t*               pdata;
        uint32_t               dataLength;
        uint8_t*               psense;
        uint32_t   senseDataSize; // should be reduced to uint8 in the future as sense data maxes at 252Bytes
        uint32_t   timeout;       // seconds
        uint8_t    verbose;
        scsiStatus returnStatus;
        ataPassthroughCommand* pAtaCmdOpts;
        bool                   isSoftReset;
        bool                   isHardReset;
        bool                   fwdlFirstSegment;
        bool                   fwdlLastSegment;
    } ScsiIoCtx;

#define OPERATION_CODE                  (0)

#define SCSI_REQUEST_SENSE_DESC_BIT_SET (0x01)

#define SCSI_CTDP_BIT_SET               (0x02)

    typedef enum eScsiSanitizeFeatureEnum
    {
        SCSI_SANITIZE_OVERWRITE           = 0x01,
        SCSI_SANITIZE_BLOCK_ERASE         = 0x02,
        SCSI_SANITIZE_CRYPTOGRAPHIC_ERASE = 0x03,
        SCSI_SANITIZE_EXIT_FAILURE_MODE   = 0x1F
    } eScsiSanitizeFeature;

    typedef enum eScsiSanitizeOverwriteTestEnum
    {
        SANITIZE_OVERWRITE_NO_CHANGES = 0x00,
        SANITIZE_OVERWRITE_VENDOR1    = 0x01,
        SANITIZE_OVERWRITE_VENDOR2    = 0x02,
        SANITIZE_OVERWRITE_VENDOR3    = 0x03
    } eScsiSanitizeOverwriteTest;

    typedef enum eReadBufferModeEnum
    {
        SCSI_RB_COMBINED_HEADER_AND_DATA                                = 0x00, // obsolete (see SPC or SCSI2)
        SCSI_RB_VENDOR_SPECIFIC                                         = 0x01,
        SCSI_RB_DATA                                                    = 0x02,
        SCSI_RB_DESCRIPTOR                                              = 0x03,
        SCSI_RB_READ_DATA_FROM_ECHO_BUFFER                              = 0x0A,
        SCSI_RB_ECHO_BUFFER_DESCRIPTOR                                  = 0x0B,
        SCSI_RB_READ_MICROCODE_STATUS                                   = 0x0F,
        SCSI_RB_ENABLE_EXPANDER_COMMUNICATIONS_PROTOCOL_AND_ECHO_BUFFER = 0x1A, // obsolete (See SPC3)
        SCSI_RB_ERROR_HISTORY                                           = 0x1C,
    } eReadBufferMode;

    // Buffer ID's 0x00 - 0x03
    typedef enum eReadBufErrHistModeSpecificEnum
    {
        RD_BUF_ERR_HIST_MS_SNAPSHOT_VENDOR_SPECIFIC = 0x00,
        RD_BUF_ERR_HIST_MS_SNAPSHOT_INTERNAL_STATUS = 0x01
        // All others are reserved
    } eReadBufErrHistModeSpecific;

    typedef enum eWriteBufferModeEnum
    {
        SCSI_WB_COMBINED_HEADER_AND_DATA                                = 0x00, // obsolete (see SPC or SCSI2)
        SCSI_WB_VENDOR_SPECIFIC                                         = 0x01,
        SCSI_WB_DATA                                                    = 0x02,
        SCSI_WB_RESERVED                                                = 0x03,
        SCSI_WB_DL_MICROCODE_TEMP_ACTIVATE                              = 0x04,
        SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE                              = 0x05,
        SCSI_WB_DL_MICROCODE_OFFSETS_ACTIVATE                           = 0x06,
        SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_ACTIVATE                      = 0x07,
        SCSI_WB_WRITE_DATA_TO_ECHO_BUFFER                               = 0x0A,
        SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_SELECT_ACTIVATE_DEFER         = 0x0D,
        SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER                         = 0x0E,
        SCSI_WB_ACTIVATE_DEFERRED_MICROCODE                             = 0x0F,
        SCSI_WB_ENABLE_EXPANDER_COMMUNICATIONS_PROTOCOL_AND_ECHO_BUFFER = 0x1A, // obsolete (see SPC2)
        SCSI_WB_DISABLE_EXPANDER_COMMUNICATIONS_PROTOCOL                = 0x1B, // obsolete (see SPC2)
        SCSI_WB_DOWNLOAD_APPLICATION_CLIENT_ERROR_HISTORY               = 0x1C,
    } eWriteBufferMode;

#define SCSI_ERROR_HISTORY_HEADER_LEN    UINT32_C(32)
#define SCSI_ERROR_HISTORY_DIR_ENTRY_LEN UINT32_C(8)

    typedef enum eScsiErrorHistoryBufferIDsEnum
    {
        SCSI_ERR_HIS_BUF_DIRECTORY                             = 0x00,
        SCSI_ERR_HIS_BUF_DIRECTORY_CREATE_SNAPSHOT             = 0x01,
        SCSI_ERR_HIS_BUF_DIRECTORY_NEW_IT_NEXUS                = 0x02,
        SCSI_ERR_HIS_BUF_DIRECTORY_NEW_IT_NEXUS_CREAT_SNAPSHOT = 0x03,
        // All buffer ID's between 0x10 and 0xEF are vendor specific
        SCSI_ERR_HIS_BUF_CLEAR_ERR_HIST_IT_NEXUS                   = 0xFE,
        SCSI_ERR_HIS_BUF_CLEAR_ERR_HIST_IT_NEXUS_RELEASE_SNAPSHOTS = 0xFF
    } ScsiErrorHistoryBuffer;

    typedef enum ePersistentReserveInServiceActionsEnum
    {
        SCSI_PERSISTENT_RESERVE_IN_READ_KEYS           = 0, // SPC
        SCSI_PERSISTENT_RESERVE_IN_READ_RESERVATION    = 1, // SPC
        SCSI_PERSISTENT_RESERVE_IN_REPORT_CAPABILITIES = 2, // SPC3
        SCSI_PERSISTENT_RESERVE_IN_READ_FULL_STATUS    = 3, // SPC3
    } ePersistentReserveInServiceActions;

    typedef enum ePersistentReserveOutServiceActionsEnum
    {
        SCSI_PERSISTENT_RESERVE_OUT_REGISTER                         = 0, // SPC
        SCSI_PERSISTENT_RESERVE_OUT_RESERVE                          = 1, // SPC
        SCSI_PERSISTENT_RESERVE_OUT_RELEASE                          = 2, // SPC
        SCSI_PERSISTENT_RESERVE_OUT_CLEAR                            = 3, // SPC
        SCSI_PERSISTENT_RESERVE_OUT_PREEMPT                          = 4, // SPC
        SCSI_PERSISTENT_RESERVE_OUT_PREEMPT_AND_ABORT                = 5, // SPC
        SCSI_PERSISTENT_RESERVE_OUT_REGISTER_AND_IGNORE_EXISTING_KEY = 6, // SPC2
        SCSI_PERSISTENT_RESERVE_OUT_REGISTER_AND_MOVE                = 7, // SPC3
        SCSI_PERSISTENT_RESERVE_OUT_REPLACE_LOST_RESERVATION         = 8, // SPC4
    } ePersistentReserveOutServiceActions;

    // some of these commands have something like _CMD in the name or a missing underscore in order
    // to avoid conflict with a system header somewhere in linux or windows. - TJE
    typedef enum ScsiCmdsEnum
    {
        ACCESS_CONTROL_IN            = 0x86,
        ACCESS_CONTROL_OUT           = 0x87,
        ATA_PASS_THROUGH_12          = 0xA1,
        ATA_PASS_THROUGH_16          = 0x85,
        COMPARE_AND_WRITE            = 0x89,
        EXTENDED_COPY                = 0x83,
        SCSI_FORMAT_UNIT_CMD         = 0x04,
        SCSI_FORMAT_WITH_PRESET_CMD  = 0x38,
        CHANGE_ALIASES_CMD           = 0xA4,
        GET_LBA_STATUS               = 0x9E,
        INQUIRY_CMD                  = 0x12,
        LOG_SELECT_CMD               = 0x4C,
        LOG_SENSE_CMD                = 0x4D,
        MAINTENANCE_IN_CMD           = 0xA3,
        MAINTENANCE_OUT_CMD          = 0xA4,
        MODE_SELECT_6_CMD            = 0x15,
        MODE_SELECT10                = 0x55,
        MODE_SENSE_6_CMD             = 0x1A,
        MODE_SENSE10                 = 0x5A,
        ORWRITE_16                   = 0x8B,
        ORWRITE_32                   = 0x7F,
        PERSISTENT_RESERVE_IN_CMD    = 0x5E,
        PERSISTENT_RESERVE_OUT_CMD   = 0x5F,
        POPULATE_TOKEN               = 0x83,
        PRE_FETCH_10                 = 0x34,
        PRE_FETCH_16                 = 0x90,
        PREVENT_ALLOW_MEDIUM_REMOVAL = 0x1E,
        READ6                        = 0x08,
        READ10                       = 0x28,
        READ12                       = 0xA8,
        READ16                       = 0x88,
        READ32                       = 0x7F,
        READ_ATTRIBUTE               = 0x8C,
        READ_BUFFER_CMD              = 0x3C,
        READ_BUFFER_16_CMD           = 0x9B,
        READ_CAPACITY_10             = 0x25,
        READ_CAPACITY_16             = 0x9E,
        READ_DEFECT_DATA_10_CMD      = 0x37,
        READ_DEFECT_DATA_12_CMD      = 0xB7,
        READ_LONG_10                 = 0x3E,
        READ_LONG_16                 = 0x9E,
        READ_MEDIA_SERIAL_NUMBER     = 0xAB,
        REASSIGN_BLOCKS_6 =
            0x07, // I added the _6 to the name because it conflicts with a windows structure definition - TJE
        RECEIVE_COPY_RESULTS                   = 0x84,
        RECEIVE_DIAGNOSTIC_RESULTS             = 0x1C,
        RECEIVE_ROD_TOKEN_INFORMATION          = 0x84,
        REDUNDANCY_GROUP_IN                    = 0xBA,
        REDUNDANCY_GROUP_OUT                   = 0xBB,
        REMOVE_I_T_NEXUS                       = 0xA4,
        REPORT_REFERRALS                       = 0x9E,
        REPORT_ALIASES_CMD                     = 0xA3,
        REPORT_IDENTIFYING_INFORMATION         = 0xA3,
        REPORT_LUNS_CMD                        = 0xA0,
        REPORT_PRIORITY_CMD                    = 0xA3,
        REPORT_SUPPORTED_OPERATION_CODES_CMD   = 0xA3,
        REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCS = 0xA3,
        REPORT_TARGET_PORT_GROUPS_CMD          = 0xA3,
        REQUEST_SENSE_CMD                      = 0x03,
        REZERO_UNIT_CMD                        = 0x01,
        SANITIZE_CMD                           = 0x48,
        SECURITY_PROTOCOL_IN                   = 0xA2,
        SECURITY_PROTOCOL_OUT                  = 0xB5,
        SEND_DIAGNOSTIC_CMD                    = 0x1D,
        SET_IDENTIFYING_INFORMATION            = 0xA4,
        SET_PRIORITY_CMD                       = 0xA4,
        SET_TARGET_PORT_GROUPS_CMD             = 0xA4,
        SET_TIMESTAMP_CMD                      = 0xA4,
        SPARE_IN                               = 0xBC,
        SPARE_OUT                              = 0xBD,
        START_STOP_UNIT_CMD                    = 0x1B,
        SYNCHRONIZE_CACHE_10                   = 0x35,
        SYNCHRONIZE_CACHE_16_CMD               = 0x91,
        TEST_UNIT_READY_CMD                    = 0x00,
        UNMAP_CMD                              = 0x42,
        VERIFY10                               = 0x2F,
        VERIFY12                               = 0xAF,
        VERIFY16                               = 0x8F,
        VERIFY32                               = 0x7F,
        VOLUME_SET_IN                          = 0xBE,
        VOLUME_SET_OUT                         = 0xBF,
        WRITE6                                 = 0x0A,
        WRITE10                                = 0x2A,
        WRITE12                                = 0xAA,
        WRITE16                                = 0x8A,
        WRITE32                                = 0x7F,
        WRITE_AND_VERIFY_10                    = 0x2E,
        WRITE_AND_VERIFY_12                    = 0xAE,
        WRITE_AND_VERIFY_16                    = 0x8E,
        WRITE_AND_VERIFY_32                    = 0x7F,
        WRITE_ATTRIBUTE_CMD                    = 0x8D,
        WRITE_BUFFER_CMD                       = 0x3B,
        WRITE_LONG_10_CMD                      = 0x3F,
        WRITE_LONG_16_CMD                      = 0x9F,
        WRITE_SAME_10_CMD                      = 0x41,
        WRITE_SAME_16_CMD                      = 0x93,
        WRITE_SAME_32_CMD                      = 0x7F,
        WRITE_USING_TOKEN                      = 0x83,
        XDWRITEREAD_10                         = 0x53,
        XDWRITEREAD_32                         = 0x7F,
        XPWRITE_10                             = 0x51,
        XPWRITE_32                             = 0x7F,
        ZONE_MANAGEMENT_OUT                    = 0x94,
        ZONE_MANAGEMENT_IN                     = 0x95,
    } eScsiCmds;

    typedef enum eScsiVpdPagesEnum
    {
        SUPPORTED_VPD_PAGES = 0x00,
        STANDARD_INQUIRY    = 0x00,
        // 01h - 7Fh - ASCII Information
        UNIT_SERIAL_NUMBER = 0x80,
        IMPLEMENTED_OPERATING_DEFINITIONS =
            0x81, // Obsolete. Used to tell what can be sent with the change definition command (scsi 2 vs scsi 3, etc)
        ASCII_IMPLEMENTED_OPERATING_DEFINITION       = 0x82, // Obsolete
        DEVICE_IDENTIFICATION                        = 0x83,
        SOFTWARE_INTERFACE_IDENTIFICATION            = 0x84,
        MANAGEMENT_NETWORK_ADDRESSES                 = 0x85,
        EXTENDED_INQUIRY_DATA                        = 0x86,
        MODE_PAGE_POLICY                             = 0x87,
        SCSI_PORTS                                   = 0x88,
        ATA_INFORMATION                              = 0x89,
        POWER_CONDITION                              = 0x8A,
        DEVICE_CONSTITUENTS                          = 0x8B,
        CFA_PROFILE_INFORMATION                      = 0x8C,
        POWER_CONSUMPTION                            = 0x8D,
        THIRD_PARTY_COPY                             = 0x8F,
        PROTOCOL_SPECIFIC_LU_INFO                    = 0x90,
        PROTOCOL_SPECIFIC_PORT_INFO                  = 0x91,
        SCSI_FEATURE_SETS                            = 0x92,
        BLOCK_LIMITS                                 = 0xB0,
        BLOCK_DEVICE_CHARACTERISTICS                 = 0xB1,
        LOGICAL_BLOCK_PROVISIONING                   = 0xB2,
        REFERALS                                     = 0xB3,
        SUPPORTED_BLOCK_LENGTHS_AND_PROTECTION_TYPES = 0xB4,
        BLOCK_DEVICE_CHARACTERISTISCS_EXT            = 0xB5,
        ZONED_BLOCK_DEVICE_CHARACTERISTICS           = 0xB6,
        BLOCK_LIMITS_EXTENSION                       = 0xB7,
        FORMAT_PRESETS                               = 0xB8,
        CONCURRENT_POSITIONING_RANGES                = 0xB9,
        CAPACITY_PRODUCT_IDENTIFICATION_MAPPING      = 0xBA,
        // C0h - FFh are Vendor specific
    } eScsiVpdPages;

    // these enums are only for VPD pages with fixed lengths..add onto this as we need more things in here
    typedef enum eScsiVPDPageLengthsEnum
    {
        VPD_EXTENDED_INQUIRY_LEN             = 64,
        VPD_POWER_CONDITION_LEN              = 18,
        VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN = 64,
        VPD_ATA_INFORMATION_LEN              = 572,
        VPD_BLOCK_LIMITS_LEN                 = 64, // length if from SBC4
        VPD_LOGICAL_BLOCK_PROVISIONING_LEN =
            8, // This is only a correct length if there are no provisioning group descriptors
        VPD_ZONED_BLOCK_DEVICE_CHARACTERISTICS_LEN = 64, // ZBC-2
    } ScsiVPDPageLengths;

    typedef enum eScsiModePageControlEnum
    {
        MPC_CURRENT_VALUES   = 0x0,
        MPC_CHANGABLE_VALUES = 0x1,
        MPC_DEFAULT_VALUES   = 0x2,
        MPC_SAVED_VALUES     = 0x3
    } eScsiModePageControl;

    typedef enum eScsiLogPageControlEnum
    {
        LPC_THRESHOLD_VALUES          = 0x0,
        LPC_CUMULATIVE_VALUES         = 0x1,
        LPC_DEFAULT_THRESHOLD_VALUES  = 0x2,
        LPC_DEFAULT_CUMULATIVE_VALUES = 0x3,
        LPC_DEFAULT_ALL_VALUES        = 0x4,
    } eScsiLogPageControl;

    typedef enum eScsiModeParametersEnum // does not do subpage codes...only page codes. Add more as needed
    {
        MP_READ_WRITE_ERROR_RECOVERY      = 0x01,
        MP_DISCONNECT_RECONNECT           = 0x02,
        MP_RIGID_DISK_GEOMETRY            = 0x04, // This is long obsolete.
        MP_FLEXIBLE_DISK_GEOMETRY         = 0x05, // Long obsolete
        MP_VERIFY_ERROR_RECOVERY          = 0x07,
        MP_CACHING                        = 0x08,
        MP_PERIPHERAL_DEVICE              = 0x09, // Obsolete
        MP_CONTROL                        = 0x0A,
        MP_MEDIUM_TYPES_SUPPORTED         = 0x0B, // Obsolete
        MP_NOTCH_AND_PARTITION            = 0x0C, // Obsolete
        MP_OBS_POWER_CONDITION            = 0x0D, // Obsolete page. Named different than power condition page below.
        MP_XOR_CONTROL                    = 0x10, // Obsolete
        MP_ENCLOSURE_SERVICES_MANAGEMENT  = 0x14,
        MP_EXTENDED                       = 0x15,
        MP_EXTENDED_DEVICE_TYPE_SPECIFIC  = 0x16,
        MP_PROTOCOL_SPECIFIC_LOGICAL_UNIT = 0x18,
        MP_PROTOCOL_SPECIFIC_PORT         = 0x19,
        MP_POWER_CONDTION                 = 0x1A,
        MP_POWER_CONSUMPTION              = 0x1A,
        MP_BACKGROUND_CONTROL             = 0x1C,
        MP_INFORMATION_EXCEPTIONS_CONTROL = 0x1C,
        MP_LOGICAL_BLOCK_PROVISIONING     = 0x1C,
        MP_RETURN_ALL_PAGES               = 0x3F,
        MP_UNKNOWN_PARAMETER
    } eScsiModeParameters;

// This is for returning all subpages of a specified mode page
#define MP_SP_ALL_SUBPAGES 0xFF

    // put defined lengths in here....not all mode pages/parameters have defined lengths so only the ones that do are in
    // here This is TOTAL length, not necessarily the length field in the page
    typedef enum eScsiModeParameterLengthsEnum
    {
        MP_CONTROL_LEN                   = 12,
        MP_CONTROL_EXTENSION_LEN         = 32,
        MP_DISCONNECT_RECONNECT_LEN      = 16,
        MP_POWER_CONDITION_LEN           = 40,
        MP_POWER_CONSUMPTION_LEN         = 16,
        MP_BACKGROUND_CONTROL_LEN        = 16,
        MP_CACHING_LEN                   = 20,
        MP_INFORMATION_EXCEPTIONS_LEN    = 12,
        MP_READ_WRITE_ERROR_RECOVERY_LEN = 12,
        MP_VERIFY_ERROR_RECOVERY_LEN     = 12,
        MP_RIGID_DISK_GEOMETRY_LEN       = 24,
    } eScsiModeParameterLengths;

    typedef enum eScsiPowerConditionValuesEnum
    {
        PC_START_VALID     = 0x0, // process START and LOEJ bits
        PC_ACTIVE          = 0x1,
        PC_IDLE            = 0x2,
        PC_STANDBY         = 0x3,
        PC_SLEEP           = 0x5, // Obsolete since SBC2. Requires a reset to wake
        PC_LU_CONTROL      = 0x7,
        PC_FORCE_IDLE_0    = 0xA,
        PC_FORCE_STANDBY_0 = 0xB,
        PC_RESERVED_VALUE
    } eScsiPowerConditionValues;

    typedef enum eScsiLogPagesEnum // these are all the pages. Some may need a specific subpage to be used to access
                                   // them
    {
        LP_SUPPORTED_LOG_PAGES                            = 0x00,
        LP_SUPPORTED_LOG_PAGES_AND_SUBPAGES               = 0x00,
        LP_BUFFER_OVERRUN_UNDERRUN                        = 0x01,
        LP_WRITE_ERROR_COUNTERS                           = 0x02,
        LP_READ_ERROR_COUNTERS                            = 0x03,
        LP_READ_REVERSE_ERROR_COUNTERS                    = 0x04,
        LP_VERIFY_ERROR_COUNTERS                          = 0x05,
        LP_NON_MEDIUM_ERROR                               = 0x06,
        LP_LAST_N_ERROR_EVENTS                            = 0x07,
        LP_FORMAT_STATUS_LOG_PAGE                         = 0x08,
        LP_LAST_N_DEFFERRED_ERRORS_OR_ASYNCHRONOUS_EVENTS = 0x0B,
        LP_LOGICAL_BLOCK_PROVISIONING                     = 0x0C,
        LP_TEMPERATURE                                    = 0x0D,
        LP_ENVIRONMENTAL_LIMITS                           = 0x0D,
        LP_ENVIRONMENTAL_REPORTING                        = 0x0D,
        LP_START_STOP_CYCLE_COUNTER                       = 0x0E,
        LP_UTILIZATION                                    = 0x0E,
        LP_APPLICATION_CLIENT                             = 0x0F,
        LP_SELF_TEST_RESULTS                              = 0x10,
        LP_SOLID_STATE_MEDIA                              = 0x11,
        LP_ZONED_DEVICE_STATISTICS                        = 0x14, // subpage 01
        LP_BACKGROUND_SCAN_RESULTS                        = 0x15,
        LP_PENDING_DEFECTS                                = 0x15,
        LP_LPS_MISALLIGNMENT                              = 0x15,
        LP_ATA_PASSTHROUGH_RESULTS                        = 0x16,
        LP_NON_VOLITILE_CACHE                             = 0x17,
        LP_PROTOCOL_SPECIFIC_PORT                         = 0x18,
        LP_CACHE_MEMORY_STATISTICS                        = 0x19,
        LP_GENERAL_STATISTICS_AND_PERFORMANCE             = 0x19,
        LP_GROUP_STATISTICS_AND_PERFORMANCE               = 0x19,
        LP_POWER_CONDITIONS_TRANSITIONS                   = 0x1A,
        LP_INFORMATION_EXCEPTIONS                         = 0x2F,
    } eScsiLogPages;

    typedef enum eScsiLogPageLengthsEnum // only lengths that are specified are in here. Some pages may not have a set
                                         // length
    {
        LP_SELF_TEST_RESULTS_LEN = 404,
        LP_TEMPERATURE_LEN       = 16, // this is only set to this size because there are currently only 2 temperature
                                       // parameters defined in the spec and this size takes that into account
        LP_SOLID_STATE_MEDIA_LEN =
            12, // this is only set to this size since only 1 parameter code is currently defined in the SBC spec
        LP_INFORMATION_EXCEPTIONS_LEN = 12, // setting to 12 since a SAT device will only likely return 12 bytes of
                                            // data....this shouldn't be used when reading this page from a SAS device.
    } eScsiLogPageLengths;

#define MODE_PARAMETER_HEADER_6_LEN       UINT8_C(4)
#define MODE_PARAMETER_HEADER_10_LEN      UINT8_C(8)

#define MODE_HEADER_6_MP_LEN_OFFSET       (0)
#define MODE_HEADER_6_MEDIUM_TYPE_OFFSET  (1)
#define MODE_HEADER_6_DEV_SPECIFIC        (2)
#define MODE_HEADER_6_BLK_DESC_OFFSET     (3)

#define MODE_HEADER_10_MP_LEN_OFFSET      (0)
#define MODE_HEADER_10_MEDIUM_TYPE_OFFSET (2)
#define MODE_HEADER_10_DEV_SPECIFIC       (3)
#define MODE_HEADER_10_BLK_DESC_OFFSET    (6)

#define GENERAL_BLOCK_DESCRIPTOR_LEN                                                                                   \
    UINT8_C(8) // for all devices without longLBA set and NOT direct access block devices
#define GEN_BLK_DESC_DENSITY_CODE_OFFSET         (0)
#define GEN_BLK_DESC_NUM_BLOCKS_MSB_OFFSET       (1)
#define GEN_BLK_DESC_NUM_BLOCKS_LSB_OFFSET       (3)
#define GEN_BLK_DESC_BLOCK_LEN_MSB_OFFSET        (5)
#define GEN_BLK_DESC_BLOCK_LEN_LSB_OFFSET        (7)

#define SHORT_LBA_BLOCK_DESCRIPTOR_LEN           UINT8_C(8) // for mode sense/select 6
#define SHORT_LBA_BLK_DESC_NUM_BLOCKS_MSB_OFFSET (0)
#define SHORT_LBA_BLK_DESC_NUM_BLOCKS_LSB_OFFSET (3)
#define SHORT_LBA_BLK_DESC_BLOCK_LEN_MSB_OFFSET  (5)
#define SHORT_LBA_BLK_DESC_BLOCK_LEN_LSB_OFFSET  (7)

#define LONG_LBA_BLOCK_DESCRIPTOR_LEN            UINT8_C(16) // for mode sense/select 10
#define LONG_LBA_BLK_DESC_NUM_BLOCKS_MSB_OFFSET  (0)
#define LONG_LBA_BLK_DESC_NUM_BLOCKS_LSB_OFFSET  (7)
#define LONG_LBA_BLK_DESC_BLOCK_LEN_MSB_OFFSET   (12)
#define LONG_LBA_BLK_DESC_BLOCK_LEN_LSB_OFFSET   (15)

#define LOG_PAGE_HEADER_LENGTH                   UINT8_C(4)

#define READ_CAPACITY_10_LEN                     UINT8_C(8)
#define READ_CAPACITY_16_LEN                     UINT8_C(32)

    typedef enum eSCSIPeripheralQualifierEnum
    {
        PERIPHERAL_QUALIFIER_ACCESSIBLE_TO_TASK_ROUTER                 = 0x00,
        PERIPHERAL_QUALIFIER_NOT_ACCESSIBLE_TO_TASK_ROUTER_BUT_CAPABLE = 0x01,
        PERIPHERAL_QUALIFIER_RESERVED_2                                = 0x02,
        PERIPHERAL_QUALIFIER_NOT_ACCESSIBLE_TO_TASK_ROUTER             = 0x03,
        PERIPHERAL_QUALIFIER_RESERVED_4                                = 0x04,
        PERIPHERAL_QUALIFIER_RESERVED_5                                = 0x05,
        PERIPHERAL_QUALIFIER_RESERVED_6                                = 0x06,
        PERIPHERAL_QUALIFIER_RESERVED_7                                = 0x07
    } eSCSIPeripheralQualifier;

    typedef enum eSCSIPeripheralDeviceTypeEnum
    {
        PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE        = 0x00,
        PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE    = 0x01,
        PERIPHERAL_PRINTER_DEVICE                    = 0x02,
        PERIPHERAL_PROCESSOR_DEVICE                  = 0x03,
        PERIPHERAL_WRITE_ONCE_DEVICE                 = 0x04,
        PERIPHERAL_CD_DVD_DEVICE                     = 0x05,
        PERIPHERAL_SCANNER_DEVICE                    = 0x06,
        PERIPHERAL_OPTICAL_MEMORY_DEVICE             = 0x07,
        PERIPHERAL_MEDIUM_CHANGER_DEVICE             = 0x08,
        PERIPHERAL_COMMUNICATIONS_DEVICE             = 0x09,
        PERIPHERAL_ASC_IT8_1                         = 0x0A, // ASC IT8 (Graphic arts pre-press devices)
        PERIPHERAL_ASC_IT8_2                         = 0x0B, // ASC IT8 (Graphic arts pre-press devices)
        PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE   = 0x0C,
        PERIPHERAL_ENCLOSURE_SERVICES_DEVICE         = 0x0D,
        PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE   = 0x0E,
        PERIPHERAL_OPTICAL_CARD_READER_WRITER_DEVICE = 0x0F,
        PERIPHERAL_BRIDGE_CONTROLLER_COMMANDS        = 0x10,
        PERIPHERAL_OBJECT_BASED_STORAGE_DEVICE       = 0x11,
        PERIPHERAL_AUTOMATION_DRIVE_INTERFACE        = 0x12,
        PERIPHERAL_SECURITY_MANAGER_DEVICE           = 0x13,
        PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE   = 0x14,
        PERIPHERAL_RESERVED3                         = 0x15,
        PERIPHERAL_RESERVED4                         = 0x16,
        PERIPHERAL_RESERVED5                         = 0x17,
        PERIPHERAL_RESERVED6                         = 0x18,
        PERIPHERAL_RESERVED7                         = 0x19,
        PERIPHERAL_RESERVED8                         = 0x1A,
        PERIPHERAL_RESERVED9                         = 0x1B,
        PERIPHERAL_RESERVED10                        = 0x1C,
        PERIPHERAL_RESERVED11                        = 0x1D,
        PERIPHERAL_WELL_KNOWN_LOGICAL_UNIT           = 0x1E,
        PERIPHERAL_UNKNOWN_OR_NO_DEVICE_TYPE         = 0x1F
    } eSCSIPeripheralDeviceType;

    typedef enum eSCSIAddressDescriptorsEnum
    {
        AD_SHORT_BLOCK_FORMAT_ADDRESS_DESCRIPTOR               = 0x00,
        AD_EXTENDED_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR = 0x01,
        AD_EXTENDED_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR  = 0x02,
        AD_LONG_BLOCK_FORMAT_ADDRESS_DESCRIPTOR                = 0x03,
        AD_BYTES_FROM_INDEX_FORMAT_ADDRESS_DESCRIPTOR          = 0x04,
        AD_PHYSICAL_SECTOR_FORMAT_ADDRESS_DESCRIPTOR           = 0x05,
        AD_VENDOR_SPECIFIC                                     = 0x06,
        AD_RESERVED                                            = 0x07
    } eSCSIAddressDescriptors;

    // for the report supported operations command
    typedef enum eSCSIReportingOptionsEnum
    {
        REPORT_ALL                                                  = 0x0,
        REPORT_OPERATION_CODE                                       = 0x1,
        REPORT_OPERATION_CODE_AND_SERVICE_ACTION                    = 0x2,
        REPORT_OPERATION_CODE_AND_SERVICE_ACTION_ONE_COMMAND_FORMAT = 0x3,
    } eSCSIReportingOptions;

    // for SCSI send/receive diagnostics commands
    typedef enum eSCSIDiagnosticPagesEnum
    {
        DIAG_PAGE_SUPPORTED_PAGES = 0x00,
        // SES - 01h - 2Fh
        DIAG_PAGE_CONFIGURATION                        = 0x01,
        DIAG_PAGE_ENCLOSURE_CONTROL_STATUS             = 0x02,
        DIAG_PAGE_HELP_TEXT                            = 0x03,
        DIAG_PAGE_STRING                               = 0x04,
        DIAG_PAGE_THRESHOLD                            = 0x05,
        DIAG_PAGE_ARRAY_CONTROL                        = 0x06, // obsolete since SES2
        DIAG_PAGE_ELEMENT_DESCRIPTOR                   = 0x07,
        DIAG_PAGE_SHORT_ENCLOSURE_STATUS               = 0x08,
        DIAG_PAGE_ENCLOSURE_BUSY                       = 0x09,
        DIAG_PAGE_ADDITIONAL_ELEMENT_STATUS            = 0x0A,
        DIAG_PAGE_SUBENCLOSURE_HELP_TEXT               = 0x0B,
        DIAG_PAGE_SUBENCLOSURE_STRING                  = 0x0C,
        DIAG_PAGE_SUPPORTED_SES_DIAGNOSTIC_PAGES       = 0x0D,
        DIAG_PAGE_DOWNLOAD_MICROCODE_CONTROL_STATUS    = 0x0E,
        DIAG_PAGE_SUBENCLOSURE_NICKNAME_CONTROL_STATUS = 0x0F,
        // SPC 30h - 3F
        DIAG_PAGE_PROTOCOL_SPECIFIC = 0x3F,
        // SBC 40h - 7Fh
        DIAG_PAGE_TRANSLATE_ADDRESS = 0x40,
        DIAG_PAGE_DEVICE_STATUS     = 0x41, // obsolete since SBC2
        DIAG_PAGE_REBUILD_ASSIST    = 0x42,
    } eSCSIDiagnosticPages;

#define SCSI_LOG_PARAMETER_HEADER_LENGTH UINT8_C(4) // bytes
#define SCSI_VPD_PAGE_HEADER_LENGTH      UINT8_C(4) // bytes

    // The values in here may be incomplete. They are from the SPC5 spec, Table E.19 - Standard Code value guidelines
    typedef enum eStandardCodeEnum
    {
        STANDARD_CODE_NOT_SUPPORTED = 0,
        // 1 - 8 architecture model
        STANDARD_CODE_SAM  = 1,
        STANDARD_CODE_SAM2 = 2,
        STANDARD_CODE_SAM3 = 3,
        STANDARD_CODE_SAM4 = 4,
        STANDARD_CODE_SAM5 = 5,
        STANDARD_CODE_SAM6 = 6,
        // 9-64 Command Set
        STANDARD_CODE_SPC  = 9,
        STANDARD_CODE_MMC  = 10,
        STANDARD_CODE_SCC  = 11,
        STANDARD_CODE_SBC  = 12,
        STANDARD_CODE_SMC  = 13,
        STANDARD_CODE_SES  = 14,
        STANDARD_CODE_SCC2 = 15,
        STANDARD_CODE_SSC  = 16,
        STANDARD_CODE_RBC  = 17,
        STANDARD_CODE_MMC2 = 18,
        STANDARD_CODE_SPC2 = 19,
        STANDARD_CODE_OCRW = 20,
        STANDARD_CODE_MMC3 = 21,
        STANDARD_CODE_RMC  = 22,
        STANDARD_CODE_SMC2 = 23,
        STANDARD_CODE_SPC3 = 24,
        STANDARD_CODE_SBC2 = 25,
        STANDARD_CODE_OSD  = 26,
        STANDARD_CODE_SSC2 = 27,
        STANDARD_CODE_BCC  = 28,
        STANDARD_CODE_MMC4 = 29,
        STANDARD_CODE_ADC  = 30,
        STANDARD_CODE_SES2 = 31,
        STANDARD_CODE_SSC3 = 32,
        STANDARD_CODE_MMC5 = 33,
        STANDARD_CODE_OSD2 = 34,
        STANDARD_CODE_SPC4 = 35,
        STANDARD_CODE_SMC3 = 36,
        STANDARD_CODE_ADC2 = 37,
        STANDARD_CODE_SBC3 = 38,
        STANDARD_CODE_MMC6 = 39,
        STANDARD_CODE_ADC3 = 40,
        STANDARD_CODE_SSC4 = 41,
        STANDARD_CODE_OSD3 = 43,
        STANDARD_CODE_SES3 = 44,
        STANDARD_CODE_SSC5 = 45,
        STANDARD_CODE_SPC5 = 46,
        STANDARD_CODE_SFSC = 47,
        STANDARD_CODE_SBC4 = 48,
        STANDARD_CODE_ZBC  = 49,
        STANDARD_CODE_ADC4 = 50,
        STANDARD_CODE_ZBC2 = 51,
        STANDARD_CODE_SES4 = 52,
        // 65 - 84 Physical Mapping protocol
        STANDARD_CODE_SSA_TL2 = 65,
        STANDARD_CODE_SSA_TL1 = 66,
        STANDARD_CODE_SSA_S3P = 67,
        STANDARD_CODE_SSA_S2P = 68,
        STANDARD_CODE_SIP     = 69,
        STANDARD_CODE_FCP     = 70,
        STANDARD_CODE_SBP2    = 71,
        STANDARD_CODE_FCP2    = 72,
        STANDARD_CODE_SST     = 73,
        STANDARD_CODE_SRP     = 74,
        STANDARD_CODE_iSCSI   = 75,
        STANDARD_CODE_SBP3    = 76,
        STANDARD_CODE_SRP2    = 77,
        STANDARD_CODE_ADP     = 78,
        STANDARD_CODE_ADT     = 79,
        STANDARD_CODE_FCP3    = 80,
        STANDARD_CODE_ADT2    = 81,
        STANDARD_CODE_FCP4    = 82,
        STANDARD_CODE_ADT3    = 83,
        // 85 - 94 Parallel SCSI Physical
        STANDARD_CODE_SPI    = 85,
        STANDARD_CODE_FAST20 = 86,
        STANDARD_CODE_SPI2   = 87,
        STANDARD_CODE_SPI3   = 88,
        STANDARD_CODE_EPI    = 89,
        STANDARD_CODE_SPI4   = 90,
        STANDARD_CODE_SPI5   = 91,
        // 95 - 104 Serial Attached SCSI
        STANDARD_CODE_SAS    = 95,
        STANDARD_CODE_SAS1_1 = 96,
        STANDARD_CODE_SAS2   = 97,
        STANDARD_CODE_SAS2_1 = 98,
        STANDARD_CODE_SAS3   = 99,
        STANDARD_CODE_SAS4   = 100,
        // 105 - 154 Fibre Channel
        STANDARD_CODE_FC_PH    = 105,
        STANDARD_CODE_FC_AL    = 106,
        STANDARD_CODE_FC_AL2   = 107,
        STANDARD_CODE_AC_PH3   = 108,
        STANDARD_CODE_FC_FS    = 109,
        STANDARD_CODE_FC_PI    = 110,
        STANDARD_CODE_FC_PI2   = 111,
        STANDARD_CODE_FC_FS2   = 112,
        STANDARD_CODE_FC_LS    = 113,
        STANDARD_CODE_FC_SP    = 114,
        STANDARD_CODE_FC_PI3   = 115,
        STANDARD_CODE_FC_PI4   = 116,
        STANDARD_CODE_FC_10GFC = 117,
        STANDARD_CODE_FC_SP2   = 118,
        STANDARD_CODE_FC_FS3   = 119,
        STANDARD_CODE_FC_LS2   = 120,
        STANDARD_CODE_FC_PI5   = 121,
        STANDARD_CODE_FC_PI6   = 122,
        STANDARD_CODE_FC_FS4   = 123,
        STANDARD_CODE_FC_LS3   = 124,
        STANDARD_CODE_FC_SCM   = 149,
        STANDARD_CODE_FC_DA2   = 150,
        STANDARD_CODE_FC_DA    = 151,
        STANDARD_CODE_FC_TAPE  = 152,
        STANDARD_CODE_FC_FLA   = 153,
        STANDARD_CODE_FC_PLDA  = 154,
        // 155 - 164 SSA
        STANDARD_CODE_SSA_PH2 = 155,
        STANDARD_CODE_SSA_PH3 = 156,
        // 165 - 174 IEEE 1394
        STANDARD_CODE_IEEE_1394_1995 = 165,
        STANDARD_CODE_IEEE_1394a     = 166,
        STANDARD_CODE_IEEE_1394b     = 167,
        // 175 - 200 ATAPI & USB
        STANDARD_CODE_ATA_ATAPI6 = 175,
        STANDARD_CODE_ATA_ATAPI7 = 176,
        STANDARD_CODE_ATA_ATAPI8 = 177,
        STANDARD_CODE_USB        = 185,
        STANDARD_CODE_UAS        = 186,
        STANDARD_CODE_ACSx       = 187,
        STANDARD_CODE_UAS2       = 188,
        // 201 - 224 Networking
        // 225 - 244 ATM
        // 245 - 260 Translators
        STANDARD_CODE_SAT  = 245,
        STANDARD_CODE_SAT2 = 246,
        STANDARD_CODE_SAT3 = 247,
        STANDARD_CODE_SAT4 = 248,
        // 261 - 270 SAS Transport Protocols
        STANDARD_CODE_SPL  = 261,
        STANDARD_CODE_SPL2 = 262,
        STANDARD_CODE_SPL3 = 263,
        STANDARD_CODE_SPL4 = 264,
        STANDARD_CODE_SPL5 = 265,
        // 271 - 290 SCSI over PCI Extress Transport Protocols
        STANDARD_CODE_SOP  = 271,
        STANDARD_CODE_PQI  = 272,
        STANDARD_CODE_SOP2 = 273,
        STANDARD_CODE_PQI2 = 274,
        // 291 - 2045 Reserved for Expansion
        STANDARD_CODE_IEEE_1667 = 2046,
        STANDARD_CODE_RESERVED  = 2047
    } eStandardCode;

    typedef enum eMRIEModesEnum
    {
        SCSI_MRIE_NO_REPORTING                                  = 0,
        SCSI_MRIE_ASYNCHRONOUS_EVENT_REPORTING                  = 1, // obsolete
        SCSI_MRIE_GENERATE_UNIT_ATTENTION                       = 2,
        SCSI_MRIE_CONDITIONALLY_GENERATE_RECOVERED_ERROR        = 3,
        SCSI_MRIE_UNCONDITIONALLY_GENERATE_RECOVERED_ERROR      = 4,
        SCSI_MRIE_GENERATE_NO_SENSE                             = 5,
        SCSI_MRIE_ONLY_REPORT_ON_EXCEPTION_CONDITION_ON_REQUEST = 6,
        // modes 7h - Bh are reserved
        // modes Ch - Fh are vendor specific
    } eMRIEModes;

#define SAT_SECURITY_INFO_LEN                                                                                          \
    UINT8_C(16) // security protocol in to receive ATA security information is 16bytes in length from SAT
#define SAT_SECURITY_PASS_LEN                                                                                          \
    UINT8_C(36) // all security protocol out commands that send the password must be 36 bytes in length according to SAT
    // SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD = 0xEF (defined in cmds.h for common use)
    typedef enum eSATSecurityDevicePasswordEnum
    {
        SAT_SECURITY_PROTOCOL_SPECIFIC_READ_INFO =
            0x0000, // should only be used to read security protocol information (ATA security info)
        SAT_SECURITY_PROTOCOL_SPECIFIC_SET_PASSWORD     = 0x0001, // setting password and sending data to device
        SAT_SECURITY_PROTOCOL_SPECIFIC_UNLOCK           = 0x0002, // unlocking the drive with a provided password
        SAT_SECURITY_PROTOCOL_SPECIFIC_ERASE_PREPARE    = 0x0003, // erase prepare command. Non-data
        SAT_SECURITY_PROTOCOL_SPECIFIC_ERASE_UNIT       = 0x0004, // erase unit command with provided password
        SAT_SECURITY_PROTOCOL_SPECIFIC_FREEZE_LOCK      = 0x0005, // freeze lock command. Non-data
        SAT_SECURITY_PROTOCOL_SPECIFIC_DISABLE_PASSWORD = 0x0006, // disable password command with provided password
        // All others are reserved
    } eSATSecurityDevicePassword;

    typedef enum eSCSIProtocolIDEnum
    {
        SCSI_PROTOCOL_ID_FIBRE_CHANNEL        = 0x0,
        SCSI_PROTOCOL_ID_SPI                  = 0x1, // Parallel SCSI
        SCSI_PROTOCOL_ID_SSA                  = 0x2, // Serial Storage Architecture
        SCSI_PROTOCOL_ID_SBP                  = 0x3, // IEEE 1394
        SCSI_PROTOCOL_ID_SRP                  = 0x4, // SCSI RDMA
        SCSI_PROTOCOL_ID_iSCSI                = 0x5, // internet SCSI
        SCSI_PROTOCOL_ID_SAS                  = 0x6, // Serial Attached SCSI
        SCSI_PROTOCOL_ID_ADT                  = 0x7, // Automation/Drive interface transport protocol
        SCSI_PROTOCOL_ID_ATA                  = 0x8, // AT Attachment Interface
        SCSI_PROTOCOL_ID_UAS                  = 0x9, // USB Attached SCSI
        SCSI_PROTOCOL_ID_SOP                  = 0xA, // SCSI over PCI express
        SCSI_PROTOCOL_ID_PCIe                 = 0xB, // PCI Express Protocols
        SCSI_PROTOCOL_ID_RESERVED1            = 0xC,
        SCSI_PROTOCOL_ID_RESERVED2            = 0xD,
        SCSI_PROTOCOL_ID_RESERVED3            = 0xE,
        SCSI_PROTOCOL_ID_NO_SPECIFIC_PROTOCOL = 0xF
    } eSCSIProtocolID;

#define REPORT_LUNS_MIN_LENGTH                                                                                         \
    UINT16_C(16) // this is the minimum length from SPC, but this requirement was removed later -TJE

    OPENSEA_TRANSPORT_API bool is_LaCie_USB_Vendor_ID(const char* t10VendorIdent);
    OPENSEA_TRANSPORT_API bool is_Seagate_USB_Vendor_ID(const char* t10VendorIdent);
    OPENSEA_TRANSPORT_API bool is_Seagate_SAS_Vendor_ID(const char* t10VendorIdent);
    OPENSEA_TRANSPORT_API void seagate_Serial_Number_Cleanup(const char* t10VendorIdent,
                                                             char**      unitSerialNumber,
                                                             size_t      unitSNSize);

    // SCSI Architecture model status's
    typedef enum eSAMStatusEnum
    {
        SAM_STATUS_GOOD                       = 0x00,
        SAM_STATUS_CHECK_CONDITION            = 0x02,
        SAM_STATUS_CONDITION_MET              = 0x03,
        SAM_STATUS_BUSY                       = 0x04,
        SAM_STATUS_INTERMEDIATE               = 0x10,
        SAM_STATUS_INTERMEDIATE_CONDITION_MET = 0x14,
        SAM_STATUS_RESERVATION_CONFLICT       = 0x18,
        SAM_STATUS_COMMAND_TERMINATED         = 0x22,
        SAM_STATUS_TASK_SET_FULL              = 0x28,
        SAM_STATUS_ACA_ACTIVE                 = 0x30,
        SAM_STATUS_TASK_ABORTED               = 0x40,
    } eSAMStatus;

#if defined(__cplusplus)
} // extern "C"
#endif
