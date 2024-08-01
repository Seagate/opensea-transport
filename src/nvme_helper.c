// SPDX-License-Identifier: MPL-2.0
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

#include "common_types.h"
#include "precision_timer.h"
#include "memory_safety.h"
#include "type_conversion.h"
#include "string_utils.h"
#include "bit_manip.h"
#include "code_attributes.h"
#include "math_utils.h"
#include "error_translation.h"
#include "io_utils.h"

#include "platform_helper.h"
#include <stdio.h>
#include "nvme_helper.h"
#include "nvme_helper_func.h"
#include "common_public.h"

//pointer to NVMe controller identify data should always be 4096B since all NVMe identify data is this long.
//All parameters should be the length of what is required in NVMe spec + 1 for a M_NULLPTR terminating character.
static void fill_NVMe_Strings_From_Ctrl_Data(uint8_t* ptrCtrlData, char nvmMN[NVME_CTRL_IDENTIFY_MN_LEN + 1], char nvmSN[NVME_CTRL_IDENTIFY_SN_LEN + 1], char nvmFW[NVME_CTRL_IDENTIFY_FW_LEN + 1])
{
    if (ptrCtrlData)
    {
        nvmeIDCtrl* ctrlData = C_CAST(nvmeIDCtrl*, ptrCtrlData);
        //make sure buffers all all zeroed out before filling them
        memset(nvmMN, 0, M_Min(MODEL_NUM_LEN + 1, NVME_CTRL_IDENTIFY_MN_LEN + 1));
        memset(nvmSN, 0, M_Min(SERIAL_NUM_LEN + 1, NVME_CTRL_IDENTIFY_SN_LEN + 1));
        memset(nvmFW, 0, M_Min(FW_REV_LEN + 1, NVME_CTRL_IDENTIFY_FW_LEN + 1));
        //fill each buffer with data from NVMe ctrl data
        memcpy(nvmSN, ctrlData->sn, M_Min(SERIAL_NUM_LEN, NVME_CTRL_IDENTIFY_SN_LEN));
        remove_Leading_And_Trailing_Whitespace(nvmSN);
        memcpy(nvmFW, ctrlData->fr, M_Min(FW_REV_LEN, NVME_CTRL_IDENTIFY_FW_LEN));
        remove_Leading_And_Trailing_Whitespace(nvmFW);
        memcpy(nvmMN, ctrlData->mn, M_Min(MODEL_NUM_LEN, NVME_CTRL_IDENTIFY_MN_LEN));
        remove_Leading_And_Trailing_Whitespace(nvmMN);
    }
    return;
}

// \file nvme_cmds.c   Implementation for NVM Express helper functions
//                     The intention of the file is to be generic & not OS specific

