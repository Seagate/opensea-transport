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
#include <stdio.h>
#include <stdlib.h> // for mbstowcs_s
#include <stddef.h> // offsetof
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wchar.h>
#include <string.h>
#include <windows.h>                // added for forced PnP rescan
#include <tchar.h>
#include <initguid.h>
//#if !defined(DISABLE_NVME_PASSTHROUGH)
#include <ntddstor.h>
//#endif
//NOTE: ARM requires 10.0.16299.0 API to get this library!
#include <cfgmgr32.h>               // added for forced PnP rescan
//#include <setupapi.h> //NOTE: Not available for ARM
#include <devpropdef.h>
#include <devpkey.h>
#include <winbase.h>
#include "common.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"
#include "win_helper.h"
#include "sat_helper_func.h"
#include "usb_hacks.h"
#include "common_public.h"

#if !defined(DISABLE_NVME_PASSTHROUGH)
#include "nvme_helper.h"
#include "nvme_helper_func.h"
#endif

#if defined (ENABLE_CSMI) //when this is enabled, the "get_Device", "get_Device_Count", and "get_Device_List" will also check for CSMI devices unless a flag is given to ignore them. For apps only doing CSMI, call the csmi implementations directly
#include "csmi_helper_func.h"
#endif

//MinGW may or may not have some of these, so there is a need to define these here to build properly when they are otherwise not available.
//TODO: as mingw changes versions, some of these below may be available. Need to have a way to check mingw preprocessor defines for versions to work around these.
#if defined (__MINGW32__)
    #if !defined (ATA_FLAGS_NO_MULTIPLE)
        #define ATA_FLAGS_NO_MULTIPLE (1 << 5)
    #endif
    #if !defined (BusTypeSpaces)
        #define BusTypeSpaces 16
    #endif
    #if !defined (BusTypeNvme)
        #define BusTypeNvme 17
    #endif

    //This is for looking up hardware IDs of devices for PCIe/USB, etc
    #if !defined (DEVPKEY_Device_HardwareIds)
        DEFINE_DEVPROPKEY(DEVPKEY_Device_HardwareIds,            0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 3); 
    #endif

    #if !defined (CM_GETIDLIST_FILTER_PRESENT)
        #define CM_GETIDLIST_FILTER_PRESENT             (0x00000100)
    #endif
    #if !defined (CM_GETIDLIST_FILTER_CLASS)
        #define CM_GETIDLIST_FILTER_CLASS               (0x00000200)
    #endif

    #if !defined (GUID_DEVINTERFACE_DISK)
        DEFINE_GUID(GUID_DEVINTERFACE_DISK,                   0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
    #endif

#endif

#if WINVER < SEA_WIN32_WINNT_WINBLUE && !defined (BusTypeNvme)
#define BusTypeNvme 17
#endif

extern bool validate_Device_Struct(versionBlock);

int get_Windows_SMART_IO_Support(tDevice *device);
#if WINVER >= SEA_WIN32_WINNT_WIN10
int get_Windows_FWDL_IO_Support(tDevice *device, STORAGE_BUS_TYPE busType);
bool is_Firmware_Download_Command_Compatible_With_Win_API(ScsiIoCtx *scsiIoCtx);//TODO: add nvme support...may not need an NVMe version since it's the only way to update code on NVMe
int send_Win_ATA_Get_Log_Page_Cmd(ScsiIoCtx *scsiIoCtx);
int send_Win_ATA_Identify_Cmd(ScsiIoCtx *scsiIoCtx);
#if !defined(DISABLE_NVME_PASSTHROUGH)
void set_Namespace_ID_For_Device(tDevice *device);//For Win 10 NVMe
#endif
#endif
#if defined (_DEBUG)
// \fn print_bus_type (BYTE type)
// \nbrief Funtion to print in human readable format the BusType of a device
// \param BYTE which is STORAGE_BUS_TYPE windows enum
static void print_bus_type( BYTE type );

void print_bus_type( BYTE type )
{
    switch (type)
    {
    case BusTypeScsi:
        printf("SCSI");
        break;
    case BusTypeAtapi:
        printf("ATAPI");
        break;
    case BusTypeAta:
        printf("ATA");
        break;
    case BusType1394:
        printf("1394");
        break;
    case BusTypeSsa:
        printf("SSA");
        break;
    case BusTypeFibre:
        printf("FIBRE");
        break;
    case BusTypeUsb:
        printf("USB");
        break;
    case BusTypeRAID:
        printf("RAID");
        break;
    case BusTypeiScsi:
        printf("iSCSI");
        break;
    case BusTypeSas:
        printf("SAS");
        break;
    case BusTypeSata:
        printf("SATA");
        break;
    case BusTypeSd:
        printf("SD");
        break;
    case BusTypeMmc:
        printf("MMC");
        break;
    case BusTypeVirtual:
        printf("VIRTUAL");
        break;
    case BusTypeFileBackedVirtual:
        printf("FILEBACKEDVIRTUAL");
        break;
#if WINVER >= SEA_WIN32_WINNT_WIN8
    case BusTypeSpaces:
        printf("Spaces");
        break;
#if WINVER >= SEA_WIN32_WINNT_WINBLUE //8.1 introduced NVMe
    case BusTypeNvme:
        printf("NVMe");
        break;
#if WINVER >= SEA_WIN32_WINNT_WIN10 //Win10 API kits may have more or less of these bus types so need to also check which version of the Win10 API is being targetted
#if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_10586
    case BusTypeSCM:
        printf("SCM");
        break;
#if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    case BusTypeUfs:
        printf("UFS");
        break;
#endif //WIN_API_TARGET_VERSION >= Win 10 API 10.0.15063.0
#endif //WIN_API_TARGET_VERSION >= Win 10 API 10.0.10586.0
#endif //WINVER >= WIN10
#endif //WINVER >= WIN8.1
#endif //WINVER >= WIN8.0
    case BusTypeMax:
        printf("MAX");
        break;
    case BusTypeMaxReserved:
        printf("MAXRESERVED");
        break;
    default:
        printf("UNKNOWN");
        break;
    }
}
#endif

//This function is only used in get_Adapter_IDs which is why it's here. If this is useful for something else in the future, move it to opensea-common.
void convert_String_Spaces_To_Underscores(char *stringToChange)
{
    size_t stringLen = 0, iter = 0;
    if (stringToChange == NULL)
    {
        return;
    }
    stringLen = strlen(stringToChange);
    if (stringLen == 0)
    {
        return;
    }
    while (iter <= stringLen)
    {
        if (isspace(stringToChange[iter]))
        {
            stringToChange[iter] = '_';
        }
        iter++;
    }
}

//This function uses cfgmgr32 for figuring out the adapter information. 
//It is possible to do this with setupapi as well. cfgmgr32 is supposedly available in some form for universal apps, whereas setupapi is not.
int get_Adapter_IDs(tDevice *device, PSTORAGE_DEVICE_DESCRIPTOR deviceDescriptor, ULONG deviceDescriptorLength)
{
    int ret = BAD_PARAMETER;
    if (deviceDescriptor && deviceDescriptorLength > sizeof(STORAGE_DEVICE_DESCRIPTOR))//make sure we have a device descriptor bigger than the header so we can access the raw data
    {
        //First, get the list of disk device IDs, then locate a matching one...then find the parent and parse the IDs out.
        TCHAR *listBuffer = NULL;
        ULONG deviceIdListLen = 0;
        CONFIGRET cmRet = CR_SUCCESS;
#if WINVER > SEA_WIN32_WINNT_VISTA
        TCHAR *filter = TEXT("{4d36e967-e325-11ce-bfc1-08002be10318}");
        ULONG deviceIdListFlags = CM_GETIDLIST_FILTER_PRESENT | CM_GETIDLIST_FILTER_CLASS;//Both of these flags require Windows 7 and later. The else case below will handle older OSs
        cmRet = CM_Get_Device_ID_List_Size(&deviceIdListLen, filter, deviceIdListFlags);
        if (cmRet == CR_SUCCESS)
        {
            if (deviceIdListLen > 0)
            {
                listBuffer = (TCHAR*)calloc(deviceIdListLen, sizeof(TCHAR));
                if (listBuffer)
                {
                    cmRet = CM_Get_Device_ID_List(filter, listBuffer, deviceIdListLen, deviceIdListFlags);
                }
                else
                {
                    return MEMORY_FAILURE;
                }
            }
        }
        else if (cmRet == CR_INVALID_FLAG)
#else 
        if(cmRet == CR_SUCCESS)
#endif //WINVER > SEA_WIN32_WINNT_VISTA
        {
            //older OS? Try the legacy method which should work for Win2000+
            //This requires knowing if we are searching for USB vs SCSI device IDs
            //TODO: We may need to add other things for firewire or other attachment types that existed back in Vista or XP if they aren't handled under USB or SCSI
            TCHAR *scsiFilter = TEXT("SCSI"), *usbFilter = TEXT("USBSTOR");//Need to use USBSTOR in order to find a match. Using USB returns a list of VID/PID but we don't have a way to match that.
            ULONG scsiIdListLen = 0, usbIdListLen = 0;
            ULONG filterFlags = CM_GETIDLIST_FILTER_ENUMERATOR;
            TCHAR *scsiListBuff = NULL, *usbListBuff = NULL;
            CONFIGRET scsicmRet = CR_SUCCESS, usbcmRet = CR_SUCCESS;
            //First get the SCSI list, then the USB list/ TODO: add more things to the list as we need them.
            scsicmRet = CM_Get_Device_ID_List_Size(&scsiIdListLen, scsiFilter, filterFlags);
            if (scsicmRet == CR_SUCCESS)
            {
                if (scsiIdListLen > 0)
                {
                    scsiListBuff = (TCHAR*)calloc(scsiIdListLen, sizeof(TCHAR));
                    if (scsiListBuff)
                    {
                        scsicmRet = CM_Get_Device_ID_List(scsiFilter, scsiListBuff, scsiIdListLen, filterFlags);
                    }
                    else
                    {
                        ret = MEMORY_FAILURE;
                    }                    
                }
            }
            usbcmRet = CM_Get_Device_ID_List_Size(&usbIdListLen, usbFilter, filterFlags);
            if (usbcmRet == CR_SUCCESS)
            {
                if (usbIdListLen > 0)
                {
                    usbListBuff = (TCHAR*)calloc(usbIdListLen, sizeof(TCHAR));
                    if (usbListBuff)
                    {
                        usbcmRet = CM_Get_Device_ID_List(usbFilter, usbListBuff, usbIdListLen, filterFlags);
                    }
                    else
                    {
                        ret = MEMORY_FAILURE;
                    }
                }
            }
            //now that we got USB and SCSI, we need to merge them together into a common list
            deviceIdListLen = scsiIdListLen + usbIdListLen;
            listBuffer = (TCHAR*)calloc(deviceIdListLen, sizeof(TCHAR));
            if (listBuffer)
            {
                ULONG copyOffset = 0;
                if (scsicmRet == CR_SUCCESS && scsiIdListLen > 0 && scsiListBuff)
                {
                    memcpy(&listBuffer[copyOffset], scsiListBuff, scsiIdListLen);
                    copyOffset += scsiIdListLen - 1;
                }
                if (usbcmRet == CR_SUCCESS && usbIdListLen > 0 && usbListBuff)
                {
                    memcpy(&listBuffer[copyOffset], usbListBuff, usbIdListLen);
                    copyOffset += usbIdListLen - 1;
                }
                //add other lists here and offset them as needed
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
            safe_Free(scsiListBuff);
            safe_Free(usbListBuff);
        }
        else
        {
            return FAILURE;
        }
        //If we are here, we should have a list of device IDs to check
        //First, reduce the list to something that seems like it is the drive we are looking for...we could have more than 1 match, but we're filtering out all the extras that definitely aren't the correct device.
        if (ret != MEMORY_FAILURE && cmRet == CR_SUCCESS && deviceIdListLen > 0)
        {
            bool foundMatch = false;
            //Now we have a list of device IDs.
            //Each device ID is structured with a EmumeratorName\Disk&Ven_<vendor ID>&Prod_<Product ID>&Rev_<Revision number>\<some random numbers and characters>
            //Since we have a device descriptor, we have the vendor ID, product ID, and revision number, so we can match those.
            //NOTE: Some string matching is possible, but not reliable. All string matching was removed due to it not quite working as expected when it was needed the most.
            //This is potentially slower without it, but it will be fine...-TJE

            //loop through the device IDs and see if we find anything that matches.
            for (LPTSTR deviceID = listBuffer; *deviceID && !foundMatch; deviceID += _tcslen(deviceID) + 1)
            {
                //convert the deviceID to uppercase to make matching easier
                _tcsupr(deviceID);
                DEVINST deviceInstance = 0;
                //if a match is found, call locate devnode. If this is not present, it will fail and we need to continue through the loop
                cmRet = CM_Locate_DevNode(&deviceInstance, deviceID, CM_LOCATE_DEVNODE_NORMAL);
                if (CR_SUCCESS == cmRet)
                {
                    //with the device node, get the interface list for this device (disk class GUID is used). This SHOULD only return one idem which is the full device path. TODO: save this device path
                    ULONG interfaceListSize = 0;
                    GUID classGUID = GUID_DEVINTERFACE_DISK;//TODO: If the tDevice handle that was opened was a tape, changer or something else, change this GUID.
                    cmRet = CM_Get_Device_Interface_List_Size(&interfaceListSize, &classGUID, deviceID, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
                    if (CR_SUCCESS == cmRet && interfaceListSize > 0)
                    {
                        TCHAR *interfaceList = (TCHAR*)calloc(interfaceListSize, sizeof(TCHAR));
                        cmRet = CM_Get_Device_Interface_List(&classGUID, deviceID, interfaceList, interfaceListSize * sizeof(TCHAR), CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
                        if (CR_SUCCESS == cmRet)
                        {
                            //Loop through this list, just in case more than one thing comes through
                            for (LPTSTR deviceID = interfaceList; *deviceID && !foundMatch; deviceID += _tcslen(deviceID) + 1)
                            {
                                //With this device path, open a handle and get the storage device number. This is a match for the PhysicalDriveX number and we can check that for a match
                                HANDLE deviceHandle = CreateFile(deviceID,
                                    GENERIC_WRITE | GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL,
                                    OPEN_EXISTING,
                                    0,
                                    NULL);
                                if (deviceHandle != INVALID_HANDLE_VALUE)
                                {
                                    //If the storage device number matches, get the parent device instance, then the parent device ID. This will contain the USB VID/PID and PCI Vendor, product, and revision numbers.
                                    STORAGE_DEVICE_NUMBER deviceNumber;
                                    memset(&deviceNumber, 0, sizeof(STORAGE_DEVICE_NUMBER));
                                    DWORD returnedDataSize = 0;
                                    if (DeviceIoControl(deviceHandle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &deviceNumber, sizeof(STORAGE_DEVICE_NUMBER), &returnedDataSize, NULL))
                                    {
                                        if (deviceNumber.DeviceNumber == device->os_info.os_drive_number)
                                        {
                                            DEVINST parentInst = 0;
                                            foundMatch = true;
                                            //Now that we have a matching handle, get the parent information, then parse the values from that string
                                            cmRet = CM_Get_Parent(&parentInst, deviceInstance, 0);
                                            if (CR_SUCCESS == cmRet)
                                            {
                                                ULONG parentLen = 0;
                                                cmRet = CM_Get_Device_ID_Size(&parentLen, parentInst, 0);
                                                parentLen += 1;
                                                if (CR_SUCCESS == cmRet)
                                                {
                                                    TCHAR *parentBuffer = (TCHAR*)calloc(parentLen, sizeof(TCHAR));
                                                    cmRet = CM_Get_Device_ID(parentInst, parentBuffer, parentLen, 0);
                                                    if (CR_SUCCESS == cmRet)
                                                    {
                                                        //uncomment this else case to view all the possible device or parent properties when figuring out what else is necessary to store for a new device.
                                                        /*  //This is a comment switch. two slashes means uncomment the below, 1 means comment it out
                                                        {
                                                            ULONG propertyBufLen = 0;
                                                            DEVPROPTYPE propertyType = 0;
                                                            DEVINST propInst = deviceInstance;
                                                            const DEVPROPKEY *devproperty = &DEVPKEY_NAME;
                                                            uint16_t counter = 0, instanceCounter = 0;
                                                            //device instance first!
                                                            while (instanceCounter < 2)
                                                            {
                                                                if (instanceCounter > 0)
                                                                {
                                                                    printf("\n==========================\n");
                                                                    printf("Parent instance properties\n");
                                                                    printf("==========================\n\n");
                                                                }
                                                                else
                                                                {
                                                                    printf("\n==========================\n");
                                                                    printf("Device instance properties\n");
                                                                    printf("==========================\n\n");
                                                                }
                                                                while (devproperty)
                                                                {
                                                                    //print out name of property being checked.
                                                                    printf("========================================================================\n");
                                                                    switch (counter)
                                                                    {
                                                                    case 0:
                                                                        devproperty = &DEVPKEY_Device_DeviceDesc;
                                                                        printf("DEVPKEY_Device_DeviceDesc: \n");
                                                                        break;
                                                                    case 1:
                                                                        devproperty = &DEVPKEY_Device_HardwareIds;
                                                                        printf("DEVPKEY_Device_HardwareIds: \n");
                                                                        break;
                                                                    case 2:
                                                                        devproperty = &DEVPKEY_Device_CompatibleIds;
                                                                        printf("DEVPKEY_Device_CompatibleIds: \n");
                                                                        break;
                                                                    case 3:
                                                                        devproperty = &DEVPKEY_Device_Service;
                                                                        printf("DEVPKEY_Device_Service: \n");
                                                                        break;
                                                                    case 4:
                                                                        devproperty = &DEVPKEY_Device_Class;
                                                                        printf("DEVPKEY_Device_Class: \n");
                                                                        break;
                                                                    case 5:
                                                                        devproperty = &DEVPKEY_Device_ClassGuid;
                                                                        printf("DEVPKEY_Device_ClassGuid: \n");
                                                                        break;
                                                                    case 6:
                                                                        devproperty = &DEVPKEY_Device_Driver;
                                                                        printf("DEVPKEY_Device_Driver: \n");
                                                                        break;
                                                                    case 7:
                                                                        devproperty = &DEVPKEY_Device_ConfigFlags;
                                                                        printf("DEVPKEY_Device_ConfigFlags: \n");
                                                                        break;
                                                                    case 8:
                                                                        devproperty = &DEVPKEY_Device_Manufacturer;
                                                                        printf("DEVPKEY_Device_Manufacturer: \n");
                                                                        break;
                                                                    case 9:
                                                                        devproperty = &DEVPKEY_Device_FriendlyName;
                                                                        printf("DEVPKEY_Device_FriendlyName: \n");
                                                                        break;
                                                                    case 10:
                                                                        devproperty = &DEVPKEY_Device_LocationInfo;
                                                                        printf("DEVPKEY_Device_LocationInfo: \n");
                                                                        break;
                                                                    case 11:
                                                                        devproperty = &DEVPKEY_Device_PDOName;
                                                                        printf("DEVPKEY_Device_PDOName: \n");
                                                                        break;
                                                                    case 12:
                                                                        devproperty = &DEVPKEY_Device_Capabilities;
                                                                        printf("DEVPKEY_Device_Capabilities: \n");
                                                                        break;
                                                                    case 13:
                                                                        devproperty = &DEVPKEY_Device_UINumber;
                                                                        printf("DEVPKEY_Device_UINumber: \n");
                                                                        break;
                                                                    case 14:
                                                                        devproperty = &DEVPKEY_Device_UpperFilters;
                                                                        printf("DEVPKEY_Device_UpperFilters: \n");
                                                                        break;
                                                                    case 15:
                                                                        devproperty = &DEVPKEY_Device_LowerFilters;
                                                                        printf("DEVPKEY_Device_LowerFilters: \n");
                                                                        break;
                                                                    case 16:
                                                                        devproperty = &DEVPKEY_Device_BusTypeGuid;
                                                                        printf("DEVPKEY_Device_BusTypeGuid: \n");
                                                                        break;
                                                                    case 17:
                                                                        devproperty = &DEVPKEY_Device_LegacyBusType;
                                                                        printf("DEVPKEY_Device_LegacyBusType: \n");
                                                                        break;
                                                                    case 18:
                                                                        devproperty = &DEVPKEY_Device_BusNumber;
                                                                        printf("DEVPKEY_Device_BusNumber: \n");
                                                                        break;
                                                                    case 19:
                                                                        devproperty = &DEVPKEY_Device_EnumeratorName;
                                                                        printf("DEVPKEY_Device_EnumeratorName: \n");
                                                                        break;
                                                                    case 20:
                                                                        devproperty = &DEVPKEY_Device_Security;
                                                                        printf("DEVPKEY_Device_Security: \n");
                                                                        break;
                                                                    case 21:
                                                                        devproperty = &DEVPKEY_Device_SecuritySDS;
                                                                        printf("DEVPKEY_Device_SecuritySDS: \n");
                                                                        break;
                                                                    case 22:
                                                                        devproperty = &DEVPKEY_Device_DevType;
                                                                        printf("DEVPKEY_Device_DevType: \n");
                                                                        break;
                                                                    case 23:
                                                                        devproperty = &DEVPKEY_Device_Exclusive;
                                                                        printf("DEVPKEY_Device_Exclusive: \n");
                                                                        break;
                                                                    case 24:
                                                                        devproperty = &DEVPKEY_Device_Characteristics;
                                                                        printf("DEVPKEY_Device_Characteristics: \n");
                                                                        break;
                                                                    case 25:
                                                                        devproperty = &DEVPKEY_Device_Address;
                                                                        printf("DEVPKEY_Device_Address: \n");
                                                                        break;
                                                                    case 26:
                                                                        devproperty = &DEVPKEY_Device_UINumberDescFormat;
                                                                        printf("DEVPKEY_Device_UINumberDescFormat: \n");
                                                                        break;
                                                                    case 27:
                                                                        devproperty = &DEVPKEY_Device_PowerData;
                                                                        printf("DEVPKEY_Device_PowerData: \n");
                                                                        break;
                                                                    case 28:
                                                                        devproperty = &DEVPKEY_Device_RemovalPolicy;
                                                                        printf("DEVPKEY_Device_RemovalPolicy: \n");
                                                                        break;
                                                                    case 29:
                                                                        devproperty = &DEVPKEY_Device_RemovalPolicyDefault;
                                                                        printf("DEVPKEY_Device_RemovalPolicyDefault: \n");
                                                                        break;
                                                                    case 30:
                                                                        devproperty = &DEVPKEY_Device_RemovalPolicyOverride;
                                                                        printf("DEVPKEY_Device_RemovalPolicyOverride: \n");
                                                                        break;
                                                                    case 31:
                                                                        devproperty = &DEVPKEY_Device_InstallState;
                                                                        printf("DEVPKEY_Device_InstallState: \n");
                                                                        break;
                                                                    case 32:
                                                                        devproperty = &DEVPKEY_Device_LocationPaths;
                                                                        printf("DEVPKEY_Device_LocationPaths: \n");
                                                                        break;
                                                                    case 33:
                                                                        devproperty = &DEVPKEY_Device_BaseContainerId;
                                                                        printf("DEVPKEY_Device_BaseContainerId: \n");
                                                                        break;
                                                                    case 34:
                                                                        devproperty = &DEVPKEY_Device_InstanceId;
                                                                        printf("DEVPKEY_Device_InstanceId: \n");
                                                                        break;
                                                                    case 35:
                                                                        devproperty = &DEVPKEY_Device_DevNodeStatus;
                                                                        printf("DEVPKEY_Device_DevNodeStatus: \n");
                                                                        break;
                                                                    case 36:
                                                                        devproperty = &DEVPKEY_Device_ProblemCode;
                                                                        printf("DEVPKEY_Device_ProblemCode: \n");
                                                                        break;
                                                                    case 37:
                                                                        devproperty = &DEVPKEY_Device_EjectionRelations;
                                                                        printf("DEVPKEY_Device_EjectionRelations: \n");
                                                                        break;
                                                                    case 38:
                                                                        devproperty = &DEVPKEY_Device_RemovalRelations;
                                                                        printf("DEVPKEY_Device_RemovalRelations: \n");
                                                                        break;
                                                                    case 39:
                                                                        devproperty = &DEVPKEY_Device_PowerRelations;
                                                                        printf("DEVPKEY_Device_PowerRelations: \n");
                                                                        break;
                                                                    case 40:
                                                                        devproperty = &DEVPKEY_Device_BusRelations;
                                                                        printf("DEVPKEY_Device_BusRelations: \n");
                                                                        break;
                                                                    case 41:
                                                                        devproperty = &DEVPKEY_Device_Parent;
                                                                        printf("DEVPKEY_Device_Parent: \n");
                                                                        break;
                                                                    case 42:
                                                                        devproperty = &DEVPKEY_Device_Children;
                                                                        printf("DEVPKEY_Device_Children: \n");
                                                                        break;
                                                                    case 43:
                                                                        devproperty = &DEVPKEY_Device_Siblings;
                                                                        printf("DEVPKEY_Device_Siblings: \n");
                                                                        break;
                                                                    case 44:
                                                                        devproperty = &DEVPKEY_Device_TransportRelations;
                                                                        printf("DEVPKEY_Device_TransportRelations: \n");
                                                                        break;
                                                                    case 45:
                                                                        devproperty = &DEVPKEY_Device_ProblemStatus;
                                                                        printf("DEVPKEY_Device_ProblemStatus: \n");
                                                                        break;
                                                                    case 46:
                                                                        devproperty = &DEVPKEY_Device_Reported;
                                                                        printf("DEVPKEY_Device_Reported: \n");
                                                                        break;
                                                                    case 47:
                                                                        devproperty = &DEVPKEY_Device_Legacy;
                                                                        printf("DEVPKEY_Device_Legacy: \n");
                                                                        break;
                                                                    case 48:
                                                                        devproperty = &DEVPKEY_Device_ContainerId;
                                                                        printf("DEVPKEY_Device_ContainerId: \n");
                                                                        break;
                                                                    case 49:
                                                                        devproperty = &DEVPKEY_Device_InLocalMachineContainer;
                                                                        printf("DEVPKEY_Device_InLocalMachineContainer: \n");
                                                                        break;
                                                                    case 50:
                                                                        devproperty = &DEVPKEY_Device_Model;
                                                                        printf("DEVPKEY_Device_Model: \n");
                                                                        break;
                                                                    case 51:
                                                                        devproperty = &DEVPKEY_Device_ModelId;
                                                                        printf("DEVPKEY_Device_ModelId: \n");
                                                                        break;
                                                                    case 52:
                                                                        devproperty = &DEVPKEY_Device_FriendlyNameAttributes;
                                                                        printf("DEVPKEY_Device_FriendlyNameAttributes: \n");
                                                                        break;
                                                                    case 53:
                                                                        devproperty = &DEVPKEY_Device_ManufacturerAttributes;
                                                                        printf("DEVPKEY_Device_ManufacturerAttributes: \n");
                                                                        break;
                                                                    case 54:
                                                                        devproperty = &DEVPKEY_Device_PresenceNotForDevice;
                                                                        printf("DEVPKEY_Device_PresenceNotForDevice: \n");
                                                                        break;
                                                                    case 55:
                                                                        devproperty = &DEVPKEY_Device_SignalStrength;
                                                                        printf("DEVPKEY_Device_SignalStrength: \n");
                                                                        break;
                                                                    case 56:
                                                                        devproperty = &DEVPKEY_Device_IsAssociateableByUserAction;
                                                                        printf("DEVPKEY_Device_IsAssociateableByUserAction: \n");
                                                                        break;
                                                                    case 57:
                                                                        devproperty = &DEVPKEY_Device_ShowInUninstallUI;
                                                                        printf("DEVPKEY_Device_ShowInUninstallUI: \n");
                                                                        break;
                                                                    case 58:
                                                                        devproperty = &DEVPKEY_Device_Numa_Proximity_Domain;
                                                                        printf("DEVPKEY_Device_Numa_Proximity_Domain: \n");
                                                                        break;
                                                                    case 59:
                                                                        devproperty = &DEVPKEY_Device_DHP_Rebalance_Policy;
                                                                        printf("DEVPKEY_Device_DHP_Rebalance_Policy: \n");
                                                                        break;
                                                                    case 60:
                                                                        devproperty = &DEVPKEY_Device_Numa_Node;
                                                                        printf("DEVPKEY_Device_Numa_Node: \n");
                                                                        break;
                                                                    case 61:
                                                                        devproperty = &DEVPKEY_Device_BusReportedDeviceDesc;
                                                                        printf("DEVPKEY_Device_BusReportedDeviceDesc: \n");
                                                                        break;
                                                                    case 62:
                                                                        devproperty = &DEVPKEY_Device_IsPresent;
                                                                        printf("DEVPKEY_Device_IsPresent: \n");
                                                                        break;
                                                                    case 63:
                                                                        devproperty = &DEVPKEY_Device_HasProblem;
                                                                        printf("DEVPKEY_Device_HasProblem: \n");
                                                                        break;
                                                                    case 64:
                                                                        devproperty = &DEVPKEY_Device_ConfigurationId;
                                                                        printf("DEVPKEY_Device_ConfigurationId: \n");
                                                                        break;
                                                                    case 65:
                                                                        devproperty = &DEVPKEY_Device_ReportedDeviceIdsHash;
                                                                        printf("DEVPKEY_Device_ReportedDeviceIdsHash: \n");
                                                                        break;
                                                                    case 66:
                                                                        devproperty = &DEVPKEY_Device_PhysicalDeviceLocation;
                                                                        printf("DEVPKEY_Device_PhysicalDeviceLocation: \n");
                                                                        break;
                                                                    case 67:
                                                                        devproperty = &DEVPKEY_Device_BiosDeviceName;
                                                                        printf("DEVPKEY_Device_BiosDeviceName: \n");
                                                                        break;
                                                                    case 68:
                                                                        devproperty = &DEVPKEY_Device_DriverProblemDesc;
                                                                        printf("DEVPKEY_Device_DriverProblemDesc: \n");
                                                                        break;
                                                                    case 69:
                                                                        devproperty = &DEVPKEY_Device_DebuggerSafe;
                                                                        printf("DEVPKEY_Device_DebuggerSafe: \n");
                                                                        break;
                                                                    case 70:
                                                                        devproperty = &DEVPKEY_Device_PostInstallInProgress;
                                                                        printf("DEVPKEY_Device_PostInstallInProgress: \n");
                                                                        break;
                                                                    case 71:
                                                                        devproperty = &DEVPKEY_Device_Stack;
                                                                        printf("DEVPKEY_Device_Stack: \n");
                                                                        break;
                                                                    case 72:
                                                                        devproperty = &DEVPKEY_Device_ExtendedConfigurationIds;
                                                                        printf("DEVPKEY_Device_ExtendedConfigurationIds: \n");
                                                                        break;
                                                                    case 73:
                                                                        devproperty = &DEVPKEY_Device_IsRebootRequired;
                                                                        printf("DEVPKEY_Device_IsRebootRequired: \n");
                                                                        break;
                                                                    case 74:
                                                                        devproperty = &DEVPKEY_Device_FirmwareDate;
                                                                        printf("DEVPKEY_Device_FirmwareDate: \n");
                                                                        break;
                                                                    case 75:
                                                                        devproperty = &DEVPKEY_Device_FirmwareVersion;
                                                                        printf("DEVPKEY_Device_FirmwareVersion: \n");
                                                                        break;
                                                                    case 76:
                                                                        devproperty = &DEVPKEY_Device_FirmwareRevision;
                                                                        printf("DEVPKEY_Device_FirmwareRevision: \n");
                                                                        break;
                                                                    case 77:
                                                                        devproperty = &DEVPKEY_Device_DependencyProviders;
                                                                        printf("DEVPKEY_Device_DependencyProviders: \n");
                                                                        break;
                                                                    case 78:
                                                                        devproperty = &DEVPKEY_Device_DependencyDependents;
                                                                        printf("DEVPKEY_Device_DependencyDependents: \n");
                                                                        break;
                                                                    case 79:
                                                                        devproperty = &DEVPKEY_Device_SoftRestartSupported;
                                                                        printf("DEVPKEY_Device_SoftRestartSupported: \n");
                                                                        break;
                                                                    case 80:
                                                                        devproperty = &DEVPKEY_Device_ExtendedAddress;
                                                                        printf("DEVPKEY_Device_ExtendedAddress: \n");
                                                                        break;
                                                                    case 81:
                                                                        devproperty = &DEVPKEY_Device_SessionId;
                                                                        printf("DEVPKEY_Device_SessionId: \n");
                                                                        break;
                                                                    case 82:
                                                                        devproperty = &DEVPKEY_Device_InstallDate;
                                                                        printf("DEVPKEY_Device_InstallDate: \n");
                                                                        break;
                                                                    case 83:
                                                                        devproperty = &DEVPKEY_Device_FirstInstallDate;
                                                                        printf("DEVPKEY_Device_FirstInstallDate: \n");
                                                                        break;
                                                                    case 84:
                                                                        devproperty = &DEVPKEY_Device_LastArrivalDate;
                                                                        printf("DEVPKEY_Device_LastArrivalDate: \n");
                                                                        break;
                                                                    case 85:
                                                                        devproperty = &DEVPKEY_Device_LastRemovalDate;
                                                                        printf("DEVPKEY_Device_LastRemovalDate: \n");
                                                                        break;
                                                                    case 86:
                                                                        devproperty = &DEVPKEY_Device_DriverDate;
                                                                        printf("DEVPKEY_Device_DriverDate: \n");
                                                                        break;
                                                                    case 87:
                                                                        devproperty = &DEVPKEY_Device_DriverVersion;
                                                                        printf("DEVPKEY_Device_DriverVersion: \n");
                                                                        break;
                                                                    case 88:
                                                                        devproperty = &DEVPKEY_Device_DriverDesc;
                                                                        printf("DEVPKEY_Device_DriverDesc: \n");
                                                                        break;
                                                                    case 89:
                                                                        devproperty = &DEVPKEY_Device_DriverInfPath;
                                                                        printf("DEVPKEY_Device_DriverInfPath: \n");
                                                                        break;
                                                                    case 90:
                                                                        devproperty = &DEVPKEY_Device_DriverInfSection;
                                                                        printf("DEVPKEY_Device_DriverInfSection: \n");
                                                                        break;
                                                                    case 91:
                                                                        devproperty = &DEVPKEY_Device_DriverInfSectionExt;
                                                                        printf("DEVPKEY_Device_DriverInfSectionExt: \n");
                                                                        break;
                                                                    case 92:
                                                                        devproperty = &DEVPKEY_Device_MatchingDeviceId;
                                                                        printf("DEVPKEY_Device_MatchingDeviceId: \n");
                                                                        break;
                                                                    case 93:
                                                                        devproperty = &DEVPKEY_Device_DriverProvider;
                                                                        printf("DEVPKEY_Device_DriverProvider: \n");
                                                                        break;
                                                                    case 94:
                                                                        devproperty = &DEVPKEY_Device_DriverPropPageProvider;
                                                                        printf("DEVPKEY_Device_DriverPropPageProvider: \n");
                                                                        break;
                                                                    case 95:
                                                                        devproperty = &DEVPKEY_Device_DriverCoInstallers;
                                                                        printf("DEVPKEY_Device_DriverCoInstallers: \n");
                                                                        break;
                                                                    case 96:
                                                                        devproperty = &DEVPKEY_Device_ResourcePickerTags;
                                                                        printf("DEVPKEY_Device_ResourcePickerTags: \n");
                                                                        break;
                                                                    case 97:
                                                                        devproperty = &DEVPKEY_Device_ResourcePickerExceptions;
                                                                        printf("DEVPKEY_Device_ResourcePickerExceptions: \n");
                                                                        break;
                                                                    case 98:
                                                                        devproperty = &DEVPKEY_Device_DriverRank;
                                                                        printf("DEVPKEY_Device_DriverRank: \n");
                                                                        break;
                                                                    case 99:
                                                                        devproperty = &DEVPKEY_Device_DriverLogoLevel;
                                                                        printf("DEVPKEY_Device_DriverLogoLevel: \n");
                                                                        break;
                                                                    case 100:
                                                                        devproperty = &DEVPKEY_Device_NoConnectSound;
                                                                        printf("DEVPKEY_Device_NoConnectSound: \n");
                                                                        break;
                                                                    case 101:
                                                                        devproperty = &DEVPKEY_Device_GenericDriverInstalled;
                                                                        printf("DEVPKEY_Device_GenericDriverInstalled: \n");
                                                                        break;
                                                                    case 102:
                                                                        devproperty = &DEVPKEY_Device_AdditionalSoftwareRequested;
                                                                        printf("DEVPKEY_Device_AdditionalSoftwareRequested: \n");
                                                                        break;
                                                                    case 103:
                                                                        devproperty = &DEVPKEY_Device_SafeRemovalRequired;
                                                                        printf("DEVPKEY_Device_SafeRemovalRequired: \n");
                                                                        break;
                                                                    case 104:
                                                                        devproperty = &DEVPKEY_Device_SafeRemovalRequiredOverride;
                                                                        printf("DEVPKEY_Device_SafeRemovalRequiredOverride: \n");
                                                                        break;
                                                                    case 105:
                                                                        devproperty = &DEVPKEY_DrvPkg_Model;
                                                                        printf("DEVPKEY_DrvPkg_Model: \n");
                                                                        break;
                                                                    case 106:
                                                                        devproperty = &DEVPKEY_DrvPkg_VendorWebSite;
                                                                        printf("DEVPKEY_DrvPkg_VendorWebSite: \n");
                                                                        break;
                                                                    case 107:
                                                                        devproperty = &DEVPKEY_DrvPkg_DetailedDescription;
                                                                        printf("DEVPKEY_DrvPkg_DetailedDescription: \n");
                                                                        break;
                                                                    case 108:
                                                                        devproperty = &DEVPKEY_DrvPkg_DocumentationLink;
                                                                        printf("DEVPKEY_DrvPkg_DocumentationLink: \n");
                                                                        break;
                                                                    case 109:
                                                                        devproperty = &DEVPKEY_DrvPkg_Icon;
                                                                        printf("DEVPKEY_DrvPkg_Icon: \n");
                                                                        break;
                                                                    case 110:
                                                                        devproperty = &DEVPKEY_DrvPkg_BrandingIcon;
                                                                        printf("DEVPKEY_DrvPkg_BrandingIcon: \n");
                                                                        break;
                                                                    case 111:
                                                                        devproperty = &DEVPKEY_DeviceClass_UpperFilters;
                                                                        printf("DEVPKEY_DeviceClass_UpperFilters: \n");
                                                                        break;
                                                                    case 112:
                                                                        devproperty = &DEVPKEY_DeviceClass_LowerFilters;
                                                                        printf("DEVPKEY_DeviceClass_LowerFilters: \n");
                                                                        break;
                                                                    case 113:
                                                                        devproperty = &DEVPKEY_DeviceClass_Security;
                                                                        printf("DEVPKEY_DeviceClass_Security: \n");
                                                                        break;
                                                                    case 114:
                                                                        devproperty = &DEVPKEY_DeviceClass_SecuritySDS;
                                                                        printf("DEVPKEY_DeviceClass_SecuritySDS: \n");
                                                                        break;
                                                                    case 115:
                                                                        devproperty = &DEVPKEY_DeviceClass_DevType;
                                                                        printf("DEVPKEY_DeviceClass_DevType: \n");
                                                                        break;
                                                                    case 116:
                                                                        devproperty = &DEVPKEY_DeviceClass_Exclusive;
                                                                        printf("DEVPKEY_DeviceClass_Exclusive: \n");
                                                                        break;
                                                                    case 117:
                                                                        devproperty = &DEVPKEY_DeviceClass_Characteristics;
                                                                        printf("DEVPKEY_DeviceClass_Characteristics: \n");
                                                                        break;
                                                                    case 118:
                                                                        devproperty = &DEVPKEY_DeviceClass_Name;
                                                                        printf("DEVPKEY_DeviceClass_Name: \n");
                                                                        break;
                                                                    case 119:
                                                                        devproperty = &DEVPKEY_DeviceClass_ClassName;
                                                                        printf("DEVPKEY_DeviceClass_ClassName: \n");
                                                                        break;
                                                                    case 120:
                                                                        devproperty = &DEVPKEY_DeviceClass_Icon;
                                                                        printf("DEVPKEY_DeviceClass_Icon: \n");
                                                                        break;
                                                                    case 121:
                                                                        devproperty = &DEVPKEY_DeviceClass_ClassInstaller;
                                                                        printf("DEVPKEY_DeviceClass_ClassInstaller: \n");
                                                                        break;
                                                                    case 122:
                                                                        devproperty = &DEVPKEY_DeviceClass_PropPageProvider;
                                                                        printf("DEVPKEY_DeviceClass_PropPageProvider: \n");
                                                                        break;
                                                                    case 123:
                                                                        devproperty = &DEVPKEY_DeviceClass_NoInstallClass;
                                                                        printf("DEVPKEY_DeviceClass_NoInstallClass: \n");
                                                                        break;
                                                                    case 124:
                                                                        devproperty = &DEVPKEY_DeviceClass_NoDisplayClass;
                                                                        printf("DEVPKEY_DeviceClass_NoDisplayClass: \n");
                                                                        break;
                                                                    case 125:
                                                                        devproperty = &DEVPKEY_DeviceClass_SilentInstall;
                                                                        printf("DEVPKEY_DeviceClass_SilentInstall: \n");
                                                                        break;
                                                                    case 126:
                                                                        devproperty = &DEVPKEY_DeviceClass_NoUseClass;
                                                                        printf("DEVPKEY_DeviceClass_NoUseClass: \n");
                                                                        break;
                                                                    case 127:
                                                                        devproperty = &DEVPKEY_DeviceClass_DefaultService;
                                                                        printf("DEVPKEY_DeviceClass_DefaultService: \n");
                                                                        break;
                                                                    case 128:
                                                                        devproperty = &DEVPKEY_DeviceClass_IconPath;
                                                                        printf("DEVPKEY_DeviceClass_IconPath: \n");
                                                                        break;
                                                                    case 129:
                                                                        devproperty = &DEVPKEY_DeviceClass_DHPRebalanceOptOut;
                                                                        printf("DEVPKEY_DeviceClass_DHPRebalanceOptOut: \n");
                                                                        break;
                                                                    case 130:
                                                                        devproperty = &DEVPKEY_DeviceClass_ClassCoInstallers;
                                                                        printf("DEVPKEY_DeviceClass_ClassCoInstallers: \n");
                                                                        break;
                                                                    case 131:
                                                                        devproperty = &DEVPKEY_DeviceInterface_FriendlyName;
                                                                        printf("DEVPKEY_DeviceInterface_FriendlyName: \n");
                                                                        break;
                                                                    case 132:
                                                                        devproperty = &DEVPKEY_DeviceInterface_Enabled;
                                                                        printf("DEVPKEY_DeviceInterface_Enabled: \n");
                                                                        break;
                                                                    case 133:
                                                                        devproperty = &DEVPKEY_DeviceInterface_ClassGuid;
                                                                        printf("DEVPKEY_DeviceInterface_ClassGuid: \n");
                                                                        break;
                                                                    case 134:
                                                                        devproperty = &DEVPKEY_DeviceInterface_ReferenceString;
                                                                        printf("DEVPKEY_DeviceInterface_ReferenceString: \n");
                                                                        break;
                                                                    case 135:
                                                                        devproperty = &DEVPKEY_DeviceInterface_Restricted;
                                                                        printf("DEVPKEY_DeviceInterface_Restricted: \n");
                                                                        break;
                                                                    case 136:
                                                                        devproperty = &DEVPKEY_DeviceInterface_UnrestrictedAppCapabilities;
                                                                        printf("DEVPKEY_DeviceInterface_UnrestrictedAppCapabilities: \n");
                                                                        break;
                                                                    case 137:
                                                                        devproperty = &DEVPKEY_DeviceInterface_SchematicName;
                                                                        printf("DEVPKEY_DeviceInterface_SchematicName: \n");
                                                                        break;
                                                                    case 138:
                                                                        devproperty = &DEVPKEY_DeviceInterfaceClass_DefaultInterface;
                                                                        printf("DEVPKEY_DeviceInterfaceClass_DefaultInterface: \n");
                                                                        break;
                                                                    case 139:
                                                                        devproperty = &DEVPKEY_DeviceInterfaceClass_Name;
                                                                        printf("DEVPKEY_DeviceInterfaceClass_Name: \n");
                                                                        break;
                                                                    case 140:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Address;
                                                                        printf("DEVPKEY_DeviceContainer_Address: \n");
                                                                        break;
                                                                    case 141:
                                                                        devproperty = &DEVPKEY_DeviceContainer_DiscoveryMethod;
                                                                        printf("DEVPKEY_DeviceContainer_DiscoveryMethod: \n");
                                                                        break;
                                                                    case 142:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsEncrypted;
                                                                        printf("DEVPKEY_DeviceContainer_IsEncrypted: \n");
                                                                        break;
                                                                    case 143:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsAuthenticated;
                                                                        printf("DEVPKEY_DeviceContainer_IsAuthenticated: \n");
                                                                        break;
                                                                    case 144:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsConnected;
                                                                        printf("DEVPKEY_DeviceContainer_IsConnected: \n");
                                                                        break;
                                                                    case 145:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsPaired;
                                                                        printf("DEVPKEY_DeviceContainer_IsPaired: \n");
                                                                        break;
                                                                    case 146:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Icon;
                                                                        printf("DEVPKEY_DeviceContainer_Icon: \n");
                                                                        break;
                                                                    case 147:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Version;
                                                                        printf("DEVPKEY_DeviceContainer_Version: \n");
                                                                        break;
                                                                    case 148:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Last_Seen;
                                                                        printf("DEVPKEY_DeviceContainer_Last_Seen: \n");
                                                                        break;
                                                                    case 149:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Last_Connected;
                                                                        printf("DEVPKEY_DeviceContainer_Last_Connected: \n");
                                                                        break;
                                                                    case 150:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsShowInDisconnectedState;
                                                                        printf("DEVPKEY_DeviceContainer_IsShowInDisconnectedState: \n");
                                                                        break;
                                                                    case 151:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsLocalMachine;
                                                                        printf("DEVPKEY_DeviceContainer_IsLocalMachine: \n");
                                                                        break;
                                                                    case 152:
                                                                        devproperty = &DEVPKEY_DeviceContainer_MetadataPath;
                                                                        printf("DEVPKEY_DeviceContainer_MetadataPath: \n");
                                                                        break;
                                                                    case 153:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsMetadataSearchInProgress;
                                                                        printf("DEVPKEY_DeviceContainer_IsMetadataSearchInProgress: \n");
                                                                        break;
                                                                    case 154:
                                                                        devproperty = &DEVPKEY_DeviceContainer_MetadataChecksum;
                                                                        printf("DEVPKEY_DeviceContainer_MetadataChecksum: \n");
                                                                        break;
                                                                    case 155:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsNotInterestingForDisplay;
                                                                        printf("DEVPKEY_DeviceContainer_IsNotInterestingForDisplay: \n");
                                                                        break;
                                                                    case 156:
                                                                        devproperty = &DEVPKEY_DeviceContainer_LaunchDeviceStageOnDeviceConnect;
                                                                        printf("DEVPKEY_DeviceContainer_LaunchDeviceStageOnDeviceConnect: \n");
                                                                        break;
                                                                    case 157:
                                                                        devproperty = &DEVPKEY_DeviceContainer_LaunchDeviceStageFromExplorer;
                                                                        printf("DEVPKEY_DeviceContainer_LaunchDeviceStageFromExplorer: \n");
                                                                        break;
                                                                    case 158:
                                                                        devproperty = &DEVPKEY_DeviceContainer_BaselineExperienceId;
                                                                        printf("DEVPKEY_DeviceContainer_BaselineExperienceId: \n");
                                                                        break;
                                                                    case 159:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsDeviceUniquelyIdentifiable;
                                                                        printf("DEVPKEY_DeviceContainer_IsDeviceUniquelyIdentifiable: \n");
                                                                        break;
                                                                    case 160:
                                                                        devproperty = &DEVPKEY_DeviceContainer_AssociationArray;
                                                                        printf("DEVPKEY_DeviceContainer_AssociationArray: \n");
                                                                        break;
                                                                    case 161:
                                                                        devproperty = &DEVPKEY_DeviceContainer_DeviceDescription1;
                                                                        printf("DEVPKEY_DeviceContainer_DeviceDescription1: \n");
                                                                        break;
                                                                    case 162:
                                                                        devproperty = &DEVPKEY_DeviceContainer_DeviceDescription2;
                                                                        printf("DEVPKEY_DeviceContainer_DeviceDescription2: \n");
                                                                        break;
                                                                    case 163:
                                                                        devproperty = &DEVPKEY_DeviceContainer_HasProblem;
                                                                        printf("DEVPKEY_DeviceContainer_HasProblem: \n");
                                                                        break;
                                                                    case 164:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsSharedDevice;
                                                                        printf("DEVPKEY_DeviceContainer_IsSharedDevice: \n");
                                                                        break;
                                                                    case 165:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsNetworkDevice;
                                                                        printf("DEVPKEY_DeviceContainer_IsNetworkDevice: \n");
                                                                        break;
                                                                    case 166:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsDefaultDevice;
                                                                        printf("DEVPKEY_DeviceContainer_IsDefaultDevice: \n");
                                                                        break;
                                                                    case 167:
                                                                        devproperty = &DEVPKEY_DeviceContainer_MetadataCabinet;
                                                                        printf("DEVPKEY_DeviceContainer_MetadataCabinet: \n");
                                                                        break;
                                                                    case 168:
                                                                        devproperty = &DEVPKEY_DeviceContainer_RequiresPairingElevation;
                                                                        printf("DEVPKEY_DeviceContainer_RequiresPairingElevation: \n");
                                                                        break;
                                                                    case 169:
                                                                        devproperty = &DEVPKEY_DeviceContainer_ExperienceId;
                                                                        printf("DEVPKEY_DeviceContainer_ExperienceId: \n");
                                                                        break;
                                                                    case 170:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Category;
                                                                        printf("DEVPKEY_DeviceContainer_Category: \n");
                                                                        break;
                                                                    case 171:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Category_Desc_Singular;
                                                                        printf("DEVPKEY_DeviceContainer_Category_Desc_Singular: \n");
                                                                        break;
                                                                    case 172:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Category_Desc_Plural;
                                                                        printf("DEVPKEY_DeviceContainer_Category_Desc_Plural: \n");
                                                                        break;
                                                                    case 173:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Category_Icon;
                                                                        printf("DEVPKEY_DeviceContainer_Category_Icon: \n");
                                                                        break;
                                                                    case 174:
                                                                        devproperty = &DEVPKEY_DeviceContainer_CategoryGroup_Desc;
                                                                        printf("DEVPKEY_DeviceContainer_CategoryGroup_Desc: \n");
                                                                        break;
                                                                    case 175:
                                                                        devproperty = &DEVPKEY_DeviceContainer_CategoryGroup_Icon;
                                                                        printf("DEVPKEY_DeviceContainer_CategoryGroup_Icon: \n");
                                                                        break;
                                                                    case 176:
                                                                        devproperty = &DEVPKEY_DeviceContainer_PrimaryCategory;
                                                                        printf("DEVPKEY_DeviceContainer_PrimaryCategory: \n");
                                                                        break;
                                                                    case 178:
                                                                        devproperty = &DEVPKEY_DeviceContainer_UnpairUninstall;
                                                                        printf("DEVPKEY_DeviceContainer_UnpairUninstall: \n");
                                                                        break;
                                                                    case 179:
                                                                        devproperty = &DEVPKEY_DeviceContainer_RequiresUninstallElevation;
                                                                        printf("DEVPKEY_DeviceContainer_RequiresUninstallElevation: \n");
                                                                        break;
                                                                    case 180:
                                                                        devproperty = &DEVPKEY_DeviceContainer_DeviceFunctionSubRank;
                                                                        printf("DEVPKEY_DeviceContainer_DeviceFunctionSubRank: \n");
                                                                        break;
                                                                    case 181:
                                                                        devproperty = &DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected;
                                                                        printf("DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected: \n");
                                                                        break;
                                                                    case 182:
                                                                        devproperty = &DEVPKEY_DeviceContainer_ConfigFlags;
                                                                        printf("DEVPKEY_DeviceContainer_ConfigFlags: \n");
                                                                        break;
                                                                    case 183:
                                                                        devproperty = &DEVPKEY_DeviceContainer_PrivilegedPackageFamilyNames;
                                                                        printf("DEVPKEY_DeviceContainer_PrivilegedPackageFamilyNames: \n");
                                                                        break;
                                                                    case 184:
                                                                        devproperty = &DEVPKEY_DeviceContainer_CustomPrivilegedPackageFamilyNames;
                                                                        printf("DEVPKEY_DeviceContainer_CustomPrivilegedPackageFamilyNames: \n");
                                                                        break;
                                                                    case 185:
                                                                        devproperty = &DEVPKEY_DeviceContainer_IsRebootRequired;
                                                                        printf("DEVPKEY_DeviceContainer_IsRebootRequired: \n");
                                                                        break;
                                                                    case 186:
                                                                        devproperty = &DEVPKEY_DeviceContainer_FriendlyName;
                                                                        printf("DEVPKEY_DeviceContainer_FriendlyName: \n");
                                                                        break;
                                                                    case 187:
                                                                        devproperty = &DEVPKEY_DeviceContainer_Manufacturer;
                                                                        printf("DEVPKEY_DeviceContainer_Manufacturer: \n");
                                                                        break;
                                                                    case 188:
                                                                        devproperty = &DEVPKEY_DeviceContainer_ModelName;
                                                                        printf("DEVPKEY_DeviceContainer_ModelName: \n");
                                                                        break;
                                                                    case 189:
                                                                        devproperty = &DEVPKEY_DeviceContainer_ModelNumber;
                                                                        printf("DEVPKEY_DeviceContainer_ModelNumber: \n");
                                                                        break;
                                                                    case 190:
                                                                        devproperty = &DEVPKEY_DeviceContainer_InstallInProgress;
                                                                        printf("DEVPKEY_DeviceContainer_InstallInProgress: \n");
                                                                        break;
                                                                    case 191:
                                                                        devproperty = &DEVPKEY_DevQuery_ObjectType;
                                                                        printf("DEVPKEY_DevQuery_ObjectType: \n");
                                                                        break;
                                                                    default:
                                                                        devproperty = NULL;
                                                                        break;
                                                                    }
                                                                    propertyBufLen = 0;
                                                                    cmRet = CM_Get_DevNode_PropertyW(propInst, devproperty, &propertyType, NULL, &propertyBufLen, 0);
                                                                    if (CR_SUCCESS == cmRet || CR_INVALID_POINTER == cmRet || CR_BUFFER_SMALL == cmRet)//We'll probably get an invalid pointer or small buffer, but this will return the size of the buffer we need, so allow it through - TJE
                                                                    {
                                                                        PBYTE propertyBuf = (PBYTE)calloc(propertyBufLen + 1, sizeof(BYTE));
                                                                        if (propertyBuf)
                                                                        {
                                                                            propertyBufLen += 1;
                                                                            cmRet = CM_Get_DevNode_PropertyW(propInst, devproperty, &propertyType, propertyBuf, &propertyBufLen, 0);
                                                                            if (CR_SUCCESS == cmRet)
                                                                            {
                                                                                switch (propertyType)
                                                                                {
                                                                                case DEVPROP_TYPE_STRING:
                                                                                    // Fall-through //
                                                                                case DEVPROP_TYPE_STRING_LIST:
                                                                                    //setup to handle multiple strings
                                                                                    for (LPWSTR property = (LPWSTR)propertyBuf; *property; property += wcslen(property) + 1)
                                                                                    {
                                                                                        if (property && ((uintptr_t)property - (uintptr_t)propertyBuf) < propertyBufLen && wcslen(property))
                                                                                        {
                                                                                            wprintf(L"\t%s\n", property);
                                                                                        }
                                                                                    }
                                                                                    break;
                                                                                case DEVPROP_TYPE_SBYTE://8bit signed byte
                                                                                {
                                                                                    char *signedByte = (char*)propertyBuf;
                                                                                    printf("\t%" PRId8 "\n", *signedByte);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_BYTE:
                                                                                {
                                                                                    BYTE *unsignedByte = (BYTE*)propertyBuf;
                                                                                    printf("\t%" PRIu8 "\n", *unsignedByte);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_INT16:
                                                                                {
                                                                                    INT16 *signed16 = (INT16*)propertyBuf;
                                                                                    printf("\t%" PRId16 "\n", *signed16);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_UINT16:
                                                                                {
                                                                                    UINT16 *unsigned16 = (UINT16*)propertyBuf;
                                                                                    printf("\t%" PRIu16 "\n", *unsigned16);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_INT32:
                                                                                {
                                                                                    INT32 *signed32 = (INT32*)propertyBuf;
                                                                                    printf("\t%" PRId32 "\n", *signed32);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_UINT32:
                                                                                {
                                                                                    UINT32 *unsigned32 = (UINT32*)propertyBuf;
                                                                                    printf("\t%" PRIu32 "\n", *unsigned32);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_INT64:
                                                                                {
                                                                                    INT64 *signed64 = (INT64*)propertyBuf;
                                                                                    printf("\t%" PRId64 "\n", *signed64);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_UINT64:
                                                                                {
                                                                                    UINT64 *unsigned64 = (UINT64*)propertyBuf;
                                                                                    printf("\t%" PRIu64 "\n", *unsigned64);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_FLOAT:
                                                                                {
                                                                                    FLOAT *theFloat = (FLOAT*)propertyBuf;
                                                                                    printf("\t%f\n", *theFloat);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_DOUBLE:
                                                                                {
                                                                                    DOUBLE *theFloat = (DOUBLE*)propertyBuf;
                                                                                    printf("\t%f\n", *theFloat);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_BOOLEAN:
                                                                                {
                                                                                    BOOLEAN *theBool = (BOOLEAN*)propertyBuf;
                                                                                    if (*theBool == DEVPROP_FALSE)
                                                                                    {
                                                                                        printf("\tFALSE\n");
                                                                                    }
                                                                                    else //if (*theBool == DEVPROP_TRUE)
                                                                                    {
                                                                                        printf("\tTRUE\n");
                                                                                    }
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_ERROR://win32 error code
                                                                                {
                                                                                    DWORD *win32Error = (DWORD*)propertyBuf;
                                                                                    print_Windows_Error_To_Screen(*win32Error);
                                                                                }
                                                                                break;
                                                                                case DEVPROP_TYPE_DECIMAL://128bit decimal
                                                                                case DEVPROP_TYPE_GUID://128bit guid
                                                                                case DEVPROP_TYPE_CURRENCY:
                                                                                case DEVPROP_TYPE_DATE:
                                                                                case DEVPROP_TYPE_FILETIME:
                                                                                case DEVPROP_TYPE_SECURITY_DESCRIPTOR:
                                                                                case DEVPROP_TYPE_SECURITY_DESCRIPTOR_STRING:
                                                                                case DEVPROP_TYPE_DEVPROPKEY:
                                                                                case DEVPROP_TYPE_DEVPROPTYPE:
                                                                                case DEVPROP_TYPE_BINARY://custom binary data
                                                                                case DEVPROP_TYPE_NTSTATUS://NTSTATUS code
                                                                                case DEVPROP_TYPE_STRING_INDIRECT:
                                                                                default:
                                                                                    print_Data_Buffer(propertyBuf, propertyBufLen, true);
                                                                                    break;
                                                                                }
                                                                            }
                                                                        }
                                                                        safe_Free(propertyBuf);
                                                                    }
                                                                    //else
                                                                    //{
                                                                    //    printf("\tUnable to find requested property\n");
                                                                    //}
                                                                    ++counter;
                                                                }
                                                                ++instanceCounter;
                                                                counter = 0;
                                                                //change to parent instance
                                                                propInst = parentInst;
                                                                //reset to beginning of properties
                                                                devproperty = &DEVPKEY_NAME;
                                                                propertyType = 0;
                                                                propertyBufLen = 0;
                                                            }
                                                        }
                                                        /*/
                                                        //*/

                                                        //here is where we need to parse the USB VID/PID or TODO: PCI Vendor, Product, and Revision numbers
                                                        if (_tcsncmp(TEXT("USB"), parentBuffer, _tcsclen(TEXT("USB"))) == 0)
                                                        {
                                                            ULONG propertyBufLen = 0;
                                                            DEVPROPTYPE propertyType = 0;
                                                            int scannedVals =_sntscanf_s(parentBuffer, parentLen, TEXT("USB\\VID_%") TEXT(SCNx32) TEXT("&PID_%") TEXT(SCNx32) TEXT("\\%*s"), &device->drive_info.adapter_info.vendorID, &device->drive_info.adapter_info.productID);
                                                            device->drive_info.adapter_info.vendorIDValid = true;
                                                            device->drive_info.adapter_info.productIDValid = true;
                                                            if (scannedVals < 2)
                                                            {
#if (_DEBUG)
                                                                printf("Could not scan all values. Scanned %d values\n", scannedVals);
#endif
                                                            }
                                                            device->drive_info.adapter_info.infoType = ADAPTER_INFO_USB;
                                                            //unfortunately, this device ID doesn't have a revision in it for USB.
                                                            //We can do this other property request to read it, but it's wide characters only. No TCHARs allowed.
                                                            cmRet = CM_Get_DevNode_PropertyW(parentInst, &DEVPKEY_Device_HardwareIds, &propertyType, NULL, &propertyBufLen, 0);
                                                            if (CR_SUCCESS == cmRet || CR_INVALID_POINTER == cmRet || CR_BUFFER_SMALL == cmRet)//We'll probably get an invalid pointer or small buffer, but this will return the size of the buffer we need, so allow it through - TJE
                                                            {
                                                                PBYTE propertyBuf = (PBYTE)calloc(propertyBufLen + 1, sizeof(BYTE));
                                                                if (propertyBuf)
                                                                {
                                                                    propertyBufLen += 1;
                                                                    //NOTE: This key contains all 3 parts, VID, PID, and REV from the "parentInst": Example: USB\VID_174C&PID_2362&REV_0100
                                                                    if (CR_SUCCESS == CM_Get_DevNode_PropertyW(parentInst, &DEVPKEY_Device_HardwareIds, &propertyType, propertyBuf, &propertyBufLen, 0))
                                                                    {
                                                                        //multiple strings can be returned.
                                                                        for (LPWSTR property = (LPWSTR)propertyBuf; *property; property += wcslen(property) + 1)
                                                                        {
                                                                            if (property && ((uintptr_t)property - (uintptr_t)propertyBuf) < propertyBufLen && wcslen(property))
                                                                            {
                                                                                LPWSTR revisionStr = wcsstr(property, L"REV_");
                                                                                if (revisionStr)
                                                                                {
                                                                                    if (1 == swscanf(revisionStr, L"REV_%x", &device->drive_info.adapter_info.revision))
                                                                                    {
                                                                                        device->drive_info.adapter_info.revisionValid = true;
                                                                                        break;
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                                safe_Free(propertyBuf);
                                                            }
                                                        }
                                                        else if (_tcsncmp(TEXT("PCI"), parentBuffer, _tcsclen(TEXT("PCI"))) == 0)
                                                        {
                                                            uint32_t subsystem = 0;
                                                            uint8_t revision = 0;
                                                            int scannedVals = _sntscanf_s(parentBuffer, parentLen, TEXT("PCI\\VEN_%") TEXT(SCNx32) TEXT("&DEV_%") TEXT(SCNx32) TEXT("&SUBSYS_%") TEXT(SCNx32) TEXT("&REV_%") TEXT(SCNx8) TEXT("\\%*s"), &device->drive_info.adapter_info.vendorID, &device->drive_info.adapter_info.productID, &subsystem, &revision);
                                                            device->drive_info.adapter_info.vendorIDValid = true;
                                                            device->drive_info.adapter_info.productIDValid = true;
                                                            device->drive_info.adapter_info.revision = revision;
                                                            device->drive_info.adapter_info.revisionValid = true;
                                                            device->drive_info.adapter_info.infoType = ADAPTER_INFO_PCI;
                                                            if (scannedVals < 4)
                                                            {
#if (_DEBUG)
                                                                printf("Could not scan all values. Scanned %d values\n", scannedVals);
#endif
                                                            }
                                                            //can also read DEVPKEY_Device_HardwareIds for parentInst to get all this data
                                                        }
                                                        else if (_tcsncmp(TEXT("1394"), parentBuffer, _tcsclen(TEXT("1394"))) == 0)
                                                        {
                                                            //Parent buffer already contains the vendor ID as part of the buffer where a full WWN is reported...we just need first 6 bytes. example, brackets added for clarity: 1394\Maxtor&5000DV__v1.00.00\[0010B9]20003D9D6E
                                                            //DEVPKEY_Device_CompatibleIds gets use 1394 specifier ID for the device instance
                                                            //DEVPKEY_Device_CompatibleIds gets revision and specifier ID for the parent instance: 1394\<specifier>&<revision>
                                                            //DEVPKEY_Device_ConfigurationId gets revision and specifier ID for parent instance: sbp2.inf:1394\609E&10483,sbp2_install
                                                            //NOTE: There is no currently known way to get the product ID for this interface
                                                            ULONG propertyBufLen = 0;
                                                            DEVPROPTYPE propertyType = 0;
                                                            const DEVPROPKEY *propertyKey = &DEVPKEY_Device_CompatibleIds;
                                                            //scan parentBuffer to get vendor
                                                            TCHAR *nextToken = NULL;
                                                            TCHAR *token = _tcstok_s(parentBuffer, TEXT("\\"), &nextToken);
                                                            while (token && nextToken &&  _tcsclen(nextToken) > 0)
                                                            {
                                                                token = _tcstok_s(NULL, TEXT("\\"), &nextToken);
                                                            }
                                                            if (token)
                                                            {
                                                                //at this point, the token contains only the part we care about reading
                                                                //We need the first 6 characters to convert into hex for the vendor ID
                                                                TCHAR vendorIDString[7] = { 0 };
                                                                _tcsncpy_s(vendorIDString, 7 * sizeof(TCHAR), token, 6);
                                                                _tprintf(TEXT("%s\n"), vendorIDString);
                                                                int result = _stscanf(token, TEXT("%06") TEXT(SCNx32), &device->drive_info.adapter_info.vendorID);
                                                                if (result == 1)
                                                                {
                                                                    device->drive_info.adapter_info.vendorIDValid = true;
                                                                }
                                                            }

                                                            device->drive_info.adapter_info.infoType = ADAPTER_INFO_IEEE1394;
                                                            cmRet = CM_Get_DevNode_PropertyW(parentInst, propertyKey, &propertyType, NULL, &propertyBufLen, 0);
                                                            if (CR_SUCCESS == cmRet || CR_INVALID_POINTER == cmRet || CR_BUFFER_SMALL == cmRet)//We'll probably get an invalid pointer or small buffer, but this will return the size of the buffer we need, so allow it through - TJE
                                                            {
                                                                PBYTE propertyBuf = (PBYTE)calloc(propertyBufLen + 1, sizeof(BYTE));
                                                                if (propertyBuf)
                                                                {
                                                                    propertyBufLen += 1;
                                                                    if (CR_SUCCESS == CM_Get_DevNode_PropertyW(parentInst, propertyKey, &propertyType, propertyBuf, &propertyBufLen, 0))
                                                                    {
                                                                        //multiple strings can be returned for some properties. This one will most likely only return one.
                                                                        for (LPWSTR property = (LPWSTR)propertyBuf; *property; property += wcslen(property) + 1)
                                                                        {
                                                                            if (property && ((uintptr_t)property - (uintptr_t)propertyBuf) < propertyBufLen && wcslen(property))
                                                                            {
                                                                                int scannedVals = _snwscanf_s((const wchar_t*)propertyBuf, propertyBufLen, L"1394\\%x&%x", &device->drive_info.adapter_info.specifierID, &device->drive_info.adapter_info.revision);
                                                                                if (scannedVals < 2)
                                                                                {
#if (_DEBUG)
                                                                                    printf("Could not scan all values. Scanned %d values\n", scannedVals);
#endif
                                                                                }
                                                                                else
                                                                                {
                                                                                    device->drive_info.adapter_info.specifierIDValid = true;
                                                                                    device->drive_info.adapter_info.revisionValid = true;
                                                                                    break;
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                                safe_Free(propertyBuf);
                                                            }
                                                        }
                                                    }
                                                    safe_Free(parentBuffer);
                                                }
                                            }
                                            else
                                            {
                                                //for some reason, we matched, but didn't find a matching drive number. Keep going through the list and trying to figure it out!
                                                foundMatch = false;
                                            }
                                        }
                                    }
                                    CloseHandle(deviceHandle);
                                }
                            }
                        }
                        safe_Free(interfaceList);
                    }
                }//else node is not available most likely...possibly not attached to the system.
            }
            if (foundMatch)
            {
                ret = SUCCESS;
            }
        }
        safe_Free(listBuffer);
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
#if WINVER >= SEA_WIN32_WINNT_WINBLUE
int send_Win_NVMe_Firmware_Activate_Miniport_Command(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    PSRB_IO_CONTROL         srbControl;
    PFIRMWARE_REQUEST_BLOCK firmwareRequest;
    PUCHAR                  buffer = NULL;
    ULONG                   bufferSize;
    ULONG                   firmwareStructureOffset;
    PSTORAGE_FIRMWARE_ACTIVATE  firmwareActivate;
#if defined (_DEBUG)
    printf("%s: -->\n", __FUNCTION__);
    printf("%s: Slot %" PRIu8 "\n", __FUNCTION__, (uint8_t)M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0));
#endif

    //
    // The STORAGE_FIRMWARE_INFO is located after SRB_IO_CONTROL and FIRMWARE_RESQUEST_BLOCK
    //
    firmwareStructureOffset = ((sizeof(SRB_IO_CONTROL) + \
        sizeof(FIRMWARE_REQUEST_BLOCK) - 1) / sizeof(PVOID) + 1) * sizeof(PVOID);
    bufferSize = 4096; //Since Panther Max xfer is 4k
    bufferSize += firmwareStructureOffset;
    bufferSize += FIELD_OFFSET(STORAGE_FIRMWARE_DOWNLOAD, ImageBuffer);

    buffer = (PUCHAR)calloc_aligned(bufferSize, sizeof(UCHAR), nvmeIoCtx->device->os_info.minimumAlignment);
    if (!buffer)
    {
        return MEMORY_FAILURE;
    }

    srbControl = (PSRB_IO_CONTROL)buffer;
    srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
    srbControl->ControlCode = IOCTL_SCSI_MINIPORT_FIRMWARE;
    RtlMoveMemory(srbControl->Signature, IOCTL_MINIPORT_SIGNATURE_FIRMWARE, 8);
    srbControl->Timeout = 30;
    srbControl->Length = bufferSize - sizeof(SRB_IO_CONTROL);

    firmwareRequest = (PFIRMWARE_REQUEST_BLOCK)(srbControl + 1);
    firmwareRequest->Version = FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION;
    firmwareRequest->Size = sizeof(FIRMWARE_REQUEST_BLOCK);
    firmwareRequest->Function = FIRMWARE_FUNCTION_ACTIVATE;
    firmwareRequest->Flags = FIRMWARE_REQUEST_FLAG_CONTROLLER;
    firmwareRequest->DataBufferOffset = firmwareStructureOffset;
    firmwareRequest->DataBufferLength = bufferSize - firmwareStructureOffset;

    firmwareActivate = (PSTORAGE_FIRMWARE_ACTIVATE)((PUCHAR)srbControl + firmwareRequest->DataBufferOffset);
    firmwareActivate->Version = 1;
    firmwareActivate->Size = sizeof(STORAGE_FIRMWARE_ACTIVATE);
    firmwareActivate->SlotToActivate = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0);

    DWORD returned_data = 0;
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    start_Timer(&commandTimer);
    //
    // Send the activation request
    //
    //success = DeviceIoControl(IoContext.hHandle,
    int fwdlIO = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_SCSI_MINIPORT,
        buffer,
        bufferSize,
        buffer,
        bufferSize,
        &returned_data,
        &overlappedStruct
    );
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == nvmeIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
#if defined (_DEBUG)
    printf("%s: nvmeIoCtx->device->os_info.last_error=%d(0x%x)\n", \
        __FUNCTION__, nvmeIoCtx->device->os_info.last_error, nvmeIoCtx->device->os_info.last_error);
#endif
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    //dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        //TODO: We need to figure out what error codes Windows will return and how to dummy up the return value to match - TJE
        switch (nvmeIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE:
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }
    safe_Free_aligned(buffer);
#if defined (_DEBUG)
    printf("%s: <-- (ret=%d)\n", __FUNCTION__, ret);
#endif
    return ret;

}
#endif //WINVER
#endif //DISABLE_NVME_PASSTHROUGH

int get_os_drive_number( char *filename )
{
    int  drive_num = -1;
    char *pdev     = NULL;
    //char * next_token = NULL;
    pdev = strrchr(filename, 'e');
    if (pdev != NULL)
        drive_num = atoi(pdev + 1);
    return drive_num;
}

//-----------------------------------------------------------------------------
//
//  close_Device()
//
//! \brief   Description:  Given a device, close it's handle.
//
//  Entry:
//!   \param[in] device = device stuct that holds device information.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int close_Device(tDevice *dev)
{
    int retValue = 0;
    if (dev)
    {
#if defined (ENABLE_CSMI)
        if (strncmp(dev->os_info.name, "\\\\.\\SCSI", 8) == 0)
        {
            return close_CSMI_Device(dev);
        }
#endif
        retValue = CloseHandle(dev->os_info.fd);
        dev->os_info.last_error = GetLastError();
        if ( retValue )
        {
            dev->os_info.fd = INVALID_HANDLE_VALUE;
            return SUCCESS;
        }
        else
        {
            return FAILURE;
        }
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int get_Device(const char *filename, tDevice *device )
{
#if defined (ENABLE_CSMI)
    //check is the handle is in the format of a CSMI device handle so we can open the csmi device properly.
    uint32_t port = 0;
    uint32_t phy = 0;
    int isCSMIHandle = sscanf_s(filename, "\\\\.\\SCSI%" SCNu32 ":%" SCNu32, &port, &phy);
    if (isCSMIHandle != EOF && isCSMIHandle == 2)
    {
        return get_CSMI_Device(filename, device);
    }
#endif
    int                         ret           = FAILURE;
    int                         win_ret       = 0;
    ULONG                       returned_data = 0;
    PSTORAGE_DEVICE_DESCRIPTOR  device_desc   = NULL;
    PSTORAGE_ADAPTER_DESCRIPTOR adapter_desc  = NULL;
    STORAGE_PROPERTY_QUERY      query;
    STORAGE_DESCRIPTOR_HEADER   header;

#if defined UNICODE
    WCHAR device_name[80] = { 0 };
    LPCWSTR ptrDeviceName = &device_name[0];
    mbstowcs_s(NULL, device_name, strlen(filename) + 1, filename, _TRUNCATE); //Plus null
#else
    char device_name[40] = { 0 };
    LPCSTR ptrDeviceName = &device_name[0];
    strcpy(&device_name[0], filename);
#endif

    //printf("%s -->\n Opening Device %s\n",__FUNCTION__, filename);
    if (!(validate_Device_Struct(device->sanity)))
        return LIBRARY_MISMATCH;

    //lets try to open the device.
    device->os_info.fd = CreateFile(ptrDeviceName,
                                    /* We are reverting to the GENERIC_WRITE | GENERIC_READ because
                                       in the use case of a dll where multiple applications are using
                                       our library, this needs to not request full access. If you suspect
                                       some commands might fail (e.g. ISE/SED because of that
                                       please write to developers  -MA */
                                    GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL,
                                    OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                    FILE_FLAG_OVERLAPPED,
#else
                                    0,
#endif
                                    NULL);

    device->os_info.last_error = GetLastError();

    // Check if we get a invalid handle back.
    if (device->os_info.fd == INVALID_HANDLE_VALUE)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Error: opening dev %s. Error: %"PRId32"\n", filename, device->os_info.last_error);
        }
        ret = FAILURE;
    }
    else
    {
        //set the handle name
        strncpy_s(device->os_info.name, 30, filename, 30);

        if (strstr(device->os_info.name, "Physical"))
        {
            uint32_t drive = UINT32_MAX;
            sscanf_s(device->os_info.name, "\\\\.\\PhysicalDrive%" SCNu32, &drive);
            sprintf(device->os_info.friendlyName, "PD%" PRIu32, drive);
            device->os_info.os_drive_number = drive;
        }
        else if (strstr(device->os_info.name, "CDROM"))
        {
            uint32_t drive = UINT32_MAX;
            sscanf_s(device->os_info.name, "\\\\.\\CDROM%" SCNu32, &drive);
            sprintf(device->os_info.friendlyName, "CDROM%" PRIu32, drive);
            device->os_info.os_drive_number = drive;
        }
        else if (strstr(device->os_info.name, "Tape"))
        {
            uint32_t drive = UINT32_MAX;
            sscanf_s(device->os_info.name, "\\\\.\\Tape%" SCNu32, &drive);
            sprintf(device->os_info.friendlyName, "TAPE%" PRIu32, drive);
            device->os_info.os_drive_number = drive;
        }
        else if (strstr(device->os_info.name, "Changer"))
        {
            uint32_t drive = UINT32_MAX;
            sscanf_s(device->os_info.name, "\\\\.\\Changer%" SCNu32, &drive);
            sprintf(device->os_info.friendlyName, "CHGR%" PRIu32, drive);
            device->os_info.os_drive_number = drive;
        }

        //map the drive to a volume letter
        DWORD driveLetters = 0;
        char currentLetter = 'A';
        driveLetters = GetLogicalDrives();
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;//Setting this since we have code here to detect the volumes in the OS
        bool foundVolumeLetter = false;
        while (driveLetters > 0 && !foundVolumeLetter)
        {
            if (driveLetters & BIT0)
            {
                //a volume with this letter exists...check it's physical device number
#if defined UNICODE
                WCHAR device_name[80] = { 0 };
                LPWSTR ptrLetterName = &device_name[0];
                swprintf(ptrLetterName, 80, L"\\\\.\\%c:", currentLetter);
                HANDLE letterHandle = CreateFile((LPCWSTR)ptrLetterName,
#else
                char device_name[40] = { 0 };
                LPSTR ptrLetterName = &device_name[0];
                snprintf(ptrLetterName, 40, "\\\\.\\%c:", currentLetter);
                HANDLE letterHandle = CreateFile((LPCSTR)ptrLetterName,
#endif
                    GENERIC_WRITE | GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
//#if !defined(WINDOWS_DISABLE_OVERLAPPED)
//                    FILE_FLAG_OVERLAPPED,
//#else
                    0,
//#endif
                    NULL);
                if (letterHandle != INVALID_HANDLE_VALUE)
                {
                    DWORD returnedBytes = 0;
                    DWORD maxExtents = 32;//https://technet.microsoft.com/en-us/library/cc772180(v=ws.11).aspx
                    PVOLUME_DISK_EXTENTS diskExtents = NULL;
                    DWORD diskExtentsSizeBytes = sizeof(VOLUME_DISK_EXTENTS) + (sizeof(DISK_EXTENT) * maxExtents);
                    diskExtents = (PVOLUME_DISK_EXTENTS)malloc(diskExtentsSizeBytes);
                    if (diskExtents)
                    {
                        if (DeviceIoControl(letterHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, diskExtents, diskExtentsSizeBytes, &returnedBytes, NULL))
                        {
                            for (DWORD counter = 0; counter < diskExtents->NumberOfDiskExtents; ++counter)
                            {
                                if (diskExtents->Extents[counter].DiskNumber == device->os_info.os_drive_number)
                                {
                                    foundVolumeLetter = true;
                                    device->os_info.fileSystemInfo.hasFileSystem = true;//We found a filesystem for this drive, so set this to true.
                                    //now we need to determine if this volume has the system directory on it.
                                    CHAR systemDirectoryPath[4096] = { 0 };//4096 SHOULD be plenty...

                                    if (GetSystemDirectoryA(systemDirectoryPath, 4096) > 0)
                                    {
                                        if (strlen(systemDirectoryPath) > 0)
                                        {
                                            //we need to check only the first letter of the returned string since this is the volume letter
                                            if (systemDirectoryPath[0] == currentLetter)
                                            {
                                                //This volume contains a system directory
                                                device->os_info.fileSystemInfo.isSystemDisk = true;
                                            }
                                        }
                                        #if defined (_DEBUG)
                                        else
                                        {
                                            printf("\nWARNING! Asked for system directory, but got a zero length string! Unable to detect if this is a drive with a system folder!\n");
                                        }
                                        #endif
                                    }
                                    break;
                                }
                            }
                        }
                        //DWORD lastError = GetLastError();
                        safe_Free(diskExtents);
                    }
                }
                CloseHandle(letterHandle);
            }
            driveLetters = driveLetters >> 1;//shift the bits by 1 and we will go onto the next drive letter
            ++currentLetter;//increment the letter
        }

        //set the OS Type
        device->os_info.osType = OS_WINDOWS;

        // Lets get the SCSI address
        win_ret = DeviceIoControl(device->os_info.fd,
                                  IOCTL_SCSI_GET_ADDRESS,
                                  NULL,
                                  0,
                                  &device->os_info.scsi_addr,
                                  sizeof(device->os_info.scsi_addr),
                                  &returned_data,
                                  FALSE);  //returns 0 on failure
        if (win_ret == 0)
        {
            device->os_info.scsi_addr.PortNumber = 0xFF;
            device->os_info.scsi_addr.PathId = 0xFF;
            device->os_info.scsi_addr.TargetId = 0xFF;
            device->os_info.scsi_addr.Lun = 0xFF;
        }
        // Lets get some properties.
        memset(&query, 0, sizeof(STORAGE_PROPERTY_QUERY));
        memset(&header, 0, sizeof(STORAGE_DESCRIPTOR_HEADER));

        query.QueryType = PropertyStandardQuery;
        query.PropertyId = StorageAdapterProperty;

        win_ret = DeviceIoControl(device->os_info.fd,
                                  IOCTL_STORAGE_QUERY_PROPERTY,
                                  &query,
                                  sizeof(STORAGE_PROPERTY_QUERY),
                                  &header,
                                  sizeof(STORAGE_DESCRIPTOR_HEADER),
                                  &returned_data,
                                  FALSE);
        if ((win_ret > 0) && (header.Size != 0))
        {
            adapter_desc = (PSTORAGE_ADAPTER_DESCRIPTOR)LocalAlloc(LPTR, header.Size);
            if (adapter_desc != NULL)
            {
                win_ret = DeviceIoControl(device->os_info.fd,
                                          IOCTL_STORAGE_QUERY_PROPERTY,
                                          &query,
                                          sizeof(STORAGE_PROPERTY_QUERY),
                                          adapter_desc,
                                          header.Size,
                                          &returned_data,
                                          FALSE);

                if (win_ret > 0)
                {
                    // TODO: Copy any of the adapter stuff.
                    #if defined (_DEBUG)
                    printf("Adapter BusType: ");
                    print_bus_type(adapter_desc->BusType);
                    printf(" \n");
                    #endif
                    //saving max transfer size (in bytes)
                    device->os_info.adapterMaxTransferSize = adapter_desc->MaximumTransferLength;

                    //saving the SRB type so that we know when an adapter supports the new SCSI Passthrough EX IOCTLS - TJE
#if WINVER >= SEA_WIN32_WINNT_WIN8 //If this check is wrong, make sure minGW is properly defining WINVER in the makefile.
                    if (is_Windows_8_Or_Higher())//from opensea-common now to remove versionhelpes.h include
                    {
                        device->os_info.srbtype = adapter_desc->SrbType;
                    }
                    else
#endif
                    {
                        device->os_info.srbtype = SRB_TYPE_SCSI_REQUEST_BLOCK;
                    }
                    switch (adapter_desc->AlignmentMask)
                    {
                    case 0://byte
                        device->os_info.minimumAlignment = 1;
                        break;
                    case 1://word
                        device->os_info.minimumAlignment = 2;
                        break;
                    case 3://dword
                        device->os_info.minimumAlignment = 4;
                        break;
                    case 7://qword
                        device->os_info.minimumAlignment = 8;
                        break;
                    default:
                        device->os_info.minimumAlignment = 0;
                        break;
                    }
                    device->os_info.alignmentMask = adapter_desc->AlignmentMask;//may be needed later....currently unused
                    // Now lets get device stuff
                    query.PropertyId = StorageDeviceProperty;
                    memset(&header, 0, sizeof(STORAGE_DESCRIPTOR_HEADER));
                    win_ret = DeviceIoControl(device->os_info.fd,
                                          IOCTL_STORAGE_QUERY_PROPERTY,
                                          &query,
                                          sizeof(STORAGE_PROPERTY_QUERY),
                                          &header,
                                          sizeof(STORAGE_DESCRIPTOR_HEADER),
                                          &returned_data,
                                          FALSE);

                    if ((win_ret > 0) && (header.Size != 0))
                    {
                        device_desc = (PSTORAGE_DEVICE_DESCRIPTOR)LocalAlloc(LPTR, header.Size);
                        if (device_desc != NULL)
                        {
                            win_ret = DeviceIoControl(device->os_info.fd,
                                                  IOCTL_STORAGE_QUERY_PROPERTY,
                                                  &query,
                                                  sizeof(STORAGE_PROPERTY_QUERY),
                                                  device_desc,
                                                  header.Size,
                                                  &returned_data,
                                                  FALSE);
                            if (win_ret > 0)
                            {

                                get_Adapter_IDs(device, device_desc, header.Size);

#if WINVER >= SEA_WIN32_WINNT_WIN10
                                get_Windows_FWDL_IO_Support(device, device_desc->BusType);
#else
                                device->os_info.fwdlIOsupport.fwdlIOSupported = false;//this API is not available before Windows 10
#endif
                                //#if defined (_DEBUG)
                                //printf("Drive BusType: ");
                                //print_bus_type(device_desc->BusType);
                                //printf(" \n");
                                //#endif

                                if ((adapter_desc->BusType == BusTypeAta) ||
                                    (device_desc->BusType == BusTypeAta)
                                    )
                                {
                                    device->drive_info.drive_type = ATA_DRIVE;
                                    device->drive_info.interface_type = IDE_INTERFACE;
                                    device->os_info.ioType = WIN_IOCTL_ATA_PASSTHROUGH;
                                    get_Windows_SMART_IO_Support(device);//might be used later
                                }
                                else if ((adapter_desc->BusType == BusTypeAtapi) ||
                                    (device_desc->BusType == BusTypeAtapi)
                                    )
                                {
                                    device->drive_info.drive_type = ATAPI_DRIVE;
                                    device->drive_info.interface_type = IDE_INTERFACE;
                                    device->drive_info.passThroughHacks.someHacksSetByOSDiscovery = true;
                                    device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//This is GENERALLY true because almost all times the CDB is passed through instead of translated for a passthrough command, so just block it no matter what.
                                    //TODO: These devices use the SCSI MMC command set in packet commands over ATA...other than for a few other commands.
                                    //If we care to properly support this, we should investigate either how to send a packet command, or we should try issuing only SCSI commands
                                    device->os_info.ioType = WIN_IOCTL_ATA_PASSTHROUGH;
                                    get_Windows_SMART_IO_Support(device);//might be used later
                                }
                                else if ((device_desc->BusType == BusTypeSata))
                                {
                                    if (strncmp(WIN_CDROM_DRIVE, filename, strlen(WIN_CDROM_DRIVE)) == 0)
                                    {
                                        device->drive_info.drive_type = ATAPI_DRIVE;
                                        device->drive_info.passThroughHacks.someHacksSetByOSDiscovery = true;
                                        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//This is GENERALLY true because almost all times the CDB is passed through instead of translated for a passthrough command, so just block it no matter what.
                                    }
                                    else
                                    {
                                        device->drive_info.drive_type = ATA_DRIVE;
                                    }
                                    //we are assuming, for now, that SAT translation is being done below, and so far through testing on a few chipsets this appears to be correct.
                                    device->drive_info.interface_type = IDE_INTERFACE;
                                    device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
                                    get_Windows_SMART_IO_Support(device);//might be used later
                                }
                                else if (device_desc->BusType == BusTypeUsb)
                                {
                                    //set this to SCSI_DRIVE. The fill_Drive_Info_Data call will change this if it supports SAT
                                    device->drive_info.drive_type = SCSI_DRIVE;
                                    device->drive_info.interface_type = USB_INTERFACE;
                                    device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
                                }
                                else if (device_desc->BusType == BusType1394)
                                {
                                    //set this to SCSI_DRIVE. The fill_Drive_Info_Data call will change this if it supports SAT
                                    device->drive_info.drive_type = SCSI_DRIVE;
                                    device->drive_info.interface_type = IEEE_1394_INTERFACE;
                                    device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
                                }
                                //NVMe bustype can be defined for Win7 with openfabrics nvme driver, so make sure we can handle it...it shows as a SCSI device on this interface unless you use a SCSI?: handle with the IOCTL directly to the driver.
                                else if (device_desc->BusType == BusTypeNvme && device_desc->VendorIdOffset == 0)//Open fabrics will set a vendorIDoffset, MSFT driver will not.
                                {
#if WINVER >= SEA_WIN32_WINNT_WIN10 && !defined(DISABLE_NVME_PASSTHROUGH)
                                    device->drive_info.drive_type = NVME_DRIVE;
                                    device->drive_info.interface_type = NVME_INTERFACE;
                                    //set_Namespace_ID_For_Device(device);
                                    device->os_info.osReadWriteRecommended = true;//setting this so that read/write LBA functions will call Windows functions when possible for this.
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareCommit = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareDownload = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getFeatures = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyController = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyNamespace = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.vendorUnique = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities = true;
#else
                                    device->drive_info.drive_type = SCSI_DRIVE;
                                    device->drive_info.interface_type = SCSI_INTERFACE;
                                    device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
#endif
                                }
                                else //treat anything else as a SCSI device.
                                {
                                    device->drive_info.interface_type = SCSI_INTERFACE;
                                    //This does NOT mean that drive_type is SCSI, but set SCSI drive for now
                                    device->drive_info.drive_type = SCSI_DRIVE;
                                    device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
                                }

                                //Doing this here because the NSID may be needed for NVMe over USB interfaces too
                                device->drive_info.namespaceID = device->os_info.scsi_addr.Lun + 1;

                                if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
                                {
                                    setup_Passthrough_Hacks_By_ID(device);
                                }

                                //For now force direct IO all the time to match previous functionality.
                                //TODO: Investigate how to decide using double buffered vs direct vs mixed.
                                //Note: On a couple systems here, when using double buffered IO with ATA Pass-through, invalid checksums are returned for identify commands, but direct is fine...
                                device->os_info.ioMethod = WIN_IOCTL_FORCE_ALWAYS_DIRECT;

                                if (device->dFlags & OPEN_HANDLE_ONLY)//This is this far down because there is a lot of other things that need to be saved in order for windows pass-through to work correctly.
                                {
                                    return SUCCESS;
                                }

                                // Lets fill out rest of info
                                //TODO: This doesn't work for ATAPI on Windows right now. Will need to debug it more to figure out what other parts are wrong to get it fully functional.
                                //This won't be easy since ATAPI is a weird SCSI over ATA hybrid-TJE
                                ret = fill_Drive_Info_Data(device);

                                /*
                                While in most newer systems we found out that _force_ SCSI PassThrough will work,
                                using older version of WinPE will cause the SCSI IOCTL to fail - MA
                                */
                                if ((ret != SUCCESS) && (device->drive_info.interface_type == IDE_INTERFACE) )
                                {
                                    //we weren't successful getting device information...so now try switching to the other IOCTLs
                                    if (device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH || device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH_EX)
                                    {
                                        device->os_info.ioType = WIN_IOCTL_ATA_PASSTHROUGH;
                                    }
                                    else if (device->os_info.ioType == WIN_IOCTL_ATA_PASSTHROUGH)//ATA pass-through didn't work...so try SCSI pass-through just in case before falling back to legacy
                                    {
                                        device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
                                    }
                                    ret = fill_Drive_Info_Data(device);
                                    if (ret != SUCCESS)
                                    {
                                        //if we are here, then we are likely dealing with an old legacy driver that doesn't support these other IOs we've been trying...so fall back to some good old legacy stuff that may still not work. - TJE
                                        bool idePassThroughSupported = false;
                                        //test an identify command with IDE pass-through
                                        device->os_info.ioType = WIN_IOCTL_IDE_PASSTHROUGH_ONLY;
                                        //TODO: use check power mode command instead?
                                        if (SUCCESS == ata_Identify(device, (uint8_t *)&device->drive_info.IdentifyData.ata.Word000, sizeof(tAtaIdentifyData)) || SUCCESS == ata_Identify_Packet_Device(device, (uint8_t *)&device->drive_info.IdentifyData.ata.Word000, sizeof(tAtaIdentifyData)))
                                        {
                                            idePassThroughSupported = true;
                                        }
                                        if (device->os_info.winSMARTCmdSupport.smartIOSupported && idePassThroughSupported)
                                        {
                                            device->os_info.ioType = WIN_IOCTL_SMART_AND_IDE;
                                        }
                                        else if (device->os_info.winSMARTCmdSupport.smartIOSupported && !idePassThroughSupported)
                                        {
                                            device->os_info.ioType = WIN_IOCTL_SMART_ONLY;
                                        }
                                        device->os_info.osReadWriteRecommended = true;
                                        ret = fill_Drive_Info_Data(device);
                                    }
                                }

                                //Fill in IDE for ATA interface so we can know based on scan output which passthrough may need debugging
                                if (device->drive_info.interface_type == IDE_INTERFACE)
                                {
                                    memset(device->drive_info.T10_vendor_ident, 0, sizeof(device->drive_info.T10_vendor_ident));
                                    //Setting the vendor ID for ATA controllers like this so we can have an idea when we detect what we think is IDE and what we think is SATA. This may be helpful for debugging later. - TJE
                                    if (adapter_desc->BusType == BusTypeSata)
                                    {
                                        sprintf(device->drive_info.T10_vendor_ident, "%s", "SATA");
                                    }
                                    else
                                    {
                                        sprintf(device->drive_info.T10_vendor_ident, "%s", "IDE");
                                    }
                                }
                                //now windows api gives us some extra details that we should check to make sure that our fill_Drive_Info_Data call did correctly...just for some things the generic code may miss
                                switch (device_desc->BusType)
                                {
                                case BusTypeAtapi:
                                    device->drive_info.drive_type = ATAPI_DRIVE;
                                    device->drive_info.media_type = MEDIA_OPTICAL;
                                    break;
                                case BusTypeSd:
                                    device->drive_info.drive_type = FLASH_DRIVE;
                                    device->drive_info.media_type = MEDIA_SSM_FLASH;
                                    device->drive_info.interface_type = SD_INTERFACE;
                                    break;
                                case BusTypeMmc:
                                    device->drive_info.drive_type = FLASH_DRIVE;
                                    device->drive_info.media_type = MEDIA_SSM_FLASH;
                                    device->drive_info.interface_type = MMC_INTERFACE;
                                    break;
                                default:
                                    //do nothing since we assume everything else was set correctly earlier
                                    break;
                                }
                            }
                            LocalFree(device_desc);
                            device_desc = NULL;
                        } // else couldn't device desc allocate memory
                    } // either ret or size is zero
                }
                LocalFree(adapter_desc);
                adapter_desc = NULL;
            } // else couldn't adapter desc allocate memory
        } // either ret or size is zero
    }
    // Just in case we bailed out in any way.
    device->os_info.last_error = GetLastError();

    //printf("%s <--\n",__FUNCTION__);
    return ret;  //if we didn't get to fill_In_Device_Info FAILURE
}

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
//!                      NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
{
    HANDLE fd = NULL;
#if defined (UNICODE)
    wchar_t deviceName[40] = { 0 };
#else
    char deviceName[40] = { 0 };
#endif

    //Configuration manager library is not available on ARM for Windows. Library didn't exist when I went looking for it - TJE
#if !defined (_M_ARM) && !defined (_M_ARM_ARMV7VE) && !defined (_M_ARM_FP ) && !defined (_M_ARM64)
    //try forcing a system rescan before opening the list. This should help with crappy drivers or bad hotplug support - TJE
    DEVINST deviceInstance;
    DEVINSTID tree = NULL;//set to null for root of device tree
    ULONG locateNodeFlags = 0;//add flags here if we end up needing them
    if (CR_SUCCESS == CM_Locate_DevNode(&deviceInstance, tree, locateNodeFlags))
    {
        ULONG reenumerateFlags = 0;
        CM_Reenumerate_DevNode(deviceInstance, reenumerateFlags);
    }
#endif

    int  driveNumber = 0, found = 0;
    for (driveNumber = 0; driveNumber < MAX_DEVICES_TO_SCAN; driveNumber++)
    {
#if defined (UNICODE)
    wsprintf(deviceName, L"\\\\.\\PHYSICALDRIVE%d", driveNumber);
#else
     snprintf(deviceName, sizeof(deviceName), "\\\\.\\PhysicalDrive%d", driveNumber);
#endif
        //lets try to open the device.
        fd = CreateFile(deviceName,
                        GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                        OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                        FILE_FLAG_OVERLAPPED,
#else
                        0,
#endif
                        NULL);
        if (fd != INVALID_HANDLE_VALUE)
        {
            ++found;
            CloseHandle(fd);
        }
    }

    *numberOfDevices = found;

#if defined (ENABLE_CSMI)
    if (!(flags & GET_DEVICE_FUNCS_IGNORE_CSMI))//check whether they want CSMI devices or not
    {
        uint32_t csmiDeviceCount = 0;
        int csmiRet = get_CSMI_Device_Count(&csmiDeviceCount, flags);
        if (csmiRet == SUCCESS)
        {
            *numberOfDevices += csmiDeviceCount;
        }
    }
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
//!                        allocated must be the multiple of device structure.
//!                        The application can pass in less memory than needed
//!                        for all devices in the system, in which case the library
//!                        will fill the provided memory with how ever many device
//!                        structures it can hold.
//  Entry:
//!   \param[out] ptrToDeviceList = pointer to the allocated memory for the device list
//!   \param[in]  sizeInBytes = size of the entire list in bytes.
//!   \param[in]  versionBlock = versionBlock structure filled in by application for
//!                              sanity check by library.
//!   \param[in] flags = eScanFlags based mask to let application control.
//!                      NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, WARN_NOT_ALL_DEVICES_ENUMERATED - some deviec had trouble being enumerated.
//!                     Validate that it's drive_type is not UNKNOWN_DRIVE, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    int returnValue = SUCCESS;
    int numberOfDevices = 0;
    int driveNumber = 0, found = 0, failedGetDeviceCount = 0;
#if defined (UNICODE)
    wchar_t deviceName[40] = { 0 };
#else
    char deviceName[40] = { 0 };
#endif
    char    name[80] = { 0 }; //Because get device needs char
    HANDLE fd = INVALID_HANDLE_VALUE;
    tDevice * d = NULL;
#if defined (ENABLE_CSMI)
    uint32_t csmiDeviceCount = 0;
    if (!(flags & GET_DEVICE_FUNCS_IGNORE_CSMI))//check whether they want CSMI devices or not
    {

        int csmiRet = get_CSMI_Device_Count(&csmiDeviceCount, flags);
        if (csmiRet != SUCCESS)
        {
            csmiDeviceCount = 0;
        }
    }
#endif

    //TODO: Check if sizeInBytes is a multiple of
    if (!(ptrToDeviceList) || (!sizeInBytes))
    {
        returnValue = BAD_PARAMETER;
    }
    else if ((!(validate_Device_Struct(ver))))
    {
        returnValue = LIBRARY_MISMATCH;
    }
    else
    {
        numberOfDevices = sizeInBytes / sizeof(tDevice);
#if defined (ENABLE_CSMI)
        numberOfDevices -= csmiDeviceCount;
#endif
        d = ptrToDeviceList;
        for (driveNumber = 0; ((driveNumber < MAX_DEVICES_TO_SCAN) && (found < numberOfDevices)); driveNumber++)
        {
#if defined (UNICODE)
            wsprintf(deviceName, L"\\\\.\\%hs%d", WIN_PHYSICAL_DRIVE, driveNumber);
#else
            snprintf(deviceName, sizeof(deviceName), "%s%d", WIN_PHYSICAL_DRIVE, driveNumber);
#endif
      //lets try to open the device.
            fd = CreateFile((LPCTSTR)deviceName,
                GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                FILE_FLAG_OVERLAPPED,
#else
                0,
#endif
                NULL);
            if (fd != INVALID_HANDLE_VALUE)
            {
                CloseHandle(fd);
                snprintf(name, 80, "%s%d", WIN_PHYSICAL_DRIVE, driveNumber);
                eVerbosityLevels temp = d->deviceVerbosity;
                memset(d, 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size = ver.size;
                d->sanity.version = ver.version;
                d->dFlags = flags;
                returnValue = get_Device(name, d);
                if (returnValue != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                found++;
                d++;
            }
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (failedGetDeviceCount)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
#if defined (ENABLE_CSMI)
        if (!(flags & GET_DEVICE_FUNCS_IGNORE_CSMI) && csmiDeviceCount > 0)
        {
            int csmiRet = get_CSMI_Device_List(&ptrToDeviceList[numberOfDevices], csmiDeviceCount * sizeof(tDevice), ver, flags);
            if (returnValue == SUCCESS && csmiRet != SUCCESS)
            {
                //this will override the normal ret if it is already set to success with the CSMI return value
                returnValue = csmiRet;
            }
        }
#endif
    }

    return returnValue;
}

#if defined _NTSCSI_USER_MODE_
typedef struct _scsiPassThroughEXIOStruct
{
    union
    {
        SCSI_PASS_THROUGH_DIRECT_EX scsiPassThroughEXDirect;
        SCSI_PASS_THROUGH_EX        scsiPassThroughEX;
    };
    UCHAR                           cdbPad[CDB_LEN_32 - 1];//Padding for max sized CDB that the above struct can make...this needs to be here because of the ANYSIZE_ARRAY trick they use in the structure. If CDBs larger than 32B are ever made, this will need adjusting - TJE
    ULONG                           padding;//trying to help buffer alignment like the MS example shows.
    STOR_ADDR_BTL8                  storeAddr;
    UCHAR                           senseBuffer[SPC3_SENSE_LEN]; // If we do auto-sense, we need to allocate 252 bytes, according to SPC-3.
    UCHAR                           dataInBuffer[DOUBLE_BUFFERED_MAX_TRANSFER_SIZE];//Setting to this defined value to help prevent problems...TJE
    UCHAR                           dataOutBuffer[DOUBLE_BUFFERED_MAX_TRANSFER_SIZE];//Setting to this defined value to help prevent problems...TJE
} scsiPassThroughEXIOStruct, *ptrSCSIPassThroughEXIOStruct;

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int convert_SCSI_CTX_To_SCSI_Pass_Through_EX(ScsiIoCtx *scsiIoCtx, ptrSCSIPassThroughEXIOStruct psptd)
{
    int ret = SUCCESS;
    memset(&psptd->scsiPassThroughEX, 0, sizeof(SCSI_PASS_THROUGH_EX));
    psptd->scsiPassThroughEX.Version = 0;//MSDN says set this to zero
    psptd->scsiPassThroughEX.Length = sizeof(SCSI_PASS_THROUGH_EX);
    psptd->scsiPassThroughEX.CdbLength = scsiIoCtx->cdbLength;
    psptd->scsiPassThroughEX.StorAddressLength = sizeof(STOR_ADDR_BTL8);
    psptd->scsiPassThroughEX.ScsiStatus = 0;
    psptd->scsiPassThroughEX.SenseInfoLength = SPC3_SENSE_LEN;
    psptd->scsiPassThroughEX.Reserved = 0;
    //setup the store port address struct
    psptd->storeAddr.Type = STOR_ADDRESS_TYPE_BTL8;//Microsoft documentation says to set this
    psptd->storeAddr.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    //The host bus adapter (HBA) port number.
    psptd->storeAddr.Port = scsiIoCtx->device->os_info.scsi_addr.PortNumber;//This may or maynot be correct. Need to test it.
    psptd->storeAddr.Path = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->storeAddr.Target = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->storeAddr.Lun = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->storeAddr.Reserved = 0;
    psptd->scsiPassThroughEX.StorAddressOffset = offsetof(scsiPassThroughEXIOStruct, storeAddr);
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_OUT:
        psptd->scsiPassThroughEX.DataDirection = SCSI_IOCTL_DATA_OUT;
        psptd->scsiPassThroughEX.DataOutTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEX.DataOutBufferOffset = offsetof(scsiPassThroughEXIOStruct, dataOutBuffer);
        psptd->scsiPassThroughEX.DataInBufferOffset = 0;
        break;
    case XFER_DATA_IN:
        psptd->scsiPassThroughEX.DataDirection = SCSI_IOCTL_DATA_IN;
        psptd->scsiPassThroughEX.DataInTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEX.DataInBufferOffset = offsetof(scsiPassThroughEXIOStruct, dataInBuffer);
        psptd->scsiPassThroughEX.DataOutBufferOffset = 0;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassThroughEX.DataDirection = SCSI_IOCTL_DATA_UNSPECIFIED;
        break;
        //add in the next case later when we have better defined a difference between in and out data buffers...-TJE
        /*case XFER_DATA_IN_OUT:
        case XFER_DATA_OUT_IN:
        psptd->SptdEx.DataDirection = SCSI_IOCTL_DATA_BIDIRECTIONAL;
        psptd->SptdEx.DataInTransferLength = scsiIoCtx->dataLength;
        psptd->SptdEx.DataInBuffer = scsiIoCtx->pdata;
        psptd->SptdEx.DataOutTransferLength = scsiIoCtx->dataLength;
        psptd->SptdEx.DataOutBuffer = scsiIoCtx->pdata;
        break;*/
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = FAILURE;
        break;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        psptd->scsiPassThroughEX.TimeOutValue = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            psptd->scsiPassThroughEX.TimeOutValue = scsiIoCtx->timeout;
        }
        else
        {
            psptd->scsiPassThroughEX.TimeOutValue = 15;
        }
    }
    psptd->scsiPassThroughEX.SenseInfoOffset = offsetof(scsiPassThroughEXIOStruct, senseBuffer);
    memcpy(psptd->scsiPassThroughEX.Cdb, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    return ret;
}

int send_SCSI_Pass_Through_EX(ScsiIoCtx *scsiIoCtx)
{
    int           ret = FAILURE;
    BOOL          success = FALSE;
    ULONG         returned_data = 0;
    ptrSCSIPassThroughEXIOStruct sptdioEx = (ptrSCSIPassThroughEXIOStruct)malloc(sizeof(scsiPassThroughEXIOStruct));
    if (!sptdioEx)
    {
        return MEMORY_FAILURE;
    }
    seatimer_t commandTimer;
    memset(sptdioEx, 0, sizeof(scsiPassThroughEXIOStruct));
    memset(&commandTimer, 0, sizeof(seatimer_t));
    if (SUCCESS == convert_SCSI_CTX_To_SCSI_Pass_Through_EX(scsiIoCtx, sptdioEx))
    {
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD sptBufInLen = sizeof(scsiPassThroughEXIOStruct);
        DWORD sptBufOutLen = sizeof(scsiPassThroughEXIOStruct);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            sptBufOutLen += scsiIoCtx->dataLength;
            break;
        case XFER_DATA_OUT:
            //need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                memcpy(sptdioEx->dataOutBuffer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
            sptBufInLen += scsiIoCtx->dataLength;
            break;
        default:
            break;
        }
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_SCSI_PASS_THROUGH_EX,
            &sptdioEx->scsiPassThroughEX,
            sptBufInLen,
            &sptdioEx->scsiPassThroughEX,
            sptBufOutLen,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        scsiIoCtx->returnStatus.senseKey = sptdioEx->scsiPassThroughEX.ScsiStatus;

        if (success)
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; //setting to zero to be compatible with linux
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, sptdioEx->dataInBuffer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }

        // Any sense data?
        if (scsiIoCtx->psense != NULL && scsiIoCtx->senseDataSize > 0)
        {
            memcpy(scsiIoCtx->psense, &sptdioEx->senseBuffer[0], M_Min(sptdioEx->scsiPassThroughEX.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != NULL)
        {
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[3];
                break;
            }
        }
        safe_Free(sptdioEx);
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int convert_SCSI_CTX_To_SCSI_Pass_Through_EX_Direct(ScsiIoCtx *scsiIoCtx, ptrSCSIPassThroughEXIOStruct psptd, uint8_t *alignedPointer)
{
    int ret = SUCCESS;
    memset(&psptd->scsiPassThroughEXDirect, 0, sizeof(SCSI_PASS_THROUGH_DIRECT_EX));
    psptd->scsiPassThroughEXDirect.Version = 0;//MSDN says set this to zero
    psptd->scsiPassThroughEXDirect.Length = sizeof(SCSI_PASS_THROUGH_DIRECT_EX);
    psptd->scsiPassThroughEXDirect.CdbLength = scsiIoCtx->cdbLength;
    psptd->scsiPassThroughEXDirect.StorAddressLength = sizeof(STOR_ADDR_BTL8);
    psptd->scsiPassThroughEXDirect.ScsiStatus = 0;
    psptd->scsiPassThroughEXDirect.SenseInfoLength = SPC3_SENSE_LEN;
    psptd->scsiPassThroughEXDirect.Reserved = 0;
    //setup the store port address struct
    psptd->storeAddr.Type = STOR_ADDRESS_TYPE_BTL8;//Microsoft documentation says to set this
    psptd->storeAddr.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    //The host bus adapter (HBA) port number.
    psptd->storeAddr.Port = scsiIoCtx->device->os_info.scsi_addr.PortNumber;//This may or maynot be correct. Need to test it.
    psptd->storeAddr.Path = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->storeAddr.Target = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->storeAddr.Lun = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->storeAddr.Reserved = 0;
    psptd->scsiPassThroughEXDirect.StorAddressOffset = offsetof(scsiPassThroughEXIOStruct, storeAddr);
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_OUT:
        psptd->scsiPassThroughEXDirect.DataDirection = SCSI_IOCTL_DATA_OUT;
        psptd->scsiPassThroughEXDirect.DataOutTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEXDirect.DataOutBuffer = alignedPointer;
        psptd->scsiPassThroughEXDirect.DataInBuffer = NULL;
        break;
    case XFER_DATA_IN:
        psptd->scsiPassThroughEXDirect.DataDirection = SCSI_IOCTL_DATA_IN;
        psptd->scsiPassThroughEXDirect.DataInTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEXDirect.DataInBuffer = alignedPointer;
        psptd->scsiPassThroughEXDirect.DataOutBuffer = NULL;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassThroughEXDirect.DataDirection = SCSI_IOCTL_DATA_UNSPECIFIED;
        break;
        //add in the next case later when we have better defined a difference between in and out data buffers...-TJE
        /*case XFER_DATA_IN_OUT:
        case XFER_DATA_OUT_IN:
        psptd->SptdEx.DataDirection = SCSI_IOCTL_DATA_BIDIRECTIONAL;
        psptd->SptdEx.DataInTransferLength = scsiIoCtx->dataLength;
        psptd->SptdEx.DataInBuffer = scsiIoCtx->pdata;
        psptd->SptdEx.DataOutTransferLength = scsiIoCtx->dataLength;
        psptd->SptdEx.DataOutBuffer = scsiIoCtx->pdata;
        break;*/
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = FAILURE;
        break;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        psptd->scsiPassThroughEXDirect.TimeOutValue = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            psptd->scsiPassThroughEXDirect.TimeOutValue = scsiIoCtx->timeout;
        }
        else
        {
            psptd->scsiPassThroughEXDirect.TimeOutValue = 15;
        }
    }
    psptd->scsiPassThroughEXDirect.SenseInfoOffset = offsetof(scsiPassThroughEXIOStruct, senseBuffer);
    memcpy(psptd->scsiPassThroughEXDirect.Cdb, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    return ret;
}

int send_SCSI_Pass_Through_EX_Direct(ScsiIoCtx *scsiIoCtx)
{
    int           ret = FAILURE;
    BOOL          success = FALSE;
    ULONG         returned_data = 0;
    size_t scsiPTIoStructSize = sizeof(scsiPassThroughEXIOStruct);
    ptrSCSIPassThroughEXIOStruct sptdio = (ptrSCSIPassThroughEXIOStruct)malloc(sizeof(scsiPassThroughEXIOStruct));//add cdb and data length so that the memory allocated correctly!
    if (!sptdio)
    {
        return MEMORY_FAILURE;
    }
    seatimer_t commandTimer;
    memset(sptdio, 0, sizeof(scsiPassThroughEXIOStruct));
    memset(&commandTimer, 0, sizeof(seatimer_t));
    bool localAlignedBuffer = false;
    uint8_t *alignedPointer = scsiIoCtx->pdata;
    uint8_t *localBuffer = NULL;//we need to save this to free up the memory properly later.
    //Check the alignment...if we need to use a local buffer, we'll use one, then copy the data back
    if (scsiIoCtx->pdata && scsiIoCtx->device->os_info.alignmentMask != 0)
    {
        //This means the driver requires some sort of aligned pointer for the data buffer...so let's check and make sure that the user's pointer is aligned
        //If the user's pointer isn't aligned properly, align something local that is aligned to meet the driver's requirements, then copy data back for them.
        alignedPointer = (uint8_t*)(((UINT_PTR)scsiIoCtx->pdata + (UINT_PTR)scsiIoCtx->device->os_info.alignmentMask) & ~(UINT_PTR)scsiIoCtx->device->os_info.alignmentMask);
        if (alignedPointer != scsiIoCtx->pdata)
        {
            localAlignedBuffer = true;
            uint32_t totalBufferSize = scsiIoCtx->dataLength + scsiIoCtx->device->os_info.alignmentMask;
            localBuffer = (uint8_t*)calloc(totalBufferSize, sizeof(uint8_t));//TODO: If we want to remove allocating more memory, we should investigate making the scsiIoCtx->pdata a double pointer so we can reallocate it for the user.
            if (!localBuffer)
            {
                perror("error allocating aligned buffer for ATA Passthrough Direct...attempting to use user's pointer.");
                localAlignedBuffer = false;
                alignedPointer = scsiIoCtx->pdata;
            }
            else
            {
                alignedPointer = (uint8_t*)(((UINT_PTR)localBuffer + (UINT_PTR)scsiIoCtx->device->os_info.alignmentMask) & ~(UINT_PTR)scsiIoCtx->device->os_info.alignmentMask);
                if (scsiIoCtx->direction == XFER_DATA_OUT)
                {
                    memcpy(alignedPointer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
                }
            }
        }
    }
    if (SUCCESS == convert_SCSI_CTX_To_SCSI_Pass_Through_EX_Direct(scsiIoCtx, sptdio, alignedPointer))
    {
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD sptBufLen = sizeof(scsiPassThroughEXIOStruct);
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_SCSI_PASS_THROUGH_DIRECT_EX,
            &sptdio->scsiPassThroughEXDirect,
            sptBufLen,
            &sptdio->scsiPassThroughEXDirect,
            sptBufLen,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        scsiIoCtx->returnStatus.senseKey = sptdio->scsiPassThroughEXDirect.ScsiStatus;

        if (success)
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; //setting to zero to be compatible with linux
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }

        // Any sense data?
        if (scsiIoCtx->psense != NULL && scsiIoCtx->senseDataSize > 0)
        {
            memcpy(scsiIoCtx->psense, &sptdio->senseBuffer[0], M_Min(sptdio->scsiPassThroughEXDirect.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != NULL)
        {
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[3];
                break;
            }
        }
        safe_Free(sptdio);
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_Free(localBuffer);
    return ret;
}
#endif //#if defined _NTSCSI_USER_MODE_

//This structure MUST be dynamically allocated for double buffered transfers so that there is enough room for the return data! - TJE
typedef struct _scsiPassThroughIOStruct {
    union
    {
        SCSI_PASS_THROUGH_DIRECT    scsiPassthroughDirect;
        SCSI_PASS_THROUGH           scsiPassthrough;
    };
    ULONG                       padding;//trying to help buffer alignment like the MS example shows.
    UCHAR                       senseBuffer[SPC3_SENSE_LEN]; // If we do auto-sense, we need to allocate 252 bytes, according to SPC-3.
    UCHAR                       dataBuffer[1];//for double buffered transfer only
} scsiPassThroughIOStruct, *ptrSCSIPassThroughIOStruct;

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int convert_SCSI_CTX_To_SCSI_Pass_Through_Direct(ScsiIoCtx *scsiIoCtx, ptrSCSIPassThroughIOStruct psptd, uint8_t *alignedPointer)
{
    int ret = SUCCESS;
    psptd->scsiPassthroughDirect.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    psptd->scsiPassthroughDirect.PathId = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->scsiPassthroughDirect.TargetId = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->scsiPassthroughDirect.Lun = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->scsiPassthroughDirect.CdbLength = scsiIoCtx->cdbLength;
    psptd->scsiPassthroughDirect.ScsiStatus = 255;//set to something invalid
    psptd->scsiPassthroughDirect.SenseInfoLength = scsiIoCtx->senseDataSize;
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        psptd->scsiPassthroughDirect.DataIn = SCSI_IOCTL_DATA_IN;
        psptd->scsiPassthroughDirect.DataTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassthroughDirect.DataBuffer = alignedPointer;
        break;
    case XFER_DATA_OUT:
        psptd->scsiPassthroughDirect.DataIn = SCSI_IOCTL_DATA_OUT;
        psptd->scsiPassthroughDirect.DataTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassthroughDirect.DataBuffer = alignedPointer;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassthroughDirect.DataIn = SCSI_IOCTL_DATA_UNSPECIFIED;
        psptd->scsiPassthroughDirect.DataTransferLength = 0;
        psptd->scsiPassthroughDirect.DataBuffer = NULL;// psptd->SenseBuffer;
        break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = FAILURE;
        break;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        psptd->scsiPassthroughDirect.TimeOutValue = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            psptd->scsiPassthroughDirect.TimeOutValue = scsiIoCtx->timeout;
        }
        else
        {
            psptd->scsiPassthroughDirect.TimeOutValue = 15;
        }
    }
    //Use offsetof macro to set where to place the sense data. Old code, for whatever reason, didn't always work right...see comments below-TJE
    psptd->scsiPassthroughDirect.SenseInfoOffset = offsetof(scsiPassThroughIOStruct, senseBuffer);
    //sets the offset to the beginning of the sense buffer-TJE
    //psptd->scsiPassthroughDirect.SenseInfoOffset = (ULONG)((&psptd->senseBuffer[0] - (uint8_t*)&psptd->scsiPassthroughDirect));
    memcpy(psptd->scsiPassthroughDirect.Cdb, scsiIoCtx->cdb, sizeof(psptd->scsiPassthroughDirect.Cdb));
    return ret;
}

int convert_SCSI_CTX_To_SCSI_Pass_Through_Double_Buffered(ScsiIoCtx *scsiIoCtx, ptrSCSIPassThroughIOStruct psptd)
{
    int ret = SUCCESS;
    psptd->scsiPassthrough.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    psptd->scsiPassthrough.PathId = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->scsiPassthrough.TargetId = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->scsiPassthrough.Lun = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->scsiPassthrough.CdbLength = scsiIoCtx->cdbLength;
    psptd->scsiPassthrough.ScsiStatus = 255;//set to something invalid
    psptd->scsiPassthrough.SenseInfoLength = scsiIoCtx->senseDataSize;
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        psptd->scsiPassthrough.DataIn = SCSI_IOCTL_DATA_IN;
        psptd->scsiPassthrough.DataTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassthrough.DataBufferOffset = offsetof(scsiPassThroughIOStruct, dataBuffer);
        break;
    case XFER_DATA_OUT:
        psptd->scsiPassthrough.DataIn = SCSI_IOCTL_DATA_OUT;
        psptd->scsiPassthrough.DataTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassthrough.DataBufferOffset = offsetof(scsiPassThroughIOStruct, dataBuffer);;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassthrough.DataIn = SCSI_IOCTL_DATA_UNSPECIFIED;
        psptd->scsiPassthrough.DataTransferLength = 0;
        psptd->scsiPassthrough.DataBufferOffset = offsetof(scsiPassThroughIOStruct, dataBuffer);//this may also be better off as NULL...IDK - TJE
        break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = FAILURE;
        break;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        psptd->scsiPassthrough.TimeOutValue = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            psptd->scsiPassthrough.TimeOutValue = scsiIoCtx->timeout;
        }
        else
        {
            psptd->scsiPassthrough.TimeOutValue = 15;
        }
    }
    //Use offsetof macro to set where to place the sense data. Old code, for whatever reason, didn't always work right...see comments below-TJE
    psptd->scsiPassthrough.SenseInfoOffset = offsetof(scsiPassThroughIOStruct, senseBuffer);
    //sets the offset to the beginning of the sense buffer-TJE
    //psptd->scsiPassthrough.SenseInfoOffset = (ULONG)((&psptd->senseBuffer[0] - (uint8_t*)&psptd->scsiPassthrough));
    memcpy(psptd->scsiPassthrough.Cdb, scsiIoCtx->cdb, sizeof(psptd->scsiPassthrough.Cdb));
    return ret;
}

int send_SCSI_Pass_Through(ScsiIoCtx *scsiIoCtx)
{
    int           ret = FAILURE;
    BOOL          success = FALSE;
    ULONG         returned_data = 0;
    ptrSCSIPassThroughIOStruct sptdioDB = (ptrSCSIPassThroughIOStruct)malloc(sizeof(scsiPassThroughIOStruct) + scsiIoCtx->dataLength);
    if (!sptdioDB)
    {
        return MEMORY_FAILURE;
    }
    seatimer_t commandTimer;
    memset(sptdioDB, 0, sizeof(scsiPassThroughIOStruct));
    memset(&commandTimer, 0, sizeof(seatimer_t));
    if (SUCCESS == convert_SCSI_CTX_To_SCSI_Pass_Through_Double_Buffered(scsiIoCtx, sptdioDB))
    {
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD scsiPassThroughInLength = sizeof(scsiPassThroughIOStruct);
        DWORD scsiPassThroughOutLength = sizeof(scsiPassThroughIOStruct);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            scsiPassThroughOutLength += scsiIoCtx->dataLength;
            break;
        case XFER_DATA_OUT:
            //need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                memcpy(sptdioDB->dataBuffer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
            scsiPassThroughInLength += scsiIoCtx->dataLength;
            break;
        default:
            break;
        }
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (overlappedStruct.hEvent == NULL)
        {
            safe_Free(sptdioDB);
            return OS_PASSTHROUGH_FAILURE;
        }
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_SCSI_PASS_THROUGH,
            &sptdioDB->scsiPassthrough,
            scsiPassThroughInLength,
            &sptdioDB->scsiPassthrough,
            scsiPassThroughOutLength,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        if (success)
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; //setting to zero to be compatible with linux
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, sptdioDB->dataBuffer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
        scsiIoCtx->returnStatus.senseKey = sptdioDB->scsiPassthrough.ScsiStatus;

        // Any sense data?
        if (scsiIoCtx->psense != NULL && scsiIoCtx->senseDataSize > 0)
        {
            memcpy(scsiIoCtx->psense, sptdioDB->senseBuffer, M_Min(sptdioDB->scsiPassthrough.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != NULL)
        {
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[3];
                break;
            }
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

int send_SCSI_Pass_Through_Direct(ScsiIoCtx *scsiIoCtx)
{
    int           ret = FAILURE;
    BOOL          success = FALSE;
    ULONG         returned_data = 0;
    scsiPassThroughIOStruct sptdio;
    seatimer_t commandTimer;
    memset(&sptdio, 0, sizeof(scsiPassThroughIOStruct));
    memset(&commandTimer, 0, sizeof(seatimer_t));
    bool localAlignedBuffer = false;
    uint8_t *alignedPointer = scsiIoCtx->pdata;
    uint8_t *localBuffer = NULL;//we need to save this to free up the memory properly later.
    //Check the alignment...if we need to use a local buffer, we'll use one, then copy the data back
    if (scsiIoCtx->pdata && scsiIoCtx->device->os_info.alignmentMask != 0)
    {
        //This means the driver requires some sort of aligned pointer for the data buffer...so let's check and make sure that the user's pointer is aligned
        //If the user's pointer isn't aligned properly, align something local that is aligned to meet the driver's requirements, then copy data back for them.
        alignedPointer = (uint8_t*)(((UINT_PTR)scsiIoCtx->pdata + (UINT_PTR)scsiIoCtx->device->os_info.alignmentMask) & ~(UINT_PTR)scsiIoCtx->device->os_info.alignmentMask);
        if (alignedPointer != scsiIoCtx->pdata)
        {
            localAlignedBuffer = true;
            uint32_t totalBufferSize = scsiIoCtx->dataLength + scsiIoCtx->device->os_info.alignmentMask;
            localBuffer = (uint8_t*)calloc(totalBufferSize, sizeof(uint8_t));//TODO: If we want to remove allocating more memory, we should investigate making the scsiIoCtx->pdata a double pointer so we can reallocate it for the user.
            if (!localBuffer)
            {
                perror("error allocating aligned buffer for ATA Passthrough Direct...attempting to use user's pointer.");
                localAlignedBuffer = false;
                alignedPointer = scsiIoCtx->pdata;
            }
            else
            {
                alignedPointer = (uint8_t*)(((UINT_PTR)localBuffer + (UINT_PTR)scsiIoCtx->device->os_info.alignmentMask) & ~(UINT_PTR)scsiIoCtx->device->os_info.alignmentMask);
                if (scsiIoCtx->direction == XFER_DATA_OUT)
                {
                    memcpy(alignedPointer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
                }
            }
        }
    }
    if (SUCCESS == convert_SCSI_CTX_To_SCSI_Pass_Through_Direct(scsiIoCtx, &sptdio, alignedPointer))
    {
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD scsiPassThroughBufLen = sizeof(scsiPassThroughIOStruct);
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_SCSI_PASS_THROUGH_DIRECT,
            &sptdio.scsiPassthroughDirect,
            scsiPassThroughBufLen,
            &sptdio.scsiPassthroughDirect,
            scsiPassThroughBufLen,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        if (success)
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; //setting to zero to be compatible with linux
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }

        // Any sense data?
        if (scsiIoCtx->psense != NULL && scsiIoCtx->senseDataSize > 0)
        {
            memcpy(scsiIoCtx->psense, sptdio.senseBuffer, M_Min(sptdio.scsiPassthroughDirect.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != NULL)
        {
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq = scsiIoCtx->psense[3];
                break;
            }
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_Free(localBuffer);
    return ret;
}

int send_SCSI_Pass_Through_IO( ScsiIoCtx *scsiIoCtx )
{
    if (scsiIoCtx->device->os_info.ioMethod == WIN_IOCTL_FORCE_ALWAYS_DIRECT)
    {
        if (scsiIoCtx->cdbLength <= CDB_LEN_16)
        {
            return send_SCSI_Pass_Through_Direct(scsiIoCtx);
        }
#if defined _NTSCSI_USER_MODE_
        else if (scsiIoCtx->device->os_info.srbtype == SRB_TYPE_STORAGE_REQUEST_BLOCK)
        {
            return send_SCSI_Pass_Through_EX_Direct(scsiIoCtx);
        }
#endif
    }
    else if (scsiIoCtx->device->os_info.ioMethod == WIN_IOCTL_FORCE_ALWAYS_DOUBLE_BUFFERED)
    {
        if (scsiIoCtx->cdbLength <= CDB_LEN_16)
        {
            return send_SCSI_Pass_Through(scsiIoCtx);
        }
#if defined _NTSCSI_USER_MODE_
        else if (scsiIoCtx->device->os_info.srbtype == SRB_TYPE_STORAGE_REQUEST_BLOCK)
        {
            return send_SCSI_Pass_Through_EX(scsiIoCtx);
        }
#endif
    }
    else
    {
        /*The following commend out of check on srbtype is a fix for the Microsoft Hyper-V issue
            where it was not able to support _EX commands even though the device / adapter descriptor
            said that it could based on the SRB Type.*/
        if (/*scsiIoCtx->device->os_info.srbtype == SRB_TYPE_SCSI_REQUEST_BLOCK && */scsiIoCtx->cdbLength <= CDB_LEN_16 && scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH)
        {
            if (scsiIoCtx->dataLength > DOUBLE_BUFFERED_MAX_TRANSFER_SIZE)
            {
                return send_SCSI_Pass_Through_Direct(scsiIoCtx);
            }
            else
            {
                return send_SCSI_Pass_Through(scsiIoCtx);
            }
        }
#if defined _NTSCSI_USER_MODE_
        else if (scsiIoCtx->device->os_info.srbtype == SRB_TYPE_STORAGE_REQUEST_BLOCK)//supports 32byte IOCTLS
        {
            if (scsiIoCtx->dataLength > DOUBLE_BUFFERED_MAX_TRANSFER_SIZE)
            {
                return send_SCSI_Pass_Through_EX_Direct(scsiIoCtx);
            }
            else
            {
                return send_SCSI_Pass_Through_EX(scsiIoCtx);
            }
        }
#endif
    }
    return OS_PASSTHROUGH_FAILURE;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int convert_SCSI_CTX_To_ATA_PT_Direct(ScsiIoCtx *p_scsiIoCtx, PATA_PASS_THROUGH_DIRECT ptrATAPassThroughDirect, uint8_t *alignedDataPointer)
{
    int ret = SUCCESS;

    ptrATAPassThroughDirect->Length = sizeof(ATA_PASS_THROUGH_DIRECT);
    ptrATAPassThroughDirect->AtaFlags = ATA_FLAGS_DRDY_REQUIRED;

    switch (p_scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_DATA_IN;
        ptrATAPassThroughDirect->DataTransferLength = p_scsiIoCtx->dataLength;
        ptrATAPassThroughDirect->DataBuffer = alignedDataPointer;
#if WINVER >= SEA_WIN32_WINNT_VISTA
        if (p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount <= 1 && p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 == 0)
        {
            ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_NO_MULTIPLE;
        }
#endif
        break;
    case XFER_DATA_OUT:
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_DATA_OUT;
        ptrATAPassThroughDirect->DataTransferLength = p_scsiIoCtx->dataLength;
        ptrATAPassThroughDirect->DataBuffer = alignedDataPointer;
#if WINVER >= SEA_WIN32_WINNT_VISTA
        if (p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount <= 1 && p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 == 0)
        {
            ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_NO_MULTIPLE;
        }
#endif
        break;
    case XFER_NO_DATA:
        ptrATAPassThroughDirect->DataTransferLength = 0;
        ptrATAPassThroughDirect->DataBuffer = NULL;
        break;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = BAD_PARAMETER;
        break;
    }
    //set the DMA flag if needed
    switch (p_scsiIoCtx->pAtaCmdOpts->commadProtocol)
    {
    case ATA_PROTOCOL_DMA:
    case ATA_PROTOCOL_UDMA:
    case ATA_PROTOCOL_PACKET_DMA:
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_USE_DMA;
        break;
    case ATA_PROTOCOL_DEV_DIAG:
    case ATA_PROTOCOL_NO_DATA:
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PIO:
        //these are supported but no flags need to be set
        break;
    case ATA_PROTOCOL_RET_INFO:
        //this doesn't do anything in ATA PassThrough and is only useful for SCSI PassThrough since this is an HBA request, not a drive request, but we don't want to print out an error message
        return NOT_SUPPORTED;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nProtocol Not Supported in ATA Pass Through.\n");
        }
        ret = NOT_SUPPORTED;
        break;
    }
    if (p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > p_scsiIoCtx->timeout)
    {
        ptrATAPassThroughDirect->TimeOutValue = p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (p_scsiIoCtx->timeout != 0)
        {
            ptrATAPassThroughDirect->TimeOutValue = p_scsiIoCtx->timeout;
        }
        else
        {
            ptrATAPassThroughDirect->TimeOutValue = 15;
        }
    }
    ptrATAPassThroughDirect->PathId = p_scsiIoCtx->device->os_info.scsi_addr.PathId;
    ptrATAPassThroughDirect->TargetId = p_scsiIoCtx->device->os_info.scsi_addr.TargetId;
    ptrATAPassThroughDirect->Lun = p_scsiIoCtx->device->os_info.scsi_addr.Lun;
    // Task File
    ptrATAPassThroughDirect->CurrentTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
    ptrATAPassThroughDirect->CurrentTaskFile[1] = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount; // Sector Count Reg
    ptrATAPassThroughDirect->CurrentTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // Sector Number ( or LBA Lo )
    ptrATAPassThroughDirect->CurrentTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid; // Cylinder Low ( or LBA Mid )
    ptrATAPassThroughDirect->CurrentTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi; // Cylinder High (or LBA Hi)
    ptrATAPassThroughDirect->CurrentTaskFile[5] = p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead; // Device/Head Register
    ptrATAPassThroughDirect->CurrentTaskFile[6] = p_scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
    ptrATAPassThroughDirect->CurrentTaskFile[7] = 0; // Reserved
    if (p_scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        //set the 48bit command flag
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_48BIT_COMMAND;
        ptrATAPassThroughDirect->PreviousTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.Feature48;// Features Ext Register
        ptrATAPassThroughDirect->PreviousTaskFile[1] = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;// Sector Count Ext Register
        ptrATAPassThroughDirect->PreviousTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;// LBA Lo Ext
        ptrATAPassThroughDirect->PreviousTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;// LBA Mid Ext
        ptrATAPassThroughDirect->PreviousTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;// LBA Hi Ext
        ptrATAPassThroughDirect->PreviousTaskFile[5] = 0;//p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;//this is either 0 or device...
        ptrATAPassThroughDirect->PreviousTaskFile[6] = 0;
        ptrATAPassThroughDirect->PreviousTaskFile[7] = 0;
    }
    return ret;
}

int send_ATA_Passthrough_Direct(ScsiIoCtx *scsiIoCtx)
{
    int ret = FAILURE;
    BOOL success;
    ULONG returned_data = 0;
    ATA_PASS_THROUGH_DIRECT ataPassThroughDirect;
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    memset(&ataPassThroughDirect, 0, sizeof(ATA_PASS_THROUGH_DIRECT));
    bool localAlignedBuffer = false;
    uint8_t *alignedPointer = scsiIoCtx->pdata;
    uint8_t *localBuffer = NULL;//we need to save this to free up the memory properly later.
    //Check the alignment...if we need to use a local buffer, we'll use one, then copy the data back
    if (scsiIoCtx->pdata && scsiIoCtx->device->os_info.alignmentMask != 0)
    {
        //This means the driver requires some sort of aligned pointer for the data buffer...so let's check and make sure that the user's pointer is aligned
        //If the user's pointer isn't aligned properly, align something local that is aligned to meet the driver's requirements, then copy data back for them.
        alignedPointer = (uint8_t*)(((UINT_PTR)scsiIoCtx->pdata + (UINT_PTR)scsiIoCtx->device->os_info.alignmentMask) & ~(UINT_PTR)scsiIoCtx->device->os_info.alignmentMask);
        if (alignedPointer != scsiIoCtx->pdata)
        {
            localAlignedBuffer = true;
            uint32_t totalBufferSize = scsiIoCtx->dataLength + scsiIoCtx->device->os_info.alignmentMask;
            localBuffer = (uint8_t*)calloc(totalBufferSize, sizeof(uint8_t));//TODO: If we want to remove allocating more memory, we should investigate making the scsiIoCtx->pdata a double pointer so we can reallocate it for the user.
            if (!localBuffer)
            {
                perror("error allocating aligned buffer for ATA Passthrough Direct...attempting to use user's pointer.");
                localAlignedBuffer = false;
                alignedPointer = scsiIoCtx->pdata;
            }
            else
            {
                alignedPointer = (uint8_t*)(((UINT_PTR)localBuffer + (UINT_PTR)scsiIoCtx->device->os_info.alignmentMask) & ~(UINT_PTR)scsiIoCtx->device->os_info.alignmentMask);
                if (scsiIoCtx->direction == XFER_DATA_OUT)
                {
                    memcpy(alignedPointer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
                }
            }
        }
    }

    ret = convert_SCSI_CTX_To_ATA_PT_Direct(scsiIoCtx, &ataPassThroughDirect, alignedPointer);
    if (SUCCESS == ret)
    {
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_ATA_PASS_THROUGH_DIRECT,
            &ataPassThroughDirect,
            sizeof(ATA_PASS_THROUGH_DIRECT),
            &ataPassThroughDirect,
            sizeof(ATA_PASS_THROUGH_DIRECT),
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_NOT_SUPPORTED://this is what is returned when we try to send a sanitize command in Win10
                ret = OS_COMMAND_BLOCKED;
                break;
            case ERROR_IO_DEVICE://OS_PASSTHROUGH_FAILURE
            case ERROR_INVALID_PARAMETER://Or command not supported?
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (success)
        {
            ret = SUCCESS;
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                //memcpy the data back to the user's pointer since we had to allocate one locally.
                memcpy(scsiIoCtx->pdata, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            scsiIoCtx->returnStatus.senseKey = 0x01;
        }
        scsiIoCtx->returnStatus.asc = 0x00;//might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D;//might need to change this later
        //get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in descriptor format
        //current = 28bit, previous = 48bit
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
                    scsiIoCtx->psense[12] = ataPassThroughDirect.PreviousTaskFile[1];// Sector Count Ext
                    scsiIoCtx->psense[14] = ataPassThroughDirect.PreviousTaskFile[2];// LBA Lo Ext
                    scsiIoCtx->psense[16] = ataPassThroughDirect.PreviousTaskFile[3];// LBA Mid Ext
                    scsiIoCtx->psense[18] = ataPassThroughDirect.PreviousTaskFile[4];// LBA Hi
                }
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = ataPassThroughDirect.CurrentTaskFile[0];// Error
                scsiIoCtx->psense[13] = ataPassThroughDirect.CurrentTaskFile[1];// Sector Count
                scsiIoCtx->psense[15] = ataPassThroughDirect.CurrentTaskFile[2];// LBA Lo
                scsiIoCtx->psense[17] = ataPassThroughDirect.CurrentTaskFile[3];// LBA Mid
                scsiIoCtx->psense[19] = ataPassThroughDirect.CurrentTaskFile[4];// LBA Hi
                scsiIoCtx->psense[20] = ataPassThroughDirect.CurrentTaskFile[5];// Device/Head
                scsiIoCtx->psense[21] = ataPassThroughDirect.CurrentTaskFile[6];// Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc = 0x20;
        scsiIoCtx->returnStatus.ascq = 0x00;
        //dummy up sense data
        if (scsiIoCtx->psense != NULL)
        {
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            //fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            //acq
            scsiIoCtx->psense[12] = 0x20;//invalid operation code
            //acsq
            scsiIoCtx->psense[13] = 0x00;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Couldn't convert SCSI-To-IDE interface (direct)\n");
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_Free(localBuffer);//This will check if NULL before freeing it, so we shouldn't have any issues.
    return ret;
}

typedef struct _ATADoubleBufferedIO
{
    ATA_PASS_THROUGH_EX ataPTCommand;
    ULONG padding;//trying to help buffer alignment like the MS spti example shows....may or may not be needed here
    UCHAR dataBuffer[1];
}ATADoubleBufferedIO, *ptrATADoubleBufferedIO;

int convert_SCSI_CTX_To_ATA_PT_Ex(ScsiIoCtx *p_scsiIoCtx, ptrATADoubleBufferedIO p_t_ata_pt)
{
    int ret = SUCCESS;

    p_t_ata_pt->ataPTCommand.Length = sizeof(ATA_PASS_THROUGH_EX);
    p_t_ata_pt->ataPTCommand.AtaFlags = ATA_FLAGS_DRDY_REQUIRED;

    switch (p_scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_DATA_IN;
        p_t_ata_pt->ataPTCommand.DataTransferLength = p_scsiIoCtx->dataLength;
        p_t_ata_pt->ataPTCommand.DataBufferOffset = offsetof(ATADoubleBufferedIO, dataBuffer);
#if WINVER >= SEA_WIN32_WINNT_VISTA
        if (p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount <= 1 && p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 == 0)
        {
            p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_NO_MULTIPLE;
        }
#endif
        break;
    case XFER_DATA_OUT:
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_DATA_OUT;
        p_t_ata_pt->ataPTCommand.DataTransferLength = p_scsiIoCtx->dataLength;
        p_t_ata_pt->ataPTCommand.DataBufferOffset = offsetof(ATADoubleBufferedIO, dataBuffer);
#if WINVER >= SEA_WIN32_WINNT_VISTA
        if (p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount <= 1 && p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 == 0)
        {
            p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_NO_MULTIPLE;
        }
#endif
        break;
    case XFER_NO_DATA:
        p_t_ata_pt->ataPTCommand.DataTransferLength = 0;
        p_t_ata_pt->ataPTCommand.DataBufferOffset = offsetof(ATADoubleBufferedIO, dataBuffer);//we always allocate at least 1 byte here...so give it something? Or do we set NULL? Seems to work as is... - TJE
        break;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = BAD_PARAMETER;
        break;
    }
    //set the DMA flag if needed
    switch (p_scsiIoCtx->pAtaCmdOpts->commadProtocol)
    {
    case ATA_PROTOCOL_DMA:
    case ATA_PROTOCOL_UDMA:
    case ATA_PROTOCOL_PACKET_DMA:
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_USE_DMA;
        break;
    case ATA_PROTOCOL_DEV_DIAG:
    case ATA_PROTOCOL_NO_DATA:
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PIO:
        //these are supported but no flags need to be set
        break;
    case ATA_PROTOCOL_RET_INFO:
        //this doesn't do anything in ATA PassThrough and is only useful for SCSI PassThrough since this is an HBA request, not a drive request, but we don't want to print out an error message
        return NOT_SUPPORTED;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nProtocol Not Supported in ATA Pass Through.\n");
        }
        ret = NOT_SUPPORTED;
        break;
    }
    if (p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > p_scsiIoCtx->timeout)
    {
        p_t_ata_pt->ataPTCommand.TimeOutValue = p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (p_scsiIoCtx->timeout != 0)
        {
            p_t_ata_pt->ataPTCommand.TimeOutValue = p_scsiIoCtx->timeout;
        }
        else
        {
            p_t_ata_pt->ataPTCommand.TimeOutValue = 15;
        }
    }
    p_t_ata_pt->ataPTCommand.PathId = p_scsiIoCtx->device->os_info.scsi_addr.PathId;
    p_t_ata_pt->ataPTCommand.TargetId = p_scsiIoCtx->device->os_info.scsi_addr.TargetId;
    p_t_ata_pt->ataPTCommand.Lun = p_scsiIoCtx->device->os_info.scsi_addr.Lun;
    // Task File
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[1] = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount; // Sector Count Reg
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // Sector Number ( or LBA Lo )
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid; // Cylinder Low ( or LBA Mid )
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi; // Cylinder High (or LBA Hi)
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[5] = p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead; // Device/Head Register
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[6] = p_scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[7] = 0; // Reserved
    if (p_scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        //set the 48bit command flag
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_48BIT_COMMAND;
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.Feature48;// Features Ext Register
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[1] = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;// Sector Count Ext Register
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;// LBA Lo Ext
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;// LBA Mid Ext
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;// LBA Hi Ext
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[5] = 0;//p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;//this is either 0 or device...
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[6] = 0;
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[7] = 0;
    }
    return ret;
}

int send_ATA_Passthrough_Ex(ScsiIoCtx *scsiIoCtx)
{
    int ret = FAILURE;
    BOOL success;
    ULONG returned_data = 0;
    uint32_t dataLength = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
    ptrATADoubleBufferedIO doubleBufferedIO = (ptrATADoubleBufferedIO)malloc(sizeof(ATA_PASS_THROUGH_EX) + dataLength);
    if (!doubleBufferedIO)
    {
        //something went really wrong...
        return MEMORY_FAILURE;
    }
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    memset(doubleBufferedIO, 0, sizeof(ATA_PASS_THROUGH_EX) + dataLength);
    ret = convert_SCSI_CTX_To_ATA_PT_Ex(scsiIoCtx, doubleBufferedIO);
    if (SUCCESS == ret)
    {
        ULONG inBufferLength = sizeof(ATA_PASS_THROUGH_DIRECT);
        ULONG outBufferLength = sizeof(ATA_PASS_THROUGH_DIRECT);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            outBufferLength += M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
            break;
        case XFER_DATA_OUT:
            //need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                memcpy(doubleBufferedIO->dataBuffer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
            inBufferLength += M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
            break;
        default:
            break;
        }
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_ATA_PASS_THROUGH,
            &doubleBufferedIO->ataPTCommand,
            inBufferLength,
            &doubleBufferedIO->ataPTCommand,
            outBufferLength,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (success)
        {
            ret = SUCCESS;
            //copy the data buffer back to the user's data pointer
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, doubleBufferedIO->dataBuffer, scsiIoCtx->dataLength);
            }
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
            scsiIoCtx->returnStatus.senseKey = 0x01;
        }
        scsiIoCtx->returnStatus.asc = 0x00;//might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D;//might need to change this later
        //get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in descriptor format
        //current = 28bit, previous = 48bit
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
                    scsiIoCtx->psense[12] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[1];// Sector Count Ext
                    scsiIoCtx->psense[14] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[2];// LBA Lo Ext
                    scsiIoCtx->psense[16] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[3];// LBA Mid Ext
                    scsiIoCtx->psense[18] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[4];// LBA Hi
                }
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[0];// Error
                scsiIoCtx->psense[13] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[1];// Sector Count
                scsiIoCtx->psense[15] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[2];// LBA Lo
                scsiIoCtx->psense[17] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[3];// LBA Mid
                scsiIoCtx->psense[19] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[4];// LBA Hi
                scsiIoCtx->psense[20] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[5];// Device/Head
                scsiIoCtx->psense[21] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[6];// Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc = 0x20;
        scsiIoCtx->returnStatus.ascq = 0x00;
        //dummy up sense data
        if (scsiIoCtx->psense != NULL)
        {
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            //fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            //acq
            scsiIoCtx->psense[12] = 0x20;//invalid operation code
            //acsq
            scsiIoCtx->psense[13] = 0x00;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Couldn't convert SCSI-To-IDE interface (douuble buffered)\n");
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_Free(doubleBufferedIO);
    return ret;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int send_ATA_Pass_Through_IO( ScsiIoCtx *scsiIoCtx )
{
    if (scsiIoCtx->device->os_info.ioMethod == WIN_IOCTL_FORCE_ALWAYS_DIRECT)
    {
        return send_ATA_Passthrough_Direct(scsiIoCtx);
    }
    else if (scsiIoCtx->device->os_info.ioMethod == WIN_IOCTL_FORCE_ALWAYS_DOUBLE_BUFFERED)
    {
        return send_ATA_Passthrough_Ex(scsiIoCtx);
    }
    else
    {
        //check the transfer length to decide
        if (scsiIoCtx->dataLength > DOUBLE_BUFFERED_MAX_TRANSFER_SIZE || scsiIoCtx->pAtaCmdOpts->dataSize > DOUBLE_BUFFERED_MAX_TRANSFER_SIZE)
        {
            //direct IO
            return send_ATA_Passthrough_Direct(scsiIoCtx);
        }
        else
        {
            //double buffered IO
            return send_ATA_Passthrough_Ex(scsiIoCtx);
        }
    }
}

typedef struct _IDEDoubleBufferedIO
{
    IDEREGS ideRegisters;
    ULONG   dataBufferSize;
    UCHAR   dataBuffer[1];
}IDEDoubleBufferedIO, *ptrIDEDoubleBufferedIO;

int convert_SCSI_CTX_To_IDE_PT(ScsiIoCtx *p_scsiIoCtx, ptrIDEDoubleBufferedIO p_t_ide_pt)
{
    int ret = SUCCESS;

    if (p_scsiIoCtx->pAtaCmdOpts->commandType != ATA_CMD_TYPE_TASKFILE)
    {
        return NOT_SUPPORTED;
    }

    p_t_ide_pt->dataBufferSize = p_scsiIoCtx->dataLength;

    //set the DMA flag if needed
    switch (p_scsiIoCtx->pAtaCmdOpts->commadProtocol)
    {
    case ATA_PROTOCOL_DMA:
    case ATA_PROTOCOL_UDMA:
    case ATA_PROTOCOL_PACKET_DMA:
        //TODO: Some comments in old old legacy code say DMA is not supported with this IOCTL, so they don't issue the command...should we do this? - TJE
        break;
    case ATA_PROTOCOL_DEV_DIAG:
    case ATA_PROTOCOL_NO_DATA:
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PIO:
        //these are supported but no flags need to be set
        break;
    case ATA_PROTOCOL_RET_INFO:
        //this doesn't do anything in ATA PassThrough and is only useful for SCSI PassThrough since this is an HBA request, not a drive request, but we don't want to print out an error message
        return NOT_SUPPORTED;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nProtocol Not Supported in ATA Pass Through.\n");
        }
        ret = NOT_SUPPORTED;
        break;
    }
    //Timeout cannot be set...-TJE
    // Task File (No extended commands allowed!)
    p_t_ide_pt->ideRegisters.bFeaturesReg = p_scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
    p_t_ide_pt->ideRegisters.bSectorCountReg = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount; // Sector Count Reg
    p_t_ide_pt->ideRegisters.bSectorNumberReg = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // Sector Number ( or LBA Lo )
    p_t_ide_pt->ideRegisters.bCylLowReg = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid; // Cylinder Low ( or LBA Mid )
    p_t_ide_pt->ideRegisters.bCylHighReg = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi; // Cylinder High (or LBA Hi)
    p_t_ide_pt->ideRegisters.bDriveHeadReg = p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead; // Device/Head Register
    p_t_ide_pt->ideRegisters.bCommandReg = p_scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
    p_t_ide_pt->ideRegisters.bReserved = RESERVED;

    return ret;
}
//This code has not been tested! - TJE
int send_IDE_Pass_Through_IO(ScsiIoCtx *scsiIoCtx)
{
    int ret = FAILURE;
    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return OS_PASSTHROUGH_FAILURE;//or NOT_SUPPORTED ? - TJE
    }
    BOOL success;
    ULONG returned_data = 0;
    uint32_t dataLength = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
    ptrIDEDoubleBufferedIO doubleBufferedIO = (ptrIDEDoubleBufferedIO)malloc(sizeof(IDEDoubleBufferedIO) - 1 + dataLength);
    if (!doubleBufferedIO)
    {
        //something went really wrong...
        return MEMORY_FAILURE;
    }
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    memset(doubleBufferedIO, 0, sizeof(IDEDoubleBufferedIO) - 1 + dataLength);
    ret = convert_SCSI_CTX_To_IDE_PT(scsiIoCtx, doubleBufferedIO);
    if (SUCCESS == ret)
    {
        ULONG inBufferLength = sizeof(IDEREGS) + sizeof(ULONG);
        ULONG outBufferLength = sizeof(IDEREGS) + sizeof(ULONG);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            outBufferLength += M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
            break;
        case XFER_DATA_OUT:
            //need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                memcpy(doubleBufferedIO->dataBuffer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
            inBufferLength += M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
            break;
        default:
            break;
        }
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_IDE_PASS_THROUGH,
            doubleBufferedIO,
            inBufferLength,
            doubleBufferedIO,
            outBufferLength,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (success)
        {
            ret = SUCCESS;
            //copy the data buffer back to the user's data pointer
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, doubleBufferedIO->dataBuffer, scsiIoCtx->dataLength);
            }
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_IO_DEVICE://OS_PASSTHROUGH_FAILURE
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            scsiIoCtx->returnStatus.senseKey = 0x01;
        }
        scsiIoCtx->returnStatus.asc = 0x00;//might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D;//might need to change this later
        //get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in descriptor format
        //current = 28bit, previous = 48bit
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
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = doubleBufferedIO->ideRegisters.bFeaturesReg;// Error
                scsiIoCtx->psense[13] = doubleBufferedIO->ideRegisters.bSectorCountReg;// Sector Count
                scsiIoCtx->psense[15] = doubleBufferedIO->ideRegisters.bSectorNumberReg;// LBA Lo
                scsiIoCtx->psense[17] = doubleBufferedIO->ideRegisters.bCylLowReg;// LBA Mid
                scsiIoCtx->psense[19] = doubleBufferedIO->ideRegisters.bCylHighReg;// LBA Hi
                scsiIoCtx->psense[20] = doubleBufferedIO->ideRegisters.bDriveHeadReg;// Device/Head
                scsiIoCtx->psense[21] = doubleBufferedIO->ideRegisters.bCommandReg;// Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc = 0x20;
        scsiIoCtx->returnStatus.ascq = 0x00;
        //dummy up sense data
        if (scsiIoCtx->psense != NULL)
        {
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            //fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            //acq
            scsiIoCtx->psense[12] = 0x20;//invalid operation code
            //acsq
            scsiIoCtx->psense[13] = 0x00;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Couldn't convert SCSI-To-IDE interface (legacy IDE double buffered)\n");
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_Free(doubleBufferedIO);
    return ret;
}
#if WINVER >= SEA_WIN32_WINNT_WIN10
/*
This API only supported deferred download and activate commands as defined in ACS3+ and SPC4+

This table defines when this API is supported based on the drive and interface of the drive.
      IDE | SCSI
ATA    Y  |   N
SCSI   N  |   Y

This table defines when this API is supported based on the Interface and the Command being sent
       ATA DL | ATA DL DMA | SCSI WB
IDE       Y   |      Y     |    N
SCSI      N   |      N     |    Y

The reason the API is used only in the instances shown above is because the library is trying to
honor issuing the expected command on a specific interface.

If the drive is an ATA drive, behind a SAS controller, then a Write buffer command is issued to the
controller to be translated according to the SAT spec. Sometimes, this may not be what a caller is wanting to do
so we assume that we will only issue the command the caller is expecting to issue.

There is an option to allow using this API call with any supported FWDL command regardless of drive type and interface that can be set.
Device->os_info.fwdlIOsupport.allowFlexibleUseOfAPI set to true will check for a supported SCSI or ATA command and all other payload
requirements and allow it to be issued for any case. This is good if your only goal is to get firmware to a drive and don't care about testing a specific command sequence.
NOTE: Some SAS HBAs will issue a readlogext command before each download command when performing deferred download, which may not be expected if taking a bus trace of the sequence.

*/



bool is_Firmware_Download_Command_Compatible_With_Win_API(ScsiIoCtx *scsiIoCtx)//TODO: add nvme support
{
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
    printf("Checking if FWDL Command is compatible with Win 10 API\n");
#endif
    if (!scsiIoCtx->device->os_info.fwdlIOsupport.fwdlIOSupported)
    {
        //OS doesn't support this IO on this device, so just say no!
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
        printf("\tFalse (not Supported)\n");
#endif
        return false;
    }
    //If we are trying to send an ATA command, then only use the API if it's IDE.
    //SCSI and RAID interfaces depend on the SATL to translate it correctly, but that is not checked by windows and is not possible since nothing responds to the report supported operation codes command
    //A future TODO will be to have either a lookup table or additional check somewhere to send the report supported operation codes command, but this is good enough for now, since it's unlikely a SATL will implement that...
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
    printf("scsiIoCtx = %p\t->pAtaCmdOpts = %p\tinterface type: %d\n", scsiIoCtx, scsiIoCtx->pAtaCmdOpts, scsiIoCtx->device->drive_info.interface_type);
#endif
    if (scsiIoCtx->device->os_info.fwdlIOsupport.allowFlexibleUseOfAPI)
    {
        uint32_t transferLengthBytes = 0;
        bool supportedCMD = false;
        bool isActivate = false;
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
        printf("Flexible Win10 FWDL API allowed. Checking for supported commands\n");
#endif
        if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
        {
            uint8_t wbMode = M_GETBITRANGE(scsiIoCtx->cdb[1], 4, 0);
            if (wbMode == SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER)
            {
                supportedCMD = true;
                transferLengthBytes = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            }
            else if (wbMode == SCSI_WB_ACTIVATE_DEFERRED_MICROCODE)
            {
                supportedCMD = true;
                isActivate = true;
            }
        }
        else if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE || scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA))
        {

            if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0E)
            {
                supportedCMD = true;
                transferLengthBytes = M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaLow, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) * LEGACY_DRIVE_SEC_SIZE;
            }
            else if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
            {
                supportedCMD = true;
                isActivate = true;
            }
        }
        if (supportedCMD)
        {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
            printf("\tDetected supported command\n");
#endif
            if (isActivate)
            {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                printf("\tTrue - is an activate command\n");
#endif
                return true;
            }
            else
            {
                if (transferLengthBytes < scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize && (transferLengthBytes % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment == 0))
                {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                    printf("\tTrue - payload fits FWDL requirements from OS/Driver\n");
#endif
                    return true;
                }
            }
        }
    }
    else if (scsiIoCtx && scsiIoCtx->pAtaCmdOpts && scsiIoCtx->device->drive_info.interface_type == IDE_INTERFACE)
    {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
        printf("Checking ATA command info for FWDL support\n");
#endif
        //We're sending an ATA passthrough command, and the OS says the io is supported, so it SHOULD work. - TJE
        if (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE || scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA)
        {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
            printf("Is Download Microcode command (%" PRIX8 "h)\n", scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus);
#endif
            if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0E)
            {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                printf("Is deferred download mode Eh\n");
#endif
                //We know it's a download command, now we need to make sure it's a multiple of the Windows alignment requirement and that it isn't larger than the maximum allowed
                uint16_t transferSizeSectors = M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaLow, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount);
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                printf("Transfersize sectors: %" PRIu16 "\n", transferSizeSectors);
                printf("Transfersize bytes: %" PRIu32 "\tMaxXferSize: %" PRIu32 "\n", (uint32_t)(transferSizeSectors * LEGACY_DRIVE_SEC_SIZE), scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize);
                printf("Transfersize sectors %% alignment: %" PRIu32 "\n", ((uint32_t)(transferSizeSectors * LEGACY_DRIVE_SEC_SIZE) % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment));
#endif
                if ((uint32_t)(transferSizeSectors * LEGACY_DRIVE_SEC_SIZE) < scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize && ((uint32_t)(transferSizeSectors * LEGACY_DRIVE_SEC_SIZE) % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment == 0))
                {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                    printf("\tTrue (0x0E)\n");
#endif
                    return true;
                }
            }
            else if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
            {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                printf("\tTrue (0x0F)\n");
#endif
                return true;
            }
        }
    }
    else if(scsiIoCtx)//sending a SCSI command
    {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
        printf("Checking SCSI command info for FWDL Support\n");
#endif
        //TODO? Should we check that this is a SCSI Drive? Right now we'll just attempt the download and let the drive/SATL handle translation
        //check that it's a write buffer command for a firmware download & it's a deferred download command since that is all that is supported
        if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
        {
            uint8_t wbMode = M_GETBITRANGE(scsiIoCtx->cdb[1], 4, 0);
            uint32_t transferLength = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            switch (wbMode)
            {
            case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER:
                if (transferLength < scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize && (transferLength % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment == 0))
                {
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                    printf("\tTrue (SCSI Mode 0x0E)\n");
#endif
                    return true;
                }
                break;
            case SCSI_WB_ACTIVATE_DEFERRED_MICROCODE:
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
                printf("\tTrue (SCSI Mode 0x0F)\n");
#endif
                return true;
                break;
            default:
                break;
            }
        }
    }
#if defined (_DEBUG_FWDL_API_COMPATABILITY)
    printf("\tFalse\n");
#endif
    return false;
}

//TODO: handle more than 1 firmware slot per device.-TJE
int get_Windows_FWDL_IO_Support(tDevice *device, STORAGE_BUS_TYPE busType)
{
    int ret = NOT_SUPPORTED;
    STORAGE_HW_FIRMWARE_INFO_QUERY fwdlInfo;
    memset(&fwdlInfo, 0, sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY));
    fwdlInfo.Version = sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY);
    fwdlInfo.Size = sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY);
    uint8_t slotCount = 1;
    if (busType == BusTypeNvme)
    {
        slotCount = 7;//Max of 7 firmware slots on NVMe...might as well read in everything even if we aren't using it today.-TJE
    }
    uint32_t outputDataSize = sizeof(STORAGE_HW_FIRMWARE_INFO) + (sizeof(STORAGE_HW_FIRMWARE_SLOT_INFO) * slotCount);
    uint8_t *outputData = (uint8_t*)malloc(outputDataSize);
    if (!outputData)
    {
        return MEMORY_FAILURE;
    }
    memset(outputData, 0, outputDataSize);
    DWORD returned_data = 0;
    //STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER is needed for NVMe to report relavant data. Without it, we only see 1 slot available.
    if (busType == BusTypeNvme)
    {
        fwdlInfo.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
    }
    int fwdlRet = DeviceIoControl(device->os_info.fd,
        IOCTL_STORAGE_FIRMWARE_GET_INFO,
        &fwdlInfo,
        sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY),
        outputData,
        outputDataSize,
        &returned_data,
        NULL);
    //Got the version info, but that doesn't mean we'll be successful with commands...
    if (fwdlRet)
    {
        PSTORAGE_HW_FIRMWARE_INFO fwdlSupportedInfo = (PSTORAGE_HW_FIRMWARE_INFO)outputData;
        device->os_info.fwdlIOsupport.fwdlIOSupported = fwdlSupportedInfo->SupportUpgrade;
        device->os_info.fwdlIOsupport.payloadAlignment = fwdlSupportedInfo->ImagePayloadAlignment;
        device->os_info.fwdlIOsupport.maxXferSize = fwdlSupportedInfo->ImagePayloadMaxSize;
        //TODO: store more FWDL information as we need it
#if defined (_DEBUG)
        printf("Got Win10 FWDL Info\n");
        printf("\tSupported: %d\n", fwdlSupportedInfo->SupportUpgrade);
        printf("\tPayload Alignment: %d\n", fwdlSupportedInfo->ImagePayloadAlignment);
        printf("\tmaxXferSize: %d\n", fwdlSupportedInfo->ImagePayloadMaxSize);
        printf("\tPendingActivate: %d\n", fwdlSupportedInfo->PendingActivateSlot);
        printf("\tActiveSlot: %d\n", fwdlSupportedInfo->ActiveSlot);
        printf("\tSlot Count: %d\n", fwdlSupportedInfo->SlotCount);
        printf("\tFirmware Shared: %d\n", fwdlSupportedInfo->FirmwareShared);
        //print out what's in the slots!
        for (uint8_t iter = 0; iter < fwdlSupportedInfo->SlotCount && iter < slotCount; ++iter)
        {
            printf("\t    Firmware Slot %d:\n", fwdlSupportedInfo->Slot[iter].SlotNumber);
            printf("\t\tRead Only: %d\n", fwdlSupportedInfo->Slot[iter].ReadOnly);
            printf("\t\tRevision: %s\n", fwdlSupportedInfo->Slot[iter].Revision);
        }
#endif
        ret = SUCCESS;
    }
    else
    {
        DWORD lastError = GetLastError();
        ret = FAILURE;
    }
    safe_Free(outputData);
    return ret;
}

bool is_Activate_Command(ScsiIoCtx *scsiIoCtx)
{
    bool isActivate = false;
    if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE || scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA))
    {
        //check the subcommand (feature)
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
        {
            isActivate = true;
        }
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
    {
        //it's a write buffer command, so we need to also check the mode.
        uint8_t wbMode = M_GETBITRANGE(scsiIoCtx->cdb[1], 4, 0);
        switch (wbMode)
        {
        case 0x0F:
            isActivate = true;
            break;
        default:
            break;
        }
    }
    return isActivate;
}

int win10_FW_Activate_IO_SCSI(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        printf("Sending firmware activate with Win10 API\n");
    }
    //send the activate IOCTL
    STORAGE_HW_FIRMWARE_ACTIVATE downloadActivate;
    memset(&downloadActivate, 0, sizeof(STORAGE_HW_FIRMWARE_ACTIVATE));
    downloadActivate.Version = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    downloadActivate.Size = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    //downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE;
    if (scsiIoCtx && !scsiIoCtx->pAtaCmdOpts)
    {
        downloadActivate.Slot = scsiIoCtx->cdb[2];//Set the slot number to the buffer ID number...This is the closest this translates.
    }
    if (scsiIoCtx->device->drive_info.interface_type == NVME_INTERFACE)
    {
        //if we are on NVMe, but the command comes to here, then someone forced SCSI mode, so let's set this flag correctly
        downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
    }
    DWORD returned_data = 0;
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(scsiIoCtx->device->os_info.fd,
        IOCTL_STORAGE_FIRMWARE_ACTIVATE,
        &downloadActivate,
        sizeof(STORAGE_HW_FIRMWARE_ACTIVATE),
        NULL,
        0,
        &returned_data,
        &overlappedStruct
    );
    scsiIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        scsiIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    //dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
        if (scsiIoCtx->pAtaCmdOpts)
        {
            //set status register to 50
            memset(&scsiIoCtx->pAtaCmdOpts->rtfr, 0, sizeof(ataReturnTFRs));
            scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
            scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x02;//This is supposed to be set when the drive has applied the new code.
            //also set sense data with an ATA passthrough return descriptor
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
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;// Error
                scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt;// Sector Count
                scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow;// LBA Lo
                scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid;// LBA Mid
                scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;// LBA Hi
                scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device;// Device/Head
                scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status;// Status
            }
        }
    }
    else
    {
        switch (scsiIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE://aborted command is the best we can do
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            if (scsiIoCtx->pAtaCmdOpts)
            {
                memset(&scsiIoCtx->pAtaCmdOpts->rtfr, 0, sizeof(ataReturnTFRs));
                scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ABORT;
                //we need to also set sense data that matches...
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
                    //fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;// Error
                    scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt;// Sector Count
                    scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow;// LBA Lo
                    scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid;// LBA Mid
                    scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;// LBA Hi
                    scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device;// Device/Head
                    scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status;// Status
                }
            }
            else
            {
                //setting fixed format...
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
                scsiIoCtx->psense[2] = SENSE_KEY_ABORTED_COMMAND;
                scsiIoCtx->psense[7] = 7;//set so that ASC, ASCQ, & FRU are available...even though they are zeros
            }
            break;
        case ERROR_INVALID_FUNCTION:
            //disable the support bits for Win10 FWDL API.
            //The driver said it's supported, but when we try to issue the commands it fails with this status, so try pass-through as we would otherwise use.
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Win 10 FWDL API returned invalid function, retrying with passthrough\n");
            }
            scsiIoCtx->device->os_info.fwdlIOsupport.fwdlIOSupported = false;
            return send_IO(scsiIoCtx);
            break;
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
        if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
        }
    }
    return ret;
}
//DO NOT Attempt to use the STORAGE_HW_FIRMWARE_DOWNLOAD_V2 structure! This is not compatible as the low-level driver has a hard-coded alignment for the image buffer and will not transmit the correct data!!!
int win10_FW_Download_IO_SCSI(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    uint32_t dataLength = 0;
    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        printf("Sending deferred download with Win10 API\n");
    }
    if (scsiIoCtx->pAtaCmdOpts)
    {
        dataLength = scsiIoCtx->pAtaCmdOpts->dataSize;
    }
    else
    {
        dataLength = scsiIoCtx->dataLength;
    }
    //send download IOCTL
    DWORD downloadStructureSize = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD) + dataLength;
    PSTORAGE_HW_FIRMWARE_DOWNLOAD downloadIO = (PSTORAGE_HW_FIRMWARE_DOWNLOAD)malloc(downloadStructureSize);
    if (!downloadIO)
    {
        return MEMORY_FAILURE;
    }
    memset(downloadIO, 0, downloadStructureSize);
    downloadIO->Version = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD);
    downloadIO->Size = downloadStructureSize;
#if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    if (scsiIoCtx->device->os_info.fwdlIOsupport.isLastSegmentOfDownload)
    {
        //This IS documented on MSDN but VS2015 can't seem to find it...
        //One website says that this flag is new in Win10 1704 - creators update (10.0.15021)
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
    }
#endif
#if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    if (scsiIoCtx->device->os_info.fwdlIOsupport.isFirstSegmentOfDownload)
    {
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
    }
#endif
    if (scsiIoCtx->device->drive_info.interface_type == NVME_INTERFACE)
    {
        //if we are on NVMe, but the command comes to here, then someone forced SCSI mode, so let's set this flag correctly
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
    }
    if (scsiIoCtx && !scsiIoCtx->pAtaCmdOpts)
    {
        downloadIO->Slot = scsiIoCtx->cdb[2];//Set the slot number to the buffer ID number...This is the closest this translates.
    }
    //we need to set the offset since MS uses this in the command sent to the device.
    downloadIO->Offset = 0;//TODO: Make sure this works even though the buffer pointer is only the current segment!
    if (scsiIoCtx && scsiIoCtx->pAtaCmdOpts)
    {
        //get offset from the tfrs
        downloadIO->Offset = M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaHi, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid) * LEGACY_DRIVE_SEC_SIZE;
    }
    else if (scsiIoCtx)
    {
        //get offset from the cdb
        downloadIO->Offset = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    }
    else
    {
        safe_Free(downloadIO);
        return BAD_PARAMETER;
    }
    //set the size of the buffer
    downloadIO->BufferSize = dataLength;
    //now copy the buffer into this IOCTL struct
    memcpy(downloadIO->ImageBuffer, scsiIoCtx->pdata, dataLength);
    //time to issue the IO
    DWORD returned_data = 0;
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(scsiIoCtx->device->os_info.fd,
        IOCTL_STORAGE_FIRMWARE_DOWNLOAD,
        downloadIO,
        downloadStructureSize,
        NULL,
        0,
        &returned_data,
        &overlappedStruct
    );
    scsiIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        scsiIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    //dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
        if (scsiIoCtx->pAtaCmdOpts)
        {
            //set status register to 50
            memset(&scsiIoCtx->pAtaCmdOpts->rtfr, 0, sizeof(ataReturnTFRs));
            scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
            if (scsiIoCtx->device->os_info.fwdlIOsupport.isLastSegmentOfDownload)
            {
                scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x03;//device has all segments saved and is ready to activate
            }
            else
            {
                scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x01;//device is expecting more code
            }
            //also set sense data with an ATA passthrough return descriptor
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
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;// Error
                scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt;// Sector Count
                scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow;// LBA Lo
                scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid;// LBA Mid
                scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;// LBA Hi
                scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device;// Device/Head
                scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status;// Status
            }
        }
    }
    else
    {
        switch (scsiIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE://aborted command is the best we can do
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            if (scsiIoCtx->pAtaCmdOpts)
            {
                memset(&scsiIoCtx->pAtaCmdOpts->rtfr, 0, sizeof(ataReturnTFRs));
                scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ABORT;
                //we need to also set sense data that matches...
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
                    //fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;// Error
                    scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt;// Sector Count
                    scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow;// LBA Lo
                    scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid;// LBA Mid
                    scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;// LBA Hi
                    scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device;// Device/Head
                    scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status;// Status
                }
            }
            else
            {
                //setting fixed format...
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
                scsiIoCtx->psense[2] = SENSE_KEY_ABORTED_COMMAND;
                scsiIoCtx->psense[7] = 7;//set so that ASC, ASCQ, & FRU are available...even though they are zeros
            }
            break;
        case ERROR_INVALID_FUNCTION:
            //disable the support bits for Win10 FWDL API.
            //The driver said it's supported, but when we try to issue the commands it fails with this status, so try pass-through as we would otherwise use.
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Win 10 FWDL API returned invalid function, retrying with passthrough\n");
            }
            scsiIoCtx->device->os_info.fwdlIOsupport.fwdlIOSupported = false;
            return send_IO(scsiIoCtx);
            break;
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
        if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
        }
    }
    return ret;
}

