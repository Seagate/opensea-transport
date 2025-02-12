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

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "sort_and_search.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "common_public.h"
#include "nvme_helper.h"
#include "nvme_helper_func.h"
#include "platform_helper.h"
#include <stdio.h>

// pointer to NVMe controller identify data should always be 4096B since all NVMe identify data is this long.
// All parameters should be the length of what is required in NVMe spec + 1 for a M_NULLPTR terminating character.
static void fill_NVMe_Strings_From_Ctrl_Data(uint8_t* ptrCtrlData,
                                             char     nvmMN[NVME_CTRL_IDENTIFY_MN_LEN + 1],
                                             char     nvmSN[NVME_CTRL_IDENTIFY_SN_LEN + 1],
                                             char     nvmFW[NVME_CTRL_IDENTIFY_FW_LEN + 1])
{
    if (ptrCtrlData)
    {
        nvmeIDCtrl* ctrlData = C_CAST(nvmeIDCtrl*, ptrCtrlData);
        // make sure buffers all all zeroed out before filling them
        safe_memset(nvmMN, NVME_CTRL_IDENTIFY_MN_LEN + 1, 0, NVME_CTRL_IDENTIFY_MN_LEN + 1);
        safe_memset(nvmSN, NVME_CTRL_IDENTIFY_SN_LEN + 1, 0, NVME_CTRL_IDENTIFY_SN_LEN + 1);
        safe_memset(nvmFW, NVME_CTRL_IDENTIFY_FW_LEN + 1, 0, NVME_CTRL_IDENTIFY_FW_LEN + 1);
        // fill each buffer with data from NVMe ctrl data
        safe_memcpy(nvmSN, NVME_CTRL_IDENTIFY_SN_LEN + 1, ctrlData->sn, NVME_CTRL_IDENTIFY_SN_LEN);
        remove_Leading_And_Trailing_Whitespace(nvmSN);
        safe_memcpy(nvmFW, NVME_CTRL_IDENTIFY_FW_LEN + 1, ctrlData->fr, NVME_CTRL_IDENTIFY_FW_LEN);
        remove_Leading_And_Trailing_Whitespace(nvmFW);
        safe_memcpy(nvmMN, NVME_CTRL_IDENTIFY_MN_LEN + 1, ctrlData->mn, NVME_CTRL_IDENTIFY_MN_LEN);
        remove_Leading_And_Trailing_Whitespace(nvmMN);
    }
}

// \file nvme_cmds.c   Implementation for NVM Express helper functions
//                     The intention of the file is to be generic & not OS specific