// \fn fill_In_NVMe_Device_Info(device device)
// \brief Sends a set Identify etc commands & fills in the device information
// \param device device struture
// \return SUCCESS - pass, !SUCCESS fail or something went wrong
eReturnValues fill_In_NVMe_Device_Info(tDevice *device)
{
    eReturnValues ret = UNKNOWN;

    //set some pointers to where we want to fill in information...we're doing this so that on USB, we can store some info about the child drive, without disrupting the standard drive_info that has already been filled in by the fill_SCSI_Info function
    uint64_t *fillWWN = &device->drive_info.worldWideName;
    uint32_t *fillLogicalSectorSize = &device->drive_info.deviceBlockSize;
    uint32_t *fillPhysicalSectorSize = &device->drive_info.devicePhyBlockSize;
    uint16_t *fillSectorAlignment = &device->drive_info.sectorAlignment;
    uint64_t *fillMaxLba = &device->drive_info.deviceMaxLba;

    //If not an NVMe interface, such as USB, then we need to store things differently
    //RAID Interface should be treated as "Native" or "NVME_INTERFACE" since there is likely an underlying API providing direct access of some kind.
    if (device->drive_info.interface_type != NVME_INTERFACE && device->drive_info.interface_type != RAID_INTERFACE)
    {
        device->drive_info.bridge_info.isValid = true;
        fillWWN = &device->drive_info.bridge_info.childWWN;
        fillLogicalSectorSize = &device->drive_info.bridge_info.childDeviceBlockSize;
        fillPhysicalSectorSize = &device->drive_info.bridge_info.childDevicePhyBlockSize;
        fillSectorAlignment = &device->drive_info.bridge_info.childSectorAlignment;
        fillMaxLba = &device->drive_info.bridge_info.childDeviceMaxLba;
    }

    nvmeIDCtrl * ctrlData = &device->drive_info.IdentifyData.nvme.ctrl; //Conroller information data structure
    nvmeIDNameSpaces * nsData = &device->drive_info.IdentifyData.nvme.ns; //Name Space Data structure 

#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif

    ret = nvme_Identify(device, C_CAST(uint8_t *, ctrlData), 0, NVME_IDENTIFY_CTRL);

#ifdef _DEBUG
    printf("fill NVMe info ret = %d\n", ret);
#endif

    if (ret == SUCCESS)
    {
        uint16_t enduranceGroup = 0;
        if (device->drive_info.interface_type != NVME_INTERFACE && device->drive_info.interface_type != RAID_INTERFACE)
        {
            fill_NVMe_Strings_From_Ctrl_Data(C_CAST(uint8_t*, ctrlData), device->drive_info.bridge_info.childDriveMN, device->drive_info.bridge_info.childDriveSN, device->drive_info.bridge_info.childDriveFW);
        }
        else
        {
            fill_NVMe_Strings_From_Ctrl_Data(C_CAST(uint8_t*, ctrlData), device->drive_info.product_identification, device->drive_info.serialNumber, device->drive_info.product_revision);
        }
        //set the t10 vendor id to NVMe
        snprintf(device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1, "NVMe");
        device->drive_info.media_type = MEDIA_NVM;//This will bite us someday when someone decided to put non-ssds on NVMe interface.
        //set scsi version to 6 if it is not already set
        if (device->drive_info.scsiVersion == 0)
        {
            device->drive_info.scsiVersion = 6;//most likely this is what will be set by a translator and keep other parts of code working correctly
        }

        //Do not overwrite this with non-NVMe interfaces. This is used by USB to figure out and track bridge chip specific things that are stored in this location
        if (device->drive_info.interface_type == NVME_INTERFACE && !device->drive_info.adapter_info.vendorIDValid)
        {
            device->drive_info.adapter_info.vendorID = ctrlData->vid;
            device->drive_info.adapter_info.vendorIDValid = true;
        }
        //set the IEEE OUI into the WWN since we use the WWN for detecting if the drive is a Seagate drive.
        //This is a shortcut to verify this is a Seagate drive later. There is a SCSI translation whitepaper we can follow, but that will be
        //more complicated of a change due to the different formatting used.
        *fillWWN = M_BytesTo8ByteValue(0x05, ctrlData->ieee[2], ctrlData->ieee[1], ctrlData->ieee[0], 0, 0, 0, 0) << 4;

        ret = nvme_Identify(device, C_CAST(uint8_t *, nsData), device->drive_info.namespaceID, NVME_IDENTIFY_NS);

        if (ret == SUCCESS)
        {
            uint8_t flbas = M_GETBITRANGE(nsData->flbas, 3, 0);
            //get the LBAF number. THis field varies depending on other things reported by the drive in NVMe 2.0
            if (nsData->nlbaf > 16)
            {
                //need to append 2 more bits to interpret this correctly since number of formats > 16
                flbas |= M_GETBITRANGE(nsData->flbas, 6, 5) << 4;
            }
            *fillLogicalSectorSize = C_CAST(uint32_t, power_Of_Two(nsData->lbaf[flbas].lbaDS));
            *fillPhysicalSectorSize = *fillLogicalSectorSize; //True for NVMe?
            *fillSectorAlignment = 0;

            *fillMaxLba = nsData->nsze - 1;//spec says this is from 0 to (n-1)!

            enduranceGroup = nsData->endgid;
            if (ctrlData->lpa & BIT5 && ctrlData->ctratt & BIT4 && enduranceGroup > 0)
            {
                //Check if this is an HDD
                //First read the supported logs log page, then if the rotating media log is there, read it.
                uint8_t* supportedLogs = C_CAST(uint8_t*, safe_calloc_aligned(1024, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (supportedLogs)
                {
                    nvmeGetLogPageCmdOpts supLogs;
                    memset(&supLogs, 0, sizeof(nvmeGetLogPageCmdOpts));
                    supLogs.addr = supportedLogs;
                    supLogs.dataLen = 1024;
                    supLogs.lid = NVME_LOG_SUPPORTED_PAGES_ID;
                    if (SUCCESS == nvme_Get_Log_Page(device, &supLogs))
                    {
                        uint32_t rotMediaOffset = NVME_LOG_ROTATIONAL_MEDIA_INFORMATION_ID * 4;
                        uint32_t rotMediaSup = M_BytesTo4ByteValue(supportedLogs[rotMediaOffset + 3], supportedLogs[rotMediaOffset + 2], supportedLogs[rotMediaOffset + 1], supportedLogs[rotMediaOffset + 0]);
                        if (rotMediaSup & BIT0)
                        {
                            //rotational media log is supported.
                            //Set the media type because this is supported, at least for now. We can read the log and the actual rotation rate if needed.
                            device->drive_info.media_type = MEDIA_HDD;
                        }
                    }
                    safe_Free_aligned(C_CAST(void**, &supportedLogs));
                }
            }
        }
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif

    return ret;
}

void print_NVMe_Cmd_Verbose(const nvmeCmdCtx * cmdCtx)
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
    //Data Direction:
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
    //printf("Cmd result 0x%02X\n", cmdCtx->result);
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

void get_NVMe_Status_Fields_From_DWord(uint32_t nvmeStatusDWord, bool *doNotRetry, bool *more, uint8_t *statusCodeType, uint8_t *statusCode)
{
    if (doNotRetry && more && statusCodeType && statusCode)
    {
        *doNotRetry = nvmeStatusDWord & BIT31;
        *more = nvmeStatusDWord & BIT30;
        *statusCodeType = M_GETBITRANGE(nvmeStatusDWord, 27, 25);
        *statusCode = M_GETBITRANGE(nvmeStatusDWord, 24, 17);
    }
}

//NOTE: this function needs to be expanded as new status codes are added
eReturnValues check_NVMe_Status(uint32_t nvmeStatusDWord)
{
    eReturnValues ret = SUCCESS;
    //bool doNotRetry = nvmeStatusDWord & BIT31;
    //bool more  = nvmeStatusDWord & BIT30;
    uint8_t statusCodeType = M_GETBITRANGE(nvmeStatusDWord, 27, 25);
    uint8_t statusCode = M_GETBITRANGE(nvmeStatusDWord, 24, 17);

    switch (statusCodeType)
    {
    case NVME_SCT_GENERIC_COMMAND_STATUS://generic
        switch (statusCode)
        {
        case NVME_GEN_SC_SUCCESS_:
            ret = SUCCESS;
            break;
        case NVME_GEN_SC_INVALID_OPCODE_:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_INVALID_FIELD_:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_CMDID_CONFLICT_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_DATA_XFER_ERROR_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_POWER_LOSS_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_INTERNAL_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_ABORT_REQ_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_ABORT_QUEUE_:
            ret = ABORTED;
            break;
        case NVME_GEN_SC_FUSED_FAIL_:
            ret = ABORTED;
            break;
        case NVME_GEN_SC_FUSED_MISSING_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_INVALID_NS_:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_CMD_SEQ_ERROR_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_INVALID_SGL_SEGMENT_DESCRIPTOR:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_INVALID_NUMBER_OF_SGL_DESCRIPTORS:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_DATA_SGL_LENGTH_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_METADATA_SGL_LENGTH_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_SGL_DESCRIPTOR_TYPE_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_INVALID_USE_OF_CONTROLLER_MEMORY_BUFFER:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_PRP_OFFSET_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_ATOMIC_WRITE_UNIT_EXCEEDED:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_OPERATION_DENIED:
            ret = DEVICE_ACCESS_DENIED;
            break;
        case NVME_GEN_SC_SGL_OFFSET_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_HOST_IDENTIFIER_INCONSISTENT_FORMAT:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_EXPIRED:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_COMMAND_ABORTED_DUE_TO_PREEMPT_AND_ABORT:
            ret = ABORTED;
            break;
        case NVME_GEN_SC_SANITIZE_FAILED:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_SANITIZE_IN_PROGRESS:
            ret = IN_PROGRESS;
            break;
        case NVME_GEN_SC_SGL_DATA_BLOCK_GRANULARITY_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_GEN_SC_COMMAND_NOT_SUPPORTED_FOR_QUEUE_IN_CMB:
            ret = NOT_SUPPORTED;
            break;
            //80-BF are NVM command set specific                
        case NVME_GEN_SC_LBA_RANGE_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_CAP_EXCEEDED_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_NS_NOT_READY_:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_RESERVATION_CONFLICT:
            ret = FAILURE;
            break;
        case NVME_GEN_SC_FORMAT_IN_PROGRESS:
            ret = IN_PROGRESS;
            break;
        default:
            ret = UNKNOWN;
            break;
        }
        break;
    case NVME_SCT_COMMAND_SPECIFIC_STATUS://command specific
        switch (statusCode)
        {
        case NVME_CMD_SP_SC_CQ_INVALID_:
        case NVME_CMD_SP_SC_QID_INVALID_:
        case NVME_CMD_SP_SC_QUEUE_SIZE_:
        case NVME_CMD_SP_SC_ABORT_LIMIT_:
            //NVME_CMD_SP_SC_ABORT_MISSING_ = 0x04,//reserved in NVMe specs
        case NVME_CMD_SP_SC_ASYNC_LIMIT_:
            ret = FAILURE;
            break;
        case NVME_CMD_SP_SC_INVALID_FIRMWARE_SLOT_:
            ret = FAILURE;
            break;
        case NVME_CMD_SP_SC_INVALIDFIRMWARE_IMAGE_:
            ret = FAILURE;
            break;
        case NVME_CMD_SP_SC_INVALID_INTERRUPT_VECTOR_:
        case NVME_CMD_SP_SC_INVALID_LOG_PAGE_:
        case NVME_CMD_SP_SC_INVALID_FORMAT_:
            ret = NOT_SUPPORTED;
            break;
        case NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET:
            ret = SUCCESS;
            break;
        case NVME_CMD_SP_SC_INVALID_QUEUE_DELETION:
        case NVME_CMD_SP_SC_FEATURE_IDENTIFIER_NOT_SAVABLE:
        case NVME_CMD_SP_SC_FEATURE_NOT_CHANGEABLE:
        case NVME_CMD_SP_SC_FEATURE_NOT_NAMESPACE_SPECIFC:
            ret = FAILURE;
            break;
        case NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET:
        case NVME_CMD_SP_SC_FW_ACT_REQ_RESET:
        case NVME_CMD_SP_SC_FW_ACT_REQ_MAX_TIME_VIOALTION:
            ret = SUCCESS;
            break;
        case NVME_CMD_SP_SC_FW_ACT_PROHIBITED:
        case NVME_CMD_SP_SC_OVERLAPPING_RANGE:
        case NVME_CMD_SP_SC_NS_INSUFFICIENT_CAP:
        case NVME_CMD_SP_SC_NS_ID_UNAVAILABLE:
        case NVME_CMD_SP_SC_NS_ALREADY_ATTACHED:
        case NVME_CMD_SP_SC_NS_IS_PRIVATE:
        case NVME_CMD_SP_SC_NS_NOT_ATTACHED:
            ret = FAILURE;
            break;
        case NVME_CMD_SP_SC_THIN_PROVISIONING_NOT_SUPPORTED:
        case NVME_CMD_SP_SC_CONTROLLER_LIST_INVALID:
            ret = NOT_SUPPORTED;
            break;
        case NVME_CMD_SP_SC_DEVICE_SELF_TEST_IN_PROGRESS:
            ret = IN_PROGRESS;
            break;
        case NVME_CMD_SP_SC_BOOT_PARTITION_WRITE_PROHIBITED:
            ret = FAILURE;
            break;
        case NVME_CMD_SP_SC_INVALID_CONTROLLER_IDENTIFIER:
        case NVME_CMD_SP_SC_INVALID_SECONDARY_CONTROLLER_STATE:
        case NVME_CMD_SP_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES:
        case NVME_CMD_SP_SC_INVALID_RESOURCE_IDENTIFIER:
            ret = NOT_SUPPORTED;
            break;
            //80-BF are NVM command set specific                 
        case NVME_CMD_SP_SC_CONFLICTING_ATTRIBUTES_:
        case NVME_CMD_SP_SC_INVALID_PROTECTION_INFORMATION:
        case NVME_CMD_SP_SC_ATTEMPTED_WRITE_TO_READ_ONLY_RANGE:
            ret = FAILURE;
            break;
        default:
            ret = UNKNOWN;
            break;
        }
        break;
    case NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS://media or data errors
        switch (statusCode)
        {
        case NVME_MED_ERR_SC_WRITE_FAULT_:
        case NVME_MED_ERR_SC_UNREC_READ_ERROR_:
        case NVME_MED_ERR_SC_ETE_GUARD_CHECK_:
        case NVME_MED_ERR_SC_ETE_APPTAG_CHECK_:
        case NVME_MED_ERR_SC_ETE_REFTAG_CHECK_:
        case NVME_MED_ERR_SC_COMPARE_FAILED_:
        case NVME_MED_ERR_SC_DEALLOCATED_OR_UNWRITTEN_LOGICAL_BLOCK:
            ret = FAILURE;
            break;
        case NVME_MED_ERR_SC_ACCESS_DENIED_:
            ret = DEVICE_ACCESS_DENIED;
            break;
        default:
            ret = UNKNOWN;
            break;
        }
        break;
    case NVME_SCT_VENDOR_SPECIFIC_STATUS:
        //fall through to default.
    default:
        //unknown meaning. Either reserved or vendor unique.
        ret = UNKNOWN;
        break;
    }
    return ret;
}

void print_NVMe_Cmd_Result_Verbose(const nvmeCmdCtx * cmdCtx)
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
        bool dnr = false;
        bool more = false;
        uint8_t statusCodeType = 0;
        uint8_t statusCode = 0;
        printf("%" PRIX32 "h\n", cmdCtx->commandCompletionData.statusAndCID);
        get_NVMe_Status_Fields_From_DWord(cmdCtx->commandCompletionData.statusAndCID, &dnr, &more, &statusCodeType, &statusCode);
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
#define NVME_STATUS_CODE_STRING_LENGTH 62
        DECLARE_ZERO_INIT_ARRAY(char, statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH);
        DECLARE_ZERO_INIT_ARRAY(char, statusCodeString, NVME_STATUS_CODE_STRING_LENGTH);
        //also print out the phase tag, CID. NOTE: These aren't available in Linux!
        switch (statusCodeType)
        {
        case NVME_SCT_GENERIC_COMMAND_STATUS://generic
            snprintf(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Generic Status");
            switch (statusCode)
            {
            case NVME_GEN_SC_SUCCESS_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Success");
                break;
            case NVME_GEN_SC_INVALID_OPCODE_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Command Opcode");
                break;
            case NVME_GEN_SC_INVALID_FIELD_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Field in Command");
                break;
            case NVME_GEN_SC_CMDID_CONFLICT_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command ID Conflict");
                break;
            case NVME_GEN_SC_DATA_XFER_ERROR_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Data Transfer Error");
                break;
            case NVME_GEN_SC_POWER_LOSS_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Commands Aborted due to Power Less Notification");
                break;
            case NVME_GEN_SC_INTERNAL_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Internal Error");
                break;
            case NVME_GEN_SC_ABORT_REQ_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Abort Requested");
                break;
            case NVME_GEN_SC_ABORT_QUEUE_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Aborted due to SQ Deletion");
                break;
            case NVME_GEN_SC_FUSED_FAIL_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Aborted due to Failed Fused Command");
                break;
            case NVME_GEN_SC_FUSED_MISSING_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Aborted due to Missing Fused Command");
                break;
            case NVME_GEN_SC_INVALID_NS_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Namespace or Format");
                break;
            case NVME_GEN_SC_CMD_SEQ_ERROR_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Sequence Error");
                break;
            case NVME_GEN_SC_INVALID_SGL_SEGMENT_DESCRIPTOR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid SGL Segment Descriptor");
                break;
            case NVME_GEN_SC_INVALID_NUMBER_OF_SGL_DESCRIPTORS:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Number of SGL Descriptors");
                break;
            case NVME_GEN_SC_DATA_SGL_LENGTH_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Data SGL Length Invalid");
                break;
            case NVME_GEN_SC_METADATA_SGL_LENGTH_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Metadata SGL Length Invalid");
                break;
            case NVME_GEN_SC_SGL_DESCRIPTOR_TYPE_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "SGL Descriptor Type Invalid");
                break;
            case NVME_GEN_SC_INVALID_USE_OF_CONTROLLER_MEMORY_BUFFER:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Use of Controller Memory Buffer");
                break;
            case NVME_GEN_SC_PRP_OFFSET_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "PRP Offset Invalid");
                break;
            case NVME_GEN_SC_ATOMIC_WRITE_UNIT_EXCEEDED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Atomic Write Unit Exceeded");
                break;
            case NVME_GEN_SC_OPERATION_DENIED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Operation Denied");
                break;
            case NVME_GEN_SC_SGL_OFFSET_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "SGL Offset Invalid");
                break;
            case NVME_GEN_SC_HOST_IDENTIFIER_INCONSISTENT_FORMAT:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Host Identifier Inconsistent Format");
                break;
            case NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_EXPIRED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Keep Alive Timeout Expired");
                break;
            case NVME_GEN_SC_KEEP_ALIVE_TIMEOUT_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Keel Alive Timeout Invalid");
                break;
            case NVME_GEN_SC_COMMAND_ABORTED_DUE_TO_PREEMPT_AND_ABORT:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Aborted due to Preempt and Abort");
                break;
            case NVME_GEN_SC_SANITIZE_FAILED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Sanitize Failed");
                break;
            case NVME_GEN_SC_SANITIZE_IN_PROGRESS:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Sanitize In Progress");
                break;
            case NVME_GEN_SC_SGL_DATA_BLOCK_GRANULARITY_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "SGL Data Block Granularity Invalid");
                break;
            case NVME_GEN_SC_COMMAND_NOT_SUPPORTED_FOR_QUEUE_IN_CMB:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Not Supported for Queue in CMB");
                break;
            case NVME_GEN_SC_NS_IS_WRITE_PROTECTED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace is Write Protected");
                break;
            case NVME_GEN_SC_COMMAND_INTERRUPTED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Interrupted");
                break;
            case NVME_GEN_SC_TRANSIENT_TRANSPORT_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Transient Transport Error");
                break;
            case NVME_GEN_SC_COMMAND_PROHIBITED_BY_CMD_AND_FEAT_LOCKDOWN:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Prohibited by Command and Feature Lockdown");
                break;
            case NVME_GEN_SC_ADMIN_COMMAND_MEDIA_NOT_READY:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Admin Command Media Not Ready");
                break;
                //80-BF are NVM command set specific                
            case NVME_GEN_SC_LBA_RANGE_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "LBA Out of Range");
                break;
            case NVME_GEN_SC_CAP_EXCEEDED_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Capacity Exceeded");
                break;
            case NVME_GEN_SC_NS_NOT_READY_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Not Ready");
                break;
            case NVME_GEN_SC_RESERVATION_CONFLICT:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Reservation Conflict");
                break;
            case NVME_GEN_SC_FORMAT_IN_PROGRESS:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Format In Progress");
                break;
            case NVME_GEN_SC_INVALID_VALUE_SIZE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Value Size");
                break;
            case NVME_GEN_SC_INVALID_KEY_SIZE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Key Size");
                break;
            case NVME_GEN_SC_KV_KEY_DOES_NOT_EXIST:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "KV Key Does Not Exist");
                break;
            case NVME_GEN_SC_UNRECOVERED_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unrecovered Error");
                break;
            case NVME_GEN_SC_KEY_EXISTS:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Key Exists");
                break;
            default:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
                break;
            }
            break;
        case NVME_SCT_COMMAND_SPECIFIC_STATUS://command specific
            snprintf(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Command Specific Status");
            switch (statusCode)
            {
            case NVME_CMD_SP_SC_CQ_INVALID_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Completion Queue Invalid");
                break;
            case NVME_CMD_SP_SC_QID_INVALID_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Queue Identifier");
                break;
            case NVME_CMD_SP_SC_QUEUE_SIZE_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Queue Size");
                break;
            case NVME_CMD_SP_SC_ABORT_LIMIT_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Aborted Command Limit Exceeded");
                break;
                //NVME_CMD_SP_SC_ABORT_MISSING_ = 0x04,//reserved in NVMe specs
            case NVME_CMD_SP_SC_ASYNC_LIMIT_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Asynchronous Event Request Limit Exceeded");
                break;
            case NVME_CMD_SP_SC_INVALID_FIRMWARE_SLOT_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Firmware Slot");
                break;
            case NVME_CMD_SP_SC_INVALIDFIRMWARE_IMAGE_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Firmware Image");
                break;
            case NVME_CMD_SP_SC_INVALID_INTERRUPT_VECTOR_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Interrupt Vector");
                break;
            case NVME_CMD_SP_SC_INVALID_LOG_PAGE_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Log Page");
                break;
            case NVME_CMD_SP_SC_INVALID_FORMAT_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Format");
                break;
            case NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Firmware Activation Requires Conventional Reset");
                break;
            case NVME_CMD_SP_SC_INVALID_QUEUE_DELETION:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Queue Deletion");
                break;
            case NVME_CMD_SP_SC_FEATURE_IDENTIFIER_NOT_SAVABLE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Feature Identifier Not Savable");
                break;
            case NVME_CMD_SP_SC_FEATURE_NOT_CHANGEABLE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Feature Not Changeable");
                break;
            case NVME_CMD_SP_SC_FEATURE_NOT_NAMESPACE_SPECIFC:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Feature Not Namespace Specific");
                break;
            case NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Firmware Activation Requires NVM Subsystem Reset");
                break;
            case NVME_CMD_SP_SC_FW_ACT_REQ_RESET:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Firmware Activation Requires Reset");
                break;
            case NVME_CMD_SP_SC_FW_ACT_REQ_MAX_TIME_VIOALTION:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Firmware Activation Requires Maximum Time Violation");
                break;
            case NVME_CMD_SP_SC_FW_ACT_PROHIBITED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Firmware Activation Prohibited");
                break;
            case NVME_CMD_SP_SC_OVERLAPPING_RANGE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Overlapping Range");
                break;
            case NVME_CMD_SP_SC_NS_INSUFFICIENT_CAP:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Insufficient Capacity");
                break;
            case NVME_CMD_SP_SC_NS_ID_UNAVAILABLE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Identifier Unavailable");
                break;
            case NVME_CMD_SP_SC_NS_ALREADY_ATTACHED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Already Attached");
                break;
            case NVME_CMD_SP_SC_NS_IS_PRIVATE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Is Private");
                break;
            case NVME_CMD_SP_SC_NS_NOT_ATTACHED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Not Attached");
                break;
            case NVME_CMD_SP_SC_THIN_PROVISIONING_NOT_SUPPORTED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Thin Provisioning Not Supported");
                break;
            case NVME_CMD_SP_SC_CONTROLLER_LIST_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Controller List Invalid");
                break;
            case NVME_CMD_SP_SC_DEVICE_SELF_TEST_IN_PROGRESS:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Device Self-test In Progress");
                break;
            case NVME_CMD_SP_SC_BOOT_PARTITION_WRITE_PROHIBITED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Boot Partition Write Prohibited");
                break;
            case NVME_CMD_SP_SC_INVALID_CONTROLLER_IDENTIFIER:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Controller Identifier");
                break;
            case NVME_CMD_SP_SC_INVALID_SECONDARY_CONTROLLER_STATE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Secondary Controller State");
                break;
            case NVME_CMD_SP_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Number of Controller Resources");
                break;
            case NVME_CMD_SP_SC_INVALID_RESOURCE_IDENTIFIER:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Resource Identifier");
                break;
            case NVME_CMD_SP_SC_SANITIZE_PROHIBITED_WHILE_PERSISTENT_MEMORY_REGION_IS_ENABLED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Sanitize Prohibited While Persistent Memory Region is Enabled");
                break;
            case NVME_CMD_SP_SC_ANA_GROUP_IDENTIFIER_INVALID:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "ANA Group Identifier Invalid");
                break;
            case NVME_CMD_SP_SC_ANA_ATTACH_FAILED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "ANA Attach Failed");
                break;
            case NVME_CMD_SP_SC_INSUFFICIENT_CAPACITY:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unsufficient Capacity");
                break;
            case NVME_CMD_SP_SC_NAMESPACE_ATTACHMENT_LIMIT_EXCEEDED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Namespace Attachment Limit Exceeded");
                break;
            case NVME_CMD_SP_SC_PROHIBITION_OF_COMMAND_EXECUTION_NOT_SUPPORTED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Prohibition of Command Execution Not Supported");
                break;
            case NVME_CMD_SP_SC_IO_COMMAND_SET_NOT_SUPPORTED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "I/O Command Set Not Supported");
                break;
            case NVME_CMD_SP_SC_IO_COMMAND_SET_NOT_ENABLED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "I/O Command Set Not Enabled");
                break;
            case NVME_CMD_SP_SC_IO_COMMAND_SET_COMBINATION_REJECTED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "I/O Command Set Combination Rejected");
                break;
            case NVME_CMD_SP_SC_INVALID_IO_COMMAND_SET:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid I/O Command Set");
                break;
            case NVME_CMD_SP_SC_IDENTIFIER_UNAVAILABLE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Identifier Unavailable");
                break;
                //80-BF are NVM command set specific                 
            case NVME_CMD_SP_SC_CONFLICTING_ATTRIBUTES_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Conflicting Attributes");
                break;
            case NVME_CMD_SP_SC_INVALID_PROTECTION_INFORMATION:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Protection Information");
                break;
            case NVME_CMD_SP_SC_ATTEMPTED_WRITE_TO_READ_ONLY_RANGE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Attempted Write to Read Only Range");
                break;
            case NVME_CMD_SP_SC_COMMAND_SIZE_LIMIT_EXCEEDED:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Size Limit Exceeded");
                break;
            case NVME_CMD_SP_SC_ZONED_BOUNDARY_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Zoned Boundary Error");
                break;
            case NVME_CMD_SP_SC_ZONE_IS_FULL:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Zone is Full");
                break;
            case NVME_CMD_SP_SC_ZONE_IS_READ_ONLY:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Zone is Read-Only");
                break;
            case NVME_CMD_SP_SC_ZONE_IS_OFFLINE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Zone is Offline");
                break;
            case NVME_CMD_SP_SC_ZONE_INVALID_WRITE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Zone Invalid Write");
                break;
            case NVME_CMD_SP_SC_TOO_MANY_ACTIVE_ZONES:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Too Many Active Zones");
                break;
            case NVME_CMD_SP_SC_TOO_MANY_OPEN_ZONES:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Too Many Open Zones");
                break;
            case NVME_CMD_SP_SC_INVALID_ZONE_STATE_TRANSITION:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Invalid Zone State Transition");
                break;
            default:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
                break;
            }
            break;
        case NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS://media or data errors
            snprintf(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Media And Data Integrity Errors");
            switch (statusCode)
            {
            case NVME_MED_ERR_SC_WRITE_FAULT_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Write Fault");
                break;
            case NVME_MED_ERR_SC_UNREC_READ_ERROR_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unrecovered Read Error");
                break;
            case NVME_MED_ERR_SC_ETE_GUARD_CHECK_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "End-to-end Guard Check Error");
                break;
            case NVME_MED_ERR_SC_ETE_APPTAG_CHECK_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "End-to-end Application Tag Check Error");
                break;
            case NVME_MED_ERR_SC_ETE_REFTAG_CHECK_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "End-to-end Reference Tag Check Error");
                break;
            case NVME_MED_ERR_SC_COMPARE_FAILED_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Compare Failure");
                break;
            case NVME_MED_ERR_SC_ACCESS_DENIED_:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Access Denied");
                break;
            case NVME_MED_ERR_SC_DEALLOCATED_OR_UNWRITTEN_LOGICAL_BLOCK:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Deallocated or Unwritten Logical Block");
                break;
            case NVME_MED_ERR_SC_END_TO_END_STORAGE_TAG_CHECK_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "End-To-End Storage Tag Check Error");
                break;
            default:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
                break;
            }
            break;
        case NVME_SCT_PATH_RELATED_STATUS:
            snprintf(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Path Related Status Errors");
            switch (statusCode)
            {
            case NVME_PATH_SC_INTERNAL_PATH_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Internal Path Error");
                break;
            case NVME_PATH_SC_ASYMMETRIC_ACCESS_PERSISTENT_LOSS:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Asymmetric Access Persistent Loss");
                break;
            case NVME_PATH_SC_ASYMMETRIC_ACCESS_INACCESSIBLE:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Asymmetric Access Inaccessible");
                break;
            case NVME_PATH_SC_ASYMMETRIC_ACCESS_TRANSITION:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Asymmetric Access Transition");
                break;
            case NVME_PATH_SC_CONTROLLER_PATHING_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Controller Pathing Error");
                break;
            case NVME_PATH_SC_HOST_PATHING_ERROR:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Host Pathing Error");
                break;
            case NVME_PATH_SC_COMMAND_ABORTED_BY_HOST:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Command Aborted By Host");
                break;
            default:
                snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
                break;
            }
            break;
        case NVME_SCT_VENDOR_SPECIFIC_STATUS:
            snprintf(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Vendor Specific");
            snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
            break;
        default:
            snprintf(statusCodeTypeString, NVME_STATUS_CODE_TYPE_STRING_LENGTH, "Unknown");
            snprintf(statusCodeString, NVME_STATUS_CODE_STRING_LENGTH, "Unknown");
            break;
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

char *nvme_cmd_to_string(int admin, uint8_t opcode)
{
    if (admin) {
        switch (opcode) {
        case NVME_ADMIN_CMD_DELETE_SQ:  return "Delete I/O Submission Queue";
        case NVME_ADMIN_CMD_CREATE_SQ:  return "Create I/O Submission Queue";
        case NVME_ADMIN_CMD_GET_LOG_PAGE:   return "Get Log Page";
        case NVME_ADMIN_CMD_DELETE_CQ:  return "Delete I/O Completion Queue";
        case NVME_ADMIN_CMD_CREATE_CQ:  return "Create I/O Completion Queue";
        case NVME_ADMIN_CMD_IDENTIFY:   return "Identify";
        case NVME_ADMIN_CMD_ABORT_CMD:  return "Abort";
        case NVME_ADMIN_CMD_SET_FEATURES:   return "Set Features";
        case NVME_ADMIN_CMD_GET_FEATURES:   return "Get Features";
        case NVME_ADMIN_CMD_ASYNC_EVENT:    return "Asynchronous Event Request";
        case NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT:   return "Namespace Management";
        case NVME_ADMIN_CMD_ACTIVATE_FW:    return "Firmware Commit";
        case NVME_ADMIN_CMD_DOWNLOAD_FW:    return "Firmware Image Download";
        case NVME_ADMIN_CMD_DEVICE_SELF_TEST:   return "Device Self-test";
        case NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT:   return "Namespace Attachment";
        case NVME_ADMIN_CMD_KEEP_ALIVE: return "Keep Alive";
        case NVME_ADMIN_CMD_DIRECTIVE_SEND: return "Directive Send";
        case NVME_ADMIN_CMD_DIRECTIVE_RECEIVE:  return "Directive Receive";
        case NVME_ADMIN_CMD_VIRTUALIZATION_MANAGEMENT:  return "Virtualization Management";
        case NVME_ADMIN_CMD_NVME_MI_SEND:   return "NVMEe-MI Send";
        case NVME_ADMIN_CMD_NVME_MI_RECEIVE:    return "NVMEe-MI Receive";
        case NVME_ADMIN_CMD_DOORBELL_BUFFER_CONFIG:     return "Doorbell Buffer Config";
        case NVME_ADMIN_CMD_NVME_OVER_FABRICS:      return "NVMe Over Fabric";
        case NVME_ADMIN_CMD_FORMAT_NVM: return "Format NVM";
        case NVME_ADMIN_CMD_SECURITY_SEND:  return "Security Send";
        case NVME_ADMIN_CMD_SECURITY_RECV:  return "Security Receive";
        case NVME_ADMIN_CMD_SANITIZE:   return "Sanitize";
        }
    } else {
        switch (opcode) {
        case NVME_CMD_FLUSH:        return "Flush";
        case NVME_CMD_WRITE:        return "Write";
        case NVME_CMD_READ:     return "Read";
        case NVME_CMD_WRITE_UNCOR:  return "Write Uncorrectable";
        case NVME_CMD_COMPARE:      return "Compare";
        case NVME_CMD_WRITE_ZEROS:  return "Write Zeroes";
        case NVME_CMD_DATA_SET_MANAGEMENT:      return "Dataset Management";
        case NVME_CMD_RESERVATION_REGISTER: return "Reservation Register";
        case NVME_CMD_RESERVATION_REPORT:   return "Reservation Report";
        case NVME_CMD_RESERVATION_ACQUIRE:  return "Reservation Acquire";
        case NVME_CMD_RESERVATION_RELEASE:  return "Reservation Release";
        }
    }

    return "Unknown";
}

eReturnValues nvme_Get_SMART_Log_Page(tDevice *device, uint32_t nsid, uint8_t * pData, uint32_t dataLen)
{
    eReturnValues ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
    nvmeSmartLog * smartLog; // in case we need to align memory
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    if ((pData == M_NULLPTR) || (dataLen < NVME_SMART_HEALTH_LOG_LEN))
    {
        return ret;
    }

    memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    smartLog = C_CAST(nvmeSmartLog *, pData);

    cmdOpts.nsid = nsid;
    //cmdOpts.addr = C_CAST(uint64_t, smartLog);
    cmdOpts.addr = C_CAST(uint8_t*, smartLog);
    cmdOpts.dataLen = NVME_SMART_HEALTH_LOG_LEN;
    cmdOpts.lid = NVME_LOG_SMART_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_ERROR_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
{
    eReturnValues ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    //Should be able to pull at least one entry. 
    if ((pData == M_NULLPTR) || (dataLen < sizeof(nvmeErrLogEntry)))
    {
        return ret;
    }

    memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid = NVME_LOG_ERROR_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_FWSLOTS_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
{
    eReturnValues ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    //Should be able to pull at least one entry. 
    if ((pData == M_NULLPTR) || (dataLen < sizeof(nvmeFirmwareSlotInfo)))
    {
        return ret;
    }

    memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid = NVME_LOG_FW_SLOT_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_CmdSptEfft_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
{
    eReturnValues ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    //Should be able to pull at least one entry. 
    if ((pData == M_NULLPTR) || (dataLen < sizeof(nvmeFirmwareSlotInfo)))
    {
        return ret;
    }

    memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid = NVME_LOG_CMD_SPT_EFET_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues nvme_Get_DevSelfTest_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
{
    eReturnValues ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    //Should be able to pull at least one entry. 
    if ((pData == M_NULLPTR) || (dataLen < sizeof(nvmeFirmwareSlotInfo)))
    {
        return ret;
    }

    memset(&cmdOpts, 0, sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr = pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid = NVME_LOG_DEV_SELF_TEST_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//Seagate unique?
eReturnValues nvme_Read_Ext_Smt_Log(tDevice *device, EXTENDED_SMART_INFO_T *ExtdSMARTInfo)
{
    eReturnValues ret = SUCCESS;
    nvmeGetLogPageCmdOpts getExtSMARTLog;
    memset(&getExtSMARTLog, 0, sizeof(nvmeGetLogPageCmdOpts));
    getExtSMARTLog.dataLen = sizeof(EXTENDED_SMART_INFO_T);
    getExtSMARTLog.lid = 0xC4;
    getExtSMARTLog.nsid = device->drive_info.namespaceID;
    getExtSMARTLog.addr = C_CAST(uint8_t*, ExtdSMARTInfo);

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
