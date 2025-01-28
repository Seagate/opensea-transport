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
#if defined(ENABLE_OFNVME)

#    include "bit_manip.h"
#    include "code_attributes.h"
#    include "common_types.h"
#    include "error_translation.h"
#    include "math_utils.h"
#    include "memory_safety.h"
#    include "precision_timer.h"
#    include "string_utils.h"
#    include "type_conversion.h"

#    include "cmds.h"
#    include "nvme_helper_func.h"
#    include "of_nvme_helper.h"
#    include "of_nvme_helper_func.h"
#    include "scsi_helper_func.h"
#    include "sntl_helper.h"
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
DISABLE_WARNING_4255
#    include <windows.h>
RESTORE_WARNING_4255
#    if defined(_DEBUG) && !defined(OFNVME_DEBUG)
#        define OFNVME_DEBUG
#    endif //_DEBUG && !OFNVME_DEBUG

static void print_Ofnvme_SRB_Status(uint32_t srbStatus)
{
    switch (srbStatus)
    {
    case NVME_IOCTL_SUCCESS:
        printf("Success\n");
        break;
    case NVME_IOCTL_INTERNAL_ERROR:
        printf("Internal Error\n");
        break;
    case NVME_IOCTL_INVALID_IOCTL_CODE:
        printf("Invalid IOCTL Code\n");
        break;
    case NVME_IOCTL_INVALID_SIGNATURE:
        printf("Invalid Signature\n");
        break;
    case NVME_IOCTL_INSUFFICIENT_IN_BUFFER:
        printf("Insufficient In Buffer\n");
        break;
    case NVME_IOCTL_INSUFFICIENT_OUT_BUFFER:
        printf("Insufficient Out Buffer\n");
        break;
    case NVME_IOCTL_UNSUPPORTED_ADMIN_CMD:
        printf("Unsupported Admin Command\n");
        break;
    case NVME_IOCTL_UNSUPPORTED_NVM_CMD:
        printf("Unsupported NVM Command\n");
        break;
    case NVME_IOCTL_UNSUPPORTED_OPERATION:
        printf("Unsupported Operation\n");
        break;
    case NVME_IOCTL_INVALID_ADMIN_VENDOR_SPECIFIC_OPCODE:
        printf("Invalid Admin Vendor Specific Opcode\n");
        break;
    case NVME_IOCTL_INVALID_NVM_VENDOR_SPECIFIC_OPCODE:
        printf("Invalid NVM Vendor Specific Opcode\n");
        break;
    case NVME_IOCTL_ADMIN_VENDOR_SPECIFIC_NOT_SUPPORTED: // i.e., AVSCC = 0
        printf("Admin Vendor Specific Not Supported\n");
        break;
    case NVME_IOCTL_NVM_VENDOR_SPECIFIC_NOT_SUPPORTED: // i.e., NVSCC = 0
        printf("NVM Vendor Specific Not Supported\n");
        break;
    case NVME_IOCTL_INVALID_DIRECTION_SPECIFIED: // Direction > 3
        printf("Invalid Direction Specified\n");
        break;
    case NVME_IOCTL_INVALID_META_BUFFER_LENGTH:
        printf("Invalid Meta Buffer Length\n");
        break;
    case NVME_IOCTL_PRP_TRANSLATION_ERROR:
        printf("PRP Translation Error\n");
        break;
    case NVME_IOCTL_INVALID_PATH_TARGET_ID:
        printf("Invalid Path Target ID\n");
        break;
    case NVME_IOCTL_FORMAT_NVM_PENDING: // Only one Format NVM at a time
        printf("Format NVM Pending\n");
        break;
    case NVME_IOCTL_FORMAT_NVM_FAILED:
        printf("Format NVM Failed\n");
        break;
    case NVME_IOCTL_INVALID_NAMESPACE_ID:
        printf("Invalid Namespace ID\n");
        break;
    case NVME_IOCTL_MAX_SSD_NAMESPACES_REACHED:
        printf("Max SSD Namespaces Reached\n");
        break;
    case NVME_IOCTL_ZERO_DATA_TX_LENGTH_ERROR:
        printf("Zero Data TX Length Error\n");
        break;
    case NVME_IOCTL_MAX_AER_REACHED:
        printf("Max AER reached\n");
        break;
    case NVME_IOCTL_ATTACH_NAMESPACE_FAILED:
        printf("Attach Namespace Failed\n");
        break;
    }
}

