//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2017-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
#include <Bus/Ata/AtaAtapiPassThru/AtaAtapiPassThru.h>//part of mdemodulepackage. Being used to help see if ATAPassthrough is from IDE or AHCI driver.
#if !defined (DISABLE_NVME_PASSTHROUGH)
#include <Protocol/NvmExpressPassthru.h>
#endif
extern bool validate_Device_Struct(versionBlock);

//Define this to turn on extra prints to the screen for debugging UEFI passthrough issues.
//#define UEFI_PASSTHRU_DEBUG_MESSAGES

#if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
//This color will be used for normal debug messages that are infomative. Critical memory allocation errors among other things are going to be RED.
eConsoleColors uefiDebugMessageColor = BLUE;
#endif

#define IS_ALIGNED(addr, size)      (((UINTN) (addr) & (size - 1)) == 0)

//If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

#define UEFI_HANDLE_STRING_LENGTH 64

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

void close_Passthru_Protocol_Ptr(EFI_GUID ptGuid, void **pPassthru, uint32_t controllerID)
{
    EFI_STATUS uefiStatus = EFI_SUCCESS;
    EFI_HANDLE *handle = NULL;
    UINTN nodeCount = 0;

    if (!gBS) //make sure global boot services pointer is valid before accessing it.
    {
        return;
    }

    uefiStatus = gBS->LocateHandleBuffer ( ByProtocol, &ptGuid, NULL, &nodeCount, &handle);
    if(EFI_ERROR(uefiStatus))
    {
        return;
    }
    //NOTE: This code below assumes that we only care to change color output on node 0. This seems to work from a quick test, but may not be correct. Not sure what the other 2 nodes are for...serial?
    uefiStatus = gBS->CloseProtocol( handle[controllerID], &ptGuid, gImageHandle, NULL);
    //TODO: based on the error code, rather than assuming failure, check for supported/not supported.
    if(EFI_ERROR(uefiStatus))
    {
        perror("Failed to close simple text output protocol\n");
    }
    else
    {
        *pPassthru = NULL;//this pointer is no longer valid!
    }
    return;
}
//ATA PT since UDK 2010
int get_ATA_Passthru_Protocol_Ptr(EFI_ATA_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(ataPtGUID, (void**)pPassthru, controllerID);
}

void close_ATA_Passthru_Protocol_Ptr(EFI_ATA_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(ataPtGUID, (void**)pPassthru, controllerID);
}

int get_SCSI_Passthru_Protocol_Ptr(EFI_SCSI_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(scsiPtGUID, (void**)pPassthru, controllerID);
}

void close_SCSI_Passthru_Protocol_Ptr(EFI_SCSI_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(scsiPtGUID, (void**)pPassthru, controllerID);
}

int get_Ext_SCSI_Passthru_Protocol_Ptr(EFI_EXT_SCSI_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(scsiPtGUID, (void**)pPassthru, controllerID);
}

