//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file nvme_helper.h
// \brief Defines the constants structures to help with NVM Express Specification
//        This file acts as a OS agnostic glue layer for different OSes. 

#pragma once

#if !defined(DISABLE_NVME_PASSTHROUGH)
#include "common_public.h"
#if defined (__cplusplus)
extern "C"
{
#endif

    #define NVME_IDENTIFY_DATA_LEN (4096)
    #define NVME_SMART_HEALTH_LOG_LEN (512)
    #define NVME_DWORD_SIZE (4)
    #define NVME_ALL_NAMESPACES       (0xFFFFFFFFU)

    #define NVME_MAX_FW_SLOTS         (7)

    /* Controller Registers Section 3 & 3.1 NVMe specifications*/
    typedef  struct _nvmeBarCtrlRegisters {
    	uint64_t	cap; 		/* 00h Controller Capabilities */
    	uint32_t	vs;	    	/* 08h Version */
    	uint32_t	intms;		/* 0Ch Interrupt Mask Set */
    	uint32_t	intmc;		/* 10h Interrupt Mask Clear */
    	uint32_t	cc;			/* 14h Controller Configuration */
    	uint32_t	reserved1;	/* 18h Reserved */
    	uint32_t	csts;		/* 1Ch Controller Status */
    	uint32_t	nssr;		/* 20h NVM Sumsystem Reset (Optional) */
    	uint32_t	aqa;		/* 24h Admin Queue Attributes */
    	uint64_t	asq;		/* 28h Admin SQ Base Address */
    	uint64_t	acq;		/* 30h Admin CQ Base Address */
    } nvmeBarCtrlRegisters;

    typedef enum _eNvmeCmdType {
        NVM_ADMIN_CMD,
        NVM_CMD
    } eNvmeCmdType; 

    typedef enum _eNvmeIdentifyCNS {
        NVME_IDENTIFY_NS,
        NVME_IDENTIFY_CTRL,
        NVME_IDENTIFY_ALL_ACTIVE_NS
    } eNvmeIdentifyCNS;

    typedef enum _eNvmePowerFlags{
    	NVME_PS_FLAG_MAX_POWER_SCALE	= 1 << 0,
    	NVME_PS_FLAG_NON_OP_STATE	= 1 << 1,
    } eNvmePowerFlags;

    typedef enum _eNvmeCtrlCap {
    	NVME_ONCS_CTRL_COMPARE			= 1 << 0,
    	NVME_ONCS_CTRL_WRITE_UNCORRECTABLE	= 1 << 1,
    	NVME_ONCS_CTRL_DSM			= 1 << 2,
    	NVME_VWC_CTRL_PRESENT			= 1 << 0,
    } eNvmeCtrlCap;

    typedef enum _eNvmeNameSpace {
    	NVME_NS_FEATURE_THIN_	    = 1 << 0,
    	NVME_NS_LBAF_BEST_RP	    = 0,
    	NVME_NS_LBAF_BETTER_RP	    = 1,
    	NVME_NS_LBAF_GOOD_RP	    = 2,
    	NVME_NS_LBAF_DEGRADED_RP	= 3
    } eNvmeNameSpace ;

    //Figure 78: Get Log Page - Error Information Log Entry (Log Identifier 01h)
    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _nvmeErrLogEntry {
        uint64_t            errorCount;
        uint16_t            subQueueID;
        uint16_t            cmdID;
        uint16_t            statusField;
        uint16_t            paramErrLocation;
        uint64_t            lba;
        uint32_t            nsid;
        uint8_t             vendorSpecificInfoAvailable;
        uint8_t             resv1[3]; 
        uint64_t            cmdSpecificInfo;
        uint8_t             resv2[24];
    #if !defined (__GNUC__)
    } nvmeErrLogEntry;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeErrLogEntry;
    #endif

    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _nvmeSmartLog {
    	uint8_t 			criticalWarning;
    	uint8_t 			temperature[2];
    	uint8_t 			availSpare;
    	uint8_t 			spareThresh;
    	uint8_t 			percentUsed;
    	uint8_t 			rsvd6[26];
    	uint8_t 			dataUnitsRead[16];
    	uint8_t 			dataUnitsWritten[16];
    	uint8_t 			hostReads[16];
    	uint8_t 			hostWrites[16];
    	uint8_t 			ctrlBusyTime[16];
    	uint8_t 			powerCycles[16];
    	uint8_t 			powerOnHours[16];
    	uint8_t 			unsafeShutdowns[16];
    	uint8_t 			mediaErrors[16];
    	uint8_t 			numErrLogEntries[16];
    	uint32_t 			warningTempTime;
    	uint32_t 			criticalCompTime;
    	uint16_t 			tempSensor[8];
		uint32_t			thermalMgmtTemp1TransCount;
		uint32_t			thermalMgmtTemp2TransCount;
		uint32_t			totalTimeThermalMgmtTemp1;
		uint32_t			totalTimeThermalMgmtTemp2;
    	uint8_t 			rsvd216[280];
    #if !defined (__GNUC__)
    } nvmeSmartLog;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeSmartLog;
    #endif

    #if !defined (__GNUC__)
    #pragma pack(push, 1)
    #endif
    typedef struct _nvmeFirmwareSlotInfo {
        uint8_t     afi; //Active Firmware Info Bit 2:0 indicates the firmware slot 
        uint8_t     rsvd1[7];
        uint64_t    FSR[7];
        uint8_t     rsvd2[448];
    #if !defined (__GNUC__)
    } nvmeFirmwareSlotInfo;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeFirmwareSlotInfo;
    #endif


    typedef enum _eNvmeSmartAttr{
    	NVME_SMART_CRIT_SPARE_		= 1 << 0,
    	NVME_SMART_CRIT_TEMPERATURE_	= 1 << 1,
    	NVME_SMART_CRIT_RELIABILITY_	= 1 << 2,
    	NVME_SMART_CRIT_MEDIA_		= 1 << 3,
    	NVME_SMART_CRIT_VOLATILE_MEMORY_	= 1 << 4,
    } eNvmeSmartAttr;

    typedef struct _nvmeLBARangeType {
    	uint8_t 			rangeType; //type
    	uint8_t 			attributes;
    	uint8_t 			rsvd2[14];
    	uint64_t			slba;
    	uint64_t			nlb;
    	uint8_t 			guid[16];
    	uint8_t 			rsvd48[16];
    } nvmeLBARangeType;

    typedef enum _eNvmeLBARanges {
    	NVME_LBART_TYPE_FS_	= 0x01,
    	NVME_LBART_TYPE_RAID_	= 0x02,
    	NVME_LBART_TYPE_CACHE_	= 0x03,
    	NVME_LBART_TYPE_SWAP_	= 0x04,

    	NVME_LBART_ATTRIB_TEMP_	= 1 << 0,
    	NVME_LBART_ATTRIB_HIDE_	= 1 << 1,
    } eNvmeLBARanges;

    /* I/O commands */

    typedef enum _eNvmeOPCodes {
    	NVME_CMD_FLUSH		            = 0x00,
    	NVME_CMD_WRITE		            = 0x01,
    	NVME_CMD_READ		            = 0x02,
    	NVME_CMD_WRITE_UNCOR	        = 0x04,
    	NVME_CMD_COMPARE	            = 0x05,
        NVME_CMD_WRITE_ZEROS            = 0x08,
        NVME_CMD_DATA_SET_MANAGEMENT    = 0x09,
        NVME_CMD_RESERVATION_REGISTER   = 0x0D,
        NVME_CMD_RESERVATION_REPORT     = 0x0E,
        NVME_CMD_RESERVATION_ACQUIRE    = 0x11,
        NVME_CMD_RESERVATION_RELEASE    = 0x15,
    } eNvmeOPCodes;

    typedef struct _nvmCommand {
        uint8_t 			opcode; //CDW0
        uint8_t 			flags; //CDW0
        uint16_t 			commandId; //CDW0
        uint32_t 			nsid; //CDW1
        uint32_t 			cdw2; //CDW2
        uint32_t 			cdw3; //CDW3
        uint64_t 			metadata; //CDW4 & 5
        uint64_t 			prp1; //CDW6 & 7
        uint64_t 			prp2; //CDW8 & 9
        uint32_t 			cdw10;//CDW10
        uint32_t 			cdw11;//CDW11
        uint32_t 			cdw12;//CDW12
        uint32_t 			cdw13;//CDW13
        uint32_t 			cdw14;//CDW14
        uint32_t 			cdw15;//CDW15
    } nvmCommand;

    typedef struct _nvmeRWCommand {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			nsid;
    	uint64_t			rsvd2;
    	uint64_t 			metadata;
    	uint64_t 			prp1;
    	uint64_t 			prp2;
    	uint64_t 			slba;
    	uint16_t 			length;
    	uint16_t 			control;
    	uint32_t 			dsmgmt;
    	uint32_t 			reftag;
    	uint16_t 			apptag;
    	uint16_t 			appmask;
    } nvmeRWCommand;

    typedef enum _eNvmeRWCmds {
    	NVME_RW_LR_			= 1 << 15,
    	NVME_RW_FUA_			= 1 << 14,
    	NVME_RW_DSM_FREQ_UNSPEC_		= 0,
    	NVME_RW_DSM_FREQ_TYPICAL_	= 1,
    	NVME_RW_DSM_FREQ_RARE_		= 2,
    	NVME_RW_DSM_FREQ_READS_		= 3,
    	NVME_RW_DSM_FREQ_WRITES_		= 4,
    	NVME_RW_DSM_FREQ_RW_		= 5,
    	NVME_RW_DSM_FREQ_ONCE_		= 6,
    	NVME_RW_DSM_FREQ_PREFETCH_	= 7,
    	NVME_RW_DSM_FREQ_TEMP_		= 8,
    	NVME_RW_DSM_LATENCY_NONE_	= 0 << 4,
    	NVME_RW_DSM_LATENCY_IDLE_	= 1 << 4,
    	NVME_RW_DSM_LATENCY_NORM_	= 2 << 4,
    	NVME_RW_DSM_LATENCY_LOW_		= 3 << 4,
    	NVME_RW_DSM_SEQ_REQ_		= 1 << 6,
    	NVME_RW_DSM_COMPRESSED_		= 1 << 7,
    } eNvmeRWCmds;

    typedef struct _nvmeDsmCmd {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			nsid;
    	uint64_t			rsvd2[2];
    	uint64_t 			prp1;
    	uint64_t 			prp2;
    	uint32_t 			nr;
    	uint32_t 			attributes;
    	uint32_t 			rsvd12[4];
    } nvmeDsmCmd;

    typedef enum _eNvmeDsmCmds{
    	NVME_DSMGMT_IDR_		= 1 << 0,
    	NVME_DSMGMT_IDW_		= 1 << 1,
    	NVME_DSMGMT_AD_		= 1 << 2,
    } eNvmeDsmCmds;

    typedef struct _nvmeDsmRange {
    	uint32_t 			cattr;
    	uint32_t 			nlb;
    	uint64_t 			slba;
    } nvmeDsmRange;

    // NVMe Spec - Figure 62: Firmware Commit - Command Dword 10
    typedef enum _nvmeFWCommitAction {
        NVME_CA_REPLACE_NOT_ACTIVITED = 0,
        NVME_CA_REPLACE_ACTIVITE_ON_RST = 1,
        NVME_CA_ACTIVITE_ON_RST = 2,
        NVME_CA_ACTIVITE_IMMEDIATE = 3,
        NVME_CA_INVALID
    } nvmeFWCommitAction;

    // NVMe Spec - Figure 63: Firmware Commit - Command Specific Status Values
    typedef enum _nvmeFWCommitRC {
        NVME_FW_DL_INVALID_SLOT         = 0x06,
        NVME_FW_DL_INVALID_IMG          = 0x07,
        NVME_FW_DL_REQUIRES_SYS_RST     = 0x0B,
        NVME_FW_DL_REQUIRES_NVM_RST     = 0x10,
        NVME_FW_DL_ON_NEXT_RST          = 0x11,
        NVME_FW_DL_MAX_TIME_VIOLATION   = 0x12,
        NVME_FW_DL_ACT_PROHIBITED       = 0x13,
        NVME_FW_DL_OVERLAPPING_RANGE    = 0x14,
    } nvmeFWCommitRC;

    /* Admin commands */

    typedef enum _eNVMeAdminOpCodes {
    	NVME_ADMIN_CMD_DELETE_SQ		            = 0x00,
    	NVME_ADMIN_CMD_CREATE_SQ		            = 0x01,
    	NVME_ADMIN_CMD_GET_LOG_PAGE		            = 0x02,
    	NVME_ADMIN_CMD_DELETE_CQ		            = 0x04,
    	NVME_ADMIN_CMD_CREATE_CQ		            = 0x05,
    	NVME_ADMIN_CMD_IDENTIFY		                = 0x06,
    	NVME_ADMIN_CMD_ABORT_CMD		            = 0x08,
    	NVME_ADMIN_CMD_SET_FEATURES		            = 0x09,
    	NVME_ADMIN_CMD_GET_FEATURES		            = 0x0A,
    	NVME_ADMIN_CMD_ASYNC_EVENT		            = 0x0C,
        NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT         = 0x0D,
    	NVME_ADMIN_CMD_ACTIVATE_FW		            = 0x10,
    	NVME_ADMIN_CMD_DOWNLOAD_FW		            = 0x11,
        NVME_ADMIN_CMD_DEVICE_SELF_TEST             = 0x14,
        NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT         = 0x15,
        NVME_ADMIN_CMD_KEEP_ALIVE                   = 0x18,
        NVME_ADMIN_CMD_DIRECTIVE_SEND               = 0x19,
        NVME_ADMIN_CMD_DIRECTIVE_RECEIVE            = 0x1A,
        NVME_ADMIN_CMD_VIRTUALIZATION_MANAGEMENT    = 0x1C,
        NVME_ADMIN_CMD_NVME_MI_SEND                 = 0x1D,
        NVME_ADMIN_CMD_NVME_MI_RECEIVE              = 0x1E,
        NVME_ADMIN_CMD_DOORBELL_BUFFER_CONFIG       = 0x7C,
        NVME_ADMIN_CMD_NVME_OVER_FABRICS            = 0x7F,
    	NVME_ADMIN_CMD_FORMAT_NVM		            = 0x80,
    	NVME_ADMIN_CMD_SECURITY_SEND	            = 0x81,
    	NVME_ADMIN_CMD_SECURITY_RECV	            = 0x82,
        NVME_ADMIN_CMD_SANITIZE                     = 0x84,
    } eNVMeAdminOpCodes;

    typedef enum _eNvmeFeatures {
    	NVME_QUEUE_PHYS_CONTIG_	= (1 << 0),
    	NVME_CQ_IRQ_ENABLED_	= (1 << 1),
    	NVME_SQ_PRIO_URGENT_	= (0 << 1),
    	NVME_SQ_PRIO_HIGH_	= (1 << 1),
    	NVME_SQ_PRIO_MEDIUM_	= (2 << 1),
    	NVME_SQ_PRIO_LOW_	= (3 << 1),
    	NVME_FEAT_ARBITRATION_	= 0x01,
    	NVME_POWER_MGMT_FEAT	= 0x02,
    	NVME_FEAT_LBA_RANGE_	= 0x03,
    	NVME_FEAT_TEMP_THRESH_	= 0x04,
    	NVME_FEAT_ERR_RECOVERY_	= 0x05,
    	NVME_FEAT_VOLATILE_WC_	= 0x06,
    	NVME_FEAT_NUM_QUEUES_	= 0x07,
    	NVME_FEAT_IRQ_COALESCE_	= 0x08,
    	NVME_FEAT_IRQ_CONFIG_	= 0x09,
    	NVME_FEAT_WRITE_ATOMIC_	= 0x0a,
    	NVME_FEAT_ASYNC_EVENT_	= 0x0b,
    	NVME_FEAT_SW_PROGRESS_	= 0x0c,
    	NVME_LOG_ERROR_ID   	= 0x01,
    	NVME_LOG_SMART_ID		= 0x02,
    	NVME_LOG_FW_SLOT_ID	    = 0x03,
    	NVME_LOG_RESERVATION_ID	= 0x80,
    	NVME_FWACT_REPL_		= (0 << 3),
    	NVME_FWACT_REPL_ACTV_	= (1 << 3),
    	NVME_FWACT_ACTV_		= (2 << 3),
    } eNvmeFeatures;


    typedef enum _eNvmeFeaturesSelectValue {
        NVME_CURRENT_FEAT_SEL,
        NVME_DEFAULT_FEAT_SEL,
        NVME_SAVED_FEAT_SEL,
        NVME_SUPPORTED_FEAT_SEL
    } eNvmeFeaturesSelectValue;

    typedef struct _nvmeGetLogPageCmdOpts {
        uint32_t    nsid;
    	uint64_t	metadata; // MPTR
    	//uint64_t	addr;   //PRP Entry 1
        uint8_t	    *addr;   //PRP Entry 1
    	uint32_t 	metadataLen;
    	uint32_t 	dataLen;
        uint8_t     lid; //Log Page identifier, part of Command Dword 10(CDW10)
    } nvmeGetLogPageCmdOpts;

    /* This Command options structure is common for both Get/Set
       According to nvme spec prp1, prp2, Dword 10 and Dword 11
       are used in the Admin Get/Set features commands
    */
    typedef struct _nvmeFeaturesCmdOpt {
        uint64_t    prp1;
        uint64_t    prp2;
        //Following are part of Dword 10 in nvmeSpec 
        uint8_t     sv; // Save Value Used for Set Features command as Bit 31 
        uint8_t     rsvd; // //this part is reserved for both Get/Set Features
        uint8_t     sel; // Select Value SEL Bit 10:08 are used in Get Features. 
        uint8_t     fid; // Feature Identifier used for both Get/Set
        uint32_t    featSetGetValue; //Value returned or to be set as Dword11
    } nvmeFeaturesCmdOpt;

    typedef struct _nvmeIdentify {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			nsid;
    	uint64_t			rsvd2[2];
    	uint64_t 			prp1;
    	uint64_t 			prp2;
    	uint32_t 			cns;
    	uint32_t 			rsvd11[5];
    } nvmeIdentify;

    typedef struct _nvmeFeatures {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			nsid;
    	uint64_t			rsvd2[2];
    	uint64_t 			prp1;
    	uint64_t 			prp2;
    	uint32_t 			fid;
    	uint32_t 			dword11;
    	uint32_t 			rsvd12[4];
    } nvmeFeatures;

    typedef struct _nvmeCreateCQ {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			rsvd1[5];
    	uint64_t 			prp1;
    	uint64_t			rsvd8;
    	uint16_t 			cqid;
    	uint16_t 			qsize;
    	uint16_t 			cqFlags;
    	uint16_t 			irq_vector;
    	uint32_t 			rsvd12[4];
    } nvmeCreateCQ;

    typedef struct _nvmeCreateSQ {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			rsvd1[5];
    	uint64_t 			prp1;
    	uint64_t			rsvd8;
    	uint16_t 			sqid;
    	uint16_t 			qsize;
    	uint16_t 			sqFlags;
    	uint16_t 			cqid;
    	uint32_t 			rsvd12[4];
    } nvmeCreateSQ;

    typedef struct _nvmeDeleteQueue {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			rsvd1[9];
    	uint16_t 			qid;
    	uint16_t 			rsvd10;
    	uint32_t 			rsvd11[5];
    } nvmeDeleteQueue;

    typedef struct _nvmeAbortCmd {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			rsvd1[9];
    	uint16_t 			sqid;
    	uint16_t 			cid;
    	uint32_t 			rsvd11[5];
    } nvmeAbortCmd;

    typedef struct _nvmeDownloadFirmware {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			rsvd1[5];
    	uint64_t 			prp1;
    	uint64_t 			prp2;
    	uint32_t 			numd;
    	uint32_t 			offset;
    	uint32_t 			rsvd12[4];
    } nvmeDownloadFirmware;

    typedef enum _eProtectionInfoLoc {
        PROTECTION_INFO_AT_END,
        PROTECTION_INFO_AT_BEGIN
    } eProtectionInfoLoc;

    typedef struct _nvmeFormatCmdOpts {
        uint32_t            nsid; //Can be 0xFFFFFFFF to apply to all namespaces
        uint8_t             lbaf; //nible that contains a 0-16 LBA format to apply 
        uint8_t             ms;   //Metadata Setting. 1 if metadata is xfered as part of extended lba. 
        uint8_t             pi;   //Protection Information. 0=none, 1=Type1 PI, 2=Type2, 3=Type3 
        uint8_t             pil;  //Protection Information Location. 1=PI xfered as first 8 bytes of metadata
        uint8_t             ses;  //Secure Erase Settings. 0=none requested, 1=requested, 2=use Crtypto Erase
    } nvmeFormatCmdOpts;

    typedef struct _nvmeFormatCMD {
    	uint8_t 			opcode;
    	uint8_t 			flags;
    	uint16_t 			commandId;
    	uint32_t 			nsid;
    	uint64_t			rsvd2[4];
    	uint32_t 			cdw10;
    	uint32_t 			rsvd11[5];
    } nvmeFormatCMD;

    typedef enum _eNvmeReturnStatus {
    	NVME_SC_SUCCESS_			= 0x0,
    	NVME_SC_INVALID_OPCODE_		= 0x1,
    	NVME_SC_INVALID_FIELD_		= 0x2,
    	NVME_SC_CMDID_CONFLICT_		= 0x3,
    	NVME_SC_DATA_XFER_ERROR_		= 0x4,
    	NVME_SC_POWER_LOSS_		= 0x5,
    	NVME_SC_INTERNAL_		= 0x6,
    	NVME_SC_ABORT_REQ_		= 0x7,
    	NVME_SC_ABORT_QUEUE_		= 0x8,
    	NVME_SC_FUSED_FAIL_		= 0x9,
    	NVME_SC_FUSED_MISSING_		= 0xa,
    	NVME_SC_INVALID_NS_		= 0xb,
    	NVME_SC_CMD_SEQ_ERROR_		= 0xc,
    	NVME_SC_LBA_RANGE_		= 0x80,
    	NVME_SC_CAP_EXCEEDED_		= 0x81,
    	NVME_SC_NS_NOT_READY_		= 0x82,
    	NVME_SC_CQ_INVALID_		= 0x100,
    	NVME_SC_QID_INVALID_		= 0x101,
    	NVME_SC_QUEUE_SIZE_		= 0x102,
    	NVME_SC_ABORT_LIMIT_		= 0x103,
    	NVME_SC_ABORT_MISSING_		= 0x104,
    	NVME_SC_ASYNC_LIMIT_		= 0x105,
    	NVME_SC_FIRMWARE_SLOT_		= 0x106,
    	NVME_SC_FIRMWARE_IMAGE_		= 0x107,
    	NVME_SC_INVALID_VECTOR_		= 0x108,
    	NVME_SC_INVALID_LOG_PAGE_	= 0x109,
    	NVME_SC_INVALID_FORMAT_		= 0x10a,
    	NVME_SC_BAD_ATTRIBUTES_		= 0x180,
    	NVME_SC_WRITE_FAULT_		= 0x280,
    	NVME_SC_READ_ERROR_		= 0x281,
    	NVME_SC_GUARD_CHECK_		= 0x282,
    	NVME_SC_APPTAG_CHECK_		= 0x283,
    	NVME_SC_REFTAG_CHECK_		= 0x284,
    	NVME_SC_COMPARE_FAILED_		= 0x285,
    	NVME_SC_ACCESS_DENIED_		= 0x286,
    	NVME_SC_DNR_			= 0x4000,
    } eNvmeReturnStatus;

    typedef struct _nvmeCompletion {
    	uint32_t 	result;		/* Used by admin commands to return data */
    	uint32_t 	rsvd;
    	uint16_t 	sqHead;	/* how much of this queue may be reclaimed */
    	uint16_t 	sqId;		/* submission queue that generated this entry */
    	uint16_t 	commandId;	/* of the command which completed */
    	uint16_t 	status;		/* did the command fail, and if so, why? */
    } nvmeCompletion;

    typedef struct _nvmeUserIO {
    	uint8_t 	opcode;
    	uint8_t 	flags;
    	uint16_t 	control;
    	uint16_t 	nblocks;
    	uint16_t 	rsvd;
    	uint64_t	metadata;
    	uint64_t	addr;
    	uint64_t	slba;
    	uint32_t 	dsmgmt;
    	uint32_t 	reftag;
    	uint16_t 	apptag;
    	uint16_t 	appmask;
    } nvmeUserIO;

    typedef struct _nvmeAdminCommand {
    	uint8_t 	opcode; //Common Dword 0 (CDW0)
    	uint8_t 	flags; //Common Dword 0 (CDW0) 
    	uint16_t 	rsvd1; //Common Dword 0 (CDW0)
    	uint32_t 	nsid;  //Namespace Identifier (NSID)
    	uint32_t 	cdw2; // Reserved?
    	uint32_t 	cdw3; // Reserved?
    	uint64_t	metadata; // MPTR
    	uint64_t	addr;   //PRP Entry 1
    	uint32_t 	metadataLen;
    	uint32_t 	dataLen;
    	uint32_t 	cdw10; //Command Dword 10(CDW10)
    	uint32_t 	cdw11; //Command Dword 10(CDW10)
    	uint32_t 	cdw12; //Command Dword 10(CDW10)
    	uint32_t 	cdw13; //Command Dword 10(CDW10)
    	uint32_t 	cdw14; //Command Dword 10(CDW10)
    	uint32_t 	cdw15; //Command Dword 10(CDW10)
    } nvmeAdminCommand;

    typedef struct _nvmeCommands {
        union {
            nvmeAdminCommand adminCmd; //Use for commands part of the Admin command set
            nvmCommand nvmCmd; //Use for commands part of the NVM command set
        };
    } nvmeCommands;

    // \struct typedef struct _nvmeCmdCtx
    typedef struct _nvmeCmdCtx
    {
        tDevice                 *device;
        eNvmeCmdType            commandType;
        eDataTransferDirection  commandDirection;
        nvmeCommands            cmd;
        uint8_t                 *ptrData;
        uint32_t                dataSize;
        uint32_t                timeout; //in seconds 
        uint32_t                result; //For Admin commands
        bool                    useSpecificNSID;//This MUST be used to be compatible with Windows 10 API. Set to true when attempting to read a specific NSID that is NOT the same as the current handle. I.E. getting namespace data from a different namespace
                                                //This is primarily used for the vendor unique pass-through, 
                                                //but may be checked otherwise since Win10 API only talks to the current NSID, 
                                                //unless you are pulling a log or identify data from the controller. - TJE
        //uint8_t                 reserved1[3];
        //tDevice                 *device;
    } nvmeCmdCtx;


#if defined (__cplusplus)
}
#endif

#endif