//call check function above to make sure this api call will actually work...
int windows_Firmware_Download_IO_SCSI(ScsiIoCtx *scsiIoCtx)
{
    if (!scsiIoCtx)
    {
        return BAD_PARAMETER;
    }
    if (is_Activate_Command(scsiIoCtx))
    {
        return win10_FW_Activate_IO_SCSI(scsiIoCtx);
    }
    else
    {
        return win10_FW_Download_IO_SCSI(scsiIoCtx);
    }
}
#endif

//Checks if SMART IO is supported and sets some bit flags up for later when issuing those IOs
int get_Windows_SMART_IO_Support(tDevice *device)
{
    ULONG returned_data = 0;
    GETVERSIONINPARAMS smartVersionInfo;
    memset(&smartVersionInfo, 0, sizeof(GETVERSIONINPARAMS));
    int smartRet = DeviceIoControl(device->os_info.fd,
        SMART_GET_VERSION,
        NULL,
        0,
        &smartVersionInfo,
        sizeof(GETVERSIONINPARAMS),
        &returned_data,
        NULL);
    //Got the version info, but that doesn't mean we'll be successful with commands...
    if (smartRet)
    {
        DWORD smartError = GetLastError();
        if (!smartError)//if there was an error, then assume the driver does not support this request. - TJE
        {
            if (smartVersionInfo.fCapabilities > 0)
            {
                device->os_info.winSMARTCmdSupport.smartIOSupported = true;
                if (smartVersionInfo.fCapabilities & CAP_ATA_ID_CMD)
                {
                    device->os_info.winSMARTCmdSupport.ataIDsupported = true;
                }
                if (smartVersionInfo.fCapabilities & CAP_ATAPI_ID_CMD)
                {
                    device->os_info.winSMARTCmdSupport.atapiIDsupported = true;
                }
                if (smartVersionInfo.fCapabilities & CAP_SMART_CMD)
                {
                    device->os_info.winSMARTCmdSupport.smartSupported = true;
                }
                //TODO: Save driver version info? skipping for now since it doesn't appear useful.-TJE
                device->os_info.winSMARTCmdSupport.deviceBitmap = smartVersionInfo.bIDEDeviceMap;
                if (smartVersionInfo.bIDEDeviceMap & (BIT1 | BIT3 | BIT5 | BIT7))
                {
                    device->drive_info.ata_Options.isDevice1 = true;
                }
            }
        }
    }
    return SUCCESS;
}

