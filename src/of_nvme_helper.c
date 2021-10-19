//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#if defined (ENABLE_OFNVME)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "common.h"
#include "of_nvme_helper.h"
#include "of_nvme_helper_func.h"
#include "cmds.h"
#include "sntl_helper.h"
#include "scsi_helper_func.h"
#include "nvme_helper_func.h"

//Need to setup an admin identify and try sending it. If this device doesn't support this IOCTL, it should fail, otherwise it will work.
//This is the same way the sample app works. Would be better if there was some other buffer to just return and validate that reported the driver name, version, etc
bool supports_OFNVME_IO(HANDLE deviceHandle)
{
    bool supported = false;
    uint32_t bufferSize = sizeof(NVME_PASS_THROUGH_IOCTL) + UINT32_C(4096);
    uint8_t *passthroughBuffer = C_CAST(uint8_t*, calloc_aligned(bufferSize, sizeof(uint8_t), sizeof(void*)));
    if (passthroughBuffer)
    {
        seatimer_t commandTimer;
        BOOL success = TRUE;
        PNVME_PASS_THROUGH_IOCTL ioctl = C_CAST(PNVME_PASS_THROUGH_IOCTL, passthroughBuffer);
        ioctl->SrbIoCtrl.HeaderLength = sizeof(SRB_IO_CONTROL);
        memcpy(ioctl->SrbIoCtrl.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
        ioctl->SrbIoCtrl.ControlCode = C_CAST(ULONG, NVME_PASS_THROUGH_SRB_IO_CODE);
        ioctl->SrbIoCtrl.Length = bufferSize - sizeof(SRB_IO_CONTROL);
        ioctl->SrbIoCtrl.Timeout = 15;

        memset(&commandTimer, 0, sizeof(seatimer_t));

        ioctl->QueueId = 0;//admin queue
        ioctl->NVMeCmd[0] = 0x06;//identify
        ioctl->NVMeCmd[10] = 1;//admin identify

        //Set these to zero like the sample code does
        ioctl->VendorSpecific[0] = 0;
        ioctl->VendorSpecific[1] = 0;

        ioctl->Direction = NVME_FROM_DEV_TO_HOST;//read or non-data
        ioctl->MetaDataLen = 0;//no metadata or interleaved should be set to zero.
        ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL) + 4096;
        ioctl->DataBufferLen = 0;//Set to zero because we aren't sending any data, only receiving.
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        DWORD last_error = ERROR_SUCCESS;
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        DWORD returned_data = 0;
        start_Timer(&commandTimer);
        success = DeviceIoControl(deviceHandle,
            IOCTL_SCSI_MINIPORT,
            ioctl,
            bufferSize,
            ioctl,
            bufferSize,
            &returned_data,
            &overlappedStruct);
        last_error = GetLastError();
        if (ERROR_IO_PENDING == last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(deviceHandle, &overlappedStruct, &returned_data, TRUE);
            last_error = GetLastError();
        }
        else if (last_error != ERROR_SUCCESS)
        {
            supported = false;
        }
        stop_Timer(&commandTimer);
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = NULL;
        }
        if (success)
        {
            supported = true;
        }
        else
        {
            supported = false;
        }
        safe_Free_aligned(passthroughBuffer)
    }
    return supported;
}