// Need to setup an admin identify and try sending it. If this device doesn't support this IOCTL, it should fail,
// otherwise it will work. This is the same way the sample app works. Would be better if there was some other buffer to
// just return and validate that reported the driver name, version, etc
bool supports_OFNVME_IO(HANDLE deviceHandle)
{
    bool     supported  = false;
    uint32_t bufferSize = sizeof(NVME_PASS_THROUGH_IOCTL) + UINT32_C(4096);
    uint8_t* passthroughBuffer =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(bufferSize, sizeof(uint8_t), sizeof(void*)));
    if (passthroughBuffer)
    {
        DECLARE_SEATIMER(commandTimer);
        BOOL                     success = TRUE;
        PNVME_PASS_THROUGH_IOCTL ioctl   = C_CAST(PNVME_PASS_THROUGH_IOCTL, passthroughBuffer);
        ioctl->SrbIoCtrl.HeaderLength    = sizeof(SRB_IO_CONTROL);
        safe_memcpy(ioctl->SrbIoCtrl.Signature, 8, NVME_SIG_STR, NVME_SIG_STR_LEN);
        ioctl->SrbIoCtrl.ControlCode = C_CAST(ULONG, NVME_PASS_THROUGH_SRB_IO_CODE);
        ioctl->SrbIoCtrl.Length      = C_CAST(ULONG, bufferSize - sizeof(SRB_IO_CONTROL));
        ioctl->SrbIoCtrl.Timeout     = 15;

        ioctl->QueueId     = 0;    // admin queue
        ioctl->NVMeCmd[0]  = 0x06; // identify
        ioctl->NVMeCmd[10] = 1;    // admin identify

        // Set these to zero like the sample code does
        ioctl->VendorSpecific[0] = 0;
        ioctl->VendorSpecific[1] = 0;

        ioctl->Direction       = NVME_FROM_DEV_TO_HOST; // read or non-data
        ioctl->MetaDataLen     = 0;                     // no metadata or interleaved should be set to zero.
        ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL) + 4096;
        ioctl->DataBufferLen   = 0;  // Set to zero because we aren't sending any data, only receiving.
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        DWORD      last_error = ERROR_SUCCESS;
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        DWORD returned_data     = DWORD_C(0);
        start_Timer(&commandTimer);
        success    = DeviceIoControl(deviceHandle, IOCTL_SCSI_MINIPORT, ioctl, bufferSize, ioctl, bufferSize,
                                     &returned_data, &overlappedStruct);
        last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            last_error) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                        // flag, everything will work like old synchronous code.-TJE
        {
            success    = GetOverlappedResult(deviceHandle, &overlappedStruct, &returned_data, TRUE);
            last_error = GetLastError();
        }
        else if (last_error != ERROR_SUCCESS)
        {
            supported = false;
        }
        stop_Timer(&commandTimer);
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
        }
        if (MSFT_BOOL_TRUE(success))
        {
            supported = true;
        }
        else
        {
            supported = false;
        }
        safe_free_aligned(&passthroughBuffer);
    }
    return supported;
}

eReturnValues send_OFNVME_Reset(tDevice* device)
{
    eReturnValues  ret = OS_COMMAND_NOT_AVAILABLE; // Start with this since older drivers may or may not support this.
    SRB_IO_CONTROL ofnvmeReset;
    safe_memset(&ofnvmeReset, sizeof(SRB_IO_CONTROL), 0, sizeof(SRB_IO_CONTROL));

    ofnvmeReset.HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(ofnvmeReset.Signature, 8, NVME_SIG_STR, NVME_SIG_STR_LEN);
    ofnvmeReset.ControlCode = C_CAST(ULONG, NVME_RESET_DEVICE);
    ofnvmeReset.Length      = sizeof(SRB_IO_CONTROL);

    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    DWORD returned_data     = DWORD_C(0);
    BOOL  success =
        DeviceIoControl(device->os_info.scsiSRBHandle, IOCTL_SCSI_MINIPORT, &ofnvmeReset, sizeof(SRB_IO_CONTROL),
                        &ofnvmeReset, sizeof(SRB_IO_CONTROL), &returned_data, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        device->os_info.last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                    // overlapped flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(device->os_info.scsiSRBHandle, &overlappedStruct, &returned_data, TRUE);
        device->os_info.last_error = GetLastError();
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    if (MSFT_BOOL_TRUE(success))
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("OFNVME Error: ");
            print_Ofnvme_SRB_Status(ofnvmeReset.ReturnCode);
        }
        switch (ofnvmeReset.ReturnCode)
        {
        case NVME_IOCTL_SUCCESS:
            ret = SUCCESS;
            break;
        case NVME_IOCTL_INVALID_IOCTL_CODE:
            ret = OS_COMMAND_NOT_AVAILABLE;
            break;
        default:
            ret = FAILURE;
            break;
        }
    }
    return ret;
}

