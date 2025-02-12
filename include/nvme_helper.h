// SPDX-License-Identifier: MPL-2.0

//! \file nvme_helper.h
//! \brief Defines the constants structures to help with NVM Express Specification
//! This file acts as a OS agnostic glue layer for different OSes.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once
#include "common_public.h"
#if defined(__cplusplus)
extern "C"
{
#endif

#define NVME_IDENTIFY_DATA_LEN    (4096)
#define NVME_SMART_HEALTH_LOG_LEN (512)
#define NVME_DWORD_SIZE           (4)
#define NVME_ALL_NAMESPACES       UINT32_C(0xFFFFFFFF) // This was chosen over 0 for backwards compatibility - TJE

#define NVME_MAX_FW_SLOTS         (7)

    /* Controller Registers Section 3 & 3.1 NVMe specifications*/
    typedef struct s_nvmeBarCtrlRegisters
    {
        uint64_t cap;       /* 00h Controller Capabilities */
        uint32_t vs;        /* 08h Version */
        uint32_t intms;     /* 0Ch Interrupt Mask Set */
        uint32_t intmc;     /* 10h Interrupt Mask Clear */
        uint32_t cc;        /* 14h Controller Configuration */
        uint32_t reserved1; /* 18h Reserved */
        uint32_t csts;      /* 1Ch Controller Status */
        uint32_t nssr;      /* 20h NVM Sumsystem Reset (Optional) */
        uint32_t aqa;       /* 24h Admin Queue Attributes */
        uint64_t asq;       /* 28h Admin SQ Base Address */
        uint64_t acq;       /* 30h Admin CQ Base Address */
    } nvmeBarCtrlRegisters;

    typedef enum eNvmeCmdTypeEnum
    {
        NVM_ADMIN_CMD,
        NVM_CMD,
        NVM_UNKNOWN_CMD_SET // NVMe allows for additional command sets to be defined, but only 2 are currently, so make
                            // sure we have an "unknown" option
    } eNvmeCmdType;

    typedef enum eNvmeIdentifyCNSEnum
    {
        NVME_IDENTIFY_NS                    = 0,
        NVME_IDENTIFY_CTRL                  = 1,
        NVME_IDENTIFY_ALL_ACTIVE_NS         = 2,
        NVME_IDENTIFY_NS_ID_DESCRIPTOR_LIST = 3,
    } eNvmeIdentifyCNS;

    typedef enum eNvmePowerFlagsEnum
    {
        NVME_PS_FLAG_MAX_POWER_SCALE = 1 << 0,
        NVME_PS_FLAG_NON_OP_STATE    = 1 << 1,
    } eNvmePowerFlags;

#if 0
    typedef enum eNvmeCtrlCapEnum {
        NVME_ONCS_CTRL_COMPARE          = 1 << 0,
        NVME_ONCS_CTRL_WRITE_UNCORRECTABLE  = 1 << 1,
        NVME_ONCS_CTRL_DSM          = 1 << 2,
        NVME_VWC_CTRL_PRESENT           = 1 << 0,
    } eNvmeCtrlCap;
#endif
    typedef enum eNvmeNameSpaceEnum
    {
        NVME_NS_LBAF_BEST_RP     = 0,
        NVME_NS_LBAF_BETTER_RP   = 1,
        NVME_NS_LBAF_GOOD_RP     = 2,
        NVME_NS_LBAF_DEGRADED_RP = 3
    } eNvmeNameSpace;

    // Figure 78: Get Log Page - Error Information Log Entry (Log Identifier 01h)
    M_PACK_ALIGN_STRUCT(nvmeErrLogEntry, 1, uint64_t errorCount; uint16_t subQueueID; uint16_t cmdID;
                        uint16_t statusField;
                        uint16_t paramErrLocation;
                        uint64_t lba;
                        uint32_t nsid;
                        uint8_t  vendorSpecificInfoAvailable;
                        uint8_t  resv1[3];
                        uint64_t cmdSpecificInfo;
                        uint8_t  resv2[24];);

    M_PACK_ALIGN_STRUCT(nvmeSmartLog, 1, uint8_t criticalWarning; uint8_t temperature[2]; uint8_t availSpare;
                        uint8_t  spareThresh;
                        uint8_t  percentUsed;
                        uint8_t  enduranceGroupCriticalWarning;
                        uint8_t  rsvd6[25];
                        uint8_t  dataUnitsRead[16];
                        uint8_t  dataUnitsWritten[16];
                        uint8_t  hostReads[16];
                        uint8_t  hostWrites[16];
                        uint8_t  ctrlBusyTime[16];
                        uint8_t  powerCycles[16];
                        uint8_t  powerOnHours[16];
                        uint8_t  unsafeShutdowns[16];
                        uint8_t  mediaErrors[16];
                        uint8_t  numErrLogEntries[16];
                        uint32_t warningTempTime;
                        uint32_t criticalCompTime;
                        uint16_t tempSensor[8];
                        uint32_t thermalMgmtTemp1TransCount;
                        uint32_t thermalMgmtTemp2TransCount;
                        uint32_t totalTimeThermalMgmtTemp1;
                        uint32_t totalTimeThermalMgmtTemp2;
                        uint8_t  rsvd216[280];);

    M_PACK_ALIGN_STRUCT(nvmeFirmwareSlotInfo,
                        1,
                        uint8_t  afi; // Active Firmware Info Bit 2:0 indicates the firmware slot
                        uint8_t  rsvd1[7];
                        uint64_t FSR[7];
                        uint8_t  rsvd2[448];);

    enum
    {
        /* Self-test log Validation bits */
        NVME_SELF_TEST_VALID_NSID = 1 << 0,
        NVME_SELF_TEST_VALID_FLBA = 1 << 1,
        NVME_SELF_TEST_VALID_SCT  = 1 << 2,
        NVME_SELF_TEST_VALID_SC   = 1 << 3,
        NVME_SELF_TEST_REPORTS    = 20,
    };

    M_PACK_ALIGN_STRUCT(nvmeSelfTestRes, 1, uint8_t deviceSelfTestStatus; uint8_t segmentNum;
                        uint8_t  validDiagnosticInfo;
                        uint8_t  rsvd;
                        uint64_t powerOnHours;
                        uint32_t nsid;
                        uint64_t failingLba;
                        uint8_t  statusCodeType;
                        uint8_t  statusCode;
                        uint8_t  vendorSpecific[2];);

    M_PACK_ALIGN_STRUCT(nvmeSelfTestLog, 1, uint8_t crntDevSelftestOprn; uint8_t crntDevSelftestCompln; uint8_t rsvd[2];
                        nvmeSelfTestRes result[20];);

    enum
    {
        NVME_CMD_EFFECTS_CSUPP    = 1 << 0,
        NVME_CMD_EFFECTS_LBCC     = 1 << 1,
        NVME_CMD_EFFECTS_NCC      = 1 << 2,
        NVME_CMD_EFFECTS_NIC      = 1 << 3,
        NVME_CMD_EFFECTS_CCC      = 1 << 4,
        NVME_CMD_EFFECTS_CSE_MASK = 3 << 16,
    };

    M_PACK_ALIGN_STRUCT(nvmeEffectsLog, 1, uint32_t acs[256]; uint32_t iocs[256]; uint8_t resv[2048];);

    typedef enum eNvmeSmartAttrEnum
    {
        NVME_SMART_CRIT_SPARE_           = 1 << 0,
        NVME_SMART_CRIT_TEMPERATURE_     = 1 << 1,
        NVME_SMART_CRIT_RELIABILITY_     = 1 << 2,
        NVME_SMART_CRIT_MEDIA_           = 1 << 3,
        NVME_SMART_CRIT_VOLATILE_MEMORY_ = 1 << 4,
    } eNvmeSmartAttr;

    typedef struct s_nvmeLBARangeType
    {
        uint8_t  rangeType; // type
        uint8_t  attributes;
        uint8_t  rsvd2[14];
        uint64_t slba;
        uint64_t nlb;
        uint8_t  guid[16];
        uint8_t  rsvd48[16];
    } nvmeLBARangeType;

    typedef enum eNvmeLBARangesEnum
    {
        NVME_LBART_TYPE_FS_    = 0x01,
        NVME_LBART_TYPE_RAID_  = 0x02,
        NVME_LBART_TYPE_CACHE_ = 0x03,
        NVME_LBART_TYPE_SWAP_  = 0x04,

        NVME_LBART_ATTRIB_TEMP_ = 1 << 0,
        NVME_LBART_ATTRIB_HIDE_ = 1 << 1,
    } eNvmeLBARanges;

/**
 * Seagate Specific Log pages - Start
 */

/**
 * Seagate NVMe specific structures to identify all supported
 * log pages.
 */
#define MAX_LOG_PAGE_LEN 4096

    /**
     * Log Page 0xC5 - Supported Log Pages
     */
    typedef struct s_logPageMapEntry
    {
        uint32_t logPageID;
        uint32_t logPageSignature;
        uint32_t logPageVersion;
    } logPageMapEntry;

#define MAX_SUPPORTED_LOG_PAGE_ENTRIES ((MAX_LOG_PAGE_LEN - sizeof(uint32_t)) / sizeof(logPageMapEntry))

    typedef struct s_logPageMap
    {
        uint32_t        numLogPages;
        logPageMapEntry logPageEntry[MAX_SUPPORTED_LOG_PAGE_ENTRIES];
    } logPageMap;

    /**
     * Get Log Page - Supercap DRAM SMART Log Entry (Log Identifier
     * CFh)
     */

    typedef struct s_u128
    {
        uint64_t LS__u64;
        uint64_t MS__u64;
    } u128;

    M_PACK_ALIGN_STRUCT(nvmeSuperCapDramSmartAttr,
                        1,
                        uint16_t superCapCurrentTemperature;      // 00-01
                        uint16_t superCapMaximumTemperature;      // 02-03
                        uint8_t  superCapStatus;                  // 04
                        uint8_t  reserved5to7[3];                 // 05-07
                        u128     dataUnitsReadToDramNamespace;    // 08-23
                        u128     dataUnitsWrittenToDramNamespace; // 24-39
                        uint64_t dramCorrectableErrorCount;       // 40-47
                        uint64_t dramUncorrectableErrorCount;     // 48-55
    );

    typedef struct s_nvmeSuperCapDramSmart
    {
        nvmeSuperCapDramSmartAttr attrScSmart;
        uint8_t                   vendorSpecificReserved[456]; // 56-511
    } nvmeSuperCapDramSmart;

    M_PACK_ALIGN_STRUCT(nvmeTemetryLogHdr, 1, uint8_t logId; uint8_t rsvd1[4]; uint8_t ieeeId[3];
                        uint16_t teleDataArea1;
                        uint16_t teleDataArea2;
                        uint16_t teleDataArea3;
                        uint8_t  rsvd14[368];
                        uint8_t  teleDataAval;
                        uint8_t  teleDataGenNum;
                        uint8_t  reasonIdentifier[128];);

    /**************************
     * PCIE ERROR INFORMATION
     **************************/
    M_PACK_ALIGN_STRUCT(nvmePcieErrorLogPage, 1, uint32_t version; uint32_t badDllpErrCnt; uint32_t badTlpErrCnt;
                        uint32_t rcvrErrCnt;
                        uint32_t replayTOErrCnt;
                        uint32_t replayNumRolloverErrCnt;
                        uint32_t fcProtocolErrCnt;
                        uint32_t dllpProtocolErrCnt;
                        uint32_t cmpltnTOErrCnt;
                        uint32_t rcvrQOverflowErrCnt;
                        uint32_t unexpectedCplTlpErrCnt;
                        uint32_t cplTlpURErrCnt;
                        uint32_t cplTlpCAErrCnt;
                        uint32_t reqCAErrCnt;
                        uint32_t reqURErrCnt;
                        uint32_t ecrcErrCnt;
                        uint32_t malformedTlpErrCnt;
                        uint32_t cplTlpPoisonedErrCnt;
                        uint32_t memRdTlpPoisonedErrCnt;);
    // EOF PCIE ERROR INFORMATION

    /**
     * Seagate Specific Log pages - End
     */

    /* I/O commands */

    typedef enum eNvmeOPCodesEnum
    {
        NVME_CMD_FLUSH                = 0x00,
        NVME_CMD_WRITE                = 0x01,
        NVME_CMD_READ                 = 0x02,
        NVME_CMD_WRITE_UNCOR          = 0x04,
        NVME_CMD_COMPARE              = 0x05,
        NVME_CMD_WRITE_ZEROS          = 0x08,
        NVME_CMD_DATA_SET_MANAGEMENT  = 0x09,
        NVME_CMD_VERIFY               = 0x0C,
        NVME_CMD_RESERVATION_REGISTER = 0x0D,
        NVME_CMD_RESERVATION_REPORT   = 0x0E,
        NVME_CMD_RESERVATION_ACQUIRE  = 0x11,
        NVME_CMD_RESERVATION_RELEASE  = 0x15,
    } eNvmeOPCodes;

    M_PACK_ALIGN_STRUCT(nvmCommand,
                        1,
                        uint8_t  opcode;    // CDW0
                        uint8_t  flags;     // CDW0
                        uint16_t commandId; // CDW0
                        uint32_t nsid;      // CDW1
                        uint32_t cdw2;      // CDW2
                        uint32_t cdw3;      // CDW3
                        uint64_t metadata;  // CDW4 & 5
                        uint64_t prp1;      // CDW6 & 7
                        uint64_t prp2;      // CDW8 & 9
                        uint32_t cdw10;     // CDW10
                        uint32_t cdw11;     // CDW11
                        uint32_t cdw12;     // CDW12
                        uint32_t cdw13;     // CDW13
                        uint32_t cdw14;     // CDW14
                        uint32_t cdw15;     // CDW15
    );

    // NVMe Spec - Figure 62: Firmware Commit - Command Dword 10
    typedef enum nvmeFWCommitActionEnum
    {
        NVME_CA_REPLACE_NOT_ACTIVITED            = 0,
        NVME_CA_REPLACE_ACTIVITE_ON_RST          = 1,
        NVME_CA_ACTIVITE_ON_RST                  = 2,
        NVME_CA_ACTIVITE_IMMEDIATE               = 3,
        NVME_CA_DOWNLOAD_REP_BOOT_PART_W_PART_ID = 6,
        NVME_CA_MARK_BOOT_PART_AS_ACTIVE         = 7
    } nvmeFWCommitAction;

    /* Admin commands */
    typedef enum eNVMeAdminOpCodesEnum
    {
        NVME_ADMIN_CMD_DELETE_SQ                 = 0x00,
        NVME_ADMIN_CMD_CREATE_SQ                 = 0x01,
        NVME_ADMIN_CMD_GET_LOG_PAGE              = 0x02,
        NVME_ADMIN_CMD_DELETE_CQ                 = 0x04,
        NVME_ADMIN_CMD_CREATE_CQ                 = 0x05,
        NVME_ADMIN_CMD_IDENTIFY                  = 0x06,
        NVME_ADMIN_CMD_ABORT_CMD                 = 0x08,
        NVME_ADMIN_CMD_SET_FEATURES              = 0x09,
        NVME_ADMIN_CMD_GET_FEATURES              = 0x0A,
        NVME_ADMIN_CMD_ASYNC_EVENT               = 0x0C,
        NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT      = 0x0D,
        NVME_ADMIN_CMD_ACTIVATE_FW               = 0x10,
        NVME_ADMIN_CMD_DOWNLOAD_FW               = 0x11,
        NVME_ADMIN_CMD_DEVICE_SELF_TEST          = 0x14,
        NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT      = 0x15,
        NVME_ADMIN_CMD_KEEP_ALIVE                = 0x18,
        NVME_ADMIN_CMD_DIRECTIVE_SEND            = 0x19,
        NVME_ADMIN_CMD_DIRECTIVE_RECEIVE         = 0x1A,
        NVME_ADMIN_CMD_VIRTUALIZATION_MANAGEMENT = 0x1C,
        NVME_ADMIN_CMD_NVME_MI_SEND              = 0x1D,
        NVME_ADMIN_CMD_NVME_MI_RECEIVE           = 0x1E,
        NVME_ADMIN_CMD_DOORBELL_BUFFER_CONFIG    = 0x7C,
        NVME_ADMIN_CMD_NVME_OVER_FABRICS         = 0x7F,
        NVME_ADMIN_CMD_FORMAT_NVM                = 0x80,
        NVME_ADMIN_CMD_SECURITY_SEND             = 0x81,
        NVME_ADMIN_CMD_SECURITY_RECV             = 0x82,
        NVME_ADMIN_CMD_SANITIZE                  = 0x84,
    } eNVMeAdminOpCodes;

    // This enum should only be for thing in set/get features commands!!!
    // Anything else should be in a separate enum. No need to clutter this one!
    // TODO: we should name these so we don't need a trailing underscore
    typedef enum eNvmeFeaturesEnum
    {
        NVME_FEAT_ARBITRATION_                            = 0x01,
        NVME_FEAT_POWER_MGMT_                             = 0x02,
        NVME_FEAT_LBA_RANGE_                              = 0x03,
        NVME_FEAT_TEMP_THRESH_                            = 0x04,
        NVME_FEAT_ERR_RECOVERY_                           = 0x05,
        NVME_FEAT_VOLATILE_WC_                            = 0x06,
        NVME_FEAT_NUM_QUEUES_                             = 0x07,
        NVME_FEAT_IRQ_COALESCE_                           = 0x08,
        NVME_FEAT_IRQ_CONFIG_                             = 0x09,
        NVME_FEAT_WRITE_ATOMIC_                           = 0x0A,
        NVME_FEAT_ASYNC_EVENT_                            = 0x0B,
        NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION_      = 0x0C,
        NVME_FEAT_HOST_MEMORY_BUFFER_                     = 0x0D,
        NVME_FEAT_TIMESTAMP_                              = 0x0E,
        NVME_FEAT_KEEP_ALIVE_TIMER_                       = 0x0F,
        NVME_FEAT_HOST_CONTROLLED_THERMAL_MANAGEMENT_     = 0x10,
        NVME_FEAT_NON_OPERATIONAL_POWER_STATE_CONFIG_     = 0x11,
        NVME_FEAT_READ_RECOVERY_LEVEL_CONFIG_             = 0x12,
        NVME_FEAT_PREDICATABLE_LATENCY_MODE_CONFIG_       = 0x13,
        NVME_FEAT_PREDICATABLE_LATENCY_MODE_WINDOW_       = 0x14,
        NVME_FEAT_LBA_STATUS_INFORMATION_REPORT_INTERVAL_ = 0x15, // NVM command set
        NVME_FEAT_HOST_BEHAVIOR_SUPPORT_                  = 0x16,
        NVME_FEAT_SANITIZE_CONFIG_                        = 0x17,
        NVME_FEAT_ENDURANCE_GROUP_EVENT_CONFIGURATION_    = 0x18,
        NVME_FEAT_IO_COMMAND_SET_PROFILE_                 = 0x19,
        NVME_FEAT_SPINUP_CONTROL_                         = 0x1A,
        // key value - 20h
        //  78h-7C = reserved for management features
        NVME_FEAT_ENHANCED_CONTROLLER_METADATA_ = 0x7D,
        NVME_FEAT_CONTROLLER_METADATA_          = 0x7E,
        NVME_FEAT_NAMESPACE_METADATA_           = 0x7F,
        // NVM command set specific
        NVME_FEAT_SOFTWARE_PROGRESS_MARKER_          = 0x80,
        NVME_FEAT_HOST_IDENTIFIER_                   = 0x81,
        NVME_FEAT_RESERVATION_NOTIFICATION_MASK_     = 0x82,
        NVME_FEAT_RESERVATION_PERSISTANCE_           = 0x83,
        NVME_FEAT_NAMESPACE_WRITE_PROTECTION_CONFIG_ = 0x84,
    } eNvmeFeatures;

    // Not sure where these belong...but not in the above enum
    /*NVME_QUEUE_PHYS_CONTIG_   = (1 << 0),
        NVME_CQ_IRQ_ENABLED_    = (1 << 1),
        NVME_SQ_PRIO_URGENT_    = (0 << 1),
        NVME_SQ_PRIO_HIGH_  = (1 << 1),
        NVME_SQ_PRIO_MEDIUM_    = (2 << 1),
        NVME_SQ_PRIO_LOW_   = (3 << 1),*/

    typedef enum eNvmeLogsEnum
    {
        NVME_LOG_SUPPORTED_PAGES_ID                          = 0x00,
        NVME_LOG_ERROR_ID                                    = 0x01,
        NVME_LOG_SMART_ID                                    = 0x02,
        NVME_LOG_FW_SLOT_ID                                  = 0x03,
        NVME_LOG_CHANGED_NAMESPACE_LIST                      = 0x04,
        NVME_LOG_CMD_SPT_EFET_ID                             = 0x05,
        NVME_LOG_DEV_SELF_TEST_ID                            = 0x06,
        NVME_LOG_TELEMETRY_HOST_ID                           = 0x07,
        NVME_LOG_TELEMETRY_CTRL_ID                           = 0x08,
        NVME_LOG_ENDURANCE_GROUP_INFO_ID                     = 0x09,
        NVME_LOG_PREDICTABLE_LATENCY_PER_NVM_SET_ID          = 0x0A,
        NVME_LOG_PREDICTABLE_LATENCY_EVENT_AGREGATE_ID       = 0x0B,
        NVME_LOG_ASYMMETRIC_NAMESPACE_ACCESS_ID              = 0x0C,
        NVME_LOG_PERSISTENT_EVENT_LOG_ID                     = 0x0D,
        NVME_LOG_COMMAND_SET_SPECIFIC_ID                     = 0x0E,
        NVME_LOG_ENDURANCE_GROUP_EVENT_AGREGATE_ID           = 0x0F,
        NVME_LOG_MEDIA_UNIT_STATUS_ID                        = 0x10,
        NVME_LOG_SUPPORTED_CAPACITY_CONFIGURATION_LIST_ID    = 0x11,
        NVME_LOG_FETURE_IDENTIFIERS_SUPPORTED_AND_EFFECTS_ID = 0x12,
        NVME_LOG_MN_COMMANDS_SUPPORTED_AND_EFFECTS_ID        = 0x13,
        NVME_LOG_COMMAND_AND_FEATURE_LOCKDOWN_ID             = 0x14,
        NVME_LOG_BOOT_PARTITION_ID                           = 0x15,
        NVME_LOG_ROTATIONAL_MEDIA_INFORMATION_ID             = 0x16,
        NVME_LOG_DISCOVERY_ID                                = 0x70,
        NVME_LOG_RESERVATION_ID                              = 0x80,
        NVME_LOG_SANITIZE_ID                                 = 0x81,
    } eNvmeLogs;

    typedef enum eNvmeFeaturesSelectValueEnum
    {
        NVME_CURRENT_FEAT_SEL,
        NVME_DEFAULT_FEAT_SEL,
        NVME_SAVED_FEAT_SEL,
        NVME_SUPPORTED_FEAT_SEL
    } eNvmeFeaturesSelectValue;

    typedef struct s_nvmeGetLogPageCmdOpts
    {
        uint32_t nsid;
        uint64_t metadata; // MPTR
        // uint64_t  addr;   //PRP Entry 1
        uint8_t* addr; // PRP Entry 1
        uint32_t metadataLen;
        uint32_t dataLen;
        uint8_t  lid; // Log Page identifier, part of Command Dword 10(CDW10)
        // Additional attributes to support Log Page 7 and 8
        uint32_t lsp;
        uint32_t rae;
        uint64_t offset;
    } nvmeGetLogPageCmdOpts;

    /* This Command options structure is common for both Get/Set
       According to nvme spec prp1, prp2, Dword 10 and Dword 11
       are used in the Admin Get/Set features commands
    */
    typedef struct s_nvmeFeaturesCmdOpt
    {
        uint8_t* dataPtr;
        uint32_t dataLength;
        // Following are part of Dword 10 in nvmeSpec
        uint8_t  sv;              // Save Value Used for Set Features command as Bit 31
        uint8_t  rsvd;            // //this part is reserved for both Get/Set Features
        uint8_t  sel;             // Select Value SEL Bit 10:08 are used in Get Features.
        uint8_t  fid;             // Feature Identifier used for both Get/Set
        uint32_t featSetGetValue; // Value returned or to be set as Dword11
        uint32_t nsid;
    } nvmeFeaturesCmdOpt;

#if 0
    typedef struct s_nvmeIdentify {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            nsid;
        uint64_t            rsvd2[2];
        uint64_t            prp1;
        uint64_t            prp2;
        uint32_t            cns;
        uint32_t            rsvd11[5];
    } nvmeIdentify;

    typedef struct s_nvmeFeatures {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            nsid;
        uint64_t            rsvd2[2];
        uint64_t            prp1;
        uint64_t            prp2;
        uint32_t            fid;
        uint32_t            dword11;
        uint32_t            rsvd12[4];
    } nvmeFeatures;

    typedef struct s_nvmeCreateCQ {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            rsvd1[5];
        uint64_t            prp1;
        uint64_t            rsvd8;
        uint16_t            cqid;
        uint16_t            qsize;
        uint16_t            cqFlags;
        uint16_t            irq_vector;
        uint32_t            rsvd12[4];
    } nvmeCreateCQ;

    typedef struct s_nvmeCreateSQ {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            rsvd1[5];
        uint64_t            prp1;
        uint64_t            rsvd8;
        uint16_t            sqid;
        uint16_t            qsize;
        uint16_t            sqFlags;
        uint16_t            cqid;
        uint32_t            rsvd12[4];
    } nvmeCreateSQ;

    typedef struct s_nvmeDeleteQueue {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            rsvd1[9];
        uint16_t            qid;
        uint16_t            rsvd10;
        uint32_t            rsvd11[5];
    } nvmeDeleteQueue;

    typedef struct s_nvmeAbortCmd {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            rsvd1[9];
        uint16_t            sqid;
        uint16_t            cid;
        uint32_t            rsvd11[5];
    } nvmeAbortCmd;

    typedef struct s_nvmeDownloadFirmware {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            rsvd1[5];
        uint64_t            prp1;
        uint64_t            prp2;
        uint32_t            numd;
        uint32_t            offset;
        uint32_t            rsvd12[4];
    } nvmeDownloadFirmware;
#endif

    typedef enum eProtectionInfoLocEnum
    {
        PROTECTION_INFO_AT_END,
        PROTECTION_INFO_AT_BEGIN
    } eProtectionInfoLoc;

    typedef struct s_nvmeFormatCmdOpts
    {
        uint32_t nsid; // Can be 0xFFFFFFFF to apply to all namespaces
        uint8_t  lbaf; // nible that contains a 0-16 LBA format to apply
        uint8_t  ms;   // Metadata Setting. 1 if metadata is xfered as part of extended lba.
        uint8_t  pi;   // Protection Information. 0=none, 1=Type1 PI, 2=Type2, 3=Type3
        uint8_t  pil;  // Protection Information Location. 1=PI xfered as first 8 bytes of metadata
        uint8_t  ses;  // Secure Erase Settings. 0=none requested, 1=requested, 2=use Crtypto Erase
    } nvmeFormatCmdOpts;

#if 0
    typedef struct s_nvmeFormatCMD {
        uint8_t             opcode;
        uint8_t             flags;
        uint16_t            commandId;
        uint32_t            nsid;
        uint64_t            rsvd2[4];
        uint32_t            cdw10;
        uint32_t            rsvd11[5];
    } nvmeFormatCMD;
#endif

    typedef struct s_nvmeStatus
    {
        uint8_t       statusCodeType;
        uint8_t       statusCode;
        eReturnValues ret;
        const char*   description;
    } nvmeStatus;

    const nvmeStatus* get_NVMe_Status(uint32_t nvmeStatusDWord);

    typedef enum eNvmeStatusCodeTypeEnum
    {
        NVME_SCT_GENERIC_COMMAND_STATUS          = 0,
        NVME_SCT_COMMAND_SPECIFIC_STATUS         = 1,
        NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS = 2,
        NVME_SCT_PATH_RELATED_STATUS             = 3,
        // 3-6 are reserved
        NVME_SCT_VENDOR_SPECIFIC_STATUS = 7
    } eNvmeStatusCodeType;

    typedef enum eNvmeGenericStatusCodeEnum
    {
        NVME_GEN_SC_SUCCESS_                                    = 0x0,
        NVME_GEN_SC_INVALID_OPCODE_                             = 0x1,
        NVME_GEN_SC_INVALID_FIELD_                              = 0x2,
        NVME_GEN_SC_CMDID_CONFLICT_                             = 0x3,
        NVME_GEN_SC_DATA_XFER_ERROR_                            = 0x4,
        NVME_GEN_SC_POWER_LOSS_                                 = 0x5,
        NVME_GEN_SC_INTERNAL_                                   = 0x6,
        NVME_GEN_SC_ABORT_REQ_                                  = 0x7,
        NVME_GEN_SC_ABORT_QUEUE_                                = 0x8,
        NVME_GEN_SC_FUSED_FAIL_                                 = 0x9,
        NVME_GEN_SC_FUSED_MISSING_                              = 0xA,
        NVME_GEN_SC_INVALID_NS_                                 = 0xB,
        NVME_GEN_SC_CMD_SEQ_ERROR_                              = 0xC,
        NVME_GEN_SC_INVALID_SGL_SEGMENT_DESCRIPTOR              = 0x0D,
        NVME_GEN_SC_INVALID_NUMBER_OF_SGL_DESCRIPTORS           = 0x0E,
        NVME_GEN_SC_DATA_SGL_LENGTH_INVALID                     = 0x0F,
        NVME_GEN_SC_METADATA_SGL_LENGTH_INVALID                 = 0x10,
        NVME_GEN_SC_SGL_DESCRIPTOR_TYPE_INVALID                 = 0x11,
        NVME_GEN_SC_INVALID_USE_OF_CONTROLLER_MEMORY_BUFFER     = 0x12,
        NVME_GEN_SC_PRP_OFFSET_INVALID                          = 0x13,
        NVME_GEN_SC_ATOMIC_WRITE_UNIT_EXCEEDED                  = 0x14,
        NVME_GEN_SC_OPERATION_DENIED                            = 0x15,
        NVME_GEN_SC_SGL_OFFSET_INVALID                          = 0x16,
        NVME_GEN_SC_HOST_IDENTIFIER_INCONSISTENT_FORMAT         = 0x18,
        NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_EXPIRED                  = 0x19,
        NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_INVALID                  = 0x1A,
        NVME_GEN_SC_COMMAND_ABORTED_DUE_TO_PREEMPT_AND_ABORT    = 0x1B,
        NVME_GEN_SC_SANITIZE_FAILED                             = 0x1C,
        NVME_GEN_SC_SANITIZE_IN_PROGRESS                        = 0x1D,
        NVME_GEN_SC_SGL_DATA_BLOCK_GRANULARITY_INVALID          = 0x1E,
        NVME_GEN_SC_COMMAND_NOT_SUPPORTED_FOR_QUEUE_IN_CMB      = 0x1F,
        NVME_GEN_SC_NS_IS_WRITE_PROTECTED                       = 0x20,
        NVME_GEN_SC_COMMAND_INTERRUPTED                         = 0x21,
        NVME_GEN_SC_TRANSIENT_TRANSPORT_ERROR                   = 0x22,
        NVME_GEN_SC_COMMAND_PROHIBITED_BY_CMD_AND_FEAT_LOCKDOWN = 0x23,
        NVME_GEN_SC_ADMIN_COMMAND_MEDIA_NOT_READY               = 0x24,
        // 80-BF are NVM command set specific
        NVME_GEN_SC_LBA_RANGE_            = 0x80,
        NVME_GEN_SC_CAP_EXCEEDED_         = 0x81,
        NVME_GEN_SC_NS_NOT_READY_         = 0x82,
        NVME_GEN_SC_RESERVATION_CONFLICT  = 0x83,
        NVME_GEN_SC_FORMAT_IN_PROGRESS    = 0x84,
        NVME_GEN_SC_INVALID_VALUE_SIZE    = 0x85,
        NVME_GEN_SC_INVALID_KEY_SIZE      = 0x86,
        NVME_GEN_SC_KV_KEY_DOES_NOT_EXIST = 0x87,
        NVME_GEN_SC_UNRECOVERED_ERROR     = 0x88,
        NVME_GEN_SC_KEY_EXISTS            = 0x89,
    } eNvmeReturnStatus;

    typedef enum eNvmeCmdSpecificStatusEnum
    {
        NVME_CMD_SP_SC_CQ_INVALID_  = 0x00,
        NVME_CMD_SP_SC_QID_INVALID_ = 0x01,
        NVME_CMD_SP_SC_QUEUE_SIZE_  = 0x02,
        NVME_CMD_SP_SC_ABORT_LIMIT_ = 0x03,
        // NVME_CMD_SP_SC_ABORT_MISSING_ = 0x04,//reserved in NVMe specs
        NVME_CMD_SP_SC_ASYNC_LIMIT_                                                  = 0x05,
        NVME_CMD_SP_SC_INVALID_FIRMWARE_SLOT_                                        = 0x06,
        NVME_CMD_SP_SC_INVALIDFIRMWARE_IMAGE_                                        = 0x07,
        NVME_CMD_SP_SC_INVALID_INTERRUPT_VECTOR_                                     = 0x08,
        NVME_CMD_SP_SC_INVALID_LOG_PAGE_                                             = 0x09,
        NVME_CMD_SP_SC_INVALID_FORMAT_                                               = 0x0A,
        NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET                                 = 0x0B,
        NVME_CMD_SP_SC_INVALID_QUEUE_DELETION                                        = 0x0C,
        NVME_CMD_SP_SC_FEATURE_IDENTIFIER_NOT_SAVABLE                                = 0x0D,
        NVME_CMD_SP_SC_FEATURE_NOT_CHANGEABLE                                        = 0x0E,
        NVME_CMD_SP_SC_FEATURE_NOT_NAMESPACE_SPECIFC                                 = 0x0F,
        NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET                                   = 0x10,
        NVME_CMD_SP_SC_FW_ACT_REQ_RESET                                              = 0x11,
        NVME_CMD_SP_SC_FW_ACT_REQ_MAX_TIME_VIOALTION                                 = 0x12,
        NVME_CMD_SP_SC_FW_ACT_PROHIBITED                                             = 0x13,
        NVME_CMD_SP_SC_OVERLAPPING_RANGE                                             = 0x14,
        NVME_CMD_SP_SC_NS_INSUFFICIENT_CAP                                           = 0x15,
        NVME_CMD_SP_SC_NS_ID_UNAVAILABLE                                             = 0x16,
        NVME_CMD_SP_SC_NS_ALREADY_ATTACHED                                           = 0x18,
        NVME_CMD_SP_SC_NS_IS_PRIVATE                                                 = 0x19,
        NVME_CMD_SP_SC_NS_NOT_ATTACHED                                               = 0x1A,
        NVME_CMD_SP_SC_THIN_PROVISIONING_NOT_SUPPORTED                               = 0x1B,
        NVME_CMD_SP_SC_CONTROLLER_LIST_INVALID                                       = 0x1C,
        NVME_CMD_SP_SC_DEVICE_SELF_TEST_IN_PROGRESS                                  = 0x1D,
        NVME_CMD_SP_SC_BOOT_PARTITION_WRITE_PROHIBITED                               = 0x1E,
        NVME_CMD_SP_SC_INVALID_CONTROLLER_IDENTIFIER                                 = 0x1F,
        NVME_CMD_SP_SC_INVALID_SECONDARY_CONTROLLER_STATE                            = 0x20,
        NVME_CMD_SP_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES                        = 0x21,
        NVME_CMD_SP_SC_INVALID_RESOURCE_IDENTIFIER                                   = 0x22,
        NVME_CMD_SP_SC_SANITIZE_PROHIBITED_WHILE_PERSISTENT_MEMORY_REGION_IS_ENABLED = 0x23,
        NVME_CMD_SP_SC_ANA_GROUP_IDENTIFIER_INVALID                                  = 0x24,
        NVME_CMD_SP_SC_ANA_ATTACH_FAILED                                             = 0x25,
        NVME_CMD_SP_SC_INSUFFICIENT_CAPACITY                                         = 0x26,
        NVME_CMD_SP_SC_NAMESPACE_ATTACHMENT_LIMIT_EXCEEDED                           = 0x27,
        NVME_CMD_SP_SC_PROHIBITION_OF_COMMAND_EXECUTION_NOT_SUPPORTED                = 0x28,
        NVME_CMD_SP_SC_IO_COMMAND_SET_NOT_SUPPORTED                                  = 0x29,
        NVME_CMD_SP_SC_IO_COMMAND_SET_NOT_ENABLED                                    = 0x2A,
        NVME_CMD_SP_SC_IO_COMMAND_SET_COMBINATION_REJECTED                           = 0x2B,
        NVME_CMD_SP_SC_INVALID_IO_COMMAND_SET                                        = 0x2C,
        NVME_CMD_SP_SC_IDENTIFIER_UNAVAILABLE                                        = 0x2D,
        // 70-7F are Directive Specific
        //
        // 80-BF are NVM command set specific
        // IO Commands
        NVME_CMD_SP_SC_CONFLICTING_ATTRIBUTES_            = 0x80,
        NVME_CMD_SP_SC_INVALID_PROTECTION_INFORMATION     = 0x81,
        NVME_CMD_SP_SC_ATTEMPTED_WRITE_TO_READ_ONLY_RANGE = 0x82,
        NVME_CMD_SP_SC_COMMAND_SIZE_LIMIT_EXCEEDED        = 0x83,
        NVME_CMD_SP_SC_ZONED_BOUNDARY_ERROR               = 0xB8,
        NVME_CMD_SP_SC_ZONE_IS_FULL                       = 0xB9,
        NVME_CMD_SP_SC_ZONE_IS_READ_ONLY                  = 0xBA,
        NVME_CMD_SP_SC_ZONE_IS_OFFLINE                    = 0xBB,
        NVME_CMD_SP_SC_ZONE_INVALID_WRITE                 = 0xBC,
        NVME_CMD_SP_SC_TOO_MANY_ACTIVE_ZONES              = 0xBD,
        NVME_CMD_SP_SC_TOO_MANY_OPEN_ZONES                = 0xBE,
        NVME_CMD_SP_SC_INVALID_ZONE_STATE_TRANSITION      = 0xBF,
    } eNvmeCmdSpecificStatus;

    typedef enum eNvmeMediaDataErrStatusEnum
    {
        NVME_MED_ERR_SC_WRITE_FAULT_                           = 0x80,
        NVME_MED_ERR_SC_UNREC_READ_ERROR_                      = 0x81,
        NVME_MED_ERR_SC_ETE_GUARD_CHECK_                       = 0x82,
        NVME_MED_ERR_SC_ETE_APPTAG_CHECK_                      = 0x83,
        NVME_MED_ERR_SC_ETE_REFTAG_CHECK_                      = 0x84,
        NVME_MED_ERR_SC_COMPARE_FAILED_                        = 0x85,
        NVME_MED_ERR_SC_ACCESS_DENIED_                         = 0x86,
        NVME_MED_ERR_SC_DEALLOCATED_OR_UNWRITTEN_LOGICAL_BLOCK = 0x87,
        NVME_MED_ERR_SC_END_TO_END_STORAGE_TAG_CHECK_ERROR     = 0x88,
    } eNvmeMediaDataErrStatus;

    typedef enum eNvmePathRelatedStatusEnum
    {
        NVME_PATH_SC_INTERNAL_PATH_ERROR               = 0x00,
        NVME_PATH_SC_ASYMMETRIC_ACCESS_PERSISTENT_LOSS = 0x01,
        NVME_PATH_SC_ASYMMETRIC_ACCESS_INACCESSIBLE    = 0x02,
        NVME_PATH_SC_ASYMMETRIC_ACCESS_TRANSITION      = 0x03,
        // 60-6F are Controller detected Pathing errors
        NVME_PATH_SC_CONTROLLER_PATHING_ERROR = 0x60,
        // 70-7F are Host detected Pathing errors
        NVME_PATH_SC_HOST_PATHING_ERROR      = 0x70,
        NVME_PATH_SC_COMMAND_ABORTED_BY_HOST = 0x71,
        // 80-BFh are other pathing errors
        // c0-FFh are vendor specific
    } eNvmePathRelatedStatus;

    M_PACK_ALIGN_STRUCT(nvmeAdminCommand,
                        1,
                        uint8_t  opcode;   // Common Dword 0 (CDW0)
                        uint8_t  flags;    // Common Dword 0 (CDW0)
                        uint16_t rsvd1;    // Common Dword 0 (CDW0)
                        uint32_t nsid;     // Namespace Identifier (NSID)
                        uint32_t cdw2;     // Reserved?
                        uint32_t cdw3;     // Reserved?
                        uint64_t metadata; // MPTR
                        uint64_t addr;     // PRP Entry 1
                        uint32_t metadataLen;
                        uint32_t empty; // Was set to datalen, but that is redundant where this is otherwise used. Need
                                        // this here to keep this the correct size for use elsewhere.
                        uint32_t cdw10; // Command Dword 10(CDW10)
                        uint32_t cdw11; // Command Dword 10(CDW10)
                        uint32_t cdw12; // Command Dword 10(CDW10)
                        uint32_t cdw13; // Command Dword 10(CDW10)
                        uint32_t cdw14; // Command Dword 10(CDW10)
                        uint32_t cdw15; // Command Dword 10(CDW10)
    );

    typedef struct s_nvmCommandDWORDS
    {
        uint32_t cdw0;  // CDW0
        uint32_t cdw1;  // CDW1
        uint32_t cdw2;  // CDW2
        uint32_t cdw3;  // CDW3
        uint32_t cdw4;  // CDW4
        uint32_t cdw5;  // CDW5
        uint32_t cdw6;  // CDW6
        uint32_t cdw7;  // CDW7
        uint32_t cdw8;  // CDW8
        uint32_t cdw9;  // CDW9
        uint32_t cdw10; // CDW10
        uint32_t cdw11; // CDW11
        uint32_t cdw12; // CDW12
        uint32_t cdw13; // CDW13
        uint32_t cdw14; // CDW14
        uint32_t cdw15; // CDW15
    } nvmCommandDWORDS;

    typedef struct s_nvmeCommands
    {
        union
        {
            nvmeAdminCommand adminCmd; // Use for commands part of the Admin command set
            nvmCommand       nvmCmd;   // Use for commands part of the NVM command set
            nvmCommandDWORDS dwords; // So that we can see the whole thing as separate DWORDs...mostly used for unknown
                                     // case to print out. This is the 64 bytes of command structure.
        };
    } nvmeCommands;

    typedef struct s_completionQueueEntry
    {
        bool dw0Valid; // OS doing passthrough may or may not get this field back...so this is set to indicate it was
                       // retrieved by the OS
        bool dw1Valid;
        bool dw2Valid;
        bool dw3Valid; // AKA status and CID. Don't expect a valid CID though! Not every OS will give us that.
        union
        {
            uint32_t commandSpecific; // AKA result
            uint32_t dw0;
        };
        union
        {
            uint32_t dw1Reserved;
            uint32_t dw1;
        };
        union
        {
            uint32_t sqIDandHeadPtr; // This likely won't contain anything valid even if the OS passthrough gave us this
                                     // DWORD
            uint32_t dw2;
        };
        union
        {
            uint32_t statusAndCID;
            uint32_t dw3;
        };
    } completionQueueEntry;

    // \struct typedef struct s_nvmeCmdCtx
    typedef struct s_nvmeCmdCtx
    {
        tDevice*     device;
        eNvmeCmdType commandType; // admin vs nvm command (needed for some OSs to send to the correct queue)
        eDataTransferDirection
            commandDirection; // this should match the NVMe definition in opcode bits 1:0. 00 - no data, 01 - host to
                              // controller (out), 10 - controller to host (in), 11 - bidirectional
        nvmeCommands         cmd; // cmd definition. This will be accessed depending on what is set to comandType field
        uint8_t*             ptrData;  // buffer to hold data being sent or received
        uint32_t             dataSize; // size of data being sent or received in BYTES
        uint32_t             timeout;  // in seconds
        completionQueueEntry commandCompletionData;
        bool                 fwdlFirstSegment; // fwdl unique flag to help low-level OS code
        bool                 fwdlLastSegment;  // fwdl unique flag to help low-level OS code
        uint32_t             delay_io;
    } nvmeCmdCtx;

    // Smart attribute IDs

    typedef enum
    {
        VS_ATTR_ID_SOFT_READ_ERROR_RATE               = 1,
        VS_ATTR_ID_REALLOCATED_SECTOR_COUNT           = 5,
        VS_ATTR_ID_POWER_ON_HOURS                     = 9,
        VS_ATTR_ID_POWER_FAIL_EVENT_COUNT             = 11,
        VS_ATTR_ID_DEVICE_POWER_CYCLE_COUNT           = 12,
        VS_ATTR_ID_RAW_READ_ERROR_RATE                = 13,
        VS_ATTR_ID_GROWN_BAD_BLOCK_COUNT              = 40,
        VS_ATTR_ID_END_2_END_CORRECTION_COUNT         = 41,
        VS_ATTR_ID_MIN_MAX_WEAR_RANGE_COUNT           = 42,
        VS_ATTR_ID_REFRESH_COUNT                      = 43,
        VS_ATTR_ID_BAD_BLOCK_COUNT_USER               = 44,
        VS_ATTR_ID_BAD_BLOCK_COUNT_SYSTEM             = 45,
        VS_ATTR_ID_THERMAL_THROTTLING_STATUS          = 46,
        VS_ATTR_ID_ALL_PCIE_CORRECTABLE_ERROR_COUNT   = 47,
        VS_ATTR_ID_ALL_PCIE_UNCORRECTABLE_ERROR_COUNT = 48,
        VS_ATTR_ID_INCOMPLETE_SHUTDOWN_COUNT          = 49,
        VS_ATTR_ID_GB_ERASED_LSB                      = 100,
        VS_ATTR_ID_GB_ERASED_MSB                      = 101,
        VS_ATTR_ID_LIFETIME_ENTERING_PS4_COUNT        = 102,
        VS_ATTR_ID_LIFETIME_ENTERING_PS3_COUNT        = 103,
        VS_ATTR_ID_LIFETIME_DEVSLEEP_EXIT_COUNT       = 104,
        VS_ATTR_ID_RETIRED_BLOCK_COUNT                = 170,
        VS_ATTR_ID_PROGRAM_FAILURE_COUNT              = 171,
        VS_ATTR_ID_ERASE_FAIL_COUNT                   = 172,
        VS_ATTR_ID_AVG_ERASE_COUNT                    = 173,
        VS_ATTR_ID_UNEXPECTED_POWER_LOSS_COUNT        = 174,
        VS_ATTR_ID_WEAR_RANGE_DELTA                   = 177,
        VS_ATTR_ID_SATA_INTERFACE_DOWNSHIFT_COUNT     = 183,
        VS_ATTR_ID_END_TO_END_CRC_ERROR_COUNT         = 184,
        VS_ATTR_ID_UNCORRECTABLE_ECC_ERRORS           = 188,
        VS_ATTR_ID_MAX_LIFE_TEMPERATURE               = 194,
        VS_ATTR_ID_RAISE_ECC_CORRECTABLE_ERROR_COUNT  = 195,
        VS_ATTR_ID_UNCORRECTABLE_RAISE_ERRORS         = 198,
        VS_ATTR_ID_DRIVE_LIFE_PROTECTION_STATUS       = 230,
        VS_ATTR_ID_REMAINING_SSD_LIFE                 = 231,
        VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB       = 233,
        VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB       = 234,
        VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB      = 241,
        VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB      = 242,
        VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB         = 243,
        VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB         = 244,
        VS_ATTR_ID_FREE_SPACE                         = 245,
        VS_ATTR_ID_TRIM_COUNT_LSB                     = 250,
        VS_ATTR_ID_TRIM_COUNT_MSB                     = 251,
        VS_ATTR_ID_OP_PERCENTAGE                      = 252,
        VS_ATTR_ID_MAX_SOC_LIFE_TEMPERATURE           = 253,
    } smart_attributes_ids;

/***************************
 * Extended-SMART Information
 ***************************/
#pragma pack(push, 1)
#define NUMBER_EXTENDED_SMART_ATTRIBUTES 42

    typedef enum EXTENDED_SMART_VERSION_Enum
    {
        EXTENDED_SMART_VERSION_NONE, // 0
        EXTENDED_SMART_VERSION_GEN,  // 1
        EXTENDED_SMART_VERSION_FB,   // 2
    } EXTENDED_SMART_VERSION;

    typedef struct s_SmartVendorSpecific
    {
        uint8_t  AttributeNumber;
        uint16_t SmartStatus;
        uint8_t  NominalValue;
        uint8_t  LifetimeWorstValue;
        uint32_t Raw0_3;
        uint8_t  RawHigh[3];
    } SmartVendorSpecific;

    typedef struct s_EXTENDED_SMART_INFO_T
    {
        uint16_t            Version;
        SmartVendorSpecific vendorData[NUMBER_EXTENDED_SMART_ATTRIBUTES];
        uint8_t             vendor_specific_reserved[6];
    } EXTENDED_SMART_INFO_T;

    typedef struct fb_smart_attribute_data
    {
        uint8_t  AttributeNumber; // 00
        uint8_t  Rsvd[3];         // 01 -03
        uint32_t LSDword;         // 04-07
        uint32_t MSDword;         // 08 - 11
    } fb_smart_attribute_data;

    typedef struct s_U128
    {
        uint64_t LSU64;
        uint64_t MSU64;
    } U128;

    typedef struct s_fb_log_page_CF_Attr
    {
        uint16_t SuperCapCurrentTemperature;      // 00-01
        uint16_t SuperCapMaximumTemperature;      // 02-03
        uint8_t  SuperCapStatus;                  // 04
        uint8_t  Reserved5to7[3];                 // 05-07
        U128     DataUnitsReadToDramNamespace;    // 08-23
        U128     DataUnitsWrittenToDramNamespace; // 24-39
        uint64_t DramCorrectableErrorCount;       // 40-47
        uint64_t DramUncorrectableErrorCount;     // 48-55
    } fb_log_page_CF_Attr;

    typedef struct s_fb_log_page_CF
    {
        fb_log_page_CF_Attr AttrCF;
        uint8_t             Vendor_Specific_Reserved[456]; // 56-511
    } fb_log_page_CF;

#pragma pack(pop)

    /* EOF Extended-SMART Information*/

#if defined(__cplusplus)
}
#endif