void close_Ext_SCSI_Passthru_Protocol_Ptr(EFI_EXT_SCSI_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(scsiPtGUID, (void**)pPassthru, controllerID);
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
//NVMe since UDK 2015
int get_NVMe_Passthru_Protocol_Ptr(EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(nvmePtGUID, (void**)pPassthru, controllerID);
}

void close_NVMe_Passthru_Protocol_Ptr(EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL **pPassthru, uint32_t controllerID)
{
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(nvmePtGUID, (void**)pPassthru, controllerID);
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
    snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
    snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", filename);
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
                    ATA_ATAPI_PASS_THRU_INSTANCE *instance = ATA_PASS_THRU_PRIVATE_DATA_FROM_THIS(pPassthru);
                    if (instance->Mode == EfiAtaIdeMode && device->os_info.address.ata.portMultiplierPort > 0)
                    {
                        //If the driver is running in IDE mode, it will set the pmport value to non-zero for device 1. Because of this, and some sample EDK2 code, we need to make sure we set the device 1 bit when issuing commands!
                        device->drive_info.ata_Options.isDevice1 = true;
                    }
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                    #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                    set_Console_Colors(true, GREEN);
                    printf("Protocol Mode = %d\n", instance->Mode);//0 means IDE, 1 means AHCI, 2 means RAID, but we shouldn't see RAID here ever.
                    set_Console_Colors(true, DEFAULT);
                    #endif
                    //TODO: save ioalignment so callers above can properly allocate aligned memory.
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath)
                //close the protocol
                close_ATA_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum);
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
            uint8_t targetStringIter = 0;
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.drive_type = SCSI_DRIVE;
            device->os_info.passthroughType = UEFI_PASSTHROUGH_SCSI_EXT;
            //TODO: validate convertion of targetAsString to the array we need to save for the targetID
            for(uint8_t iter = 0; iter < 32 && targetIDIter >= 0; iter += 2, --targetIDIter, ++targetStringIter)
            {
                char smallString[4] = { 0 };//need to break the string into two charaters at a time then convert that to a integer to save for target name
                snprintf(smallString, 4, "%c%c", targetAsString[iter], targetAsString[iter + 1]);
                device->os_info.address.scsiEx.target[targetStringIter] = strtol(smallString, NULL, 16 /*string is in base 16*/);
            }
            EFI_EXT_SCSI_PASS_THRU_PROTOCOL *pPassthru;
            if (SUCCESS == get_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, (uint8_t*)(&device->os_info.address.scsiEx.target), device->os_info.address.scsiEx.lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    memcpy(&device->os_info.devicePath, devicePath, M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath)
                close_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum);
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
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, device->os_info.address.scsi.target, device->os_info.address.scsi.lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    memcpy(&device->os_info.devicePath, devicePath, M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath)
                close_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum);
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
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                }
                else
                {
                    //device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_Free(devicePath)
                close_NVMe_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum);
                device->drive_info.namespaceID = device->os_info.address.nvme.namespaceID;
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

