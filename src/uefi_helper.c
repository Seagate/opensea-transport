//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//

#include "common.h"
#include "common_platform.h"
#include "uefi_helper.h"
#include "cmds.h"
#include "sat_helper_func.h"
#include "sntl_helper.h"
//these are EDK2 include files
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>//to get global boot services pointer. This pointer should be checked before use, but any app using stdlib will have this set.
#include <Protocol/AtaPassThru.h>
#include <Protocol/ScsiPassThru.h>
#include <Protocol/ScsiPassThruExt.h>
#include <Protocol/DevicePath.h> //TODO: Add a function that can print out a device path???
#if !defined (DISABLE_NVME_PASSTHROUGH)
#include <Protocol/NvmExpressPassthru.h>
#endif
extern bool validate_Device_Struct(versionBlock);

//Define this to turn on extra prints to the screen for debugging UEFI passthrough issues.
#define UEFI_PASSTHRU_DEBUG_MESSAGES

#if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
//This color will be used for normal debug messages that are infomative. Critical memory allocation errors among other things are going to be RED.
eConsoleColors uefiDebugMessageColor = BLUE;
#endif

int get_Passthru_Protocol_Ptr(EFI_GUID ptGuid, void **pPassthru, uint32_t controllerID)
{
    int ret = SUCCESS;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_HANDLE *handle = NULL;
    UINTN nodeCount = 0;

    if (!gBS) //make sure global boot services pointer is valid before accessing it.
    {
        return MEMORY_FAILURE;
    }

    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &ptGuid, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }
    //NOTE: This code below assumes that the caller knows the controller they intend to open. Meaning they've already done some sort of system scan.
    uefiStatus = gBS->OpenProtocol( handle[controllerID], &ptGuid, pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    //TODO: based on the error code, rather than assuming failure, check for supported/not supported.
    if(EFI_ERROR(uefiStatus))
    {
        ret = FAILURE;
    }
    return ret;
}

int get_ATA_Passthru_Protocol_Ptr(EFI_ATA_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(ataPtGUID, (void**)pPassthru, controllerID);
}

int get_SCSI_Passthru_Protocol_Ptr(EFI_SCSI_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(scsiPtGUID, (void**)pPassthru, controllerID);
}