#define INVALID_IOCTL 0xFFFFFFFF
//returns which IOCTL code we'll use for the specified command
DWORD io_For_SMART_Cmd(ScsiIoCtx *scsiIoCtx)
{
    if (scsiIoCtx->pAtaCmdOpts->commandType != ATA_CMD_TYPE_TASKFILE)
    {
        return INVALID_IOCTL;
    }
    if (!scsiIoCtx->device->os_info.winSMARTCmdSupport.smartIOSupported)
    {
        return INVALID_IOCTL;
    }
    //this checks to make sure we are issuing a STD spec Identify or SMART command to the drive. Non-standard things won't be supported by the lower level drivers or OS - TJE
    switch (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus)
    {
    case ATA_IDENTIFY:
        //make sure it's standard spec identify! (no lba fields set or feature field set)
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaLow != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaMid != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaHi != 0)
        {
            return INVALID_IOCTL;
        }
        else if (scsiIoCtx->device->os_info.winSMARTCmdSupport.ataIDsupported)
        {
            return SMART_RCV_DRIVE_DATA;
        }
        else
        {
            return INVALID_IOCTL;
        }
    case ATAPI_IDENTIFY:
        //make sure it's standard spec identify! (no lba fields set or feature field set)
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaLow != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaMid != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaHi != 0)
        {
            return INVALID_IOCTL;
        }
        else if (scsiIoCtx->device->os_info.winSMARTCmdSupport.atapiIDsupported)
        {
            return SMART_RCV_DRIVE_DATA;
        }
        else
        {
            return INVALID_IOCTL;
        }
    case ATA_SMART:
        if (scsiIoCtx->device->os_info.winSMARTCmdSupport.smartSupported)
        {
            //check that the feature field matches something Microsoft documents support for...using MS defines - TJE
            switch (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature)
            {
            case READ_ATTRIBUTES:
            case READ_THRESHOLDS:
            case SMART_READ_LOG:
                return SMART_RCV_DRIVE_DATA;
            case ENABLE_DISABLE_AUTOSAVE:
            case SAVE_ATTRIBUTE_VALUES:
                return SMART_SEND_DRIVE_COMMAND;
            case EXECUTE_OFFLINE_DIAGS://need to check which test to run!
                switch (scsiIoCtx->pAtaCmdOpts->tfr.LbaLow)
                {
                case SMART_OFFLINE_ROUTINE_OFFLINE:
                case SMART_SHORT_SELFTEST_OFFLINE:
                case SMART_EXTENDED_SELFTEST_OFFLINE:
                case SMART_ABORT_OFFLINE_SELFTEST:
                case SMART_SHORT_SELFTEST_CAPTIVE:
                case SMART_EXTENDED_SELFTEST_CAPTIVE:
                    return SMART_SEND_DRIVE_COMMAND;
                default:
                    return INVALID_IOCTL;
                }
            case SMART_WRITE_LOG:
            case ENABLE_SMART:
            case DISABLE_SMART:
            case RETURN_SMART_STATUS:
            case ENABLE_DISABLE_AUTO_OFFLINE:
                return SMART_SEND_DRIVE_COMMAND;
            default:
                return INVALID_IOCTL;
            }
        }
        else
        {
            return INVALID_IOCTL;
        }
    default:
        return INVALID_IOCTL;
    }
}