eReturnValues send_OFNVME_Add_Namespace(tDevice* device)
{
    eReturnValues  ret = OS_COMMAND_NOT_AVAILABLE; // Start with this since older drivers may or may not support this.
    SRB_IO_CONTROL ofnvmeReset;
    safe_memset(&ofnvmeReset, sizeof(SRB_IO_CONTROL), 0, sizeof(SRB_IO_CONTROL));

    ofnvmeReset.HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(ofnvmeReset.Signature, 8, NVME_SIG_STR, NVME_SIG_STR_LEN);
    ofnvmeReset.ControlCode = C_CAST(ULONG, NVME_HOT_ADD_NAMESPACE);
    ofnvmeReset.Length      = sizeof(SRB_IO_CONTROL);

    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    DWORD returned_data     = DWORD_C(0);
    BOOL  success =
        DeviceIoControl(device->os_info.scsiSRBHandle, IOCTL_SCSI_MINIPORT, &ofnvmeReset, sizeof(SRB_IO_CONTROL),
                        &ofnvmeReset, sizeof(SRB_IO_CONTROL), &returned_data, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        device->os_info.last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                    // overlapped flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(device->os_info.scsiSRBHandle, &overlappedStruct, &returned_data, TRUE);
        device->os_info.last_error = GetLastError();
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    if (MSFT_BOOL_TRUE(success))
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("OFNVME Error: ");
            print_Ofnvme_SRB_Status(ofnvmeReset.ReturnCode);
        }
        switch (ofnvmeReset.ReturnCode)
        {
        case NVME_IOCTL_SUCCESS:
            ret = SUCCESS;
            break;
        case NVME_IOCTL_INVALID_IOCTL_CODE:
            ret = OS_COMMAND_NOT_AVAILABLE;
            break;
        default:
            ret = FAILURE;
            break;
        }
    }
    return ret;
}

eReturnValues send_OFNVME_Remove_Namespace(tDevice* device)
{
    eReturnValues  ret = OS_COMMAND_NOT_AVAILABLE; // Start with this since older drivers may or may not support this.
    SRB_IO_CONTROL ofnvmeReset;
    safe_memset(&ofnvmeReset, sizeof(SRB_IO_CONTROL), 0, sizeof(SRB_IO_CONTROL));

    ofnvmeReset.HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(ofnvmeReset.Signature, 8, NVME_SIG_STR, NVME_SIG_STR_LEN);
    ofnvmeReset.ControlCode = C_CAST(ULONG, NVME_HOT_REMOVE_NAMESPACE);
    ofnvmeReset.Length      = sizeof(SRB_IO_CONTROL);

    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    DWORD returned_data     = DWORD_C(0);
    BOOL  success =
        DeviceIoControl(device->os_info.scsiSRBHandle, IOCTL_SCSI_MINIPORT, &ofnvmeReset, sizeof(SRB_IO_CONTROL),
                        &ofnvmeReset, sizeof(SRB_IO_CONTROL), &returned_data, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        device->os_info.last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                    // overlapped flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(device->os_info.scsiSRBHandle, &overlappedStruct, &returned_data, TRUE);
        device->os_info.last_error = GetLastError();
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    if (MSFT_BOOL_TRUE(success))
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("OFNVME Error: ");
            print_Ofnvme_SRB_Status(ofnvmeReset.ReturnCode);
        }
        switch (ofnvmeReset.ReturnCode)
        {
        case NVME_IOCTL_SUCCESS:
            ret = SUCCESS;
            break;
        case NVME_IOCTL_INVALID_IOCTL_CODE:
            ret = OS_COMMAND_NOT_AVAILABLE;
            break;
        default:
            ret = FAILURE;
            break;
        }
    }
    return ret;
}