void print_UEFI_SCSI_Adapter_Status(uint8_t adapterStatus)
{
    switch (adapterStatus)
    {
    case EFI_SCSI_STATUS_HOST_ADAPTER_OK:
        printf("\tAdapter Status: %02xh - OK\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_TIMEOUT_COMMAND:
        printf("\tAdapter Status: %02xh - Timeout Command\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_TIMEOUT:
        printf("\tAdapter Status: %02xh - Timeout\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_MESSAGE_REJECT:
        printf("\tAdapter Status: %02xh - Message Reject\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_BUS_RESET:
        printf("\tAdapter Status: %02xh - Bus Reset\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_PARITY_ERROR:
        printf("\tAdapter Status: %02xh - Parity Error\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_REQUEST_SENSE_FAILED:
        printf("\tAdapter Status: %02xh - Request Sense Failed\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_SELECTION_TIMEOUT:
        printf("\tAdapter Status: %02xh - Selection Timeout\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_DATA_OVERRUN_UNDERRUN:
        printf("\tAdapter Status: %02xh - Data Overrun Underrun\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_BUS_FREE:
        printf("\tAdapter Status: %02xh - Bus Free\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_PHASE_ERROR:
        printf("\tAdapter Status: %02xh - Phase Error\n", adapterStatus);
        break;
    case EFI_SCSI_STATUS_HOST_ADAPTER_OTHER:
        printf("\tAdapter Status: %02xh - Other\n", adapterStatus);
        break;
    default:
        printf("\tAdapter Status: %02xh - Unknown Status\n", adapterStatus);
        break;
    }
}

void print_UEFI_SCSI_Target_Status(uint8_t targetStatus)
{
    switch (targetStatus)
    {
    case EFI_SCSI_STATUS_TARGET_GOOD:
        printf("\tTarget Status: %02xh - Good\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_CHECK_CONDITION:
        printf("\tTarget Status: %02xh - Check Condition\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_CONDITION_MET:
        printf("\tTarget Status: %02xh - Condition Met\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_BUSY:
        printf("\tTarget Status: %02xh - Busy\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_INTERMEDIATE:
        printf("\tTarget Status: %02xh - Intermediate\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_INTERMEDIATE_CONDITION_MET:
        printf("\tTarget Status: %02xh - Intermediate Condition Met\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_RESERVATION_CONFLICT:
        printf("\tTarget Status: %02xh - Reservation Conflict\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_COMMOND_TERMINATED:
        printf("\tTarget Status: %02xh - Command Terminated\n", targetStatus);
        break;
    case EFI_SCSI_STATUS_TARGET_QUEUE_FULL:
        printf("\tTarget Status: %02xh - Queue Full\n", targetStatus);
        break;
    default:
        printf("\tTarget Status: %02xh - Unknown Status\n", targetStatus);
        break;
    }
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
        seatimer_t commandTimer;
        uint8_t *alignedPointer = scsiIoCtx->pdata;
        uint8_t *alignedCDB = scsiIoCtx->cdb;
        uint8_t *alignedSensePtr = scsiIoCtx->psense;
        uint8_t *localBuffer = NULL;
        uint8_t *localCDB = NULL;
        uint8_t *localSensePtr = NULL;
        bool localAlignedBuffer = false, localSenseBuffer = false;
        EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET	*srp;//scsi request packet

        srp = (EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET *) calloc_aligned(1, sizeof(EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);

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
            if (scsiIoCtx->timeout > 0)
            {
                srp->Timeout = scsiIoCtx->timeout * 1e7;//value is in 100ns units. zero means wait indefinitely
            }
            else
            {
                srp->Timeout = 15 * 1e7; //15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->pdata, pPassthru->Mode->IoAlign))
        {
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
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

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->cdb, pPassthru->Mode->IoAlign))
        {
            //allocate an aligned buffer here!
            localCDB = (uint8_t *)calloc_aligned(scsiIoCtx->cdbLength, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
            if (!localCDB)
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

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->psense, pPassthru->Mode->IoAlign))
        {
            //allocate an aligned buffer here!
            localSenseBuffer = true;
            localSensePtr = (uint8_t *)calloc_aligned(scsiIoCtx->senseDataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
            if (!localSensePtr)
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
        start_Timer(&commandTimer);
        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.scsi.target, scsiIoCtx->device->os_info.address.scsi.lun, srp, NULL);
        stop_Timer(&commandTimer);
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SCSI Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-TransferLength = %" PRIu32 "\n", srp->TransferLength);
        print_UEFI_SCSI_Adapter_Status(srp->HostAdapterStatus);
        print_UEFI_SCSI_Target_Status(srp->TargetStatus);
        set_Console_Colors(true, DEFAULT);
        #endif
        //TODO: Check host adapter status and target status
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        scsiIoCtx->device->os_info.last_error = Status;

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
        else
        {
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(scsiIoCtx->device->os_info.last_error);
                set_Console_Colors(true, DEFAULT);
            }
            if (Status == EFI_WRITE_PROTECTED)
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
        }
        safe_Free(localBuffer)
        safe_Free(localCDB)
        safe_Free(localSensePtr)
        safe_Free(srp)
        close_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum);
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

void print_UEFI_SCSI_Ex_Adapter_Status(uint8_t adapterStatus)
{
    switch (adapterStatus)
    {
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_OK:
        printf("\tAdapter Status: %02xh - OK\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_TIMEOUT_COMMAND:
        printf("\tAdapter Status: %02xh - Timeout Command\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_TIMEOUT:
        printf("\tAdapter Status: %02xh - Timeout\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_MESSAGE_REJECT:
        printf("\tAdapter Status: %02xh - Message Reject\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_BUS_RESET:
        printf("\tAdapter Status: %02xh - Bus Reset\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_PARITY_ERROR:
        printf("\tAdapter Status: %02xh - Parity Error\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_REQUEST_SENSE_FAILED:
        printf("\tAdapter Status: %02xh - Request Sense Failed\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_SELECTION_TIMEOUT:
        printf("\tAdapter Status: %02xh - Selection Timeout\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_DATA_OVERRUN_UNDERRUN:
        printf("\tAdapter Status: %02xh - Data Overrun Underrun\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_BUS_FREE:
        printf("\tAdapter Status: %02xh - Bus Free\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_PHASE_ERROR:
        printf("\tAdapter Status: %02xh - Phase Error\n", adapterStatus);
        break;
    case EFI_EXT_SCSI_STATUS_HOST_ADAPTER_OTHER:
        printf("\tAdapter Status: %02xh - Other\n", adapterStatus);
        break;
    default:
        printf("\tAdapter Status: %02xh - Unknown Status\n", adapterStatus);
        break;
    }
}

void print_UEFI_SCSI_Ex_Target_Status(uint8_t targetStatus)
{
    switch (targetStatus)
    {
    case EFI_EXT_SCSI_STATUS_TARGET_GOOD:
        printf("\tTarget Status: %02xh - Good\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_CHECK_CONDITION:
        printf("\tTarget Status: %02xh - Check Condition\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_CONDITION_MET:
        printf("\tTarget Status: %02xh - Condition Met\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_BUSY:
        printf("\tTarget Status: %02xh - Busy\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_INTERMEDIATE:
        printf("\tTarget Status: %02xh - Intermediate\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_INTERMEDIATE_CONDITION_MET:
        printf("\tTarget Status: %02xh - Intermediate Condition Met\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_RESERVATION_CONFLICT:
        printf("\tTarget Status: %02xh - Reservation Conflict\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_TASK_SET_FULL:
        printf("\tTarget Status: %02xh - Task Set Full\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_ACA_ACTIVE:
        printf("\tTarget Status: %02xh - ACA Active\n", targetStatus);
        break;
    case EFI_EXT_SCSI_STATUS_TARGET_TASK_ABORTED:
        printf("\tTarget Status: %02xh - Task Aborted\n", targetStatus);
        break;
    default:
        printf("\tTarget Status: %02xh - Unknown Status\n", targetStatus);
        break;
    }
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
        seatimer_t commandTimer;
        uint8_t *alignedPointer = scsiIoCtx->pdata;
        uint8_t *alignedCDB = scsiIoCtx->cdb;
        uint8_t *alignedSensePtr = scsiIoCtx->psense;
        uint8_t *localBuffer = NULL;
        uint8_t *localCDB = NULL;
        uint8_t *localSensePtr = NULL;
        bool localAlignedBuffer = false, localSenseBuffer = false;
        EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET	*srp;// Extended scsi request packet

        srp = (EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET *) calloc_aligned(1, sizeof(EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);

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
           if (scsiIoCtx->timeout > 0)
            {
                srp->Timeout = scsiIoCtx->timeout * 1e7;//value is in 100ns units. zero means wait indefinitely
            }
            else
            {
                srp->Timeout = 15 * 1e7; //15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->pdata, pPassthru->Mode->IoAlign))
        {
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoTcx->pdata is not aligned! Creating local aligned buffer\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
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
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, DEFAULT);
            }
            #endif
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->cdb, pPassthru->Mode->IoAlign))
        {
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoTcx->cdb is not aligned! Creating local cdb buffer\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            //allocate an aligned buffer here!
            localCDB = (uint8_t *)calloc_aligned(scsiIoCtx->cdbLength, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
            if (!localCDB)
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
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedCDB, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! AlignedCDB is still not properly aligned\n");
                set_Console_Colors(true, DEFAULT);
            }
            #endif
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->psense, pPassthru->Mode->IoAlign))
        {
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoTcx->psense is not aligned! Creating local sense buffer\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            //allocate an aligned buffer here!
            localSenseBuffer = true;
            localSensePtr = (uint8_t *)calloc_aligned(scsiIoCtx->senseDataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
            if (!localSensePtr)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned sense data pointer!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedSensePtr = localSensePtr;
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedsenseptr is still not properly aligned\n");
                set_Console_Colors(true, DEFAULT);
            }
            #endif
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
        start_Timer(&commandTimer);
        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.scsiEx.target, scsiIoCtx->device->os_info.address.scsiEx.lun, srp, NULL);
        stop_Timer(&commandTimer);
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SCSIEx Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-InTransferLength = %" PRIu32 "\tOutTransferLength = %" PRIu32 "\n", srp->InTransferLength, srp->OutTransferLength);
        print_UEFI_SCSI_Ex_Adapter_Status(srp->HostAdapterStatus);
        print_UEFI_SCSI_Ex_Target_Status(srp->TargetStatus);
        set_Console_Colors(true, DEFAULT);
        #endif

        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        scsiIoCtx->device->os_info.last_error = Status;
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
        else
        {
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(scsiIoCtx->device->os_info.last_error);
                set_Console_Colors(true, DEFAULT);
            }
            if (Status == EFI_WRITE_PROTECTED)
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
        }
        safe_Free(localBuffer)
        safe_Free(localCDB)
        safe_Free(localSensePtr)
        safe_Free(srp)
        close_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum);
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
        seatimer_t commandTimer;
        uint8_t *alignedPointer = scsiIoCtx->pAtaCmdOpts->ptrData;
        uint8_t* localBuffer = NULL;
        bool localAlignedBuffer = false;
        EFI_ATA_PASS_THRU_COMMAND_PACKET	*ataPacket = NULL;// ata command packet
        EFI_ATA_COMMAND_BLOCK *ataCommand = (EFI_ATA_COMMAND_BLOCK*)calloc_aligned(1, sizeof(EFI_ATA_COMMAND_BLOCK), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
        EFI_ATA_STATUS_BLOCK *ataStatus = (EFI_ATA_STATUS_BLOCK*)calloc_aligned(1, sizeof(EFI_ATA_STATUS_BLOCK), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);

        ataPacket = (EFI_ATA_PASS_THRU_COMMAND_PACKET *) calloc_aligned(1, sizeof(EFI_ATA_PASS_THRU_COMMAND_PACKET), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);

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
            if (scsiIoCtx->timeout > 0)
            {
                ataPacket->Timeout = scsiIoCtx->pAtaCmdOpts->timeout * 1e7; //value is in 100ns units. zero means wait indefinitely
            }
            else
            {
                ataPacket->Timeout = 15 * 1e7; //15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->pAtaCmdOpts->ptrData, pPassthru->Mode->IoAlign))
        {
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoCtx->pAtaCmdOpts->ptrData is not aligned! Creating local aligned buffer\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(scsiIoCtx->pAtaCmdOpts->dataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
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
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, DEFAULT);
            }
            #endif
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
        ataCommand->AtaDeviceHead = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;//TODO: If a port multiplier value is present, we need to set the device select bit for compatibility with IDE mode.
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
        start_Timer(&commandTimer);
        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.ata.port, scsiIoCtx->device->os_info.address.ata.portMultiplierPort, ataPacket, NULL);
        stop_Timer(&commandTimer);
        //convert return status from sending the command into a return value for opensea-transport
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("ATA Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-InTransferLength = %" PRIu32 "\t OutTransferLength = %" PRIu32 "\n", ataPacket->InTransferLength, ataPacket->OutTransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif

        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        scsiIoCtx->device->os_info.last_error = Status;

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
        safe_Free(ataPacket)
        safe_Free(localBuffer)
        safe_Free(ataStatus)
        safe_Free(ataCommand)
        close_ATA_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum);
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
        seatimer_t commandTimer;
        uint8_t *alignedPointer = nvmeIoCtx->ptrData;
        uint8_t *localBuffer = NULL;
        bool localAlignedBuffer = false;
        EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET	*nrp = NULL;
        EFI_NVM_EXPRESS_COMMAND *nvmCommand = (EFI_NVM_EXPRESS_COMMAND*)calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_COMMAND), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
        EFI_NVM_EXPRESS_COMPLETION *nvmCompletion = (EFI_NVM_EXPRESS_COMPLETION*)calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_COMPLETION), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);

        if (!nvmCommand || !nvmCompletion)
        {
            printf("Error allocating command or completion for nrp\n");
            return MEMORY_FAILURE;
        }

        nrp = (EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET *)calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);

        if (!nrp)
        {
            printf("Error allocating NRP\n");
            return MEMORY_FAILURE;
        }

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
            if (nvmeIoCtx->timeout > 0)
            {
                nrp->CommandTimeout = nvmeIoCtx->timeout * 1e7; //value is in 100ns units. zero means wait indefinitely
            }
            else
            {
                nrp->CommandTimeout = 15 * 1e7; //15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        //This is a hack for now. We should be enforcing pointers and data transfer size on in, our, or bidirectional commands up above even if nothing is expected in the return data buffer - TJE
        if (nvmeIoCtx->commandDirection != XFER_NO_DATA && (nvmeIoCtx->dataSize == 0 || !nvmeIoCtx->ptrData))
        {
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, YELLOW);
            printf("WARNING! Data transfer command specifying zero length!\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(M_Max(512, nvmeIoCtx->dataSize), sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
            if (!localBuffer)
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, RED);
                printf("Failed to allocate memory for an aligned data pointer - missing buffer on data xfer command!\n");
                set_Console_Colors(true, DEFAULT);
                #endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            nvmeIoCtx->dataSize = M_Max(512, nvmeIoCtx->dataSize);
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, DEFAULT);
            }
            #endif
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(nvmeIoCtx->ptrData, pPassthru->Mode->IoAlign))
        {
            #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("nvmeIoCtx->ptrData is not aligned! Creating local aligned buffer\n");
            set_Console_Colors(true, DEFAULT);
            #endif
            //allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer = (uint8_t*)calloc_aligned(nvmeIoCtx->dataSize, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
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
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, DEFAULT);
                #endif
            }
        }

        //set transfer information
        nrp->TransferBuffer = alignedPointer;
        nrp->TransferLength = nvmeIoCtx->dataSize;

        //TODO: Handle metadata pointer...right now attempting to allocate something locally here just to get things working.
        nrp->MetadataBuffer = calloc_aligned(16, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
        nrp->MetadataLength = 16;

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
        if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
        {
            //printf("Sending ADMIN with NSID = %" PRIX32 "h\n", nvmeIoCtx->cmd.adminCmd.nsid);
            start_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = Status = pPassthru->PassThru(pPassthru, nvmeIoCtx->cmd.adminCmd.nsid, nrp, NULL);
            stop_Timer(&commandTimer);
            //printf("\tAdmin command returned %d\n", Status);
        }
        else
        {
            start_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = Status = pPassthru->PassThru(pPassthru, nvmeIoCtx->device->os_info.address.nvme.namespaceID, nrp, NULL);
            stop_Timer(&commandTimer);
        }
        #if defined (UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("NVMe Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-TransferLength = %" PRIu32 "\n", nrp->TransferLength);
        set_Console_Colors(true, DEFAULT);
        #endif
        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
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
            if (localAlignedBuffer && nvmeIoCtx->commandDirection == XFER_DATA_IN && nvmeIoCtx->ptrData)
            {
                memcpy(nvmeIoCtx->ptrData, alignedPointer, nvmeIoCtx->dataSize);
            }
        }
        else
        {
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(nvmeIoCtx->device->os_info.last_error);
                set_Console_Colors(true, DEFAULT);
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
        }
        safe_Free(nrp->MetadataBuffer)//TODO: Need to figure out a better way to handle the metadata than this...
        safe_Free(nrp)
        safe_Free(localBuffer)
        safe_Free(nvmCommand)
        safe_Free(nvmCompletion)
        close_NVMe_Passthru_Protocol_Ptr(&pPassthru, nvmeIoCtx->device->os_info.controllerNum);
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

int os_nvme_Reset(tDevice *device)
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

int os_nvme_Subsystem_Reset(tDevice *device)
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
                        safe_Free(devicePath)
                    }
                }
            }
        }
        //close the protocol
        gBS->CloseProtocol(handle[counter], &ataPtGUID, gImageHandle, NULL);
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
                            char ataHandle[UEFI_HANDLE_STRING_LENGTH] = { 0 };
                            snprintf(ataHandle, UEFI_HANDLE_STRING_LENGTH, "ata:%" PRIx16 ":%" PRIx16 ":%" PRIx16, counter, port, pmport);
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
                        safe_Free(devicePath)
                    }
                }
            }
        }
        //close the protocol
        gBS->CloseProtocol(handle[counter], &ataPtGUID, gImageHandle, NULL);
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
        uint64_t lun = UINT64_MAX;//doesn't specify what we should start with for this.
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextDevice(pPassthru, &target, & lun);
            if(uefiStatus == EFI_SUCCESS && target != UINT16_MAX && lun != UINT64_MAX)
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
                safe_Free(devicePath)
            }
        }
        //close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, NULL);
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
        uint64_t lun = UINT64_MAX;//doesn't specify what we should start with for this.
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextDevice(pPassthru, &target, & lun);
            if(uefiStatus == EFI_SUCCESS && target != UINT32_MAX && lun != UINT64_MAX)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    char scsiHandle[UEFI_HANDLE_STRING_LENGTH] = { 0 };
                    snprintf(scsiHandle, UEFI_HANDLE_STRING_LENGTH, "scsi:%" PRIx16 ":%" PRIx32 ":%" PRIx64, counter, target, lun);
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
                safe_Free(devicePath)
            }
        }
        //close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, NULL);
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
        uint64_t lun = UINT64_MAX;//doesn't specify what we should start with for this.
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
            if(uefiStatus == EFI_SUCCESS && memcmp(target, invalidTarget, TARGET_MAX_BYTES) != 0 && lun != UINT64_MAX)
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
                safe_Free(devicePath)
            }
        }
        //close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, NULL);
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
        uint64_t lun = UINT64_MAX;//doesn't specify what we should start with for this.
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
            if(uefiStatus == EFI_SUCCESS && memcmp(target, invalidTarget, TARGET_MAX_BYTES) != 0 && lun != UINT64_MAX)
            {
                //we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a device by using build device path
                EFI_DEVICE_PATH_PROTOCOL *devicePath;//will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if(buildPath == EFI_SUCCESS)
                {
                    //found a device!!!
                    char scsiExHandle[UEFI_HANDLE_STRING_LENGTH] = { 0 };
                    snprintf(scsiExHandle, UEFI_HANDLE_STRING_LENGTH, "scsiEx:%" PRIx16 ":%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 ":%" PRIx64, counter, target[0], target[1], target[2], target[3], target[4], target[5], target[6], target[7], target[8], target[9], target[10], target[11], target[12], target[13], target[14], target[15], lun);
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
                safe_Free(devicePath)
            }
        }
        //close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, NULL);
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
                safe_Free(devicePath)
            }
        }
        //close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &nvmePtGUID, gImageHandle, NULL);
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
                    char nvmeHandle[UEFI_HANDLE_STRING_LENGTH] = { 0 };
                    snprintf(nvmeHandle, UEFI_HANDLE_STRING_LENGTH, "nvme:%" PRIx16 ":%" PRIx32, counter, namespaceID);
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
                safe_Free(devicePath)
            }
        }
        //close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &nvmePtGUID, gImageHandle, NULL);
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
int get_Device_Count(uint32_t * numberOfDevices, M_ATTR_UNUSED uint64_t flags)
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
int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, M_ATTR_UNUSED uint64_t flags)
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

int os_Read(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool async, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Write(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool async, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Verify(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

int os_Flush(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}

int os_Lock_Device(M_ATTR_UNUSED tDevice *device)
{
    return SUCCESS;
}

int os_Unlock_Device(M_ATTR_UNUSED tDevice *device)
{
    return SUCCESS;
}