bool is_ATA_Cmd_Supported_By_SMART_IO(ScsiIoCtx *scsiIoCtx)
{
    if (INVALID_IOCTL != io_For_SMART_Cmd(scsiIoCtx))
    {
        return true;
    }
    else
    {
        return false;
    }
}

int convert_SCSI_CTX_To_ATA_SMART_Cmd(ScsiIoCtx *scsiIoCtx, PSENDCMDINPARAMS smartCmd)
{
    if (!is_ATA_Cmd_Supported_By_SMART_IO(scsiIoCtx))
    {
        return BAD_PARAMETER;//or NOT_SUPPORTED? - TJE
    }
    smartCmd->cBufferSize = scsiIoCtx->dataLength;
    //set up the registers
    smartCmd->irDriveRegs.bFeaturesReg = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
    smartCmd->irDriveRegs.bSectorCountReg = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
    smartCmd->irDriveRegs.bSectorNumberReg = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
    smartCmd->irDriveRegs.bCylLowReg = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
    smartCmd->irDriveRegs.bCylHighReg = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
    smartCmd->irDriveRegs.bDriveHeadReg = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
    smartCmd->irDriveRegs.bCommandReg = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
    smartCmd->irDriveRegs.bReserved = RESERVED;

    //set device-head bitmask for the driver...see https://msdn.microsoft.com/en-us/library/windows/hardware/ff554977(v=vs.85).aspx
    if (scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT0 ||
        scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT2 ||
        scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT4 ||
        scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT6
        )
    {
        //this drive is a master...make sure bit 4 is not set!
        if (smartCmd->irDriveRegs.bDriveHeadReg & BIT4)
        {
            smartCmd->irDriveRegs.bDriveHeadReg ^= BIT4;
        }
    }
    else if (scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT1 ||
        scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT3 ||
        scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT5 ||
        scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & BIT7)
    {
        //this drive is a slave...make sure bit 4 is set
        smartCmd->irDriveRegs.bDriveHeadReg |= BIT4;
    }

    smartCmd->bDriveNumber = 0;//MSDN says not to set this to anything! https://msdn.microsoft.com/en-us/library/windows/hardware/ff565401(v=vs.85).aspx

    smartCmd->bReserved[0] = RESERVED;
    smartCmd->bReserved[1] = RESERVED;
    smartCmd->bReserved[2] = RESERVED;

    smartCmd->dwReserved[0] = RESERVED;
    smartCmd->dwReserved[1] = RESERVED;
    smartCmd->dwReserved[2] = RESERVED;
    smartCmd->dwReserved[3] = RESERVED;
    return SUCCESS;
}