eReturnValues send_OFNVME_IO(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
#    if defined(OFNVME_DEBUG)
    printf("ofnvme: NVM passthrough request\n");
#    endif // OFNVME_DEBUG
    uint32_t bufferSize =
        sizeof(NVME_PASS_THROUGH_IOCTL) +
        nvmeIoCtx->dataSize; // NOTE: No metadata. Don't think Windows supports a separate metadata buffer
    uint8_t* passthroughBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(bufferSize, sizeof(uint8_t), nvmeIoCtx->device->os_info.minimumAlignment));
    if (passthroughBuffer)
    {
        DECLARE_SEATIMER(commandTimer);
        BOOL                     success = TRUE;
        PNVME_PASS_THROUGH_IOCTL ioctl   = C_CAST(PNVME_PASS_THROUGH_IOCTL, passthroughBuffer);
        ioctl->SrbIoCtrl.HeaderLength    = sizeof(SRB_IO_CONTROL);
        safe_memcpy(ioctl->SrbIoCtrl.Signature, 8, NVME_SIG_STR, NVME_SIG_STR_LEN);
        ioctl->SrbIoCtrl.ControlCode = C_CAST(ULONG, NVME_PASS_THROUGH_SRB_IO_CODE);
        ioctl->SrbIoCtrl.Length      = C_CAST(ULONG, bufferSize - sizeof(SRB_IO_CONTROL));
        ioctl->SrbIoCtrl.Timeout     = nvmeIoCtx->timeout;

        // setup NVMe DWORDS based on NVM or ADMIN command
        // Set queue ID. 0 for admin, something else otherwise
        switch (nvmeIoCtx->commandType)
        {
        case NVM_ADMIN_CMD:
            ioctl->QueueId    = 0; // admin queue
            ioctl->NVMeCmd[0] = nvmeIoCtx->cmd.adminCmd
                                    .opcode; // This doesn't currently take into account fused or PRP vs SGL transfers
            ioctl->NVMeCmd[1] = nvmeIoCtx->cmd.adminCmd.nsid;
            ioctl->NVMeCmd[2] = nvmeIoCtx->cmd.adminCmd.cdw2;
            ioctl->NVMeCmd[3] = nvmeIoCtx->cmd.adminCmd.cdw3;
            // data pointers are in next DWORDs not sure if these should be set here since they will be virtual
            // addresses
            ioctl->NVMeCmd[10] = nvmeIoCtx->cmd.adminCmd.cdw10;
            ioctl->NVMeCmd[11] = nvmeIoCtx->cmd.adminCmd.cdw11;
            ioctl->NVMeCmd[12] = nvmeIoCtx->cmd.adminCmd.cdw12;
            ioctl->NVMeCmd[13] = nvmeIoCtx->cmd.adminCmd.cdw13;
            ioctl->NVMeCmd[14] = nvmeIoCtx->cmd.adminCmd.cdw14;
            ioctl->NVMeCmd[15] = nvmeIoCtx->cmd.adminCmd.cdw15;
            break;
        case NVM_CMD:
            ioctl->QueueId = 1; // should this always be set to 1? or something else depending on capabitlies?
            ioctl->NVMeCmd[0] =
                nvmeIoCtx->cmd.nvmCmd.opcode; // This doesn't currently take into account fused or PRP vs SGL transfers
            ioctl->NVMeCmd[1] = nvmeIoCtx->cmd.nvmCmd.nsid;
            ioctl->NVMeCmd[2] = nvmeIoCtx->cmd.nvmCmd.cdw2;
            ioctl->NVMeCmd[3] = nvmeIoCtx->cmd.nvmCmd.cdw3;
            // data pointers are in next DWORDs not sure if these should be set here since they will be virtual
            // addresses
            ioctl->NVMeCmd[10] = nvmeIoCtx->cmd.nvmCmd.cdw10;
            ioctl->NVMeCmd[11] = nvmeIoCtx->cmd.nvmCmd.cdw11;
            ioctl->NVMeCmd[12] = nvmeIoCtx->cmd.nvmCmd.cdw12;
            ioctl->NVMeCmd[13] = nvmeIoCtx->cmd.nvmCmd.cdw13;
            ioctl->NVMeCmd[14] = nvmeIoCtx->cmd.nvmCmd.cdw14;
            ioctl->NVMeCmd[15] = nvmeIoCtx->cmd.nvmCmd.cdw15;
            break;
        case NVM_UNKNOWN_CMD_SET:
            // Fallthrough to default
        default:
            safe_free_aligned(&passthroughBuffer);
            return BAD_PARAMETER;
        }

        // Setting to zero like the sample file does. No idea what these are used for.
        ioctl->VendorSpecific[0] = 0;
        ioctl->VendorSpecific[1] = 0;

        // databuffer length...set to zero for read or non-data. set to a value for sending data to the device
        // set direction along with length
        // set return buffer length...at least size of the IOCTL structure, but larger if expecting data from the
        // device...whatever that length will be
        switch (nvmeIoCtx->commandDirection)
        {
        case XFER_NO_DATA:
            ioctl->DataBufferLen   = 0;
            ioctl->Direction       = NVME_NO_DATA_TX;
            ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL);
            break;
        case XFER_DATA_IN:
            ioctl->Direction       = NVME_FROM_DEV_TO_HOST; // read or non-data
            ioctl->MetaDataLen     = 0;                     // no metadata or interleaved should be set to zero.
            ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL) +
                                     nvmeIoCtx->dataSize; // this should include metadata size when supporting metadata!
            ioctl->DataBufferLen = 0; // Set to zero because we aren't sending any data, only receiving.
            break;
        case XFER_DATA_OUT:
            ioctl->Direction       = NVME_FROM_HOST_TO_DEV;
            ioctl->MetaDataLen     = 0; // no metadata or interleaved should be set to zero.
            ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL); // only the size of the structure since we aren't
                                                                      // reading anything back from the device.
            ioctl->DataBufferLen =
                nvmeIoCtx->dataSize; // NOTE: This size is supposed to include metadata! It also depends on if metadata
                                     // is interleaved or at the beginning of the buffer
            safe_memcpy(ioctl->DataBuffer, nvmeIoCtx->dataSize, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
            break;
        case XFER_DATA_IN_OUT:
        case XFER_DATA_OUT_IN:
            // TODO: Handle bidirectional transfers!!!
            // NVME_BI_DIRECTION
        default:
            safe_free_aligned(&passthroughBuffer);
            return BAD_PARAMETER;
        }

        // do device io control here
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        nvmeIoCtx->device->os_info.last_error = 0;
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        DWORD returned_data     = DWORD_C(0);
        start_Timer(&commandTimer);
        success = DeviceIoControl(nvmeIoCtx->device->os_info.scsiSRBHandle, IOCTL_SCSI_MINIPORT, ioctl, bufferSize,
                                  ioctl, bufferSize, &returned_data, &overlappedStruct);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            nvmeIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
        {
            success =
                GetOverlappedResult(nvmeIoCtx->device->os_info.scsiSRBHandle, &overlappedStruct, &returned_data, TRUE);
            nvmeIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
        }
        if (MSFT_BOOL_TRUE(success))
        {
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("OFNVME Error: ");
                print_Ofnvme_SRB_Status(ioctl->SrbIoCtrl.ReturnCode);
            }
            if (ioctl->SrbIoCtrl.ReturnCode == NVME_IOCTL_SUCCESS)
            {
                if (nvmeIoCtx->commandDirection == XFER_DATA_IN)
                {
                    // copy back in the data that was read from the device.
                    safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, ioctl->DataBuffer,
                                M_Min(nvmeIoCtx->dataSize, ioctl->ReturnBufferLen - sizeof(NVME_PASS_THROUGH_IOCTL)));
                }
                // copy back completion data
                nvmeIoCtx->commandCompletionData.commandSpecific = ioctl->CplEntry[0];
                nvmeIoCtx->commandCompletionData.dw1Reserved     = ioctl->CplEntry[1];
                nvmeIoCtx->commandCompletionData.sqIDandHeadPtr  = ioctl->CplEntry[2];
                nvmeIoCtx->commandCompletionData.statusAndCID    = ioctl->CplEntry[3];
                nvmeIoCtx->commandCompletionData.dw0Valid        = true;
                nvmeIoCtx->commandCompletionData.dw1Valid        = true;
                nvmeIoCtx->commandCompletionData.dw2Valid        = true;
                nvmeIoCtx->commandCompletionData.dw3Valid        = true;
                ret                                              = SUCCESS;
            }
            else
            {
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }
        else
        {
            switch (nvmeIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_NOT_SUPPORTED: // this is what is returned when we try to send a sanitize command in Win10
                ret = OS_COMMAND_BLOCKED;
                break;
            case ERROR_IO_DEVICE: // OS_PASSTHROUGH_FAILURE
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
            }
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("OFNVME Error: ");
                print_Ofnvme_SRB_Status(ioctl->SrbIoCtrl.ReturnCode);
            }
        }

        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

        safe_free_aligned(&passthroughBuffer);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
#    if defined(OFNVME_DEBUG)
    printf("ofnvme: NVM passthrough request result = %u\n", ret);
#    endif
    return ret;
}
#else

#endif // ENABLE_OFNVME