int get_Ext_SCSI_Passthru_Protocol_Ptr(EFI_EXT_SCSI_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(scsiPtGUID, (void**)pPassthru, controllerID);
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
int get_NVMe_Passthru_Protocol_Ptr(EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(nvmePtGUID, (void**)pPassthru, controllerID);
}
#endif

//filename (handle) formats:
//ata:<controllerID>:<port>:<portMultiplierPort>
//scsi:<controllerID>:<target>:<lun>
//scsiEx:<controllerID>:<target>:<lun>
//nvme:<controllerID>:<namespaceID>

int get_Device(const char *filename, tDevice *device)
{
    char interface[10] = { 0 };
    strcpy(device->os_info.name, filename);
    strcpy(device->os_info.friendlyName, filename);
    device->os_info.osType = OS_UEFI;
    if (strstr(filename, "ata"))
    {
        int res = sscanf(filename, "%3s:%" SCNx16 ":%" SCNx16 ":%" SCNx16, &interface, &device->os_info.controllerNum, & device->os_info.address.ata.port, &device->os_info.address.ata.portMultiplierPort);
        if(res >= 4 && res != EOF)
        {
            device->drive_info.interface_type = IDE_INTERFACE;
            device->drive_info.drive_type = ATA_DRIVE;
            device->os_info.passthroughType = UEFI_PASSTHROUGH_ATA;
            EFI_ATA_PASS_THRU_PROTOCOL *pPassthru;
            if (SUCCESS == get_ATA_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL *devicePath; //will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, device->os_info.address.ata.port, device->os_info.address.ata.portMultiplierPort, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    memcpy(&device->os_info.devicePath, devicePath, M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath);
            }
            else
            {
                return FAILURE;
            }
        }
        else
        {
            return FAILURE;
        }
    }
    else if (strstr(filename, "scsiEx"))
    {
        char targetAsString[32] = { 0 };
        int res = sscanf(filename, "%6s:%" SCNx16 ":%32s:%" SCNx64, &interface, &device->os_info.controllerNum, &targetAsString[0], &device->os_info.address.scsiEx.lun);
        if(res >= 4 && res != EOF)
        {
            int8_t targetIDIter = 15;
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.drive_type = SCSI_DRIVE;
            device->os_info.passthroughType = UEFI_PASSTHROUGH_SCSI_EXT;
            //TODO: validate convertion of targetAsString to the array we need to save for the targetID
            for(uint8_t iter = 0; iter < 32 && targetIDIter > 0; iter += 2, --targetIDIter)
            {
                char smallString[4] = { 0 };//need to break the string into two charaters at a time then convert that to a integer to save for target name
                sprintf(smallString, "%c%c", targetAsString[iter], targetAsString[iter + 1]);
                device->os_info.address.scsiEx.target[targetIDIter] = strtol(smallString, NULL, TARGET_MAX_BYTES);
            }
            EFI_EXT_SCSI_PASS_THRU_PROTOCOL *pPassthru;
            if (SUCCESS == get_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, (uint8_t*)(&device->os_info.address.scsiEx.target), device->os_info.address.scsiEx.lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    memcpy(&device->os_info.devicePath, devicePath, M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath);
            }
            else
            {
                return FAILURE;
            }
        }
    }
    else if (strstr(filename,  "scsi"))
    {
        int res = sscanf(filename, "%4s:%" SCNx16 ":%" SCNx32 ":%" SCNx64, &interface, &device->os_info.controllerNum, &device->os_info.address.scsi.target, &device->os_info.address.scsi.lun);
        if(res >=3 && res != EOF)
        {
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.drive_type = SCSI_DRIVE;
            device->os_info.passthroughType = UEFI_PASSTHROUGH_SCSI;
            EFI_SCSI_PASS_THRU_PROTOCOL *pPassthru;
            if (SUCCESS == get_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_SCSI_PASS_THRU_PROTOCOL *pPassthru;
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, device->os_info.address.scsi.target, device->os_info.address.scsi.lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    memcpy(&device->os_info.devicePath, devicePath, M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath);
            }
            else
            {
                return FAILURE;
            }
        }
        else
        {
            return FAILURE;
        }
    }
    #if !defined (DISABLE_NVME_PASSTHROUGH)
    else if (strstr(filename, "nvme"))
    {
        int res = sscanf(filename, "%4s:%" SCNx16 ":%" SCNx32, &interface, &device->os_info.controllerNum, &device->os_info.address.nvme.namespaceID);
        if(res >=3 && res != EOF)
        {
            device->drive_info.interface_type = NVME_INTERFACE;
            device->drive_info.drive_type = NVME_DRIVE;
            device->os_info.passthroughType = UEFI_PASSTHROUGH_NVME;
            EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *pPassthru;
            if (SUCCESS == get_NVMe_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, device->os_info.address.nvme.namespaceID, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    memcpy(&device->os_info.devicePath, devicePath, M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath);
            }
            else
            {
                return FAILURE;
            }
        }
        else
        {
            return FAILURE;
        }
    }
    #endif
    else
    {
        return NOT_SUPPORTED;
    }
    //fill in the drive info
    return fill_Drive_Info_Data(device);
}

int device_Reset(ScsiIoCtx *scsiIoCtx)
{
    //need to investigate if there is a way to do this in uefi
    return NOT_SUPPORTED;
}

int bus_Reset(ScsiIoCtx *scsiIoCtx)
{
    //need to investigate if there is a way to do this in uefi
    return NOT_SUPPORTED;
}

int send_UEFI_SCSI_Passthrough(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_SCSI_PASS_THRU_PROTOCOL *pPassthru;
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI SCSI Passthru Command\n");
    set_Console_Colors(true, DEFAULT);
    #endif
    if(SUCCESS == get_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum))
    {   
        uint8_t *alignedPointer = scsiIoCtx->pdata;
        uint8_t *alignedCDB = scsiIoCtx->cdb;
        uint8_t *alignedSensePtr = scsiIoCtx->psense;
        uint8_t *localBuffer = NULL;
        uint8_t *localCDB = NULL;
        uint8_t *localSensePtr = NULL;
        bool localAlignedBuffer = false, localSenseBuffer = false;
        EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET	*srp;//scsi request packet

        srp = (EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET *) calloc_aligned(1, sizeof(EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET), pPassthru->Mode->IoAlign);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Got SCSI Passthru protocol pointer:\n");
        if (pPassthru->Mode->Attributes & EFI_SCSI_PASS_THRU_ATTRIBUTES_PHYSICAL)
        {
            printf("\tphysical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_SCSI_PASS_THRU_ATTRIBUTES_LOGICAL)
        {
            printf("\tlogical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_SCSI_PASS_THRU_ATTRIBUTES_NONBLOCKIO)
        {
            printf("\tnon-blocking IO supported\n");
        }
        printf("\tIOAlignment required: %" PRIu32 "\n", pPassthru->Mode->IoAlign);
        printf("\tAdapterID: %" PRIu32 "\n", pPassthru->Mode->AdapterId);
        set_Console_Colors(true, DEFAULT);
        #endif

        if(scsiIoCtx->timeout == UINT32_MAX)
        {
            srp->Timeout = 0;//value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            srp->Timeout = scsiIoCtx->timeout * 1e-7;//value is in 100ns units. zero means wait indefinitely
        }

        alignedPointer = (uint8_t*)(((uint64_t)scsiIoCtx->pdata + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->pdata != alignedPointer)
        {
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                memcpy(alignedPointer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
        }

        alignedCDB = (uint8_t*)(((uint64_t)scsiIoCtx->cdb + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->cdb != alignedCDB)
        {
            //allocate an aligned buffer here!
            localCDB = (uint8_t *)calloc_aligned(scsiIoCtx->cdbLength, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned CDB pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedCDB = localCDB;
            //copy CDB into aligned CDB memory pointer
            memcpy(alignedCDB, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
        }

        alignedSensePtr = (uint8_t *)(((uint64_t)scsiIoCtx->psense + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->psense != alignedSensePtr)
        {
            //allocate an aligned buffer here!
            localSenseBuffer = true;
            localSensePtr = (uint8_t *)calloc_aligned(scsiIoCtx->senseDataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned sense data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedSensePtr = localSensePtr;
        }

        srp->DataBuffer = alignedPointer;
        srp->TransferLength = scsiIoCtx->dataLength;
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_OUT:
            srp->DataDirection = 1;
            break;
        case XFER_DATA_IN:
            srp->DataDirection = 0;
            break;
        case XFER_NO_DATA:
            srp->DataDirection = 0;
            break;
        case XFER_DATA_OUT_IN: //bidirectional command support not allowed with this type of passthru
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, uefiDebugMessageColor);
            printf("Send UEFI SCSI PT CMD NOT AVAILABLE\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            return OS_COMMAND_NOT_AVAILABLE;
        default:
            return BAD_PARAMETER;
        }
        srp->SenseData = alignedSensePtr;// Need to verify is this correct or not
        srp->CdbLength = scsiIoCtx->cdbLength;
        srp->SenseDataLength = scsiIoCtx->senseDataSize;
        srp->Cdb = alignedCDB;

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending SCSI Passthru command\n");
        printf("\t->TransferLength = %" PRIu32 "\n", srp->TransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.scsi.target, scsiIoCtx->device->os_info.address.scsi.lun, srp, NULL);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SCSI Passthru command returned %d\n", Status);
        printf("\t<-TransferLength = %" PRIu32 "\n", srp->TransferLength);
        printf("\tHost Adapter Status = %" PRIu8 "\t Target Status = %" PRIu8 "\n", srp->HostAdapterStatus, srp->TargetStatus);
        set_Console_Colors(true, DEFAULT);
        #endif
        //TODO: Check host adapter status and target status

        if (Status == EFI_SUCCESS)
        {
            ret = SUCCESS;
            if (localSenseBuffer)
            {
                memcpy(scsiIoCtx->psense, alignedSensePtr, M_Min(scsiIoCtx->senseDataSize, srp->SenseDataLength));
            }
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else if (Status == EFI_WRITE_PROTECTED)
        {
            ret = PERMISSION_DENIED;
        }
        else if (Status == EFI_DEVICE_ERROR)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        else if (Status == EFI_INVALID_PARAMETER || Status == EFI_NOT_FOUND)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        safe_Free_aligned(localBuffer);
        safe_Free_aligned(localCDB);
        safe_Free_aligned(localSensePtr);
        safe_Free_aligned(srp);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSI Passthru function returning %d\n", ret);
    set_Console_Colors(true, DEFAULT);
    #endif
    return ret;
}

//TODO: This was added later, prevously only SCSI passthrough existed. May need to add #if defiend (some UDK version)
int send_UEFI_SCSI_Passthrough_Ext(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_EXT_SCSI_PASS_THRU_PROTOCOL *pPassthru;
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI SCSIEx Passthru Command\n");
    set_Console_Colors(true, DEFAULT);
    #endif
    if(SUCCESS == get_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum))
    {
        uint8_t *alignedPointer = scsiIoCtx->pdata;
        uint8_t *alignedCDB = scsiIoCtx->cdb;
        uint8_t *alignedSensePtr = scsiIoCtx->psense;
        uint8_t *localBuffer = NULL;
        uint8_t *localCDB = NULL;
        uint8_t *localSensePtr = NULL;
        bool localAlignedBuffer = false, localSenseBuffer = false;
        EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET	*srp;// Extended scsi request packet

        srp = (EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET *) calloc_aligned(1, sizeof(EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET), pPassthru->Mode->IoAlign);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Got SCSIEx Passthru protocol pointer:\n");
        if (pPassthru->Mode->Attributes & EFI_EXT_SCSI_PASS_THRU_ATTRIBUTES_PHYSICAL)
        {
            printf("\tphysical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_EXT_SCSI_PASS_THRU_ATTRIBUTES_LOGICAL)
        {
            printf("\tlogical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_EXT_SCSI_PASS_THRU_ATTRIBUTES_NONBLOCKIO)
        {
            printf("\tnon-blocking IO supported\n");
        }
        printf("\tIOAlignment required: %" PRIu32 "\n", pPassthru->Mode->IoAlign);
        printf("\tAdapterID: %" PRIu32 "\n", pPassthru->Mode->AdapterId);
        set_Console_Colors(true, DEFAULT);
        #endif

        if(scsiIoCtx->timeout == UINT32_MAX)
        {
            srp->Timeout = 0;//value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            srp->Timeout = scsiIoCtx->timeout * 1e-7;//value is in 100ns units. zero means wait indefinitely
        }

        alignedPointer = (uint8_t*)(((uint64_t)scsiIoCtx->pdata + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->pdata != alignedPointer)
        {
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                memcpy(alignedPointer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
        }

        alignedCDB = (uint8_t*)(((uint64_t)scsiIoCtx->cdb + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->cdb != alignedCDB)
        {
            //allocate an aligned buffer here!
            localCDB = (uint8_t *)calloc_aligned(scsiIoCtx->cdbLength, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned CDB pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedCDB = localCDB;
            //copy CDB into aligned CDB memory pointer
            memcpy(alignedCDB, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
        }

        alignedSensePtr = (uint8_t *)(((uint64_t)scsiIoCtx->psense + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->psense != alignedSensePtr)
        {
            //allocate an aligned buffer here!
            localSenseBuffer = true;
            localSensePtr = (uint8_t *)calloc_aligned(scsiIoCtx->senseDataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned sense data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedSensePtr = localSensePtr;
        }


        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_OUT:
            srp->OutDataBuffer = alignedPointer;
            srp->InDataBuffer = NULL;
            srp->OutTransferLength = scsiIoCtx->dataLength;
            srp->DataDirection = 1;
            break;
        case XFER_DATA_IN:
            srp->InDataBuffer = alignedPointer;
            srp->OutDataBuffer = NULL;
            srp->InTransferLength = scsiIoCtx->dataLength;
            srp->DataDirection = 0;
            break;
        case XFER_NO_DATA:
            srp->OutDataBuffer = NULL;
            srp->OutDataBuffer = NULL;
            srp->DataDirection = 0;
            srp->InTransferLength = 0;
            srp->OutTransferLength = 0;
            break;
        //case XFER_DATA_OUT_IN: //TODO: bidirectional command support
            //srp->DataDirection = 2;//bidirectional command
        default:
            return BAD_PARAMETER;
        }
        srp->SenseData = alignedSensePtr;
        srp->CdbLength = scsiIoCtx->cdbLength;
        srp->SenseDataLength = scsiIoCtx->senseDataSize;
        srp->Cdb = alignedCDB;

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending SCSIEx Passthru command\n");
        printf("\t->InTransferLength = %" PRIu32 "\tOutTransferLength = %" PRIu32 "\n", srp->InTransferLength, srp->OutTransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.scsiEx.target, scsiIoCtx->device->os_info.address.scsiEx.lun, srp, NULL);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SCSIEx Passthru command returned %d\n", Status);
        printf("\t<-InTransferLength = %" PRIu32 "\tOutTransferLength = %" PRIu32 "\n", srp->InTransferLength, srp->OutTransferLength);
        printf("\tHost Adapter Status = %" PRIu8 "\t Target Status = %" PRIu8 "\n", srp->HostAdapterStatus, srp->TargetStatus);
        set_Console_Colors(true, DEFAULT);
        #endif

        //TODO: check adapter and target status

        if (Status == EFI_SUCCESS)
        {
            ret = SUCCESS;
            if (localSenseBuffer)
            {
                memcpy(scsiIoCtx->psense, alignedSensePtr, M_Min(scsiIoCtx->senseDataSize, srp->SenseDataLength));
            }
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else if (Status == EFI_WRITE_PROTECTED)
        {
            ret = PERMISSION_DENIED;
        }
        else if (Status == EFI_DEVICE_ERROR)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        else if (Status == EFI_INVALID_PARAMETER || Status == EFI_NOT_FOUND)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        safe_Free_aligned(localBuffer);
        safe_Free_aligned(localCDB);
        safe_Free_aligned(localSensePtr);
        safe_Free_aligned(srp);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSIEx Passthru function returning %d\n", ret);
    set_Console_Colors(true, DEFAULT);
    #endif
    return ret;
}

//TODO: This was added later, prevously only SCSI passthrough existed. May need to add #if defined (some UDK version)
int send_UEFI_ATA_Passthrough(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_ATA_PASS_THRU_PROTOCOL *pPassthru;
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI ATA Passthru command\n");
    set_Console_Colors(true, DEFAULT);
    #endif
    if(SUCCESS == get_ATA_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum))
    {
        uint8_t *alignedPointer = scsiIoCtx->pAtaCmdOpts->ptrData;
        uint8_t* localBuffer = NULL;
        bool localAlignedBuffer = false;
        EFI_ATA_PASS_THRU_COMMAND_PACKET	*ataPacket;// ata command packet
        EFI_ATA_COMMAND_BLOCK *ataCommand = (EFI_ATA_COMMAND_BLOCK*)calloc_aligned(1, sizeof(EFI_ATA_COMMAND_BLOCK), pPassthru->Mode->IoAlign);
        EFI_ATA_STATUS_BLOCK *ataStatus = (EFI_ATA_STATUS_BLOCK*)calloc_aligned(1, sizeof(EFI_ATA_STATUS_BLOCK), pPassthru->Mode->IoAlign);

        ataPacket = (EFI_ATA_PASS_THRU_COMMAND_PACKET *) calloc_aligned(1, sizeof(EFI_ATA_PASS_THRU_COMMAND_PACKET), pPassthru->Mode->IoAlign);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Got ATA Passthru protocol pointer:\n");
        if (pPassthru->Mode->Attributes & EFI_ATA_PASS_THRU_ATTRIBUTES_PHYSICAL)
        {
            printf("\tphysical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_ATA_PASS_THRU_ATTRIBUTES_LOGICAL)
        {
            printf("\tlogical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_ATA_PASS_THRU_ATTRIBUTES_NONBLOCKIO)
        {
            printf("\tnon-blocking IO supported\n");
        }
        printf("\tIOAlignment required: %" PRIu32 "\n", pPassthru->Mode->IoAlign);
        set_Console_Colors(true, DEFAULT);
        #endif

        if(scsiIoCtx->timeout == UINT32_MAX)
        {
            ataPacket->Timeout = 0;//value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            ataPacket->Timeout = scsiIoCtx->pAtaCmdOpts->timeout * 1e-7;//value is in 100ns units. zero means wait indefinitely
        }
        alignedPointer = (uint8_t*)(((uint64_t)scsiIoCtx->pAtaCmdOpts->ptrData + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (scsiIoCtx->pAtaCmdOpts->ptrData != alignedPointer)
        {
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(scsiIoCtx->pAtaCmdOpts->dataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                memcpy(alignedPointer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
        }

        switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
        {
        case XFER_DATA_OUT:
            ataPacket->OutDataBuffer = alignedPointer;
            ataPacket->InDataBuffer = NULL;
            ataPacket->OutTransferLength = scsiIoCtx->pAtaCmdOpts->dataSize;
            break;
        case XFER_DATA_IN:
            ataPacket->InDataBuffer = alignedPointer;
            ataPacket->OutDataBuffer = NULL;
            ataPacket->InTransferLength = scsiIoCtx->pAtaCmdOpts->dataSize;
            break;
        case XFER_NO_DATA:
            ataPacket->OutDataBuffer = NULL;
            ataPacket->OutDataBuffer = NULL;
            ataPacket->InTransferLength = 0;
            ataPacket->OutTransferLength = 0;
            break;
        //case XFER_DATA_OUT_IN: //TODO: bidirectional command support...not sure why this ATA interface supports this when there aren't commands to do this, but might as well...
            //ataPacket->DataDirection = 2;//bidirectional command
        default:
            return BAD_PARAMETER;
        }
        //set status block and command block
        //TODO: we should probably check that scsiIoCtx->pAtaCmdOpts is available first, but this SHOULD be ok since this is what we do on other systems
        ataCommand->AtaCommand = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
        ataCommand->AtaFeatures = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
        ataCommand->AtaSectorNumber = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
        ataCommand->AtaCylinderLow = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
        ataCommand->AtaCylinderHigh = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
        ataCommand->AtaDeviceHead = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
        ataCommand->AtaSectorNumberExp = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;
        ataCommand->AtaCylinderLowExp = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;
        ataCommand->AtaCylinderHighExp = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;
        ataCommand->AtaFeaturesExp = scsiIoCtx->pAtaCmdOpts->tfr.Feature48;
        ataCommand->AtaSectorCount = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
        ataCommand->AtaSectorCountExp = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;

        ataPacket->Asb = ataStatus;
        ataPacket->Acb = ataCommand;

        //Set the protocol
        switch (scsiIoCtx->pAtaCmdOpts->commadProtocol)
        {
        case ATA_PROTOCOL_HARD_RESET:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_ATA_HARDWARE_RESET;
            break;
        case ATA_PROTOCOL_SOFT_RESET:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_ATA_SOFTWARE_RESET;
            break;
        case ATA_PROTOCOL_NO_DATA:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_ATA_NON_DATA;
            break;
        case ATA_PROTOCOL_PIO:
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_PIO_DATA_OUT;
            }
            else //data in
            {
                ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_PIO_DATA_IN;
            }
            break;
        case ATA_PROTOCOL_DMA:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_DMA;
            break;
        case ATA_PROTOCOL_DMA_QUE:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_DMA_QUEUED;
            break;
        case ATA_PROTOCOL_DEV_DIAG:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_DEVICE_DIAGNOSTIC;
            break;
        case ATA_PROTOCOL_DEV_RESET:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_DEVICE_RESET;
            break;
        case ATA_PROTOCOL_UDMA:
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_UDMA_DATA_OUT;
            }
            else //data in
            {
                ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_UDMA_DATA_IN;
            }
            break;
        case ATA_PROTOCOL_DMA_FPDMA:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_FPDMA;
            break;
        case ATA_PROTOCOL_RET_INFO:
            ataPacket->Protocol = EFI_ATA_PASS_THRU_PROTOCOL_RETURN_RESPONSE;
            break;
        default:
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("\nProtocol Not Supported in ATA Pass Through.\n");
            }
            return NOT_SUPPORTED;
            break;
        }

        //Set the passthrough length data (where it is, bytes, etc) (essentially building an SAT ATA pass-through command)
        ataPacket->Length |= EFI_ATA_PASS_THRU_LENGTH_BYTES;//ALWAYS set this. We will always set the transfer length as a number of bytes to transfer.
        //Setting 512B vs 4096 vs anything else doesn't matter in UEFI since we can set number of bytes for our transferlength anytime.

//      switch (scsiIoCtx->pAtaCmdOpts->ataTransferBlocks)
//      {
//      case ATA_PT_512B_BLOCKS:
//      case ATA_PT_LOGICAL_SECTOR_SIZE:
//          //TODO: Not sure what, if anything there is to set for these values
//          break;
//      case ATA_PT_NUMBER_OF_BYTES:
//
//          break;
//      case ATA_PT_NO_DATA_TRANSFER:
//          //TODO: not sure if there is anything to set for this value
//          break;
//      default:
//          break;
//      }

        switch (scsiIoCtx->pAtaCmdOpts->ataCommandLengthLocation)
        {
        case ATA_PT_LEN_NO_DATA:
            ataPacket->Length |= EFI_ATA_PASS_THRU_LENGTH_NO_DATA_TRANSFER;
            break;
        case ATA_PT_LEN_FEATURES_REGISTER:
            ataPacket->Length |= EFI_ATA_PASS_THRU_LENGTH_FEATURES;
            break;
        case ATA_PT_LEN_SECTOR_COUNT:
            ataPacket->Length |= EFI_ATA_PASS_THRU_LENGTH_SECTOR_COUNT;
            break;
        case ATA_PT_LEN_TPSIU:
            ataPacket->Length |= EFI_ATA_PASS_THRU_LENGTH_TPSIU;
            break;
        default:
            break;
        }
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending ATA Passthru command\n");
        printf("\t->InTransferLength = %" PRIu32 "\t OutTransferLength = %" PRIu32 "\n", ataPacket->InTransferLength, ataPacket->OutTransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.ata.port, scsiIoCtx->device->os_info.address.ata.portMultiplierPort, ataPacket, NULL);

        //convert return status from sending the command into a return value for opensea-transport
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("ATA Passthru command returned %d\n", Status);
        printf("\t<-InTransferLength = %" PRIu32 "\t OutTransferLength = %" PRIu32 "\n", ataPacket->InTransferLength, ataPacket->OutTransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        if (Status == EFI_SUCCESS)
        {
            if (localAlignedBuffer && scsiIoCtx->pAtaCmdOpts->commandDirection == XFER_DATA_IN)
            {
                //memcpy the data back to the user's pointer since we had to allocate one locally.
                memcpy(scsiIoCtx->pAtaCmdOpts->ptrData, alignedPointer, scsiIoCtx->dataLength);
            }
            ret = SUCCESS;
            //convert RTFRs to sense data since the above layer is using SAT for everthing to make it easy to port across systems
            scsiIoCtx->returnStatus.senseKey = 0;
            scsiIoCtx->returnStatus.asc = 0x00;//might need to change this later
            scsiIoCtx->returnStatus.ascq = 0x00;//might need to change this later
            if (scsiIoCtx->psense != NULL)//check that the pointer is valid
            {
                if (scsiIoCtx->senseDataSize >= 22)//check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
                {
                    scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
                    scsiIoCtx->returnStatus.senseKey = 0x01;//check condition
                    //setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->returnStatus.asc = 0x00;
                    scsiIoCtx->returnStatus.ascq = 0x1D;
                    //now fill in the sens buffer
                    scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                    scsiIoCtx->psense[1] = 0x01;//recovered error
                    //setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->psense[2] = 0x00;//ASC
                    scsiIoCtx->psense[3] = 0x1D;//ASCQ
                    scsiIoCtx->psense[4] = 0;
                    scsiIoCtx->psense[5] = 0;
                    scsiIoCtx->psense[6] = 0;
                    scsiIoCtx->psense[7] = 0x0E;//additional sense length
                    scsiIoCtx->psense[8] = 0x09;//descriptor code
                    scsiIoCtx->psense[9] = 0x0C;//additional descriptor length
                    scsiIoCtx->psense[10] = 0;
                    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
                    {
                        scsiIoCtx->psense[10] |= 0x01;//set the extend bit
                        //fill in the ext registers while we're in this if...no need for another one
                        scsiIoCtx->psense[12] = ataStatus->AtaSectorCountExp;// Sector Count Ext
                        scsiIoCtx->psense[14] = ataStatus->AtaSectorNumberExp;// LBA Lo Ext
                        scsiIoCtx->psense[16] = ataStatus->AtaCylinderLowExp;// LBA Mid Ext
                        scsiIoCtx->psense[18] = ataStatus->AtaCylinderHighExp;// LBA Hi
                    }
                    //fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = ataStatus->AtaError;// Error
                    scsiIoCtx->psense[13] = ataStatus->AtaSectorCount;// Sector Count
                    scsiIoCtx->psense[15] = ataStatus->AtaSectorNumber;// LBA Lo
                    scsiIoCtx->psense[17] = ataStatus->AtaCylinderLow;// LBA Mid
                    scsiIoCtx->psense[19] = ataStatus->AtaCylinderHigh;// LBA Hi
                    scsiIoCtx->psense[20] = ataStatus->AtaDeviceHead;// Device/Head
                    scsiIoCtx->psense[21] = ataStatus->AtaStatus;// Status
                }
            }
        }
        else //error, set appropriate return code
        {
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(scsiIoCtx->device->os_info.last_error);
                set_Console_Colors(true, DEFAULT);
            }
            if (Status == EFI_DEVICE_ERROR)
            {
                //command failed. Not sure if this should be dummied up as 51h - 04h or not.
                ret = OS_PASSTHROUGH_FAILURE;
            } 
            else if (Status == EFI_WRITE_PROTECTED)
            {
                ret = PERMISSION_DENIED;
            }
            else if (Status == EFI_INVALID_PARAMETER || Status == EFI_NOT_FOUND)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }
        safe_Free_aligned(ataPacket);
        safe_Free_aligned(localBuffer);
        safe_Free_aligned(ataStatus);
        safe_Free_aligned(ataCommand);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("ATA Passthru function returning %d\n", ret);
    set_Console_Colors(true, DEFAULT);
    #endif
    return ret;
}

int send_IO( ScsiIoCtx *scsiIoCtx )
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("Sending command with send_IO\n");
    }
    switch (scsiIoCtx->device->os_info.passthroughType)
    {
    case UEFI_PASSTHROUGH_SCSI:
        ret = send_UEFI_SCSI_Passthrough(scsiIoCtx);
        break;
    case UEFI_PASSTHROUGH_SCSI_EXT:
        ret = send_UEFI_SCSI_Passthrough_Ext(scsiIoCtx);
        break;
    case UEFI_PASSTHROUGH_ATA:
        if (scsiIoCtx->pAtaCmdOpts)
        {   
            ret = send_UEFI_ATA_Passthrough(scsiIoCtx);
        }
        else
        {
            ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        }
        break;
#if !defined (DISABLE_NVME_PASSTHROUGH)
    case UEFI_PASSTHROUGH_NVME:
        ret = sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        break;
#endif
    default:
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SendIO CMD NOT AVAILABLE: %d\n", scsiIoCtx->device->os_info.passthroughType);
        set_Console_Colors(true, DEFAULT);
        #endif
        ret = OS_COMMAND_NOT_AVAILABLE;
        break;
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
int send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *pPassthru;
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI NVMe Passthru Command\n");
    set_Console_Colors(true, DEFAULT);
    #endif
    if(SUCCESS == get_NVMe_Passthru_Protocol_Ptr(&pPassthru, nvmeIoCtx->device->os_info.controllerNum))
    { 
        uint8_t *alignedPointer = nvmeIoCtx->ptrData;
        uint8_t *localBuffer = NULL;
        bool localAlignedBuffer = false;
        EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET	*nrp;
        EFI_NVM_EXPRESS_COMMAND *nvmCommand = (EFI_NVM_EXPRESS_COMMAND*)calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_COMMAND), pPassthru->Mode->IoAlign);
        EFI_NVM_EXPRESS_COMPLETION *nvmCompletion = (EFI_NVM_EXPRESS_COMPLETION*)calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_COMPLETION), pPassthru->Mode->IoAlign);

        nrp = (EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET *)calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET), pPassthru->Mode->IoAlign);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Got NVMe Passthru protocol pointer:\n");
        if (pPassthru->Mode->Attributes & EFI_NVM_EXPRESS_PASS_THRU_ATTRIBUTES_PHYSICAL)
        {
            printf("\tphysical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_NVM_EXPRESS_PASS_THRU_ATTRIBUTES_LOGICAL)
        {
            printf("\tlogical device\n");
        }
        if (pPassthru->Mode->Attributes & EFI_NVM_EXPRESS_PASS_THRU_ATTRIBUTES_NONBLOCKIO)
        {
            printf("\tnon-blocking IO supported\n");
        }
        if (pPassthru->Mode->Attributes & EFI_NVM_EXPRESS_PASS_THRU_ATTRIBUTES_CMD_SET_NVM)
        {
            printf("\tNVM command set supported\n");
        }
        printf("\tIOAlignment required: %" PRIu32 "\n", pPassthru->Mode->IoAlign);
        set_Console_Colors(true, DEFAULT);
        #endif

        if(nvmeIoCtx->timeout == UINT32_MAX)
        {
            nrp->CommandTimeout = 0;//value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            nrp->CommandTimeout = nvmeIoCtx->timeout * 1e-7;//value is in 100ns units. zero means wait indefinitely
        }

        alignedPointer = (uint8_t*)(((uint64_t)nvmeIoCtx->ptrData + (uint64_t)pPassthru->Mode->IoAlign) & ~(uint64_t)pPassthru->Mode->IoAlign);
        if (nvmeIoCtx->ptrData != alignedPointer)
        {
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(nvmeIoCtx->dataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (nvmeIoCtx->commandDirection == XFER_DATA_OUT)
            {
                memcpy(alignedPointer, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
            }
        }

        //set transfer information
        nrp->TransferBuffer = alignedPointer;
        nrp->TransferLength = nvmeIoCtx->dataSize;

        //TODO: Handle metadata pointer
        nrp->MetadataBuffer = NULL;
        nrp->MetadataLength = 0;

        //set queue type & command
        switch(nvmeIoCtx->commandType)
        {
        case NVM_ADMIN_CMD:
            nrp->QueueType = NVME_ADMIN_QUEUE;
            nvmCommand->Cdw0.Opcode = nvmeIoCtx->cmd.adminCmd.opcode;
            nvmCommand->Cdw0.FusedOperation = NORMAL_CMD;//TODO: handle fused Commands
            nvmCommand->Cdw0.Reserved = RESERVED;
            nvmCommand->Nsid = nvmeIoCtx->cmd.adminCmd.nsid;
            if(nvmeIoCtx->cmd.adminCmd.cdw2)
            {
              nvmCommand->Cdw2 = nvmeIoCtx->cmd.adminCmd.cdw2;
              nvmCommand->Flags |= CDW2_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw3)
            {
              nvmCommand->Cdw3 = nvmeIoCtx->cmd.adminCmd.cdw3;
              nvmCommand->Flags |= CDW3_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw10)
            {
              nvmCommand->Cdw10 = nvmeIoCtx->cmd.adminCmd.cdw10;
              nvmCommand->Flags |= CDW10_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw11)
            {
              nvmCommand->Cdw11 = nvmeIoCtx->cmd.adminCmd.cdw11;
              nvmCommand->Flags |= CDW11_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw12)
            {
              nvmCommand->Cdw12 = nvmeIoCtx->cmd.adminCmd.cdw12;
              nvmCommand->Flags |= CDW12_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw13)
            {
              nvmCommand->Cdw13 = nvmeIoCtx->cmd.adminCmd.cdw13;
              nvmCommand->Flags |= CDW13_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw14)
            {
              nvmCommand->Cdw14 = nvmeIoCtx->cmd.adminCmd.cdw14;
              nvmCommand->Flags |= CDW14_VALID;
            }
            if(nvmeIoCtx->cmd.adminCmd.cdw15)
            {
              nvmCommand->Cdw15 = nvmeIoCtx->cmd.adminCmd.cdw15;
              nvmCommand->Flags |= CDW15_VALID;
            }
            break;
        case NVM_CMD:
            nrp->QueueType = NVME_IO_QUEUE;
            nvmCommand->Cdw0.Opcode = nvmeIoCtx->cmd.nvmCmd.opcode;
            nvmCommand->Cdw0.FusedOperation = NORMAL_CMD;//TODO: handle fused Commands
            nvmCommand->Cdw0.Reserved = RESERVED;
            nvmCommand->Nsid = nvmeIoCtx->cmd.nvmCmd.nsid;
            if(nvmeIoCtx->cmd.nvmCmd.cdw2)
            {
              nvmCommand->Cdw2 = nvmeIoCtx->cmd.nvmCmd.cdw2;
              nvmCommand->Flags |= CDW2_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw3)
            {
              nvmCommand->Cdw3 = nvmeIoCtx->cmd.nvmCmd.cdw3;
              nvmCommand->Flags |= CDW3_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw10)
            {
              nvmCommand->Cdw10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
              nvmCommand->Flags |= CDW10_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw11)
            {
              nvmCommand->Cdw11 = nvmeIoCtx->cmd.nvmCmd.cdw11;
              nvmCommand->Flags |= CDW11_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw12)
            {
              nvmCommand->Cdw12 = nvmeIoCtx->cmd.nvmCmd.cdw12;
              nvmCommand->Flags |= CDW12_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw13)
            {
              nvmCommand->Cdw13 = nvmeIoCtx->cmd.nvmCmd.cdw13;
              nvmCommand->Flags |= CDW13_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw14)
            {
              nvmCommand->Cdw14 = nvmeIoCtx->cmd.nvmCmd.cdw14;
              nvmCommand->Flags |= CDW14_VALID;
            }
            if(nvmeIoCtx->cmd.nvmCmd.cdw15)
            {
              nvmCommand->Cdw15 = nvmeIoCtx->cmd.nvmCmd.cdw15;
              nvmCommand->Flags |= CDW15_VALID;
            }
            break;
        default:
            return BAD_PARAMETER;
            break;
        }

        nrp->NvmeCmd = nvmCommand;
        nrp->NvmeCompletion = nvmCompletion;

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending NVMe Passthru command\n");
        printf("\t->TransferLength = %" PRIu32 "\n", nrp->TransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        Status = pPassthru->PassThru(pPassthru, nvmeIoCtx->device->os_info.address.nvme.namespaceID, nrp, NULL);

        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("NVMe Passthru command returned %d\n", Status);
        printf("\t<-TransferLength = %" PRIu32 "\n", nrp->TransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        //TODO: check completion information and pass it back up.

        if (Status == EFI_SUCCESS)
        {
            ret = SUCCESS;
            nvmeIoCtx->commandCompletionData.commandSpecific = nvmCompletion->DW0;
            nvmeIoCtx->commandCompletionData.dw1Reserved = nvmCompletion->DW1;
            nvmeIoCtx->commandCompletionData.sqIDandHeadPtr = nvmCompletion->DW2;
            nvmeIoCtx->commandCompletionData.statusAndCID = nvmCompletion->DW3;
            nvmeIoCtx->commandCompletionData.dw0Valid = true;
            nvmeIoCtx->commandCompletionData.dw1Valid = true;
            nvmeIoCtx->commandCompletionData.dw2Valid = true;
            nvmeIoCtx->commandCompletionData.dw3Valid = true;
            if (nvmeIoCtx->commandDirection == XFER_DATA_IN && localAlignedBuffer)
            {
                memcpy(nvmeIoCtx->ptrData, alignedPointer, nvmeIoCtx->dataSize);
            }
        }
        else if (Status == EFI_WRITE_PROTECTED)
        {
            ret = PERMISSION_DENIED;
        }
        else if (Status == EFI_DEVICE_ERROR)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        else if (Status == EFI_INVALID_PARAMETER || Status == EFI_NOT_FOUND)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        safe_Free_aligned(nrp);
        safe_Free_aligned(localBuffer);
        safe_Free_aligned(nvmCommand);
        safe_Free_aligned(nvmCompletion);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("NVMe Passthru function returning %d\n", ret);
    set_Console_Colors(true, DEFAULT);
    #endif
    return ret;
}

int pci_Read_Bar_Reg(tDevice * device, uint8_t * pData, uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int nvme_Reset(tDevice *device)
{
    //This is a stub. We may not be able to do this in Windows, but want this here in case we can and to make code otherwise compile without ifdefs
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Sending NVMe Reset\n");
    }

    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        print_Return_Enum("NVMe Reset", OS_COMMAND_NOT_AVAILABLE);
    }
    return OS_COMMAND_NOT_AVAILABLE;
}

int nvme_Subsystem_Reset(tDevice *device)
{
    //This is a stub. We may not be able to do this in Windows, but want this here in case we can and to make code otherwise compile without ifdefs
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Sending NVMe Subsystem Reset\n");
    }

    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        print_Return_Enum("NVMe Subsystem Reset", OS_COMMAND_NOT_AVAILABLE);
    }
    return OS_COMMAND_NOT_AVAILABLE;
}

#endif //DISABLE_NVME_PASSTHROUGH

int close_Device(tDevice *device)
{
    return NOT_SUPPORTED;
}

uint32_t get_ATA_Device_Count()
{
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_ATA_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &ataPtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return 0;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &ataPtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        uint16_t port = UINT16_MAX;//start here since this will make the api find the first available ata port
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextPort(pPassthru, &port);
            if(uefiStatus == EFI_SUCCESS && port != UINT16_MAX)
            {
                //need to call get next device now
                uint16_t pmport = UINT16_MAX;//start here so we can find the first port multiplier port
                EFI_STATUS getNextDevice = EFI_SUCCESS;
                while(getNextDevice == EFI_SUCCESS)
                {
                    getNextDevice = pPassthru->GetNextDevice(pPassthru, port, &pmport);
                    if(getNextDevice == EFI_SUCCESS)
                    {
                        //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                        EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                        EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, port, pmport, &devicePath);
                        if(buildPath == EFI_SUCCESS)
                        {
                            //found a device!!!
                            ++deviceCount;
                        }
                        //EFI_NOT_FOUND means no device at this place.
                        //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                        //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                        safe_Free(devicePath);
                    }
                }
            }
        }
        ++counter;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("ATA Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, DEFAULT);
    #endif
    return deviceCount;
}

int get_ATA_Devices(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t *index)
{
    int ret = NOT_SUPPORTED;
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_ATA_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &ataPtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &ataPtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        uint16_t port = UINT16_MAX;//start here since this will make the api find the first available ata port
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextPort(pPassthru, &port);
            if(uefiStatus == EFI_SUCCESS && port != UINT16_MAX)
            {
                //need to call get next device now
                uint16_t pmport = UINT16_MAX;//start here so we can find the first port multiplier port
                EFI_STATUS getNextDevice = EFI_SUCCESS;
                while(getNextDevice == EFI_SUCCESS)
                {
                    getNextDevice = pPassthru->GetNextDevice(pPassthru, port, &pmport);
                    if(getNextDevice == EFI_SUCCESS)
                    {
                        //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                        EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                        EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, port, pmport, &devicePath);
                        if(buildPath == EFI_SUCCESS)
                        {
                            //found a device!!!
                            char ataHandle[32] = { 0 };
                            sprintf(ataHandle, "ata:%" PRIx16 ":%" PRIx16 ":%" PRIx16, counter, port, pmport);
                            int result = get_Device(ataHandle, &ptrToDeviceList[*index]);
                            if(result != SUCCESS)
                            {
                                ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                            }
                            ++(*index);
                            ++deviceCount;
                        }
                        //EFI_NOT_FOUND means no device at this place.
                        //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                        //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                        safe_Free(devicePath);
                    }
                }
            }
        }
        ++counter;
    }
    if(uefiStatus == EFI_NOT_FOUND)
    {
        //loop finished and we found a port/device
        if(deviceCount > 0)
        {
            //assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}

uint32_t get_SCSI_Device_Count()
{
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_SCSI_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &scsiPtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return 0;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &scsiPtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        uint32_t target = UINT32_MAX;//start here since this will make the api find the first available scsi target
        uint64_t lun = 0;//doesn't specify what we should start with for this.
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextDevice(pPassthru, &target, & lun);
            if(uefiStatus == EFI_SUCCESS && target != UINT16_MAX)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    ++deviceCount;
                }
                //EFI_NOT_FOUND means no device at this place.
                //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_Free(devicePath);
            }
        }
        ++counter;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSI Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, DEFAULT);
    #endif
    return deviceCount;
}

int get_SCSI_Devices(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t *index)
{
    int ret = NOT_SUPPORTED;
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_SCSI_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &scsiPtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &scsiPtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        uint32_t target = UINT32_MAX;//start here since this will make the api find the first available scsi target
        uint64_t lun = 0;//doesn't specify what we should start with for this.
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextDevice(pPassthru, &target, & lun);
            if(uefiStatus == EFI_SUCCESS && target != UINT32_MAX)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    char scsiHandle[64] = { 0 };
                    sprintf(scsiHandle, "scsi:%" PRIx16 ":%" PRIx32 ":%" PRIx64, counter, target, lun);
                    int result = get_Device(scsiHandle, &ptrToDeviceList[*index]);
                    if(result != SUCCESS)
                    {
                        ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                    }
                    ++(*index);
                    ++deviceCount;
                }
                //EFI_NOT_FOUND means no device at this place.
                //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_Free(devicePath);
            }
        }
        ++counter;
    }
    if(uefiStatus == EFI_NOT_FOUND)
    {
        //loop finished and we found a port/device
        if(deviceCount > 0)
        {
            //assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}

uint32_t get_SCSIEx_Device_Count()
{
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_EXT_SCSI_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &scsiPtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return 0;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uint8_t invalidTarget[TARGET_MAX_BYTES];
        uint8_t target[TARGET_MAX_BYTES];
        uint8_t *targetPtr = &target[0];
        uint64_t lun = 0xFF;//doesn't specify what we should start with for this.
        uefiStatus = gBS->OpenProtocol(handle[counter], &scsiPtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        memset(target, 0xFF, TARGET_MAX_BYTES);
        memset(invalidTarget, 0xFF, TARGET_MAX_BYTES);
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextTargetLun(pPassthru, &targetPtr, &lun);
            if(uefiStatus == EFI_SUCCESS && memcmp(target, invalidTarget, TARGET_MAX_BYTES) != 0)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, targetPtr, lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    ++deviceCount;
                }
                //EFI_NOT_FOUND means no device at this place.
                //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_Free(devicePath);
            }
        }
        ++counter;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSIEx Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, DEFAULT);
    #endif
    return deviceCount;
}

int get_SCSIEx_Devices(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t *index)
{
    int ret = NOT_SUPPORTED;
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_EXT_SCSI_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &scsiPtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uint8_t invalidTarget[TARGET_MAX_BYTES];
        uint8_t target[TARGET_MAX_BYTES];
        uint8_t *targetPtr = &target[0];
        uint64_t lun = 0xFF;//doesn't specify what we should start with for this.
        uefiStatus = gBS->OpenProtocol(handle[counter], &scsiPtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        memset(target, 0xFF, TARGET_MAX_BYTES);
        memset(invalidTarget, 0xFF, TARGET_MAX_BYTES);
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextTargetLun(pPassthru, &targetPtr, & lun);
            if(uefiStatus == EFI_SUCCESS && memcmp(target, invalidTarget, TARGET_MAX_BYTES) != 0)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    char scsiExHandle[64] = { 0 };
                    sprintf(scsiExHandle, "scsiEx:%" PRIx16 ":%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 ":%" PRIx64, counter, target[0], target[1], target[2], target[3], target[4], target[5], target[6], target[7], target[8], target[9], target[10], target[11], target[12], target[13], target[14], target[15], lun);
                    int result = get_Device(scsiExHandle, &ptrToDeviceList[*index]);
                    if(result != SUCCESS)
                    {
                        ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                    }
                    ++(*index);
                    ++deviceCount;
                }
                //EFI_NOT_FOUND means no device at this place.
                //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_Free(devicePath);
            }
        }
        ++counter;
    }
    if(uefiStatus == EFI_NOT_FOUND)
    {
        //loop finished and we found a port/device
        if(deviceCount > 0)
        {
            //assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
uint32_t get_NVMe_Device_Count()
{
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &nvmePtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return 0;
    }
    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &nvmePtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        uint32_t namespaceID = UINT32_MAX;//start here since this will make the api find the first available nvme namespace
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextNamespace(pPassthru, &namespaceID);
            if(uefiStatus == EFI_SUCCESS && namespaceID != UINT32_MAX)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, namespaceID, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    ++deviceCount;
                }
                //EFI_NOT_FOUND means no device at this place.
                //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_Free(devicePath);
            }
        }
        ++counter;
    }
    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("NVMe Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, DEFAULT);
    #endif
    return deviceCount;
}

int get_NVMe_Devices(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t *index)
{
    int ret = NOT_SUPPORTED;
    uint32_t deviceCount = 0;
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *pPassthru;
    EFI_HANDLE *handle;
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &nvmePtGUID, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }
    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &nvmePtGUID, (void **)&pPassthru, gImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if(EFI_ERROR(uefiStatus))
        {
            //TODO: continue to next handle in loop? Or fail?
            continue;
        }
        uint32_t namespaceID = UINT32_MAX;//start here since this will make the api find the first available nvme namespace
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextNamespace(pPassthru, &namespaceID);
            if(uefiStatus == EFI_SUCCESS && namespaceID != UINT32_MAX)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, namespaceID, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    char nvmeHandle[64] = { 0 };
                    sprintf(nvmeHandle, "nvme:%" PRIx16 ":%" PRIx32, counter, namespaceID);
                    int result = get_Device(nvmeHandle, &ptrToDeviceList[*index]);
                    if(result != SUCCESS)
                    {
                        ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                    }
                    ++(*index);
                    ++deviceCount;
                }
                //EFI_NOT_FOUND means no device at this place.
                //EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us according to the API)
                //EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_Free(devicePath);
            }
        }
        ++counter;
    }
    if(uefiStatus == EFI_NOT_FOUND)
    {
        //loop finished and we found a port/device
        if(deviceCount > 0)
        {
            //assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}
#endif

//-----------------------------------------------------------------------------
//
//  get_Device_Count()
//
//! \brief   Description:  Get the count of devices in the system that this library
//!                        can talk to. This function is used in conjunction with
//!                        get_Device_List, so that enough memory is allocated.
//
//  Entry:
//!   \param[out] numberOfDevices = integer to hold the number of devices found.
//!   \param[in] flags = eScanFlags based mask to let application control.
//!						 NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
{
    //TODO: handle flags
    *numberOfDevices += get_ATA_Device_Count();
    *numberOfDevices += get_SCSI_Device_Count();
    *numberOfDevices += get_SCSIEx_Device_Count();
    #if !defined (DISABLE_NVME_PASSTHROUGH)
    *numberOfDevices += get_NVMe_Device_Count();
    #endif
    return SUCCESS;
}

//-----------------------------------------------------------------------------
//
//  get_Device_List()
//
//! \brief   Description:  Get a list of devices that the library supports.
//!                        Use get_Device_Count to figure out how much memory is
//!                        needed to be allocated for the device list. The memory
//!						   allocated must be the multiple of device structure.
//!						   The application can pass in less memory than needed
//!						   for all devices in the system, in which case the library
//!                        will fill the provided memory with how ever many device
//!						   structures it can hold.
//  Entry:
//!   \param[out] ptrToDeviceList = pointer to the allocated memory for the device list
//!   \param[in]  sizeInBytes = size of the entire list in bytes.
//!   \param[in]  versionBlock = versionBlock structure filled in by application for
//!								 sanity check by library.
//!   \param[in] flags = eScanFlags based mask to let application control.
//!						 NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    uint32_t index = 0;
    //TODO: handle flags and validate size of device list and version block
    get_ATA_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    get_SCSI_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    get_SCSIEx_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    #if !defined (DISABLE_NVME_PASSTHROUGH)
    get_NVMe_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    #endif
    return SUCCESS;
}

int os_Read(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Write(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Verify(tDevice *device, uint64_t lba, uint32_t range)
{
    return NOT_SUPPORTED;
}

int os_Flush(tDevice *device)
{
    return NOT_SUPPORTED;
}