int send_ATA_SMART_Cmd_IO(ScsiIoCtx *scsiIoCtx)
{
    int ret = FAILURE;
    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    BOOL success;
    ULONG returned_data = 0;
    uint32_t dataInLength = 1;//set to one for the minus 1s below
    uint32_t dataOutLength = 1;//set to one for the minus 1s below
    bool nonDataRTFRs = false;
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        dataOutLength = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
        break;
    case XFER_DATA_OUT:
        dataInLength = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
        break;
    case XFER_NO_DATA:
        dataOutLength += sizeof(IDEREGS);//need to add this much to the buffer to get the RTFRs back as the data buffer!
        nonDataRTFRs = true;
        break;
    default:
        break;
    }
    PSENDCMDINPARAMS smartIOin = (PSENDCMDINPARAMS)malloc(sizeof(SENDCMDINPARAMS) - 1 + dataInLength);
    if (!smartIOin)
    {
        //something went really wrong...
        return MEMORY_FAILURE;
    }
    PSENDCMDOUTPARAMS smartIOout = (PSENDCMDOUTPARAMS)malloc(sizeof(SENDCMDOUTPARAMS) - 1 + dataOutLength);
    if (!smartIOout)
    {
        safe_Free(smartIOin);
        //something went really wrong...
        return MEMORY_FAILURE;
    }
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    memset(smartIOin, 0, sizeof(SENDCMDINPARAMS) - 1 + dataInLength);
    memset(smartIOout, 0, sizeof(SENDCMDOUTPARAMS) - 1 + dataOutLength);
    ret = convert_SCSI_CTX_To_ATA_SMART_Cmd(scsiIoCtx, smartIOin);
    if (SUCCESS == ret)
    {
        ULONG inBufferLength = sizeof(SENDCMDINPARAMS);
        ULONG outBufferLength = sizeof(SENDCMDOUTPARAMS);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            outBufferLength += dataOutLength - 1;
            break;
        case XFER_DATA_OUT:
            //need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                memcpy(smartIOin->bBuffer, scsiIoCtx->pdata, dataInLength);
            }
            inBufferLength += dataInLength - 1;
            break;
        case XFER_NO_DATA:
            outBufferLength += sizeof(IDEREGS);
            break;
        default:
            break;
        }
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            io_For_SMART_Cmd(scsiIoCtx),//This function gets the correct IOCTL for us
            smartIOin,
            inBufferLength,
            smartIOout,
            outBufferLength,
            &returned_data,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (success)
        {
            ret = SUCCESS;
            //copy the data buffer back to the user's data pointer
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                memcpy(scsiIoCtx->pdata, smartIOout->bBuffer, M_Min(scsiIoCtx->dataLength, smartIOout->cBufferSize));
            }
            //use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_IO_DEVICE://OS_PASSTHROUGH_FAILURE
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
            scsiIoCtx->returnStatus.senseKey = 0x01;
        }
        scsiIoCtx->returnStatus.asc = 0x00;//might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D;//might need to change this later
        //get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in descriptor format
        //current = 28bit, previous = 48bit
        if (scsiIoCtx->psense != NULL)//check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >= 22)//check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                ataReturnTFRs smartTFRs;
                memset(&smartTFRs, 0, sizeof(ataReturnTFRs));
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
                //TODO: should this be checked somewhere above? or is here good?
                switch (smartIOout->DriverStatus.bDriverError)
                {
                case SMART_NO_ERROR://command issued without error
                    smartTFRs.status = 0x50;
                    if (nonDataRTFRs)//only look for RTFRs on a non-data command!
                    {
                        PIDEREGS ptrIDErtfrs = (PIDEREGS)(smartIOout->bBuffer);
                        smartTFRs.error = ptrIDErtfrs->bFeaturesReg;
                        smartTFRs.secCnt = ptrIDErtfrs->bSectorCountReg;
                        smartTFRs.lbaLow = ptrIDErtfrs->bSectorNumberReg;
                        smartTFRs.lbaMid = ptrIDErtfrs->bCylLowReg;
                        smartTFRs.lbaHi = ptrIDErtfrs->bCylHighReg;
                        smartTFRs.device = ptrIDErtfrs->bDriveHeadReg;
                        smartTFRs.status = ptrIDErtfrs->bCommandReg;
                    }
                    break;
                case SMART_IDE_ERROR://command error...status register should get set to 51h
                    smartTFRs.status = 0x51;
                    smartTFRs.error = smartIOout->DriverStatus.bIDEError;
                    //This should be sufficient since the drive returned some other type of command abort...BUT we may want to attempt looking for RTFRs to know more.
                    break;
                case SMART_INVALID_FLAG:
                case SMART_INVALID_COMMAND:
                case SMART_INVALID_BUFFER:
                case SMART_INVALID_DRIVE:
                case SMART_INVALID_IOCTL:
                case SMART_ERROR_NO_MEM:
                case SMART_INVALID_REGISTER://should we return NOT_SUPPORTED here? - TJE
                case SMART_NOT_SUPPORTED:
                case SMART_NO_IDE_DEVICE:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }
                scsiIoCtx->psense[11] = smartTFRs.error;// Error
                scsiIoCtx->psense[13] = smartTFRs.secCnt;// Sector Count
                scsiIoCtx->psense[15] = smartTFRs.lbaLow;// LBA Lo
                scsiIoCtx->psense[17] = smartTFRs.lbaMid;// LBA Mid
                scsiIoCtx->psense[19] = smartTFRs.lbaHi;// LBA Hi
                scsiIoCtx->psense[20] = smartTFRs.device;// Device/Head
                scsiIoCtx->psense[21] = smartTFRs.status;// Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc = 0x20;
        scsiIoCtx->returnStatus.ascq = 0x00;
        //dummy up sense data
        if (scsiIoCtx->psense != NULL)
        {
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            //fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            //acq
            scsiIoCtx->psense[12] = 0x20;//invalid operation code
            //acsq
            scsiIoCtx->psense[13] = 0x00;
        }
    }
    else
    {
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Couldn't convert SCSI-To-IDE interface (SMART IO)\n");
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_Free(smartIOin);
    safe_Free(smartIOout);
    return ret;
}