int send_OFNVME_Reset(tDevice * device)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;//Start with this since older drivers may or may not support this.
    SRB_IO_CONTROL ofnvmeReset;
    memset(&ofnvmeReset, 0, sizeof(SRB_IO_CONTROL));

    ofnvmeReset.HeaderLength = sizeof(SRB_IO_CONTROL);
    memcpy(ofnvmeReset.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
    ofnvmeReset.ControlCode = C_CAST(ULONG, NVME_RESET_DEVICE);
    ofnvmeReset.Length = sizeof(SRB_IO_CONTROL);

    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD returned_data = 0;
    BOOL success = DeviceIoControl(device->os_info.scsiSRBHandle,
        IOCTL_SCSI_MINIPORT,
        &ofnvmeReset,
        sizeof(SRB_IO_CONTROL),
        &ofnvmeReset,
        sizeof(SRB_IO_CONTROL),
        &returned_data,
        &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
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
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
    }
    if (success)
    {
        //TODO: Check the SRB_IO_CONTROL return code and check if it was successful or not. For now, if it reports success, we'll call it done. - TJE
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

int send_OFNVME_Add_Namespace(tDevice * device)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;//Start with this since older drivers may or may not support this.
    SRB_IO_CONTROL ofnvmeReset;
    memset(&ofnvmeReset, 0, sizeof(SRB_IO_CONTROL));

    ofnvmeReset.HeaderLength = sizeof(SRB_IO_CONTROL);
    memcpy(ofnvmeReset.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
    ofnvmeReset.ControlCode = C_CAST(ULONG, NVME_HOT_ADD_NAMESPACE);
    ofnvmeReset.Length = sizeof(SRB_IO_CONTROL);

    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD returned_data = 0;
    BOOL success = DeviceIoControl(device->os_info.scsiSRBHandle,
        IOCTL_SCSI_MINIPORT,
        &ofnvmeReset,
        sizeof(SRB_IO_CONTROL),
        &ofnvmeReset,
        sizeof(SRB_IO_CONTROL),
        &returned_data,
        &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
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
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
    }
    if (success)
    {
        //TODO: Check the SRB_IO_CONTROL return code and check if it was successful or not. For now, if it reports success, we'll call it done. - TJE
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

int send_OFNVME_Remove_Namespace(tDevice * device)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;//Start with this since older drivers may or may not support this.
    SRB_IO_CONTROL ofnvmeReset;
    memset(&ofnvmeReset, 0, sizeof(SRB_IO_CONTROL));

    ofnvmeReset.HeaderLength = sizeof(SRB_IO_CONTROL);
    memcpy(ofnvmeReset.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
    ofnvmeReset.ControlCode = C_CAST(ULONG, NVME_HOT_REMOVE_NAMESPACE);
    ofnvmeReset.Length = sizeof(SRB_IO_CONTROL);

    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD returned_data = 0;
    BOOL success = DeviceIoControl(device->os_info.scsiSRBHandle,
        IOCTL_SCSI_MINIPORT,
        &ofnvmeReset,
        sizeof(SRB_IO_CONTROL),
        &ofnvmeReset,
        sizeof(SRB_IO_CONTROL),
        &returned_data,
        &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
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
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
    }
    if (success)
    {
        //TODO: Check the SRB_IO_CONTROL return code and check if it was successful or not. For now, if it reports success, we'll call it done. - TJE
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

int send_OFNVME_IO(nvmeCmdCtx * nvmeIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;

    uint32_t bufferSize = sizeof(NVME_PASS_THROUGH_IOCTL) + nvmeIoCtx->dataSize;//TODO: add metadata. This will be returned first in the data buffer if there is any
    uint8_t *passthroughBuffer = C_CAST(uint8_t*, calloc_aligned(bufferSize, sizeof(uint8_t), nvmeIoCtx->device->os_info.minimumAlignment));
    if (passthroughBuffer)
    {
        seatimer_t commandTimer;
        BOOL success = TRUE;
        PNVME_PASS_THROUGH_IOCTL ioctl = C_CAST(PNVME_PASS_THROUGH_IOCTL, passthroughBuffer);
        ioctl->SrbIoCtrl.HeaderLength = sizeof(SRB_IO_CONTROL);
        memcpy(ioctl->SrbIoCtrl.Signature, NVME_SIG_STR, NVME_SIG_STR_LEN);
        ioctl->SrbIoCtrl.ControlCode = C_CAST(ULONG, NVME_PASS_THROUGH_SRB_IO_CODE);
        ioctl->SrbIoCtrl.Length = bufferSize - sizeof(SRB_IO_CONTROL);
        ioctl->SrbIoCtrl.Timeout = nvmeIoCtx->timeout;

        memset(&commandTimer, 0, sizeof(seatimer_t));

        //setup NVMe DWORDS based on NVM or ADMIN command
        //Set queue ID. 0 for admin, something else otherwise
        switch (nvmeIoCtx->commandType)
        {
        case NVM_ADMIN_CMD:
            ioctl->QueueId = 0;//admin queue
            ioctl->NVMeCmd[0] = nvmeIoCtx->cmd.adminCmd.opcode;//This doesn't currently take into account fused or PRP vs SGL transfers
            ioctl->NVMeCmd[1] = nvmeIoCtx->cmd.adminCmd.nsid;
            ioctl->NVMeCmd[2] = nvmeIoCtx->cmd.adminCmd.cdw2;
            ioctl->NVMeCmd[3] = nvmeIoCtx->cmd.adminCmd.cdw3;
            //data pointers are in next DWORDs not sure if these should be set here since they will be virtual addresses
            //TODO: fill in metadata and prp addresses
            ioctl->NVMeCmd[10] = nvmeIoCtx->cmd.adminCmd.cdw10;
            ioctl->NVMeCmd[11] = nvmeIoCtx->cmd.adminCmd.cdw11;
            ioctl->NVMeCmd[12] = nvmeIoCtx->cmd.adminCmd.cdw12;
            ioctl->NVMeCmd[13] = nvmeIoCtx->cmd.adminCmd.cdw13;
            ioctl->NVMeCmd[14] = nvmeIoCtx->cmd.adminCmd.cdw14;
            ioctl->NVMeCmd[15] = nvmeIoCtx->cmd.adminCmd.cdw15;
            break;
        case NVM_CMD:
            ioctl->QueueId = 1;//TODO: should this always be set to 1? or something else depending on capabitlies?
            ioctl->NVMeCmd[0] = nvmeIoCtx->cmd.nvmCmd.opcode;//This doesn't currently take into account fused or PRP vs SGL transfers
            ioctl->NVMeCmd[1] = nvmeIoCtx->cmd.nvmCmd.nsid;
            ioctl->NVMeCmd[2] = nvmeIoCtx->cmd.nvmCmd.cdw2;
            ioctl->NVMeCmd[3] = nvmeIoCtx->cmd.nvmCmd.cdw3;
            //data pointers are in next DWORDs not sure if these should be set here since they will be virtual addresses
            //TODO: fill in metadata and prp addresses
            ioctl->NVMeCmd[10] = nvmeIoCtx->cmd.nvmCmd.cdw10;
            ioctl->NVMeCmd[11] = nvmeIoCtx->cmd.nvmCmd.cdw11;
            ioctl->NVMeCmd[12] = nvmeIoCtx->cmd.nvmCmd.cdw12;
            ioctl->NVMeCmd[13] = nvmeIoCtx->cmd.nvmCmd.cdw13;
            ioctl->NVMeCmd[14] = nvmeIoCtx->cmd.nvmCmd.cdw14;
            ioctl->NVMeCmd[15] = nvmeIoCtx->cmd.nvmCmd.cdw15;
            break;
        case NVM_UNKNOWN_CMD_SET:
            //Fallthrough to default
        default:
            safe_Free_aligned(passthroughBuffer)
            return BAD_PARAMETER;
        }

        //TODO: Handle setting vendor unique qualifiers for vendor unique commands. Not sure what those should be right now or how they are used by the driver code.
        //Setting to zero like the sample file does.
        ioctl->VendorSpecific[0] = 0;
        ioctl->VendorSpecific[1] = 0;

        //databuffer length...set to zero for read or non-data. set to a value for sending data to the device
        //set direction along with length
        //set return buffer length...at least size of the IOCTL structure, but larger if expecting data from the device...whatever that length will be
        switch (nvmeIoCtx->commandDirection)
        {
        case XFER_NO_DATA:
            ioctl->DataBufferLen = 0;
            ioctl->Direction = NVME_NO_DATA_TX;
            ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL);
            break;
        case XFER_DATA_IN:
            ioctl->Direction = NVME_FROM_DEV_TO_HOST;//read or non-data
            ioctl->MetaDataLen = 0;//no metadata or interleaved should be set to zero.
            ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL) + nvmeIoCtx->dataSize;//this should include metadata size when supporting metadata!
            ioctl->DataBufferLen = 0;//Set to zero because we aren't sending any data, only receiving.
            break;
        case XFER_DATA_OUT:
            ioctl->Direction = NVME_FROM_HOST_TO_DEV;
            ioctl->MetaDataLen = 0;//no metadata or interleaved should be set to zero.
            ioctl->ReturnBufferLen = sizeof(NVME_PASS_THROUGH_IOCTL);//only the size of the structure since we aren't reading anything back from the device.
            ioctl->DataBufferLen = nvmeIoCtx->dataSize;//NOTE: This size is supposed to include metadata! It also depends on if metadata is interleaved or at the beginning of the buffer
            memcpy(ioctl->DataBuffer, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
            break;
        case XFER_DATA_IN_OUT:
        case XFER_DATA_OUT_IN:
            //TODO: Handle bidirectional transfers!!!
            //NVME_BI_DIRECTION
        default:
            safe_Free_aligned(passthroughBuffer)
            return BAD_PARAMETER;
        }

        //do device io control here
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        nvmeIoCtx->device->os_info.last_error = 0;
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        DWORD returned_data = 0;
        start_Timer(&commandTimer);
        success = DeviceIoControl(nvmeIoCtx->device->os_info.scsiSRBHandle,
            IOCTL_SCSI_MINIPORT,
            ioctl,
            bufferSize,
            ioctl,
            bufferSize,
            &returned_data,
            &overlappedStruct);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == nvmeIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(nvmeIoCtx->device->os_info.scsiSRBHandle, &overlappedStruct, &returned_data, TRUE);
            nvmeIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = NULL;
        }
        if (success)
        {
            if (ioctl->SrbIoCtrl.ReturnCode == NVME_IOCTL_SUCCESS)
            {
                if (nvmeIoCtx->commandDirection == XFER_DATA_IN)
                {
                    //copy back in the data that was read from the device.
                    memcpy(nvmeIoCtx->ptrData, ioctl->DataBuffer, M_Min(nvmeIoCtx->dataSize, ioctl->ReturnBufferLen - sizeof(NVME_PASS_THROUGH_IOCTL)));
                }
                //copy back completion data
                nvmeIoCtx->commandCompletionData.commandSpecific = ioctl->CplEntry[0];
                nvmeIoCtx->commandCompletionData.dw1Reserved = ioctl->CplEntry[1];
                nvmeIoCtx->commandCompletionData.sqIDandHeadPtr = ioctl->CplEntry[2];
                nvmeIoCtx->commandCompletionData.statusAndCID = ioctl->CplEntry[3];
                nvmeIoCtx->commandCompletionData.dw0Valid = true;
                nvmeIoCtx->commandCompletionData.dw1Valid = true;
                nvmeIoCtx->commandCompletionData.dw2Valid = true;
                nvmeIoCtx->commandCompletionData.dw3Valid = true;
                ret = SUCCESS;
            }
            else
            {
                ret = OS_PASSTHROUGH_FAILURE;
                //TODO: translate driver return code to something printable in verbose output.
            }
        }
        else
        {
            switch (nvmeIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_NOT_SUPPORTED://this is what is returned when we try to send a sanitize command in Win10
                ret = OS_COMMAND_BLOCKED;
                break;
            case ERROR_IO_DEVICE://OS_PASSTHROUGH_FAILURE
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
            }
        }

        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

        safe_Free_aligned(passthroughBuffer)
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}
#else
;
#endif //ENABLE_OFNVME