// \fn fill_In_NVMe_Device_Info(device device)
// \brief Sends a set Identify etc commands & fills in the device information
// \param device device struture
// \return SUCCESS - pass, !SUCCESS fail or something went wrong
eReturnValues fill_In_NVMe_Device_Info(tDevice* device)
{
    eReturnValues ret = UNKNOWN;

    // set some pointers to where we want to fill in information...we're doing this so that on USB, we can store some
    // info about the child drive, without disrupting the standard drive_info that has already been filled in by the
    // fill_SCSI_Info function
    uint64_t* fillWWN                = &device->drive_info.worldWideName;
    uint32_t* fillLogicalSectorSize  = &device->drive_info.deviceBlockSize;
    uint32_t* fillPhysicalSectorSize = &device->drive_info.devicePhyBlockSize;
    uint16_t* fillSectorAlignment    = &device->drive_info.sectorAlignment;
    uint64_t* fillMaxLba             = &device->drive_info.deviceMaxLba;

    // If not an NVMe interface, such as USB, then we need to store things differently
    // RAID Interface should be treated as "Native" or "NVME_INTERFACE" since there is likely an underlying API
    // providing direct access of some kind.
    if (device->drive_info.interface_type != NVME_INTERFACE && device->drive_info.interface_type != RAID_INTERFACE)
    {
        device->drive_info.bridge_info.isValid = true;
        fillWWN                                = &device->drive_info.bridge_info.childWWN;
        fillLogicalSectorSize                  = &device->drive_info.bridge_info.childDeviceBlockSize;
        fillPhysicalSectorSize                 = &device->drive_info.bridge_info.childDevicePhyBlockSize;
        fillSectorAlignment                    = &device->drive_info.bridge_info.childSectorAlignment;
        fillMaxLba                             = &device->drive_info.bridge_info.childDeviceMaxLba;
    }

    nvmeIDCtrl*       ctrlData = &device->drive_info.IdentifyData.nvme.ctrl; // Controller information data structure
    nvmeIDNameSpaces* nsData   = &device->drive_info.IdentifyData.nvme.ns;   // Name Space Data structure

#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif

    ret = nvme_Identify(device, C_CAST(uint8_t*, ctrlData), 0, NVME_IDENTIFY_CTRL);

#ifdef _DEBUG
    printf("fill NVMe info ret = %d\n", ret);
#endif

    if (ret == SUCCESS)
    {
        uint16_t enduranceGroup = UINT16_C(0);
        if (device->drive_info.interface_type != NVME_INTERFACE && device->drive_info.interface_type != RAID_INTERFACE)
        {
            fill_NVMe_Strings_From_Ctrl_Data(C_CAST(uint8_t*, ctrlData), device->drive_info.bridge_info.childDriveMN,
                                             device->drive_info.bridge_info.childDriveSN,
                                             device->drive_info.bridge_info.childDriveFW);
        }
        else
        {
            fill_NVMe_Strings_From_Ctrl_Data(C_CAST(uint8_t*, ctrlData), device->drive_info.product_identification,
                                             device->drive_info.serialNumber, device->drive_info.product_revision);
        }
        // set the t10 vendor id to NVMe
        snprintf_err_handle(device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1, "NVMe");
        device->drive_info.media_type =
            MEDIA_NVM; // This will bite us someday when someone decided to put non-ssds on NVMe interface.
        // set scsi version to 6 if it is not already set
        if (device->drive_info.scsiVersion == 0)
        {
            device->drive_info.scsiVersion = 6; // most likely this is what will be set by a translator and keep other
                                                // parts of code working correctly
        }

        // Do not overwrite this with non-NVMe interfaces. This is used by USB to figure out and track bridge chip
        // specific things that are stored in this location
        if (device->drive_info.interface_type == NVME_INTERFACE && !device->drive_info.adapter_info.vendorIDValid)
        {
            device->drive_info.adapter_info.vendorID      = ctrlData->vid;
            device->drive_info.adapter_info.vendorIDValid = true;
        }
        // set the IEEE OUI into the WWN since we use the WWN for detecting if the drive is a Seagate drive.
        // This is a shortcut to verify this is a Seagate drive later. There is a SCSI translation whitepaper we can
        // follow, but that will be more complicated of a change due to the different formatting used.
        *fillWWN = M_BytesTo8ByteValue(0x05, ctrlData->ieee[2], ctrlData->ieee[1], ctrlData->ieee[0], 0, 0, 0, 0) << 4;

        ret = nvme_Identify(device, C_CAST(uint8_t*, nsData), device->drive_info.namespaceID, NVME_IDENTIFY_NS);

        if (ret == SUCCESS)
        {
            uint8_t flbas = get_bit_range_uint8(nsData->flbas, 3, 0);
            // get the LBAF number. THis field varies depending on other things reported by the drive in NVMe 2.0
            if (nsData->nlbaf > 16)
            {
                // need to append 2 more bits to interpret this correctly since number of formats > 16
                flbas |= get_bit_range_uint8(nsData->flbas, 6, 5) << 4;
            }
            *fillLogicalSectorSize  = C_CAST(uint32_t, power_Of_Two(nsData->lbaf[flbas].lbaDS));
            *fillPhysicalSectorSize = *fillLogicalSectorSize; // True for NVMe?
            *fillSectorAlignment    = 0;

            *fillMaxLba = nsData->nsze - 1; // spec says this is from 0 to (n-1)!

            enduranceGroup = nsData->endgid;
            if (ctrlData->lpa & BIT5 && ctrlData->ctratt & BIT4 && enduranceGroup > 0)
            {
                // Check if this is an HDD
                // First read the supported logs log page, then if the rotating media log is there, read it.
                uint8_t* supportedLogs = M_REINTERPRET_CAST(
                    uint8_t*, safe_calloc_aligned(1024, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (supportedLogs)
                {
                    nvmeGetLogPageCmdOpts supLogs;
                    safe_memset(&supLogs, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
                    supLogs.addr    = supportedLogs;
                    supLogs.dataLen = 1024;
                    supLogs.lid     = NVME_LOG_SUPPORTED_PAGES_ID;
                    if (SUCCESS == nvme_Get_Log_Page(device, &supLogs))
                    {
                        uint32_t rotMediaOffset = NVME_LOG_ROTATIONAL_MEDIA_INFORMATION_ID * 4;
                        uint32_t rotMediaSup =
                            M_BytesTo4ByteValue(supportedLogs[rotMediaOffset + 3], supportedLogs[rotMediaOffset + 2],
                                                supportedLogs[rotMediaOffset + 1], supportedLogs[rotMediaOffset + 0]);
                        if (rotMediaSup & BIT0)
                        {
                            // rotational media log is supported.
                            // Set the media type because this is supported, at least for now. We can read the log and
                            // the actual rotation rate if needed.
                            device->drive_info.media_type = MEDIA_HDD;
                        }
                    }
                    safe_free_aligned(&supportedLogs);
                }
            }
        }
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif

    return ret;
}

void print_NVMe_Cmd_Verbose(const nvmeCmdCtx* cmdCtx)
{
    printf("Sending NVM Command:\n");
    printf("\tType: ");
    switch (cmdCtx->commandType)
    {
    case NVM_ADMIN_CMD:
        printf("Admin");
        break;
    case NVM_CMD:
        printf("NVM");
        break;
    case NVM_UNKNOWN_CMD_SET:
    default:
        printf("Unknown");
        break;
    }
    printf("\n");
    printf("\tData Direction: ");
    // Data Direction:
    switch (cmdCtx->commandDirection)
    {
    case XFER_NO_DATA:
        printf("No Data");
        break;
    case XFER_DATA_IN:
        printf("Data In");
        break;
    case XFER_DATA_OUT:
        printf("Data Out");
        break;
    default:
        printf("Unknown");
        break;
    }
    printf("\n");
    printf("Data Length: %" PRIu32 "\n", cmdCtx->dataSize);
    // printf("Cmd result 0x%02X\n", cmdCtx->result);
    printf("Command Bytes:\n");
    switch (cmdCtx->commandType)
    {
    case NVM_ADMIN_CMD:
        printf("\tOpcode (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.adminCmd.opcode);
        printf("\tFlags (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.adminCmd.flags);
        printf("\tReserved (CDW0) = %" PRIu16 "\n", cmdCtx->cmd.adminCmd.rsvd1);
        printf("\tNSID = %" PRIX32 "h\n", cmdCtx->cmd.adminCmd.nsid);
        printf("\tCDW2 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw2);
        printf("\tCDW3 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw3);
        printf("\tMetadata Ptr = %" PRIX64 "h\n", cmdCtx->cmd.adminCmd.metadata);
        printf("\tMetadata Length = %" PRIu32 "\n", cmdCtx->cmd.adminCmd.metadataLen);
        printf("\tData Ptr = %" PRIX64 "h\n", cmdCtx->cmd.adminCmd.addr);
        printf("\tCDW10 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw10);
        printf("\tCDW11 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw11);
        printf("\tCDW12 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw12);
        printf("\tCDW13 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw13);
        printf("\tCDW14 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw14);
        printf("\tCDW15 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw15);
        break;
    case NVM_CMD:
        printf("\tOpcode (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.nvmCmd.opcode);
        printf("\tFlags (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.nvmCmd.flags);
        printf("\tCommand ID (CDW0) = %" PRIu16 "\n", cmdCtx->cmd.nvmCmd.commandId);
        printf("\tNSID = %" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.nsid);
        printf("\tCDW2 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw2);
        printf("\tCDW3 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw3);
        printf("\tMetadata Ptr (CDW4 & 5) = %" PRIX64 "h\n", cmdCtx->cmd.nvmCmd.metadata);
        printf("\tData Pointer (CDW6 & 7) = %" PRIX64 "h\n", cmdCtx->cmd.nvmCmd.prp1);
        printf("\tData Pointer (CDW8 & 9) = %" PRIX64 "h\n", cmdCtx->cmd.nvmCmd.prp2);
        printf("\tCDW10 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw10);
        printf("\tCDW11 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw11);
        printf("\tCDW12 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw12);
        printf("\tCDW13 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw13);
        printf("\tCDW14 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw14);
        printf("\tCDW15 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw15);
        break;
    default:
        printf("\tCDW0  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw0);
        printf("\tCDW1  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw1);
        printf("\tCDW2  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw2);
        printf("\tCDW3  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw3);
        printf("\tCDW4  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw4);
        printf("\tCDW5  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw5);
        printf("\tCDW6  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw6);
        printf("\tCDW7  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw7);
        printf("\tCDW8  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw8);
        printf("\tCDW9  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw9);
        printf("\tCDW10 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw10);
        printf("\tCDW11 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw11);
        printf("\tCDW12 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw12);
        printf("\tCDW13 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw13);
        printf("\tCDW14 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw14);
        printf("\tCDW15 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw15);
        break;
    }
    printf("\n");
}

void get_NVMe_Status_Fields_From_DWord(uint32_t nvmeStatusDWord,
                                       bool*    doNotRetry,
                                       bool*    more,
                                       uint8_t* statusCodeType,
                                       uint8_t* statusCode)
{
    DISABLE_NONNULL_COMPARE
    if (doNotRetry != M_NULLPTR && more != M_NULLPTR && statusCodeType != M_NULLPTR && statusCode != M_NULLPTR)
    {
        *doNotRetry     = nvmeStatusDWord & BIT31;
        *more           = nvmeStatusDWord & BIT30;
        *statusCodeType = get_8bit_range_uint32(nvmeStatusDWord, 27, 25);
        *statusCode     = get_8bit_range_uint32(nvmeStatusDWord, 24, 17);
    }
    RESTORE_NONNULL_COMPARE
}

// Status codes must be in numeric order!
// These will be binary searched so out of order will break binary search!
static nvmeStatus nvmeStatusLookup[] = {
    // Generic status codes first
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SUCCESS_, SUCCESS, "Success"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_OPCODE_, NOT_SUPPORTED, "Invalid Command Opcode"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_FIELD_, NOT_SUPPORTED, "Invalid Field in Command"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_CMDID_CONFLICT_, FAILURE, "Command ID Conflict"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_DATA_XFER_ERROR_, FAILURE, "Data Transfer Error"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_POWER_LOSS_, FAILURE,
     "Commands Aborted due to Power Less Notification"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INTERNAL_, FAILURE, "Internal Error"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_ABORT_REQ_, FAILURE, "Command Abort Requested"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_ABORT_QUEUE_, ABORTED,
     "Command Aborted due to Submission Queue Deletion"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_FUSED_FAIL_, ABORTED, "Command Aborted due to Failed Fused Command"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_FUSED_MISSING_, FAILURE,
     "Command Aborted due to Missing Fused Command"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_NS_, NOT_SUPPORTED, "Invalid Namespace or Format"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_CMD_SEQ_ERROR_, FAILURE, "Command Sequence Error"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_SGL_SEGMENT_DESCRIPTOR, NOT_SUPPORTED,
     "Invalid SGL Segment Descriptor"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_NUMBER_OF_SGL_DESCRIPTORS, NOT_SUPPORTED,
     "Invalid Number of SGL Descriptors"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_DATA_SGL_LENGTH_INVALID, NOT_SUPPORTED, "Data SGL Length Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_METADATA_SGL_LENGTH_INVALID, NOT_SUPPORTED,
     "Metadata SGL Length Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SGL_DESCRIPTOR_TYPE_INVALID, NOT_SUPPORTED,
     "SGL Descriptor Type Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_USE_OF_CONTROLLER_MEMORY_BUFFER, NOT_SUPPORTED,
     "Invalid Use of Controller Memory Buffer"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_PRP_OFFSET_INVALID, NOT_SUPPORTED, "PRP Offset Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_ATOMIC_WRITE_UNIT_EXCEEDED, FAILURE, "Atomic Write Unit Exceeded"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_OPERATION_DENIED, DEVICE_ACCESS_DENIED, "Operation Denied"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SGL_OFFSET_INVALID, NOT_SUPPORTED, "SGL Offset Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_HOST_IDENTIFIER_INCONSISTENT_FORMAT, FAILURE,
     "Host Identifier Inconsistent Format"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_EXPIRED, FAILURE, "Keep Alive Timeout Expired"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_INVALID, NOT_SUPPORTED,
     "Keep Alive Timeout Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_COMMAND_ABORTED_DUE_TO_PREEMPT_AND_ABORT, ABORTED,
     "Command Aborted Due To Preempt And Abort"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SANITIZE_FAILED, FAILURE, "Sanitize Failed"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SANITIZE_IN_PROGRESS, IN_PROGRESS, "Sanitize In Progress"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SGL_DATA_BLOCK_GRANULARITY_INVALID, NOT_SUPPORTED,
     "SGL Data Block Granularity Invalid"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_COMMAND_NOT_SUPPORTED_FOR_QUEUE_IN_CMB, NOT_SUPPORTED,
     "Command Not Supported for Queue in Controller Memory Buffer"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_NS_IS_WRITE_PROTECTED, FAILURE, "Namespace is Write Protected"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_COMMAND_INTERRUPTED, ABORTED, "Command Interrupted"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_TRANSIENT_TRANSPORT_ERROR, FAILURE, "Transient Transport Error"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_COMMAND_PROHIBITED_BY_CMD_AND_FEAT_LOCKDOWN, DEVICE_ACCESS_DENIED,
     "Command Prohibited by Command and Feature Lockdown"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_ADMIN_COMMAND_MEDIA_NOT_READY, FAILURE,
     "Admin Command Media Not Ready"},
    // 80-BF are NVM command set specific
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_LBA_RANGE_, FAILURE, "LBA Out Of Range"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_CAP_EXCEEDED_, FAILURE, "Capacity Exceeded"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_NS_NOT_READY_, FAILURE, "Namespace Not Ready"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_RESERVATION_CONFLICT, FAILURE, "Reservation Conflict"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_FORMAT_IN_PROGRESS, IN_PROGRESS, "Format In Progress"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_VALUE_SIZE, NOT_SUPPORTED, "Invalid Value Size"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_KEY_SIZE, NOT_SUPPORTED, "Invalid Key Size"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_KV_KEY_DOES_NOT_EXIST, NOT_SUPPORTED, "KV Key Does Not Exist"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_UNRECOVERED_ERROR, FAILURE, "Unrecovered Error"},
    {NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_KEY_EXISTS, FAILURE, "Key Exists"},
    // Command set specific status codes
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_CQ_INVALID_, FAILURE, "Completion Queue Invalid"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_QID_INVALID_, FAILURE, "Invalid Queue Identifier"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_QUEUE_SIZE_, FAILURE, "Invalid Queue Size"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ABORT_LIMIT_, FAILURE, "Aborted Command Limit Exceeded"},
    //{ NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ABORT_MISSING_, FAILURE, "Abort Missing"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ASYNC_LIMIT_, FAILURE,
     "Asynchronous Event Request Limit Exceeded"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_FIRMWARE_SLOT_, FAILURE, "Invalid Firmware Slot"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALIDFIRMWARE_IMAGE_, FAILURE, "Invalid Firmware Image"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_INTERRUPT_VECTOR_, NOT_SUPPORTED,
     "Invalid Interrupt Vector"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_LOG_PAGE_, NOT_SUPPORTED, "Invalid Log Page"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_FORMAT_, NOT_SUPPORTED, "Invalid Format"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET, SUCCESS,
     "Firmware Activation Requires Conventional Reset"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_QUEUE_DELETION, FAILURE, "Invalid Queue Deletion"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FEATURE_IDENTIFIER_NOT_SAVABLE, FAILURE,
     "Feature Identifier Not Savable"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FEATURE_NOT_CHANGEABLE, FAILURE, "Feature Not Changeable"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FEATURE_NOT_NAMESPACE_SPECIFC, FAILURE,
     "Feature Not Namespace Specific"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET, SUCCESS,
     "Firmware Activation Requires NVM Subsystem Reset"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FW_ACT_REQ_RESET, SUCCESS, "Firmware Activation Requires Reset"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FW_ACT_REQ_MAX_TIME_VIOALTION, SUCCESS,
     "Firmware Activation Requires Max Time Violation"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_FW_ACT_PROHIBITED, FAILURE, "Firmware Activation Prohibited"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_OVERLAPPING_RANGE, FAILURE, "Overlapping Range"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_NS_INSUFFICIENT_CAP, FAILURE, "Namespace Insufficient Capacity"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_NS_ID_UNAVAILABLE, FAILURE, "Namespace Identifier Unavailable"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_NS_ALREADY_ATTACHED, FAILURE, "Namespace Already Attached"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_NS_IS_PRIVATE, FAILURE, "Namespace Is Private"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_NS_NOT_ATTACHED, FAILURE, "Namespace Not Attached"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_THIN_PROVISIONING_NOT_SUPPORTED, NOT_SUPPORTED,
     "Thin Provisioning Not Supported"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_CONTROLLER_LIST_INVALID, NOT_SUPPORTED,
     "Controller List Invalid"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_DEVICE_SELF_TEST_IN_PROGRESS, IN_PROGRESS,
     "Device Self Test In Progress"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_BOOT_PARTITION_WRITE_PROHIBITED, FAILURE,
     "Boot Partition Write Prohibited"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_CONTROLLER_IDENTIFIER, NOT_SUPPORTED,
     "Invalid Controller Identifier"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_SECONDARY_CONTROLLER_STATE, NOT_SUPPORTED,
     "Invalid Secondary Controller State"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES, NOT_SUPPORTED,
     "Invalid Number of Controller Resources"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_RESOURCE_IDENTIFIER, NOT_SUPPORTED,
     "Invalid Resource Identifier"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_SANITIZE_PROHIBITED_WHILE_PERSISTENT_MEMORY_REGION_IS_ENABLED,
     NOT_SUPPORTED, "Sanitize Prohibited While Persistent Memory Region is Enabled"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ANA_GROUP_IDENTIFIER_INVALID, NOT_SUPPORTED,
     "ANA Group Identifier Invalid"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ANA_ATTACH_FAILED, FAILURE, "ANA Attach Failed"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INSUFFICIENT_CAPACITY, FAILURE, "Insufficient Capacity"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_NAMESPACE_ATTACHMENT_LIMIT_EXCEEDED, FAILURE,
     "Namespace Attachment Limit Exceeded"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_PROHIBITION_OF_COMMAND_EXECUTION_NOT_SUPPORTED, NOT_SUPPORTED,
     "Prohibition of Command Execution Not Supported"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_IO_COMMAND_SET_NOT_SUPPORTED, NOT_SUPPORTED,
     "I/O Command Set Not Supported"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_IO_COMMAND_SET_NOT_ENABLED, NOT_SUPPORTED,
     "I/O Command Set Not Enabled"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_IO_COMMAND_SET_COMBINATION_REJECTED, FAILURE,
     "I/O Command Set Combination Rejected"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_IO_COMMAND_SET, FAILURE, "Invalid I/O Command Set"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_IDENTIFIER_UNAVAILABLE, FAILURE, "Identifier Unavailable"},
    // 80-BF are NVM command set specific
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_CONFLICTING_ATTRIBUTES_, FAILURE, "Conflicting Attributes"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_PROTECTION_INFORMATION, FAILURE,
     "Invalid Protection Information"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ATTEMPTED_WRITE_TO_READ_ONLY_RANGE, FAILURE,
     "Attempted Write to Read Only Range"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_COMMAND_SIZE_LIMIT_EXCEEDED, FAILURE,
     "Command Size Limit Exceeded"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ZONED_BOUNDARY_ERROR, FAILURE, "Zoned Boundary Error"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ZONE_IS_FULL, FAILURE, "Zone is Full"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ZONE_IS_READ_ONLY, FAILURE, "Zone is Read-Only"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ZONE_IS_OFFLINE, FAILURE, "Zone is Offline"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ZONE_INVALID_WRITE, FAILURE, "Zone Invalid Write"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_TOO_MANY_ACTIVE_ZONES, FAILURE, "Too Many Active Zones"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_TOO_MANY_OPEN_ZONES, FAILURE, "Too Many Open Zones"},
    {NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_INVALID_ZONE_STATE_TRANSITION, FAILURE,
     "Invalid Zone State Transition"},
    // Media and Data Integrity Errors
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_WRITE_FAULT_, FAILURE, "Write Fault"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_UNREC_READ_ERROR_, FAILURE, "Unrecovered Read Error"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_ETE_GUARD_CHECK_, FAILURE,
     "End-to-end Guard Check Error"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_ETE_APPTAG_CHECK_, FAILURE,
     "End-to-end Application Tag Check Error"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_ETE_REFTAG_CHECK_, FAILURE,
     "End-to-end Reference Tag Check Error"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_COMPARE_FAILED_, FAILURE, "Compare Failure"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_ACCESS_DENIED_, DEVICE_ACCESS_DENIED, "Access Denied"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_DEALLOCATED_OR_UNWRITTEN_LOGICAL_BLOCK, FAILURE,
     "Deallocated or Unwritten Logical Block Data"},
    {NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_END_TO_END_STORAGE_TAG_CHECK_ERROR, FAILURE,
     "End-To-End Storage Tag Check Error"},
    // Path Related Status
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_INTERNAL_PATH_ERROR, FAILURE, "Internal Path Error"},
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_ASYMMETRIC_ACCESS_PERSISTENT_LOSS, FAILURE,
     "Asymmetric Access Persistent Loss"},
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_ASYMMETRIC_ACCESS_INACCESSIBLE, FAILURE,
     "Asymmetric Access Inaccessible"},
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_ASYMMETRIC_ACCESS_TRANSITION, FAILURE, "Asymmetric Access Transition"},
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_CONTROLLER_PATHING_ERROR, FAILURE, "Controller Pathing Error"},
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_HOST_PATHING_ERROR, FAILURE, "Host Pathing Error"},
    {NVME_SCT_PATH_RELATED_STATUS, NVME_PATH_SC_COMMAND_ABORTED_BY_HOST, ABORTED, "Command Aborted By Host"},
};

static int cmp_NVMe_Status(nvmeStatus* a, nvmeStatus* b)
{
    // compare status code type, if they are same, compare status code
    int ret = a->statusCodeType - b->statusCodeType;
    if (ret)
    {
        return ret;
    }
    else
    {
        return (a->statusCode - b->statusCode);
    }
}

const nvmeStatus* get_NVMe_Status(uint32_t nvmeStatusDWord)
{
    nvmeStatus  key;
    nvmeStatus* result = M_NULLPTR;
    safe_memset(&key, sizeof(nvmeStatus), 0, sizeof(nvmeStatus));
    key.statusCodeType = get_8bit_range_uint32(nvmeStatusDWord, 27, 25);
    key.statusCode     = get_8bit_range_uint32(nvmeStatusDWord, 24, 17);

    result = M_REINTERPRET_CAST(nvmeStatus*, safe_bsearch(&key, nvmeStatusLookup, SIZE_OF_STACK_ARRAY(nvmeStatusLookup),
                                                          sizeof(nvmeStatusLookup[0]),
                                                          (int (*)(const void*, const void*))cmp_NVMe_Status));

    return result;
}

// NOTE: this function needs to be expanded as new status codes are added
eReturnValues check_NVMe_Status(uint32_t nvmeStatusDWord)
{
    eReturnValues     ret  = UNKNOWN;
    const nvmeStatus* stat = get_NVMe_Status(nvmeStatusDWord);

    if (stat != M_NULLPTR)
    {
        ret = stat->ret;
    }

    return ret;
}

void print_NVMe_Cmd_Result_Verbose(const nvmeCmdCtx* cmdCtx)
{
    printf("NVM Command Completion:\n");
    printf("\tCommand Specific (DW0): ");
    if (cmdCtx->commandCompletionData.dw0Valid)
    {
        printf("%" PRIX32 "h\n", cmdCtx->commandCompletionData.commandSpecific);
    }
    else
    {
        printf("Unavailable from OS\n");
    }
    printf("\tReserved (DW1): ");
    if (cmdCtx->commandCompletionData.dw1Valid)
    {
        printf("%" PRIX32 "h\n", cmdCtx->commandCompletionData.dw1Reserved);
    }
    else
    {
        printf("Unavailable from OS\n");
    }
    printf("\tSQ ID & SQ Head Ptr (DW2): ");
    if (cmdCtx->commandCompletionData.dw2Valid)
    {
        printf("%" PRIX32 "h\n", cmdCtx->commandCompletionData.sqIDandHeadPtr);
    }
    else
    {
        printf("Unavailable from OS\n");
    }
    printf("\tStatus & CID (DW3): ");
    if (cmdCtx->commandCompletionData.dw3Valid)
    {
        bool    dnr            = false;
        bool    more           = false;
        uint8_t statusCodeType = UINT8_C(0);
        uint8_t statusCode     = UINT8_C(0);
        printf("%" PRIX32 "h\n", cmdCtx->commandCompletionData.statusAndCID);
        get_NVMe_Status_Fields_From_DWord(cmdCtx->commandCompletionData.statusAndCID, &dnr, &more, &statusCodeType,
                                          &statusCode);
        printf("\t\tDo Not Retry: ");
        if (dnr)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
        printf("\t\tMore: ");
        if (more)
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
#define NVME_STATUS_CODE_TYPE_STRING_LENGTH 32
#define NVME_STATUS_CODE_STRING_LENGTH      62
        DECLARE_ZERO_INIT_ARRAY(char, statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH);
        DECLARE_ZERO_INIT_ARRAY(char, statusCodeString, NVME_STATUS_CODE_STRING_LENGTH);
        // also print out the phase tag, CID. NOTE: These aren't available in Linux!
        const nvmeStatus* stat = get_NVMe_Status(cmdCtx->commandCompletionData.statusAndCID);
        switch (statusCodeType)
        {
        case NVME_SCT_GENERIC_COMMAND_STATUS:
            snprintf_err_handle(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Generic Command Status");
            break;
        case NVME_SCT_COMMAND_SPECIFIC_STATUS:
            snprintf_err_handle(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Command Specific Status");
            break;
        case NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS:
            snprintf_err_handle(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH,
                                "Media And Data Integrity Errors");
            break;
        case NVME_SCT_PATH_RELATED_STATUS:
            snprintf_err_handle(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Path Related Status");
            break;
        case NVME_SCT_VENDOR_SPECIFIC_STATUS:
            snprintf_err_handle(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Vendor Specific");
            break;
        default:
            snprintf_err_handle(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Unknown");
            break;
        }
        if (stat != M_NULLPTR)
        {
            snprintf_err_handle(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "%s", stat->description);
        }
        else
        {
            if (statusCodeType == NVME_SCT_VENDOR_SPECIFIC_STATUS)
            {

                snprintf_err_handle(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Vendor Specific");
            }
            else
            {
                snprintf_err_handle(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
            }
        }
        printf("\t\tStatus Code Type: %s (%" PRIX8 "h)\n", statusCodeTypeString, statusCodeType);
        printf("\t\tStatus Code: %s (%" PRIX8 "h)\n", statusCodeString, statusCode);
    }
    else
    {
        printf("Unavailable from OS\n");
    }
    printf("\n");
}

const char* nvme_cmd_to_string(int admin, uint8_t opcode)
{
    if (admin)
    {
        switch (opcode)
        {
        case NVME_ADMIN_CMD_DELETE_SQ:
            return "Delete I/O Submission Queue";
        case NVME_ADMIN_CMD_CREATE_SQ:
            return "Create I/O Submission Queue";
        case NVME_ADMIN_CMD_GET_LOG_PAGE:
            return "Get Log Page";
        case NVME_ADMIN_CMD_DELETE_CQ:
            return "Delete I/O Completion Queue";
        case NVME_ADMIN_CMD_CREATE_CQ:
            return "Create I/O Completion Queue";
        case NVME_ADMIN_CMD_IDENTIFY:
            return "Identify";
        case NVME_ADMIN_CMD_ABORT_CMD:
            return "Abort";
        case NVME_ADMIN_CMD_SET_FEATURES:
            return "Set Features";
        case NVME_ADMIN_CMD_GET_FEATURES:
            return "Get Features";
        case NVME_ADMIN_CMD_ASYNC_EVENT:
            return "Asynchronous Event Request";
        case NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT:
            return "Namespace Management";
        case NVME_ADMIN_CMD_ACTIVATE_FW:
            return "Firmware Commit";
        case NVME_ADMIN_CMD_DOWNLOAD_FW:
            return "Firmware Image Download";
        case NVME_ADMIN_CMD_DEVICE_SELF_TEST:
            return "Device Self-test";
        case NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT:
            return "Namespace Attachment";
        case NVME_ADMIN_CMD_KEEP_ALIVE:
            return "Keep Alive";
        case NVME_ADMIN_CMD_DIRECTIVE_SEND:
            return "Directive Send";
        case NVME_ADMIN_CMD_DIRECTIVE_RECEIVE:
            return "Directive Receive";
        case NVME_ADMIN_CMD_VIRTUALIZATION_MANAGEMENT:
            return "Virtualization Management";
        case NVME_ADMIN_CMD_NVME_MI_SEND:
            return "NVMEe-MI Send";
        case NVME_ADMIN_CMD_NVME_MI_RECEIVE:
            return "NVMEe-MI Receive";
        case NVME_ADMIN_CMD_DOORBELL_BUFFER_CONFIG:
            return "Doorbell Buffer Config";
        case NVME_ADMIN_CMD_NVME_OVER_FABRICS:
            return "NVMe Over Fabric";
        case NVME_ADMIN_CMD_FORMAT_NVM:
            return "Format NVM";
        case NVME_ADMIN_CMD_SECURITY_SEND:
            return "Security Send";
        case NVME_ADMIN_CMD_SECURITY_RECV:
            return "Security Receive";
        case NVME_ADMIN_CMD_SANITIZE:
            return "Sanitize";
        }
    }
    else
    {
        switch (opcode)
        {
        case NVME_CMD_FLUSH:
            return "Flush";
        case NVME_CMD_WRITE:
            return "Write";
        case NVME_CMD_READ:
            return "Read";
        case NVME_CMD_WRITE_UNCOR:
            return "Write Uncorrectable";
        case NVME_CMD_COMPARE:
            return "Compare";
        case NVME_CMD_WRITE_ZEROS:
            return "Write Zeroes";
        case NVME_CMD_DATA_SET_MANAGEMENT:
            return "Dataset Management";
        case NVME_CMD_RESERVATION_REGISTER:
            return "Reservation Register";
        case NVME_CMD_RESERVATION_REPORT:
            return "Reservation Report";
        case NVME_CMD_RESERVATION_ACQUIRE:
            return "Reservation Acquire";
        case NVME_CMD_RESERVATION_RELEASE:
            return "Reservation Release";
        }
    }

    return "Unknown";
}

eReturnValues nvme_Get_SMART_Log_Page(tDevice* device, uint32_t nsid, uint8_t* pData, uint32_t dataLen)
{
    eReturnValues         ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
    nvmeSmartLog*         smartLog; // in case we need to align memory
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    DISABLE_NONNULL_COMPARE
    if (pData == M_NULLPTR || (dataLen < NVME_SMART_HEALTH_LOG_LEN))
    {
        return ret;
    }
    RESTORE_NONNULL_COMPARE

    safe_memset(&cmdOpts, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
    smartLog = C_CAST(nvmeSmartLog*, pData);

    cmdOpts.nsid = nsid;
    // cmdOpts.addr = C_CAST(uint64_t, smartLog);
    cmdOpts.addr    = C_CAST(uint8_t*, smartLog);
    cmdOpts.dataLen = NVME_SMART_HEALTH_LOG_LEN;
    cmdOpts.lid     = NVME_LOG_SMART_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_ERROR_Log_Page(tDevice* device, uint8_t* pData, uint32_t dataLen)
{
    eReturnValues         ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    // Should be able to pull at least one entry.
    DISABLE_NONNULL_COMPARE
    if (pData == M_NULLPTR || (dataLen < sizeof(nvmeErrLogEntry)))
    {
        return ret;
    }
    RESTORE_NONNULL_COMPARE

    safe_memset(&cmdOpts, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr    = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid     = NVME_LOG_ERROR_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_FWSLOTS_Log_Page(tDevice* device, uint8_t* pData, uint32_t dataLen)
{
    eReturnValues         ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    // Should be able to pull at least one entry.
    DISABLE_NONNULL_COMPARE
    if (pData == M_NULLPTR || (dataLen < sizeof(nvmeFirmwareSlotInfo)))
    {
        return ret;
    }
    RESTORE_NONNULL_COMPARE

    safe_memset(&cmdOpts, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr    = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid     = NVME_LOG_FW_SLOT_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_CmdSptEfft_Log_Page(tDevice* device, uint8_t* pData, uint32_t dataLen)
{
    eReturnValues         ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    // Should be able to pull at least one entry.
    DISABLE_NONNULL_COMPARE
    if (pData == M_NULLPTR || (dataLen < sizeof(nvmeEffectsLog)))
    {
        return ret;
    }
    RESTORE_NONNULL_COMPARE

    safe_memset(&cmdOpts, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr    = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid     = NVME_LOG_CMD_SPT_EFET_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_DevSelfTest_Log_Page(tDevice* device, uint8_t* pData, uint32_t dataLen)
{
    eReturnValues         ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    // Should be able to pull at least one entry.
    DISABLE_NONNULL_COMPARE
    if (pData == M_NULLPTR || (dataLen < sizeof(nvmeSelfTestLog)))
    {
        return ret;
    }
    RESTORE_NONNULL_COMPARE

    safe_memset(&cmdOpts, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr    = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid     = NVME_LOG_DEV_SELF_TEST_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

// Seagate unique?
eReturnValues nvme_Read_Ext_Smt_Log(tDevice* device, EXTENDED_SMART_INFO_T* ExtdSMARTInfo)
{
    eReturnValues         ret = SUCCESS;
    nvmeGetLogPageCmdOpts getExtSMARTLog;
    safe_memset(&getExtSMARTLog, sizeof(nvmeGetLogPageCmdOpts), 0, sizeof(nvmeGetLogPageCmdOpts));
    getExtSMARTLog.dataLen = sizeof(EXTENDED_SMART_INFO_T);
    getExtSMARTLog.lid     = 0xC4;
    getExtSMARTLog.nsid    = device->drive_info.namespaceID;
    getExtSMARTLog.addr    = C_CAST(uint8_t*, ExtdSMARTInfo);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Reading NVMe Ext SMART Log\n");
    }
    ret = nvme_Get_Log_Page(device, &getExtSMARTLog);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Ext SMART Log", ret);
    }
    return ret;
}