int os_Device_Reset(tDevice *device)
{
    int ret = FAILURE;
    //this IOCTL is only supported for non-scsi devices, which includes anything (ata or scsi) attached to a USB or SCSI or SAS interface
    //TODO: Remove ATA filter and allow this to try anyways
    if (scsiIoCtx->device->drive_info.drive_type == ATA_DRIVE)
    {
        //This does not seem to work since it is obsolete and likely not implemented in modern drivers
        //use the Windows API call - http://msdn.microsoft.com/en-us/library/windows/hardware/ff560603%28v=vs.85%29.aspx
        //ULONG returned_data = 0;
        BOOL success = 0;
        SetLastError(NO_ERROR);
        device->os_info.last_error = NO_ERROR;
        success = DeviceIoControl(device->os_info.fd,
            OBSOLETE_IOCTL_STORAGE_RESET_DEVICE,
            NULL,
            0,
            NULL,
            0,
            NULL,
            FALSE);
        device->os_info.last_error = GetLastError();
        if (success && device->os_info.last_error == NO_ERROR)
        {
            ret = SUCCESS;
        }
        else
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
    }
    else
    {
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    //TODO: catch not supported versus an error
    return ret;
}

int os_Bus_Reset(tDevice *device)
{
    int ret = FAILURE;
    //This does not seem to work since it is obsolete and likely not implemented in modern drivers
    //use the Windows API call - http://msdn.microsoft.com/en-us/library/windows/hardware/ff560600%28v=vs.85%29.aspx
    ULONG returned_data = 0;
    BOOL success = 0;
    STORAGE_BUS_RESET_REQUEST reset = { 0 };
    reset.PathId = device->os_info.scsi_addr.PathId;
    SetLastError(NO_ERROR);
    device->os_info.last_error = NO_ERROR;
    success = DeviceIoControl(device->os_info.fd,
        OBSOLETE_IOCTL_STORAGE_RESET_BUS,
        &reset,
        sizeof(reset),
        &reset,
        sizeof(reset),
        &returned_data,
        FALSE);
    device->os_info.last_error = GetLastError();
    if (success && device->os_info.last_error == NO_ERROR)
    {
        ret = SUCCESS;
    }
    else
    {
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    //TODO: catch not supported versus an error
    return ret;
}

int os_Controller_Reset(tDevice *device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int send_IO( ScsiIoCtx *scsiIoCtx )
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("Sending command with send_IO\n");
    }
#if WINVER >= SEA_WIN32_WINNT_WIN10
    //TODO: We should figure out a better way to handle when to use the Windows API for these IOs than this...not sure if there should be a function called "is command in Win API" or something like that to check for it or not.-TJE
    if (is_Firmware_Download_Command_Compatible_With_Win_API(scsiIoCtx))
    {
        ret = windows_Firmware_Download_IO_SCSI(scsiIoCtx);
    }
    else
    {
#endif
        switch (scsiIoCtx->device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
            //Note: This code is problematic for ATAPI devices. Windows just passes any CDB through to the device, which may not be what we want when we want identify packet device data or other things like it.
            if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_ATA_PASSTHROUGH)
            {
                if (scsiIoCtx->pAtaCmdOpts)
                {
                    ret = send_ATA_Pass_Through_IO(scsiIoCtx);
                }
                else if (scsiIoCtx->device->drive_info.drive_type == ATAPI_DRIVE)
                {
                    //ATAPI drives talk with SCSI commands...hopefully this works! If this doesn't work, then we will need to write some other function to check the request and call the Win API. - TJE
                    ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
                }
                else
                {
                    ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
                }
            }
            else if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH || scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH_EX)//we need this else because we CAN issue scsi pass through commands to an ata interface drive; Windows returns dummied up data for things like inquiry and read capacity which we do send occasionally, so do not remove this.
            {
                if (scsiIoCtx->device->drive_info.drive_type == ATAPI_DRIVE)//ATAPI drives should just receive the CDB to do what they will with it
                {
                    //TODO: Should we check if this is a SAT ATA pass-through command?
                    ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
                }
                //else if (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_IDENTIFY)//TODO: make sure all other LBA registers are zero
                //{
                //  ret = send_Win_ATA_Identify_Cmd(scsiIoCtx);
                //}
                //else if (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_READ_LOG_EXT || scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_READ_LOG_EXT_DMA)
                //{
                //  ret = send_Win_ATA_Get_Log_Page_Cmd(scsiIoCtx);
                //}
                else if (scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_12 || scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_16)
                {
                    ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
                }
                else //not letting the low level translate other things since many of the translations are wrong or fail to report errors - TJE
                {
                    ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
                }
            }
            else if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SMART_ONLY)
            {
                if (scsiIoCtx->pAtaCmdOpts)
                {
                    ret = send_ATA_SMART_Cmd_IO(scsiIoCtx);
                }
                else
                {
                    ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
                }
            }
            else if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_IDE_PASSTHROUGH_ONLY)
            {
                if (scsiIoCtx->pAtaCmdOpts)
                {
                    ret = send_IDE_Pass_Through_IO(scsiIoCtx);
                }
                else
                {
                    ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
                }
            }
            else if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SMART_AND_IDE)
            {
                //In this case, we want to use the SMART IO over the IDE pass-through whenever we can since it has better support. - TJE
                if (scsiIoCtx->pAtaCmdOpts)
                {
                    if (is_ATA_Cmd_Supported_By_SMART_IO(scsiIoCtx))
                    {
                        ret = send_ATA_SMART_Cmd_IO(scsiIoCtx);
                    }
                    else
                    {
                        ret = send_IDE_Pass_Through_IO(scsiIoCtx);
                    }
                }
                else
                {
                    ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
                }
            }
            else
            {
                if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
                {
                    printf("Error: Unknown IOCTL type to issue ATA commands.\n");
                }
                ret = BAD_PARAMETER;
            }
            break;
        case USB_INTERFACE:
        case IEEE_1394_INTERFACE:
        case NVME_INTERFACE://for now send it as a SCSI command. Later we made need to define a different method of talking to the NVMe device
        case SCSI_INTERFACE:
            ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
            break;
        case RAID_INTERFACE:
            if (scsiIoCtx->device->issue_io != NULL)
            {
                ret = scsiIoCtx->device->issue_io(scsiIoCtx);
            }
            else
            {
                if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                {
                    printf("Raid PassThrough Interface is not supported for this device \n");
                }
                ret = NOT_SUPPORTED;
            }
            break;
        default:
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("Target Device does not have a valid interface\n");
            }
            ret = BAD_PARAMETER;
            break;
        }
#if WINVER >= SEA_WIN32_WINNT_WIN10
    }
#endif
    return ret;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)

#if WINVER >= SEA_WIN32_WINNT_WIN10
/*
    MS Windows treats specification commands different from Vendor Unique Commands.
*/
#define NVME_ERROR_ENTRY_LENGTH 64
int send_NVMe_Vendor_Unique_IO(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = SUCCESS;
    uint32_t nvmePassthroughDataSize = nvmeIoCtx->dataSize + sizeof(STORAGE_PROTOCOL_COMMAND) + STORAGE_PROTOCOL_COMMAND_LENGTH_NVME + NVME_ERROR_ENTRY_LENGTH;
    if (nvmeIoCtx->commandDirection == XFER_DATA_IN_OUT || nvmeIoCtx->commandDirection == XFER_DATA_OUT_IN)
    {
        //assuming bidirectional commands have the same amount of data transferring in each direction
        //TODO: Validate that this assumption is actually correct.
        nvmePassthroughDataSize += nvmeIoCtx->dataSize;
    }
    uint8_t *commandBuffer = (uint8_t*)_aligned_malloc(nvmePassthroughDataSize, 8);
    memset(commandBuffer, 0, nvmePassthroughDataSize);

    //Setup the storage protocol command structure.

    PSTORAGE_PROTOCOL_COMMAND protocolCommand = (PSTORAGE_PROTOCOL_COMMAND)(commandBuffer);

    protocolCommand->Version = STORAGE_PROTOCOL_STRUCTURE_VERSION;
    protocolCommand->Length = sizeof(STORAGE_PROTOCOL_COMMAND);
    protocolCommand->ProtocolType = ProtocolTypeNvme;
    protocolCommand->ReturnStatus = 0;
    protocolCommand->ErrorCode = 0;
    protocolCommand->CommandLength = STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;
    if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
    {
        protocolCommand->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_ADMIN_COMMAND;
        protocolCommand->Flags = STORAGE_PROTOCOL_COMMAND_FLAG_ADAPTER_REQUEST;
        nvmeAdminCommand *command = (nvmeAdminCommand*)&protocolCommand->Command;
        memcpy(command, &nvmeIoCtx->cmd.adminCmd, STORAGE_PROTOCOL_COMMAND_LENGTH_NVME);
    }
    else
    {
        protocolCommand->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_NVM_COMMAND;
        nvmCommand *command = (nvmCommand*)&protocolCommand->Command;
        memcpy(command, &nvmeIoCtx->cmd.nvmCmd, STORAGE_PROTOCOL_COMMAND_LENGTH_NVME);
    }

    //TODO: Save error info? Seems to be from NVMe error log
    protocolCommand->ErrorInfoLength = NVME_ERROR_ENTRY_LENGTH;
    protocolCommand->ErrorInfoOffset = FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command) + STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;

    //TODO: If we stor the error info (NVMe error log info) in this structure, we will need to adjust the data offsets below
    switch (nvmeIoCtx->commandDirection)
    {
    case XFER_DATA_IN:
        protocolCommand->DataToDeviceTransferLength = 0;
        protocolCommand->DataFromDeviceTransferLength = nvmeIoCtx->dataSize;
        protocolCommand->DataToDeviceBufferOffset = 0;
        protocolCommand->DataFromDeviceBufferOffset = protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength;
        break;
    case XFER_DATA_OUT:
        protocolCommand->DataToDeviceTransferLength = nvmeIoCtx->dataSize;
        protocolCommand->DataFromDeviceTransferLength = 0;
        protocolCommand->DataToDeviceBufferOffset = protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength;
        protocolCommand->DataFromDeviceBufferOffset = 0;
        //copy the data we're sending into this structure to send to the device
        if (nvmeIoCtx->ptrData)
        {
            memcpy(&commandBuffer[protocolCommand->DataToDeviceBufferOffset], nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
        break;
    case XFER_NO_DATA:
        protocolCommand->DataToDeviceTransferLength = 0;
        protocolCommand->DataFromDeviceTransferLength = 0;
        protocolCommand->DataToDeviceBufferOffset = 0;
        protocolCommand->DataFromDeviceBufferOffset = 0;
        break;
    default://Bi-directional transfers are not supported in NVMe right now.
        protocolCommand->DataToDeviceTransferLength = nvmeIoCtx->dataSize;
        protocolCommand->DataFromDeviceTransferLength = nvmeIoCtx->dataSize;
        protocolCommand->DataToDeviceBufferOffset = protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength;
        protocolCommand->DataFromDeviceBufferOffset = protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength + protocolCommand->DataToDeviceTransferLength;
        //copy the data we're sending into this structure to send to the device
        if (nvmeIoCtx->ptrData)
        {
            memcpy(&commandBuffer[protocolCommand->DataToDeviceBufferOffset], nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
        break;
    }

    if (nvmeIoCtx->timeout == 0)
    {
        if (nvmeIoCtx->device->drive_info.defaultTimeoutSeconds)
        {
            protocolCommand->TimeOutValue = nvmeIoCtx->device->drive_info.defaultTimeoutSeconds;
        }
        else
        {
            protocolCommand->TimeOutValue = 15;
        }
    }
    else
    {
        protocolCommand->TimeOutValue = nvmeIoCtx->timeout;
    }

    //Command has been set up, so send it!
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    nvmeIoCtx->device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    seatimer_t commandTimer;
    DWORD returned_data = 0;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    BOOL success = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_PROTOCOL_COMMAND,
        commandBuffer,
        nvmePassthroughDataSize,
        commandBuffer,
        nvmePassthroughDataSize,
        &returned_data,
        &overlappedStruct);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == nvmeIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;

    if (success)
    {
        ret = SUCCESS;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (ret == SUCCESS)
    {
        if (nvmeIoCtx->commandDirection != XFER_DATA_OUT && protocolCommand->DataFromDeviceBufferOffset != 0 && nvmeIoCtx->ptrData)
        {
            memcpy(nvmeIoCtx->ptrData, &commandBuffer[protocolCommand->DataFromDeviceBufferOffset], nvmeIoCtx->dataSize);
        }
    }

#if defined (_DEBUG)
    if (protocolCommand->ErrorInfoOffset > 0)
    {
        uint64_t errorCount = M_BytesTo8ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 7], commandBuffer[protocolCommand->ErrorInfoOffset + 6], commandBuffer[protocolCommand->ErrorInfoOffset + 5], commandBuffer[protocolCommand->ErrorInfoOffset + 4], commandBuffer[protocolCommand->ErrorInfoOffset + 3], commandBuffer[protocolCommand->ErrorInfoOffset + 2], commandBuffer[protocolCommand->ErrorInfoOffset + 1], commandBuffer[protocolCommand->ErrorInfoOffset + 0]);
        uint16_t submissionQueueID = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 9], commandBuffer[protocolCommand->ErrorInfoOffset + 8]);
        uint16_t commandID = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 11], commandBuffer[protocolCommand->ErrorInfoOffset + 10]);
        uint16_t statusField = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 13], commandBuffer[protocolCommand->ErrorInfoOffset + 12]);
        uint16_t parameterErrorLocation = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 15], commandBuffer[protocolCommand->ErrorInfoOffset + 14]);
        uint64_t lba = M_BytesTo8ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 23], commandBuffer[protocolCommand->ErrorInfoOffset + 22], commandBuffer[protocolCommand->ErrorInfoOffset + 21], commandBuffer[protocolCommand->ErrorInfoOffset + 20], commandBuffer[protocolCommand->ErrorInfoOffset + 19], commandBuffer[protocolCommand->ErrorInfoOffset + 18], commandBuffer[protocolCommand->ErrorInfoOffset + 17], commandBuffer[protocolCommand->ErrorInfoOffset + 16]);
        uint32_t nsid = M_BytesTo4ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 27], commandBuffer[protocolCommand->ErrorInfoOffset + 26], commandBuffer[protocolCommand->ErrorInfoOffset + 25], commandBuffer[protocolCommand->ErrorInfoOffset + 24]);
        uint8_t vendorSpecific = commandBuffer[protocolCommand->ErrorInfoOffset + 28];
        uint64_t commandSpecific = M_BytesTo8ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 39], commandBuffer[protocolCommand->ErrorInfoOffset + 38], commandBuffer[protocolCommand->ErrorInfoOffset + 37], commandBuffer[protocolCommand->ErrorInfoOffset + 36], commandBuffer[protocolCommand->ErrorInfoOffset + 35], commandBuffer[protocolCommand->ErrorInfoOffset + 34], commandBuffer[protocolCommand->ErrorInfoOffset + 33], commandBuffer[protocolCommand->ErrorInfoOffset + 32]);
        //TODO: This is useful for debugging but may not want it showing otherwise!!!
        if (errorCount > 0)
        {
            printf("Win 10 VU IO Error Info:\n");
            printf("\tError Count: %" PRIu64 "\n", errorCount);
            printf("\tSQID: %" PRIu16 "\n", submissionQueueID);
            printf("\tCID: %" PRIu16 "\n", commandID);
            printf("\tStatus: %" PRIu16"\n", statusField);
            printf("\tParameterErrorLocation: %" PRIu16 "\n", parameterErrorLocation);
            printf("\tLBA: %" PRIu64 "\n", lba);
            printf("\tNSID: %" PRIu32 "\n", nsid);
            printf("\tVU: %" PRIX8 "\n", vendorSpecific);
            printf("\tCommand Specific: %" PRIX64 "\n", commandSpecific);
        }
    }
#endif

    //TODO: figure out if we need to check this return status or not.
    /*switch (pNVMeWinCtx->storageProtocolCommand.ReturnStatus)
    {
    case STORAGE_PROTOCOL_STATUS_PENDING:
    case STORAGE_PROTOCOL_STATUS_SUCCESS:
    case STORAGE_PROTOCOL_STATUS_ERROR:
    case STORAGE_PROTOCOL_STATUS_INVALID_REQUEST:
    case STORAGE_PROTOCOL_STATUS_NO_DEVICE:
    case STORAGE_PROTOCOL_STATUS_BUSY:
    case STORAGE_PROTOCOL_STATUS_DATA_OVERRUN:
    case STORAGE_PROTOCOL_STATUS_INSUFFICIENT_RESOURCES:
    case STORAGE_PROTOCOL_STATUS_NOT_SUPPORTED:
    default:
    }*/
    //get the error return code
    nvmeIoCtx->commandCompletionData.commandSpecific = protocolCommand->FixedProtocolReturnData;
    nvmeIoCtx->commandCompletionData.dw0Valid = true;
    nvmeIoCtx->commandCompletionData.statusAndCID = protocolCommand->ErrorCode;
    nvmeIoCtx->commandCompletionData.dw1Valid = true;
    //TODO: do we need this error code, or do we look at the error info offset for the provided length???
    //set last command time
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    //check how long it took to set timeout error if necessary
    if (get_Seconds(commandTimer) > protocolCommand->TimeOutValue)
    {
        ret = COMMAND_TIMEOUT;
    }
    _aligned_free(commandBuffer);
    commandBuffer = NULL;
    return ret;
}

#if !defined(NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE)
#define NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE 0
#endif

#if !defined(NVME_IDENTIFY_CNS_CONTROLLER)
#define NVME_IDENTIFY_CNS_CONTROLLER 1
#endif

#if !defined (NVME_IDENTIFY_CNS_ACTIVE_NAMESPACES)
#define NVME_IDENTIFY_CNS_ACTIVE_NAMESPACES 2
#endif

void set_Namespace_ID_For_Device(tDevice *device)
{
    //This function is way more complicated than it needs to be! (Thanks MS)
    //First, we read controller identify data to see how many namespaces are supported.
    //TODO: Second, if the controller supports more than 1 namespace, we will issue a few additional commands to try and determine which one it is...
    uint8_t nvmeControllerIdentify[NVME_IDENTIFY_DATA_LEN] = { 0 };
    if (SUCCESS == nvme_Identify(device, nvmeControllerIdentify, 0, 1))
    {
        uint32_t maxNamespaces = M_BytesTo4ByteValue(nvmeControllerIdentify[519], nvmeControllerIdentify[518], nvmeControllerIdentify[517], nvmeControllerIdentify[516]);
        if (maxNamespaces > 1)
        {
            //Check if the current namespace size matches the total NVM size or not.
            //If it does match, then we only have 1 namespace in use right now.
            struct
            {
                uint64_t highPart;
                uint64_t lowPart;
            }tnvmCap;
            tnvmCap.highPart = M_BytesTo8ByteValue(nvmeControllerIdentify[295], nvmeControllerIdentify[294], nvmeControllerIdentify[293], nvmeControllerIdentify[292], nvmeControllerIdentify[291], nvmeControllerIdentify[290], nvmeControllerIdentify[289], nvmeControllerIdentify[288]);
            tnvmCap.lowPart = M_BytesTo8ByteValue(nvmeControllerIdentify[287], nvmeControllerIdentify[286], nvmeControllerIdentify[285], nvmeControllerIdentify[284], nvmeControllerIdentify[283], nvmeControllerIdentify[282], nvmeControllerIdentify[281], nvmeControllerIdentify[280]);
            uint8_t nvmeNamespaceIdentify[NVME_IDENTIFY_DATA_LEN] = { 0 };
            nvme_Identify(device, nvmeNamespaceIdentify, 0, 0);//This command SHOULD always pass, but it doesn't matter if it fails right now since the rest of this code should work regardless. - TJE
            struct
            {
                uint64_t highPart;
                uint64_t lowPart;
            }nvmCap;
            nvmCap.highPart = M_BytesTo8ByteValue(nvmeNamespaceIdentify[63], nvmeNamespaceIdentify[62], nvmeNamespaceIdentify[61], nvmeNamespaceIdentify[60], nvmeNamespaceIdentify[59], nvmeNamespaceIdentify[58], nvmeNamespaceIdentify[57], nvmeNamespaceIdentify[56]);
            nvmCap.lowPart = M_BytesTo8ByteValue(nvmeNamespaceIdentify[55], nvmeNamespaceIdentify[54], nvmeNamespaceIdentify[53], nvmeNamespaceIdentify[52], nvmeNamespaceIdentify[51], nvmeNamespaceIdentify[50], nvmeNamespaceIdentify[49], nvmeNamespaceIdentify[48]);
            //If these two capacities match, and the device supports management and attachment commands (which it SHOULD), then we are good to go...
            if (nvmCap.highPart == tnvmCap.highPart && nvmCap.lowPart == tnvmCap.lowPart && nvmeControllerIdentify[256] & BIT3)
            {
                device->drive_info.namespaceID = 1;
            }
            else
            {
                //      1. If we know the devie supports multiple namespaces, take current handle value - maxNamespaces
                //      2. Start at that handle value (or zero) and open that handle
                //      3. Check that the device MN, SN, and IEEE OUI matches the handle we had coming into here.
                //      4. If the MN matches (and/or vendor ID is "NVMe") then we can get the difference between the handle numbers to know what namespace we are talking to since Windows will enumerate them in order.
                //      NOTE: All NVMe namespaces start at 1, so we just need to make sure we take that into account.
                //      NOTE2: This code is long and ugly because other attempts by reading topology information or trying to read namespace list or other namespace identify data all failed.
                int32_t currentHandleNumber = 0;
                char currentHandleString[31] = { 0 };
                memcpy(&currentHandleString, device->os_info.name, 30);
                convert_String_To_Upper_Case(currentHandleString);
                int sscanfRet = sscanf(currentHandleString, WIN_PHYSICAL_DRIVE "%" PRId32, &currentHandleNumber);//This will get the handle value we are talking to right now...
                if (sscanfRet == 0 || sscanfRet == EOF)
                {
                    //couldn't parse the handle name for this, so stop and return
                    return;
                }

                uint32_t startHandleValue = 0;
                if ((int32_t)(currentHandleNumber - maxNamespaces) > 0)
                {
                    startHandleValue = (currentHandleNumber - maxNamespaces);
                }

                //If our start and current handle match, this is the first namespace - TJE
                if (startHandleValue == currentHandleNumber)//This SHOULD prevent us from needing to compare anything in namespace identify data and shorten the amount of code below - TJE
                {
                    device->drive_info.namespaceID = 1;
                }
                else
                {
                    //First, get some current identifying information
                    char currentModelNumber[41] = { 0 };
                    memcpy(currentModelNumber, &nvmeControllerIdentify[24], 40);
                    char currentSerialNumber[21] = { 0 };
                    memcpy(currentSerialNumber, &nvmeControllerIdentify[4], 20);
                    uint32_t currentIEEEOUI = M_BytesTo4ByteValue(0, nvmeControllerIdentify[75], nvmeControllerIdentify[74], nvmeControllerIdentify[73]);
                    uint16_t currentControllerID = M_BytesTo2ByteValue(nvmeControllerIdentify[79], nvmeControllerIdentify[78]);//only on NVMe 1.1 & higher devices - TJE
                    uint8_t currentFguid[16] = { 0 };//NVMe 1.3 & higher (when implemented)
                    memcpy(currentFguid, &nvmeControllerIdentify[112], 16);

                    bool foundAMatchingDevice = false;
                    //Now, we need to loop until we find the first device that has a matching MN & SN & IEEE OUI
                    for (uint32_t counter = startHandleValue; counter < MAX_DEVICES_TO_SCAN && (int32_t)counter <= currentHandleNumber && !foundAMatchingDevice; ++counter)
                    {
                        char device_name[40] = { 0 };
                        LPCSTR ptrDeviceName = &device_name[0];
                        sprintf(&device_name[0], WIN_PHYSICAL_DRIVE "%" PRIu32, counter);
                        //First set a device name into a buffer to open it
                        HANDLE checkHandle = CreateFileA(ptrDeviceName,
                            /* We are reverting to the GENERIC_WRITE | GENERIC_READ because
                            in the use case of a dll where multiple applications are using
                            our library, this needs to not request full access. If you suspect
                            some commands might fail (e.g. ISE/SED because of that
                            please notify the developers -MA */
                            GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                            FILE_FLAG_OVERLAPPED,
#else
                            0,
#endif
                            NULL);

                        if (checkHandle != INVALID_HANDLE_VALUE)
                        {
                            //This is a valid device!
                            //Get some device descriptor information to make sure it's an NVMe interface
                            // Now lets get device stuff
                            STORAGE_PROPERTY_QUERY      query;
                            STORAGE_DESCRIPTOR_HEADER   header;
                            DWORD returned_data = 0;
                            memset(&query, 0, sizeof(STORAGE_PROPERTY_QUERY));
                            memset(&header, 0, sizeof(STORAGE_DESCRIPTOR_HEADER));
                            query.QueryType = PropertyStandardQuery;
                            query.PropertyId = StorageDeviceProperty;
                            if (DeviceIoControl(checkHandle,
                                IOCTL_STORAGE_QUERY_PROPERTY,
                                &query,
                                sizeof(STORAGE_PROPERTY_QUERY),
                                &header,
                                sizeof(STORAGE_DESCRIPTOR_HEADER),
                                &returned_data,
                                FALSE))
                            {
                                PSTORAGE_DEVICE_DESCRIPTOR device_desc = (PSTORAGE_DEVICE_DESCRIPTOR)LocalAlloc(LPTR, header.Size);
                                if (DeviceIoControl(checkHandle,
                                    IOCTL_STORAGE_QUERY_PROPERTY,
                                    &query,
                                    sizeof(STORAGE_PROPERTY_QUERY),
                                    device_desc,
                                    header.Size,
                                    &returned_data,
                                    FALSE)
                                )
                                {
                                    if (device_desc->BusType == BusTypeNvme)
                                    {
                                        //we know it's an NVMe device, so now we need to get the controller identify data for this device and see if it matches this device or not
                                        //BOOL    result;
                                        PVOID   buffer = NULL;
                                        ULONG   bufferLength = 0;
                                        ULONG   returnedLength = 0;

                                        PSTORAGE_PROPERTY_QUERY query = NULL;
                                        PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
                                        PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

                                        //
                                        // Allocate buffer for use.
                                        //
                                        bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + NVME_IDENTIFY_DATA_LEN;
                                        buffer = malloc(bufferLength);

                                        if (buffer)
                                        {

                                            /*
                                            Initialize query data structure to get Identify Controller Data.
                                            */
                                            ZeroMemory(buffer, bufferLength);

                                            query = (PSTORAGE_PROPERTY_QUERY)buffer;
                                            protocolDataDescr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
                                            protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

                                            //Identify controller data
                                            query->PropertyId = StorageAdapterProtocolSpecificProperty;
                                            protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_CONTROLLER;
                                            query->QueryType = PropertyStandardQuery;
                                            protocolData->ProtocolType = ProtocolTypeNvme;
                                            protocolData->DataType = NVMeDataTypeIdentify;
                                            protocolData->ProtocolDataRequestSubValue = 0;
                                            protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
                                            protocolData->ProtocolDataLength = NVME_IDENTIFY_DATA_LEN;

                                            /*
                                            // Send request down.
                                            */

                                            if (DeviceIoControl(checkHandle,
                                                IOCTL_STORAGE_QUERY_PROPERTY,
                                                buffer,
                                                bufferLength,
                                                buffer,
                                                bufferLength,
                                                &returnedLength,
                                                NULL
                                                )
                                                )
                                            {
                                                //got namespace identify information for this handle we're checking.
                                                //Now compare it and see if we have a match!!!
                                                uint8_t* checkIdentifyControllerData = (char*)((PCHAR)protocolData + protocolData->ProtocolDataOffset);

                                                char checkModelNumber[41] = { 0 };
                                                memcpy(checkModelNumber, &checkIdentifyControllerData[24], 40);
                                                char checkSerialNumber[21] = { 0 };
                                                memcpy(checkSerialNumber, &checkIdentifyControllerData[4], 20);
                                                uint32_t checkIEEEOUI = M_BytesTo4ByteValue(0, checkIdentifyControllerData[75], checkIdentifyControllerData[74], checkIdentifyControllerData[73]);
                                                uint16_t checkControllerID = M_BytesTo2ByteValue(checkIdentifyControllerData[79], checkIdentifyControllerData[78]);//only on NVMe 1.1 & higher devices - TJE
                                                uint8_t checkFguid[16] = { 0 };//NVMe 1.3 & higher (when implemented)
                                                memcpy(checkFguid, &checkIdentifyControllerData[112], 16);

                                                if (checkIEEEOUI == currentIEEEOUI
                                                    &&
                                                    strcmp(checkModelNumber, currentModelNumber) == 0
                                                    &&
                                                    strcmp(checkSerialNumber, currentSerialNumber) == 0
                                                    &&
                                                    checkControllerID == currentControllerID
                                                    &&
                                                    memcmp(checkFguid, currentFguid, 16) == 0
                                                    )
                                                {
                                                    //This is the same controller for this drive!!!
                                                    //We have found a match!
                                                    //now check the difference between the handle values and we'll have the namespace number!
                                                    foundAMatchingDevice = true;
                                                    if ((currentHandleNumber - counter) > 0)
                                                    {
                                                        //We should only ever fall into here because the currentHandleNumber should always be greater than the handle we are trying to check.
                                                        device->drive_info.namespaceID = (currentHandleNumber - counter) + 1;//plus 1 since the NSID is a 1 based value
#if defined (_DEBUG)
                                                        printf("NSID was set to %" PRIu32 "\n", device->drive_info.namespaceID);
#endif
                                                    }
                                                    else
                                                    {
                                                        //This is not expected!!!
#if defined (_DEBUG)
                                                        printf("ERROR encountered while determining the NSID of this device!\n");
#endif
                                                    }
                                                }
                                            }
                                            safe_Free(buffer);
                                        }
                                    }
                                }
                                LocalFree(device_desc);
                            }
                            //else...we couldn't figure it out...just bailing since our normal get_Device code would! - TJE
                            //close the handle since we are now done with it
                            CloseHandle(checkHandle);
                        }

                    }

                    if (!foundAMatchingDevice)
                    {
                        //This LIKELY means that the device supports multiple namespaces, but is only using 1 at the moment
                        //This assumption SHOULD be fairly safe to make, but this might be a bug later - TJE
                        device->drive_info.namespaceID = 1;
                    }
                }
            }
        }
        else
        {
            device->drive_info.namespaceID = 1;
        }
    }

    return;// ret;
}

int send_Win_NVMe_Identify_Cmd(nvmeCmdCtx *nvmeIoCtx)
{
    int     ret = SUCCESS;
    BOOL    result;
    PVOID   buffer = NULL;
    ULONG   bufferLength = 0;
    ULONG   returnedLength = 0;

    PSTORAGE_PROPERTY_QUERY query = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + NVME_IDENTIFY_DATA_LEN;
    buffer = malloc(bufferLength);

    if (buffer == NULL)
    {
        #if defined (_DEBUG)
        printf("%s: allocate buffer failed, exit",__FUNCTION__);
        #endif
        return MEMORY_FAILURE;
    }

    /*
        Initialize query data structure to get Identify Controller Data.
    */
    ZeroMemory(buffer, bufferLength);

    query = (PSTORAGE_PROPERTY_QUERY)buffer;
    protocolDataDescr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
    protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

    //check that the rest of dword 10 is zero!
    if ((nvmeIoCtx->cmd.adminCmd.cdw10 >> 8) != 0)
    {
        //these bytes are reserved in NVMe 1.2 which is the highest MS supports right now. - TJE
        safe_Free(buffer);
        return OS_COMMAND_NOT_AVAILABLE;
    }

    switch (M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw10))
    {
    case 0://for the specified namespace. If nsid = UINT32_MAX it's for all namespaces
        query->PropertyId = StorageDeviceProtocolSpecificProperty;
        protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE;
        break;
    case 1://Identify controller data
        query->PropertyId = StorageAdapterProtocolSpecificProperty;
        protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_CONTROLLER;
        break;
    case 2://list of 1024 active namespace IDs
        query->PropertyId = StorageAdapterProtocolSpecificProperty;
        protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_ACTIVE_NAMESPACES;
        //NOTE: This command is documented in MSDN, but it doesn't actually work, so we are returning an error instead! - TJE
        safe_Free(buffer);
        return OS_COMMAND_NOT_AVAILABLE;
        break;
        //All values below here are added in NVMe 1.3, which MS doesn't support yet! - TJE
    case 3://list of namespace identification descriptor structures
        //namespace management cns values
    case 0x10://list of up to 1024 namespace IDs with namespace identifier greater than one specified in NSID
    case 0x11://identify namespace data for NSID specified
    case 0x12://list of up to 2047 controller identifiers greater than or equal to the value specified in CDW10.CNTID. List contains controller identifiers that are attached to the namespace specified
    case 0x13://list of up to 2047 controller identifiers greater than or equal to the value specified in CDW10.CNTID. List contains controller identifiers that that may or may not be attached to namespaces
    case 0x14://primary controller capabilities
    case 0x15://secondary controller list
    default:
        safe_Free(buffer);
        return OS_COMMAND_NOT_AVAILABLE;
    }

    query->QueryType = PropertyStandardQuery;

    protocolData->ProtocolType = ProtocolTypeNvme;
    protocolData->DataType = NVMeDataTypeIdentify;
    protocolData->ProtocolDataRequestSubValue = 0;
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = NVME_IDENTIFY_DATA_LEN;

    /*
    // Send request down.
    */
    #if defined (_DEBUG)
    printf("%s: Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
    #endif

    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    result = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_QUERY_PROPERTY,
        buffer,
        bufferLength,
        buffer,
        bufferLength,
        &returnedLength,
        NULL
    );
    stop_Timer(&commandTimer);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (result == 0)
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        ret = OS_PASSTHROUGH_FAILURE;
    }
    else
    {
        char* identifyControllerData = (char*)((PCHAR)protocolData + protocolData->ProtocolDataOffset);
        memcpy(nvmeIoCtx->ptrData, identifyControllerData, nvmeIoCtx->dataSize);
    }

    safe_Free(buffer);

    return ret;
}

int send_Win_NVMe_Get_Log_Page_Cmd(nvmeCmdCtx *nvmeIoCtx)
{
    int32_t returnValue = SUCCESS;
    BOOL    result;
    PVOID   buffer = NULL;
    ULONG   bufferLength = 0;
    ULONG   returnedLength = 0;

    PSTORAGE_PROPERTY_QUERY query = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + nvmeIoCtx->dataSize;
    buffer = malloc(bufferLength);

    if (buffer == NULL) {
#if defined (_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query = (PSTORAGE_PROPERTY_QUERY)buffer;
    protocolDataDescr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
    protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

    query->PropertyId = StorageAdapterProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;

    protocolData->ProtocolType = ProtocolTypeNvme;
    protocolData->DataType = NVMeDataTypeLogPage;
    protocolData->ProtocolDataRequestValue = nvmeIoCtx->cmd.adminCmd.cdw10 & 0x000000FF;
    protocolData->ProtocolDataRequestSubValue = M_Nibble2(nvmeIoCtx->cmd.adminCmd.cdw10);//bits 11:08 log page specific
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = nvmeIoCtx->dataSize;

    //
    // Send request down.
    //
#if defined (_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
#endif
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    result = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_QUERY_PROPERTY,
        buffer,
        bufferLength,
        buffer,
        bufferLength,
        &returnedLength,
        NULL
    );
    stop_Timer(&commandTimer);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (!result || (returnedLength == 0))
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        returnValue = OS_PASSTHROUGH_FAILURE;
    }
    else
    {
        //
        // Validate the returned data.
        //
        if ((protocolDataDescr->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) ||
            (protocolDataDescr->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)))
        {
            #if defined (_DEBUG)
            printf("%s: Error Log - data descriptor header not valid\n", __FUNCTION__);
            #endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < nvmeIoCtx->dataSize))
        {
            #if defined (_DEBUG)
            printf("%s: Error Log - ProtocolData Offset/Length not valid\n", __FUNCTION__);
            #endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        uint8_t* logData = (uint8_t*)((PCHAR)protocolData + protocolData->ProtocolDataOffset);
        if (nvmeIoCtx->ptrData && protocolData->ProtocolDataLength > 0)
        {
            memcpy(nvmeIoCtx->ptrData, logData, M_Min(protocolData->ProtocolDataLength, nvmeIoCtx->dataSize));
        }
        nvmeIoCtx->commandCompletionData.commandSpecific = protocolData->FixedProtocolReturnData;//This should only be DWORD 0
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }

    free(buffer);

    return returnValue;
}

int send_Win_NVMe_Get_Features_Cmd(nvmeCmdCtx *nvmeIoCtx)
{
    int32_t returnValue = SUCCESS;
    BOOL    result;
    PVOID   buffer = NULL;
    ULONG   bufferLength = 0;
    ULONG   returnedLength = 0;

    PSTORAGE_PROPERTY_QUERY query = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + nvmeIoCtx->dataSize;
    buffer = malloc(bufferLength);

    if (buffer == NULL) {
#if defined (_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query = (PSTORAGE_PROPERTY_QUERY)buffer;
    protocolDataDescr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
    protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

    query->PropertyId = StorageAdapterProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;

    protocolData->ProtocolType = ProtocolTypeNvme;
    protocolData->DataType = NVMeDataTypeFeature;
    protocolData->ProtocolDataRequestValue = M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw10);
    protocolData->ProtocolDataRequestSubValue = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 10, 8);//Examples show this as set to zero...I'll try setting this to the "select" field value...0 does get current info, which is probably what is wanted most of the time.
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = nvmeIoCtx->dataSize;

    //
    // Send request down.
    //
#if defined (_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
#endif
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    result = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_QUERY_PROPERTY,
        buffer,
        bufferLength,
        buffer,
        bufferLength,
        &returnedLength,
        NULL
    );
    start_Timer(&commandTimer);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (!result || (returnedLength == 0))
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        returnValue = OS_PASSTHROUGH_FAILURE;
    }
    else
    {
        //
        // Validate the returned data.
        //
        if ((protocolDataDescr->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) ||
            (protocolDataDescr->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)))
        {
#if defined (_DEBUG)
            printf("%s: Error Feature - data descriptor header not valid\n", __FUNCTION__);
#endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < nvmeIoCtx->dataSize))
        {
#if defined (_DEBUG)
            printf("%s: Error Feature - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        uint8_t* featData = (uint8_t*)((PCHAR)protocolData + protocolData->ProtocolDataOffset);
        if (nvmeIoCtx->ptrData && protocolData->ProtocolDataLength > 0)
        {
            memcpy(nvmeIoCtx->ptrData, featData, M_Min(nvmeIoCtx->dataSize, protocolData->ProtocolDataLength));
        }
        nvmeIoCtx->commandCompletionData.commandSpecific = protocolData->FixedProtocolReturnData;//This should only be DWORD 0 on a get features command anyways...
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }

    safe_Free(buffer);

    return returnValue;
}

int send_Win_NVMe_Firmware_Activate_Command(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
#if defined (_DEBUG)
    printf("%s: -->\n", __FUNCTION__);
#endif
    //send the activate IOCTL
    STORAGE_HW_FIRMWARE_ACTIVATE downloadActivate;
    memset(&downloadActivate, 0, sizeof(STORAGE_HW_FIRMWARE_ACTIVATE));
    downloadActivate.Version = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    downloadActivate.Size = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    uint8_t activateAction = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 5, 3);
    downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;//this command must go to the controller, not the namespace
    if (activateAction == NVME_CA_ACTIVITE_ON_RST || activateAction == NVME_CA_ACTIVITE_IMMEDIATE)//check the activate action
    {
        //Activate actions 2, & 3 sound like the closest match to this flag. Each of these requests switching to the a firmware already on the drive.
        //Activate action 0 & 1 say to replace a firmware image in a specified slot (and to or not to activate).
        downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE;
    }
    downloadActivate.Slot = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0);
#if defined (_DEBUG)
    printf("%s: downloadActivate->Version=%d\n\t->Size=%d\n\t->Flags=0x%X\n\t->Slot=%d\n",\
        __FUNCTION__, downloadActivate.Version,downloadActivate.Size, downloadActivate.Flags, downloadActivate.Slot);
#endif
    DWORD returned_data = 0;
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_FIRMWARE_ACTIVATE,
        &downloadActivate,
        sizeof(STORAGE_HW_FIRMWARE_ACTIVATE),
        NULL,
        0,
        &returned_data,
        &overlappedStruct
    );
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == nvmeIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
#if defined (_DEBUG)
    printf("%s: nvmeIoCtx->device->os_info.last_error=%d(0x%x)\n", \
        __FUNCTION__, nvmeIoCtx->device->os_info.last_error, nvmeIoCtx->device->os_info.last_error);
#endif
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    //dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        //TODO: We need to figure out what error codes Windows will return and how to dummy up the return value to match - TJE
        switch (nvmeIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE:
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }
#if defined (_DEBUG)
    printf("%s: <-- (ret=%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

//uncomment this flag to switch to force using the older structure if we need to.
//#define DISABLE_FWDL_V2 1

int send_Win_NVMe_Firmware_Image_Download_Command(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
#if defined (_DEBUG)
    printf("%s: -->\n", __FUNCTION__);
#endif
    //send download IOCTL
#if defined (WIN_API_TARGET_VERSION) && !defined (DISABLE_FWDL_V2) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    DWORD downloadStructureSize = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2) + nvmeIoCtx->dataSize;
    PSTORAGE_HW_FIRMWARE_DOWNLOAD_V2 downloadIO = (PSTORAGE_HW_FIRMWARE_DOWNLOAD_V2)malloc(downloadStructureSize);
#if defined (_DEBUG)
    printf("%s: sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2)=%zu+%d=%d\n", \
        __FUNCTION__, sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2), nvmeIoCtx->dataSize, downloadStructureSize);
#endif
#else
    DWORD downloadStructureSize = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD) + nvmeIoCtx->dataSize;
    PSTORAGE_HW_FIRMWARE_DOWNLOAD downloadIO = (PSTORAGE_HW_FIRMWARE_DOWNLOAD)malloc(downloadStructureSize);
#if defined (_DEBUG)
    printf("%s: sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD)=%zu\n", __FUNCTION__, sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD));
#endif
#endif
    if (!downloadIO)
    {
        return MEMORY_FAILURE;
    }
    memset(downloadIO, 0, downloadStructureSize);
#if defined (WIN_API_TARGET_VERSION) && !defined (DISABLE_FWDL_V2) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    downloadIO->Version = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2);
#else
    downloadIO->Version = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD);
#endif
    downloadIO->Size = downloadStructureSize;
    downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
#if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    if (nvmeIoCtx->device->os_info.fwdlIOsupport.isLastSegmentOfDownload)
    {
        //This IS documented on MSDN but VS2015 can't seem to find it...
        //One website says that this flag is new in Win10 1704 - creators update (10.0.15021)
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
    }
#endif
#if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    if (nvmeIoCtx->device->os_info.fwdlIOsupport.isFirstSegmentOfDownload)
    {
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
    }
#endif
    //TODO: add firmware slot number?
    downloadIO->Slot = 0;// M_GETBITRANGE(nvmeIoCtx->cmd, 1, 0);
    //we need to set the offset since MS uses this in the command sent to the device.
    downloadIO->Offset = (uint64_t)((uint64_t)nvmeIoCtx->cmd.adminCmd.cdw11 << 2);//convert #DWords to bytes for offset
    //set the size of the buffer
    downloadIO->BufferSize = nvmeIoCtx->dataSize;
#if defined (WIN_API_TARGET_VERSION) && !defined (DISABLE_FWDL_V2) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    downloadIO->ImageSize = nvmeIoCtx->dataSize;
#endif
    //now copy the buffer into this IOCTL struct
    memcpy(downloadIO->ImageBuffer, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);

#if defined (_DEBUG)
    printf("%s: downloadIO\n\t->Version=%d\n\t->Size=%d\n\t->Flags=0x%X\n\t->Slot=%d\n\t->Offset=0x%llX\n\t->BufferSize=0x%llX\n", \
        __FUNCTION__, downloadIO->Version, downloadIO->Size, downloadIO->Flags, downloadIO->Slot, downloadIO->Offset, downloadIO->BufferSize);
    //print_Data_Buffer(downloadIO->ImageBuffer, (downloadStructureSize - FIELD_OFFSET(STORAGE_HW_FIRMWARE_DOWNLOAD, ImageBuffer)), false);
#endif

    //time to issue the IO
    DWORD returned_data = 0;
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_FIRMWARE_DOWNLOAD,
        downloadIO,
        downloadStructureSize,
        NULL,
        0,
        &returned_data,
        &overlappedStruct
    );
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == nvmeIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
#if defined (_DEBUG)
    printf("%s: nvmeIoCtx->device->os_info.last_error=%d(0x%x)\n", \
        __FUNCTION__, nvmeIoCtx->device->os_info.last_error, nvmeIoCtx->device->os_info.last_error);
#endif
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    //dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        //TODO: We need to figure out what error codes Windows will return and how to dummy up the return value to match - TJE
        switch (nvmeIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE://aborted command is the best we can do
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }
#if defined (_DEBUG)
    printf("%s: <-- (ret=%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

int win10_Translate_Security_Send(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //Windows API call does not exist...need to issue a SCSI IO and let the driver translate it for us...how silly
    if (M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 0) == 0)//check that the nvme specific field isn't set since we can't issue that
    {
        //turn verbosity to silent since we don't need to see everything from issueing the scsi io...purpose right now is to make it look like an NVM io and be transparent to the caller.
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        //TODO: Should we add some of our own verbosity output here???
        ret = scsi_SecurityProtocol_Out(nvmeIoCtx->device, M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 24), M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 23, 8), false, nvmeIoCtx->cmd.adminCmd.cdw11, nvmeIoCtx->ptrData, 0);
        //command completed, so turn verbosity back to what it was
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

int win10_Translate_Security_Receive(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //Windows API call does not exist...need to issue a SCSI IO and let the driver translate it for us...how silly
    if (M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 0) == 0)//check that the nvme specific field isn't set since we can't issue that
    {
        //turn verbosity to silent since we don't need to see everything from issueing the scsi io...purpose right now is to make it look like an NVM io and be transparent to the caller.
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        //TODO: Should we add some of our own verbosity output here???
        ret = scsi_SecurityProtocol_In(nvmeIoCtx->device, M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 24), M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 23, 8), false, nvmeIoCtx->cmd.adminCmd.cdw11, nvmeIoCtx->ptrData);
        //command completed, so turn verbosity back to what it was
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

int win10_Translate_Set_Error_Recovery_Time_Limit(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    bool dulbe = nvmeIoCtx->cmd.adminCmd.cdw11 & BIT16;
    uint16_t nvmTimeLimitedErrorRecovery = M_BytesTo2ByteValue(M_Byte1(nvmeIoCtx->cmd.adminCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw11));
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (!dulbe && !(nvmeIoCtx->cmd.adminCmd.cdw11 >> 16))//make sure unsupported fields aren't set!!!
    {
        //use read-write error recovery MP - recovery time limit field
        uint8_t *errorRecoveryMP = (uint8_t*)calloc_aligned(MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN, sizeof(uint8_t), nvmeIoCtx->device->os_info.minimumAlignment);
        if (errorRecoveryMP)
        {
            //first, read the page into memory
            if (SUCCESS == scsi_Mode_Sense_10(nvmeIoCtx->device, MP_READ_WRITE_ERROR_RECOVERY, MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN, 0, true, false, MPC_CURRENT_VALUES, errorRecoveryMP))
            {
                //modify the recovery time limit field
                errorRecoveryMP[MODE_HEADER_LENGTH10 + 10] = M_Byte1(nvmTimeLimitedErrorRecovery);
                errorRecoveryMP[MODE_HEADER_LENGTH10 + 11] = M_Byte0(nvmTimeLimitedErrorRecovery);
                //send it back to the drive
                ret = scsi_Mode_Select_10(nvmeIoCtx->device, MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN, true, false, false, errorRecoveryMP, MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN);
            }
            safe_Free_aligned(errorRecoveryMP);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Set_Volatile_Write_Cache(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    bool wce = nvmeIoCtx->cmd.adminCmd.cdw11 & BIT0;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (!(nvmeIoCtx->cmd.adminCmd.cdw11 >> 31))//make sure unsupported fields aren't set!!!
    {
        //use caching MP - write back cache enabled field
        uint8_t *cachingMP = (uint8_t*)calloc_aligned(MODE_HEADER_LENGTH10 + MP_CACHING_LEN, sizeof(uint8_t), nvmeIoCtx->device->os_info.minimumAlignment);
        if (cachingMP)
        {
            //first, read the page into memory
            if (SUCCESS == scsi_Mode_Sense_10(nvmeIoCtx->device, MP_CACHING, MODE_HEADER_LENGTH10 + MP_CACHING_LEN, 0, true, false, MPC_CURRENT_VALUES, cachingMP))
            {
                //modify the wce field
                if (wce)
                {
                    cachingMP[MODE_HEADER_LENGTH10 + 2] |= BIT2;
                }
                else
                {
                    if (cachingMP[MODE_HEADER_LENGTH10 + 2] & BIT2)//check if the bit is already set
                    {
                        cachingMP[MODE_HEADER_LENGTH10 + 2] ^= BIT2;//turn the bit off with xor
                    }
                }
                //send it back to the drive
                ret = scsi_Mode_Select_10(nvmeIoCtx->device, MODE_HEADER_LENGTH10 + MP_CACHING_LEN, true, false, false, cachingMP, MODE_HEADER_LENGTH10 + MP_CACHING_LEN);
            }
            safe_Free_aligned(cachingMP);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Set_Power_Management(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    uint8_t workloadHint = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw11, 7, 5);
    uint8_t powerState = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw11, 4, 0);
    if (workloadHint == 0 && M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw11, 31, 8) == 0)//cannot send workload hints in the API calls available, also filtering out reserved bits
    {
        //start-stop unit command? Or maybe IOCTL_STORAGE_DEVICE_POWER_CAP
        //I will use the IOCTL_STORAGE_DEVICE_POWER_CAP for this...-TJE

        //we also need to return an error if the requested power state is not supported since the call we make may not be able to tell us that, or may round it to something close
        //power states start at byte 2079 in the controller identify data...
        double maxPowerScalar = 0.01;
        if (nvmeIoCtx->device->drive_info.IdentifyData.nvme.ctrl.psd[powerState].flags & BIT0)
        {
            maxPowerScalar = 0.0001;
        }
        double maxPowerWatts = nvmeIoCtx->device->drive_info.IdentifyData.nvme.ctrl.psd[powerState].maxPower * maxPowerScalar;

        //Need to make sure this power value can actually work in Windows! MS only supports values in milliwatts, and it's possible with the code above to specify something less than 1 milliwatt...-TJE
        if ((maxPowerWatts * 1000.0) >= 1)
        {
            STORAGE_DEVICE_POWER_CAP powerCap;
            memset(&powerCap, 0, sizeof(STORAGE_DEVICE_POWER_CAP));

            powerCap.Version = STORAGE_DEVICE_POWER_CAP_VERSION_V1;
            powerCap.Size = sizeof(STORAGE_DEVICE_POWER_CAP);
            powerCap.Units = StorageDevicePowerCapUnitsMilliwatts;
            powerCap.MaxPower = (ULONG)(maxPowerWatts * 1000.0);
            DWORD returnedBytes = 0;
            SetLastError(NO_ERROR);
            seatimer_t commandTimer;
            memset(&commandTimer, 0, sizeof(seatimer_t));
            start_Timer(&commandTimer);
            BOOL success = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
                IOCTL_STORAGE_DEVICE_POWER_CAP,
                &powerCap,
                sizeof(STORAGE_DEVICE_POWER_CAP),
                &powerCap,
                sizeof(STORAGE_DEVICE_POWER_CAP),
                &returnedBytes,
                NULL //TODO: add overlapped structure!
            );
            stop_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = GetLastError();
            nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

            if (success)
            {
                //TODO: should we validate the returned data to make sure we got what value we requested? - TJE
                ret = SUCCESS;
            }
            else
            {
                if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
                {
                    printf("Windows Error: ");
                    print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
                }
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int send_NVMe_Set_Temperature_Threshold(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;

    uint8_t thsel = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw11, 21, 20);
    uint8_t tmpsel = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw11, 19, 16);

    uint16_t temperatureThreshold = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw11, 15, 0);

    //TODO: check reserved fields are zero?
    STORAGE_TEMPERATURE_THRESHOLD tempThresh;
    //STORAGE_TEMPERATURE_THRESHOLD_FLAG_ADAPTER_REQUEST
    memset(&tempThresh, 0, sizeof(STORAGE_TEMPERATURE_THRESHOLD));

    tempThresh.Version = sizeof(STORAGE_TEMPERATURE_THRESHOLD);
    tempThresh.Size = sizeof(STORAGE_TEMPERATURE_THRESHOLD);

    //TODO: When do we set this flag?...for now just setting it all the time!
    tempThresh.Flags |= STORAGE_TEMPERATURE_THRESHOLD_FLAG_ADAPTER_REQUEST;


    tempThresh.Index = tmpsel;
    tempThresh.Threshold = temperatureThreshold;
    if (thsel == 0)
    {
        tempThresh.OverThreshold = TRUE;
    }
    else if (thsel == 1)
    {
        tempThresh.OverThreshold = FALSE;
    }
    else
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    tempThresh.Reserved = RESERVED;

    //now issue the IO!
    DWORD bytesReturned = 0;
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    BOOL success = DeviceIoControl(nvmeIoCtx->device->os_info.fd,
        IOCTL_STORAGE_SET_TEMPERATURE_THRESHOLD,
        &tempThresh,
        sizeof(STORAGE_TEMPERATURE_THRESHOLD),
        NULL,
        0,
        &bytesReturned,
        0
    );
    stop_Timer(&commandTimer);
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (success)
    {
        //TODO: should we validate the returned data to make sure we got what value we requested? - TJE
        ret = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        //Todo....set a better error condition
        nvmeIoCtx->commandCompletionData.commandSpecific = 0x0E;
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
        ret = FAILURE;
    }

    return ret;
}

int send_NVMe_Set_Features_Win10(nvmeCmdCtx *nvmeIoCtx, bool *useNVMPassthrough)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    //TODO: Depending on the feature, we may need a SCSI translation, a Windows API call, or we won't be able to perform any translation at all.
    //IOCTL_STORAGE_DEVICE_POWER_CAP
    bool save = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT31;
    uint8_t featureID = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10);
    switch (featureID)
    {
    case 0x01://Arbitration
        break;
    case 0x02://Power Management
        ret = win10_Translate_Set_Power_Management(nvmeIoCtx);
        break;
    case 0x03://LBA Range Type
        break;
    case 0x04://Temperature Threshold
        ret = send_NVMe_Set_Temperature_Threshold(nvmeIoCtx);
        break;
    case 0x05://Error Recovery
        ret = win10_Translate_Set_Error_Recovery_Time_Limit(nvmeIoCtx);
        break;
    case 0x06://Volatile Write Cache
        ret = win10_Translate_Set_Volatile_Write_Cache(nvmeIoCtx);
        break;
    case 0x07://Number of Queues
    case 0x08://Interrupt coalescing
    case 0x09://Interrupt Vector Configuration
    case 0x0A://Write Atomicity Normal
    case 0x0B://Asynchronous Event Configuration
    case 0x0C://Autonomous Power State Transition
    case 0x0D://Host Memroy Buffer
    case 0x0E://Timestamp
    case 0x0F://Keep Alive Timer
        break;
    case 0x10://Host Controller Thermal Management
        break;
    case 0x80://Software Progress Marker
    case 0x81://Host Identifier
    case 0x82://Reservation Notification Mask
        break;
    case 0x83://Reservation Persistance
                //SCSI Persistent reserve out?
        break;
    default:
        //12h- 77h & 0h = reserved
        //78h - 7Fh = NVMe Management Insterface Specification
        //80h - BFh = command set specific
        //C0h - FFh = vendor specific
        if (featureID >= 0xC0 && featureID <= 0xFF)
        {
            //call the vendor specific pass-through function to try and issue this command
            if (useNVMPassthrough)
            {
                *useNVMPassthrough = true;
            }
        }
        break;
    }
    return ret;
}

int win10_Translate_Format(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    uint32_t reservedBitsDWord10 = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 12);
    uint8_t secureEraseSettings = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 11, 9);
    bool pil = nvmeIoCtx->cmd.adminCmd.cdw10 & BIT8;
    uint8_t pi = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 5);
    bool mset = nvmeIoCtx->cmd.adminCmd.cdw10 & BIT4;
    uint8_t lbaFormat = M_GETBITRANGE(nvmeIoCtx->cmd.adminCmd.cdw10, 3, 0);
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (reservedBitsDWord10 == 0 && secureEraseSettings == 0)//we dont want to miss parameters that are currently reserved and we cannot do a secure erase with this translation
    {
        //mode select with mode descriptor (if needed for block size changes)
        //First we need to map the incoming LBA format to a size in bytes to send in a mode select
        int16_t powerOfTwo = (int16_t)nvmeIoCtx->device->drive_info.IdentifyData.nvme.ns.lbaf[lbaFormat].lbaDS;
        uint16_t lbaSize = 1;
        while (powerOfTwo >= 0)
        {
            lbaSize = lbaSize << 1;//multiply by 2
            --powerOfTwo;
        }
        if (lbaSize >= LEGACY_DRIVE_SEC_SIZE && lbaSize != nvmeIoCtx->device->drive_info.deviceBlockSize)//make sure the value is greater than 512 as required by spec and that it is different than what it is already set as! - TJE
        {
            //send a mode select command with a data block descriptor set to the new size
            //but first read the control mode page with a block descriptor
            uint8_t controlPageAndBD[36] = { 0 };
            if (SUCCESS == scsi_Mode_Sense_10(nvmeIoCtx->device, MP_CONTROL, 36, 0, false, true, MPC_CURRENT_VALUES, controlPageAndBD))
            {
                //got the block descriptor...so lets modify it as we need to...then send it back to the drive(r)
                //set number of logical blocks to all F's to make this as big as possible...
                controlPageAndBD[MODE_HEADER_LENGTH10 + 0] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 1] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 2] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 3] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 4] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 5] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 6] = 0xFF;
                controlPageAndBD[MODE_HEADER_LENGTH10 + 7] = 0xFF;
                //set the block size
                controlPageAndBD[MODE_HEADER_LENGTH10 + 12] = M_Byte3(lbaSize);
                controlPageAndBD[MODE_HEADER_LENGTH10 + 13] = M_Byte2(lbaSize);
                controlPageAndBD[MODE_HEADER_LENGTH10 + 14] = M_Byte1(lbaSize);
                controlPageAndBD[MODE_HEADER_LENGTH10 + 15] = M_Byte0(lbaSize);
                //Leave everything else alone! Just send it to the drive now :)
                if (SUCCESS != scsi_Mode_Select_10(nvmeIoCtx->device, 36, true, true, false, controlPageAndBD, 36))
                {
                    return OS_COMMAND_NOT_AVAILABLE;
                }
            }
            else
            {
                return OS_COMMAND_NOT_AVAILABLE;
            }
        }
        //format unit command
        //set up parameter data if necessary and send the scsi format unit command to be translated and sent to the drive. The mode sense/select should have been cached to be used by the format command.
        //if (pi == 0)
        //{
        //    //send without parameter data
        //    ret = scsi_Format_Unit(nvmeIoCtx->device, 0, false, false, false, 0, 0, NULL, 0, 0, 60);
        //}
        //else
        //{
        //send with parameter data
        uint8_t formatParameterData[4] = { 0 };//short header
        uint8_t fmtpInfo = 0;
        uint8_t piUsage = 0;
        switch (pi)
        {
        case 0:
            break;
        case 1:
            fmtpInfo = 0x2;
            break;
        case 2:
            fmtpInfo = 0x3;
            break;
        case 3:
            fmtpInfo = 0x3;
            piUsage = 1;
            break;
        default:
            return OS_COMMAND_NOT_AVAILABLE;
        }
        formatParameterData[0] = M_GETBITRANGE(piUsage, 2, 0);
        ret = scsi_Format_Unit(nvmeIoCtx->device, fmtpInfo, false, true, false, 0, 0, formatParameterData, 4, 0, 60);
        //}
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Write_Uncorrectable(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    uint64_t totalCommandTime = 0;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    uint64_t lba = M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    for (uint16_t iter = 0; iter < (M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1); ++iter)//+1 because nvme uses a zero based range value
    {
        int individualCommandRet = scsi_Write_Long_16(nvmeIoCtx->device, true, false, false, lba + iter, 0, NULL);
        if (individualCommandRet != SUCCESS)
        {
            //This is making sure we don't have a bad command followed by a good command, then missing a bad status code
            ret = individualCommandRet;
        }
        totalCommandTime += nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds;
    }
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = totalCommandTime;
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Flush(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //TODO: should we do this or should we send a SCSI Synchronize Cache command to be translated?
    //ret = os_Flush(nvmeIoCtx->device);
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    ret = scsi_Synchronize_Cache_16(nvmeIoCtx->device, false, 0, 0, 0);
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Read(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //TODO: We need to validate other fields to make sure we make the right call...may need a SCSI read command or a simple os_Read
    //extract fields from NVMe context, then see if we can put them into a compatible SCSI command
    uint64_t startingLBA = M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    bool limitedRetry = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT31;
    bool fua = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT30;
    uint8_t prInfo = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw12, 29, 26);
    bool pract = prInfo & BIT3;
    uint8_t prchk = M_GETBITRANGE(prInfo, 2, 0);
    uint16_t numberOfLogicalBlocks = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1;//nvme is zero based!
    uint8_t dsm = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw13);
    //bool incompresible = dsm & BIT7;
    //bool sequentialRequest = dsm & BIT6;
    //uint8_t accessLatency = M_GETBITRANGE(dsm, 5, 4);
    //uint8_t accessFrequency = M_GETBITRANGE(dsm, 3, 0);
    uint32_t expectedLogicalBlockAccessTag = nvmeIoCtx->cmd.nvmCmd.cdw14;
    uint16_t expectedLogicalBlockTagMask = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
    uint16_t expectedLogicalBlockApplicationTag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
    //now validate all the fields to see if we can send this command...
    uint8_t rdProtect = 0xFF;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (pract)
    {
        if (prchk == 0x7)
        {
            rdProtect = 0;
        }
    }
    else
    {
        switch (prchk)
        {
        case 7:
            rdProtect = 1;//or 101b
            break;
        case 3:
            rdProtect = 2;
            break;
        case 0:
            rdProtect = 3;
            break;
        case 4:
            rdProtect = 4;
            break;
        default:
            //don't do anything so we can filter out unsupported fields
            break;
        }
    }
    if (rdProtect != 0xFF && dsm == 0 && !limitedRetry)//rdprotect must be a valid value AND these other fields must not be set...-TJE
    {
        //NOTE: Spec only mentions translations for read 10, 12, 16...but we may also need 32!
        //Even though it isn't in the spec, we'll attempt it anyways when we have certain fields set... - TJE
        //TODO: we should check if the drive was formatted with protection information to make a better call on what to do...-TJE
        if (expectedLogicalBlockAccessTag != 0 || expectedLogicalBlockApplicationTag != 0 || expectedLogicalBlockTagMask != 0)
        {
            //read 32 command
            ret = scsi_Read_32(nvmeIoCtx->device, rdProtect, false, fua, false, startingLBA, 0, numberOfLogicalBlocks, nvmeIoCtx->ptrData, expectedLogicalBlockAccessTag, expectedLogicalBlockApplicationTag, expectedLogicalBlockTagMask, nvmeIoCtx->dataSize);
        }
        else
        {
            //read 16 should work
            ret = scsi_Read_16(nvmeIoCtx->device, rdProtect, false, fua, false, startingLBA, 0, numberOfLogicalBlocks, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Write(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //TODO: We need to validate other fields to make sure we make the right call...may need a SCSI write command or a simple os_Write
    //extract fields from NVMe context, then see if we can put them into a compatible SCSI command
    uint64_t startingLBA = M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    bool limitedRetry = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT31;
    bool fua = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT30;
    uint8_t prInfo = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw12, 29, 26);
    bool pract = prInfo & BIT3;
    uint8_t prchk = M_GETBITRANGE(prInfo, 2, 0);
    uint8_t dtype = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw12, 23, 20);
    uint16_t numberOfLogicalBlocks = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1;//nvme is zero based!
    uint16_t dspec = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw13);
    uint8_t dsm = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw13);
    //bool incompresible = dsm & BIT7;
    //bool sequentialRequest = dsm & BIT6;
    //uint8_t accessLatency = M_GETBITRANGE(dsm, 5, 4);
    //uint8_t accessFrequency = M_GETBITRANGE(dsm, 3, 0);
    uint32_t initialLogicalBlockAccessTag = nvmeIoCtx->cmd.nvmCmd.cdw14;
    uint16_t logicalBlockTagMask = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
    uint16_t logicalBlockApplicationTag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
    //now validate all the fields to see if we can send this command...
    uint8_t wrProtect = 0xFF;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (pract)
    {
        if (prchk == 0x7)
        {
            wrProtect = 0;
        }
    }
    else
    {
        switch (prchk)
        {
        case 7:
            wrProtect = 1;//or 101b
            break;
        case 3:
            wrProtect = 2;
            break;
        case 0:
            wrProtect = 3;
            break;
        case 4:
            wrProtect = 4;
            break;
        default:
            //don't do anything so we can filter out unsupported fields
            break;
        }
    }
    if (wrProtect != 0xFF && dsm == 0 && !limitedRetry && dtype == 0 && dspec == 0)//rdprotect must be a valid value AND these other fields must not be set...-TJE
    {
        //NOTE: Spec only mentions translations for write 10, 12, 16...but we may also need 32!
        //Even though it isn't in the spec, we'll attempt it anyways when we have certain fields set... - TJE
        //TODO: we should check if the drive was formatted with protection information to make a better call on what to do...-TJE
        if (initialLogicalBlockAccessTag != 0 || logicalBlockTagMask != 0 || logicalBlockApplicationTag != 0)
        {
            //write 32 command
            ret = scsi_Write_32(nvmeIoCtx->device, wrProtect, false, fua, startingLBA, 0, numberOfLogicalBlocks, nvmeIoCtx->ptrData, initialLogicalBlockAccessTag, logicalBlockApplicationTag, logicalBlockTagMask, nvmeIoCtx->dataSize);
        }
        else
        {
            //write 16 should work
            ret = scsi_Write_16(nvmeIoCtx->device, wrProtect, false, fua, startingLBA, 0, numberOfLogicalBlocks, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Compare(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //TODO: We need to validate other fields to make sure we make the right call...may need a SCSI verify command or a simple os_Verify
    //extract fields from NVMe context, then see if we can put them into a compatible SCSI command
    uint64_t startingLBA = M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    bool limitedRetry = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT31;
    bool fua = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT30;
    uint8_t prInfo = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw12, 29, 26);
    bool pract = prInfo & BIT3;
    uint8_t prchk = M_GETBITRANGE(prInfo, 2, 0);
    uint16_t numberOfLogicalBlocks = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1;
    uint32_t expectedLogicalBlockAccessTag = nvmeIoCtx->cmd.nvmCmd.cdw14;
    uint16_t expectedLogicalBlockTagMask = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw12);
    uint16_t expectedLogicalBlockApplicationTag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12);
    //now validate all the fields to see if we can send this command...
    uint8_t vrProtect = 0xFF;
    uint8_t byteCheck = 0xFF;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (pract)//this MUST be set for the translation to work
    {
        byteCheck = 0;//this should catch all possible translation cases...
        switch (prchk)
        {
        case 7:
            vrProtect = 1;//or 101b or others...
            break;
        case 3:
            vrProtect = 2;
            break;
        case 0:
            vrProtect = 3;
            break;
        case 4:
            vrProtect = 4;
            break;
        default:
            //don't do anything so we can filter out unsupported fields
            break;
        }
    }
    if (vrProtect != 0xFF && byteCheck != 0xFF && !limitedRetry && !fua)//vrProtect must be a valid value AND these other fields must not be set...-TJE
    {
        //NOTE: Spec only mentions translations for verify 10, 12, 16...but we may also need 32!
        //Even though it isn't in the spec, we'll attempt it anyways when we have certain fields set... - TJE
        //TODO: we should check if the drive was formatted with protection information to make a better call on what to do...-TJE
        if (expectedLogicalBlockAccessTag != 0 || expectedLogicalBlockApplicationTag != 0 || expectedLogicalBlockTagMask != 0)
        {
            //verify 32 command
            ret = scsi_Verify_32(nvmeIoCtx->device, vrProtect, false, byteCheck, startingLBA, 0, numberOfLogicalBlocks, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, expectedLogicalBlockAccessTag, expectedLogicalBlockApplicationTag, expectedLogicalBlockTagMask);
        }
        else
        {
            //verify 16 should work
            ret = scsi_Verify_16(nvmeIoCtx->device, vrProtect, false, byteCheck, startingLBA, 0, numberOfLogicalBlocks, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Data_Set_Management(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //TODO: We need to validate other fields to make sure we make the right call...may need a SCSI unmap command or
    //FSCTL_FILE_LEVEL_TRIM (and maybe also FSCTL_ALLOW_EXTENDED_DASD_IO)
    //NOTE: Using SCSI Unmap command - TJE
    uint8_t numberOfRanges = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10) + 1;//this is zero based in NVMe!
    bool deallocate = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT2;//This MUST be set to 1
    bool integralDatasetForWrite = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT1;//cannot be supported
    bool integralDatasetForRead = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT0;//cannot be supported
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (deallocate && !(integralDatasetForWrite || integralDatasetForRead || (nvmeIoCtx->cmd.nvmCmd.cdw10 >> 8) || (nvmeIoCtx->cmd.nvmCmd.cdw11 >> 3)))//checking for supported/unsupported flags and reserved bits
    {
        //Each range specified will be translated to a SCSI unmap descriptor.
        //NOTE: All context attributes will be ignored since it cannot be translated. It is also optional and the controller may not do anything with it.
        //We can optionally check for this, but I do not think it's really worth it right now. I will have code in place for this, just commented out - TJE
        bool atLeastOneContextAttributeSet = false;
        //first, allocate enough memory for the Unmap command
        uint32_t unmapDataLength = 8 + (16 * numberOfRanges);
        uint8_t *unmapParameterData = (uint8_t*)calloc_aligned(unmapDataLength, sizeof(uint8_t), nvmeIoCtx->device->os_info.minimumAlignment);//each range is 16 bytes plus an 8 byte header
        if (unmapParameterData)
        {
            //in a loop, set the unmap descriptors
            uint32_t scsiOffset = 8, nvmOffset = 0;
            for (uint16_t rangeIter = 0; rangeIter < numberOfRanges && scsiOffset < unmapDataLength && nvmOffset < nvmeIoCtx->dataSize; ++rangeIter, scsiOffset += 16, nvmOffset += 16)
            {
                //get the info we need from the incomming buffer
                uint32_t nvmContextAttributes = M_BytesTo4ByteValue(nvmeIoCtx->ptrData[nvmOffset + 3], nvmeIoCtx->ptrData[nvmOffset + 2], nvmeIoCtx->ptrData[nvmOffset + 1], nvmeIoCtx->ptrData[nvmOffset + 0]);
                uint32_t nvmLengthInLBAs = M_BytesTo4ByteValue(nvmeIoCtx->ptrData[nvmOffset + 7], nvmeIoCtx->ptrData[nvmOffset + 6], nvmeIoCtx->ptrData[nvmOffset + 5], nvmeIoCtx->ptrData[nvmOffset + 4]);
                uint32_t nvmStartingLBA = M_BytesTo4ByteValue(nvmeIoCtx->ptrData[nvmOffset + 15], nvmeIoCtx->ptrData[nvmOffset + 14], nvmeIoCtx->ptrData[nvmOffset + 13], nvmeIoCtx->ptrData[nvmOffset + 12]);
                if (nvmContextAttributes)
                {
                    atLeastOneContextAttributeSet = true;
                }
                //now translate it to a scsi unmap block descriptor
                //LBA
                unmapParameterData[scsiOffset + 0] = M_Byte7(nvmStartingLBA);
                unmapParameterData[scsiOffset + 1] = M_Byte6(nvmStartingLBA);
                unmapParameterData[scsiOffset + 2] = M_Byte5(nvmStartingLBA);
                unmapParameterData[scsiOffset + 3] = M_Byte4(nvmStartingLBA);
                unmapParameterData[scsiOffset + 4] = M_Byte3(nvmStartingLBA);
                unmapParameterData[scsiOffset + 5] = M_Byte2(nvmStartingLBA);
                unmapParameterData[scsiOffset + 6] = M_Byte1(nvmStartingLBA);
                unmapParameterData[scsiOffset + 7] = M_Byte0(nvmStartingLBA);
                //length
                unmapParameterData[scsiOffset + 8] = M_Byte3(nvmLengthInLBAs);
                unmapParameterData[scsiOffset + 9] = M_Byte2(nvmLengthInLBAs);
                unmapParameterData[scsiOffset + 10] = M_Byte1(nvmLengthInLBAs);
                unmapParameterData[scsiOffset + 11] = M_Byte0(nvmLengthInLBAs);
                //reserved
                unmapParameterData[scsiOffset + 11] = RESERVED;
                unmapParameterData[scsiOffset + 12] = RESERVED;
                unmapParameterData[scsiOffset + 13] = RESERVED;
                unmapParameterData[scsiOffset + 14] = RESERVED;
                unmapParameterData[scsiOffset + 15] = RESERVED;
            }
            //now set up the unmap parameter list header
            //unmap data length
            unmapParameterData[0] = M_Byte1(unmapDataLength - 2);
            unmapParameterData[1] = M_Byte0(unmapDataLength - 2);
            //block descriptor data length
            unmapParameterData[2] = M_Byte1(scsiOffset - 8);
            unmapParameterData[3] = M_Byte0(scsiOffset - 8);
            //reserved
            unmapParameterData[4] = RESERVED;
            unmapParameterData[5] = RESERVED;
            unmapParameterData[6] = RESERVED;
            unmapParameterData[7] = RESERVED;
            //if (!atLeastOneContextAttributeSet) //This is commented out for now, but we can use it to return OS_COMMAND_NOT_SUPPORTED if we want to - TJE
            {
                //send the command
                ret = scsi_Unmap(nvmeIoCtx->device, false, 0, unmapDataLength, unmapParameterData);
            }
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
        safe_Free_aligned(unmapParameterData);
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

int win10_Translate_Reservation_Register(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //Command inputs
    uint8_t cptpl = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw10, 31, 30);
    bool iekey = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT3;
    uint8_t rrega = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw10, 2, 0);
    //data structure inputs
    //uint64_t crkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[0], nvmeIoCtx->ptrData[1], nvmeIoCtx->ptrData[2], nvmeIoCtx->ptrData[3], nvmeIoCtx->ptrData[4], nvmeIoCtx->ptrData[5], nvmeIoCtx->ptrData[6], nvmeIoCtx->ptrData[7]);
    uint64_t nrkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[8], nvmeIoCtx->ptrData[9], nvmeIoCtx->ptrData[10], nvmeIoCtx->ptrData[11], nvmeIoCtx->ptrData[12], nvmeIoCtx->ptrData[13], nvmeIoCtx->ptrData[14], nvmeIoCtx->ptrData[15]);
    //scsi command stuff
    uint8_t scsiCommandData[24] = { 0 };
    uint8_t scsiServiceAction = 0;
    bool issueSCSICommand = false;
    //now check that those can convert to SCSI...if they can, then convert it!
    if (!iekey && (rrega == 0 || rrega == 1) && (cptpl == 2 || cptpl == 3))
    {
        //can translate. Service action is 0 (Register)
        scsiServiceAction = 0;
        //set up the data buffer
        if (nrkey == 0)
        {
            //reservation key
            memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
        }
        else
        {
            //service action reservation key
            memcpy(&scsiCommandData[8], &nvmeIoCtx->ptrData[8], 8);//NRKEY
        }
        //aptpl
        if (cptpl == 3)
        {
            scsiCommandData[20] |= BIT0;
        }
        issueSCSICommand = true;
    }
    else if (iekey && (rrega == 0 || rrega == 1) && (cptpl == 2 || cptpl == 3))
    {
        //can translate. Service action is 6 (Register and ignore existing key)
        scsiServiceAction = 6;
        //set up the data buffer
        if (nrkey == 0)
        {
            //reservation key
            memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
        }
        else
        {
            //service action reservation key
            memcpy(&scsiCommandData[8], &nvmeIoCtx->ptrData[8], 8);//NRKEY
        }
        //aptpl
        if (cptpl == 3)
        {
            scsiCommandData[20] |= BIT0;
        }
        issueSCSICommand = true;
    }
    else if (!iekey && rrega == 2)
    {
        //can translate. service action is 7 (Register and move)
        scsiServiceAction = 7;
        //set up the data buffer
        //reservation key
        memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
                                                                //service action reservation key
        memcpy(&scsiCommandData[8], &nvmeIoCtx->ptrData[8], 8);//NRKEY
        issueSCSICommand = true;
    }
    if (issueSCSICommand)
    {
        //if none of the above checks caught the command, then it cannot be translated
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, 0, 24, scsiCommandData);
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

int win10_Translate_Reservation_Report(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    bool issueSCSICommand = false;
    //command bytes
    uint32_t numberOfDwords = nvmeIoCtx->cmd.nvmCmd.cdw10 + 1;
    bool eds = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT0;
    //TODO: need it issue possibly multiple scsi persistent reserve in commands to get the data we want...
    if (issueSCSICommand)
    {
        //if none of the above checks caught the command, then it cannot be translated
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        //ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, 0, 24, scsiCommandData);
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

int win10_Translate_Reservation_Acquire(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //Command inputs
    uint8_t rtype = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw10, 15, 8);
    bool iekey = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT3;
    uint8_t racqa = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw10, 2, 0);
    //data structure inputs
    //uint64_t crkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[0], nvmeIoCtx->ptrData[1], nvmeIoCtx->ptrData[2], nvmeIoCtx->ptrData[3], nvmeIoCtx->ptrData[4], nvmeIoCtx->ptrData[5], nvmeIoCtx->ptrData[6], nvmeIoCtx->ptrData[7]);
    //uint64_t prkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[8], nvmeIoCtx->ptrData[9], nvmeIoCtx->ptrData[10], nvmeIoCtx->ptrData[11], nvmeIoCtx->ptrData[12], nvmeIoCtx->ptrData[13], nvmeIoCtx->ptrData[14], nvmeIoCtx->ptrData[15]);
    //scsi command stuff
    uint8_t scsiCommandData[24] = { 0 };
    uint8_t scsiServiceAction = 0;
    uint8_t scsiType = 0xF;
    switch (rtype)
    {
    case 0://not a reservation holder
        scsiType = 0;
        break;
    case 1:
        scsiType = 1;
        break;
    case 2:
        scsiType = 3;
        break;
    case 3:
        scsiType = 5;
        break;
    case 4:
        scsiType = 6;
        break;
    case 5:
        scsiType = 7;
        break;
    case 6:
        scsiType = 8;
        break;
    default:
        //nothing to do...we'll filder out the bad SCSI type below
        break;
    }
    bool issueSCSICommand = false;
    //now check that those can convert to SCSI...if they can, then convert it!
    if (!iekey && racqa == 0)
    {
        //can translate. Service action is 1 (Reserve)
        scsiServiceAction = 1;
        //set up the data buffer
        //reservation key
        memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
        issueSCSICommand = true;
    }
    else if (!iekey && racqa == 1)
    {
        //can translate. Service action is 4 (Preempt)
        scsiServiceAction = 4;
        //set up the data buffer
        //reservation key
        memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
                                                                //service action reservation key
        memcpy(&scsiCommandData[8], &nvmeIoCtx->ptrData[8], 8);//PRKEY
        issueSCSICommand = true;
    }
    else if (!iekey && racqa == 2)
    {
        //can translate. Service action is 5 (Preempt and abort)
        scsiServiceAction = 5;
        //set up the data buffer
        //reservation key
        memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
                                                                //service action reservation key
        memcpy(&scsiCommandData[8], &nvmeIoCtx->ptrData[8], 8);//PRKEY
        issueSCSICommand = true;
    }
    if (issueSCSICommand && scsiType != 0xF)
    {
        //if none of the above checks caught the command, then it cannot be translated
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, scsiType, 24, scsiCommandData);
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

int win10_Translate_Reservation_Release(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    //Command inputs
    uint8_t rtype = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw10, 15, 8);
    bool iekey = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT3;
    uint8_t rrela = M_GETBITRANGE(nvmeIoCtx->cmd.nvmCmd.cdw10, 2, 0);
    //data structure inputs
    //uint64_t crkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[0], nvmeIoCtx->ptrData[1], nvmeIoCtx->ptrData[2], nvmeIoCtx->ptrData[3], nvmeIoCtx->ptrData[4], nvmeIoCtx->ptrData[5], nvmeIoCtx->ptrData[6], nvmeIoCtx->ptrData[7]);
    //scsi command stuff
    uint8_t scsiCommandData[24] = { 0 };
    uint8_t scsiServiceAction = 0;
    uint8_t scsiType = 0xF;
    switch (rtype)
    {
    case 0://not a reservation holder
        scsiType = 0;
        break;
    case 1:
        scsiType = 1;
        break;
    case 2:
        scsiType = 3;
        break;
    case 3:
        scsiType = 5;
        break;
    case 4:
        scsiType = 6;
        break;
    case 5:
        scsiType = 7;
        break;
    case 6:
        scsiType = 8;
        break;
    default:
        //nothing to do...we'll filder out the bad SCSI type below
        break;
    }
    bool issueSCSICommand = false;
    //now check that those can convert to SCSI...if they can, then convert it!
    if (!iekey && rrela == 0)
    {
        //can translate. Service action is 2 (Release)
        scsiServiceAction = 2;
        //set up the data buffer
        //reservation key
        memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
        issueSCSICommand = true;
    }
    else if (!iekey && rrela == 1)
    {
        //can translate. Service action is 3 (Clear)
        scsiServiceAction = 3;
        //set up the data buffer
        //reservation key
        memcpy(&scsiCommandData[0], &nvmeIoCtx->ptrData[0], 8);//CRKEY
        issueSCSICommand = true;
    }
    if (issueSCSICommand && scsiType != 0xF)
    {
        //if none of the above checks caught the command, then it cannot be translated
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, scsiType, 24, scsiCommandData);
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }

    return ret;
}

//Windows 10 added a way to query for ATA identify data. Seems to work ok.
//Note: Any odd parameters like a change in TFRs from the spec will not work here.
int send_Win_ATA_Identify_Cmd(ScsiIoCtx *scsiIoCtx)
{
    int32_t returnValue = SUCCESS;
    BOOL    result;
    PVOID   buffer = NULL;
    ULONG   bufferLength = 0;
    ULONG   returnedLength = 0;

    PSTORAGE_PROPERTY_QUERY query = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + scsiIoCtx->dataLength;
    buffer = malloc(bufferLength);

    if (buffer == NULL) {
#if defined (_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query = (PSTORAGE_PROPERTY_QUERY)buffer;
    protocolDataDescr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
    protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;

    protocolData->ProtocolType = ProtocolTypeAta;
    protocolData->DataType = AtaDataTypeIdentify;//AtaDataTypeIdentify
    protocolData->ProtocolDataRequestValue = 0;// scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
    protocolData->ProtocolDataRequestSubValue = 0;// M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid);
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = /*M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) * */512U;//sector count * 512 = number of bytes

    //
    // Send request down.
    //
#if defined (_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, scsiIoCtx->device->os_info.name);
#endif
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    result = DeviceIoControl(scsiIoCtx->device->os_info.fd,
        IOCTL_STORAGE_QUERY_PROPERTY,
        buffer,
        bufferLength,
        buffer,
        bufferLength,
        &returnedLength,
        NULL
    );
    stop_Timer(&commandTimer);
    scsiIoCtx->device->os_info.last_error = GetLastError();
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (!result || (returnedLength == 0))
    {
        if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
        }
        returnValue = OS_PASSTHROUGH_FAILURE;
    }
    else
    {
        //
        // Validate the returned data.
        //
        if ((protocolDataDescr->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) ||
            (protocolDataDescr->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)))
        {
#if defined (_DEBUG)
            printf("%s: Error Log - data descriptor header not valid\n", __FUNCTION__);
#endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < scsiIoCtx->dataLength))
        {
#if defined (_DEBUG)
            printf("%s: Error Log - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        char* logData = (char*)((PCHAR)protocolData + protocolData->ProtocolDataOffset);
        memcpy(scsiIoCtx->pdata, (void*)logData, scsiIoCtx->dataLength);
    }

    safe_Free(buffer);

    return returnValue;
}

int send_Win_ATA_Get_Log_Page_Cmd(ScsiIoCtx *scsiIoCtx)
{
    int32_t returnValue = SUCCESS;
    BOOL    result;
    PVOID   buffer = NULL;
    ULONG   bufferLength = 0;
    ULONG   returnedLength = 0;

    PSTORAGE_PROPERTY_QUERY query = NULL;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + scsiIoCtx->dataLength + 4096;
    buffer = malloc(bufferLength);

    if (buffer == NULL) {
#if defined (_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query = (PSTORAGE_PROPERTY_QUERY)buffer;
    protocolDataDescr = (PSTORAGE_PROTOCOL_DATA_DESCRIPTOR)buffer;
    protocolData = (PSTORAGE_PROTOCOL_SPECIFIC_DATA)query->AdditionalParameters;

    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;

    protocolData->ProtocolType = ProtocolTypeAta;
    protocolData->DataType = AtaDataTypeLogPage;//AtaDataTypeIdentify
    protocolData->ProtocolDataRequestValue = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;//LP
    protocolData->ProtocolDataRequestSubValue = M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid);//Page number
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) * 512U;//sector count * 512 = number of bytes

    //
    // Send request down.
    //
#if defined (_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, scsiIoCtx->device->os_info.name);
#endif
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    start_Timer(&commandTimer);
    result = DeviceIoControl(scsiIoCtx->device->os_info.fd,
        IOCTL_STORAGE_QUERY_PROPERTY,
        buffer,
        bufferLength,
        buffer,
        bufferLength,
        &returnedLength,
        NULL
    );
    stop_Timer(&commandTimer);
    scsiIoCtx->device->os_info.last_error = GetLastError();
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    //TODO: we dummy up RTFRs
    if (!result || (returnedLength == 0))
    {
        if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
        }
        returnValue = OS_PASSTHROUGH_FAILURE;
    }
    else
    {
        //
        // Validate the returned data.
        //
        if ((protocolDataDescr->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) ||
            (protocolDataDescr->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)))
        {
#if defined (_DEBUG)
            printf("%s: Error Log - data descriptor header not valid\n", __FUNCTION__);
#endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < scsiIoCtx->dataLength))
        {
#if defined (_DEBUG)
            printf("%s: Error Log - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        char* logData = (char*)((PCHAR)protocolData + protocolData->ProtocolDataOffset);
        memcpy(scsiIoCtx->pdata, (void*)logData, scsiIoCtx->dataLength);
    }

    safe_Free(buffer);

    return returnValue;
}

#endif //WINVER >= SEA_WIN32_WINNT_WIN10

//MS NVMe requirements are listed here: https://msdn.microsoft.com/en-us/library/jj134356(v=vs.85).aspx
int send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx)
{
    int ret = OS_COMMAND_NOT_AVAILABLE;
    //TODO: Should we be checking the nsid in each command before issuing it? This should happen at some point, at least to filter out "all namespaces" for certain commands since MS won't let us issue some of them through their API - TJE
    if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
    {
#if WINVER >= SEA_WIN32_WINNT_WIN10 //This should wrap around anything going through the Windows API...Win 10 is required for NVMe IOs
        //TODO: If different versions of Windows 10 API support different commands, then check WIN_API_TARGET_VERSION to see which version of the API is in use to filter this list better. - TJE
        bool useNVMPassthrough = false;//this is only true when attempting the command with the generic storage protocol command IOCTL which is supposed to be used for VU commands only. - TJE
        int inVerbosity = nvmeIoCtx->device->deviceVerbosity;
        switch (nvmeIoCtx->cmd.adminCmd.opcode)
        {
        case NVME_ADMIN_CMD_IDENTIFY:
            ret = send_Win_NVMe_Identify_Cmd(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_GET_LOG_PAGE:
            ret = send_Win_NVMe_Get_Log_Page_Cmd(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_GET_FEATURES:
            //NOTE: I don't know if this will work for different select field values...will need to debug this to find out.
            //If different select fields are not supported with this call, then we either need to do a SCSI translation or we have to return "not-supported" - TJE
            ret = send_Win_NVMe_Get_Features_Cmd(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_DOWNLOAD_FW:
            ret = send_Win_NVMe_Firmware_Image_Download_Command(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_ACTIVATE_FW:
#if defined(NVME_FW_ACTIVATE_WIN10)
            ret = send_Win_NVMe_Firmware_Activate_Command(nvmeIoCtx);
#else
            ret = send_Win_NVMe_Firmware_Activate_Miniport_Command(nvmeIoCtx);
#endif
            break;
        case NVME_ADMIN_CMD_SECURITY_SEND:
            ret = win10_Translate_Security_Send(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_SECURITY_RECV:
            ret = win10_Translate_Security_Receive(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_SET_FEATURES:
            ret = send_NVMe_Set_Features_Win10(nvmeIoCtx, &useNVMPassthrough);
            break;
        case NVME_ADMIN_CMD_FORMAT_NVM:
            ret = win10_Translate_Format(nvmeIoCtx);
            break;
        default:
            //Check if it's a vendor unique op code.
            if (nvmeIoCtx->cmd.adminCmd.opcode >= 0xC0 && nvmeIoCtx->cmd.adminCmd.opcode <= 0xFF)//admin commands in this range are vendor unique
            {
                useNVMPassthrough = true;
            }
            break;
        }
        if (useNVMPassthrough)
        {
            //Call the function to send a VU command using STORAGE_PROTOCOL_COMMAND - TJE
            ret = send_NVMe_Vendor_Unique_IO(nvmeIoCtx);
        }
#endif
    }
    else if (nvmeIoCtx->commandType == NVM_CMD)
    {
#if WINVER >= SEA_WIN32_WINNT_WIN10 //This should wrap around anything going through the Windows API...Win 10 is required for NVMe IOs
        //TODO: If different versions of Windows 10 API support different commands, then check WIN_API_TARGET_VERSION to see which version of the API is in use to filter this list better. - TJE
        bool useNVMPassthrough = false;//this is only true when attempting the command with the generic storage protocol command IOCTL which is supposed to be used for VU commands only. - TJE
        switch (nvmeIoCtx->cmd.adminCmd.opcode)
        {
        case NVME_CMD_WRITE_UNCOR://SCSI Write long command
            ret = win10_Translate_Write_Uncorrectable(nvmeIoCtx);
            break;
        case NVME_CMD_FLUSH:
            ret = win10_Translate_Flush(nvmeIoCtx);
            break;
        case NVME_CMD_READ://NOTE: This translation likely won't be hit since most code calls into the read function in cmds.h which will call os_Read instead. This is here for those requesting something specific...-TJE
            ret = win10_Translate_Read(nvmeIoCtx);
            break;
        case NVME_CMD_WRITE://NOTE: This translation likely won't be hit since most code calls into the read function in cmds.h which will call os_Read instead. This is here for those requesting something specific...-TJE
            ret = win10_Translate_Write(nvmeIoCtx);
            break;
        case NVME_CMD_COMPARE://verify?
            ret = win10_Translate_Compare(nvmeIoCtx);
            break;
        case NVME_CMD_DATA_SET_MANAGEMENT://SCSI Unmap or Win API call?
            ret = win10_Translate_Data_Set_Management(nvmeIoCtx);
            break;
        //case NVME_CMD_WRITE_ZEROS://This isn't translatable unless the SCSI to NVM translation spec is updated. - TJE
            //FSCTL_SET_ZERO_DATA (and maybe also FSCTL_ALLOW_EXTENDED_DASD_IO)...might not work and only do filesystem level stuff
            //break;
        //Removing reservation translations for now...need to review them. - TJE
        //case NVME_CMD_RESERVATION_REGISTER://Translation only available in later specifications!
        //    ret = win10_Translate_Reservation_Register(nvmeIoCtx);
        //    break;
        //case NVME_CMD_RESERVATION_REPORT://Translation only available in later specifications!
        //    ret = win10_Translate_Reservation_Report(nvmeIoCtx);
        //    break;
        //case NVME_CMD_RESERVATION_ACQUIRE://Translation only available in later specifications!
        //    ret = win10_Translate_Reservation_Acquire(nvmeIoCtx);
        //    break;
        //case NVME_CMD_RESERVATION_RELEASE://Translation only available in later specifications!
        //    ret = win10_Translate_Reservation_Release(nvmeIoCtx);
        //    break;
        default:
            //Check if it's a vendor unique op code.
            if (nvmeIoCtx->cmd.adminCmd.opcode >= 0x80 && nvmeIoCtx->cmd.adminCmd.opcode <= 0xFF)//admin commands in this range are vendor unique
            {
                useNVMPassthrough = true;
            }
            break;
        }
        if (useNVMPassthrough)
        {
            //Call the function to send a VU command using STORAGE_PROTOCOL_COMMAND - TJE
            ret = send_NVMe_Vendor_Unique_IO(nvmeIoCtx);
        }
#endif
    }
    return ret;
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

int pci_Read_Bar_Reg(tDevice * device, uint8_t * pData, uint32_t dataSize)
{
    return NOT_SUPPORTED;
}
#endif
//The overlapped structure used here changes it to asynchronous IO, but the synchronous portions of code are left here in case the device responds as a synchronous device
//and ignores the overlapped strucutre...it SHOULD work on any device like this.
//See here: https://msdn.microsoft.com/en-us/library/windows/desktop/aa365683(v=vs.85).aspx
int os_Read(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Read LBAs\n");
    }
    if (async)
    {
        //asynchronous IO is not supported right now
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            print_Return_Enum("Windows API Read", NOT_SUPPORTED);
        }
        return NOT_SUPPORTED;
    }
    //used for setting the timeout
    COMMTIMEOUTS comTimeout;
    memset(&comTimeout, 0, sizeof(COMMTIMEOUTS));
    /*BOOL timeoutGot = */
    GetCommTimeouts(device->os_info.fd, &comTimeout);//get timeouts if possible before trying to change them...
    uint64_t timeoutInSeconds = 0;
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        comTimeout.ReadTotalTimeoutConstant = 15000;//15 seconds
        timeoutInSeconds = 15;
    }
    else
    {
        comTimeout.ReadTotalTimeoutConstant = device->drive_info.defaultTimeoutSeconds * 1000;//convert time in seconds to milliseconds
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    /*BOOL timeoutSet = */
    SetCommTimeouts(device->os_info.fd, &comTimeout);
    device->os_info.last_error = GetLastError();
    //for use by the setFilePointerEx function
    LARGE_INTEGER liDistanceToMove = { 0 }, lpNewFilePointer = { 0 };
    //set the distance to move in bytes
    liDistanceToMove.QuadPart = lba * device->drive_info.deviceBlockSize;
    //set the offset here
    BOOL retStatus = SetFilePointerEx(device->os_info.fd, liDistanceToMove, &lpNewFilePointer, FILE_BEGIN);
    if (!retStatus)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            print_Return_Enum("Windows API Read", OS_PASSTHROUGH_FAILURE);
        }
        return OS_PASSTHROUGH_FAILURE;
    }
    DWORD bytesReturned = 0;

    //this api call will need some changes when asynchronous support is added in
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    overlappedStruct.Offset = M_DoubleWord0(lba * device->drive_info.deviceBlockSize);
    overlappedStruct.OffsetHigh = M_DoubleWord1(lba * device->drive_info.deviceBlockSize);
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    start_Timer(&commandTimer);
    retStatus = ReadFile(device->os_info.fd, ptrData, dataSize, &bytesReturned, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        retStatus = GetOverlappedResult(device->os_info.fd, &overlappedStruct, &bytesReturned, TRUE);
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    device->os_info.last_error = GetLastError();
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (!retStatus)//not successful
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(device->os_info.last_error);
        }
    }
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && ptrData != NULL)
    {
        printf("\t  Data Buffer being returned:\n");
        print_Data_Buffer(ptrData, dataSize, true);
        printf("\n");
    }

    if (bytesReturned != (DWORD)dataSize)
    {
        //error, didn't get all the data
        ret = FAILURE;
    }

    //clear the last command sense data and rtfrs. We'll dummy them up in a minute
    memset(&device->drive_info.lastCommandRTFRs, 0, sizeof(ataReturnTFRs));
    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);

    if (retStatus)
    {
        //successful read
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
        }
        ret = SUCCESS;
    }
    else
    {
        //failure for one reason or another. The last error may or may not tell us...unlikely to tell us :/
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
        }
        device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
        //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
        device->drive_info.lastCommandSenseData[2] |= SENSE_KEY_ABORTED_COMMAND;
        ret = FAILURE;
    }

    //check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / 1000000000) >= timeoutInSeconds)
    {
        ret = COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Read", ret);
    }
    return ret;
}

int os_Write(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Write LBAs\n");
    }
    if (async)
    {
        //asynchronous IO is not supported right now
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            print_Return_Enum("Windows API Write", NOT_SUPPORTED);
        }
        return NOT_SUPPORTED;
    }
    //used for setting the timeout
    COMMTIMEOUTS comTimeout;
    memset(&comTimeout, 0, sizeof(COMMTIMEOUTS));
    /*BOOL timeoutGot = */
    GetCommTimeouts(device->os_info.fd, &comTimeout);//get timeouts if possible before trying to change them...
    uint64_t timeoutInSeconds = 0;
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        comTimeout.WriteTotalTimeoutConstant = 15000;//15 seconds
        timeoutInSeconds = 15;
    }
    else
    {
        comTimeout.WriteTotalTimeoutConstant = device->drive_info.defaultTimeoutSeconds * 1000;//convert time in seconds to milliseconds
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    /*BOOL timeoutSet = */
    SetCommTimeouts(device->os_info.fd, &comTimeout);
    device->os_info.last_error = GetLastError();
    //for use by the setFilePointerEx function
    LARGE_INTEGER liDistanceToMove = { 0 }, lpNewFilePointer = { 0 };
    //set the distance to move in bytes
    liDistanceToMove.QuadPart = lba * device->drive_info.deviceBlockSize;
    //set the offset here
    BOOL retStatus = SetFilePointerEx(device->os_info.fd, liDistanceToMove, &lpNewFilePointer, FILE_BEGIN);
    if (!retStatus)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            print_Return_Enum("Windows API Write", OS_PASSTHROUGH_FAILURE);
        }
        return OS_PASSTHROUGH_FAILURE;
    }
    DWORD bytesReturned = 0;

    //this api call will need some changes when asynchronous support is added in
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    overlappedStruct.Offset = M_DoubleWord0(lba * device->drive_info.deviceBlockSize);
    overlappedStruct.OffsetHigh = M_DoubleWord1(lba * device->drive_info.deviceBlockSize);
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && ptrData != NULL)
    {
        printf("\t  Data Buffer being sent:\n");
        print_Data_Buffer(ptrData, dataSize, true);
        printf("\n");
    }
    start_Timer(&commandTimer);
    retStatus = WriteFile(device->os_info.fd, ptrData, dataSize, &bytesReturned, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        retStatus = GetOverlappedResult(device->os_info.fd, &overlappedStruct, &bytesReturned, TRUE);
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    device->os_info.last_error = GetLastError();
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (!retStatus)//not successful
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(device->os_info.last_error);
        }
    }
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    if (bytesReturned != (DWORD)dataSize)
    {
        //error, didn't get all the data
        ret = FAILURE;
    }

    //clear the last command sense data and rtfrs. We'll dummy them up in a minute
    memset(&device->drive_info.lastCommandRTFRs, 0, sizeof(ataReturnTFRs));
    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);

    if (retStatus)
    {
        //successful write
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
        }
        ret = SUCCESS;
    }
    else
    {
        //failure for one reason or another. The last error may or may not tell us...unlikely to tell us :/
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
        }
        device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
        //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
        device->drive_info.lastCommandSenseData[2] |= SENSE_KEY_ABORTED_COMMAND;
        ret = FAILURE;
    }
    //check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / 1000000000) >= timeoutInSeconds)
    {
        ret = COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Write", ret);
    }
    return ret;
}
#if WINVER >= SEA_WIN32_WINNT_WINXP
//IOCTL is for Win XP and higher
//Seems to work. Needs some enhancements with timers and checking return codes more closely to dummy up better sense data.
int os_Verify(tDevice *device, uint64_t lba, uint32_t range)
{
    int ret = NOT_SUPPORTED;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Verify LBAs\n");
    }
    VERIFY_INFORMATION verifyCmd;
    memset(&verifyCmd, 0, sizeof(VERIFY_INFORMATION));
    seatimer_t verifyTimer;
    memset(&verifyTimer, 0, sizeof(seatimer_t));
    verifyCmd.StartingOffset.QuadPart = lba * device->drive_info.deviceBlockSize;//LBA needs to be converted to a byte offset
    verifyCmd.Length = range * device->drive_info.deviceBlockSize;//needs to be a range in bytes!
    uint64_t timeoutInSeconds = 0;
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        timeoutInSeconds = 15;
    }
    else
    {
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    DWORD returnedBytes = 0;
    OVERLAPPED overlappedStruct;
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    overlappedStruct.Offset = M_DoubleWord0(lba * device->drive_info.deviceBlockSize);
    overlappedStruct.OffsetHigh = M_DoubleWord1(lba * device->drive_info.deviceBlockSize);
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    start_Timer(&verifyTimer);
    BOOL success = DeviceIoControl(device->os_info.fd,
        IOCTL_DISK_VERIFY,
        &verifyCmd,
        sizeof(VERIFY_INFORMATION),
        NULL,
        0,
        &returnedBytes,
        &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING == device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(device->os_info.fd, &overlappedStruct, &returnedBytes, TRUE);
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&verifyTimer);
    device->os_info.last_error = GetLastError();
    if (!success)//not successful
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(device->os_info.last_error);
        }
    }
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    //clear the last command sense data and rtfrs. We'll dummy them up in a minute
    memset(&device->drive_info.lastCommandRTFRs, 0, sizeof(ataReturnTFRs));
    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);
    if (success)
    {
        //successful verify
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
        }
        ret = SUCCESS;
    }
    else
    {
        //below are the error codes windows driver site says we can get https://msdn.microsoft.com/en-us/library/windows/hardware/ff560420(v=vs.85).aspx
        switch (GetLastError())
        {
        case 0xC0000015://STATUS_NONEXISTENT_SECTOR <-id not found!
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
                device->drive_info.lastCommandRTFRs.error = ATA_ERROR_BIT_ID_NOT_FOUND;
            }
            device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
            //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
            device->drive_info.lastCommandSenseData[2] = SENSE_KEY_ILLEGAL_REQUEST;
            //LOGICAL BLOCK ADDRESS OUT OF RANGE
            device->drive_info.lastCommandSenseData[12] = 0x21;
            device->drive_info.lastCommandSenseData[13] = 0x00;
            ret = FAILURE;
            break;
        case 0xC000009C://STATUS_DEVICE_DATA_ERROR <-bad sector!
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
                device->drive_info.lastCommandRTFRs.error = ATA_ERROR_BIT_UNCORRECTABLE_DATA;
            }
            device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
            //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
            device->drive_info.lastCommandSenseData[2] = SENSE_KEY_MEDIUM_ERROR;
            //UNRECOVERED READ ERROR
            device->drive_info.lastCommandSenseData[12] = 0x11;
            device->drive_info.lastCommandSenseData[13] = 0x00;
            ret = FAILURE;
            break;
        case 0xC0000010://STATUS_INVALID_DEVICE_REQUEST <-command not supported?
            //failure for one reason or another
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
                device->drive_info.lastCommandRTFRs.error = ATA_ERROR_BIT_ABORT;
            }
            device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
            //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
            device->drive_info.lastCommandSenseData[2] |= SENSE_KEY_ABORTED_COMMAND;
            ret = FAILURE;
            break;
        case 0xC00000B5://STATUS_IO_TIMEOUT
            ret = COMMAND_TIMEOUT;
            break;
        case 0xC000009D://STATUS_DEVICE_NOT_CONNECTED <-could be a crc error too
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
                device->drive_info.lastCommandRTFRs.error = ATA_ERROR_BIT_INTERFACE_CRC;
            }
            device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
            //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
            device->drive_info.lastCommandSenseData[2] = SENSE_KEY_MEDIUM_ERROR;
            //INFORMATION UNIT iuCRC ERROR DETECTED
            device->drive_info.lastCommandSenseData[12] = 0x47;
            device->drive_info.lastCommandSenseData[13] = 0x03;
            ret = FAILURE;
            break;
        case 0xC0000023://STATUS_BUFFER_TOO_SMALL
        case 0xC0000004://STATUS_INFO_LENGTH_MISMATCH
        case 0xC000000D://STATUS_INVALID_PARAMETER
        case 0xC000009A://STATUS_INSUFFICIENT_RESOURCES
        default:
            //failure for one reason or another
            if (device->drive_info.drive_type == ATA_DRIVE)
            {
                device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            }
            device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
            //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
            device->drive_info.lastCommandSenseData[2] |= SENSE_KEY_ABORTED_COMMAND;
            ret = FAILURE;
            break;
        }
    }
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(verifyTimer);
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    //check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / 1000000000) >= timeoutInSeconds)
    {
        ret = COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Verify", ret);
    }
    return ret;
}
#else
//verify IOTCL is not available so we need to just do a flush and a read.
int os_Verify(tDevice *device, uint64_t lba, uint32_t range)
{
    //flush the cache first to make sure we aren't reading something that is in cache than disk (as close as we can get right here)
    os_Flush(device);
    //now do a read and throw away the data
    uint8_t *readData = (uint8_t*)malloc(device->drive_info.deviceBlockSize * range);
    if (readData)
    {
        ret = os_Read(device, lba, false, readData, device->drive_info.deviceBlockSize * range);
        safe_Free(readData);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}
#endif

//This is for Windows XP and higher. This should issue a flush cache or synchronize cache command for us
int os_Flush(tDevice *device)
{
    int ret = UNKNOWN;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Flush Cache\n");
    }
    //used for setting the timeout
    COMMTIMEOUTS comTimeout;
    memset(&comTimeout, 0, sizeof(COMMTIMEOUTS));
    /*BOOL timeoutGot = */
    GetCommTimeouts(device->os_info.fd, &comTimeout);//get timeouts if possible before trying to change them...
    uint64_t timeoutInSeconds = 0;
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        comTimeout.ReadTotalTimeoutConstant = 15000;//15 seconds
        timeoutInSeconds = 15;
    }
    else
    {
        comTimeout.ReadTotalTimeoutConstant = device->drive_info.defaultTimeoutSeconds * 1000;//convert time in seconds to milliseconds
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    /*BOOL timeoutSet = */
    SetCommTimeouts(device->os_info.fd, &comTimeout);
    device->os_info.last_error = GetLastError();
    //DWORD bytesReturned = 0;

    //this api call will need some changes when asynchronous support is added in
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));
    SetLastError(ERROR_SUCCESS);//clear any cached errors before we try to send the command
    start_Timer(&commandTimer);
    int retStatus = FlushFileBuffers(device->os_info.fd);
    device->os_info.last_error = GetLastError();
    if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    if (!retStatus)//not successful
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(device->os_info.last_error);
        }
    }

    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    //clear the last command sense data and rtfrs. We'll dummy them up in a minute
    memset(&device->drive_info.lastCommandRTFRs, 0, sizeof(ataReturnTFRs));
    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);

    if (retStatus)
    {
        //successful read
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
        }
        ret = SUCCESS;
    }
    else
    {
        //failure for one reason or another. The last error may or may not tell us...unlikely to tell us :/
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
        }
        device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
        //set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to right now
        device->drive_info.lastCommandSenseData[2] |= SENSE_KEY_ABORTED_COMMAND;
        ret = FAILURE;
    }

    //check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / 1000000000) >= timeoutInSeconds)
    {
        ret = COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Flush", ret);
    }
    return ret;
}

long getpagesize(void)
{
    //implementation for get page size in windows using the WinAPI
    static long pageSize = 0;
    if (!pageSize)
    {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        pageSize = sysInfo.dwPageSize;
    }
    return pageSize;
}
