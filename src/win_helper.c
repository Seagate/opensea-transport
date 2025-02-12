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
#include "sleep.h"
#include "string_utils.h"
#include "type_conversion.h"
#include "windows_version_detect.h"

#include <fcntl.h>
#include <initguid.h>
#include <ntddstor.h>
#include <stddef.h> // offsetof
#include <stdio.h>
#include <stdlib.h> // for mbstowcs_s
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tchar.h>
#include <time.h>
#include <wchar.h>
DISABLE_WARNING_4255
#include <windows.h> // added for forced PnP rescan
RESTORE_WARNING_4255
// NOTE: ARM requires 10.0.16299.0 API to get this library!
#include <cfgmgr32.h> // added for forced PnP rescan
// #include <setupapi.h> //NOTE: Not available for ARM
#include <devpkey.h>
#include <devpropdef.h>
#include <winbase.h>

#include "ata_helper_func.h"
#include "common_public.h"
#include "sat_helper_func.h"
#include "scsi_helper_func.h"
#include "usb_hacks.h"
#include "win_helper.h"

#include "nvme_helper.h"
#include "nvme_helper_func.h"

#if defined(ENABLE_CSMI) // when this is enabled, the "get_Device", "get_Device_Count", and "get_Device_List" will also
                         // check for CSMI devices unless a flag is given to ignore them. For apps only doing CSMI, call
                         // the csmi implementations directly
#    include "csmi_helper_func.h"
#endif

#if defined(ENABLE_OFNVME)
#    include "of_nvme_helper_func.h"
#endif

#if defined(ENABLE_INTEL_RST)
#    include "intel_rst_helper.h"
#endif

#include "raid_scan_helper.h"

#if defined(_DEBUG) && !defined(WIN_DEBUG)
#    define WIN_DEBUG
#endif //_DEBUG && !WIN_DEBUG

// If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise
// you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return false;
}

// MinGW may or may not have some of these, so there is a need to define these here to build properly when they are
// otherwise not available. as mingw changes versions, some of these below may be available. Need to have a way to check
// mingw preprocessor defines for versions to work around these. NOTE: The device property keys are incomplete in mingw.
// Need to add similar code using setupapi and some sort of ifdef to switch between for VS and mingw to resolve this
// better.
#if defined(__MINGW32__) || defined(__MINGW64__)
#    if !defined(ATA_FLAGS_NO_MULTIPLE)
#        define ATA_FLAGS_NO_MULTIPLE (1 << 5)
#    endif
#    if !defined(BusTypeSpaces)
#        define BusTypeSpaces 16
#    endif
#    if !defined(BusTypeNvme)
#        define BusTypeNvme 17
#    endif

// This is for looking up hardware IDs of devices for PCIe/USB, etc
#    if defined(NEED_DEVHARDID)
DEFINE_DEVPROPKEY(DEVPKEY_Device_HardwareIds,
                  0xa45c254e,
                  0xdf1c,
                  0x4efd,
                  0x80,
                  0x20,
                  0x67,
                  0xd1,
                  0x46,
                  0xa8,
                  0x50,
                  0xe0,
                  3);
#    endif

#    if defined(NEED_DEVCOMPID)
DEFINE_DEVPROPKEY(DEVPKEY_Device_CompatibleIds,
                  0xa45c254e,
                  0xdf1c,
                  0x4efd,
                  0x80,
                  0x20,
                  0x67,
                  0xd1,
                  0x46,
                  0xa8,
                  0x50,
                  0xe0,
                  4);
#    endif

#    if !defined(CM_GETIDLIST_FILTER_PRESENT)
#        define CM_GETIDLIST_FILTER_PRESENT (0x00000100)
#    endif
#    if !defined(CM_GETIDLIST_FILTER_CLASS)
#        define CM_GETIDLIST_FILTER_CLASS (0x00000200)
#    endif

#    if !defined(GUID_DEVINTERFACE_DISK)
DEFINE_GUID(GUID_DEVINTERFACE_DISK, 0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
#    endif

#    if !defined(ERROR_DEVICE_HARDWARE_ERROR)
#        define ERROR_DEVICE_HARDWARE_ERROR 483L
#    endif

#    if !defined(ERROR_OFFSET_ALIGNMENT_VIOLATION)
#        define ERROR_OFFSET_ALIGNMENT_VIOLATION 327L
#    endif

#    if !defined(ERROR_DATA_CHECKSUM_ERROR)
#        define ERROR_DATA_CHECKSUM_ERROR 323L
#    endif

#endif

#if WINVER < SEA_WIN32_WINNT_WINBLUE && !defined(BusTypeNvme)
#    define BusTypeNvme 17
#endif

extern bool validate_Device_Struct(versionBlock);

eReturnValues get_Windows_SMART_IO_Support(tDevice* device);
#if WINVER >= SEA_WIN32_WINNT_WIN10
eReturnValues get_Windows_FWDL_IO_Support(tDevice* device, STORAGE_BUS_TYPE busType);
bool          is_Firmware_Download_Command_Compatible_With_Win_API(ScsiIoCtx* scsiIoCtx);
eReturnValues send_Win_ATA_Get_Log_Page_Cmd(ScsiIoCtx* scsiIoCtx);
eReturnValues send_Win_ATA_Identify_Cmd(ScsiIoCtx* scsiIoCtx);
#endif
#if defined(WIN_DEBUG)
// \fn print_bus_type (BYTE type)
// \nbrief Funtion to print in human readable format the BusType of a device
// \param BYTE which is STORAGE_BUS_TYPE windows enum
static void print_bus_type(BYTE type);

void print_bus_type(BYTE type)
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
#    if WINVER >= SEA_WIN32_WINNT_WIN8
    case BusTypeSpaces:
        printf("Spaces");
        break;
#        if WINVER >= SEA_WIN32_WINNT_WINBLUE // 8.1 introduced NVMe
    case BusTypeNvme:
        printf("NVMe");
        break;
#            if WINVER >= SEA_WIN32_WINNT_WIN10 // Win10 API kits may have more or less of these bus types so need to
                                                // also check which version of the Win10 API is being targetted
#                if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_10586
    case BusTypeSCM:
        printf("SCM");
        break;
#                    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    case BusTypeUfs:
        printf("UFS");
        break;
#                    endif // WIN_API_TARGET_VERSION >= Win 10 API 10.0.15063.0
#                endif     // WIN_API_TARGET_VERSION >= Win 10 API 10.0.10586.0
#            endif         // WINVER >= WIN10
#        endif             // WINVER >= WIN8.1
#    endif                 // WINVER >= WIN8.0
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
#endif // WIN_DEBUG

// This function is only used in get_Adapter_IDs which is why it's here. If this is useful for something else in the
// future, move it to opensea-common. static void convert_String_Spaces_To_Underscores(char *stringToChange)
//{
//     size_t stringLen = 0, iter = 0;
//     if (stringToChange == M_NULLPTR)
//     {
//         return;
//     }
//     stringLen = safe_strlen(stringToChange);
//     if (stringLen == 0)
//     {
//         return;
//     }
//     while (iter <= stringLen)
//     {
//         if (safe_isspace(stringToChange[iter]))
//         {
//             stringToChange[iter] = '_';
//         }
//         iter++;
//     }
// }

static bool get_IDs_From_TCHAR_String(DEVINST instance, TCHAR* buffer, size_t bufferLength, tDevice* device)
{
    bool      success = true;
    CONFIGRET cmRet   = CR_SUCCESS;
    // here is where we need to parse the USB VID/PID or TODO: PCI Vendor, Product, and Revision numbers
    if (_tcsncmp(TEXT("USB"), buffer, _tcsclen(TEXT("USB"))) == 0)
    {
        ULONG       propertyBufLen = ULONG_C(0);
        DEVPROPTYPE propertyType   = ULONG_C(0);
#if IS_MSVC_VERSION(MSVC_2015)
        int scannedVals =
            _sntscanf_s(buffer, bufferLength, TEXT("USB\\VID_%") TEXT(SCNx32) TEXT("&PID_%") TEXT(SCNx32) TEXT("\\%*s"),
                        &device->drive_info.adapter_info.vendorID, &device->drive_info.adapter_info.productID);
#else
        // This is a hack around how VS2013 handles string concatenation with how the printf format macros were defined
        // for it versus newer versions.
        int scannedVals =
            _sntscanf_s(buffer, bufferLength, TEXT("USB\\VID_%x&PID_%x\\%*s"),
                        &device->drive_info.adapter_info.vendorID, &device->drive_info.adapter_info.productID);
#endif
        device->drive_info.adapter_info.vendorIDValid  = true;
        device->drive_info.adapter_info.productIDValid = true;
        if (scannedVals < 2)
        {
#if defined(_DEBUG)
            printf("Could not scan all values. Scanned %d values\n", scannedVals);
#endif
        }
        device->drive_info.adapter_info.infoType = ADAPTER_INFO_USB;
        // unfortunately, this device ID doesn't have a revision in it for USB.
        // We can do this other property request to read it, but it's wide characters only. No TCHARs allowed.
        cmRet = CM_Get_DevNode_PropertyW(instance, &DEVPKEY_Device_HardwareIds, &propertyType, M_NULLPTR,
                                         &propertyBufLen, 0);
        if (CR_SUCCESS == cmRet || CR_INVALID_POINTER == cmRet ||
            CR_BUFFER_SMALL == cmRet) // We'll probably get an invalid pointer or small buffer, but this will return the
                                      // size of the buffer we need, so allow it through - TJE
        {
            PBYTE propertyBuf = M_REINTERPRET_CAST(PBYTE, safe_calloc(propertyBufLen + 1, sizeof(BYTE)));
            if (propertyBuf)
            {
                propertyBufLen += 1;
                // NOTE: This key contains all 3 parts, VID, PID, and REV from the "parentInst": Example:
                // USB\VID_174C&PID_2362&REV_0100
                if (CR_SUCCESS == CM_Get_DevNode_PropertyW(instance, &DEVPKEY_Device_HardwareIds, &propertyType,
                                                           propertyBuf, &propertyBufLen, 0))
                {
                    // multiple strings can be returned.
                    for (LPWSTR property = C_CAST(LPWSTR, propertyBuf); *property; property += wcslen(property) + 1)
                    {
                        if (property &&
                            (C_CAST(uintptr_t, property) - C_CAST(uintptr_t, propertyBuf)) < propertyBufLen &&
                            wcslen(property))
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
                safe_free(&propertyBuf);
            }
        }
    }
    else if (_tcsncmp(TEXT("PCI"), buffer, _tcsclen(TEXT("PCI"))) == 0)
    {
        uint32_t subsystem = UINT32_C(0);
        uint32_t revision  = UINT32_C(0);
#if IS_MSVC_VERSION(MSVC_2015)
        int scannedVals = _sntscanf_s(buffer, bufferLength,
                                      TEXT("PCI\\VEN_%") TEXT(SCNx32) TEXT("&DEV_%") TEXT(SCNx32) TEXT("&SUBSYS_%")
                                          TEXT(SCNx32) TEXT("&REV_%") TEXT(SCNx32) TEXT("\\%*s"),
                                      &device->drive_info.adapter_info.vendorID,
                                      &device->drive_info.adapter_info.productID, &subsystem, &revision);
#else
        // This is a hack around how VS2013 handles string concatenation with how the printf format macros were defined
        // for it versus newer versions.
        int scannedVals =
            _sntscanf_s(buffer, bufferLength, TEXT("PCI\\VEN_%I32x&DEV_%I32x&SUBSYS_%I32x&REV_%I32x\\%*s"),
                        &device->drive_info.adapter_info.vendorID, &device->drive_info.adapter_info.productID,
                        &subsystem, &revision);
#endif
        device->drive_info.adapter_info.vendorIDValid  = true;
        device->drive_info.adapter_info.productIDValid = true;
        device->drive_info.adapter_info.revision       = revision;
        device->drive_info.adapter_info.revisionValid  = true;
        device->drive_info.adapter_info.infoType       = ADAPTER_INFO_PCI;
        if (scannedVals < 4)
        {
#if defined(_DEBUG)
            printf("Could not scan all values. Scanned %d values\n", scannedVals);
#endif
        }
        // can also read DEVPKEY_Device_HardwareIds for parentInst to get all this data
    }
    else if (_tcsncmp(TEXT("1394"), buffer, _tcsclen(TEXT("1394"))) == 0)
    {
        // Parent buffer already contains the vendor ID as part of the buffer where a full WWN is reported...we just
        // need first 6 bytes. example, brackets added for clarity: 1394\Maxtor&5000DV__v1.00.00\[0010B9]20003D9D6E
        // DEVPKEY_Device_CompatibleIds gets use 1394 specifier ID for the device instance
        // DEVPKEY_Device_CompatibleIds gets revision and specifier ID for the parent instance:
        // 1394\<specifier>&<revision> DEVPKEY_Device_ConfigurationId gets revision and specifier ID for parent
        // instance: sbp2.inf:1394\609E&10483,sbp2_install NOTE: There is no currently known way to get the product ID
        // for this interface
        ULONG             propertyBufLen = ULONG_C(0);
        DEVPROPTYPE       propertyType   = ULONG_C(0);
        const DEVPROPKEY* propertyKey    = &DEVPKEY_Device_CompatibleIds;
        // scan buffer to get vendor
        TCHAR* nextToken = M_NULLPTR;
        TCHAR* token     = _tcstok_s(buffer, TEXT("\\"), &nextToken);
        while (token && nextToken && _tcsclen(nextToken) > 0)
        {
            token = _tcstok_s(M_NULLPTR, TEXT("\\"), &nextToken);
        }
        if (token)
        {
            // at this point, the token contains only the part we care about reading
            // We need the first 6 characters to convert into hex for the vendor ID
            DECLARE_ZERO_INIT_ARRAY(TCHAR, vendorIDString, 7);
            _tcsncpy_s(vendorIDString, 7, token, 6);
            _tprintf_s(TEXT("%s\n"), vendorIDString);
#if IS_MSVC_VERSION(MSVC_2015)
            int result = _stscanf_s(token, TEXT("%06") TEXT(SCNx32), &device->drive_info.adapter_info.vendorID);
#else
            // This is a hack around how VS2013 handles string concatenation with how the printf format macros were
            // defined for it versus newer versions.
            int result = _stscanf_s(token, TEXT("%06I32x"), &device->drive_info.adapter_info.vendorID);
#endif

            if (result == 1)
            {
                device->drive_info.adapter_info.vendorIDValid = true;
            }
        }

        device->drive_info.adapter_info.infoType = ADAPTER_INFO_IEEE1394;
        cmRet = CM_Get_DevNode_PropertyW(instance, propertyKey, &propertyType, M_NULLPTR, &propertyBufLen, 0);
        if (CR_SUCCESS == cmRet || CR_INVALID_POINTER == cmRet ||
            CR_BUFFER_SMALL == cmRet) // We'll probably get an invalid pointer or small buffer, but this will return the
                                      // size of the buffer we need, so allow it through - TJE
        {
            PBYTE propertyBuf = M_REINTERPRET_CAST(PBYTE, safe_calloc(propertyBufLen + 1, sizeof(BYTE)));
            if (propertyBuf)
            {
                propertyBufLen += 1;
                if (CR_SUCCESS ==
                    CM_Get_DevNode_PropertyW(instance, propertyKey, &propertyType, propertyBuf, &propertyBufLen, 0))
                {
                    // multiple strings can be returned for some properties. This one will most likely only return one.
                    for (LPWSTR property = C_CAST(LPWSTR, propertyBuf); *property; property += wcslen(property) + 1)
                    {
                        if (property &&
                            (C_CAST(uintptr_t, property) - C_CAST(uintptr_t, propertyBuf)) < propertyBufLen &&
                            wcslen(property))
                        {
                            int scannedVals = _snwscanf_s(C_CAST(const wchar_t*, propertyBuf), propertyBufLen,
                                                          L"1394\\%x&%x", &device->drive_info.adapter_info.specifierID,
                                                          &device->drive_info.adapter_info.revision);
                            if (scannedVals < 2)
                            {
#if defined(_DEBUG)
                                printf("Could not scan all values. Scanned %d values\n", scannedVals);
#endif
                            }
                            else
                            {
                                device->drive_info.adapter_info.specifierIDValid = true;
                                device->drive_info.adapter_info.revisionValid    = true;
                                break;
                            }
                        }
                    }
                }
                safe_free(&propertyBuf);
            }
        }
    }
    else
    {
        success = false;
    }
    return success;
}

// This function uses cfgmgr32 for figuring out the adapter information.
// It is possible to do this with setupapi as well. cfgmgr32 is supposedly available in some form for universal apps,
// whereas setupapi is not.
static eReturnValues get_Adapter_IDs(tDevice*                   device,
                                     PSTORAGE_DEVICE_DESCRIPTOR deviceDescriptor,
                                     ULONG                      deviceDescriptorLength)
{
    eReturnValues ret = BAD_PARAMETER;
    if (deviceDescriptor &&
        deviceDescriptorLength > sizeof(STORAGE_DEVICE_DESCRIPTOR)) // make sure we have a device descriptor bigger than
                                                                    // the header so we can access the raw data
    {
        // First, get the list of disk device IDs, then locate a matching one...then find the parent and parse the IDs
        // out.
        TCHAR*    listBuffer      = M_NULLPTR;
        ULONG     deviceIdListLen = ULONG_C(0);
        CONFIGRET cmRet           = CR_SUCCESS;
#if WINVER > SEA_WIN32_WINNT_VISTA
        const TCHAR* filter = TEXT("{4d36e967-e325-11ce-bfc1-08002be10318}");
        ULONG        deviceIdListFlags =
            CM_GETIDLIST_FILTER_PRESENT | CM_GETIDLIST_FILTER_CLASS; // Both of these flags require Windows 7 and later.
                                                                     // The else case below will handle older OSs
        cmRet = CM_Get_Device_ID_List_Size(&deviceIdListLen, filter, deviceIdListFlags);
        if (cmRet == CR_SUCCESS)
        {
            if (deviceIdListLen > 0)
            {
                listBuffer = M_REINTERPRET_CAST(TCHAR*, safe_calloc(deviceIdListLen, sizeof(TCHAR)));
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
        if (cmRet == CR_SUCCESS)
#endif // WINVER > SEA_WIN32_WINNT_VISTA
        {
            // older OS? Try the legacy method which should work for Win2000+
            // This requires knowing if we are searching for USB vs SCSI device IDs
            // TODO: We may need to add other things for firewire or other attachment types that existed back in Vista
            // or XP if they aren't handled under USB or SCSI
            const TCHAR* scsiFilter = TEXT("SCSI");
            const TCHAR* usbFilter = TEXT("USBSTOR"); // Need to use USBSTOR in order to find a match. Using USB returns
                                                      // a list of VID/PID but we don't have a way to match that.
            ULONG     scsiIdListLen = ULONG_C(0);
            ULONG     usbIdListLen  = ULONG_C(0);
            ULONG     filterFlags   = CM_GETIDLIST_FILTER_ENUMERATOR;
            TCHAR*    scsiListBuff  = M_NULLPTR;
            TCHAR*    usbListBuff   = M_NULLPTR;
            CONFIGRET scsicmRet     = CR_SUCCESS;
            CONFIGRET usbcmRet      = CR_SUCCESS;
            // First get the SCSI list, then the USB list/ TODO: add more things to the list as we need them.
            scsicmRet = CM_Get_Device_ID_List_Size(&scsiIdListLen, scsiFilter, filterFlags);
            if (scsicmRet == CR_SUCCESS)
            {
                if (scsiIdListLen > 0)
                {
                    scsiListBuff = M_REINTERPRET_CAST(TCHAR*, safe_calloc(scsiIdListLen, sizeof(TCHAR)));
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
                    usbListBuff = M_REINTERPRET_CAST(TCHAR*, safe_calloc(usbIdListLen, sizeof(TCHAR)));
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
            // now that we got USB and SCSI, we need to merge them together into a common list
            deviceIdListLen = scsiIdListLen + usbIdListLen;
            listBuffer      = M_REINTERPRET_CAST(TCHAR*, safe_calloc(deviceIdListLen, sizeof(TCHAR)));
            if (listBuffer)
            {
                ULONG copyOffset = ULONG_C(0);
                if (scsicmRet == CR_SUCCESS && scsiIdListLen > 0 && scsiListBuff)
                {
                    cmRet = CR_SUCCESS; // set the status as SUCCESS, as we have list of SCSI drives to use later
                    safe_memcpy(&listBuffer[copyOffset], deviceIdListLen - copyOffset, scsiListBuff, scsiIdListLen);
                    copyOffset += scsiIdListLen - 1;
                }
                if (usbcmRet == CR_SUCCESS && usbIdListLen > 0 && usbListBuff)
                {
                    cmRet = CR_SUCCESS; // set the status as SUCCESS, as we have list of USB drives to use later
                    safe_memcpy(&listBuffer[copyOffset], deviceIdListLen - copyOffset, usbListBuff, usbIdListLen);
                    copyOffset += usbIdListLen - 1;
                }
                // add other lists here and offset them as needed
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
            safe_free(&scsiListBuff);
            safe_free(&usbListBuff);
        }
        else
        {
            return FAILURE;
        }
        // If we are here, we should have a list of device IDs to check
        // First, reduce the list to something that seems like it is the drive we are looking for...we could have more
        // than 1 match, but we're filtering out all the extras that definitely aren't the correct device.
        if (ret != MEMORY_FAILURE && cmRet == CR_SUCCESS && deviceIdListLen > 0)
        {
            bool foundMatch = false;
            // Now we have a list of device IDs.
            // Each device ID is structured with a EmumeratorName\Disk&Ven_<vendor ID>&Prod_<Product ID>&Rev_<Revision
            // number>\<some random numbers and characters> Since we have a device descriptor, we have the vendor ID,
            // product ID, and revision number, so we can match those. NOTE: Some string matching is possible, but not
            // reliable. All string matching was removed due to it not quite working as expected when it was needed the
            // most. This is potentially slower without it, but it will be fine...-TJE

            // loop through the device IDs and see if we find anything that matches.
            for (LPTSTR deviceID = listBuffer; *deviceID && !foundMatch; deviceID += _tcslen(deviceID) + 1)
            {
                DEVINST deviceInstance = 0;
                // if a match is found, call locate devnode. If this is not present, it will fail and we need to
                // continue through the loop convert the deviceID to uppercase to make matching easier
                _tcsupr_s(deviceID, _tcslen(deviceID) + 1); //+1 for the NULL terminator otherwise this will fail
                cmRet = CM_Locate_DevNode(&deviceInstance, deviceID, CM_LOCATE_DEVNODE_NORMAL);
                if (CR_SUCCESS == cmRet)
                {
                    // with the device node, get the interface list for this device (disk class GUID is used). This
                    // SHOULD only return one idem which is the full device path. TODO: save this device path
                    ULONG interfaceListSize = ULONG_C(0);
                    GUID  classGUID = GUID_DEVINTERFACE_DISK; // TODO: If the tDevice handle that was opened was a tape,
                                                              // changer or something else, change this GUID.
                    cmRet = CM_Get_Device_Interface_List_Size(&interfaceListSize, &classGUID, deviceID,
                                                              CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
                    if (CR_SUCCESS == cmRet && interfaceListSize > 0)
                    {
                        TCHAR* interfaceList =
                            M_REINTERPRET_CAST(TCHAR*, safe_calloc(interfaceListSize, sizeof(TCHAR)));
                        if (interfaceList)
                        {
                            cmRet = CM_Get_Device_Interface_List(&classGUID, deviceID, interfaceList, interfaceListSize,
                                                                 CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
                            if (CR_SUCCESS == cmRet)
                            {
                                // Loop through this list, just in case more than one thing comes through
                                for (LPTSTR currentDeviceID = interfaceList; *currentDeviceID && !foundMatch;
                                     currentDeviceID += _tcslen(currentDeviceID) + 1)
                                {
                                    // With this device path, open a handle and get the storage device number. This is a
                                    // match for the PhysicalDriveX number and we can check that for a match
                                    HANDLE deviceHandle = INVALID_HANDLE_VALUE;
                                    deviceHandle        = CreateFile(currentDeviceID, GENERIC_WRITE | GENERIC_READ,
                                                                     FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR,
                                                                     OPEN_EXISTING, 0, M_NULLPTR);
                                    if (deviceHandle && deviceHandle != INVALID_HANDLE_VALUE)
                                    {
                                        // If the storage device number matches, get the parent device instance, then
                                        // the parent device ID. This will contain the USB VID/PID and PCI Vendor,
                                        // product, and revision numbers.
                                        STORAGE_DEVICE_NUMBER deviceNumber;
                                        safe_memset(&deviceNumber, sizeof(STORAGE_DEVICE_NUMBER), 0,
                                                    sizeof(STORAGE_DEVICE_NUMBER));
                                        DWORD returnedDataSize = DWORD_C(0);
                                        if (DeviceIoControl(deviceHandle, IOCTL_STORAGE_GET_DEVICE_NUMBER, M_NULLPTR, 0,
                                                            &deviceNumber, sizeof(STORAGE_DEVICE_NUMBER),
                                                            &returnedDataSize, M_NULLPTR))
                                        {
                                            if (deviceNumber.DeviceNumber == device->os_info.os_drive_number)
                                            {
                                                DEVINST parentInst = 0;
                                                foundMatch         = true;
                                                // Now that we have a matching handle, get the parent information, then
                                                // parse the values from that string
                                                cmRet = CM_Get_Parent(&parentInst, deviceInstance, 0);
                                                if (CR_SUCCESS == cmRet)
                                                {
                                                    ULONG parentLen = ULONG_C(0);
                                                    cmRet           = CM_Get_Device_ID_Size(&parentLen, parentInst, 0);
                                                    parentLen += 1;
                                                    if (CR_SUCCESS == cmRet)
                                                    {
                                                        TCHAR* parentBuffer = M_REINTERPRET_CAST(
                                                            TCHAR*, safe_calloc(parentLen, sizeof(TCHAR)));
                                                        if (parentBuffer)
                                                        {
                                                            cmRet = CM_Get_Device_ID(parentInst, parentBuffer,
                                                                                     parentLen, 0);
                                                            if (CR_SUCCESS == cmRet)
                                                            {
                                                                // uncomment this else case to view all the possible
                                                                // device or parent properties when figuring out what
                                                                // else is necessary to store for a new device.
                                                                /*  //This is a comment switch. two slashes means
                                                                uncomment the below, 1 means comment it out
                                                                {
                                                                    ULONG propertyBufLen = ULONG_C(0);
                                                                    DEVPROPTYPE propertyType = ULONG_C(0);
                                                                    DEVINST propInst = deviceInstance;
                                                                    const DEVPROPKEY *devproperty = &DEVPKEY_NAME;
                                                                    uint16_t counter = UINT16_C(0);
                                                                    uint16_t instanceCounter = UINT16_C(0);
                                                                    char *propertyName = M_NULLPTR;
                                                                    size_t propertyNameLength = SIZE_T_C(0);
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
                                                                            //get name of property being checked.
                                                                            switch (counter)
                                                                            {
                                                                            case 0:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DeviceDesc; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DeviceDesc") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DeviceDesc"); break;
                                                                            case 1:
                                                                                devproperty =
                                                                &DEVPKEY_Device_HardwareIds; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_HardwareIds") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_HardwareIds");
                                                                                break;
                                                                            case 2:
                                                                                devproperty =
                                                                &DEVPKEY_Device_CompatibleIds; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_CompatibleIds") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_CompatibleIds");
                                                                                break;
                                                                            case 3:
                                                                                devproperty = &DEVPKEY_Device_Service;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Service") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Service"); break; case 4: devproperty =
                                                                &DEVPKEY_Device_Class; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Class") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Class"); break; case 5: devproperty =
                                                                &DEVPKEY_Device_ClassGuid; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ClassGuid") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ClassGuid"); break;
                                                                            case 6:
                                                                                devproperty = &DEVPKEY_Device_Driver;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Driver") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Driver"); break; case 7: devproperty =
                                                                &DEVPKEY_Device_ConfigFlags; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ConfigFlags") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ConfigFlags");
                                                                                break;
                                                                            case 8:
                                                                                devproperty =
                                                                &DEVPKEY_Device_Manufacturer; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Manufacturer") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_Manufacturer");
                                                                                break;
                                                                            case 9:
                                                                                devproperty =
                                                                &DEVPKEY_Device_FriendlyName; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_FriendlyName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_FriendlyName");
                                                                                break;
                                                                            case 10:
                                                                                devproperty =
                                                                &DEVPKEY_Device_LocationInfo; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_LocationInfo") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_LocationInfo");
                                                                                break;
                                                                            case 11:
                                                                                devproperty = &DEVPKEY_Device_PDOName;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_PDOName") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_PDOName"); break; case 12: devproperty =
                                                                &DEVPKEY_Device_Capabilities; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Capabilities") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_Capabilities");
                                                                                break;
                                                                            case 13:
                                                                                devproperty = &DEVPKEY_Device_UINumber;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_UINumber") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_UINumber"); break; case 14: devproperty
                                                                = &DEVPKEY_Device_UpperFilters; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_UpperFilters") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_UpperFilters");
                                                                                break;
                                                                            case 15:
                                                                                devproperty =
                                                                &DEVPKEY_Device_LowerFilters; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_LowerFilters") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_LowerFilters");
                                                                                break;
                                                                            case 16:
                                                                                devproperty =
                                                                &DEVPKEY_Device_BusTypeGuid; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_BusTypeGuid") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_BusTypeGuid");
                                                                                break;
                                                                            case 17:
                                                                                devproperty =
                                                                &DEVPKEY_Device_LegacyBusType; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_LegacyBusType") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_LegacyBusType");
                                                                                break;
                                                                            case 18:
                                                                                devproperty = &DEVPKEY_Device_BusNumber;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_BusNumber") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_BusNumber"); break;
                                                                            case 19:
                                                                                devproperty =
                                                                &DEVPKEY_Device_EnumeratorName; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_EnumeratorName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_EnumeratorName");
                                                                                break;
                                                                            case 20:
                                                                                devproperty = &DEVPKEY_Device_Security;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Security") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Security"); break; case 21: devproperty
                                                                = &DEVPKEY_Device_SecuritySDS; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_SecuritySDS") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_SecuritySDS");
                                                                                break;
                                                                            case 22:
                                                                                devproperty = &DEVPKEY_Device_DevType;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DevType") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_DevType"); break; case 23: devproperty =
                                                                &DEVPKEY_Device_Exclusive; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Exclusive") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_Exclusive"); break;
                                                                            case 24:
                                                                                devproperty =
                                                                &DEVPKEY_Device_Characteristics; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Characteristics") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_Characteristics");
                                                                                break;
                                                                            case 25:
                                                                                devproperty = &DEVPKEY_Device_Address;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Address") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Address"); break; case 26: devproperty =
                                                                &DEVPKEY_Device_UINumberDescFormat; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_UINumberDescFormat") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_UINumberDescFormat"); break; case 27:
                                                                                devproperty = &DEVPKEY_Device_PowerData;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_PowerData") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_PowerData");
                                                                                //type = CM_POWER_DATA
                                                                                break;
                                                                            case 28:
                                                                                devproperty =
                                                                &DEVPKEY_Device_RemovalPolicy; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_RemovalPolicy") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_RemovalPolicy");
                                                                                break;
                                                                            case 29:
                                                                                devproperty =
                                                                &DEVPKEY_Device_RemovalPolicyDefault; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_RemovalPolicyDefault") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_RemovalPolicyDefault"); break; case 30:
                                                                                devproperty =
                                                                &DEVPKEY_Device_RemovalPolicyOverride;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_RemovalPolicyOverride") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_RemovalPolicyOverride"); break; case 31:
                                                                                devproperty =
                                                                &DEVPKEY_Device_InstallState; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_InstallState") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_InstallState");
                                                                                break;
                                                                            case 32:
                                                                                devproperty =
                                                                &DEVPKEY_Device_LocationPaths; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_LocationPaths") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_LocationPaths");
                                                                                break;
                                                                            case 33:
                                                                                devproperty =
                                                                &DEVPKEY_Device_BaseContainerId; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_BaseContainerId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_BaseContainerId");
                                                                                break;
                                                                            case 34:
                                                                                devproperty =
                                                                &DEVPKEY_Device_InstanceId; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_InstanceId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_InstanceId"); break;
                                                                            case 35:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DevNodeStatus; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DevNodeStatus") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DevNodeStatus");
                                                                                break;
                                                                            case 36:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ProblemCode; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ProblemCode") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ProblemCode");
                                                                                break;
                                                                            case 37:
                                                                                devproperty =
                                                                &DEVPKEY_Device_EjectionRelations; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_EjectionRelations") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_EjectionRelations");
                                                                                break;
                                                                            case 38:
                                                                                devproperty =
                                                                &DEVPKEY_Device_RemovalRelations; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_RemovalRelations") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_RemovalRelations");
                                                                                break;
                                                                            case 39:
                                                                                devproperty =
                                                                &DEVPKEY_Device_PowerRelations; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_PowerRelations") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_PowerRelations");
                                                                                break;
                                                                            case 40:
                                                                                devproperty =
                                                                &DEVPKEY_Device_BusRelations; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_BusRelations") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_BusRelations");
                                                                                break;
                                                                            case 41:
                                                                                devproperty = &DEVPKEY_Device_Parent;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Parent") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Parent"); break; case 42: devproperty =
                                                                &DEVPKEY_Device_Children; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Children") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Children"); break; case 43: devproperty
                                                                = &DEVPKEY_Device_Siblings; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Siblings") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Siblings"); break; case 44: devproperty
                                                                = &DEVPKEY_Device_TransportRelations; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_TransportRelations") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_TransportRelations"); break; case 45:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ProblemStatus; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ProblemStatus") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ProblemStatus");
                                                                                break;
                                                                            case 46:
                                                                                devproperty = &DEVPKEY_Device_Reported;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Reported") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Reported"); break; case 47: devproperty
                                                                = &DEVPKEY_Device_Legacy; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Legacy") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Legacy"); break; case 48: devproperty =
                                                                &DEVPKEY_Device_ContainerId; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ContainerId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ContainerId");
                                                                                break;
                                                                            case 49:
                                                                                devproperty =
                                                                &DEVPKEY_Device_InLocalMachineContainer;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_InLocalMachineContainer") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_InLocalMachineContainer"); break; case
                                                                50: devproperty = &DEVPKEY_Device_Model;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Model") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Model"); break; case 51: devproperty =
                                                                &DEVPKEY_Device_ModelId; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ModelId") + 1; propertyName
                                                                = M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_ModelId"); break; case 52: devproperty =
                                                                &DEVPKEY_Device_FriendlyNameAttributes;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_FriendlyNameAttributes") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_FriendlyNameAttributes"); break; case
                                                                53: devproperty =
                                                                &DEVPKEY_Device_ManufacturerAttributes;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ManufacturerAttributes") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_ManufacturerAttributes"); break; case
                                                                54: devproperty = &DEVPKEY_Device_PresenceNotForDevice;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_PresenceNotForDevice") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_PresenceNotForDevice"); break; case 55:
                                                                                devproperty =
                                                                &DEVPKEY_Device_SignalStrength; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_SignalStrength") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_SignalStrength");
                                                                                break;
                                                                            case 56:
                                                                                devproperty =
                                                                &DEVPKEY_Device_IsAssociateableByUserAction;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_IsAssociateableByUserAction")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_IsAssociateableByUserAction"); break;
                                                                            case 57:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ShowInUninstallUI; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ShowInUninstallUI") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ShowInUninstallUI");
                                                                                break;
                                                                            case 58:
                                                                                devproperty =
                                                                &DEVPKEY_Device_Numa_Proximity_Domain;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Numa_Proximity_Domain") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_Numa_Proximity_Domain"); break; case 59:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DHP_Rebalance_Policy; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_DHP_Rebalance_Policy") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_DHP_Rebalance_Policy"); break; case 60:
                                                                                devproperty = &DEVPKEY_Device_Numa_Node;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Numa_Node") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_Numa_Node"); break;
                                                                            case 61:
                                                                                devproperty =
                                                                &DEVPKEY_Device_BusReportedDeviceDesc;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_BusReportedDeviceDesc") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_BusReportedDeviceDesc"); break; case 62:
                                                                                devproperty = &DEVPKEY_Device_IsPresent;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_IsPresent") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_IsPresent"); break;
                                                                            case 63:
                                                                                devproperty =
                                                                &DEVPKEY_Device_HasProblem; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_HasProblem") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_HasProblem"); break;
                                                                            case 64:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ConfigurationId; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ConfigurationId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ConfigurationId");
                                                                                break;
                                                                            case 65:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ReportedDeviceIdsHash;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ReportedDeviceIdsHash") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_ReportedDeviceIdsHash"); break; case 66:
                                                                                devproperty =
                                                                &DEVPKEY_Device_PhysicalDeviceLocation;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_PhysicalDeviceLocation") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_PhysicalDeviceLocation"); break; case
                                                                67: devproperty = &DEVPKEY_Device_BiosDeviceName;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_BiosDeviceName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_BiosDeviceName");
                                                                                break;
                                                                            case 68:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverProblemDesc; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverProblemDesc") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverProblemDesc");
                                                                                break;
                                                                            case 69:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DebuggerSafe; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DebuggerSafe") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DebuggerSafe");
                                                                                break;
                                                                            case 70:
                                                                                devproperty =
                                                                &DEVPKEY_Device_PostInstallInProgress;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_PostInstallInProgress") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_PostInstallInProgress"); break; case 71:
                                                                                devproperty = &DEVPKEY_Device_Stack;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_Stack") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_Device_Stack"); break; case 72: devproperty =
                                                                &DEVPKEY_Device_ExtendedConfigurationIds;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ExtendedConfigurationIds") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_ExtendedConfigurationIds"); break; case
                                                                73: devproperty = &DEVPKEY_Device_IsRebootRequired;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_IsRebootRequired") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_IsRebootRequired");
                                                                                break;
                                                                            case 74:
                                                                                devproperty =
                                                                &DEVPKEY_Device_FirmwareDate; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_FirmwareDate") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_FirmwareDate");
                                                                                break;
                                                                            case 75:
                                                                                devproperty =
                                                                &DEVPKEY_Device_FirmwareVersion; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_FirmwareVersion") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_FirmwareVersion");
                                                                                break;
                                                                            case 76:
                                                                                devproperty =
                                                                &DEVPKEY_Device_FirmwareRevision; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_FirmwareRevision") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_FirmwareRevision");
                                                                                break;
                                                                            case 77:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DependencyProviders; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_DependencyProviders") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_DependencyProviders"); break; case 78:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DependencyDependents; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_DependencyDependents") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_DependencyDependents"); break; case 79:
                                                                                devproperty =
                                                                &DEVPKEY_Device_SoftRestartSupported; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_SoftRestartSupported") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_SoftRestartSupported"); break; case 80:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ExtendedAddress; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ExtendedAddress") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_ExtendedAddress");
                                                                                break;
                                                                            case 81:
                                                                                devproperty = &DEVPKEY_Device_SessionId;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_SessionId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_SessionId"); break;
                                                                            case 82:
                                                                                devproperty =
                                                                &DEVPKEY_Device_InstallDate; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_InstallDate") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_InstallDate");
                                                                                break;
                                                                            case 83:
                                                                                devproperty =
                                                                &DEVPKEY_Device_FirstInstallDate; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_FirstInstallDate") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_FirstInstallDate");
                                                                                break;
                                                                            case 84:
                                                                                devproperty =
                                                                &DEVPKEY_Device_LastArrivalDate; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_LastArrivalDate") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_LastArrivalDate");
                                                                                break;
                                                                            case 85:
                                                                                devproperty =
                                                                &DEVPKEY_Device_LastRemovalDate; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_LastRemovalDate") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_LastRemovalDate");
                                                                                break;
                                                                            case 86:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverDate; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverDate") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverDate"); break;
                                                                            case 87:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverVersion; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverVersion") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverVersion");
                                                                                break;
                                                                            case 88:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverDesc; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverDesc") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverDesc"); break;
                                                                            case 89:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverInfPath; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverInfPath") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverInfPath");
                                                                                break;
                                                                            case 90:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverInfSection; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverInfSection") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverInfSection");
                                                                                break;
                                                                            case 91:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverInfSectionExt; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_DriverInfSectionExt") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_DriverInfSectionExt"); break; case 92:
                                                                                devproperty =
                                                                &DEVPKEY_Device_MatchingDeviceId; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_MatchingDeviceId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_MatchingDeviceId");
                                                                                break;
                                                                            case 93:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverProvider; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverProvider") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverProvider");
                                                                                break;
                                                                            case 94:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverPropPageProvider;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverPropPageProvider") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_DriverPropPageProvider"); break; case
                                                                95: devproperty = &DEVPKEY_Device_DriverCoInstallers;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverCoInstallers") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_DriverCoInstallers"); break; case 96:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ResourcePickerTags; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ResourcePickerTags") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_ResourcePickerTags"); break; case 97:
                                                                                devproperty =
                                                                &DEVPKEY_Device_ResourcePickerExceptions;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_ResourcePickerExceptions") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_ResourcePickerExceptions"); break; case
                                                                98: devproperty = &DEVPKEY_Device_DriverRank;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverRank") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverRank"); break;
                                                                            case 99:
                                                                                devproperty =
                                                                &DEVPKEY_Device_DriverLogoLevel; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_DriverLogoLevel") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_DriverLogoLevel");
                                                                                break;
                                                                            case 100:
                                                                                devproperty =
                                                                &DEVPKEY_Device_NoConnectSound; propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_NoConnectSound") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_Device_NoConnectSound");
                                                                                break;
                                                                            case 101:
                                                                                devproperty =
                                                                &DEVPKEY_Device_GenericDriverInstalled;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_GenericDriverInstalled") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_GenericDriverInstalled"); break; case
                                                                102: devproperty =
                                                                &DEVPKEY_Device_AdditionalSoftwareRequested;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_AdditionalSoftwareRequested")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_AdditionalSoftwareRequested"); break;
                                                                            case 103:
                                                                                devproperty =
                                                                &DEVPKEY_Device_SafeRemovalRequired; propertyNameLength
                                                                = safe_strlen("DEVPKEY_Device_SafeRemovalRequired") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_SafeRemovalRequired"); break; case 104:
                                                                                devproperty =
                                                                &DEVPKEY_Device_SafeRemovalRequiredOverride;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_Device_SafeRemovalRequiredOverride")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_Device_SafeRemovalRequiredOverride"); break;
                                                                            case 105:
                                                                                devproperty = &DEVPKEY_DrvPkg_Model;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DrvPkg_Model") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_DrvPkg_Model"); break; case 106: devproperty =
                                                                &DEVPKEY_DrvPkg_VendorWebSite; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DrvPkg_VendorWebSite") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DrvPkg_VendorWebSite");
                                                                                break;
                                                                            case 107:
                                                                                devproperty =
                                                                &DEVPKEY_DrvPkg_DetailedDescription; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DrvPkg_DetailedDescription") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DrvPkg_DetailedDescription"); break; case 108:
                                                                                devproperty =
                                                                &DEVPKEY_DrvPkg_DocumentationLink; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DrvPkg_DocumentationLink") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DrvPkg_DocumentationLink");
                                                                                break;
                                                                            case 109:
                                                                                devproperty = &DEVPKEY_DrvPkg_Icon;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DrvPkg_Icon") + 1; propertyName =
                                                                M_REINTERPRET_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                snprintf_err_handle(propertyName, propertyNameLength,
                                                                "DEVPKEY_DrvPkg_Icon"); break; case 110: devproperty =
                                                                &DEVPKEY_DrvPkg_BrandingIcon; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DrvPkg_BrandingIcon") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DrvPkg_BrandingIcon");
                                                                                break;
                                                                            case 111:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_UpperFilters; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_UpperFilters") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_UpperFilters");
                                                                                break;
                                                                            case 112:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_LowerFilters; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_LowerFilters") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_LowerFilters");
                                                                                break;
                                                                            case 113:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_Security; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_Security") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_Security");
                                                                                break;
                                                                            case 114:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_SecuritySDS; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_SecuritySDS") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_SecuritySDS");
                                                                                break;
                                                                            case 115:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_DevType; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_DevType") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_DevType");
                                                                                break;
                                                                            case 116:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_Exclusive; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_Exclusive") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_Exclusive");
                                                                                break;
                                                                            case 117:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_Characteristics; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceClass_Characteristics") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_Characteristics"); break; case 118:
                                                                                devproperty = &DEVPKEY_DeviceClass_Name;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_Name") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_Name"); break;
                                                                            case 119:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_ClassName; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_ClassName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_ClassName");
                                                                                break;
                                                                            case 120:
                                                                                devproperty = &DEVPKEY_DeviceClass_Icon;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_Icon") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_Icon"); break;
                                                                            case 121:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_ClassInstaller; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceClass_ClassInstaller") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_ClassInstaller"); break; case 122:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_PropPageProvider;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_PropPageProvider") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_PropPageProvider"); break; case
                                                                123: devproperty = &DEVPKEY_DeviceClass_NoInstallClass;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_NoInstallClass") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_NoInstallClass"); break; case 124:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_NoDisplayClass; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceClass_NoDisplayClass") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_NoDisplayClass"); break; case 125:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_SilentInstall; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_SilentInstall") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_SilentInstall"); break; case 126:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_NoUseClass; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_NoUseClass") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_NoUseClass");
                                                                                break;
                                                                            case 127:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_DefaultService; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceClass_DefaultService") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_DefaultService"); break; case 128:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_IconPath; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_IconPath") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceClass_IconPath");
                                                                                break;
                                                                            case 129:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceClass_DHPRebalanceOptOut;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_DHPRebalanceOptOut") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_DHPRebalanceOptOut"); break; case
                                                                130: devproperty =
                                                                &DEVPKEY_DeviceClass_ClassCoInstallers;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceClass_ClassCoInstallers") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceClass_ClassCoInstallers"); break; case
                                                                131: devproperty =
                                                                &DEVPKEY_DeviceInterface_FriendlyName;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_FriendlyName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterface_FriendlyName"); break; case
                                                                132: devproperty = &DEVPKEY_DeviceInterface_Enabled;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_Enabled") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceInterface_Enabled");
                                                                                break;
                                                                            case 133:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceInterface_ClassGuid; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_ClassGuid") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterface_ClassGuid"); break; case 134:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceInterface_ReferenceString;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_ReferenceString") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterface_ReferenceString"); break; case
                                                                135: devproperty = &DEVPKEY_DeviceInterface_Restricted;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_Restricted") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterface_Restricted"); break; case 136:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceInterface_UnrestrictedAppCapabilities;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_UnrestrictedAppCapabilities")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterface_UnrestrictedAppCapabilities");
                                                                                break;
                                                                            case 137:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceInterface_SchematicName;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterface_SchematicName") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterface_SchematicName"); break; case
                                                                138: devproperty =
                                                                &DEVPKEY_DeviceInterfaceClass_DefaultInterface;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterfaceClass_DefaultInterface")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterfaceClass_DefaultInterface"); break;
                                                                            case 139:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceInterfaceClass_Name; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceInterfaceClass_Name") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceInterfaceClass_Name"); break; case 140:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Address; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Address") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceContainer_Address");
                                                                                break;
                                                                            case 141:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_DiscoveryMethod;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_DiscoveryMethod") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_DiscoveryMethod"); break; case
                                                                142: devproperty = &DEVPKEY_DeviceContainer_IsEncrypted;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsEncrypted") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsEncrypted"); break; case 143:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_IsAuthenticated;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsAuthenticated") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsAuthenticated"); break; case
                                                                144: devproperty = &DEVPKEY_DeviceContainer_IsConnected;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsConnected") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsConnected"); break; case 145:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_IsPaired; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsPaired") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceContainer_IsPaired");
                                                                                break;
                                                                            case 146:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Icon; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Icon") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceContainer_Icon");
                                                                                break;
                                                                            case 147:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Version; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Version") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceContainer_Version");
                                                                                break;
                                                                            case 148:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Last_Seen; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Last_Seen") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_Last_Seen"); break; case 149:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Last_Connected;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Last_Connected") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_Last_Connected"); break; case
                                                                150: devproperty =
                                                                &DEVPKEY_DeviceContainer_IsShowInDisconnectedState;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsShowInDisconnectedState")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsShowInDisconnectedState");
                                                                                break;
                                                                            case 151:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_IsLocalMachine;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsLocalMachine") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsLocalMachine"); break; case
                                                                152: devproperty =
                                                                &DEVPKEY_DeviceContainer_MetadataPath;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_MetadataPath") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_MetadataPath"); break; case
                                                                153: devproperty =
                                                                &DEVPKEY_DeviceContainer_IsMetadataSearchInProgress;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsMetadataSearchInProgress")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsMetadataSearchInProgress");
                                                                                break;
                                                                            case 154:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_MetadataChecksum;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_MetadataChecksum")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_MetadataChecksum"); break; case
                                                                155: devproperty =
                                                                &DEVPKEY_DeviceContainer_IsNotInterestingForDisplay;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsNotInterestingForDisplay")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsNotInterestingForDisplay");
                                                                                break;
                                                                            case 156:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_LaunchDeviceStageOnDeviceConnect;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_LaunchDeviceStageOnDeviceConnect")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_LaunchDeviceStageOnDeviceConnect");
                                                                                break;
                                                                            case 157:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_LaunchDeviceStageFromExplorer;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_LaunchDeviceStageFromExplorer")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_LaunchDeviceStageFromExplorer");
                                                                                break;
                                                                            case 158:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_BaselineExperienceId;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_BaselineExperienceId")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_BaselineExperienceId"); break;
                                                                            case 159:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_IsDeviceUniquelyIdentifiable;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsDeviceUniquelyIdentifiable")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsDeviceUniquelyIdentifiable");
                                                                                break;
                                                                            case 160:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_AssociationArray;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_AssociationArray")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_AssociationArray"); break; case
                                                                161: devproperty =
                                                                &DEVPKEY_DeviceContainer_DeviceDescription1;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_DeviceDescription1")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_DeviceDescription1"); break;
                                                                            case 162:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_DeviceDescription2;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_DeviceDescription2")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_DeviceDescription2"); break;
                                                                            case 163:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_HasProblem; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceContainer_HasProblem") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_HasProblem"); break; case 164:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_IsSharedDevice;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsSharedDevice") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsSharedDevice"); break; case
                                                                165: devproperty =
                                                                &DEVPKEY_DeviceContainer_IsNetworkDevice;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsNetworkDevice") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsNetworkDevice"); break; case
                                                                166: devproperty =
                                                                &DEVPKEY_DeviceContainer_IsDefaultDevice;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsDefaultDevice") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsDefaultDevice"); break; case
                                                                167: devproperty =
                                                                &DEVPKEY_DeviceContainer_MetadataCabinet;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_MetadataCabinet") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_MetadataCabinet"); break; case
                                                                168: devproperty =
                                                                &DEVPKEY_DeviceContainer_RequiresPairingElevation;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_RequiresPairingElevation")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_RequiresPairingElevation");
                                                                                break;
                                                                            case 169:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_ExperienceId;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_ExperienceId") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_ExperienceId"); break; case
                                                                170: devproperty = &DEVPKEY_DeviceContainer_Category;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceDEVPKEY_DeviceContainer_Category_HardwareIds")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DeviceContainer_Category");
                                                                                break;
                                                                            case 171:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Category_Desc_Singular;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Category_Desc_Singular")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_Category_Desc_Singular");
                                                                                break;
                                                                            case 172:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Category_Desc_Plural;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Category_Desc_Plural")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_Category_Desc_Plural"); break;
                                                                            case 173:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_Category_Icon;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Category_Icon") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_Category_Icon"); break; case
                                                                174: devproperty =
                                                                &DEVPKEY_DeviceContainer_CategoryGroup_Desc;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_CategoryGroup_Desc")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_CategoryGroup_Desc"); break;
                                                                            case 175:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_CategoryGroup_Icon;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_CategoryGroup_Icon")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_CategoryGroup_Icon"); break;
                                                                            case 176:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_PrimaryCategory;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_PrimaryCategory") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_PrimaryCategory"); break; case
                                                                178: devproperty =
                                                                &DEVPKEY_DeviceContainer_UnpairUninstall;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_UnpairUninstall") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_UnpairUninstall"); break; case
                                                                179: devproperty =
                                                                &DEVPKEY_DeviceContainer_RequiresUninstallElevation;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_RequiresUninstallElevation")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_RequiresUninstallElevation");
                                                                                break;
                                                                            case 180:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_DeviceFunctionSubRank;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_DeviceFunctionSubRank")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_DeviceFunctionSubRank"); break;
                                                                            case 181:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected");
                                                                                break;
                                                                            case 182:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_ConfigFlags; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceContainer_ConfigFlags") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_ConfigFlags"); break; case 183:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_PrivilegedPackageFamilyNames;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_PrivilegedPackageFamilyNames")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_PrivilegedPackageFamilyNames");
                                                                                break;
                                                                            case 184:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_CustomPrivilegedPackageFamilyNames;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_CustomPrivilegedPackageFamilyNames")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_CustomPrivilegedPackageFamilyNames");
                                                                                break;
                                                                            case 185:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_IsRebootRequired;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_IsRebootRequired")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_IsRebootRequired"); break; case
                                                                186: devproperty =
                                                                &DEVPKEY_DeviceContainer_FriendlyName;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_FriendlyName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_FriendlyName"); break; case
                                                                187: devproperty =
                                                                &DEVPKEY_DeviceContainer_Manufacturer;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_Manufacturer") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_Manufacturer"); break; case
                                                                188: devproperty = &DEVPKEY_DeviceContainer_ModelName;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_ModelName") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_ModelName"); break; case 189:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_ModelNumber; propertyNameLength
                                                                = safe_strlen("DEVPKEY_DeviceContainer_ModelNumber") +
                                                                1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_ModelNumber"); break; case 190:
                                                                                devproperty =
                                                                &DEVPKEY_DeviceContainer_InstallInProgress;
                                                                                propertyNameLength =
                                                                safe_strlen("DEVPKEY_DeviceContainer_InstallInProgress")
                                                                + 1; propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength,
                                                                "DEVPKEY_DeviceContainer_InstallInProgress"); break;
                                                                            case 191:
                                                                                devproperty =
                                                                &DEVPKEY_DevQuery_ObjectType; propertyNameLength =
                                                                safe_strlen("DEVPKEY_DevQuery_ObjectType") + 1;
                                                                                propertyName = C_CAST(char*,
                                                                safe_calloc(propertyNameLength, sizeof(char)));
                                                                                snprintf_err_handle(propertyName,
                                                                propertyNameLength, "DEVPKEY_DevQuery_ObjectType");
                                                                                break;
                                                                            default:
                                                                                devproperty = M_NULLPTR;
                                                                                break;
                                                                            }
                                                                            propertyBufLen = 0;
                                                                            cmRet = CM_Get_DevNode_PropertyW(propInst,
                                                                devproperty, &propertyType, M_NULLPTR, &propertyBufLen,
                                                                0); if (CR_SUCCESS == cmRet || CR_INVALID_POINTER ==
                                                                cmRet || CR_BUFFER_SMALL == cmRet)//We'll probably get
                                                                an invalid pointer or small buffer, but this will return
                                                                the size of the buffer we need, so allow it through -
                                                                TJE
                                                                            {
                                                                                PBYTE propertyBuf = C_CAST(PBYTE,
                                                                safe_calloc(propertyBufLen + 1, sizeof(BYTE))); if
                                                                (propertyBuf)
                                                                                {
                                                                                    propertyBufLen += 1;
                                                                                    cmRet =
                                                                CM_Get_DevNode_PropertyW(propInst, devproperty,
                                                                &propertyType, propertyBuf, &propertyBufLen, 0); if
                                                                (CR_SUCCESS == cmRet)
                                                                                    {
                                                                                        DEVPROPTYPE propertyModifier =
                                                                propertyType & DEVPROP_MASK_TYPEMOD;
                                                                                        //print the property name here
                                                                in case anything fails above so we don't print a bunch
                                                                of empty properties - TJE
                                                                                        printf("=========================================================================\n");
                                                                                        printf(" %s: \n", propertyName);
                                                                                        switch (propertyType &
                                                                DEVPROP_MASK_TYPE)//need to mask as there may also be
                                                                modifiers to notate lists, etc
                                                                                        {
                                                                                        case DEVPROP_TYPE_STRING:
                                                                                            // Fall-through //
                                                                                        case DEVPROP_TYPE_STRING_LIST:
                                                                                            //setup to handle multiple
                                                                strings
                                                                                        {
                                                                                            uint8_t
                                                                propListAdditionalLen = propertyModifier ==
                                                                DEVPROP_TYPEMOD_LIST ? 1 : 0;//this adjusts the loop
                                                                because if this ISN'T set, then we don't need any more
                                                                length than the string length for (LPWSTR property =
                                                                C_CAST(LPWSTR, propertyBuf); *property; property +=
                                                                wcslen(property) + propListAdditionalLen)
                                                                                            {
                                                                                                if (property &&
                                                                (C_CAST(uintptr_t, property) - C_CAST(uintptr_t,
                                                                propertyBuf)) < propertyBufLen && wcslen(property))
                                                                                                {
                                                                                                    wprintf(L"\t%s\n",
                                                                property);
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                            break;
                                                                                        case DEVPROP_TYPE_SBYTE://8bit
                                                                signed byte
                                                                                        {
                                                                                            char *signedByte =
                                                                C_CAST(char*, propertyBuf); printf("\t%" PRId8 "\n",
                                                                *signedByte);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_BYTE:
                                                                                        {
                                                                                            //TODO: Handle arrays which
                                                                could show as this type. Check propertyModifier.
                                                                                            //      Currently only
                                                                popping up for power data which can be converted to a
                                                                structure and output. BYTE *unsignedByte = C_CAST(BYTE*,
                                                                propertyBuf); printf("\t%" PRIu8 "\n", *unsignedByte);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_INT16:
                                                                                        {
                                                                                            INT16 *signed16 =
                                                                C_CAST(INT16*, propertyBuf); printf("\t%" PRId16 "\n",
                                                                *signed16);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_UINT16:
                                                                                        {
                                                                                            UINT16 *unsigned16 =
                                                                C_CAST(UINT16*, propertyBuf); printf("\t%" PRIu16 "\n",
                                                                *unsigned16);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_INT32:
                                                                                        {
                                                                                            INT32 *signed32 =
                                                                C_CAST(INT32*, propertyBuf); printf("\t%" PRId32 "\n",
                                                                *signed32);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_UINT32:
                                                                                        {
                                                                                            UINT32 *unsigned32 =
                                                                C_CAST(UINT32*, propertyBuf); printf("\t%" PRIu32 "\n",
                                                                *unsigned32);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_INT64:
                                                                                        {
                                                                                            INT64 *signed64 =
                                                                C_CAST(INT64*, propertyBuf); printf("\t%" PRId64 "\n",
                                                                *signed64);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_UINT64:
                                                                                        {
                                                                                            UINT64 *unsigned64 =
                                                                C_CAST(UINT64*, propertyBuf); printf("\t%" PRIu64 "\n",
                                                                *unsigned64);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_FLOAT:
                                                                                        {
                                                                                            FLOAT *theFloat =
                                                                C_CAST(FLOAT*, propertyBuf); printf("\t%f\n",
                                                                *theFloat);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_DOUBLE:
                                                                                        {
                                                                                            DOUBLE *theFloat =
                                                                C_CAST(DOUBLE*, propertyBuf); printf("\t%f\n",
                                                                *theFloat);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_BOOLEAN:
                                                                                        {
                                                                                            BOOLEAN *theBool =
                                                                C_CAST(BOOLEAN*, propertyBuf); if (*theBool ==
                                                                DEVPROP_FALSE)
                                                                                            {
                                                                                                printf("\tFALSE\n");
                                                                                            }
                                                                                            else //if (*theBool ==
                                                                DEVPROP_TRUE)
                                                                                            {
                                                                                                printf("\tTRUE\n");
                                                                                            }
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_ERROR://win32
                                                                error code
                                                                                        {
                                                                                            DWORD *win32Error =
                                                                C_CAST(DWORD*, propertyBuf);
                                                                                            print_Windows_Error_To_Screen(*win32Error);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_GUID://128bit
                                                                guid
                                                                                            //format ( hex digits ):
                                                                                            //{8-4-4-4-12}
                                                                                        {
                                                                                            GUID *theGuid =
                                                                C_CAST(GUID*, propertyBuf); printf("\t{%08" "lu" "-%04"
                                                                PRIX16 "-%04" PRIX16 "-", theGuid->Data1,
                                                                theGuid->Data2, theGuid->Data3);
                                                                                            //now print data 4 which is
                                                                an 8 byte array
                                                                                            //first 2 bytes first:
                                                                                            printf("%02" PRIX8 "%02"
                                                                PRIX8, theGuid->Data4[0], theGuid->Data4[1]);
                                                                                            //now remaining 6 bytes
                                                                                            printf("-%02" PRIX8 "%02"
                                                                PRIX8 "%02" PRIX8 "%02" PRIX8 "%02" PRIX8 "%02" PRIX8
                                                                "}\n", theGuid->Data4[2], theGuid->Data4[3],
                                                                theGuid->Data4[4], theGuid->Data4[5], theGuid->Data4[6],
                                                                theGuid->Data4[7]);
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_DATE:
                                                                                        {
                                                                                            //This is a double.
                                                                https://docs.microsoft.com/en-us/cpp/atl-mfc-shared/date-type?view=msvc-160
                                                                                            //This is really dumb.
                                                                                            //The conversions are all
                                                                MFC/ATL code, which we don't want.
                                                                                            //The following attempts to
                                                                convert it to something in C DATE *date = C_CAST(DATE*,
                                                                propertyBuf); uint64_t wholePart = C_CAST(uint64_t,
                                                                floor(*date)); double fractionPart = *date - wholePart;
                                                                                            printf("date = %f, whole =
                                                                %llu, fraction = %0.02f\n", *date, wholePart,
                                                                fractionPart);
                                                                                            //If we need this converted,
                                                                we can do it but it will take more work.
                                                                                            //Currently only using this
                                                                when debugging and raw data above is enough
                                                                                        }
                                                                                        break;
                                                                                        case DEVPROP_TYPE_FILETIME:
                                                                                        {
                                                                                            FILETIME *fileTime =
                                                                C_CAST(FILETIME*, propertyBuf); SYSTEMTIME systemTime;
                                                                                            TIME_ZONE_INFORMATION
                                                                currentTimeZone;
                                                                                            //DWORD tzret =
                                                                                            GetTimeZoneInformation(&currentTimeZone);//need
                                                                this to adjust the converted time below. note, return
                                                                value specifies std vs dst time (1 vs 2). 0 is unknown.
                                                                                            if
                                                                (FileTimeToSystemTime(fileTime, &systemTime))
                                                                                            {
                                                                                                SYSTEMTIME localTime;
                                                                                                //convert the system
                                                                time structure to the current time zone if
                                                                (SystemTimeToTzSpecificLocalTime(&currentTimeZone,
                                                                &systemTime, &localTime))
                                                                                                {
                                                                                                    printf("\t%u/%u/%u
                                                                %u:%u:%u\n", localTime.wMonth, localTime.wDay,
                                                                localTime.wYear, localTime.wHour, localTime.wMinute,
                                                                localTime.wSecond);
                                                                                                }
                                                                                                else
                                                                                                {
                                                                                                    printf("\t%u/%u/%u
                                                                %u:%u:%u UTC\n", systemTime.wMonth, systemTime.wDay,
                                                                systemTime.wYear, systemTime.wHour, systemTime.wMinute,
                                                                systemTime.wSecond);
                                                                                                }
                                                                                            }
                                                                                            else
                                                                                            {
                                                                                                printf("\tUnable to
                                                                convert filetime to system time\n");
                                                                                            }
                                                                                        }
                                                                                        break;
                                                                                        case
                                                                DEVPROP_TYPE_DECIMAL://128bit decimal case
                                                                DEVPROP_TYPE_CURRENCY: case
                                                                DEVPROP_TYPE_SECURITY_DESCRIPTOR: case
                                                                DEVPROP_TYPE_SECURITY_DESCRIPTOR_STRING: case
                                                                DEVPROP_TYPE_DEVPROPKEY: case DEVPROP_TYPE_DEVPROPTYPE:
                                                                                        case
                                                                DEVPROP_TYPE_BINARY://custom binary data case
                                                                DEVPROP_TYPE_NTSTATUS://NTSTATUS code case
                                                                DEVPROP_TYPE_STRING_INDIRECT: default:
                                                                                            printf("DevPropType: %"
                                                                PRIX32 "\n", C_CAST(uint32_t, propertyType &
                                                                DEVPROP_MASK_TYPE)); print_Data_Buffer(propertyBuf,
                                                                propertyBufLen, true); break;
                                                                                        }
                                                                                    }
                                                                                }
                                                                                safe_free(&propertyBuf);
                                                                            }
                                                                            //else
                                                                            //{
                                                                            //    printf("\tUnable to find requested
                                                                property\n");
                                                                            //}
                                                                            safe_free(&propertyName);
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

                                                                if (!get_IDs_From_TCHAR_String(parentInst, parentBuffer,
                                                                                               parentLen, device))
                                                                {
                                                                    // try the parent's parent. There are some cases
                                                                    // where this seems to be necessary to get this
                                                                    // data. one known case is AMD's RAID driver. There
                                                                    // may be others
                                                                    DEVINST pparentInst = 0;
                                                                    cmRet = CM_Get_Parent(&pparentInst, parentInst, 0);
                                                                    if (CR_SUCCESS == cmRet)
                                                                    {
                                                                        ULONG pparentLen = ULONG_C(0);
                                                                        cmRet = CM_Get_Device_ID_Size(&pparentLen,
                                                                                                      pparentInst, 0);
                                                                        pparentLen += ULONG_C(1);
                                                                        if (CR_SUCCESS == cmRet)
                                                                        {
                                                                            TCHAR* pparentBuffer = C_CAST(
                                                                                TCHAR*,
                                                                                safe_calloc(pparentLen, sizeof(TCHAR)));
                                                                            if (pparentBuffer)
                                                                            {
                                                                                cmRet = CM_Get_Device_ID(pparentInst,
                                                                                                         pparentBuffer,
                                                                                                         pparentLen, 0);
                                                                                if (CR_SUCCESS == cmRet)
                                                                                {

                                                                                    if (!get_IDs_From_TCHAR_String(
                                                                                            pparentInst, pparentBuffer,
                                                                                            pparentLen, device))
                                                                                    {
#if defined(_DEBUG)
                                                                                        printf("Fatal error getting "
                                                                                               "device IDs\n");
#endif // _DEBUG
                                                                                    }
                                                                                }
                                                                                safe_free_tchar(&pparentBuffer);
                                                                            }
                                                                        }
                                                                    }
                                                                }

                                                                // For driver name and version data, request the
                                                                // following with the parent's instance. (don't need
                                                                // parent's parent)
                                                                //  1. DEVPKEY_Device_Service <- gives the driver's name
                                                                //  2. DEVPKEY_Device_DriverVersion
                                                                // Both return strings that we can parse as needed.
                                                                // DEVPKEY_Device_DriverDesc gives the driver's
                                                                // "displayable" name, which is not always helpful since
                                                                // it's usually marketting crap
                                                                ULONG       propertyBufLen = ULONG_C(0);
                                                                DEVPROPTYPE propertyType   = ULONG_C(0);
                                                                cmRet                      = CM_Get_DevNode_PropertyW(
                                                                    parentInst, &DEVPKEY_Device_Service, &propertyType,
                                                                    M_NULLPTR, &propertyBufLen, 0);
                                                                if (CR_SUCCESS == cmRet ||
                                                                    CR_INVALID_POINTER == cmRet ||
                                                                    CR_BUFFER_SMALL ==
                                                                        cmRet) // We'll probably get an invalid pointer
                                                                               // or small buffer, but this will return
                                                                               // the size of the buffer we need, so
                                                                               // allow it through - TJE
                                                                {
                                                                    PBYTE propertyBuf = M_REINTERPRET_CAST(
                                                                        PBYTE,
                                                                        safe_calloc(propertyBufLen + 1, sizeof(BYTE)));
                                                                    if (propertyBuf)
                                                                    {
                                                                        propertyBufLen += 1;
                                                                        cmRet = CM_Get_DevNode_PropertyW(
                                                                            parentInst, &DEVPKEY_Device_Service,
                                                                            &propertyType, propertyBuf, &propertyBufLen,
                                                                            0);
                                                                        if (CR_SUCCESS == cmRet)
                                                                        {
                                                                            DEVPROPTYPE propertyModifier =
                                                                                propertyType & DEVPROP_MASK_TYPEMOD;
                                                                            uint8_t propListAdditionalLen =
                                                                                propertyModifier == DEVPROP_TYPEMOD_LIST
                                                                                    ? UINT8_C(1)
                                                                                    : UINT8_C(
                                                                                          0); // this adjusts the loop
                                                                                              // because if this ISN'T
                                                                                              // set, then we don't need
                                                                                              // any more length than
                                                                                              // the string length
                                                                            for (LPWSTR property =
                                                                                     C_CAST(LPWSTR, propertyBuf);
                                                                                 *property;
                                                                                 property += wcslen(property) +
                                                                                             propListAdditionalLen)
                                                                            {
                                                                                if (property &&
                                                                                    (C_CAST(uintptr_t, property) -
                                                                                     C_CAST(uintptr_t, propertyBuf)) <
                                                                                        propertyBufLen &&
                                                                                    wcslen(property))
                                                                                {
                                                                                    snprintf_err_handle(
                                                                                        device->drive_info.driver_info
                                                                                            .driverName,
                                                                                        MAX_DRIVER_NAME, "%ls",
                                                                                        property); // this should
                                                                                                   // convert the driver
                                                                                                   // name to ascii
                                                                                                   // string.-TJE
                                                                                }
                                                                            }
                                                                        }
                                                                        safe_free(&propertyBuf);
                                                                    }
                                                                }
                                                                propertyBufLen =
                                                                    0; // reset to zero before going into this function
                                                                       // to read the string again.-TJE
                                                                cmRet = CM_Get_DevNode_PropertyW(
                                                                    parentInst, &DEVPKEY_Device_DriverVersion,
                                                                    &propertyType, M_NULLPTR, &propertyBufLen, 0);
                                                                if (CR_SUCCESS == cmRet ||
                                                                    CR_INVALID_POINTER == cmRet ||
                                                                    CR_BUFFER_SMALL ==
                                                                        cmRet) // We'll probably get an invalid pointer
                                                                               // or small buffer, but this will return
                                                                               // the size of the buffer we need, so
                                                                               // allow it through - TJE
                                                                {
                                                                    PBYTE propertyBuf = M_REINTERPRET_CAST(
                                                                        PBYTE, safe_calloc(propertyBufLen + ULONG_C(1),
                                                                                           sizeof(BYTE)));
                                                                    if (propertyBuf)
                                                                    {
                                                                        propertyBufLen += ULONG_C(1);
                                                                        cmRet = CM_Get_DevNode_PropertyW(
                                                                            parentInst, &DEVPKEY_Device_DriverVersion,
                                                                            &propertyType, propertyBuf, &propertyBufLen,
                                                                            0);
                                                                        if (CR_SUCCESS == cmRet)
                                                                        {
                                                                            DEVPROPTYPE propertyModifier =
                                                                                propertyType & DEVPROP_MASK_TYPEMOD;
                                                                            uint8_t propListAdditionalLen =
                                                                                propertyModifier == DEVPROP_TYPEMOD_LIST
                                                                                    ? UINT8_C(1)
                                                                                    : UINT8_C(
                                                                                          0); // this adjusts the loop
                                                                                              // because if this ISN'T
                                                                                              // set, then we don't need
                                                                                              // any more length than
                                                                                              // the string length
                                                                            for (LPWSTR property =
                                                                                     C_CAST(LPWSTR, propertyBuf);
                                                                                 *property;
                                                                                 property += wcslen(property) +
                                                                                             propListAdditionalLen)
                                                                            {
                                                                                if (property &&
                                                                                    (C_CAST(uintptr_t, property) -
                                                                                     C_CAST(uintptr_t, propertyBuf)) <
                                                                                        propertyBufLen &&
                                                                                    wcslen(property))
                                                                                {
                                                                                    snprintf_err_handle(
                                                                                        device->drive_info.driver_info
                                                                                            .driverVersionString,
                                                                                        MAX_DRIVER_VER_STR, "%ls",
                                                                                        property);
#if defined(HAVE_MSFT_SECURE_LIB)
                                                                                    int scanfRet = swscanf_s(
                                                                                        property, L"%u.%u.%u.%u",
                                                                                        &device->drive_info.driver_info
                                                                                             .driverMajorVersion,
                                                                                        &device->drive_info.driver_info
                                                                                             .driverMinorVersion,
                                                                                        &device->drive_info.driver_info
                                                                                             .driverRevision,
                                                                                        &device->drive_info.driver_info
                                                                                             .driverBuildNumber);
#else
                                                                                    int scanfRet = swscanf(
                                                                                        property, L"%u.%u.%u.%u",
                                                                                        &device->drive_info.driver_info
                                                                                             .driverMajorVersion,
                                                                                        &device->drive_info.driver_info
                                                                                             .driverMinorVersion,
                                                                                        &device->drive_info.driver_info
                                                                                             .driverRevision,
                                                                                        &device->drive_info.driver_info
                                                                                             .driverBuildNumber);
#endif //__STDC__SECURE_LIB__
                                                                                    if (scanfRet < 4 || scanfRet == EOF)
                                                                                    {
#if defined(_DEBUG)
                                                                                        printf("Fatal error getting "
                                                                                               "driver version!\n");
#endif //_DEBUG
                                                                                    }
                                                                                    else
                                                                                    {
                                                                                        device->drive_info.driver_info
                                                                                            .majorVerValid = true;
                                                                                        device->drive_info.driver_info
                                                                                            .minorVerValid = true;
                                                                                        device->drive_info.driver_info
                                                                                            .revisionVerValid = true;
                                                                                        device->drive_info.driver_info
                                                                                            .buildVerValid = true;
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                        safe_free(&propertyBuf);
                                                                    }
                                                                }
                                                            }
                                                            safe_free_tchar(&parentBuffer);
                                                        }
                                                    }
                                                }
                                                else
                                                {
                                                    // for some reason, we matched, but didn't find a matching drive
                                                    // number. Keep going through the list and trying to figure it out!
                                                    foundMatch = false;
                                                }
                                            }
                                        }
                                        if (deviceHandle)
                                        {
                                            CloseHandle(deviceHandle);
                                        }
                                    }
                                }
                            }
                            safe_free_tchar(&interfaceList);
                        }
                    }
                } // else node is not available most likely...possibly not attached to the system.
            }
            if (foundMatch)
            {
                ret = SUCCESS;
            }
        }
        safe_free_tchar(&listBuffer);
    }
    return ret;
}

static eReturnValues win_Get_SCSI_Address(HANDLE deviceHandle, PSCSI_ADDRESS scsiAddress)
{
    eReturnValues ret = SUCCESS;
    if (scsiAddress && deviceHandle != INVALID_HANDLE_VALUE)
    {
        DWORD returnedBytes = DWORD_C(0);
        BOOL  result        = FALSE;
        safe_memset(scsiAddress, sizeof(SCSI_ADDRESS), 0, sizeof(SCSI_ADDRESS));
        result = DeviceIoControl(deviceHandle, IOCTL_SCSI_GET_ADDRESS, M_NULLPTR, 0, scsiAddress, sizeof(SCSI_ADDRESS),
                                 &returnedBytes, M_NULLPTR);
        if (MSFT_BOOL_FALSE(result))
        {
            scsiAddress->PortNumber = UINT8_MAX;
            scsiAddress->PathId     = UINT8_MAX;
            scsiAddress->TargetId   = UINT8_MAX;
            scsiAddress->Lun        = UINT8_MAX;
            ret                     = FAILURE;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static M_INLINE uint32_t win_dummy_nvme_status(uint8_t sct, uint8_t sc)
{
    return (M_STATIC_CAST(uint32_t, sct) << 25) | (M_STATIC_CAST(uint32_t, sc) << 17);
}

#define WIN_DUMMY_NVME_STATUS(sct, sc) win_dummy_nvme_status(sct, sc);

#if WINVER >= SEA_WIN32_WINNT_WINBLUE && defined(IOCTL_SCSI_MINIPORT_FIRMWARE)
static void print_Firmware_Miniport_SRB_Status(ULONG returnCode)
{
    switch (returnCode)
    {
    case FIRMWARE_STATUS_SUCCESS:
        printf("Success\n");
        break;
    case FIRMWARE_STATUS_INVALID_SLOT:
        printf("Invalid Slot\n");
        break;
    case FIRMWARE_STATUS_INVALID_IMAGE:
        printf("Invalid Image\n");
        break;
    case FIRMWARE_STATUS_ERROR:
        printf("Error\n");
        break;
    case FIRMWARE_STATUS_ILLEGAL_REQUEST:
        printf("Illegal Request\n");
        break;
    case FIRMWARE_STATUS_INVALID_PARAMETER:
        printf("Invalid Parameter\n");
        break;
    case FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
        printf("Input Buffer Too Big\n");
        break;
    case FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
        printf("Output Buffer Too Small\n");
        break;
    case FIRMWARE_STATUS_CONTROLLER_ERROR:
        printf("Controller Error\n");
        break;
    case FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
        printf("Power Cycle Required\n");
        break;
    case FIRMWARE_STATUS_DEVICE_ERROR:
        printf("Device Error\n");
        break;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    case FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
        printf("Interface CRC Error\n");
        break;
    case FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
        printf("Uncorrectable Data Error\n");
        break;
    case FIRMWARE_STATUS_MEDIA_CHANGE:
        printf("Media Change\n");
        break;
    case FIRMWARE_STATUS_ID_NOT_FOUND:
        printf("ID Not Found\n");
        break;
    case FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
        printf("Media Change Request\n");
        break;
    case FIRMWARE_STATUS_COMMAND_ABORT:
        printf("Command Abort\n");
        break;
    case FIRMWARE_STATUS_END_OF_MEDIA:
        printf("End of Media\n");
        break;
    case FIRMWARE_STATUS_ILLEGAL_LENGTH:
        printf("Illegal Length\n");
        break;
#    endif
    default:
        printf("Unknown Firmware SRB Status: 0x%" PRIX32 "\n", C_CAST(uint32_t, returnCode));
        break;
    }
}

// this in an internal function so that it can be reused for reading firmware slot info, sending a download command, or
// sending an activate command. The inputs
static eReturnValues send_Win_Firmware_Miniport_Command(HANDLE           deviceHandle,
                                                        eVerbosityLevels verboseLevel,
                                                        void*            ptrDataRequest,
                                                        uint32_t         dataRequestLength,
                                                        uint32_t         timeoutSeconds,
                                                        uint32_t         firmwareFunction,
                                                        uint32_t         firmwareFlags,
                                                        uint32_t*        returnCode,
                                                        DWORD*           lastError,
                                                        uint64_t*        timeNanoseconds)
{
    eReturnValues           ret             = OS_PASSTHROUGH_FAILURE;
    PSRB_IO_CONTROL         srbControl      = M_NULLPTR;
    PFIRMWARE_REQUEST_BLOCK firmwareRequest = M_NULLPTR;
    PUCHAR                  buffer          = M_NULLPTR;
    ULONG                   bufferSize      = sizeof(SRB_IO_CONTROL) +
                       sizeof(FIRMWARE_REQUEST_BLOCK); // ((sizeof(SRB_IO_CONTROL) + sizeof(FIRMWARE_REQUEST_BLOCK) - 1)
                                                       // / sizeof(PVOID) + 1) * sizeof(PVOID);//Right from MSFT example
                                                       // toi align the data buffer on pointer alignment - TJE
    ULONG firmwareRequestDataOffset = bufferSize;      // This must be before we add any additional length to buffersize
                                                  // since this is where data will be copied to for the request - TJE

    if (!ptrDataRequest || dataRequestLength == 0)
    {
        return BAD_PARAMETER;
    }
    if (timeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    bufferSize += dataRequestLength; // Add the length of the passed in request that will be issued.
    buffer = M_REINTERPRET_CAST(PUCHAR,
                                safe_calloc_aligned(bufferSize, sizeof(UCHAR),
                                                    sizeof(PVOID))); // Most of the info I can find seems to want this
                                                                     // pointer aligned, so this should work - TJE
    if (!buffer)
    {
        return MEMORY_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verboseLevel)
    {
        printf("\n====Sending SCSI Miniport Firmware Request====\n");
    }

    // First fill out the srb header and firmware request block since these are common for all requests.
    srbControl               = C_CAST(PSRB_IO_CONTROL, buffer);
    srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(srbControl->Signature, 8, IOCTL_MINIPORT_SIGNATURE_FIRMWARE, 8);
    if (timeoutSeconds == 0)
    {
        srbControl->Timeout = 60;
    }
    else
    {
        srbControl->Timeout = timeoutSeconds;
    }
    srbControl->ControlCode = IOCTL_SCSI_MINIPORT_FIRMWARE;
    srbControl->Length      = bufferSize - sizeof(SRB_IO_CONTROL);
    // Now fill out the Firmware request block structure
    firmwareRequest                   = C_CAST(PFIRMWARE_REQUEST_BLOCK,
                                               srbControl + 1); // this structure starts right after the SRB_IO_CONRTOL so simply add 1.
    firmwareRequest->Version          = FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION;
    firmwareRequest->Size             = sizeof(FIRMWARE_REQUEST_BLOCK);
    firmwareRequest->Function         = firmwareFunction;
    firmwareRequest->Flags            = firmwareFlags;
    firmwareRequest->DataBufferOffset = firmwareRequestDataOffset;
    firmwareRequest->DataBufferLength = bufferSize - firmwareRequestDataOffset;

    // now copy the request to the proper offset in the buffer
    safe_memcpy(buffer + firmwareRequestDataOffset, bufferSize - firmwareRequestDataOffset, ptrDataRequest,
                dataRequestLength);

    DECLARE_SEATIMER(commandTimer);
    ULONG      returnedLength = ULONG_C(0);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    if (overlappedStruct.hEvent == M_NULLPTR)
    {
        safe_free_aligned(&buffer);
        return OS_PASSTHROUGH_FAILURE;
    }
    SetLastError(ERROR_SUCCESS);
    start_Timer(&commandTimer);
    BOOL  result       = DeviceIoControl(deviceHandle, IOCTL_SCSI_MINIPORT, buffer, bufferSize, buffer, bufferSize,
                                         &returnedLength, &overlappedStruct);
    DWORD getLastError = GetLastError();
    if (ERROR_IO_PENDING ==
        getLastError) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                      // flag, everything will work like old synchronous code.-TJE
    {
        result = GetOverlappedResult(deviceHandle, &overlappedStruct, &returnedLength, TRUE);
    }
    else if (getLastError != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = M_NULLPTR;

    if (timeNanoseconds != M_NULLPTR)
    {
        *timeNanoseconds = get_Nano_Seconds(commandTimer);
    }

    if (MSFT_BOOL_TRUE(result))
    {
        // command went through
        if (firmwareFunction == FIRMWARE_FUNCTION_GET_INFO && returnedLength > 0)
        {
            // request was to read the firmware info, so copy this out to the buffer for the calling function to deal
            // with - TJE
            safe_memcpy(ptrDataRequest, dataRequestLength, buffer + firmwareRequestDataOffset,
                        M_Min(dataRequestLength, bufferSize - firmwareRequestDataOffset));
        }
        ret = SUCCESS;
        if (returnCode != M_NULLPTR)
        {
            *returnCode =
                srbControl->ReturnCode; // this is so the caller can do what it wants to with this information - TJE
        }
        if (VERBOSITY_COMMAND_VERBOSE <= verboseLevel)
        {
            printf("Firmware Miniport Status: ");
            print_Firmware_Miniport_SRB_Status(srbControl->ReturnCode);
        }
    }
    else
    {
        // something else went wrong. Check Windows last error code. Likely an incompatibility in some way.
        if (VERBOSITY_COMMAND_VERBOSE <= verboseLevel)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(getLastError);
            printf("Firmware Miniport Status: ");
            print_Firmware_Miniport_SRB_Status(srbControl->ReturnCode);
        }
    }
    if (lastError)
    {
        *lastError = getLastError;
    }
    safe_free_aligned(&buffer);
    return ret;
}

#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
static M_INLINE void safe_free_firmwareinfov2(PSTORAGE_FIRMWARE_INFO_V2* info)
{
    safe_free_core(M_REINTERPRET_CAST(void**, info));
}
#    endif

static M_INLINE void safe_free_firmwareinfo(PSTORAGE_FIRMWARE_INFO* info)
{
    safe_free_core(M_REINTERPRET_CAST(void**, info));
}

static eReturnValues get_Win_FWDL_Miniport_Capabilities(tDevice* device, bool controllerRequest)
{
    eReturnValues ret = NOT_SUPPORTED;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
    if (is_Windows_10_Or_Higher())
    {
        // V2 structures - Win10 and up
        ULONG firmwareInfoLength =
            sizeof(STORAGE_FIRMWARE_INFO_V2) +
            (sizeof(STORAGE_FIRMWARE_SLOT_INFO_V2) *
             7); // max of 7 slots in NVMe. This should be plenty of space regardless of device type -TJE
        PSTORAGE_FIRMWARE_INFO_V2 firmwareInfo =
            M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_INFO_V2, safe_calloc(firmwareInfoLength, sizeof(UCHAR)));
        if (firmwareInfo)
        {
            uint32_t returnCode = UINT32_C(0);
            // Setup input values
            firmwareInfo->Version = STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2;
            firmwareInfo->Size    = sizeof(STORAGE_FIRMWARE_INFO_V2);
            // Issue the minport IOCTL
            ret = send_Win_Firmware_Miniport_Command(device->os_info.fd, device->deviceVerbosity, firmwareInfo,
                                                     firmwareInfoLength, 15, FIRMWARE_FUNCTION_GET_INFO,
                                                     controllerRequest ? FIRMWARE_REQUEST_FLAG_CONTROLLER : 0,
                                                     &returnCode, &device->os_info.last_error, M_NULLPTR);
            if (ret == SUCCESS)
            {
                device->os_info.fwdlMiniportSupported          = true;
                device->os_info.fwdlIOsupport.fwdlIOSupported  = firmwareInfo->UpgradeSupport;
                device->os_info.fwdlIOsupport.payloadAlignment = firmwareInfo->ImagePayloadAlignment;
                device->os_info.fwdlIOsupport.maxXferSize      = firmwareInfo->ImagePayloadMaxSize;
#        if defined(_DEBUG)
                printf("Got Miniport V2 FWDL Info\n");
                printf("\tSupported: %d\n", firmwareInfo->UpgradeSupport);
                printf("\tPayload Alignment: %ld\n", firmwareInfo->ImagePayloadAlignment);
                printf("\tmaxXferSize: %ld\n", firmwareInfo->ImagePayloadMaxSize);
                printf("\tPendingActivate: %d\n", firmwareInfo->PendingActivateSlot);
                printf("\tActiveSlot: %d\n", firmwareInfo->ActiveSlot);
                printf("\tSlot Count: %d\n", firmwareInfo->SlotCount);
                printf("\tFirmware Shared: %d\n", firmwareInfo->FirmwareShared);
                // print out what's in the slots!
                for (uint8_t iter = UINT8_C(0); iter < firmwareInfo->SlotCount && iter < 7; ++iter)
                {
                    printf("\t    Firmware Slot %d:\n", firmwareInfo->Slot[iter].SlotNumber);
                    printf("\t\tRead Only: %d\n", firmwareInfo->Slot[iter].ReadOnly);
                    printf("\t\tRevision: %s\n", firmwareInfo->Slot[iter].Revision);
                }
#        endif
            }
            safe_free_firmwareinfov2(&firmwareInfo);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
#    endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
        if (is_Windows_8_One_Or_Higher())
        {
            // V1 structures - Win 8.1
            ULONG firmwareInfoLength =
                sizeof(STORAGE_FIRMWARE_INFO) +
                (sizeof(STORAGE_FIRMWARE_SLOT_INFO) *
                 7); // max of 7 slots in NVMe. This should be plenty of space regardless of device type -TJE
            PSTORAGE_FIRMWARE_INFO firmwareInfo =
                M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_INFO, safe_calloc(firmwareInfoLength, sizeof(UCHAR)));
            if (firmwareInfo)
            {
                uint32_t returnCode = UINT32_C(0);
                // Setup input values
                firmwareInfo->Version = STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION;
                firmwareInfo->Size    = sizeof(STORAGE_FIRMWARE_INFO);
                // Issue the minport IOCTL
                ret = send_Win_Firmware_Miniport_Command(device->os_info.fd, device->deviceVerbosity, firmwareInfo,
                                                         firmwareInfoLength, 15, FIRMWARE_FUNCTION_GET_INFO,
                                                         controllerRequest ? FIRMWARE_REQUEST_FLAG_CONTROLLER : 0,
                                                         &returnCode, &device->os_info.last_error, M_NULLPTR);

                if (ret == SUCCESS)
                {
                    device->os_info.fwdlMiniportSupported         = true;
                    device->os_info.fwdlIOsupport.fwdlIOSupported = firmwareInfo->UpgradeSupport;
                    device->os_info.fwdlIOsupport.payloadAlignment =
                        4096; // This is not specified in Win8, but assume this!
                    device->os_info.fwdlIOsupport.maxXferSize =
                        device->os_info.adapterMaxTransferSize > 0
                            ? device->os_info.adapterMaxTransferSize
                            : 65536; // Set 64K in case this is not otherwise set...not great but will likely work since
                                     // this is the common transfer size limit in Windows - TJE
#    if defined(_DEBUG)
                    printf("Got Miniport V1 FWDL Info\n");
                    printf("\tSupported: %d\n", firmwareInfo->UpgradeSupport);
                    // printf("\tPayload Alignment: %ld\n", firmwareInfo->ImagePayloadAlignment);
                    // printf("\tmaxXferSize: %ld\n", firmwareInfo->ImagePayloadMaxSize);
                    printf("\tPendingActivate: %d\n", firmwareInfo->PendingActivateSlot);
                    printf("\tActiveSlot: %d\n", firmwareInfo->ActiveSlot);
                    printf("\tSlot Count: %d\n", firmwareInfo->SlotCount);
                    // printf("\tFirmware Shared: %d\n", firmwareInfo->FirmwareShared);
                    // print out what's in the slots!
                    for (uint8_t iter = UINT8_C(0); iter < firmwareInfo->SlotCount && iter < UINT8_C(7); ++iter)
                    {
                        DECLARE_ZERO_INIT_ARRAY(char, v1Revision, 9);
                        snprintf_err_handle(v1Revision, 9, "%s", firmwareInfo->Slot[iter].Revision.Info);
                        printf("\t    Firmware Slot %d:\n", firmwareInfo->Slot[iter].SlotNumber);
                        printf("\t\tRead Only: %d\n", firmwareInfo->Slot[iter].ReadOnly);
                        printf("\t\tRevision: %s\n", v1Revision); // temp storage since there was not enough room for
                                                                  // null terminator in API so it gets messy.
                    }
#    endif //_DEBUG
                }
                safe_free_firmwareinfo(&firmwareInfo);
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
        }
    return ret;
}

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

There is an option to allow using this API call with any supported FWDL command regardless of drive type and interface
that can be set. Device->os_info.fwdlIOsupport.allowFlexibleUseOfAPI set to true will check for a supported SCSI or ATA
command and all other payload requirements and allow it to be issued for any case. This is good if your only goal is to
get firmware to a drive and don't care about testing a specific command sequence. NOTE: Some SAS HBAs will issue a
readlogext command before each download command when performing deferred download, which may not be expected if taking a
bus trace of the sequence.

*/

bool is_Firmware_Download_Command_Compatible_With_Win_API(ScsiIoCtx* scsiIoCtx)
{
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
    printf("Checking if FWDL Command is compatible with Win 10 API\n");
#    endif
    if (!scsiIoCtx->device->os_info.fwdlIOsupport.fwdlIOSupported)
    {
        // OS doesn't support this IO on this device, so just say no!
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
        printf("\tFalse (not Supported)\n");
#    endif
        return false;
    }
    // If we are trying to send an ATA command, then only use the API if it's IDE.
    // SCSI and RAID interfaces depend on the SATL to translate it correctly, but that is not checked by windows and is
    // not possible since nothing responds to the report supported operation codes command A future TODO will be to have
    // either a lookup table or additional check somewhere to send the report supported operation codes command, but
    // this is good enough for now, since it's unlikely a SATL will implement that...
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
    printf("scsiIoCtx = %p\t->pAtaCmdOpts = %p\tinterface type: %d\n", scsiIoCtx, scsiIoCtx->pAtaCmdOpts,
           scsiIoCtx->device->drive_info.interface_type);
#    endif
    if (scsiIoCtx->device->os_info.fwdlIOsupport.allowFlexibleUseOfAPI)
    {
        uint32_t transferLengthBytes = UINT32_C(0);
        bool     supportedCMD        = false;
        bool     isActivate          = false;
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
        printf("Flexible Win10 FWDL API allowed. Checking for supported commands\n");
#    endif
        if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
        {
            uint8_t wbMode = get_bit_range_uint8(scsiIoCtx->cdb[1], 4, 0);
            if (wbMode == SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER)
            {
                supportedCMD        = true;
                transferLengthBytes = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            }
            else if (wbMode == SCSI_WB_ACTIVATE_DEFERRED_MICROCODE)
            {
                supportedCMD = true;
                isActivate   = true;
            }
        }
        else if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_CMD ||
                                            scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA))
        {

            if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0E)
            {
                supportedCMD = true;
                transferLengthBytes =
                    M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaLow, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) *
                    LEGACY_DRIVE_SEC_SIZE;
            }
            else if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
            {
                supportedCMD = true;
                isActivate   = true;
            }
        }
        if (supportedCMD)
        {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
            printf("\tDetected supported command\n");
#    endif
            if (isActivate)
            {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                printf("\tTrue - is an activate command\n");
#    endif
                return true;
            }
            else
            {
                if (transferLengthBytes < scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize &&
                    (transferLengthBytes % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment == 0))
                {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                    printf("\tTrue - payload fits FWDL requirements from OS/Driver\n");
#    endif
                    return true;
                }
            }
        }
    }
    else if (scsiIoCtx && scsiIoCtx->pAtaCmdOpts && scsiIoCtx->device->drive_info.interface_type == IDE_INTERFACE)
    {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
        printf("Checking ATA command info for FWDL support\n");
#    endif
        // We're sending an ATA passthrough command, and the OS says the io is supported, so it SHOULD work. - TJE
        if (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_CMD ||
            scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA)
        {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
            printf("Is Download Microcode command (%" PRIX8 "h)\n", scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus);
#    endif
            if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0E)
            {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                printf("Is deferred download mode Eh\n");
#    endif
                // We know it's a download command, now we need to make sure it's a multiple of the Windows alignment
                // requirement and that it isn't larger than the maximum allowed
                uint16_t transferSizeSectors =
                    M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaLow, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount);
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                printf("Transfersize sectors: %" PRIu16 "\n", transferSizeSectors);
                printf("Transfersize bytes: %" PRIu32 "\tMaxXferSize: %" PRIu32 "\n",
                       C_CAST(uint32_t, transferSizeSectors * LEGACY_DRIVE_SEC_SIZE),
                       scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize);
                printf("Transfersize sectors %% alignment: %" PRIu32 "\n",
                       (C_CAST(uint32_t, transferSizeSectors * LEGACY_DRIVE_SEC_SIZE) %
                        scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment));
#    endif
                if (C_CAST(uint32_t, transferSizeSectors * LEGACY_DRIVE_SEC_SIZE) <
                        scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize &&
                    (C_CAST(uint32_t, transferSizeSectors * LEGACY_DRIVE_SEC_SIZE) %
                         scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment ==
                     0))
                {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                    printf("\tTrue (0x0E)\n");
#    endif
                    return true;
                }
            }
            else if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
            {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                printf("\tTrue (0x0F)\n");
#    endif
                return true;
            }
        }
    }
    else if (scsiIoCtx) // sending a SCSI command
    {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
        printf("Checking SCSI command info for FWDL Support\n");
#    endif
        // Should we check that this is a SCSI Drive? Right now we'll just attempt the download and let the drive/SATL
        // handle translation check that it's a write buffer command for a firmware download & it's a deferred download
        // command since that is all that is supported
        if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
        {
            uint8_t  wbMode         = get_bit_range_uint8(scsiIoCtx->cdb[1], 4, 0);
            uint32_t transferLength = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            switch (wbMode)
            {
            case SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER:
                if (transferLength < scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize &&
                    (transferLength % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment == 0))
                {
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                    printf("\tTrue (SCSI Mode 0x0E)\n");
#    endif
                    return true;
                }
                break;
            case SCSI_WB_ACTIVATE_DEFERRED_MICROCODE:
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
                printf("\tTrue (SCSI Mode 0x0F)\n");
#    endif
                return true;
            default:
                break;
            }
        }
    }
#    if defined(_DEBUG_FWDL_API_COMPATABILITY)
    printf("\tFalse\n");
#    endif
    return false;
}

static bool is_Activate_Command(ScsiIoCtx* scsiIoCtx)
{
    bool isActivate = false;
    if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_CMD ||
                                   scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA))
    {
        // check the subcommand (feature)
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
        {
            isActivate = true;
        }
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
    {
        // it's a write buffer command, so we need to also check the mode.
        uint8_t wbMode = get_bit_range_uint8(scsiIoCtx->cdb[1], 4, 0);
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

static eReturnValues dummy_Up_SCSI_Sense_FWDL(ScsiIoCtx* scsiIoCtx, ULONG returnCode)
{
    // assume this for anything where we could generate a usable status and a few other cases
    eReturnValues ret      = SUCCESS;
    uint8_t       senseKey = SENSE_KEY_NO_ERROR;
    uint8_t       asc      = UINT8_C(0);
    uint8_t       ascq = UINT8_C(0); // no fru since that is vendor unique info that we have no way of dummying up - TJE
    DECLARE_ZERO_INIT_ARRAY(uint8_t, localSense, SPC3_SENSE_LEN);
    if (scsiIoCtx->pAtaCmdOpts)
    {
        // sense code should be "ATA Passthrough information available" since this is how a passed ATA command will show
        // up versus a SCSI command - TJE
        senseKey = SENSE_KEY_RECOVERED_ERROR;
        asc      = 0x00;
        ascq     = 0x1D;
        // now set the ATA registers for command completion
        switch (returnCode)
        {
        case FIRMWARE_STATUS_SUCCESS:
            scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY;
            if (is_Activate_Command(scsiIoCtx))
            {
                scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x02; // applied the new microcode
            }
            else
            {
                if (scsiIoCtx->fwdlLastSegment)
                {
                    scsiIoCtx->pAtaCmdOpts->rtfr.secCnt =
                        0x03; // all segments received and saved. Waiting for activation;
                }
                else
                {
                    scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x01; // expecting more microcode
                }
            }
            break;
        case FIRMWARE_STATUS_DEVICE_ERROR:
        case FIRMWARE_STATUS_CONTROLLER_ERROR:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_DEVICE_FAULT;
            break;
        case FIRMWARE_STATUS_ERROR:
        case FIRMWARE_STATUS_INVALID_SLOT:
        case FIRMWARE_STATUS_INVALID_IMAGE:
        case FIRMWARE_STATUS_ILLEGAL_REQUEST:
        case FIRMWARE_STATUS_INVALID_PARAMETER:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ABORT;
            break;
        case FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        case FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        case FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
            break;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
        case FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_INTERFACE_CRC;
            break;
        case FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_UNCORRECTABLE_DATA;
            break;
        case FIRMWARE_STATUS_MEDIA_CHANGE:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_MEDIA_CHANGE;
            break;
        case FIRMWARE_STATUS_ID_NOT_FOUND:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ID_NOT_FOUND;
            break;
        case FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_MEDIA_CHANGE_REQUEST;
            break;
        case FIRMWARE_STATUS_COMMAND_ABORT:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ABORT;
            break;
        case FIRMWARE_STATUS_END_OF_MEDIA:
            scsiIoCtx->pAtaCmdOpts->rtfr.status =
                ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
            scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_END_OF_MEDIA;
            break;
        case FIRMWARE_STATUS_ILLEGAL_LENGTH:
            break;
#    endif
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }
    else
    {
        switch (returnCode)
        {
        case FIRMWARE_STATUS_SUCCESS:
            break;
        case FIRMWARE_STATUS_ERROR:
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            // too generic to provide additional sense info
            break;
        case FIRMWARE_STATUS_ILLEGAL_REQUEST:
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            // too generic to provide additional sense info
            break;
        case FIRMWARE_STATUS_INVALID_PARAMETER:
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x24;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        case FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        case FIRMWARE_STATUS_INVALID_SLOT:
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x24;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_INVALID_IMAGE:
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x26;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_DEVICE_ERROR:
        case FIRMWARE_STATUS_CONTROLLER_ERROR:
            senseKey = SENSE_KEY_HARDWARE_ERROR;
            asc      = 0x44;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
            break;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
        case FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            asc      = 0x47;
            ascq     = 0x03;
            break;
        case FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
            senseKey = SENSE_KEY_MEDIUM_ERROR;
            asc      = 0x11;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_MEDIA_CHANGE:
            senseKey = SENSE_KEY_UNIT_ATTENTION;
            asc      = 0x28;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_ID_NOT_FOUND:
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x21;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
            senseKey = SENSE_KEY_UNIT_ATTENTION;
            asc      = 0x5A;
            ascq     = 0x01;
            break;
        case FIRMWARE_STATUS_COMMAND_ABORT:
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            // too generic to provide additional sense info
            break;
        case FIRMWARE_STATUS_END_OF_MEDIA:
            senseKey = SENSE_KEY_NOT_READY;
            asc      = 0x3A;
            ascq     = 0x00;
            break;
        case FIRMWARE_STATUS_ILLEGAL_LENGTH:
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x24;
            ascq     = 0x00;
            break;
#    endif
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }

    // now set the sense code into the data buffer output - using fixed format for simplicity - TJE
    localSense[0] = 0x70; // fixed format
    localSense[2] = senseKey;
    // if (asc = 0x3A)
    //{
    //     //set the end of media bit?
    // }
    localSense[7]  = 0x0A; // up to byte 17 set
    localSense[12] = asc;
    localSense[13] = ascq;
    if (scsiIoCtx->pAtaCmdOpts)
    {
        localSense[0] |= BIT7; // set valid bit since we are filling in information field according to a standard - TJE
        // set information and command specific info fields for passthrough register data
        localSense[3] = scsiIoCtx->pAtaCmdOpts->rtfr.error;
        localSense[4] = scsiIoCtx->pAtaCmdOpts->rtfr.status;
        localSense[5] = scsiIoCtx->pAtaCmdOpts->rtfr.device;
        localSense[6] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt;
    }

    // copy back based on allocated length
    safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, localSense,
                M_Min(SPC3_SENSE_LEN, scsiIoCtx->senseDataSize));
    return ret;
}

#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
static M_INLINE void safe_free_firmwaredownloadv2(PSTORAGE_FIRMWARE_DOWNLOAD_V2* fwdl)
{
    safe_free_core(M_REINTERPRET_CAST(void**, fwdl));
}
#    endif

static M_INLINE void safe_free_firmwaredownload(PSTORAGE_FIRMWARE_DOWNLOAD* fwdl)
{
    safe_free_core(M_REINTERPRET_CAST(void**, fwdl));
}

static eReturnValues win_FW_Download_IO_SCSI_Miniport(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = NOT_SUPPORTED;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
    if (is_Windows_10_Or_Higher())
    {
        // V2 structures - Win10 and up
        ULONG                         firmwareDLLength = sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2) + scsiIoCtx->dataLength;
        PSTORAGE_FIRMWARE_DOWNLOAD_V2 firmwareDownload =
            M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_DOWNLOAD_V2, safe_calloc(firmwareDLLength, sizeof(UCHAR)));
        if (firmwareDownload)
        {
            uint32_t returnCode = UINT32_C(0);
            uint32_t fwdlFlags  = FIRMWARE_REQUEST_FLAG_CONTROLLER; // start with this, but may need other flags
            // Setup input values
            firmwareDownload->Version = STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2;
            firmwareDownload->Size    = sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2);
            firmwareDownload->Slot    = 0; // N/A on ATA and buffer ID on SCSI

            firmwareDownload->BufferSize = firmwareDownload->ImageSize = scsiIoCtx->dataLength;

            // we need to set the offset since MS uses this in the command sent to the device.
            firmwareDownload->Offset = 0;
            if (scsiIoCtx && scsiIoCtx->pAtaCmdOpts)
            {
                // get offset from the tfrs
                firmwareDownload->Offset = C_CAST(DWORDLONG, M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaHi,
                                                                                 scsiIoCtx->pAtaCmdOpts->tfr.LbaMid)) *
                                           LEGACY_DRIVE_SEC_SIZE;
                firmwareDownload->BufferSize = firmwareDownload->ImageSize =
                    C_CAST(DWORDLONG, M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount,
                                                          scsiIoCtx->pAtaCmdOpts->tfr.LbaLow)) *
                    C_CAST(DWORDLONG, LEGACY_DRIVE_SEC_SIZE);
            }
            else if (scsiIoCtx)
            {
                // get offset from the cdb
                firmwareDownload->Slot = scsiIoCtx->cdb[2];
                firmwareDownload->Offset =
                    M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
                firmwareDownload->BufferSize = firmwareDownload->ImageSize =
                    M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            }
            else
            {
                safe_free_firmwaredownloadv2(&firmwareDownload);
                return BAD_PARAMETER;
            }

            // copy the image to ImageBuffer
            safe_memcpy(firmwareDownload->ImageBuffer, scsiIoCtx->dataLength, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            // setup any flags
#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
            if (scsiIoCtx->fwdlLastSegment)
            {
                // This IS documented on MSDN but VS2015 can't seem to find it...
                // One website says that this flag is new in Win10 1704 - creators update (10.0.15021)
                fwdlFlags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
            }
#        endif
#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
            if (scsiIoCtx->fwdlFirstSegment)
            {
                fwdlFlags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
            }
#        endif
            // Issue the minport IOCTL
            ret = send_Win_Firmware_Miniport_Command(
                scsiIoCtx->device->os_info.fd, scsiIoCtx->device->deviceVerbosity, firmwareDownload, firmwareDLLength,
                scsiIoCtx->timeout, FIRMWARE_FUNCTION_DOWNLOAD, fwdlFlags, &returnCode,
                &scsiIoCtx->device->os_info.last_error, &scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
            if (ret == SUCCESS)
            {
                ret = dummy_Up_SCSI_Sense_FWDL(scsiIoCtx, returnCode);
            }
            safe_free_firmwaredownloadv2(&firmwareDownload);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
#    endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
        if (is_Windows_8_One_Or_Higher())
        {
            // V1 structures - Win8.1
            ULONG                      firmwareDLLength = sizeof(STORAGE_FIRMWARE_DOWNLOAD) + scsiIoCtx->dataLength;
            PSTORAGE_FIRMWARE_DOWNLOAD firmwareDownload =
                M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_DOWNLOAD, safe_calloc(firmwareDLLength, sizeof(UCHAR)));
            if (firmwareDownload)
            {
                uint32_t returnCode = UINT32_C(0);
                uint32_t fwdlFlags  = FIRMWARE_REQUEST_FLAG_CONTROLLER; // start with this, but may need other flags
                // Setup input values
                firmwareDownload->Version    = STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION;
                firmwareDownload->Size       = sizeof(STORAGE_FIRMWARE_DOWNLOAD);
                firmwareDownload->BufferSize = scsiIoCtx->dataLength;

                // we need to set the offset since MS uses this in the command sent to the device.
                firmwareDownload->Offset = 0;
                if (scsiIoCtx && scsiIoCtx->pAtaCmdOpts)
                {
                    // get offset from the tfrs
                    firmwareDownload->Offset =
                        C_CAST(DWORDLONG, M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaHi,
                                                              scsiIoCtx->pAtaCmdOpts->tfr.LbaMid)) *
                        LEGACY_DRIVE_SEC_SIZE;
                    firmwareDownload->BufferSize =
                        C_CAST(DWORDLONG, M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount,
                                                              scsiIoCtx->pAtaCmdOpts->tfr.LbaLow)) *
                        LEGACY_DRIVE_SEC_SIZE;
                }
                else if (scsiIoCtx)
                {
                    // get offset from the cdb
                    firmwareDownload->Offset =
                        M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
                    firmwareDownload->BufferSize =
                        M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
                }
                else
                {
                    safe_free_firmwaredownload(&firmwareDownload);
                    return BAD_PARAMETER;
                }

                // copy the image to ImageBuffer
                safe_memcpy(firmwareDownload->ImageBuffer, scsiIoCtx->dataLength, scsiIoCtx->pdata,
                            scsiIoCtx->dataLength);
                // no other flags to setup since 8.1 only had "controller" flag and existing slot flag (which only
                // affects activate) Issue the minport IOCTL
                ret = send_Win_Firmware_Miniport_Command(
                    scsiIoCtx->device->os_info.fd, scsiIoCtx->device->deviceVerbosity, firmwareDownload,
                    firmwareDLLength, scsiIoCtx->timeout, FIRMWARE_FUNCTION_DOWNLOAD, fwdlFlags, &returnCode,
                    &scsiIoCtx->device->os_info.last_error, &scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
                if (ret == SUCCESS)
                {
                    ret = dummy_Up_SCSI_Sense_FWDL(scsiIoCtx, returnCode);
                }
                safe_free_firmwaredownload(&firmwareDownload);
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
        }
    return ret;
}

static M_INLINE void safe_free_firmwareactivate(PSTORAGE_FIRMWARE_ACTIVATE* activate)
{
    safe_free_core(M_REINTERPRET_CAST(void**, activate));
}

static eReturnValues win_FW_Activate_IO_SCSI_Miniport(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (scsiIoCtx == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    // Only one version of activate structure - TJE
    if (is_Windows_8_One_Or_Higher())
    {
        // V1 structures - Win8.1 & Win10
        ULONG                      firmwareActivateLength = sizeof(STORAGE_FIRMWARE_ACTIVATE);
        PSTORAGE_FIRMWARE_ACTIVATE firmwareActivate =
            M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_ACTIVATE, safe_calloc(firmwareActivateLength, sizeof(UCHAR)));
        if (firmwareActivate)
        {
            uint32_t returnCode = UINT32_C(0);
            uint32_t fwdlFlags  = UINT32_C(0); // start with this, but may need other flags

            // Setup input values
            firmwareActivate->Version        = STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION;
            firmwareActivate->Size           = sizeof(STORAGE_FIRMWARE_ACTIVATE);
            firmwareActivate->SlotToActivate = 0;

            if (scsiIoCtx->pAtaCmdOpts == M_NULLPTR)
            {
                firmwareActivate->SlotToActivate =
                    scsiIoCtx
                        ->cdb[2]; // Set the slot number to the buffer ID number...This is the closest this translates.
            }
            if (scsiIoCtx->device->drive_info.interface_type ==
                NVME_INTERFACE) // SCSI interface, but NVMe in 8.1 will likely only be identified by earlier bustype or
                                // vendor id
            {
                // if we are on NVMe, but the command comes to here, then someone forced SCSI mode, so let's set this
                // flag correctly
                fwdlFlags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
            }

            // Issue the minport IOCTL
            ret = send_Win_Firmware_Miniport_Command(
                scsiIoCtx->device->os_info.fd, scsiIoCtx->device->deviceVerbosity, firmwareActivate,
                firmwareActivateLength, scsiIoCtx->timeout, FIRMWARE_FUNCTION_ACTIVATE, fwdlFlags, &returnCode,
                &scsiIoCtx->device->os_info.last_error, &scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
            if (ret == SUCCESS)
            {
                ret = dummy_Up_SCSI_Sense_FWDL(scsiIoCtx, returnCode);
            }
            safe_free_firmwareactivate(&firmwareActivate);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}

static eReturnValues windows_Firmware_Download_IO_SCSI_Miniport(ScsiIoCtx* scsiIoCtx)
{
    if (!scsiIoCtx)
    {
        return BAD_PARAMETER;
    }
    if (is_Activate_Command(scsiIoCtx))
    {
        return win_FW_Activate_IO_SCSI_Miniport(scsiIoCtx);
    }
    else
    {
        return win_FW_Download_IO_SCSI_Miniport(scsiIoCtx);
    }
}

// This may need adjusting with bus trace help in the future, but for now this is a best guess from what info we
// have.-TJE
static eReturnValues dummy_Up_NVM_Status_FWDL(nvmeCmdCtx* nvmeIoCtx, ULONG returnCode)
{
    eReturnValues ret =
        SUCCESS; // assume this for anything where we could generate a usable status and a few other cases
    switch (returnCode)
    {
    case FIRMWARE_STATUS_SUCCESS:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 0);
        break;
    case FIRMWARE_STATUS_ERROR:
        break;
    case FIRMWARE_STATUS_ILLEGAL_REQUEST: // overlapping range?
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 1);
        break;
    case FIRMWARE_STATUS_INVALID_PARAMETER: // overlapping range?
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 2);
        break;
    case FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    case FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    case FIRMWARE_STATUS_INVALID_SLOT:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, 6);
        break;
    case FIRMWARE_STATUS_INVALID_IMAGE:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, 7);
        break;
    case FIRMWARE_STATUS_DEVICE_ERROR:
    case FIRMWARE_STATUS_CONTROLLER_ERROR:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 6); // internal error
        break;
    case FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
        ret = POWER_CYCLE_REQUIRED;
        break;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    case FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 4); // data transfer error
        break;
    case FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS,
                                  0x81); // unrecovered read error...not sure what else to set if this happens-TJE
        break;
    case FIRMWARE_STATUS_MEDIA_CHANGE:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    case FIRMWARE_STATUS_ID_NOT_FOUND:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 0x80); // lba out of range
        break;
    case FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    case FIRMWARE_STATUS_COMMAND_ABORT:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 7); // command abort requested
        break;
    case FIRMWARE_STATUS_END_OF_MEDIA:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    case FIRMWARE_STATUS_ILLEGAL_LENGTH: // overlapping range???
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, 14);
        break;
#    endif
    default:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    }
    return ret;
}

static eReturnValues send_Win_NVME_Firmware_Miniport_Download(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = NOT_SUPPORTED;
#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
    if (is_Windows_10_Or_Higher())
    {
        // V2 structures - Win10 and up
        ULONG                         firmwareDLLength = sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2) + nvmeIoCtx->dataSize;
        PSTORAGE_FIRMWARE_DOWNLOAD_V2 firmwareDownload =
            M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_DOWNLOAD_V2, safe_calloc(firmwareDLLength, sizeof(UCHAR)));
        if (firmwareDownload)
        {
            uint32_t returnCode = UINT32_C(0);
            uint32_t fwdlFlags  = FIRMWARE_REQUEST_FLAG_CONTROLLER; // start with this, but may need other flags
            // Setup input values
            firmwareDownload->Version = STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2;
            firmwareDownload->Size    = sizeof(STORAGE_FIRMWARE_DOWNLOAD_V2);
            firmwareDownload->Offset  = C_CAST(ULONGLONG, nvmeIoCtx->cmd.adminCmd.cdw11)
                                       << 2; // multiply by 4 to convert words to bytes
            firmwareDownload->BufferSize =
                C_CAST(ULONGLONG, nvmeIoCtx->cmd.adminCmd.cdw10 + 1)
                << 2; // add one since this is zeroes based then multiply by 4 to convert words to bytes
            firmwareDownload->ImageSize =
                C_CAST(ULONG, nvmeIoCtx->cmd.adminCmd.cdw10 + 1)
                << 2; // add one since this is zeroes based then multiply by 4 to convert words to bytes
            firmwareDownload->Slot =
                STORAGE_FIRMWARE_INFO_INVALID_SLOT; // SHould this be zero? It technically shouldn't be used in this
                                                    // command in the NVMe spec - TJE

            // copy the image to ImageBuffer
            safe_memcpy(firmwareDownload->ImageBuffer, nvmeIoCtx->dataSize, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
            // setup any flags
#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
            if (nvmeIoCtx->fwdlLastSegment)
            {
                // This IS documented on MSDN but VS2015 can't seem to find it...
                // One website says that this flag is new in Win10 1704 - creators update (10.0.15021)
                fwdlFlags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
            }
#        endif
#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
            if (nvmeIoCtx->fwdlFirstSegment)
            {
                fwdlFlags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
            }
#        endif
            // Issue the minport IOCTL
            ret = send_Win_Firmware_Miniport_Command(
                nvmeIoCtx->device->os_info.fd, nvmeIoCtx->device->deviceVerbosity, firmwareDownload, firmwareDLLength,
                nvmeIoCtx->timeout, FIRMWARE_FUNCTION_DOWNLOAD, fwdlFlags, &returnCode,
                &nvmeIoCtx->device->os_info.last_error, &nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
            if (ret == SUCCESS)
            {
                ret = dummy_Up_NVM_Status_FWDL(nvmeIoCtx, returnCode);
            }
            safe_free_firmwaredownloadv2(&firmwareDownload);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
#    endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_THRESHOLD
        if (is_Windows_8_One_Or_Higher())
        {
            // V1 structures - Win8.1
            ULONG                      firmwareDLLength = sizeof(STORAGE_FIRMWARE_DOWNLOAD) + nvmeIoCtx->dataSize;
            PSTORAGE_FIRMWARE_DOWNLOAD firmwareDownload =
                M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_DOWNLOAD, safe_calloc(firmwareDLLength, sizeof(UCHAR)));
            if (firmwareDownload)
            {
                uint32_t returnCode = UINT32_C(0);
                uint32_t fwdlFlags  = FIRMWARE_REQUEST_FLAG_CONTROLLER; // start with this, but may need other flags
                // Setup input values
                firmwareDownload->Version = STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION;
                firmwareDownload->Size    = sizeof(STORAGE_FIRMWARE_DOWNLOAD);
                firmwareDownload->Offset  = nvmeIoCtx->cmd.adminCmd.cdw11 << 2; // multiply by 4 to convert words to
                                                                                // bytes
                firmwareDownload->BufferSize =
                    (nvmeIoCtx->cmd.adminCmd.cdw10 + 1)
                    << 2; // add one since this is zeroes based then multiply by 4 to convert words to bytes

                // copy the image to ImageBuffer
                safe_memcpy(firmwareDownload->ImageBuffer, nvmeIoCtx->dataSize, nvmeIoCtx->ptrData,
                            nvmeIoCtx->dataSize);
                // no other flags to setup since 8.1 only had "controller" flag and existing slot flag (which only
                // affects activate) Issue the minport IOCTL
                ret = send_Win_Firmware_Miniport_Command(
                    nvmeIoCtx->device->os_info.fd, nvmeIoCtx->device->deviceVerbosity, firmwareDownload,
                    firmwareDLLength, nvmeIoCtx->timeout, FIRMWARE_FUNCTION_DOWNLOAD, fwdlFlags, &returnCode,
                    &nvmeIoCtx->device->os_info.last_error, &nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
                if (ret == SUCCESS)
                {
                    ret = dummy_Up_NVM_Status_FWDL(nvmeIoCtx, returnCode);
                }
                safe_free_firmwaredownload(&firmwareDownload);
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
        }
    return ret;
}

static eReturnValues send_Win_NVME_Firmware_Miniport_Activate(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = NOT_SUPPORTED;
    // Only one version of activate structure - TJE
    if (is_Windows_8_One_Or_Higher())
    {
        // V1 structures - Win8.1 & Win10
        ULONG                      firmwareActivateLength = sizeof(STORAGE_FIRMWARE_ACTIVATE);
        PSTORAGE_FIRMWARE_ACTIVATE firmwareActivate =
            M_REINTERPRET_CAST(PSTORAGE_FIRMWARE_ACTIVATE, safe_calloc(firmwareActivateLength, sizeof(UCHAR)));
        if (firmwareActivate)
        {
            uint32_t returnCode     = UINT32_C(0);
            uint32_t fwdlFlags      = FIRMWARE_REQUEST_FLAG_CONTROLLER; // start with this, but may need other flags
            uint8_t  activateAction = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 5, 3);
            // Setup input values
            firmwareActivate->Version        = STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION;
            firmwareActivate->Size           = sizeof(STORAGE_FIRMWARE_ACTIVATE);
            firmwareActivate->SlotToActivate = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0);
            if (activateAction == NVME_CA_ACTIVITE_ON_RST ||
                activateAction == NVME_CA_ACTIVITE_IMMEDIATE) // check the activate action
            {
                // Activate actions 2, & 3 sound like the closest match to this flag. Each of these requests switching
                // to the a firmware already on the drive. Activate action 0 & 1 say to replace a firmware image in a
                // specified slot (and to or not to activate).
                fwdlFlags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE;
            }

            // Issue the minport IOCTL
            ret = send_Win_Firmware_Miniport_Command(
                nvmeIoCtx->device->os_info.fd, nvmeIoCtx->device->deviceVerbosity, firmwareActivate,
                firmwareActivateLength, nvmeIoCtx->timeout, FIRMWARE_FUNCTION_ACTIVATE, fwdlFlags, &returnCode,
                &nvmeIoCtx->device->os_info.last_error, &nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
            if (ret == SUCCESS)
            {
                ret = dummy_Up_NVM_Status_FWDL(nvmeIoCtx, returnCode);
            }
            safe_free_firmwareactivate(&firmwareActivate);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}

#endif // WINVER >= SEA_WIN32_WINNT_WINBLUE

static eReturnValues close_SCSI_SRB_Handle(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device)
    {
        if (device->os_info.scsiSRBHandle != INVALID_HANDLE_VALUE)
        {
            if (CloseHandle(device->os_info.scsiSRBHandle))
            {
                ret                           = SUCCESS;
                device->os_info.scsiSRBHandle = INVALID_HANDLE_VALUE;
            }
            else
            {
                ret = FAILURE;
            }
            device->os_info.last_error = GetLastError();
        }
    }
    return ret;
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
eReturnValues close_Device(tDevice* dev)
{
    int retValue = 0;
    DISABLE_NONNULL_COMPARE
    if (dev != M_NULLPTR)
    {
#if defined(ENABLE_CSMI)
        if (is_CSMI_Handle(dev->os_info.name))
        {
            return close_CSMI_RAID_Device(dev);
        }
        else
#endif
        {
            close_SCSI_SRB_Handle(
                dev); //\\.\SCSIx: could be opened for different reasons...so we need to close it here.
            safe_free_csmi_dev_info(
                &dev->os_info
                     .csmiDeviceData); // CSMI may have been used, so free this memory if it was before we close out.
            CloseHandle(dev->os_info.forceUnitAccessRWfd); // if FUA handle was opened, this will close it out.
            retValue                = CloseHandle(dev->os_info.fd);
            dev->os_info.last_error = GetLastError();
            if (retValue)
            {
                dev->os_info.fd = INVALID_HANDLE_VALUE;
                return SUCCESS;
            }
            else
            {
                return FAILURE;
            }
        }
    }
    else
    {
        return MEMORY_FAILURE;
    }
    RESTORE_NONNULL_COMPARE
}

// opens this handle, but does nothing else with it
static eReturnValues open_SCSI_SRB_Handle(tDevice* device)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->os_info.scsi_addr.PortNumber != 0xFF)
    {
        // open the SCSI SRB handle
        DECLARE_ZERO_INIT_ARRAY(TCHAR, scsiDeviceName, WIN_MAX_DEVICE_NAME_LENGTH);
        TCHAR* ptrSCSIDeviceName = &scsiDeviceName[0];
        _stprintf_s(scsiDeviceName, WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs%d:"), WIN_SCSI_SRB,
                    device->os_info.scsi_addr.PortNumber);
        device->os_info.scsiSRBHandle = CreateFile(ptrSCSIDeviceName,
                                                   /* We are reverting to the GENERIC_WRITE | GENERIC_READ because
                                                      in the use case of a dll where multiple applications are using
                                                      our library, this needs to not request full access. If you suspect
                                                      some commands might fail (e.g. ISE/SED because of that
                                                      please write to developers  -MA */
                                                   GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                                   FILE_FLAG_OVERLAPPED,
#else
                                                   0,
#endif
                                                   M_NULLPTR);

        device->os_info.last_error = GetLastError();

        // Check if we get a invalid handle back.
        if (device->os_info.fd == INVALID_HANDLE_VALUE)
        {
            ret = FAILURE;
        }
        else
        {
            ret = SUCCESS;
        }
    }
    return ret;
}

//////////////////////////////////
// IOCTL_STORAGE_QUERY_PROPERTY //
//////////////////////////////////

// Best I can find right now is that this IOCTL is available in XP and up. Change the definition below as needed.
// Many of the IOCTLs are wrapped for the version of Win API that they appeared in but may not be perfect.
// Using MSFT's documentation on them as best we can find. - TJE
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WINXP

// This is a basic way of getting storage properties and cannot account for some which require additional input
// parameters Others with additional parameters should be in their own function since the additional parameters will
// vary!
static eReturnValues win_Get_Property_Data(HANDLE              deviceHandle,
                                           STORAGE_PROPERTY_ID propertyID,
                                           void*               outputData,
                                           DWORD               outputDataLength)
{
    eReturnValues ret = SUCCESS;
    if (outputData)
    {
        STORAGE_PROPERTY_QUERY query;
        BOOL                   success      = FALSE;
        DWORD                  returnedData = DWORD_C(0);
        safe_memset(&query, sizeof(STORAGE_PROPERTY_QUERY), 0, sizeof(STORAGE_PROPERTY_QUERY));
        query.PropertyId = propertyID;
        query.QueryType  = PropertyStandardQuery;
        success = DeviceIoControl(deviceHandle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(STORAGE_PROPERTY_QUERY),
                                  outputData, outputDataLength, &returnedData, M_NULLPTR);
        if (MSFT_BOOL_FALSE(success))
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// Determines if a property exists (true if it does) and optionally returns the length of the property
// NOTE: This does works by requesting only the header to get the size.
//       Trying to setup a query with PropertyExistsQuery results in strange results that aren't always true, so this
//       was the best way I found to handle this - TJE
static bool storage_Property_Exists(HANDLE deviceHandle, STORAGE_PROPERTY_ID propertyID, DWORD* propertySize)
{
    bool                      exists = false;
    STORAGE_DESCRIPTOR_HEADER header;
    safe_memset(&header, sizeof(STORAGE_DESCRIPTOR_HEADER), 0, sizeof(STORAGE_DESCRIPTOR_HEADER));
    if (SUCCESS == win_Get_Property_Data(deviceHandle, propertyID, &header, sizeof(STORAGE_DESCRIPTOR_HEADER)))
    {
        if (header.Size > 0)
        {
            exists = true;
            if (propertySize)
            {
                *propertySize = header.Size;
            }
        }
    }
    return exists;
}

// This is used internally by many of the pieces of code to read properties since it's very similar in how it gets
// requested for each of them. the "sizeOfExpectedPropertyStructure" is meant to be a sizeof(MSFT_STORAGE_STRUCT)
// output. This exists because some devices or drivers don't necessarily report accurate sizes for certain calls which
// can result in really weird data if not at least a minimum expected size.
static eReturnValues check_And_Get_Storage_Property(HANDLE              deviceHandle,
                                                    STORAGE_PROPERTY_ID propertyID,
                                                    void**              outputData,
                                                    size_t              sizeOfExpectedPropertyStructure)
{
    eReturnValues ret              = NOT_SUPPORTED;
    DWORD         propertyDataSize = DWORD_C(0);
    if (outputData)
    {
        if (storage_Property_Exists(deviceHandle, propertyID, &propertyDataSize))
        {
            propertyDataSize = C_CAST(DWORD, M_Max(sizeOfExpectedPropertyStructure, propertyDataSize));
            *outputData      = safe_calloc(propertyDataSize, sizeof(uint8_t));
            if (*outputData)
            {
                ret = win_Get_Property_Data(deviceHandle, propertyID, *outputData, propertyDataSize);
            }
            else
            {
                ret = MEMORY_FAILURE;
            }
        }
    }
    else
    {
        return BAD_PARAMETER;
    }
    return ret;
}

static M_INLINE void safe_free_device_descriptor(STORAGE_DEVICE_DESCRIPTOR** devdesc)
{
    safe_free_core(M_REINTERPRET_CAST(void**, devdesc));
}

static eReturnValues win_Get_Device_Descriptor(HANDLE deviceHandle, PSTORAGE_DEVICE_DESCRIPTOR* deviceData)
{
    return check_And_Get_Storage_Property(deviceHandle, StorageDeviceProperty, C_CAST(void**, deviceData),
                                          sizeof(STORAGE_DEVICE_DESCRIPTOR));
}

static M_INLINE void safe_free_adapter_descriptor(STORAGE_ADAPTER_DESCRIPTOR** adapterdesc)
{
    safe_free_core(M_REINTERPRET_CAST(void**, adapterdesc));
}

static eReturnValues win_Get_Adapter_Descriptor(HANDLE deviceHandle, PSTORAGE_ADAPTER_DESCRIPTOR* adapterData)
{
    return check_And_Get_Storage_Property(deviceHandle, StorageAdapterProperty, C_CAST(void**, adapterData),
                                          sizeof(STORAGE_ADAPTER_DESCRIPTOR));
}

// SCSI VPD device IDs
// static eReturnValues win_Get_Device_ID_Property(HANDLE deviceHandle, PSTORAGE_DEVICE_ID_DESCRIPTOR *deviceIdData)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceIdProperty, C_CAST(void**, deviceIdData),
//     sizeof(STORAGE_DEVICE_ID_DESCRIPTOR));
// }

#    if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_VISTA
// static eReturnValues win_Get_Write_Cache_Info(HANDLE *deviceHandle, PSTORAGE_WRITE_CACHE_PROPERTY *writeCacheInfo)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceWriteCacheProperty, C_CAST(void**,
//     writeCacheInfo), sizeof(STORAGE_WRITE_CACHE_PROPERTY));
// }

static M_INLINE void safe_free_storage_access_alignment(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR** accessalignment)
{
    safe_free_core(M_REINTERPRET_CAST(void**, accessalignment));
}

static eReturnValues win_Get_Access_Alignment_Descriptor(HANDLE*                               deviceHandle,
                                                         PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR* alignmentDescriptor)
{
    return check_And_Get_Storage_Property(deviceHandle, StorageAccessAlignmentProperty,
                                          C_CAST(void**, alignmentDescriptor),
                                          sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));
}
#    endif // SEA_WIN32_WINNT_VISTA

#    if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN7
// static eReturnValues win_Get_Seek_Time_Penalty(HANDLE *deviceHandle, PDEVICE_SEEK_PENALTY_DESCRIPTOR
// *seekTimePenaltyDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceSeekPenaltyProperty, C_CAST(void**,
//     seekTimePenaltyDescriptor), sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR));
// }

// static eReturnValues win_Get_Trim(HANDLE *deviceHandle, PDEVICE_TRIM_DESCRIPTOR *trimDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceTrimProperty, C_CAST(void**, trimDescriptor),
//     sizeof(DEVICE_TRIM_DESCRIPTOR));
// }
#    endif // SEA_WIN32_WINNT_WIN7

#    if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN8
// static eReturnValues win_Get_Logical_Block_Provisioning(HANDLE *deviceHandle, PDEVICE_LB_PROVISIONING_DESCRIPTOR
// *lbProvisioningDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceLBProvisioningProperty, C_CAST(void**,
//     lbProvisioningDescriptor), sizeof(DEVICE_LB_PROVISIONING_DESCRIPTOR));
// }

// static eReturnValues win_Get_Power_Property(HANDLE *deviceHandle, PDEVICE_POWER_DESCRIPTOR *powerDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDevicePowerProperty, C_CAST(void**, powerDescriptor),
//     sizeof(DEVICE_POWER_DESCRIPTOR));
// }

// static eReturnValues win_Get_Copy_Offload(HANDLE *deviceHandle, PDEVICE_COPY_OFFLOAD_DESCRIPTOR
// *copyOffloadDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceCopyOffloadProperty, C_CAST(void**,
//     copyOffloadDescriptor), sizeof(DEVICE_COPY_OFFLOAD_DESCRIPTOR));
// }

// static eReturnValues win_Get_Resiliency_Descriptor(HANDLE *deviceHandle, PSTORAGE_DEVICE_RESILIENCY_DESCRIPTOR
// *resiliencyDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceResiliencyProperty, C_CAST(void**,
//     resiliencyDescriptor), sizeof(STORAGE_DEVICE_RESILIENCY_DESCRIPTOR));
// }
#    endif // #if defined (WINVER) && WINVER >= SEA_WIN32_WINNT_WIN8

#    if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WINBLUE
// static eReturnValues win_Get_Medium_Product_Type(HANDLE *deviceHandle, PSTORAGE_MEDIUM_PRODUCT_TYPE_DESCRIPTOR
// *mediumType)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceMediumProductType, C_CAST(void**, mediumType),
//     sizeof(STORAGE_MEDIUM_PRODUCT_TYPE_DESCRIPTOR));
// }
#    endif // SEA_WIN32_WINNT_WINBLUE a.k.a. 8.1

#    if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN10
// static eReturnValues win_Get_IO_Capability(HANDLE *deviceHandle, PSTORAGE_DEVICE_IO_CAPABILITY_DESCRIPTOR
// *capabilityDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceIoCapabilityProperty, C_CAST(void**,
//     capabilityDescriptor), sizeof(STORAGE_DEVICE_IO_CAPABILITY_DESCRIPTOR));
// }

// static eReturnValues win_Get_Adapter_Temperature(HANDLE *deviceHandle, PSTORAGE_TEMPERATURE_DATA_DESCRIPTOR
// *temperatureDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageAdapterTemperatureProperty, C_CAST(void**,
//     temperatureDescriptor), sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR));
// }

// static eReturnValues win_Get_Device_Temperature(HANDLE *deviceHandle, PSTORAGE_TEMPERATURE_DATA_DESCRIPTOR
// *temperatureDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceTemperatureProperty, C_CAST(void**,
//     temperatureDescriptor), sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR));
// }

// static eReturnValues win_Get_Adapter_Physical_Topology(HANDLE *deviceHandle, PSTORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR
// *topologyDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageAdapterPhysicalTopologyProperty, C_CAST(void**,
//     topologyDescriptor), sizeof(STORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR));
// }

// static eReturnValues win_Get_Device_Physical_Topology(HANDLE *deviceHandle, PSTORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR
// *topologyDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDevicePhysicalTopologyProperty, C_CAST(void**,
//     topologyDescriptor), sizeof(STORAGE_PHYSICAL_TOPOLOGY_DESCRIPTOR));
// }

#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_10586
/*
#define STORAGE_ATTRIBUTE_BYTE_ADDRESSABLE_IO       0x01
#define STORAGE_ATTRIBUTE_BLOCK_IO                  0x02
#define STORAGE_ATTRIBUTE_DYNAMIC_PERSISTENCE       0x04
#define STORAGE_ATTRIBUTE_VOLATILE                  0x08
#define STORAGE_ATTRIBUTE_ASYNC_EVENT_NOTIFICATION  0x10
#define STORAGE_ATTRIBUTE_PERF_SIZE_INDEPENDENT     0x20
*/
// static eReturnValues win_Get_Device_Attributes(HANDLE *deviceHandle, PSTORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR
// *deviceAttributes)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceAttributesProperty, C_CAST(void**,
//     deviceAttributes), sizeof(STORAGE_DEVICE_ATTRIBUTES_DESCRIPTOR));
// }
#        endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_10586

#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
// static eReturnValues win_Get_Rpmb(HANDLE *deviceHandle, PSTORAGE_RPMB_DESCRIPTOR *rpmbDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageAdapterRpmbProperty, C_CAST(void**, rpmbDescriptor),
//     sizeof(STORAGE_RPMB_DESCRIPTOR));
// }

// static eReturnValues win_Get_Device_Management_Status(HANDLE *deviceHandle, PSTORAGE_DEVICE_MANAGEMENT_STATUS
// *managementStatus)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceManagementStatus, C_CAST(void**,
//     managementStatus), sizeof(STORAGE_DEVICE_MANAGEMENT_STATUS));
// }

// static eReturnValues win_Get_Adapter_Serial_Number(HANDLE *deviceHandle, PSTORAGE_ADAPTER_SERIAL_NUMBER
// *adapterSerialNumber)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageAdapterSerialNumberProperty, C_CAST(void**,
//     adapterSerialNumber), sizeof(STORAGE_ADAPTER_SERIAL_NUMBER));
// }

// static eReturnValues win_Get_Device_Location(HANDLE *deviceHandle, PSTORAGE_DEVICE_LOCATION_DESCRIPTOR
// *deviceLocation)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceLocationProperty, C_CAST(void**,
//     deviceLocation), sizeof(STORAGE_DEVICE_LOCATION_DESCRIPTOR));
// }
#        endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393

#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
// static eReturnValues win_Get_Crypto_Property(HANDLE *deviceHandle, PSTORAGE_CRYPTO_DESCRIPTOR *cryptoDescriptor)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageAdapterCryptoProperty, C_CAST(void**,
//     cryptoDescriptor), sizeof(STORAGE_CRYPTO_DESCRIPTOR));
// }

// STORAGE_DEVICE_NUMA_NODE_UNKNOWN
// static eReturnValues win_Get_Device_Numa_Property(HANDLE *deviceHandle, PSTORAGE_DEVICE_NUMA_PROPERTY *numaProperty)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceNumaProperty, C_CAST(void**, numaProperty),
//     sizeof(STORAGE_DEVICE_NUMA_PROPERTY));
// }
#        endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063

#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
// static eReturnValues win_Get_Device_Zoned_Property(HANDLE *deviceHandle, PSTORAGE_ZONED_DEVICE_DESCRIPTOR
// *zonedProperty)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceZonedDeviceProperty, C_CAST(void**,
//     zonedProperty), sizeof(STORAGE_ZONED_DEVICE_DESCRIPTOR));
// }

// static eReturnValues win_Get_Device_Unsafe_Shutdown_Count(HANDLE *deviceHandle, PSTORAGE_DEVICE_UNSAFE_SHUTDOWN_COUNT
// *unsafeShutdownCount)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceUnsafeShutdownCount, C_CAST(void**,
//     unsafeShutdownCount), sizeof(STORAGE_DEVICE_UNSAFE_SHUTDOWN_COUNT));
// }
#        endif // #if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299

#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_18362
// static eReturnValues win_Get_Device_Endurance(HANDLE *deviceHandle, PSTORAGE_HW_ENDURANCE_DATA_DESCRIPTOR
// *enduranceProperty)
//{
//     return check_And_Get_Storage_Property(deviceHandle, StorageDeviceEnduranceProperty, C_CAST(void**,
//     enduranceProperty), sizeof(STORAGE_HW_ENDURANCE_DATA_DESCRIPTOR));
// }
#        endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_18362

#    endif // SEA_WIN32_WINNT_WIN10

#endif // SEA_WIN32_WINNT_WINXP for storage query property calls.

#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WINXP && defined(IOCTL_DISK_UPDATE_PROPERTIES)
// This function is supposed to update partition table information after it has been changed...this should work after an
// erase has been done to a drive - TJE
static eReturnValues win_Update_Disk_Properties(HANDLE* deviceHandle)
{
    eReturnValues ret           = NOT_SUPPORTED;
    DWORD         bytesReturned = DWORD_C(0);
    if (DeviceIoControl(deviceHandle, IOCTL_DISK_UPDATE_PROPERTIES, M_NULLPTR, 0, M_NULLPTR, 0, &bytesReturned,
                        M_NULLPTR))
    {
        ret = SUCCESS;
    }
    return ret;
}

#endif // for IOCTL_DISK_UPDATE_PROPERTIES

eReturnValues os_Update_File_System_Cache(tDevice* device)
{
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WINXP && defined(IOCTL_DISK_UPDATE_PROPERTIES)
    // TODO: Need to find a way to support other things like RAID or CSMI, etc in the future - TJE
    return win_Update_Disk_Properties(device->os_info.fd);
#else
    // Not supported on this old of an OS - TJE
    M_USE_UNUSED(device);
    return NOT_SUPPORTED;
#endif
}

#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WINXP && defined(IOCTL_DISK_DELETE_DRIVE_LAYOUT)
// This function is supposed to update partition table information after it has been changed...this should work after an
// erase has been done to a drive - TJE
static eReturnValues win_Delete_Drive_Layout(HANDLE* deviceHandle)
{
    eReturnValues ret           = NOT_SUPPORTED;
    DWORD         bytesReturned = DWORD_C(0);
    if (DeviceIoControl(deviceHandle, IOCTL_DISK_DELETE_DRIVE_LAYOUT, M_NULLPTR, 0, M_NULLPTR, 0, &bytesReturned,
                        M_NULLPTR))
    {
        ret = SUCCESS;
    }
    return ret;
}

#endif // for IOCTL_DISK_UPDATE_PROPERTIES

eReturnValues os_Erase_Boot_Sectors(tDevice* device)
{
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WINXP && defined(IOCTL_DISK_UPDATE_PROPERTIES)
    if (device->raid_device || device->issue_io || device->issue_nvme_io)
    {
        // do not do anything here for RAID devices. This is really only needed for JBOD.
        return SUCCESS;
    }
    else
    {
        return win_Delete_Drive_Layout(device->os_info.fd);
    }
#else
    // Not supported on this old of an OS - TJE
    M_USE_UNUSED(device);
    return NOT_SUPPORTED;
#endif
}

static M_INLINE void safe_free_disk_geometry(DISK_GEOMETRY** geom)
{
    safe_free_core(M_REINTERPRET_CAST(void**, geom));
}

// WinVer not wrapping this IOCTL...so it's probably old enough not to need it - TJE
static eReturnValues win_Get_Drive_Geometry(HANDLE devHandle, PDISK_GEOMETRY* geom)
{
    eReturnValues ret           = FAILURE;
    DWORD         bytesReturned = DWORD_C(0);
    DWORD         diskGeomSize  = sizeof(DISK_GEOMETRY);
    if (geom)
    {
        *geom = M_REINTERPRET_CAST(PDISK_GEOMETRY, safe_malloc(diskGeomSize));
        if (*geom)
        {
            safe_memset(*geom, diskGeomSize, 0, diskGeomSize);
            if (DeviceIoControl(devHandle, IOCTL_DISK_GET_DRIVE_GEOMETRY, M_NULLPTR, 0, *geom, diskGeomSize,
                                &bytesReturned, M_NULLPTR))
            {
                ret = SUCCESS;
            }
            else
            {
                safe_free_disk_geometry(geom);
            }
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// static eReturnValues win_Disk_Is_Writable(HANDLE devHandle, bool *writable)
//{
//     eReturnValues ret = SUCCESS;
//     DWORD bytesReturned = DWORD_C(0);
//     if (!writable)
//     {
//         return BAD_PARAMETER;
//     }
//     if (DeviceIoControl(devHandle, IOCTL_DISK_IS_WRITABLE, M_NULLPTR, 0, M_NULLPTR, 0, &bytesReturned, M_NULLPTR))
//     {
//         *writable = true;
//     }
//     else
//     {
//         *writable = false;
//         DWORD lastError = GetLastError();
//         switch (lastError)
//         {
//         case ERROR_WRITE_PROTECT:
//             *writable = false;
//             break;
//         default:
//             *writable = true;
//             break;
//         }
//         print_Windows_Error_To_Screen(lastError);
//     }
//     return ret;
// }

#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN2K
// static M_INLINE void safe_free_disk_controller_number(DISK_CONTROLLER_NUMBER **ctrlnum)
// {
//     safe_free_core(M_REINTERPRET_CAST(void**, ctrlnum));
// }
//
// static eReturnValues win_Get_IDE_Disk_Controller_Number_And_Disk_Number(HANDLE *devHandle, PDISK_CONTROLLER_NUMBER
// *numbers)
//{
//    eReturnValues ret = FAILURE;
//    DWORD bytesReturned = DWORD_C(0);
//    DWORD controllerNumberSize = sizeof(DISK_CONTROLLER_NUMBER);
//    if (numbers)
//    {
//        *numbers = M_REINTERPRET_CAST(PDISK_CONTROLLER_NUMBER, safe_malloc(controllerNumberSize));
//        if (*numbers)
//        {
//            safe_memset(*numbers, controllerNumberSize, 0, controllerNumberSize);
//            if (DeviceIoControl(devHandle, IOCTL_DISK_CONTROLLER_NUMBER, M_NULLPTR, 0, *numbers, controllerNumberSize,
//            &bytesReturned, M_NULLPTR))
//            {
//                ret = SUCCESS;
//            }
//            else
//            {
//                safe_free_disk_controller_number(numbers);
//            }
//        }
//        else
//        {
//            ret = MEMORY_FAILURE;
//        }
//    }
//    else
//    {
//        ret = BAD_PARAMETER;
//    }
//    return ret;
//}
#endif // SEA_WIN32_WINNT_NT4

#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN2K
// Geom param is required as this allocates memory for this call.
// partInfo and detectInfo are optional and are meant to be helpful to set these pointers correctly for you from
// returned data rather than needing to figure it out yourself. geom should be free'd when done with it since it is
// allocated on the heap. If partinfo or detectinfo are null on completion, then these we not returned in this IOCTL
/* Example Use:
    PDISK_GEOMETRY_EX diskGeom = M_NULLPTR;
    PDISK_PARTITION_INFO partInfo = M_NULLPTR;
    PDISK_DETECTION_INFO diskDetect = M_NULLPTR;

    if (SUCCESS == win_Get_Drive_Geometry_Ex(device->os_info.fd, &diskGeom, &partInfo, &diskDetect))
    {
        //Do stuff with diskGeom, partInfo, & diskDetect
        safe_free(&diskGeom);
    }
*/

static M_INLINE void safe_free_disk_geometry_ex(DISK_GEOMETRY_EX** geom)
{
    safe_free_core(M_REINTERPRET_CAST(void**, geom));
}

static eReturnValues win_Get_Drive_Geometry_Ex(HANDLE                devHandle,
                                               PDISK_GEOMETRY_EX*    geom,
                                               PDISK_PARTITION_INFO* partInfo,
                                               PDISK_DETECTION_INFO* detectInfo)
{
    eReturnValues ret           = FAILURE;
    DWORD         bytesReturned = DWORD_C(0);
    DWORD         diskGeomSize =
        sizeof(DISK_GEOMETRY) + sizeof(LARGE_INTEGER) + sizeof(DISK_PARTITION_INFO) + sizeof(DISK_DETECTION_INFO);
    if (geom)
    {
        *geom = M_REINTERPRET_CAST(PDISK_GEOMETRY_EX, safe_malloc(diskGeomSize));
        if (*geom)
        {
            safe_memset(*geom, diskGeomSize, 0, diskGeomSize);
            if (DeviceIoControl(devHandle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, M_NULLPTR, 0, *geom, diskGeomSize,
                                &bytesReturned, M_NULLPTR))
            {
                ret = SUCCESS;
                // Setup the other pointers if they were provided.
                if (partInfo)
                {
                    *partInfo = M_REINTERPRET_CAST(PDISK_PARTITION_INFO, (*geom)->Data);
                    if ((*partInfo)->SizeOfPartitionInfo)
                    {
                        if (detectInfo)
                        {
                            *detectInfo = M_REINTERPRET_CAST(PDISK_DETECTION_INFO,
                                                             &(*geom)->Data[(*partInfo)->SizeOfPartitionInfo]);
                            if (!(*detectInfo)->SizeOfDetectInfo)
                            {
                                *detectInfo = M_NULLPTR;
                            }
                        }
                    }
                    else
                    {
                        *partInfo = M_NULLPTR;
                        if (detectInfo)
                        {
                            *detectInfo = M_NULLPTR;
                        }
                    }
                }
            }
            else
            {
                safe_free_disk_geometry_ex(geom);
            }
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// static M_INLINE void safe_free_len_info(GET_LENGTH_INFORMATION **length)
// {
//     safe_free_core(M_REINTERPRET_CAST(void**, length));
// }
// static eReturnValues win_Get_Length_Information(HANDLE *devHandle, PGET_LENGTH_INFORMATION *length)
//{
//    eReturnValues ret = FAILURE;
//    DWORD bytesReturned = DWORD_C(0);
//    DWORD lengthInfoSize = sizeof(GET_LENGTH_INFORMATION);
//    if (length)
//    {
//        *length = M_REINTERPRET_CAST(PGET_LENGTH_INFORMATION, safe_malloc(lengthInfoSize));
//        if (*length)
//        {
//            safe_memset(*length, lengthInfoSize, 0, lengthInfoSize);
//            if (DeviceIoControl(devHandle, IOCTL_DISK_GET_LENGTH_INFO, M_NULLPTR, 0, *length, lengthInfoSize,
//            &bytesReturned, M_NULLPTR))
//            {
//                ret = SUCCESS;
//            }
//            else
//            {
//                safe_free_len_info(length);
//            }
//        }
//        else
//        {
//            ret = MEMORY_FAILURE;
//        }
//    }
//    else
//    {
//        ret = BAD_PARAMETER;
//    }
//    return ret;
//}

#endif // SEA_WIN32_WINNT_WIN2K

#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN8
// static eReturnValues win_SCSI_Get_Inquiry_Data(HANDLE deviceHandle, PSCSI_ADAPTER_BUS_INFO *scsiBusInfo)
//{
//     eReturnValues ret = SUCCESS;
//     UCHAR busCount = 1, luCount = 1;
//     DWORD inquiryDataSize = sizeof(SCSI_INQUIRY_DATA) + INQUIRYDATABUFFERSIZE;//this is supposed to be rounded up to
//     an alignment boundary??? but that is not well described... DWORD busDataLength = sizeof(SCSI_ADAPTER_BUS_INFO) +
//     busCount * sizeof(SCSI_BUS_DATA) + luCount * inquiryDataSize;//Start with this, but more memory may be necessary.
//     *scsiBusInfo = M_REINTERPRET_CAST(PSCSI_ADAPTER_BUS_INFO, safe_calloc_aligned(busDataLength, sizeof(uint8_t),
//     8)); if (scsiBusInfo)
//     {
//         BOOL success = FALSE;
//         DWORD returnedBytes = DWORD_C(0);
//         while (MSFT_BOOL_FALSE(success))
//         {
//             //try this ioctl and reallocate memory if not enough space error is returned until it can be read!
//             success = DeviceIoControl(deviceHandle, IOCTL_SCSI_GET_INQUIRY_DATA, M_NULLPTR, 0, scsiBusInfo,
//             busDataLength, &returnedBytes, M_NULLPTR); if (!success)
//             {
//                 DWORD error = GetLastError();
//                 //figure out what the error was to see if we need to allocate more memory, or exit with errors.
//                 if (error == ERROR_INSUFFICIENT_BUFFER)
//                 {
//                     //MSFT recommends doubling the buffer size until this passes.
//                     void *tempBuf = M_NULLPTR;
//                     busDataLength *= 2;
//                     tempBuf = safe_realloc_aligned(scsiBusInfo, busDataLength / 2, busDataLength, 8);
//                     if (tempBuf)
//                     {
//                         scsiBusInfo = tempBuf;
//                     }
//                 }
//                 else
//                 {
//                     print_Windows_Error_To_Screen(error);
//                     ret = FAILURE;
//                     break;
//                 }
//             }
//         }
//     }
//     else
//     {
//         ret = MEMORY_FAILURE;
//     }
//     return ret;
// }
#endif // SEA_WIN32_WINNT_WIN8

#define MAX_VOL_BITS     (32U)
#define MAX_VOL_STR_LEN  (8U)
#define MAX_DISK_EXTENTS (32U)

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
static eReturnValues get_Win_Device(const char* filename, tDevice* device)
{
    eReturnValues               ret          = FAILURE;
    eReturnValues               win_ret      = SUCCESS;
    PSTORAGE_DEVICE_DESCRIPTOR  device_desc  = M_NULLPTR;
    PSTORAGE_ADAPTER_DESCRIPTOR adapter_desc = M_NULLPTR;

    DECLARE_ZERO_INIT_ARRAY(TCHAR, device_name, WIN_MAX_DEVICE_NAME_LENGTH);
    TCHAR* ptrDeviceName = &device_name[0];
    _stprintf_s(device_name, WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs"), filename);

    // printf("%s -->\n Opening Device %s\n",__FUNCTION__, filename);
    if (!(validate_Device_Struct(device->sanity)))
    {
        return LIBRARY_MISMATCH;
    }
    // lets try to open the device.
    device->os_info.fd = CreateFile(ptrDeviceName,
                                    /* We are reverting to the GENERIC_WRITE | GENERIC_READ because
                                       in the use case of a dll where multiple applications are using
                                       our library, this needs to not request full access. If you suspect
                                       some commands might fail (e.g. ISE/SED because of that
                                       please write to developers  -MA */
                                    GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                    FILE_FLAG_OVERLAPPED,
#else
                                    0,
#endif
                                    M_NULLPTR);

    device->os_info.last_error = GetLastError();

    // Check if we get a invalid handle back.
    if (device->os_info.fd == INVALID_HANDLE_VALUE)
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Error: opening dev %s. ", filename);
            print_Windows_Error_To_Screen(device->os_info.last_error);
            printf("\n");
        }
        ret = FAILURE;
    }
    else
    {
#if defined(WIN_DEBUG)
        printf("WIN: opened dev\n");
#endif                                                        // WIN_DEBUG
        device->os_info.scsiSRBHandle = INVALID_HANDLE_VALUE; // set this to invalid ahead of anywhere that it might get
                                                              // opened below for discovering additional capabilities.
        // set the handle name
        safe_strcpy(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, filename);

        if (strstr(device->os_info.name, WIN_PHYSICAL_DRIVE))
        {
            unsigned long drive    = 0UL;
            char*         drivenum = strstr(device->os_info.name, WIN_PHYSICAL_DRIVE) + safe_strlen(WIN_PHYSICAL_DRIVE);
            if (0 != safe_strtoul(&drive, drivenum, M_NULLPTR, BASE_10_DECIMAL))
            {
                return FAILURE;
            }
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "PD%lu", drive);
            device->os_info.os_drive_number = drive;
        }
        else if (strstr(device->os_info.name, WIN_CDROM_DRIVE))
        {
            unsigned long drive    = 0UL;
            char*         drivenum = strstr(device->os_info.name, WIN_CDROM_DRIVE) + safe_strlen(WIN_CDROM_DRIVE);
            if (0 != safe_strtoul(&drive, drivenum, M_NULLPTR, BASE_10_DECIMAL))
            {
                return FAILURE;
            }
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "CDROM%lu", drive);
            device->os_info.os_drive_number = drive;
        }
        else if (strstr(device->os_info.name, WIN_TAPE_DRIVE))
        {
            unsigned long drive    = 0UL;
            char*         drivenum = strstr(device->os_info.name, WIN_TAPE_DRIVE) + safe_strlen(WIN_TAPE_DRIVE);
            if (0 != safe_strtoul(&drive, drivenum, M_NULLPTR, BASE_10_DECIMAL))
            {
                return FAILURE;
            }
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "TAPE%lu", drive);
            device->os_info.os_drive_number = drive;
        }
        else if (strstr(device->os_info.name, WIN_CHANGER_DEVICE))
        {
            unsigned long drive    = 0UL;
            char*         drivenum = strstr(device->os_info.name, WIN_CHANGER_DEVICE) + safe_strlen(WIN_CHANGER_DEVICE);
            if (0 != safe_strtoul(&drive, drivenum, M_NULLPTR, BASE_10_DECIMAL))
            {
                return FAILURE;
            }
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "CHGR%lu", drive);
            device->os_info.os_drive_number = drive;
        }
        // NOTE: No final else returning failure as we want to support some other handles that don't map to these easy
        // names.
        //       There are very long names for a drive handle we can also support if a caller knows how to pass them in.
#if defined(WIN_DEBUG)
        printf("WIN: Checking for volumes\n");
#endif // WIN_DEBUG
       // map the drive to a volume letter
        DWORD    driveLetters  = DWORD_C(0);
        TCHAR    currentLetter = 'A';
        uint32_t volumeCounter = UINT32_C(0);
        driveLetters = GetLogicalDrives(); // TODO: This will remount everything. If we can figure out a better way to
                                           // do this, we should so that not everything is remounted. - TJE
        device->os_info.fileSystemInfo.fileSystemInfoValid =
            true; // Setting this since we have code here to detect the volumes in the OS
        for (uint32_t volIter = UINT32_C(0); volIter < MAX_VOL_BITS && currentLetter <= 'Z';
             ++volIter, ++currentLetter, ++volumeCounter)
        {
            if (M_BitN(volIter) & driveLetters)
            {
                // a volume with this letter exists...check it's physical device number
                DECLARE_ZERO_INIT_ARRAY(TCHAR, volume_name, MAX_VOL_STR_LEN);
                TCHAR* ptrLetterName = &volume_name[0];
                _sntprintf_s(ptrLetterName, MAX_VOL_STR_LEN, _TRUNCATE, TEXT("\\\\.\\%c:"), currentLetter);
                HANDLE letterHandle = CreateFile(ptrLetterName, GENERIC_WRITE | GENERIC_READ,
                                                 FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
                                                 // #if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                                 //                     FILE_FLAG_OVERLAPPED,
                                                 // #else
                                                 0,
                                                 // #endif
                                                 M_NULLPTR);
                if (letterHandle != INVALID_HANDLE_VALUE)
                {
                    DWORD returnedBytes = DWORD_C(0);
                    DWORD maxExtents =
                        MAX_DISK_EXTENTS; // https://technet.microsoft.com/en-us/library/cc772180(v=ws.11).aspx
                    PVOLUME_DISK_EXTENTS diskExtents = M_NULLPTR;
                    DWORD                diskExtentsSizeBytes =
                        M_STATIC_CAST(DWORD, sizeof(VOLUME_DISK_EXTENTS) + (sizeof(DISK_EXTENT) * maxExtents));
                    diskExtents = M_REINTERPRET_CAST(PVOLUME_DISK_EXTENTS, safe_malloc(diskExtentsSizeBytes));
                    if (diskExtents != M_NULLPTR)
                    {
                        safe_memset(diskExtents, diskExtentsSizeBytes, 0, diskExtentsSizeBytes);
                        if (DeviceIoControl(letterHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, M_NULLPTR, 0,
                                            diskExtents, diskExtentsSizeBytes, &returnedBytes, M_NULLPTR))
                        {
                            for (DWORD counter = DWORD_C(0); counter < diskExtents->NumberOfDiskExtents; ++counter)
                            {
                                if (diskExtents->Extents[counter].DiskNumber == device->os_info.os_drive_number)
                                {
                                    device->os_info.fileSystemInfo.hasActiveFileSystem =
                                        true; // We found a filesystem for this drive, so set this to true.

                                    // Set a bit to note that this particular volume (letter) is on this device
                                    device->os_info.volumeBitField |= (UINT32_C(1) << volumeCounter);

                                    // now we need to determine if this volume has the system directory on it.
                                    DECLARE_ZERO_INIT_ARRAY(TCHAR, systemDirectoryPath, MAX_PATH);

                                    if (GetSystemDirectory(systemDirectoryPath, MAX_PATH) > 0)
                                    {
                                        if (_tcslen(systemDirectoryPath) > 0)
                                        {
                                            // we need to check only the first letter of the returned string since this
                                            // is the volume letter
                                            if (systemDirectoryPath[0] == currentLetter)
                                            {
                                                // This volume contains a system directory
                                                device->os_info.fileSystemInfo.isSystemDisk = true;
                                            }
                                        }
#if defined(WIN_DEBUG)
                                        else
                                        {
                                            printf(
                                                "\nWARNING! Asked for system directory, but got a zero length string! "
                                                "Unable to detect if this is a drive with a system folder!\n");
                                        }
#endif // WIN_DEBUG
                                    }
                                }
                            }
                        }
                        // DWORD lastError = GetLastError();
                        safe_free(M_REINTERPRET_CAST(void**, &diskExtents));
                    }
                }
                CloseHandle(letterHandle);
            }
        }

        // set the OS Type
        device->os_info.osType = OS_WINDOWS;
#if defined(WIN_DEBUG)
        printf("WIN: getting SCSI address\n");
#endif // WIN_DEBUG

        // Lets get the SCSI address
        win_Get_SCSI_Address(device->os_info.fd, &device->os_info.scsi_addr);

#if defined(WIN_DEBUG)
        printf("WIN: det adapter descriptor\n");
#endif // WIN_DEBUG
       //  Lets get some properties.
        win_ret = win_Get_Adapter_Descriptor(device->os_info.fd, &adapter_desc);

        if (win_ret == SUCCESS)
        {
            // TODO: Copy any of the adapter stuff.
#if defined(WIN_DEBUG)
            printf("Adapter BusType: ");
            print_bus_type(adapter_desc->BusType);
            printf(" \n");
#endif // WIN_DEBUG
       // saving max transfer size (in bytes)
            device->os_info.adapterMaxTransferSize = adapter_desc->MaximumTransferLength;

            // saving the SRB type so that we know when an adapter supports the new SCSI Passthrough EX IOCTLS - TJE
#if WINVER >=                                                                                                          \
    SEA_WIN32_WINNT_WIN8 // If this check is wrong, make sure minGW is properly defining WINVER in the makefile.
            if (is_Windows_8_Or_Higher()) // from opensea-common now to remove versionhelpes.h include
            {
                device->os_info.srbtype = adapter_desc->SrbType;
            }
            else
#endif
            {
                device->os_info.srbtype = SRB_TYPE_SCSI_REQUEST_BLOCK;
            }
            device->os_info.minimumAlignment = C_CAST(uint8_t, adapter_desc->AlignmentMask + 1);
            device->os_info.alignmentMask    = adapter_desc->AlignmentMask; // may be needed later....currently unused
#if defined(WIN_DEBUG)
            printf("WIN: get device descriptor\n");
#endif // WIN_DEBUG
            win_ret = win_Get_Device_Descriptor(device->os_info.fd, &device_desc);
            if (win_ret == SUCCESS)
            {
                bool          checkForCSMI = false;
                bool          checkForNVMe = false;
                eReturnValues fwdlResult   = NOT_SUPPORTED;

                // save the bus types to the tDevice struct since they may be helpful in certain debug scenarios
                device->os_info.adapterDescBusType = adapter_desc->BusType;
                device->os_info.deviceDescBusType  = C_CAST(
                    uint8_t, device_desc->BusType); // NOTE: This enum seems to be a byte in definition today, but if we
                                                     // run into future issues, we may need to change it. - TJE
#if defined(WIN_DEBUG)
                printf("WIN: get adapter IDs (VID/PID for USB or PCIe)\n");
#endif // WIN_DEBUG
                get_Adapter_IDs(device, device_desc, device_desc->Size);

#if WINVER >= SEA_WIN32_WINNT_WINBLUE && defined(IOCTL_SCSI_MINIPORT_FIRMWARE)
#    if defined(WIN_DEBUG)
                printf("WIN: Get MiniPort FWDL capabilities\n");
#    endif // WIN_DEBUG
                fwdlResult =
                    get_Win_FWDL_Miniport_Capabilities(device, device_desc->BusType == BusTypeNvme ? true : false);
#endif

#if WINVER >= SEA_WIN32_WINNT_WIN10
                if (fwdlResult != SUCCESS)
                {
#    if defined(WIN_DEBUG)
                    printf("WIN: get Win10 FWDL support\n");
#    endif // WIN_DEBUG
                    get_Windows_FWDL_IO_Support(device, device_desc->BusType);
                }
#else
                M_USE_UNUSED(fwdlResult);
                device->os_info.fwdlIOsupport.fwdlIOSupported = false; // this API is not available before Windows 10
#endif
#if defined(WIN_DEBUG)
                printf("Drive BusType: ");
                print_bus_type(device_desc->BusType);
                printf(" \n");
#endif // WIN_DEBUG

                if (device_desc->BusType ==
                    BusTypeUnknown) // Add other device types that can't be handled with other methods of SCSI or ATA
                                    // passthrough among other options below.
                {
                    // special case to handle unknown device types!
                    device->drive_info.drive_type                       = SCSI_DRIVE;
                    device->drive_info.interface_type                   = SCSI_INTERFACE;
                    device->os_info.ioType                              = WIN_IOCTL_BASIC;
                    checkForCSMI                                        = false;
                    checkForNVMe                                        = false;
                    device->os_info.winSMARTCmdSupport.ataIDsupported   = false;
                    device->os_info.winSMARTCmdSupport.atapiIDsupported = false;
                    device->os_info.winSMARTCmdSupport.deviceBitmap     = 0;
                    device->os_info.winSMARTCmdSupport.smartIOSupported = false;
                    device->os_info.winSMARTCmdSupport.smartSupported   = false;
                    device->os_info.osReadWriteRecommended =
                        true; // recommend calling this to reduce any translation of calls that are received. - TJE
                }
                else if ((adapter_desc->BusType == BusTypeAta) || (device_desc->BusType == BusTypeAta))
                {
                    device->drive_info.drive_type     = ATA_DRIVE;
                    device->drive_info.interface_type = IDE_INTERFACE;
                    device->os_info.ioType            = WIN_IOCTL_ATA_PASSTHROUGH;
#if defined(WIN_DEBUG)
                    printf("WIN: get SMART IO support ATA\n");
#endif                                                    // WIN_DEBUG
                    get_Windows_SMART_IO_Support(device); // might be used later
                    checkForCSMI = true;
                }
                else if ((adapter_desc->BusType == BusTypeAtapi) || (device_desc->BusType == BusTypeAtapi))
                {
                    device->drive_info.drive_type                                 = ATAPI_DRIVE;
                    device->drive_info.interface_type                             = IDE_INTERFACE;
                    device->drive_info.passThroughHacks.someHacksSetByOSDiscovery = true;
                    device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported =
                        true; // This is GENERALLY true because almost all times the CDB is passed through instead of
                              // translated for a passthrough command, so just block it no matter what.
                    // These devices use the SCSI MMC command set in packet commands over ATA...other than for a few
                    // other commands. If we care to properly support this, we should investigate either how to send a
                    // packet command, or we should try issuing only SCSI commands
                    device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
#if defined(WIN_DEBUG)
                    printf("WIN: get SMART IO support ATAPI\n");
#endif                                                    // WIN_DEBUG
                    get_Windows_SMART_IO_Support(device); // might be used later
                }
                else if (device_desc->BusType == BusTypeSata)
                {
                    if (strncmp(WIN_CDROM_DRIVE, filename, safe_strlen(WIN_CDROM_DRIVE)) == 0)
                    {
                        device->drive_info.drive_type = ATAPI_DRIVE;
                    }
                    else
                    {
                        device->drive_info.drive_type = ATA_DRIVE;
                        checkForCSMI                  = true;
                    }
                    // we are assuming, for now, that SAT translation is being done below, and so far through testing on
                    // a few chipsets this appears to be correct.
                    device->drive_info.interface_type                               = IDE_INTERFACE;
                    device->os_info.ioType                                          = WIN_IOCTL_SCSI_PASSTHROUGH;
                    device->drive_info.passThroughHacks.someHacksSetByOSDiscovery   = true;
                    device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;
#if defined(WIN_DEBUG)
                    printf("WIN: get SMART IO support SATA\n");
#endif                                                    // WIN_DEBUG
                    get_Windows_SMART_IO_Support(device); // might be used later
                }
                else if (device_desc->BusType == BusTypeUsb)
                {
                    // set this to SCSI_DRIVE. The fill_Drive_Info_Data call will change this if it supports SAT
                    device->drive_info.drive_type     = SCSI_DRIVE;
                    device->drive_info.interface_type = USB_INTERFACE;
                    device->os_info.ioType            = WIN_IOCTL_SCSI_PASSTHROUGH;
                }
                else if (device_desc->BusType == BusType1394)
                {
                    // set this to SCSI_DRIVE. The fill_Drive_Info_Data call will change this if it supports SAT
                    device->drive_info.drive_type     = SCSI_DRIVE;
                    device->drive_info.interface_type = IEEE_1394_INTERFACE;
                    device->os_info.ioType            = WIN_IOCTL_SCSI_PASSTHROUGH;
                }
                // NVMe bustype can be defined for Win7 with openfabrics nvme driver, so make sure we can handle it...it
                // shows as a SCSI device on this interface unless you use a SCSI?: handle with the IOCTL directly to
                // the driver.
                else if (device_desc->BusType == BusTypeNvme)
                {
                    device->drive_info.namespaceID = device->os_info.scsi_addr.Lun + 1;
                    if (device_desc->VendorIdOffset) // Open fabrics will set a vendorIDoffset, MSFT driver will not.
                    {
#if defined(WIN_DEBUG)
                        printf("WIN: checking for additional NVMe driver interfaces\n");
#endif // WIN_DEBUG
                        if (device->os_info.scsiSRBHandle != INVALID_HANDLE_VALUE ||
                            SUCCESS == open_SCSI_SRB_Handle(device))
                        {
                            // now see if the IOCTL is supported or not
#if defined(ENABLE_OFNVME) || defined(ENABLE_INTEL_RST)
                            // if defined hell since we can flag these interfaces on and off
                            bool foundNVMePassthrough = false;
#    if defined(ENABLE_OFNVME)
#        if defined(WIN_DEBUG)
                            printf("WIN: checking for open fabrics NVMe IOCTL\n");
#        endif // WIN_DEBUG
                            if (!foundNVMePassthrough && supports_OFNVME_IO(device->os_info.scsiSRBHandle))
                            {
                                // congratulations! nvme commands can be passed through!!!
                                device->os_info.openFabricsNVMePassthroughSupported = true;
                                device->drive_info.drive_type                       = NVME_DRIVE;
                                device->drive_info.interface_type                   = NVME_INTERFACE;
                                device->os_info.osReadWriteRecommended =
                                    true; // setting this so that read/write LBA functions will call Windows functions
                                          // when possible for this.
                                foundNVMePassthrough = true;
                                // This is set because asking for list of pages + subpages returns a list wihtout them,
                                // so it is interpretted wrong. It returns the data for list of pages WITHOUT subpages,
                                // so CDB is not validated correctly. It is possible that other drivers based on ofnvme
                                // work different, but I don't have any others to test. Tested using the latest version
                                // before it stopped receiving support-TJE Code in ofnvme 1.5 does not properly validate
                                // if log subpages or mode subpages are requested and returns the incorrect data. setup
                                // some flags for everything else though based on what is in the translation code.
                                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
                                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
                                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
                                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
                                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
                                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported =
                                    true; // depending on if this is uncommented or not in the compiled driver, it may
                                          // or may not work, but code seems to have some knowledge of this translation
                                          // in some cases.-TJE
                                device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;

#        if defined(WIN_DEBUG)
                                printf("WIN: open fabrics NVMe supported\n");
#        endif // WIN_DEBUG
                            }
#    endif // ENABLE_OFNVME
#    if defined(ENABLE_INTEL_RST)
#        if defined(WIN_DEBUG)
                            printf("WIN: Checking for Intel CSMI + RST NVMe support\n");
#        endif // WIN_DEBUG
                            if (!foundNVMePassthrough && device_Supports_CSMI_With_RST(device))
                            {
                                device->drive_info.drive_type                                 = NVME_DRIVE;
                                device->drive_info.interface_type                             = NVME_INTERFACE;
                                device->os_info.intelNVMePassthroughSupported                 = true;
                                foundNVMePassthrough                                          = true;
                                device->drive_info.passThroughHacks.someHacksSetByOSDiscovery = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .identifyGeneric = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .identifyController = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .identifyNamespace = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage =
                                    true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getFeatures =
                                    true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.setFeatures =
                                    true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .firmwareCommit = true; // NOTE: Always requires a reboot for activation even when
                                                            // error code does not indicate this
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .firmwareDownload = true;
                                // supported features: power management, temperature threshold, APST, HCTM (17.2+),
                                // vendor specific.
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.vendorUnique =
                                    true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .deviceSelfTest = true; // not documented, but found with trial and error.
                                // NOTE: some of this comes from an intel document and some comes from trial and error.
                                //       It is extremely likely that the supported commands vary by driver version, but
                                //       we do not have that level of detail to populate this list at this time-TJE

#        if defined(WIN_DEBUG)
                                printf("WIN: Intel CSMI + NVMe supported\n");
#        endif // WIN_DEBUG
                            }
#    endif // ENABLE_INTEL_RST
                            if (!foundNVMePassthrough)
#endif // ENABLE_OFNVME || ENABLE_INTEL_RST
                            {
#if defined(WIN_DEBUG)
                                printf("WIN: no NVMe passthrough found\n");
#endif // WIN_DEBUG
       // unable to do passthrough, and isn't in normal Win10 mode, this means it's some other driver that we don't know
       // how to use. Treat as SCSI
                                device->os_info.intelNVMePassthroughSupported       = false;
                                device->os_info.openFabricsNVMePassthroughSupported = false;
                                device->drive_info.drive_type                       = SCSI_DRIVE;
                                device->drive_info.interface_type                   = SCSI_INTERFACE;
                                device->os_info.ioType                              = WIN_IOCTL_SCSI_PASSTHROUGH;
                            }
                        }
                        else
                        {
#if defined(WIN_DEBUG)
                            printf("WIN: treat as SCSI. Closing SCSI SRB handle\n");
#endif // WIN_DEBUG
       // close the handle that was opened. TODO: May need to remove this in the future.
                            close_SCSI_SRB_Handle(device);
                            device->os_info.intelNVMePassthroughSupported       = false;
                            device->os_info.openFabricsNVMePassthroughSupported = false;
                            // treat as SCSI
                            device->drive_info.drive_type     = SCSI_DRIVE;
                            device->drive_info.interface_type = SCSI_INTERFACE;
                            device->os_info.ioType            = WIN_IOCTL_SCSI_PASSTHROUGH;
                        }
                    }
                    else
                    {
#if WINVER >= SEA_WIN32_WINNT_WIN10
#    if defined(WIN_DEBUG)
                        printf("WIN: Checking Win version for NVMe IOCTL support level\n");
#    endif // WIN_DEBUG
                        if (is_Windows_10_Or_Higher())
                        {
#    if defined(WIN_DEBUG)
                            printf("WIN: Win10+\n");
#    endif // WIN_DEBUG
                            device->drive_info.drive_type     = NVME_DRIVE;
                            device->drive_info.interface_type = NVME_INTERFACE;
                            device->os_info.osReadWriteRecommended =
                                true; // setting this so that read/write LBA functions will call Windows functions when
                                      // possible for this, althrough SCSI Read/write 16 will work too!
                            device->drive_info.passThroughHacks.someHacksSetByOSDiscovery                  = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareCommit =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareDownload =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getFeatures = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage =
                                true; // This has seen some changes as Win10 API has advanced. May need more specific
                                      // flags!
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                .identifyController = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyNamespace =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.vendorUnique =
                                true;
#    if defined(ENABLE_TRANSLATE_FORMAT)
                            if (is_Windows_10_Version_1607_Or_Higher())
                            {
                                if ((device->os_info.fileSystemInfo.fileSystemInfoValid &&
                                     !device->os_info.fileSystemInfo.isSystemDisk) ||
                                    is_Windows_PE())
                                {
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                        .formatCryptoSecureErase = true;
                                }
                            }
#    endif // ENABLE_TRANSLATE_FORMAT
                            if (is_Windows_10_Version_1903_Or_Higher())
                            {
#    if defined(WIN_DEBUG)
                                printf("WIN: 1903+\n");
#    endif // WIN_DEBUG
           // this is definitely blocked in 1809, so this seems to have started being available in 1903
           // NOTE: probably specific to a certain Win10 update. Not clearly documented when this became available, so
           // need to do some testing before this is perfect
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .deviceSelfTest = true;
#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
                                if ((device->os_info.fileSystemInfo.fileSystemInfoValid &&
                                     !device->os_info.fileSystemInfo.isSystemDisk))
                                {
                                    // Which is possible depends on the drive's capabilities. The
                                    // IOCTL_STORAGE_REINITIALIZE_MEDIA changes behanvrio based on OS version and drive
                                    // capabilities.
#        if defined(ENABLE_TRANSLATE_FORMAT)
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                        .formatCryptoSecureErase = true;
#        endif // ENABLE_TRANSLATE_FORMAT
               // NOTE: Cannot do format with user erase because that is not what MSFT translates for the Sanitize block
               // erase...it just does sanitize block erase.
               //       No idea when this was implemented because the documentation on this translation is non-existent
               //       right now.
               // NOTE: It is not clear if sanitize crypto was switched to starting in 1903 or if it was an earlier
               // version. The documentation only goes back to 1903 online.-TJE
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                        .sanitizeCrypto = true;
                                }
#    endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
                            }
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.securityReceive =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.securitySend =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.setFeatures =
                                true; // Only 1 feature today. <--this is old. There is now a set features API, but I
                                      // don't see what it does or does not allow. Still needs implementing. - TJE
#    if defined(WIN_DEBUG)
                            printf("WIN: Checking for Win PE\n");
#    endif // WIN_DEBUG
                            if (is_Windows_PE())
                            {
#    if defined(WIN_DEBUG)
                                printf("WIN: PE environment found\n");
#    endif // WIN_DEBUG
           // If in Windows PE, then these other commands become available
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .namespaceAttachment = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .namespaceManagement                                                        = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.format = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.miReceive =
                                    true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.miSend = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.sanitize =
                                    true;
                                // adding more detail since the previous bool was a little too generic and this will
                                // improve overall accuracy of supported commands-TJE
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .sanitizeCrypto = true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.sanitizeBlock =
                                    true;
                                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                    .sanitizeOverwrite = true;
                            }
                            if (is_Windows_10_Version_21H1_Or_Higher())
                            {
#    if defined(WIN_DEBUG)
                                printf("WIN: 21H1+\n");
#    endif // WIN_DEBUG
#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_20348
                                if ((device->os_info.fileSystemInfo.fileSystemInfoValid &&
                                     !device->os_info.fileSystemInfo.isSystemDisk))
                                {
                                    // Microsoft's documentation was wrong or just worded weirdly that it seemeed like
                                    // it should work in Win11 just like PE, but that is not the case! The only way that
                                    // seems to actually work is PE as always or the reinitialize media IOCTL which is
                                    // slightly more limited in capabilities.
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                        .sanitizeCrypto = true;
                                    device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                        .sanitizeBlock = true;
                                }
#    endif // #if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_20348
                            }
                        }
                        else
#endif // WINVER >= SEA_WIN32_WINNT_WIN10
                        {
#if defined(WIN_DEBUG)
                            printf("WIN: earlier Windows. Treating as SCSI\n");
#endif // WIN_DEBUG
                            device->drive_info.drive_type     = SCSI_DRIVE;
                            device->drive_info.interface_type = SCSI_INTERFACE;
                            device->os_info.ioType            = WIN_IOCTL_SCSI_PASSTHROUGH;
                        }
                    }
                }
                else // treat anything else as a SCSI device.
                {
                    device->drive_info.interface_type = SCSI_INTERFACE;
                    // This does NOT mean that drive_type is SCSI, but set SCSI drive for now
                    device->drive_info.drive_type = SCSI_DRIVE;
                    device->os_info.ioType        = WIN_IOCTL_SCSI_PASSTHROUGH;
                    if (device_desc->BusType == BusTypeSas)
                    {
                        // CSMI checks are needed for controllers showing as SAS or RAID.
                        // RAID must also be considered due to how drivers, especially Intel's shows devices, both RAIDs
                        // and individual devices behind controllers in RAID mode.
                        checkForCSMI = true;
                    }
                    else if (device_desc->BusType == BusTypeRAID)
                    {
#if defined(WIN_DEBUG)
                        printf("WIN: RAID bus type. Need to check for additional NVMe/CSMI support\n");
#endif // WIN_DEBUG
       // TODO: Need to figure out a better way to decide this.
       //       Unfortunately, the Intel RST driver will show NVMe drives as RAID, but no vendor ID, so we need to check
       //       them all for this until we can find something else to use. This means issuing an admin identify which
       //       should fail gracefully on drivers that don't actually support this since it's vendor unique IOCTL code
       //       and signature.
                        checkForNVMe = true;
                        checkForCSMI = true;
                    }
                }

                // Doing this here because the NSID may be needed for NVMe over USB interfaces too
                device->drive_info.namespaceID = device->os_info.scsi_addr.Lun + 1;

                if (device->drive_info.interface_type == USB_INTERFACE ||
                    device->drive_info.interface_type == IEEE_1394_INTERFACE)
                {
                    setup_Passthrough_Hacks_By_ID(device);
                }

                // For now force direct IO all the time to match previous functionality.
                // Investigate how to decide using double buffered vs direct vs mixed.
                // Note: On a couple systems here, when using double buffered IO with ATA Pass-through, invalid
                // checksums are returned for identify commands, but direct is fine... Also observed that certain
                // command data is cached in double buffered mode, but not direct mode, which is an issue for our
                // utilities This is the other reason to force direct, even though the code otherwise supports double
                // buffered IO
                device->os_info.ioMethod = WIN_IOCTL_FORCE_ALWAYS_DIRECT;

                if (device->dFlags &
                    OPEN_HANDLE_ONLY) // This is this far down because there is a lot of other things that need to be
                                      // saved in order for windows pass-through to work correctly.
                {
                    return SUCCESS;
                }

                if (checkForNVMe)
                {
                    DECLARE_ZERO_INIT_ARRAY(uint8_t, nvmeIdent, 4096);
#if defined(WIN_DEBUG)
                    printf("WIN: Additional check for Intel NVMe\n");
#endif // WIN_DEBUG
                    if (device->os_info.scsiSRBHandle != INVALID_HANDLE_VALUE ||
                        SUCCESS == open_SCSI_SRB_Handle(device))
                    {
                        // Check for Intel NVMe passthrough
                        device->drive_info.drive_type                 = NVME_DRIVE;
                        device->drive_info.interface_type             = NVME_INTERFACE;
                        device->os_info.intelNVMePassthroughSupported = true;
                        if (SUCCESS == nvme_Identify(device, nvmeIdent, 0, 1))
                        {
                            // use OS read/write calls since this driver may not allow these to work since it is limited
                            // in capabilities for passthrough.
                            device->os_info.osReadWriteRecommended = true;
                            checkForCSMI                           = false;
#if defined(WIN_DEBUG)
                            printf("WIN: Intel NVMe support found\n");
#endif // WIN_DEBUG
       // TODO: This passthrough may be limited in commands allowed to be sent. If this is limited, need to fill in the
       // nvme hacks to show what is or is not supported.
                            device->drive_info.passThroughHacks.someHacksSetByOSDiscovery                  = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported
                                .identifyController = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyNamespace =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage  = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getFeatures = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.setFeatures = true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareCommit =
                                true; // NOTE: Always requires a reboot for activation even when error code does not
                                      // indicate this
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareDownload =
                                true;
                            // supported features: power management, temperature threshold, APST, HCTM (17.2+), vendor
                            // specific.
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.vendorUnique =
                                true;
                            device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.deviceSelfTest =
                                true; // not documented, but found with trial and error.
                            // NOTE: some of this comes from an intel document and some comes from trial and error.
                            //       It is extremely likely that the supported commands vary by driver version, but we
                            //       do not have that level of detail to populate this list at this time-TJE
                        }
                        else
                        {
#if defined(WIN_DEBUG)
                            printf("WIN: Does not support Intel NVMe passthrough\n");
#endif // WIN_DEBUG
                            device->drive_info.drive_type                 = SCSI_DRIVE;
                            device->drive_info.interface_type             = SCSI_INTERFACE;
                            device->os_info.intelNVMePassthroughSupported = false;
                        }
                    }
                }

                // Lets fill out rest of info
#if defined(WIN_DEBUG)
                printf("WIN: filling device information\n");
#endif // WIN_DEBUG
                ret = fill_Drive_Info_Data(device);

                /*
                While in most newer systems we found out that _force_ SCSI PassThrough will work,
                using older version of WinPE will cause the SCSI IOCTL to fail - MA
                */
                if ((ret != SUCCESS) && (device->drive_info.interface_type == IDE_INTERFACE))
                {
#if defined(WIN_DEBUG)
                    printf("WIN: Working around legacy passthrough issues\n");
#endif // WIN_DEBUG
       // we weren't successful getting device information...so now try switching to the other IOCTLs
       // NOLINTBEGIN(bugprone-branch-clone)
                    if (device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH ||
                        device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH_EX)
                    {
                        device->os_info.ioType = WIN_IOCTL_ATA_PASSTHROUGH;
                    }
                    else if (device->os_info.ioType ==
                             WIN_IOCTL_ATA_PASSTHROUGH) // ATA pass-through didn't work...so try SCSI pass-through just
                                                        // in case before falling back to legacy
                    {
                        device->os_info.ioType = WIN_IOCTL_SCSI_PASSTHROUGH;
                    }
                    // NOLINTEND(bugprone-branch-clone)
                    ret = fill_Drive_Info_Data(device);
                    if (ret != SUCCESS)
                    {
                        // if we are here, then we are likely dealing with an old legacy driver that doesn't support
                        // these other IOs we've been trying...so fall back to some good old legacy stuff that may still
                        // not work. - TJE
                        bool idePassThroughSupported = false;
                        // test an identify command with IDE pass-through
                        device->os_info.ioType = WIN_IOCTL_IDE_PASSTHROUGH_ONLY;
                        // Try an identify command. Should we use check power mode command instead???
                        if (SUCCESS == ata_Identify(device,
                                                    C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000),
                                                    sizeof(tAtaIdentifyData)) ||
                            SUCCESS == ata_Identify_Packet_Device(
                                           device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000),
                                           sizeof(tAtaIdentifyData)))
                        {
                            idePassThroughSupported = true;
                        }
                        // NOLINTBEGIN(bugprone-branch-clone)
                        if (device->os_info.winSMARTCmdSupport.smartIOSupported && idePassThroughSupported)
                        {
                            device->os_info.ioType = WIN_IOCTL_SMART_AND_IDE;
                        }
                        else if (device->os_info.winSMARTCmdSupport.smartIOSupported && !idePassThroughSupported)
                        {
                            device->os_info.ioType = WIN_IOCTL_SMART_ONLY;
                        }
                        // NOLINTEND(bugprone-branch-clone)
                        device->os_info.osReadWriteRecommended = true;
                        ret                                    = fill_Drive_Info_Data(device);
                        checkForCSMI = false; // if we are using any of these limited, old IOCTLs, it is extremely
                                              // unlikely that CSMI will work at all.
                    }
                }

                if (checkForCSMI)
                {
#if defined(WIN_DEBUG)
                    printf("WIN: Additional CSMI check\n");
#endif // WIN_DEBUG
                    if (device->os_info.scsiSRBHandle != INVALID_HANDLE_VALUE ||
                        SUCCESS == open_SCSI_SRB_Handle(device))
                    {
#if defined(WIN_DEBUG)
                        printf("WIN: Looking for CSMI IO support\n");
#endif // WIN_DEBUG
                        if (handle_Supports_CSMI_IO(device->os_info.scsiSRBHandle, device->deviceVerbosity))
                        {
#if defined(WIN_DEBUG)
                            printf("WIN: Setting up CSMI capabilities\n");
#endif // WIN_DEBUG
       // open up the CSMI handle and populate the pointer to the csmidata structure. This may allow us to work around
       // other commands.
                            if (SUCCESS == jbod_Setup_CSMI_Info(
                                               device->os_info.scsiSRBHandle, device, 0,
                                               device->os_info.scsi_addr.PortNumber, device->os_info.scsi_addr.PathId,
                                               device->os_info.scsi_addr.TargetId, device->os_info.scsi_addr.Lun))
                            {
                                // Set flags, or other info?
                            }
                        }
                    }
                }

                // Fill in IDE for ATA interface so we can know based on scan output which passthrough may need
                // debugging
                if (device->drive_info.interface_type == IDE_INTERFACE)
                {
                    safe_memset(device->drive_info.T10_vendor_ident, sizeof(device->drive_info.T10_vendor_ident), 0,
                                sizeof(device->drive_info.T10_vendor_ident));
                    // Setting the vendor ID for ATA controllers like this so we can have an idea when we detect what we
                    // think is IDE and what we think is SATA. This may be helpful for debugging later. - TJE
                    if (adapter_desc->BusType == BusTypeSata)
                    {
                        snprintf_err_handle(device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1, "%s", "SATA");
                    }
                    else
                    {
                        snprintf_err_handle(device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1, "%s", "IDE");
                    }
                }
                // now windows api gives us some extra details that we should check to make sure that our
                // fill_Drive_Info_Data call did correctly...just for some things the generic code may miss
                switch (device_desc->BusType)
                {
                case BusTypeAtapi:
                    device->drive_info.drive_type = ATAPI_DRIVE;
                    device->drive_info.media_type = MEDIA_OPTICAL;
                    break;
                case BusTypeSd:
                    device->drive_info.drive_type     = FLASH_DRIVE;
                    device->drive_info.media_type     = MEDIA_SSM_FLASH;
                    device->drive_info.interface_type = SD_INTERFACE;
                    break;
                case BusTypeMmc:
                    device->drive_info.drive_type     = FLASH_DRIVE;
                    device->drive_info.media_type     = MEDIA_SSM_FLASH;
                    device->drive_info.interface_type = MMC_INTERFACE;
                    break;
                default:
                    // do nothing since we assume everything else was set correctly earlier
                    break;
                }
                safe_free_device_descriptor(&device_desc);
            }
            safe_free_adapter_descriptor(&adapter_desc);
        }
    }
    // Just in case we bailed out in any way.
    device->os_info.last_error = GetLastError();

    // printf("%s <--\n",__FUNCTION__);
    return ret; // if we didn't get to fill_In_Device_Info FAILURE
}
eReturnValues get_Device(const char* filename, tDevice* device)
{
#if defined(ENABLE_CSMI)
    // check is the handle is in the format of a CSMI device handle so we can open the csmi device properly.
    if (is_CSMI_Handle(filename))
    {
        return get_CSMI_RAID_Device(filename, device);
    }
    else
#endif
    {
        return get_Win_Device(filename, device);
    }
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
eReturnValues get_Device_Count(uint32_t* numberOfDevices, uint64_t flags)
{
    HANDLE fd = M_NULLPTR;
    DECLARE_ZERO_INIT_ARRAY(TCHAR, deviceName, WIN_MAX_DEVICE_NAME_LENGTH);
    ptrRaidHandleToScan raidHandleList      = M_NULLPTR;
    ptrRaidHandleToScan beginRaidHandleList = raidHandleList;
    // Configuration manager library is not available on ARM for Windows. Library didn't exist when I went looking for
    // it - TJE ARM requires 10.0.16299.0 API to get cfgmgr32 library! try forcing a system rescan before opening the
    // list. This should help with crappy drivers or bad hotplug support - TJE
    eVerbosityLevels winCountVerbosity = VERBOSITY_DEFAULT;
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES)
    {
        winCountVerbosity = VERBOSITY_COMMAND_NAMES;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE)
    {
        winCountVerbosity = VERBOSITY_COMMAND_VERBOSE;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_BUFFERS)
    {
        winCountVerbosity = VERBOSITY_BUFFERS;
    }

    if (flags & BUS_RESCAN_ALLOWED)
    {
        DEVINST   deviceInstance;
        DEVINSTID tree            = M_NULLPTR;  // set to null for root of device tree
        ULONG     locateNodeFlags = ULONG_C(0); // add flags here if we end up needing them
        if (VERBOSITY_COMMAND_NAMES <= winCountVerbosity)
        {
            printf("Running CM_Locate_Devnode on root to force system wide rescan\n");
        }
        if (CR_SUCCESS == CM_Locate_DevNode(&deviceInstance, tree, locateNodeFlags))
        {
            ULONG reenumerateFlags = ULONG_C(0);
            CM_Reenumerate_DevNode(deviceInstance, reenumerateFlags);
        }
    }

    uint32_t driveNumber = UINT32_C(0);

    uint32_t found = UINT32_C(0);
    for (driveNumber = 0; driveNumber < MAX_DEVICES_TO_SCAN; ++driveNumber)
    {
        _stprintf_s(deviceName, WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%s%u"), TEXT(WIN_PHYSICAL_DRIVE), driveNumber);
        // lets try to open the device.
        fd = CreateFile(deviceName,
                        GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                        FILE_FLAG_OVERLAPPED,
#else
                        0,
#endif
                        M_NULLPTR);
        if (fd != INVALID_HANDLE_VALUE)
        {
            ++found;
            // Check if the interface is reported as RAID from adapter data because an additional scan for RAID devices
            // will be needed.
            PSTORAGE_ADAPTER_DESCRIPTOR adapterData = M_NULLPTR;
            if (SUCCESS == win_Get_Adapter_Descriptor(fd, &adapterData))
            {
                if (adapterData->BusType == BusTypeRAID)
                {
                    if (VERBOSITY_COMMAND_NAMES <= winCountVerbosity)
                    {
                        _tprintf_s(TEXT("Detected RAID adapter: %s\n"), deviceName);
                    }
                    // get the SCSI address for this device and save it to the RAID handle list so it can be scanned for
                    // additional types of RAID interfaces.
                    SCSI_ADDRESS scsiAddress;
                    safe_memset(&scsiAddress, sizeof(SCSI_ADDRESS), 0, sizeof(SCSI_ADDRESS));
                    if (SUCCESS == win_Get_SCSI_Address(fd, &scsiAddress))
                    {
                        DECLARE_ZERO_INIT_ARRAY(char, raidHandle, RAID_HANDLE_STRING_MAX_LEN);
                        raidTypeHint raidHint;
                        safe_memset(&raidHint, sizeof(raidTypeHint), 0, sizeof(raidTypeHint));
                        raidHint.unknownRAID = true; // TODO: Look up driver name to set hint instead of unknown to
                                                     // prevent excess IOCTLs being sent from retries.
                        snprintf_err_handle(raidHandle, RAID_HANDLE_STRING_MAX_LEN, "\\\\.\\SCSI%" PRIu8 ":",
                                            scsiAddress.PortNumber);
                        if (VERBOSITY_COMMAND_NAMES <= winCountVerbosity)
                        {
                            printf("Adding SCSI port handle to RAID list to check for compatible devices: %s\n",
                                   raidHandle);
                        }
                        raidHandleList =
                            add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, raidHandle, raidHint);
                        if (!beginRaidHandleList)
                        {
                            beginRaidHandleList = raidHandleList;
                        }
                    }
                }
            }
            safe_free_adapter_descriptor(&adapterData);
            CloseHandle(fd);
        }
    }

    *numberOfDevices = found;

#if defined(ENABLE_CSMI)
    if (!(flags & GET_DEVICE_FUNCS_IGNORE_CSMI)) // check whether they want CSMI devices or not
    {
        uint32_t      csmiDeviceCount = UINT32_C(0);
        eReturnValues csmiRet         = get_CSMI_RAID_Device_Count(&csmiDeviceCount, flags, &beginRaidHandleList);
        if (csmiRet == SUCCESS)
        {
            *numberOfDevices += csmiDeviceCount;
        }
    }
    else if (VERBOSITY_COMMAND_NAMES <= winCountVerbosity)
    {
        printf("CSMI Raid scan was skipped due to flag\n");
    }
#endif

    // Clean up RAID handle list
    delete_RAID_List(beginRaidHandleList);

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
eReturnValues get_Device_List(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(TCHAR, deviceName, WIN_MAX_DEVICE_NAME_LENGTH);
    DECLARE_ZERO_INIT_ARRAY(char, name, WIN_MAX_DEVICE_NAME_LENGTH); // Because get device needs char
    HANDLE              fd                  = INVALID_HANDLE_VALUE;
    tDevice*            d                   = M_NULLPTR;
    ptrRaidHandleToScan raidHandleList      = M_NULLPTR;
    ptrRaidHandleToScan beginRaidHandleList = raidHandleList;
    eVerbosityLevels    winListVerbosity    = VERBOSITY_DEFAULT;
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES)
    {
        winListVerbosity = VERBOSITY_COMMAND_NAMES;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE)
    {
        winListVerbosity = VERBOSITY_COMMAND_VERBOSE;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_BUFFERS)
    {
        winListVerbosity = VERBOSITY_BUFFERS;
    }
    DISABLE_NONNULL_COMPARE
    if (ptrToDeviceList == M_NULLPTR || sizeInBytes == 0)
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
        d               = ptrToDeviceList;
        for (driveNumber = 0; ((driveNumber < MAX_DEVICES_TO_SCAN) && (found < numberOfDevices)); driveNumber++)
        {
            _stprintf_s(deviceName, WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%s%d"), TEXT(WIN_PHYSICAL_DRIVE), driveNumber);
            // lets try to open the device.
            fd = CreateFile(deviceName,
                            GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                            FILE_FLAG_OVERLAPPED,
#else
                            0,
#endif
                            M_NULLPTR);
            if (fd != INVALID_HANDLE_VALUE)
            {
                CloseHandle(fd);
                snprintf_err_handle(name, WIN_MAX_DEVICE_NAME_LENGTH, "%s%d", WIN_PHYSICAL_DRIVE, driveNumber);
                eVerbosityLevels temp = d->deviceVerbosity;
                safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size     = ver.size;
                d->sanity.version  = ver.version;
                d->dFlags          = flags;
                returnValue        = get_Device(name, d);
                if (returnValue != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                else
                {
                    PSTORAGE_ADAPTER_DESCRIPTOR adapterData = M_NULLPTR;
                    if (SUCCESS == win_Get_Adapter_Descriptor(d->os_info.fd, &adapterData))
                    {
                        if (adapterData->BusType == BusTypeRAID)
                        {
                            // get the SCSI address for this device and save it to the RAID handle list so it can be
                            // scanned for additional types of RAID interfaces.
                            SCSI_ADDRESS scsiAddress;
                            safe_memset(&scsiAddress, sizeof(SCSI_ADDRESS), 0, sizeof(SCSI_ADDRESS));
                            if (VERBOSITY_COMMAND_NAMES <= winListVerbosity)
                            {
                                printf("Detected RAID adapter for %s\n", name);
                            }
                            if (SUCCESS == win_Get_SCSI_Address(d->os_info.fd, &scsiAddress))
                            {
                                DECLARE_ZERO_INIT_ARRAY(char, raidHandle, RAID_HANDLE_STRING_MAX_LEN);
                                raidTypeHint raidHint;
                                safe_memset(&raidHint, sizeof(raidTypeHint), 0, sizeof(raidTypeHint));
                                raidHint.unknownRAID = true; // TODO: Look up driver name to set hint instead of unknown
                                                             // to prevent excess IOCTLs being sent from retries.
                                snprintf_err_handle(raidHandle, RAID_HANDLE_STRING_MAX_LEN, "\\\\.\\SCSI%" PRIu8 ":",
                                                    scsiAddress.PortNumber);
                                if (VERBOSITY_COMMAND_NAMES <= winListVerbosity)
                                {
                                    printf("Adding %s to RAID handle list to scan for compatible devices\n",
                                           raidHandle);
                                }
                                raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList,
                                                                                raidHandle, raidHint);
                                if (!beginRaidHandleList)
                                {
                                    beginRaidHandleList = raidHandleList;
                                }
                            }
                        }
                    }
                    safe_free_adapter_descriptor(&adapterData);
                }
                found++;
                d++;
            }
            else
            {
                // Check last error for permissions issues
                DWORD lastError = GetLastError();
                if (lastError == ERROR_ACCESS_DENIED)
                {
                    ++permissionDeniedCount;
                    ++failedGetDeviceCount;
                }
                // NOTE: No generic else like other OS's due to the way devices are scanned in Windows today. Since we
                // are just trying to open handles, they can fail for various reasons, like the handle not even being
                // valid, but that should not cause a failure. If the code is updated to use something like setupapi or
                // cfgmgr32 to figure out devices in the system, then it would make sense to add additional error checks
                // here like we have for 'nix OSs. - TJE If a handle does not exist ERROR_FILE_NOT_FOUND is returned.
                if (VERBOSITY_COMMAND_NAMES <= d->deviceVerbosity)
                {
                    _tprintf_s(TEXT("Error: opening dev %s. "), deviceName);
                    print_Windows_Error_To_Screen(lastError);
                    _tprintf_s(TEXT("\n"));
                }
            }
        }

#if defined(ENABLE_CSMI)
        if (!(flags & GET_DEVICE_FUNCS_IGNORE_CSMI))
        {
            uint32_t csmiDeviceCount = numberOfDevices - found;
            if (csmiDeviceCount > 0)
            {
                eReturnValues csmiRet = get_CSMI_RAID_Device_List(
                    &ptrToDeviceList[found], csmiDeviceCount * sizeof(tDevice), ver, flags, &beginRaidHandleList);
                if (returnValue == SUCCESS && csmiRet != SUCCESS)
                {
                    // this will override the normal ret if it is already set to success with the CSMI return value
                    returnValue = csmiRet;
                }
            }
        }
        else if (VERBOSITY_COMMAND_NAMES <= winListVerbosity)
        {
            printf("CSMI Scan skipped due to flag\n");
        }
#endif
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (permissionDeniedCount == numberOfDevices)
        {
            returnValue = PERMISSION_DENIED;
        }
        else if (failedGetDeviceCount)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
        // Clean up RAID handle list
        delete_RAID_List(beginRaidHandleList);
    }
    if (VERBOSITY_COMMAND_NAMES <= winListVerbosity)
    {
        printf("Win get device list returning %d\n", returnValue);
    }
    RESTORE_NONNULL_COMPARE
    return returnValue;
}

#if defined INCLUDED_SCSI_DOT_H
typedef struct s_scsiPassThroughEXIOStruct
{
    union
    {
        SCSI_PASS_THROUGH_DIRECT_EX scsiPassThroughEXDirect;
        SCSI_PASS_THROUGH_EX        scsiPassThroughEX;
    };
    UCHAR cdbPad[CDB_LEN_32 - 1]; // Padding for max sized CDB that the above struct can make...this needs to be here
                                  // because of the ANYSIZE_ARRAY trick they use in the structure. If CDBs larger than
                                  // 32B are ever made, this will need adjusting - TJE
    ULONG          padding;       // trying to help buffer alignment like the MS example shows.
    STOR_ADDR_BTL8 storeAddr;
    UCHAR senseBuffer[SPC3_SENSE_LEN]; // If we do auto-sense, we need to allocate 252 bytes, according to SPC-3.
    UCHAR
    dataInBuffer[DOUBLE_BUFFERED_MAX_TRANSFER_SIZE]; // Setting to this defined value to help prevent problems...TJE
    UCHAR dataOutBuffer[DOUBLE_BUFFERED_MAX_TRANSFER_SIZE]; // Setting to this defined value to help prevent
                                                            // problems...TJE
} scsiPassThroughEXIOStruct, *ptrSCSIPassThroughEXIOStruct;

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
static eReturnValues convert_SCSI_CTX_To_SCSI_Pass_Through_EX(ScsiIoCtx* scsiIoCtx, ptrSCSIPassThroughEXIOStruct psptd)
{
    eReturnValues ret = SUCCESS;
    safe_memset(&psptd->scsiPassThroughEX, sizeof(SCSI_PASS_THROUGH_EX), 0, sizeof(SCSI_PASS_THROUGH_EX));
    psptd->scsiPassThroughEX.Version           = 0; // MSDN says set this to zero
    psptd->scsiPassThroughEX.Length            = sizeof(SCSI_PASS_THROUGH_EX);
    psptd->scsiPassThroughEX.CdbLength         = scsiIoCtx->cdbLength;
    psptd->scsiPassThroughEX.StorAddressLength = sizeof(STOR_ADDR_BTL8);
    psptd->scsiPassThroughEX.ScsiStatus        = 0;
    psptd->scsiPassThroughEX.SenseInfoLength   = SPC3_SENSE_LEN;
    psptd->scsiPassThroughEX.Reserved          = 0;
    // setup the store port address struct
    psptd->storeAddr.Type          = STOR_ADDRESS_TYPE_BTL8; // Microsoft documentation says to set this
    psptd->storeAddr.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    // The host bus adapter (HBA) port number.
    psptd->storeAddr.Port =
        scsiIoCtx->device->os_info.scsi_addr.PortNumber; // This may or maynot be correct. Need to test it.
    psptd->storeAddr.Path                      = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->storeAddr.Target                    = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->storeAddr.Lun                       = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->storeAddr.Reserved                  = 0;
    psptd->scsiPassThroughEX.StorAddressOffset = offsetof(scsiPassThroughEXIOStruct, storeAddr);
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_OUT:
        psptd->scsiPassThroughEX.DataDirection         = SCSI_IOCTL_DATA_OUT;
        psptd->scsiPassThroughEX.DataOutTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEX.DataOutBufferOffset   = offsetof(scsiPassThroughEXIOStruct, dataOutBuffer);
        psptd->scsiPassThroughEX.DataInBufferOffset    = 0;
        break;
    case XFER_DATA_IN:
        psptd->scsiPassThroughEX.DataDirection        = SCSI_IOCTL_DATA_IN;
        psptd->scsiPassThroughEX.DataInTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEX.DataInBufferOffset   = offsetof(scsiPassThroughEXIOStruct, dataInBuffer);
        psptd->scsiPassThroughEX.DataOutBufferOffset  = 0;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassThroughEX.DataDirection = SCSI_IOCTL_DATA_UNSPECIFIED;
        break;
        // add in the next case later when we have better defined a difference between in and out data buffers...-TJE
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
    if (scsiIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
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
    safe_memcpy(psptd->scsiPassThroughEX.Cdb, CDB_LEN_32, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    return ret;
}

static M_INLINE void safe_free_SCSIPassthroughEx(ptrSCSIPassThroughEXIOStruct* scsipt)
{
    safe_free_core(M_REINTERPRET_CAST(void**, scsipt));
}

static eReturnValues send_SCSI_Pass_Through_EX(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues                ret           = FAILURE;
    BOOL                         success       = FALSE;
    ULONG                        returned_data = ULONG_C(0);
    ptrSCSIPassThroughEXIOStruct sptdioEx =
        M_REINTERPRET_CAST(ptrSCSIPassThroughEXIOStruct, safe_malloc(sizeof(scsiPassThroughEXIOStruct)));
    if (!sptdioEx)
    {
        return MEMORY_FAILURE;
    }
    DECLARE_SEATIMER(commandTimer);
    safe_memset(sptdioEx, sizeof(scsiPassThroughEXIOStruct), 0, sizeof(scsiPassThroughEXIOStruct));
    ret = convert_SCSI_CTX_To_SCSI_Pass_Through_EX(scsiIoCtx, sptdioEx);
    if (SUCCESS == ret)
    {
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD sptBufInLen                     = sizeof(scsiPassThroughEXIOStruct);
        DWORD sptBufOutLen                    = sizeof(scsiPassThroughEXIOStruct);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            sptBufOutLen += scsiIoCtx->dataLength;
            break;
        case XFER_DATA_OUT:
            // need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(sptdioEx->dataOutBuffer, DOUBLE_BUFFERED_MAX_TRANSFER_SIZE, scsiIoCtx->pdata,
                            scsiIoCtx->dataLength);
            }
            sptBufInLen += scsiIoCtx->dataLength;
            break;
        default:
            break;
        }
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success =
            DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_SCSI_PASS_THROUGH_EX, &sptdioEx->scsiPassThroughEX,
                            sptBufInLen, &sptdioEx->scsiPassThroughEX, sptBufOutLen, &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent          = M_NULLPTR;
        scsiIoCtx->returnStatus.senseKey = sptdioEx->scsiPassThroughEX.ScsiStatus;

        if (MSFT_BOOL_TRUE(success))
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; // setting to zero to be compatible with linux
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, sptdioEx->dataInBuffer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }

        // Any sense data?
        if (scsiIoCtx->psense != M_NULLPTR && scsiIoCtx->senseDataSize > 0)
        {
            safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, &sptdioEx->senseBuffer[0],
                        M_Min(sptdioEx->scsiPassThroughEX.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != M_NULLPTR)
        {
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[3];
                break;
            }
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_free_SCSIPassthroughEx(&sptdioEx);
    return ret;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
static eReturnValues convert_SCSI_CTX_To_SCSI_Pass_Through_EX_Direct(ScsiIoCtx*                   scsiIoCtx,
                                                                     ptrSCSIPassThroughEXIOStruct psptd,
                                                                     uint8_t*                     alignedPointer)
{
    eReturnValues ret = SUCCESS;
    safe_memset(&psptd->scsiPassThroughEXDirect, sizeof(SCSI_PASS_THROUGH_DIRECT_EX), 0,
                sizeof(SCSI_PASS_THROUGH_DIRECT_EX));
    psptd->scsiPassThroughEXDirect.Version           = 0; // MSDN says set this to zero
    psptd->scsiPassThroughEXDirect.Length            = sizeof(SCSI_PASS_THROUGH_DIRECT_EX);
    psptd->scsiPassThroughEXDirect.CdbLength         = scsiIoCtx->cdbLength;
    psptd->scsiPassThroughEXDirect.StorAddressLength = sizeof(STOR_ADDR_BTL8);
    psptd->scsiPassThroughEXDirect.ScsiStatus        = 0;
    psptd->scsiPassThroughEXDirect.SenseInfoLength   = SPC3_SENSE_LEN;
    psptd->scsiPassThroughEXDirect.Reserved          = 0;
    // setup the store port address struct
    psptd->storeAddr.Type          = STOR_ADDRESS_TYPE_BTL8; // Microsoft documentation says to set this
    psptd->storeAddr.AddressLength = STOR_ADDR_BTL8_ADDRESS_LENGTH;
    // The host bus adapter (HBA) port number.
    psptd->storeAddr.Port =
        scsiIoCtx->device->os_info.scsi_addr.PortNumber; // This may or maynot be correct. Need to test it.
    psptd->storeAddr.Path                            = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->storeAddr.Target                          = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->storeAddr.Lun                             = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->storeAddr.Reserved                        = 0;
    psptd->scsiPassThroughEXDirect.StorAddressOffset = offsetof(scsiPassThroughEXIOStruct, storeAddr);
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_OUT:
        psptd->scsiPassThroughEXDirect.DataDirection         = SCSI_IOCTL_DATA_OUT;
        psptd->scsiPassThroughEXDirect.DataOutTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEXDirect.DataOutBuffer         = alignedPointer;
        psptd->scsiPassThroughEXDirect.DataInBuffer          = M_NULLPTR;
        break;
    case XFER_DATA_IN:
        psptd->scsiPassThroughEXDirect.DataDirection        = SCSI_IOCTL_DATA_IN;
        psptd->scsiPassThroughEXDirect.DataInTransferLength = scsiIoCtx->dataLength;
        psptd->scsiPassThroughEXDirect.DataInBuffer         = alignedPointer;
        psptd->scsiPassThroughEXDirect.DataOutBuffer        = M_NULLPTR;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassThroughEXDirect.DataDirection = SCSI_IOCTL_DATA_UNSPECIFIED;
        break;
        // add in the next case later when we have better defined a difference between in and out data buffers...-TJE
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
    if (scsiIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
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
    safe_memcpy(psptd->scsiPassThroughEXDirect.Cdb, CDB_LEN_32, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    return ret;
}

static eReturnValues send_SCSI_Pass_Through_EX_Direct(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret           = FAILURE;
    BOOL          success       = FALSE;
    ULONG         returned_data = ULONG_C(0);
    // size_t scsiPTIoStructSize = sizeof(scsiPassThroughEXIOStruct);
    ptrSCSIPassThroughEXIOStruct sptdio =
        C_CAST(ptrSCSIPassThroughEXIOStruct,
               safe_malloc(sizeof(
                   scsiPassThroughEXIOStruct))); // add cdb and data length so that the memory allocated correctly!
    if (!sptdio)
    {
        return MEMORY_FAILURE;
    }
    DECLARE_SEATIMER(commandTimer);
    safe_memset(sptdio, sizeof(scsiPassThroughEXIOStruct), 0, sizeof(scsiPassThroughEXIOStruct));
    bool     localAlignedBuffer = false;
    uint8_t* alignedPointer     = scsiIoCtx->pdata;
    uint8_t* localBuffer        = M_NULLPTR; // we need to save this to free up the memory properly later.
    // Check the alignment...if we need to use a local buffer, we'll use one, then copy the data back
    if (scsiIoCtx->pdata && scsiIoCtx->device->os_info.alignmentMask != 0)
    {
        // This means the driver requires some sort of aligned pointer for the data buffer...so let's check and make
        // sure that the user's pointer is aligned If the user's pointer isn't aligned properly, align something local
        // that is aligned to meet the driver's requirements, then copy data back for them.
        alignedPointer = C_CAST(uint8_t*, (C_CAST(UINT_PTR, scsiIoCtx->pdata) +
                                           C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask)) &
                                              ~C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask));
        if (alignedPointer != scsiIoCtx->pdata)
        {
            localAlignedBuffer       = true;
            uint32_t totalBufferSize = scsiIoCtx->dataLength + scsiIoCtx->device->os_info.alignmentMask;
            localBuffer              = M_REINTERPRET_CAST(uint8_t*, safe_calloc(totalBufferSize, sizeof(uint8_t)));
            if (!localBuffer)
            {
                perror(
                    "error allocating aligned buffer for ATA Passthrough Direct...attempting to use user's pointer.");
                localAlignedBuffer = false;
                alignedPointer     = scsiIoCtx->pdata;
            }
            else
            {
                alignedPointer = C_CAST(uint8_t*, (C_CAST(UINT_PTR, localBuffer) +
                                                   C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask)) &
                                                      ~C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask));
                if (scsiIoCtx->direction == XFER_DATA_OUT)
                {
                    safe_memcpy(alignedPointer,
                                totalBufferSize - (C_CAST(uintptr_t, alignedPointer) - C_CAST(uintptr_t, localBuffer)),
                                scsiIoCtx->pdata, scsiIoCtx->dataLength);
                }
            }
        }
    }
    ret = convert_SCSI_CTX_To_SCSI_Pass_Through_EX_Direct(scsiIoCtx, sptdio, alignedPointer);
    if (SUCCESS == ret)
    {
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD      sptBufLen                  = sizeof(scsiPassThroughEXIOStruct);
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_SCSI_PASS_THROUGH_DIRECT_EX,
                                  &sptdio->scsiPassThroughEXDirect, sptBufLen, &sptdio->scsiPassThroughEXDirect,
                                  sptBufLen, &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent          = M_NULLPTR;
        scsiIoCtx->returnStatus.senseKey = sptdio->scsiPassThroughEXDirect.ScsiStatus;

        if (MSFT_BOOL_TRUE(success))
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; // setting to zero to be compatible with linux
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }

        // Any sense data?
        if (scsiIoCtx->psense != M_NULLPTR && scsiIoCtx->senseDataSize > 0)
        {
            safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, &sptdio->senseBuffer[0],
                        M_Min(sptdio->scsiPassThroughEXDirect.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != M_NULLPTR)
        {
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[3];
                break;
            }
        }
    }
    safe_free_SCSIPassthroughEx(&sptdio);
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    safe_free(&localBuffer);
    return ret;
}
#endif // #if defined INCLUDED_SCSI_DOT_H

// This structure MUST be dynamically allocated for double buffered transfers so that there is enough room for the
// return data! - TJE
typedef struct s_scsiPassThroughIOStruct
{
    union
    {
        SCSI_PASS_THROUGH_DIRECT scsiPassthroughDirect;
        SCSI_PASS_THROUGH        scsiPassthrough;
    };
    ULONG padding;                     // trying to help buffer alignment like the MS example shows.
    UCHAR senseBuffer[SPC3_SENSE_LEN]; // If we do auto-sense, we need to allocate 252 bytes, according to SPC-3.
    UCHAR dataBuffer[1];               // for double buffered transfer only
} scsiPassThroughIOStruct, *ptrSCSIPassThroughIOStruct;

static M_INLINE void safe_free_scsi_pt_io(scsiPassThroughIOStruct** scsipt)
{
    safe_free_core(M_REINTERPRET_CAST(void**, scsipt));
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
static eReturnValues convert_SCSI_CTX_To_SCSI_Pass_Through_Direct(ScsiIoCtx*                 scsiIoCtx,
                                                                  ptrSCSIPassThroughIOStruct psptd,
                                                                  const uint8_t*             alignedPointer)
{
    eReturnValues ret                            = SUCCESS;
    psptd->scsiPassthroughDirect.Length          = sizeof(SCSI_PASS_THROUGH_DIRECT);
    psptd->scsiPassthroughDirect.PathId          = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->scsiPassthroughDirect.TargetId        = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->scsiPassthroughDirect.Lun             = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->scsiPassthroughDirect.CdbLength       = scsiIoCtx->cdbLength;
    psptd->scsiPassthroughDirect.ScsiStatus      = 255; // set to something invalid
    psptd->scsiPassthroughDirect.SenseInfoLength = C_CAST(UCHAR, scsiIoCtx->senseDataSize);
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    psptd->scsiPassthroughDirect.DataTransferLength = scsiIoCtx->dataLength;
    psptd->scsiPassthroughDirect.DataBuffer         = M_CONST_CAST(PVOID, alignedPointer);
    switch (scsiIoCtx->direction)
    {
        // NOLINTBEGIN(bugprone-branch-clone)
    case XFER_DATA_IN:
        psptd->scsiPassthroughDirect.DataIn = SCSI_IOCTL_DATA_IN;
        break;
    case XFER_DATA_OUT:
        psptd->scsiPassthroughDirect.DataIn = SCSI_IOCTL_DATA_OUT;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassthroughDirect.DataIn             = UCHAR_C(0);
        psptd->scsiPassthroughDirect.DataTransferLength = ULONG_C(0);
        psptd->scsiPassthroughDirect.DataBuffer         = M_NULLPTR;
        break;
        // NOLINTEND(bugprone-branch-clone)
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = FAILURE;
        break;
    }
    if (scsiIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
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
    // Use offsetof macro to set where to place the sense data. Old code, for whatever reason, didn't always work
    // right...see comments below-TJE
    psptd->scsiPassthroughDirect.SenseInfoOffset = offsetof(scsiPassThroughIOStruct, senseBuffer);
    // sets the offset to the beginning of the sense buffer-TJE
    // psptd->scsiPassthroughDirect.SenseInfoOffset = C_CAST(ULONG, (&psptd->senseBuffer[0] - C_CAST(uint8_t*,
    // &psptd->scsiPassthroughDirect)));
    safe_memcpy(psptd->scsiPassthroughDirect.Cdb, 16, scsiIoCtx->cdb, M_Min(16, scsiIoCtx->cdbLength));
    return ret;
}

static eReturnValues convert_SCSI_CTX_To_SCSI_Pass_Through_Double_Buffered(ScsiIoCtx*                 scsiIoCtx,
                                                                           ptrSCSIPassThroughIOStruct psptd)
{
    eReturnValues ret                      = SUCCESS;
    psptd->scsiPassthrough.Length          = sizeof(SCSI_PASS_THROUGH_DIRECT);
    psptd->scsiPassthrough.PathId          = scsiIoCtx->device->os_info.scsi_addr.PathId;
    psptd->scsiPassthrough.TargetId        = scsiIoCtx->device->os_info.scsi_addr.TargetId;
    psptd->scsiPassthrough.Lun             = scsiIoCtx->device->os_info.scsi_addr.Lun;
    psptd->scsiPassthrough.CdbLength       = scsiIoCtx->cdbLength;
    psptd->scsiPassthrough.ScsiStatus      = 255; // set to something invalid
    psptd->scsiPassthrough.SenseInfoLength = C_CAST(UCHAR, scsiIoCtx->senseDataSize);
    ZeroMemory(psptd->senseBuffer, SPC3_SENSE_LEN);
    psptd->scsiPassthrough.DataTransferLength = scsiIoCtx->dataLength;
    psptd->scsiPassthrough.DataBufferOffset   = offsetof(scsiPassThroughIOStruct, dataBuffer);
    switch (scsiIoCtx->direction)
    {
        // NOLINTBEGIN(bugprone-branch-clone)
    case XFER_DATA_IN:
        psptd->scsiPassthrough.DataIn = SCSI_IOCTL_DATA_IN;
        break;
    case XFER_DATA_OUT:
        psptd->scsiPassthrough.DataIn = SCSI_IOCTL_DATA_OUT;
        break;
    case XFER_NO_DATA:
        psptd->scsiPassthrough.DataIn             = UCHAR_C(0);
        psptd->scsiPassthrough.DataTransferLength = ULONG_C(0);
        break;
        // NOLINTEND(bugprone-branch-clone)
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = FAILURE;
        break;
    }
    if (scsiIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
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
    // Use offsetof macro to set where to place the sense data. Old code, for whatever reason, didn't always work
    // right...see comments below-TJE
    psptd->scsiPassthrough.SenseInfoOffset = offsetof(scsiPassThroughIOStruct, senseBuffer);
    // sets the offset to the beginning of the sense buffer-TJE
    // psptd->scsiPassthrough.SenseInfoOffset = C_CAST(ULONG, (&psptd->senseBuffer[0] - C_CAST(uint8_t*,
    // &psptd->scsiPassthrough)));
    safe_memcpy(psptd->scsiPassthrough.Cdb, 16, scsiIoCtx->cdb, M_Min(16, scsiIoCtx->cdbLength));
    return ret;
}

static eReturnValues send_SCSI_Pass_Through(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues              ret           = FAILURE;
    BOOL                       success       = FALSE;
    ULONG                      returned_data = ULONG_C(0);
    ptrSCSIPassThroughIOStruct sptdioDB      = M_REINTERPRET_CAST(
        ptrSCSIPassThroughIOStruct, safe_malloc(sizeof(scsiPassThroughIOStruct) + scsiIoCtx->dataLength));
    if (!sptdioDB)
    {
        return MEMORY_FAILURE;
    }
    DECLARE_SEATIMER(commandTimer);
    safe_memset(sptdioDB, sizeof(scsiPassThroughIOStruct) + scsiIoCtx->dataLength, 0,
                sizeof(scsiPassThroughIOStruct) + scsiIoCtx->dataLength);
    ret = convert_SCSI_CTX_To_SCSI_Pass_Through_Double_Buffered(scsiIoCtx, sptdioDB);
    if (SUCCESS == ret)
    {
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD scsiPassThroughInLength         = sizeof(scsiPassThroughIOStruct);
        DWORD scsiPassThroughOutLength        = sizeof(scsiPassThroughIOStruct);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            scsiPassThroughOutLength += scsiIoCtx->dataLength;
            break;
        case XFER_DATA_OUT:
            // need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(sptdioDB->dataBuffer, scsiIoCtx->dataLength, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
            scsiPassThroughInLength += scsiIoCtx->dataLength;
            break;
        default:
            break;
        }
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        if (overlappedStruct.hEvent == M_NULLPTR)
        {
            safe_free_scsi_pt_io(&sptdioDB);
            return OS_PASSTHROUGH_FAILURE;
        }
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_SCSI_PASS_THROUGH, &sptdioDB->scsiPassthrough,
                                  scsiPassThroughInLength, &sptdioDB->scsiPassthrough, scsiPassThroughOutLength,
                                  &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
        if (MSFT_BOOL_TRUE(success))
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; // setting to zero to be compatible with linux
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, sptdioDB->dataBuffer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
        scsiIoCtx->returnStatus.senseKey = sptdioDB->scsiPassthrough.ScsiStatus;

        // Any sense data?
        if (scsiIoCtx->psense != M_NULLPTR && scsiIoCtx->senseDataSize > 0)
        {
            safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, sptdioDB->senseBuffer,
                        M_Min(sptdioDB->scsiPassthrough.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != M_NULLPTR)
        {
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[3];
                break;
            }
        }
    }
    safe_free_scsi_pt_io(&sptdioDB);
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

static eReturnValues send_SCSI_Pass_Through_Direct(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues           ret           = FAILURE;
    BOOL                    success       = FALSE;
    ULONG                   returned_data = ULONG_C(0);
    scsiPassThroughIOStruct sptdio;
    DECLARE_SEATIMER(commandTimer);
    safe_memset(&sptdio, sizeof(scsiPassThroughIOStruct), 0, sizeof(scsiPassThroughIOStruct));
    bool     localAlignedBuffer = false;
    uint8_t* alignedPointer     = scsiIoCtx->pdata;
    uint8_t* localBuffer        = M_NULLPTR; // we need to save this to free up the memory properly later.
    // Check the alignment...if we need to use a local buffer, we'll use one, then copy the data back
    if (scsiIoCtx->pdata && scsiIoCtx->device->os_info.alignmentMask != 0)
    {
        // This means the driver requires some sort of aligned pointer for the data buffer...so let's check and make
        // sure that the user's pointer is aligned If the user's pointer isn't aligned properly, align something local
        // that is aligned to meet the driver's requirements, then copy data back for them.
        alignedPointer = C_CAST(uint8_t*, (C_CAST(UINT_PTR, scsiIoCtx->pdata) +
                                           C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask)) &
                                              ~C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask));
        if (alignedPointer != scsiIoCtx->pdata)
        {
            localAlignedBuffer = true;
            uint32_t totalBufferSize =
                scsiIoCtx->dataLength + C_CAST(uint32_t, scsiIoCtx->device->os_info.alignmentMask);
            localBuffer = M_REINTERPRET_CAST(uint8_t*, safe_calloc(totalBufferSize, sizeof(uint8_t)));
            if (!localBuffer)
            {
                perror(
                    "error allocating aligned buffer for ATA Passthrough Direct...attempting to use user's pointer.");
                localAlignedBuffer = false;
                alignedPointer     = scsiIoCtx->pdata;
            }
            else
            {
                alignedPointer = C_CAST(uint8_t*, (C_CAST(UINT_PTR, localBuffer) +
                                                   C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask)) &
                                                      ~C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask));
                if (scsiIoCtx->direction == XFER_DATA_OUT)
                {
                    safe_memcpy(alignedPointer,
                                totalBufferSize -
                                    (C_CAST(uintptr_t, alignedPointer) - (C_CAST(uintptr_t, localBuffer))),
                                scsiIoCtx->pdata, scsiIoCtx->dataLength);
                }
            }
        }
    }
    ret = convert_SCSI_CTX_To_SCSI_Pass_Through_Direct(scsiIoCtx, &sptdio, alignedPointer);
    if (SUCCESS == ret)
    {
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        scsiIoCtx->device->os_info.last_error = 0;
        DWORD      scsiPassThroughBufLen      = sizeof(scsiPassThroughIOStruct);
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_SCSI_PASS_THROUGH_DIRECT,
                                  &sptdio.scsiPassthroughDirect, scsiPassThroughBufLen, &sptdio.scsiPassthroughDirect,
                                  scsiPassThroughBufLen, &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
            scsiIoCtx->device->os_info.last_error = GetLastError();
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
        if (MSFT_BOOL_TRUE(success))
        {
            // If the operation completes successfully, the return value is nonzero.
            // If the operation fails or is pending, the return value is zero. To get extended error information, call
            ret = SUCCESS; // setting to zero to be compatible with linux
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }

        // Any sense data?
        if (scsiIoCtx->psense != M_NULLPTR && scsiIoCtx->senseDataSize > 0)
        {
            safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, sptdio.senseBuffer,
                        M_Min(sptdio.scsiPassthroughDirect.SenseInfoLength, scsiIoCtx->senseDataSize));
        }

        if (scsiIoCtx->psense != M_NULLPTR)
        {
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.format = scsiIoCtx->psense[0];
            switch (scsiIoCtx->returnStatus.format & 0x7F)
            {
            case SCSI_SENSE_CUR_INFO_FIXED:
            case SCSI_SENSE_DEFER_ERR_FIXED:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[2] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[12];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[13];
                break;
            case SCSI_SENSE_CUR_INFO_DESC:
            case SCSI_SENSE_DEFER_ERR_DESC:
                scsiIoCtx->returnStatus.senseKey = scsiIoCtx->psense[1] & 0x0F;
                scsiIoCtx->returnStatus.asc      = scsiIoCtx->psense[2];
                scsiIoCtx->returnStatus.ascq     = scsiIoCtx->psense[3];
                break;
            }
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (localAlignedBuffer)
    {
        safe_free(&localBuffer);
    }
    return ret;
}

static eReturnValues send_SCSI_Pass_Through_IO(ScsiIoCtx* scsiIoCtx)
{
    if (scsiIoCtx->device->os_info.ioMethod == WIN_IOCTL_FORCE_ALWAYS_DIRECT)
    {
        if (scsiIoCtx->cdbLength <= CDB_LEN_16)
        {
            return send_SCSI_Pass_Through_Direct(scsiIoCtx);
        }
#if defined INCLUDED_SCSI_DOT_H
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
#if defined INCLUDED_SCSI_DOT_H
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
        if (/*scsiIoCtx->device->os_info.srbtype == SRB_TYPE_SCSI_REQUEST_BLOCK && */ scsiIoCtx->cdbLength <=
                CDB_LEN_16 &&
            scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH)
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
#if defined INCLUDED_SCSI_DOT_H
        else if (scsiIoCtx->device->os_info.srbtype == SRB_TYPE_STORAGE_REQUEST_BLOCK) // supports 32byte IOCTLS
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
static eReturnValues convert_SCSI_CTX_To_ATA_PT_Direct(ScsiIoCtx*               p_scsiIoCtx,
                                                       PATA_PASS_THROUGH_DIRECT ptrATAPassThroughDirect,
                                                       uint8_t*                 alignedDataPointer)
{
    eReturnValues ret = SUCCESS;

    ptrATAPassThroughDirect->Length             = sizeof(ATA_PASS_THROUGH_DIRECT);
    ptrATAPassThroughDirect->AtaFlags           = ATA_FLAGS_DRDY_REQUIRED;
    ptrATAPassThroughDirect->DataTransferLength = p_scsiIoCtx->dataLength;
    ptrATAPassThroughDirect->DataBuffer         = alignedDataPointer;
#if WINVER >= SEA_WIN32_WINNT_VISTA
    if (p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount <= UINT8_C(1) &&
        p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 == UINT8_C(0))
    {
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_NO_MULTIPLE;
    }
#endif // WIN_VISTA

    switch (p_scsiIoCtx->direction)
    {
        // NOLINTBEGIN(bugprone-branch-clone)
    case XFER_DATA_IN:
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_DATA_IN;
        break;
    case XFER_DATA_OUT:
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_DATA_OUT;
        break;
    case XFER_NO_DATA:
        ptrATAPassThroughDirect->DataTransferLength = ULONG_C(0);
        ptrATAPassThroughDirect->DataBuffer         = M_NULLPTR;
#if WINVER >= SEA_WIN32_WINNT_VISTA
        ptrATAPassThroughDirect->AtaFlags =
            ptrATAPassThroughDirect->AtaFlags & M_STATIC_CAST(USHORT, ~(ATA_FLAGS_NO_MULTIPLE));
#endif // WIN_VISTA
       // NOLINTEND(bugprone-branch-clone)
        break;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = BAD_PARAMETER;
        break;
    }
    // set the DMA flag if needed
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
        // these are supported but no flags need to be set
        break;
    case ATA_PROTOCOL_RET_INFO:
        // this doesn't do anything in ATA PassThrough and is only useful for SCSI PassThrough since this is an HBA
        // request, not a drive request, but we don't want to print out an error message
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nProtocol Not Supported in ATA Pass Through.\n");
        }
        return NOT_SUPPORTED;
    }
    if (p_scsiIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    if (p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > UINT32_C(0) &&
        p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > p_scsiIoCtx->timeout)
    {
        ptrATAPassThroughDirect->TimeOutValue = p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        ptrATAPassThroughDirect->TimeOutValue = p_scsiIoCtx->timeout;
        if (p_scsiIoCtx->timeout == UINT32_C(0))
        {
            ptrATAPassThroughDirect->TimeOutValue = UINT32_C(15);
        }
    }
    ptrATAPassThroughDirect->PathId   = p_scsiIoCtx->device->os_info.scsi_addr.PathId;
    ptrATAPassThroughDirect->TargetId = p_scsiIoCtx->device->os_info.scsi_addr.TargetId;
    ptrATAPassThroughDirect->Lun      = p_scsiIoCtx->device->os_info.scsi_addr.Lun;
    // Task File
    ptrATAPassThroughDirect->CurrentTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
    ptrATAPassThroughDirect->CurrentTaskFile[1] = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;  // Sector Count Reg
    ptrATAPassThroughDirect->CurrentTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // Sector Number ( or LBA Lo )
    ptrATAPassThroughDirect->CurrentTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid; // Cylinder Low ( or LBA Mid )
    ptrATAPassThroughDirect->CurrentTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;  // Cylinder High (or LBA Hi)
    ptrATAPassThroughDirect->CurrentTaskFile[5] = p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;    // Device/Head Register
    ptrATAPassThroughDirect->CurrentTaskFile[6] = p_scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
    ptrATAPassThroughDirect->CurrentTaskFile[7] = UCHAR_C(0);                                  // Reserved
    if (p_scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        // set the 48bit command flag
        ptrATAPassThroughDirect->AtaFlags |= ATA_FLAGS_48BIT_COMMAND;
        ptrATAPassThroughDirect->PreviousTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.Feature48; // Features Ext Register
        ptrATAPassThroughDirect->PreviousTaskFile[1] =
            p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48; // Sector Count Ext Register
        ptrATAPassThroughDirect->PreviousTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48; // LBA Lo Ext
        ptrATAPassThroughDirect->PreviousTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48; // LBA Mid Ext
        ptrATAPassThroughDirect->PreviousTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;  // LBA Hi Ext
        ptrATAPassThroughDirect->PreviousTaskFile[5] = UCHAR_C(0);
        ptrATAPassThroughDirect->PreviousTaskFile[6] = UCHAR_C(0);
        ptrATAPassThroughDirect->PreviousTaskFile[7] = UCHAR_C(0);
    }
    return ret;
}

static eReturnValues send_ATA_Passthrough_Direct(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues           ret           = FAILURE;
    BOOL                    success       = FALSE;
    ULONG                   returned_data = ULONG_C(0);
    ATA_PASS_THROUGH_DIRECT ataPassThroughDirect;
    DECLARE_SEATIMER(commandTimer);
    safe_memset(&ataPassThroughDirect, sizeof(ATA_PASS_THROUGH_DIRECT), 0, sizeof(ATA_PASS_THROUGH_DIRECT));
    bool     localAlignedBuffer = false;
    uint8_t* alignedPointer     = scsiIoCtx->pdata;
    uint8_t* localBuffer        = M_NULLPTR; // we need to save this to free up the memory properly later.
    // Check the alignment...if we need to use a local buffer, we'll use one, then copy the data back
    if (scsiIoCtx->pdata && scsiIoCtx->device->os_info.alignmentMask != 0)
    {
        // This means the driver requires some sort of aligned pointer for the data buffer...so let's check and make
        // sure that the user's pointer is aligned If the user's pointer isn't aligned properly, align something local
        // that is aligned to meet the driver's requirements, then copy data back for them.
        alignedPointer = C_CAST(uint8_t*, (C_CAST(UINT_PTR, scsiIoCtx->pdata) +
                                           C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask)) &
                                              ~C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask));
        if (alignedPointer != scsiIoCtx->pdata)
        {
            localAlignedBuffer = true;
            uint32_t totalBufferSize =
                scsiIoCtx->dataLength + C_CAST(uint32_t, scsiIoCtx->device->os_info.alignmentMask);
            localBuffer = M_REINTERPRET_CAST(uint8_t*, safe_calloc(totalBufferSize, sizeof(uint8_t)));
            if (!localBuffer)
            {
                perror(
                    "error allocating aligned buffer for ATA Passthrough Direct...attempting to use user's pointer.");
                localAlignedBuffer = false;
                alignedPointer     = scsiIoCtx->pdata;
            }
            else
            {
                alignedPointer = C_CAST(uint8_t*, (C_CAST(UINT_PTR, localBuffer) +
                                                   C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask)) &
                                                      ~C_CAST(UINT_PTR, scsiIoCtx->device->os_info.alignmentMask));
                if (scsiIoCtx->direction == XFER_DATA_OUT)
                {
                    safe_memcpy(alignedPointer,
                                totalBufferSize - (C_CAST(uintptr_t, alignedPointer) - C_CAST(uintptr_t, localBuffer)),
                                scsiIoCtx->pdata, scsiIoCtx->dataLength);
                }
            }
        }
    }

    ret = convert_SCSI_CTX_To_ATA_PT_Direct(scsiIoCtx, &ataPassThroughDirect, alignedPointer);
    if (SUCCESS == ret)
    {
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_ATA_PASS_THROUGH_DIRECT, &ataPassThroughDirect,
                                  sizeof(ATA_PASS_THROUGH_DIRECT), &ataPassThroughDirect,
                                  sizeof(ATA_PASS_THROUGH_DIRECT), &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
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
            case ERROR_NOT_SUPPORTED: // this is what is returned when we try to send a sanitize command in Win10
                ret = OS_COMMAND_BLOCKED;
                break;
            case ERROR_IO_DEVICE:         // OS_PASSTHROUGH_FAILURE
            case ERROR_INVALID_PARAMETER: // Or command not supported?
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
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
        }
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (MSFT_BOOL_TRUE(success))
        {
            ret = SUCCESS;
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                // memcpy the data back to the user's pointer since we had to allocate one locally.
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, alignedPointer, scsiIoCtx->dataLength);
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
        scsiIoCtx->returnStatus.asc  = 0x00; // might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D; // might need to change this later
        // get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in
        // descriptor format current = 28bit, previous = 48bit
        if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >=
                22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01; // check condition
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01; // recovered error
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2]  = 0x00; // ASC
                scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                scsiIoCtx->psense[4]  = 0;
                scsiIoCtx->psense[5]  = 0;
                scsiIoCtx->psense[6]  = 0;
                scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                scsiIoCtx->psense[8]  = 0x09; // descriptor code
                scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                scsiIoCtx->psense[10] = 0;
                if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
                {
                    scsiIoCtx->psense[10] |= 0x01; // set the extend bit
                    // fill in the ext registers while we're in this if...no need for another one
                    scsiIoCtx->psense[12] = ataPassThroughDirect.PreviousTaskFile[1]; // Sector Count Ext
                    scsiIoCtx->psense[14] = ataPassThroughDirect.PreviousTaskFile[2]; // LBA Lo Ext
                    scsiIoCtx->psense[16] = ataPassThroughDirect.PreviousTaskFile[3]; // LBA Mid Ext
                    scsiIoCtx->psense[18] = ataPassThroughDirect.PreviousTaskFile[4]; // LBA Hi
                }
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = ataPassThroughDirect.CurrentTaskFile[0]; // Error
                scsiIoCtx->psense[13] = ataPassThroughDirect.CurrentTaskFile[1]; // Sector Count
                scsiIoCtx->psense[15] = ataPassThroughDirect.CurrentTaskFile[2]; // LBA Lo
                scsiIoCtx->psense[17] = ataPassThroughDirect.CurrentTaskFile[3]; // LBA Mid
                scsiIoCtx->psense[19] = ataPassThroughDirect.CurrentTaskFile[4]; // LBA Hi
                scsiIoCtx->psense[20] = ataPassThroughDirect.CurrentTaskFile[5]; // Device/Head
                scsiIoCtx->psense[21] = ataPassThroughDirect.CurrentTaskFile[6]; // Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc      = 0x20;
        scsiIoCtx->returnStatus.ascq     = 0x00;
        // dummy up sense data
        if (scsiIoCtx->psense != M_NULLPTR)
        {
            safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
            // fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            // acq
            scsiIoCtx->psense[12] = 0x20; // invalid operation code
            // acsq
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
    safe_free(&localBuffer); // This will check if M_NULLPTR before freeing it, so we shouldn't have any issues.
    return ret;
}

typedef struct s_ATADoubleBufferedIO
{
    ATA_PASS_THROUGH_EX ataPTCommand;
    ULONG padding; // trying to help buffer alignment like the MS spti example shows....may or may not be needed here
    UCHAR dataBuffer[1];
} ATADoubleBufferedIO, *ptrATADoubleBufferedIO;

static M_INLINE void safe_free_ata_db_io(ATADoubleBufferedIO** atadbio)
{
    safe_free_core(M_REINTERPRET_CAST(void**, atadbio));
}

static eReturnValues convert_SCSI_CTX_To_ATA_PT_Ex(ScsiIoCtx* p_scsiIoCtx, ptrATADoubleBufferedIO p_t_ata_pt)
{
    eReturnValues ret = SUCCESS;

    p_t_ata_pt->ataPTCommand.Length   = sizeof(ATA_PASS_THROUGH_EX);
    p_t_ata_pt->ataPTCommand.AtaFlags = ATA_FLAGS_DRDY_REQUIRED;

    p_t_ata_pt->ataPTCommand.DataTransferLength = p_scsiIoCtx->dataLength;
    p_t_ata_pt->ataPTCommand.DataBufferOffset   = offsetof(ATADoubleBufferedIO, dataBuffer);
#if WINVER >= SEA_WIN32_WINNT_VISTA
    if (p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount <= UINT8_C(1) &&
        p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 == UINT8_C(0))
    {
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_NO_MULTIPLE;
    }
#endif // WIN_VISTA

    switch (p_scsiIoCtx->direction)
    {
        // NOLINTBEGIN(bugprone-branch-clone))
    case XFER_DATA_IN:
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_DATA_IN;
        break;
    case XFER_DATA_OUT:
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_DATA_OUT;
        break;
        // NOLINTEND(bugprone-branch-clone)
    case XFER_NO_DATA:
        p_t_ata_pt->ataPTCommand.DataTransferLength = ULONG_C(0);
        // we always allocate at least 1 byte here...so give it something? Or do
        // we set M_NULLPTR? Seems to work as is... - TJE
        // p_t_ata_pt->ataPTCommand.DataBufferOffset   = offsetof(ATADoubleBufferedIO, dataBuffer);
#if WINVER >= SEA_WIN32_WINNT_VISTA
        // Turn this bit off in case it was set
        p_t_ata_pt->ataPTCommand.AtaFlags =
            p_t_ata_pt->ataPTCommand.AtaFlags & M_STATIC_CAST(USHORT, ~(ATA_FLAGS_NO_MULTIPLE));
#endif // WIN VISTA
        break;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nData Direction Unspecified.\n");
        }
        ret = BAD_PARAMETER;
        break;
    }
    // set the DMA flag if needed
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
        // these are supported but no flags need to be set
        break;
    case ATA_PROTOCOL_RET_INFO:
        // this doesn't do anything in ATA PassThrough and is only useful for SCSI PassThrough since this is an HBA
        // request, not a drive request, but we don't want to print out an error message
        return NOT_SUPPORTED;
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nProtocol Not Supported in ATA Pass Through.\n");
        }
        ret = NOT_SUPPORTED;
        break;
    }
    if (p_scsiIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }
    if (p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        p_scsiIoCtx->device->drive_info.defaultTimeoutSeconds > p_scsiIoCtx->timeout)
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
    p_t_ata_pt->ataPTCommand.PathId   = p_scsiIoCtx->device->os_info.scsi_addr.PathId;
    p_t_ata_pt->ataPTCommand.TargetId = p_scsiIoCtx->device->os_info.scsi_addr.TargetId;
    p_t_ata_pt->ataPTCommand.Lun      = p_scsiIoCtx->device->os_info.scsi_addr.Lun;
    // Task File
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[1] = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;  // Sector Count Reg
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // Sector Number ( or LBA Lo )
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid; // Cylinder Low ( or LBA Mid )
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;  // Cylinder High (or LBA Hi)
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[5] = p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;    // Device/Head Register
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[6] = p_scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
    p_t_ata_pt->ataPTCommand.CurrentTaskFile[7] = 0;                                           // Reserved
    if (p_scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        // set the 48bit command flag
        p_t_ata_pt->ataPTCommand.AtaFlags |= ATA_FLAGS_48BIT_COMMAND;
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[0] = p_scsiIoCtx->pAtaCmdOpts->tfr.Feature48; // Features Ext Register
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[1] =
            p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48; // Sector Count Ext Register
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[2] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48; // LBA Lo Ext
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[3] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48; // LBA Mid Ext
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[4] = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;  // LBA Hi Ext
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[5] =
            0; // p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;//this is either 0 or device...
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[6] = 0;
        p_t_ata_pt->ataPTCommand.PreviousTaskFile[7] = 0;
    }
    return ret;
}

static eReturnValues send_ATA_Passthrough_Ex(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues          ret           = FAILURE;
    BOOL                   success       = FALSE;
    ULONG                  returned_data = ULONG_C(0);
    uint32_t               dataLength    = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
    ptrATADoubleBufferedIO doubleBufferedIO =
        M_REINTERPRET_CAST(ptrATADoubleBufferedIO, safe_malloc(sizeof(ATA_PASS_THROUGH_EX) + dataLength));
    if (!doubleBufferedIO)
    {
        // something went really wrong...
        return MEMORY_FAILURE;
    }
    DECLARE_SEATIMER(commandTimer);
    safe_memset(doubleBufferedIO, sizeof(ATA_PASS_THROUGH_EX) + dataLength, 0,
                sizeof(ATA_PASS_THROUGH_EX) + dataLength);
    ret = convert_SCSI_CTX_To_ATA_PT_Ex(scsiIoCtx, doubleBufferedIO);
    if (SUCCESS == ret)
    {
        ULONG inBufferLength  = sizeof(ATA_PASS_THROUGH_DIRECT);
        ULONG outBufferLength = sizeof(ATA_PASS_THROUGH_DIRECT);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            outBufferLength += M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
            break;
        case XFER_DATA_OUT:
            // need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(doubleBufferedIO->dataBuffer, scsiIoCtx->dataLength, scsiIoCtx->pdata,
                            scsiIoCtx->dataLength);
            }
            inBufferLength += M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
            break;
        default:
            break;
        }
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success                               = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_ATA_PASS_THROUGH,
                                                                &doubleBufferedIO->ataPTCommand, inBufferLength, &doubleBufferedIO->ataPTCommand,
                                                                outBufferLength, &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
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
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
        }
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (MSFT_BOOL_TRUE(success))
        {
            ret = SUCCESS;
            // copy the data buffer back to the user's data pointer
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, doubleBufferedIO->dataBuffer,
                            scsiIoCtx->dataLength);
            }
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
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
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
            scsiIoCtx->returnStatus.senseKey = 0x01;
        }
        scsiIoCtx->returnStatus.asc  = 0x00; // might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D; // might need to change this later
        // get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in
        // descriptor format current = 28bit, previous = 48bit
        if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >=
                22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01; // check condition
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01; // recovered error
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2]  = 0x00; // ASC
                scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                scsiIoCtx->psense[4]  = 0;
                scsiIoCtx->psense[5]  = 0;
                scsiIoCtx->psense[6]  = 0;
                scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                scsiIoCtx->psense[8]  = 0x09; // descriptor code
                scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                scsiIoCtx->psense[10] = 0;
                if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
                {
                    scsiIoCtx->psense[10] |= 0x01; // set the extend bit
                    // fill in the ext registers while we're in this if...no need for another one
                    scsiIoCtx->psense[12] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[1]; // Sector Count Ext
                    scsiIoCtx->psense[14] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[2]; // LBA Lo Ext
                    scsiIoCtx->psense[16] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[3]; // LBA Mid Ext
                    scsiIoCtx->psense[18] = doubleBufferedIO->ataPTCommand.PreviousTaskFile[4]; // LBA Hi
                }
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[0]; // Error
                scsiIoCtx->psense[13] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[1]; // Sector Count
                scsiIoCtx->psense[15] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[2]; // LBA Lo
                scsiIoCtx->psense[17] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[3]; // LBA Mid
                scsiIoCtx->psense[19] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[4]; // LBA Hi
                scsiIoCtx->psense[20] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[5]; // Device/Head
                scsiIoCtx->psense[21] = doubleBufferedIO->ataPTCommand.CurrentTaskFile[6]; // Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc      = 0x20;
        scsiIoCtx->returnStatus.ascq     = 0x00;
        // dummy up sense data
        if (scsiIoCtx->psense != M_NULLPTR)
        {
            safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
            // fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            // acq
            scsiIoCtx->psense[12] = 0x20; // invalid operation code
            // acsq
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
    safe_free_ata_db_io(&doubleBufferedIO);
    return ret;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
static eReturnValues send_ATA_Pass_Through_IO(ScsiIoCtx* scsiIoCtx)
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
        // check the transfer length to decide
        if (scsiIoCtx->dataLength > DOUBLE_BUFFERED_MAX_TRANSFER_SIZE ||
            scsiIoCtx->pAtaCmdOpts->dataSize > DOUBLE_BUFFERED_MAX_TRANSFER_SIZE)
        {
            // direct IO
            return send_ATA_Passthrough_Direct(scsiIoCtx);
        }
        else
        {
            // double buffered IO
            return send_ATA_Passthrough_Ex(scsiIoCtx);
        }
    }
}

typedef struct s_IDEDoubleBufferedIO
{
    IDEREGS ideRegisters;
    ULONG   dataBufferSize;
    UCHAR   dataBuffer[1];
} IDEDoubleBufferedIO, *ptrIDEDoubleBufferedIO;

static M_INLINE void safe_free_ide_db_io(IDEDoubleBufferedIO** idedbio)
{
    safe_free_core(M_REINTERPRET_CAST(void**, idedbio));
}

static eReturnValues convert_SCSI_CTX_To_IDE_PT(ScsiIoCtx* p_scsiIoCtx, ptrIDEDoubleBufferedIO p_t_ide_pt)
{
    eReturnValues ret = SUCCESS;

    if (p_scsiIoCtx->pAtaCmdOpts->commandType != ATA_CMD_TYPE_TASKFILE)
    {
        return NOT_SUPPORTED;
    }

    p_t_ide_pt->dataBufferSize = p_scsiIoCtx->dataLength;

    // set the DMA flag if needed
    switch (p_scsiIoCtx->pAtaCmdOpts->commadProtocol)
    {
    case ATA_PROTOCOL_DMA:
    case ATA_PROTOCOL_UDMA:
    case ATA_PROTOCOL_PACKET_DMA:
        // Some comments in old old legacy code say DMA is not supported with this IOCTL, so they don't issue the
        // command. We are allowing this through for now until we can debug it ourselves to see if it was an
        // implementation issue or not using aligned memory, etc.
    case ATA_PROTOCOL_DEV_DIAG:
    case ATA_PROTOCOL_NO_DATA:
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PIO:
        // these are supported but no flags need to be set
        break;
    case ATA_PROTOCOL_RET_INFO:
        // this doesn't do anything in ATA PassThrough and is only useful for SCSI PassThrough since this is an HBA
        // request, not a drive request, but we don't want to print out an error message
    default:
        if (VERBOSITY_QUIET < p_scsiIoCtx->device->deviceVerbosity)
        {
            printf("\nProtocol Not Supported in ATA Pass Through.\n");
        }
        return NOT_SUPPORTED;
        break;
    }
    // Timeout cannot be set...-TJE
    //  Task File (No extended commands allowed!)
    p_t_ide_pt->ideRegisters.bFeaturesReg     = p_scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
    p_t_ide_pt->ideRegisters.bSectorCountReg  = p_scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;  // Sector Count Reg
    p_t_ide_pt->ideRegisters.bSectorNumberReg = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;     // Sector Number ( or LBA Lo )
    p_t_ide_pt->ideRegisters.bCylLowReg       = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;     // Cylinder Low ( or LBA Mid )
    p_t_ide_pt->ideRegisters.bCylHighReg      = p_scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;      // Cylinder High (or LBA Hi)
    p_t_ide_pt->ideRegisters.bDriveHeadReg    = p_scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead; // Device/Head Register
    p_t_ide_pt->ideRegisters.bCommandReg      = p_scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
    p_t_ide_pt->ideRegisters.bReserved        = RESERVED;

    return ret;
}
// This code has not been tested! - TJE
static eReturnValues send_IDE_Pass_Through_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = FAILURE;
    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return OS_COMMAND_NOT_AVAILABLE; // or NOT_SUPPORTED ? - TJE
    }
    BOOL                   success       = FALSE;
    ULONG                  returned_data = ULONG_C(0);
    uint32_t               dataLength    = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
    ptrIDEDoubleBufferedIO doubleBufferedIO =
        M_REINTERPRET_CAST(ptrIDEDoubleBufferedIO, safe_malloc(sizeof(IDEDoubleBufferedIO) - 1 + dataLength));
    if (!doubleBufferedIO)
    {
        // something went really wrong...
        return MEMORY_FAILURE;
    }
    DECLARE_SEATIMER(commandTimer);
    safe_memset(doubleBufferedIO, sizeof(IDEDoubleBufferedIO) - 1 + dataLength, 0,
                sizeof(IDEDoubleBufferedIO) - 1 + dataLength);
    ret = convert_SCSI_CTX_To_IDE_PT(scsiIoCtx, doubleBufferedIO);
    if (SUCCESS == ret)
    {
        ULONG inBufferLength  = sizeof(IDEREGS) + sizeof(ULONG);
        ULONG outBufferLength = sizeof(IDEREGS) + sizeof(ULONG);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            outBufferLength += dataLength;
            break;
        case XFER_DATA_OUT:
            // need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(doubleBufferedIO->dataBuffer, scsiIoCtx->dataLength, scsiIoCtx->pdata,
                            scsiIoCtx->dataLength);
            }
            inBufferLength += dataLength;
            break;
        default:
            break;
        }
        scsiIoCtx->device->os_info.last_error = 0;
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_IDE_PASS_THROUGH, doubleBufferedIO,
                                  inBufferLength, doubleBufferedIO, outBufferLength, &returned_data, &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
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
        if (overlappedStruct.hEvent)
        {
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
        }
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (MSFT_BOOL_TRUE(success))
        {
            ret = SUCCESS;
            // copy the data buffer back to the user's data pointer
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, doubleBufferedIO->dataBuffer,
                            scsiIoCtx->dataLength);
            }
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_IO_DEVICE: // OS_PASSTHROUGH_FAILURE
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
            scsiIoCtx->returnStatus.senseKey = 0x01;
        }
        scsiIoCtx->returnStatus.asc  = 0x00; // might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D; // might need to change this later
        // get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in
        // descriptor format current = 28bit, previous = 48bit
        if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >=
                22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01; // check condition
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01; // recovered error
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2]  = 0x00; // ASC
                scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                scsiIoCtx->psense[4]  = 0;
                scsiIoCtx->psense[5]  = 0;
                scsiIoCtx->psense[6]  = 0;
                scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                scsiIoCtx->psense[8]  = 0x09; // descriptor code
                scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                scsiIoCtx->psense[10] = 0;
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = doubleBufferedIO->ideRegisters.bFeaturesReg;     // Error
                scsiIoCtx->psense[13] = doubleBufferedIO->ideRegisters.bSectorCountReg;  // Sector Count
                scsiIoCtx->psense[15] = doubleBufferedIO->ideRegisters.bSectorNumberReg; // LBA Lo
                scsiIoCtx->psense[17] = doubleBufferedIO->ideRegisters.bCylLowReg;       // LBA Mid
                scsiIoCtx->psense[19] = doubleBufferedIO->ideRegisters.bCylHighReg;      // LBA Hi
                scsiIoCtx->psense[20] = doubleBufferedIO->ideRegisters.bDriveHeadReg;    // Device/Head
                scsiIoCtx->psense[21] = doubleBufferedIO->ideRegisters.bCommandReg;      // Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc      = 0x20;
        scsiIoCtx->returnStatus.ascq     = 0x00;
        // dummy up sense data
        if (scsiIoCtx->psense != M_NULLPTR)
        {
            safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
            // fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            // acq
            scsiIoCtx->psense[12] = 0x20; // invalid operation code
            // acsq
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
    safe_free_ide_db_io(&doubleBufferedIO);
    return ret;
}

// This is untested code, but may work if an old system needed it.
// https://community.osr.com/discussion/64468/scsiop-ata-passthrough-0xcc
// #define UNDOCUMENTED_SCSI_IDE_PT_OP_CODE 0xCC
// static eReturnValues send_SCSI_IDE_Pass_Through_IO(ScsiIoCtx *scsiIoCtx)
//{
//     eReturnValues ret = FAILURE;
//     ScsiIoCtx ideCtx;
//     safe_memset(&ideCtx, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
//     if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
//     {
//         return OS_COMMAND_NOT_AVAILABLE;
//     }
//
//     ideCtx.cdb[0] = UNDOCUMENTED_SCSI_IDE_PT_OP_CODE;
//     ideCtx.cdb[1] = RESERVED;
//     ideCtx.cdb[2] = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature; // Features Register
//     ideCtx.cdb[3] = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount; // Sector Count Reg
//     ideCtx.cdb[4] = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // Sector Number ( or LBA Lo )
//     ideCtx.cdb[5] = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid; // Cylinder Low ( or LBA Mid )
//     ideCtx.cdb[6] = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi; // Cylinder High (or LBA Hi)
//     ideCtx.cdb[7] = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead; // Device/Head Register
//     ideCtx.cdb[8] = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus; // Command Register
//     ideCtx.cdb[9] = 0;//control register
//
//     ideCtx.cdbLength = 10;
//     ideCtx.dataLength = scsiIoCtx->dataLength;
//     ideCtx.device = scsiIoCtx->device;
//     ideCtx.direction = scsiIoCtx->direction;
//     ideCtx.pdata = scsiIoCtx->pdata;
//     ideCtx.psense = scsiIoCtx->psense;
//     ideCtx.senseDataSize = scsiIoCtx->senseDataSize;
//     ideCtx.timeout = scsiIoCtx->timeout;
//
//     ret = send_SCSI_Pass_Through(&ideCtx);
//
//     //TODO: Turn sense data into RTFRs
//
//     return ret;
// }

#if WINVER >= SEA_WIN32_WINNT_WIN10
eReturnValues get_Windows_FWDL_IO_Support(tDevice* device, STORAGE_BUS_TYPE busType)
{
    eReturnValues                  ret = NOT_SUPPORTED;
    STORAGE_HW_FIRMWARE_INFO_QUERY fwdlInfo;
    safe_memset(&fwdlInfo, sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY), 0, sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY));
    fwdlInfo.Version  = sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY);
    fwdlInfo.Size     = sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY);
    uint8_t slotCount = UINT8_C(7); // 7 is maximum number of firmware slots...always reading with this for now since it
                                    // doesn't hurt sas/sata drives. - TJE
    uint32_t outputDataSize = sizeof(STORAGE_HW_FIRMWARE_INFO) + (sizeof(STORAGE_HW_FIRMWARE_SLOT_INFO) * slotCount);
    uint8_t* outputData     = M_REINTERPRET_CAST(uint8_t*, safe_malloc(outputDataSize));
    if (outputData == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(outputData, outputDataSize, 0, outputDataSize);
    DWORD returned_data = DWORD_C(0);
    // STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER is needed for NVMe to report relavant data. Without it, we only see 1
    // slot available.
    if (busType == BusTypeNvme)
    {
        fwdlInfo.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
    }
    int fwdlRet =
        DeviceIoControl(device->os_info.fd, IOCTL_STORAGE_FIRMWARE_GET_INFO, &fwdlInfo,
                        sizeof(STORAGE_HW_FIRMWARE_INFO_QUERY), outputData, outputDataSize, &returned_data, M_NULLPTR);
    // Got the version info, but that doesn't mean we'll be successful with commands...
    if (fwdlRet)
    {
        PSTORAGE_HW_FIRMWARE_INFO fwdlSupportedInfo    = C_CAST(PSTORAGE_HW_FIRMWARE_INFO, outputData);
        device->os_info.fwdlIOsupport.fwdlIOSupported  = fwdlSupportedInfo->SupportUpgrade;
        device->os_info.fwdlIOsupport.payloadAlignment = fwdlSupportedInfo->ImagePayloadAlignment;
        device->os_info.fwdlIOsupport.maxXferSize      = fwdlSupportedInfo->ImagePayloadMaxSize;

#    if defined(_DEBUG)
        printf("Got Win10 FWDL Info\n");
        printf("\tSupported: %d\n", fwdlSupportedInfo->SupportUpgrade);
        printf("\tPayload Alignment: %ld\n", fwdlSupportedInfo->ImagePayloadAlignment);
        printf("\tmaxXferSize: %ld\n", fwdlSupportedInfo->ImagePayloadMaxSize);
        printf("\tPendingActivate: %d\n", fwdlSupportedInfo->PendingActivateSlot);
        printf("\tActiveSlot: %d\n", fwdlSupportedInfo->ActiveSlot);
        printf("\tSlot Count: %d\n", fwdlSupportedInfo->SlotCount);
        printf("\tFirmware Shared: %d\n", fwdlSupportedInfo->FirmwareShared);
        // print out what's in the slots!
        for (uint8_t iter = UINT8_C(0); iter < fwdlSupportedInfo->SlotCount && iter < slotCount; ++iter)
        {
            printf("\t    Firmware Slot %d:\n", fwdlSupportedInfo->Slot[iter].SlotNumber);
            printf("\t\tRead Only: %d\n", fwdlSupportedInfo->Slot[iter].ReadOnly);
            printf("\t\tRevision: %s\n", fwdlSupportedInfo->Slot[iter].Revision);
        }
#    endif
        ret = SUCCESS;
    }
    else
    {
        // DWORD lastError = GetLastError();
        ret = FAILURE;
    }
    safe_free(&outputData);
    return ret;
}

static eReturnValues win10_FW_Activate_IO_SCSI(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (!scsiIoCtx)
    {
        return BAD_PARAMETER;
    }
    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        printf("Sending firmware activate with Win10 API\n");
    }
    // send the activate IOCTL
    STORAGE_HW_FIRMWARE_ACTIVATE downloadActivate;
    safe_memset(&downloadActivate, sizeof(STORAGE_HW_FIRMWARE_ACTIVATE), 0, sizeof(STORAGE_HW_FIRMWARE_ACTIVATE));
    downloadActivate.Version = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    downloadActivate.Size    = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    // downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE;
    if (scsiIoCtx && !scsiIoCtx->pAtaCmdOpts)
    {
        downloadActivate.Slot =
            scsiIoCtx->cdb[2]; // Set the slot number to the buffer ID number...This is the closest this translates.
    }
    if (scsiIoCtx->device->drive_info.interface_type == NVME_INTERFACE)
    {
        // if we are on NVMe, but the command comes to here, then someone forced SCSI mode, so let's set this flag
        // correctly
        downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
    }
    DWORD returned_data = DWORD_C(0);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    DECLARE_SEATIMER(commandTimer);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_STORAGE_FIRMWARE_ACTIVATE, &downloadActivate,
                                 sizeof(STORAGE_HW_FIRMWARE_ACTIVATE), M_NULLPTR, 0, &returned_data, &overlappedStruct);
    scsiIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        scsiIoCtx->device->os_info
            .last_error) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                         // flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        scsiIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    // dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
        if (scsiIoCtx->pAtaCmdOpts)
        {
            // set status register to 50
            safe_memset(&scsiIoCtx->pAtaCmdOpts->rtfr, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
            scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
            scsiIoCtx->pAtaCmdOpts->rtfr.secCnt =
                0x02; // This is supposed to be set when the drive has applied the new code.
            // also set sense data with an ATA passthrough return descriptor
            if (scsiIoCtx->senseDataSize >=
                22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01; // check condition
                                                         // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01;  // recovered error
                                              // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2]  = 0x00; // ASC
                scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                scsiIoCtx->psense[4]  = 0;
                scsiIoCtx->psense[5]  = 0;
                scsiIoCtx->psense[6]  = 0;
                scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                scsiIoCtx->psense[8]  = 0x09; // descriptor code
                scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                scsiIoCtx->psense[10] = 0;
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;  // Error
                scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt; // Sector Count
                scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow; // LBA Lo
                scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid; // LBA Mid
                scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;  // LBA Hi
                scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device; // Device/Head
                scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status; // Status
            }
        }
    }
    else
    {
        switch (scsiIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE: // aborted command is the best we can do
            safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
            if (scsiIoCtx->pAtaCmdOpts)
            {
                safe_memset(&scsiIoCtx->pAtaCmdOpts->rtfr, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
                scsiIoCtx->pAtaCmdOpts->rtfr.status =
                    ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ABORT;
                // we need to also set sense data that matches...
                if (scsiIoCtx->senseDataSize >=
                    22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
                {
                    scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
                    scsiIoCtx->returnStatus.senseKey =
                        0x01; // check condition
                              // setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->returnStatus.asc  = 0x00;
                    scsiIoCtx->returnStatus.ascq = 0x1D;
                    // now fill in the sens buffer
                    scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                    scsiIoCtx->psense[1] = 0x01;  // recovered error
                                                  // setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->psense[2]  = 0x00; // ASC
                    scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                    scsiIoCtx->psense[4]  = 0;
                    scsiIoCtx->psense[5]  = 0;
                    scsiIoCtx->psense[6]  = 0;
                    scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                    scsiIoCtx->psense[8]  = 0x09; // descriptor code
                    scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                    scsiIoCtx->psense[10] = 0;
                    // fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;  // Error
                    scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt; // Sector Count
                    scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow; // LBA Lo
                    scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid; // LBA Mid
                    scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;  // LBA Hi
                    scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device; // Device/Head
                    scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status; // Status
                }
            }
            else
            {
                // setting fixed format...
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
                scsiIoCtx->psense[2] = SENSE_KEY_ABORTED_COMMAND;
                scsiIoCtx->psense[7] = 7; // set so that ASC, ASCQ, & FRU are available...even though they are zeros
            }
            break;
        case ERROR_INVALID_FUNCTION:
            // disable the support bits for Win10 FWDL API.
            // The driver said it's supported, but when we try to issue the commands it fails with this status, so try
            // pass-through as we would otherwise use.
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Win 10 FWDL API returned invalid function, retrying with passthrough\n");
            }
            scsiIoCtx->device->os_info.fwdlIOsupport.fwdlIOSupported = false;
            return send_IO(scsiIoCtx);
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
// DO NOT Attempt to use the STORAGE_HW_FIRMWARE_DOWNLOAD_V2 structure! This is not compatible as the low-level driver
// has a hard-coded alignment for the image buffer and will not transmit the correct data!!!
#    if !defined(STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT)
#        define STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT 0x00000002
#    endif
#    if !defined(STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT)
#        define STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT 0x00000004
#    endif

static M_INLINE void safe_free_hwfwdl(PSTORAGE_HW_FIRMWARE_DOWNLOAD* dl)
{
    safe_free_core(M_REINTERPRET_CAST(void**, dl));
}

static eReturnValues win10_FW_Download_IO_SCSI(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret        = OS_PASSTHROUGH_FAILURE;
    uint32_t      dataLength = UINT32_C(0);
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
    // send download IOCTL
    DWORD                         downloadStructureSize = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD) + dataLength;
    PSTORAGE_HW_FIRMWARE_DOWNLOAD downloadIO =
        M_REINTERPRET_CAST(PSTORAGE_HW_FIRMWARE_DOWNLOAD, safe_malloc(downloadStructureSize));
    if (downloadIO == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(downloadIO, downloadStructureSize, 0, downloadStructureSize);
    downloadIO->Version = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD);
    downloadIO->Size    = downloadStructureSize;
#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    if (scsiIoCtx->fwdlLastSegment)
    {
        // This IS documented on MSDN but VS2015 can't seem to find it...
        // One website says that this flag is new in Win10 1704 - creators update (10.0.15021)
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
    }
#    endif
#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    if (scsiIoCtx->fwdlFirstSegment)
    {
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
    }
#    endif
    if (scsiIoCtx->device->drive_info.interface_type == NVME_INTERFACE)
    {
        // if we are on NVMe, but the command comes to here, then someone forced SCSI mode, so let's set this flag
        // correctly
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
    }
    if (scsiIoCtx && !scsiIoCtx->pAtaCmdOpts)
    {
        downloadIO->Slot =
            scsiIoCtx->cdb[2]; // Set the slot number to the buffer ID number...This is the closest this translates.
    }
    // we need to set the offset since MS uses this in the command sent to the device.
    downloadIO->Offset = 0;
    if (scsiIoCtx && scsiIoCtx->pAtaCmdOpts)
    {
        // get offset from the tfrs
        downloadIO->Offset = C_CAST(DWORDLONG, M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaHi,
                                                                   scsiIoCtx->pAtaCmdOpts->tfr.LbaMid)) *
                             LEGACY_DRIVE_SEC_SIZE;
    }
    else if (scsiIoCtx)
    {
        // get offset from the cdb
        downloadIO->Offset = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    }
    else
    {
        safe_free_hwfwdl(&downloadIO);
        return BAD_PARAMETER;
    }
    // set the size of the buffer
    downloadIO->BufferSize = dataLength;
    // now copy the buffer into this IOCTL struct
    safe_memcpy(downloadIO->ImageBuffer, dataLength, scsiIoCtx->pdata, dataLength);
    // time to issue the IO
    DWORD returned_data = DWORD_C(0);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    DECLARE_SEATIMER(commandTimer);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_STORAGE_FIRMWARE_DOWNLOAD, downloadIO,
                                 downloadStructureSize, M_NULLPTR, 0, &returned_data, &overlappedStruct);
    scsiIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        scsiIoCtx->device->os_info
            .last_error) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                         // flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        scsiIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    // dummy up sense data for end result
    if (fwdlIO)
    {
        ret = SUCCESS;
        safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
        if (scsiIoCtx->pAtaCmdOpts)
        {
            // set status register to 50
            safe_memset(&scsiIoCtx->pAtaCmdOpts->rtfr, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
            scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
            if (scsiIoCtx->fwdlLastSegment)
            {
                scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x03; // device has all segments saved and is ready to activate
            }
            else
            {
                scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = 0x01; // device is expecting more code
            }
            // also set sense data with an ATA passthrough return descriptor
            if (scsiIoCtx->senseDataSize >=
                22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01; // check condition
                                                         // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01;  // recovered error
                                              // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2]  = 0x00; // ASC
                scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                scsiIoCtx->psense[4]  = 0;
                scsiIoCtx->psense[5]  = 0;
                scsiIoCtx->psense[6]  = 0;
                scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                scsiIoCtx->psense[8]  = 0x09; // descriptor code
                scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                scsiIoCtx->psense[10] = 0;
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;  // Error
                scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt; // Sector Count
                scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow; // LBA Lo
                scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid; // LBA Mid
                scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;  // LBA Hi
                scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device; // Device/Head
                scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status; // Status
            }
        }
    }
    else
    {
        switch (scsiIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE: // aborted command is the best we can do
            safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
            if (scsiIoCtx->pAtaCmdOpts)
            {
                safe_memset(&scsiIoCtx->pAtaCmdOpts->rtfr, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
                scsiIoCtx->pAtaCmdOpts->rtfr.status =
                    ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                scsiIoCtx->pAtaCmdOpts->rtfr.error = ATA_ERROR_BIT_ABORT;
                // we need to also set sense data that matches...
                if (scsiIoCtx->senseDataSize >=
                    22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
                {
                    scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
                    scsiIoCtx->returnStatus.senseKey =
                        0x01; // check condition
                              // setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->returnStatus.asc  = 0x00;
                    scsiIoCtx->returnStatus.ascq = 0x1D;
                    // now fill in the sens buffer
                    scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                    scsiIoCtx->psense[1] = 0x01;  // recovered error
                                                  // setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->psense[2]  = 0x00; // ASC
                    scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                    scsiIoCtx->psense[4]  = 0;
                    scsiIoCtx->psense[5]  = 0;
                    scsiIoCtx->psense[6]  = 0;
                    scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                    scsiIoCtx->psense[8]  = 0x09; // descriptor code
                    scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                    scsiIoCtx->psense[10] = 0;
                    // fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;  // Error
                    scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt; // Sector Count
                    scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow; // LBA Lo
                    scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid; // LBA Mid
                    scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;  // LBA Hi
                    scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device; // Device/Head
                    scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status; // Status
                }
            }
            else
            {
                // setting fixed format...
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
                scsiIoCtx->psense[2] = SENSE_KEY_ABORTED_COMMAND;
                scsiIoCtx->psense[7] = 7; // set so that ASC, ASCQ, & FRU are available...even though they are zeros
            }
            break;
        case ERROR_INVALID_FUNCTION:
            // disable the support bits for Win10 FWDL API.
            // The driver said it's supported, but when we try to issue the commands it fails with this status, so try
            // pass-through as we would otherwise use.
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Win 10 FWDL API returned invalid function, retrying with passthrough\n");
            }
            scsiIoCtx->device->os_info.fwdlIOsupport.fwdlIOSupported = false;
            safe_free_hwfwdl(&downloadIO);
            return send_IO(scsiIoCtx);
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
    safe_free_hwfwdl(&downloadIO);
    return ret;
}

// call check function above to make sure this api call will actually work...
static eReturnValues windows_Firmware_Download_IO_SCSI(ScsiIoCtx* scsiIoCtx)
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

// Checks if SMART IO is supported and sets some bit flags up for later when issuing those IOs
eReturnValues get_Windows_SMART_IO_Support(tDevice* device)
{
    ULONG              returned_data = ULONG_C(0);
    GETVERSIONINPARAMS smartVersionInfo;
    safe_memset(&smartVersionInfo, sizeof(GETVERSIONINPARAMS), 0, sizeof(GETVERSIONINPARAMS));
    int smartRet = DeviceIoControl(device->os_info.fd, SMART_GET_VERSION, M_NULLPTR, 0, &smartVersionInfo,
                                   sizeof(GETVERSIONINPARAMS), &returned_data, M_NULLPTR);
    // Got the version info, but that doesn't mean we'll be successful with commands...
    if (smartRet)
    {
        DWORD smartError = GetLastError();
        if (!smartError) // if there was an error, then assume the driver does not support this request. - TJE
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
                // TODO: Save driver version info? skipping for now since it doesn't appear useful.-TJE
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

#define INVALID_IOCTL DWORD_C(0xFFFFFFFF)
// returns which IOCTL code we'll use for the specified command
static DWORD io_For_SMART_Cmd(ScsiIoCtx* scsiIoCtx)
{
    if (scsiIoCtx->pAtaCmdOpts->commandType != ATA_CMD_TYPE_TASKFILE)
    {
        return INVALID_IOCTL;
    }
    if (!scsiIoCtx->device->os_info.winSMARTCmdSupport.smartIOSupported)
    {
        return INVALID_IOCTL;
    }
    // this checks to make sure we are issuing a STD spec Identify or SMART command to the drive. Non-standard things
    // won't be supported by the lower level drivers or OS - TJE
    switch (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus)
    {
    case ATA_IDENTIFY:
        // make sure it's standard spec identify! (no lba fields set or feature field set)
        // NOLINTBEGIN(bugprone-branch-clone)
        // This is meant to be evaluated in this order as the SMART IOCTLs do not allow for passing
        // other register values other than zero when issuing an identify
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature != UINT8_C(0) ||
            scsiIoCtx->pAtaCmdOpts->tfr.LbaLow != UINT8_C(0) || scsiIoCtx->pAtaCmdOpts->tfr.LbaMid != UINT8_C(0) ||
            scsiIoCtx->pAtaCmdOpts->tfr.LbaHi != UINT8_C(0))
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
        // NOLINTEND(bugprone-branch-clone)
    case ATAPI_IDENTIFY:
        // make sure it's standard spec identify! (no lba fields set or feature field set)
        // NOLINTBEGIN(bugprone-branch-clone)
        // This is meant to be evaluated in this order as the SMART IOCTLs do not allow for passing
        // other register values other than zero when issuing an identify
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature != UINT8_C(0) ||
            scsiIoCtx->pAtaCmdOpts->tfr.LbaLow != UINT8_C(0) || scsiIoCtx->pAtaCmdOpts->tfr.LbaMid != UINT8_C(0) ||
            scsiIoCtx->pAtaCmdOpts->tfr.LbaHi != UINT8_C(0))
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
        // NOLINTEND(bugprone-branch-clone)
    case ATA_SMART_CMD:
        if (scsiIoCtx->device->os_info.winSMARTCmdSupport.smartSupported)
        {
            // check that the feature field matches something Microsoft documents support for...using MS defines - TJE
            switch (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature)
            {
            case READ_ATTRIBUTES:
            case READ_THRESHOLDS:
            case SMART_READ_LOG:
                return SMART_RCV_DRIVE_DATA;
            case ENABLE_DISABLE_AUTOSAVE:
            case SAVE_ATTRIBUTE_VALUES:
                return SMART_SEND_DRIVE_COMMAND;
            case EXECUTE_OFFLINE_DIAGS: // need to check which test to run!
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

static bool is_ATA_Cmd_Supported_By_SMART_IO(ScsiIoCtx* scsiIoCtx)
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

static eReturnValues convert_SCSI_CTX_To_ATA_SMART_Cmd(ScsiIoCtx* scsiIoCtx, PSENDCMDINPARAMS smartCmd)
{
    if (!is_ATA_Cmd_Supported_By_SMART_IO(scsiIoCtx))
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    // NOLINTBEGIN(bugprone-branch-clone)
    // Seems to be a false-positive match here, so turning off clang-tidy
    if (scsiIoCtx->direction == XFER_DATA_OUT)
    {
        smartCmd->cBufferSize = scsiIoCtx->dataLength;
    }
    else
    {
        smartCmd->cBufferSize = DWORD_C(0);
    }
    // NOLINTEND(bugprone-branch-clone)
    // set up the registers
    smartCmd->irDriveRegs.bFeaturesReg     = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
    smartCmd->irDriveRegs.bSectorCountReg  = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
    smartCmd->irDriveRegs.bSectorNumberReg = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
    smartCmd->irDriveRegs.bCylLowReg       = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
    smartCmd->irDriveRegs.bCylHighReg      = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
    smartCmd->irDriveRegs.bDriveHeadReg    = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
    smartCmd->irDriveRegs.bCommandReg      = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
    smartCmd->irDriveRegs.bReserved        = RESERVED;

    // set device-head bitmask for the driver...see
    // https://msdn.microsoft.com/en-us/library/windows/hardware/ff554977(v=vs.85).aspx
    // NOLINTBEGIN(bugprone-branch-clone)
    // no idea why this is being detected as a clone when it is definitely not.
    // Multiple cleanup methods were attempted with no success - TJE
    if (scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & (BIT0 | BIT2 | BIT4 | BIT6))
    {
        // this is device 0...make sure bit 4 is not set!
        M_CLEAR_BIT8(smartCmd->irDriveRegs.bDriveHeadReg, UINT8_C(4));
    }
    else if (scsiIoCtx->device->os_info.winSMARTCmdSupport.deviceBitmap & (BIT1 | BIT3 | BIT5 | BIT7))
    {
        // this is device 1...make sure bit 4 is set
        smartCmd->irDriveRegs.bDriveHeadReg |= BIT4;
    }
    // NOLINTEND(bugprone-branch-clone)

    smartCmd->bDriveNumber =
        BYTE_C(0); // MSDN says not to set this to anything!
                   // https://msdn.microsoft.com/en-us/library/windows/hardware/ff565401(v=vs.85).aspx

    smartCmd->bReserved[0] = RESERVED;
    smartCmd->bReserved[1] = RESERVED;
    smartCmd->bReserved[2] = RESERVED;

    smartCmd->dwReserved[0] = RESERVED;
    smartCmd->dwReserved[1] = RESERVED;
    smartCmd->dwReserved[2] = RESERVED;
    smartCmd->dwReserved[3] = RESERVED;
    return SUCCESS;
}

static M_INLINE void safe_free_smart_send_cmd_in(SENDCMDINPARAMS** cmd)
{
    safe_free_core(M_REINTERPRET_CAST(void**, cmd));
}

static M_INLINE void safe_free_smart_send_cmd_out(SENDCMDOUTPARAMS** cmd)
{
    safe_free_core(M_REINTERPRET_CAST(void**, cmd));
}

static eReturnValues send_ATA_SMART_Cmd_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = FAILURE;
    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        return OS_COMMAND_NOT_AVAILABLE;
    }
    BOOL     success       = FALSE;
    ULONG    returned_data = ULONG_C(0);
    uint32_t dataInLength  = UINT32_C(1);  // set to one for the minus 1s below
    uint32_t dataOutLength = UINT32_C(1);  // set to one for the minus 1s below
    uint32_t magicPadding  = UINT32_C(16); // This is found through trial and error to get nForce 680i driver working.
    bool     nonDataRTFRs  = false;
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        dataOutLength = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
        break;
    case XFER_DATA_OUT:
        dataInLength = M_Max(scsiIoCtx->dataLength, scsiIoCtx->pAtaCmdOpts->dataSize);
        break;
    case XFER_NO_DATA:
        dataOutLength +=
            sizeof(IDEREGS); // need to add this much to the buffer to get the RTFRs back as the data buffer!
        nonDataRTFRs = true;
        break;
    default:
        break;
    }
    PSENDCMDINPARAMS smartIOin =
        M_REINTERPRET_CAST(PSENDCMDINPARAMS, safe_calloc(1, sizeof(SENDCMDINPARAMS) - 1 + dataInLength));
    if (!smartIOin)
    {
        // something went really wrong...
        return MEMORY_FAILURE;
    }
    PSENDCMDOUTPARAMS smartIOout = M_REINTERPRET_CAST(
        PSENDCMDOUTPARAMS, safe_calloc(1, sizeof(SENDCMDOUTPARAMS) - 1 + dataOutLength + magicPadding));
    if (!smartIOout)
    {
        safe_free_smart_send_cmd_in(&smartIOin);
        // something went really wrong
        return MEMORY_FAILURE;
    }
    DECLARE_SEATIMER(commandTimer);
    ret = convert_SCSI_CTX_To_ATA_SMART_Cmd(scsiIoCtx, smartIOin);
    if (SUCCESS == ret)
    {
        ULONG inBufferLength  = sizeof(SENDCMDINPARAMS);
        ULONG outBufferLength = sizeof(SENDCMDOUTPARAMS);
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            outBufferLength += dataOutLength - 1;
            break;
        case XFER_DATA_OUT:
            // need to copy the data we're sending to the device over!
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(smartIOin->bBuffer, dataInLength, scsiIoCtx->pdata, dataInLength);
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
        SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        start_Timer(&commandTimer);
        success                               = DeviceIoControl(scsiIoCtx->device->os_info.fd,
                                                                io_For_SMART_Cmd(scsiIoCtx), // This function gets the correct IOCTL for us
                                                                smartIOin, inBufferLength, smartIOout, outBufferLength + magicPadding, &returned_data,
                                                                &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING ==
            scsiIoCtx->device->os_info
                .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                             // overlapped flag, everything will work like old synchronous code.-TJE
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
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent        = M_NULLPTR;
        scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
        if (MSFT_BOOL_TRUE(success))
        {
            ret = SUCCESS;
            // copy the data buffer back to the user's data pointer
            if (scsiIoCtx->pdata && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, smartIOout->bBuffer,
                            M_Min(scsiIoCtx->dataLength, smartIOout->cBufferSize));
            }
            // use the format, sensekey, acq, acsq from the sense data buffer we passed in rather than what windows
            // reports...because windows doesn't always match what is in your sense buffer
            scsiIoCtx->returnStatus.senseKey = 0x00;
        }
        else
        {
            switch (scsiIoCtx->device->os_info.last_error)
            {
            case ERROR_ACCESS_DENIED:
                ret = PERMISSION_DENIED;
                break;
            case ERROR_IO_DEVICE: // OS_PASSTHROUGH_FAILURE
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
        scsiIoCtx->returnStatus.asc  = 0x00; // might need to change this later
        scsiIoCtx->returnStatus.ascq = 0x1D; // might need to change this later
        // get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in
        // descriptor format current = 28bit, previous = 48bit
        if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >=
                22) // check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                ataReturnTFRs smartTFRs;
                safe_memset(&smartTFRs, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01; // check condition
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01; // recovered error
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2]  = 0x00; // ASC
                scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                scsiIoCtx->psense[4]  = 0;
                scsiIoCtx->psense[5]  = 0;
                scsiIoCtx->psense[6]  = 0;
                scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                scsiIoCtx->psense[8]  = 0x09; // descriptor code
                scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                scsiIoCtx->psense[10] = 0;
                switch (smartIOout->DriverStatus.bDriverError)
                {
                case SMART_NO_ERROR: // command issued without error
                    smartTFRs.status = 0x50;
                    if (nonDataRTFRs) // only look for RTFRs on a non-data command!
                    {
                        PIDEREGS ptrIDErtfrs = C_CAST(PIDEREGS, smartIOout->bBuffer);
                        smartTFRs.error      = ptrIDErtfrs->bFeaturesReg;
                        smartTFRs.secCnt     = ptrIDErtfrs->bSectorCountReg;
                        smartTFRs.lbaLow     = ptrIDErtfrs->bSectorNumberReg;
                        smartTFRs.lbaMid     = ptrIDErtfrs->bCylLowReg;
                        smartTFRs.lbaHi      = ptrIDErtfrs->bCylHighReg;
                        smartTFRs.device     = ptrIDErtfrs->bDriveHeadReg;
                        smartTFRs.status     = ptrIDErtfrs->bCommandReg;
                    }
                    break;
                case SMART_IDE_ERROR: // command error...status register should get set to 51h
                    smartTFRs.status = 0x51;
                    smartTFRs.error  = smartIOout->DriverStatus.bIDEError;
                    // This should be sufficient since the drive returned some other type of command abort...BUT we may
                    // want to attempt looking for RTFRs to know more.
                    break;
                case SMART_INVALID_FLAG:
                case SMART_INVALID_COMMAND:
                case SMART_INVALID_BUFFER:
                case SMART_INVALID_DRIVE:
                case SMART_INVALID_IOCTL:
                case SMART_ERROR_NO_MEM:
                case SMART_INVALID_REGISTER: // should we return NOT_SUPPORTED here? - TJE
                case SMART_NOT_SUPPORTED:
                case SMART_NO_IDE_DEVICE:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }
                scsiIoCtx->psense[11] = smartTFRs.error;  // Error
                scsiIoCtx->psense[13] = smartTFRs.secCnt; // Sector Count
                scsiIoCtx->psense[15] = smartTFRs.lbaLow; // LBA Lo
                scsiIoCtx->psense[17] = smartTFRs.lbaMid; // LBA Mid
                scsiIoCtx->psense[19] = smartTFRs.lbaHi;  // LBA Hi
                scsiIoCtx->psense[20] = smartTFRs.device; // Device/Head
                scsiIoCtx->psense[21] = smartTFRs.status; // Status
            }
        }
    }
    else if (NOT_SUPPORTED == ret)
    {
        scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_FIXED;
        scsiIoCtx->returnStatus.senseKey = 0x05;
        scsiIoCtx->returnStatus.asc      = 0x20;
        scsiIoCtx->returnStatus.ascq     = 0x00;
        // dummy up sense data
        if (scsiIoCtx->psense != M_NULLPTR)
        {
            safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
            // fill in not supported
            scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_FIXED;
            scsiIoCtx->psense[2] = 0x05;
            // acq
            scsiIoCtx->psense[12] = 0x20; // invalid operation code
            // acsq
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
    safe_free_smart_send_cmd_in(&smartIOin);
    safe_free_smart_send_cmd_out(&smartIOout);
    return ret;
}

eReturnValues os_Device_Reset(tDevice* device)
{
    eReturnValues ret = FAILURE;
    // this IOCTL is only supported for non-scsi devices, which includes anything (ata or scsi) attached to a USB or
    // SCSI or SAS interface This does not seem to work since it is obsolete and likely not implemented in modern
    // drivers use the Windows API call -
    // http://msdn.microsoft.com/en-us/library/windows/hardware/ff560603%28v=vs.85%29.aspx ULONG returned_data =
    // ULONG_C(0);
    BOOL success = FALSE;
    SetLastError(NO_ERROR);
    device->os_info.last_error = NO_ERROR;
    success = DeviceIoControl(device->os_info.fd, OBSOLETE_IOCTL_STORAGE_RESET_DEVICE, M_NULLPTR, 0, M_NULLPTR, 0,
                              M_NULLPTR, FALSE);
    device->os_info.last_error = GetLastError();
    if (MSFT_BOOL_TRUE(success) && device->os_info.last_error == NO_ERROR)
    {
        ret = SUCCESS;
    }
    else
    {
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    return ret;
}

eReturnValues os_Bus_Reset(tDevice* device)
{
    eReturnValues ret = FAILURE;
    // This does not seem to work since it is obsolete and likely not implemented in modern drivers
    // use the Windows API call - http://msdn.microsoft.com/en-us/library/windows/hardware/ff560600%28v=vs.85%29.aspx
    ULONG                     returned_data = ULONG_C(0);
    BOOL                      success       = FALSE;
    STORAGE_BUS_RESET_REQUEST reset;
    safe_memset(&reset, sizeof(STORAGE_BUS_RESET_REQUEST), 0, sizeof(STORAGE_BUS_RESET_REQUEST));
    reset.PathId = device->os_info.scsi_addr.PathId;
    SetLastError(NO_ERROR);
    device->os_info.last_error = NO_ERROR;
    success = DeviceIoControl(device->os_info.fd, OBSOLETE_IOCTL_STORAGE_RESET_BUS, &reset, sizeof(reset), &reset,
                              sizeof(reset), &returned_data, FALSE);
    device->os_info.last_error = GetLastError();
    if (MSFT_BOOL_TRUE(success) && device->os_info.last_error == NO_ERROR)
    {
        ret = SUCCESS;
    }
    else
    {
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    return ret;
}

eReturnValues os_Controller_Reset(M_ATTR_UNUSED tDevice* device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

// TODO: We may need to switch between locking fd and scsiSrbHandle in some way...for now just locking fd value.
// https://docs.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_lock_volume
eReturnValues os_Lock_Device(tDevice* device)
{
    eReturnValues ret           = SUCCESS;
    DWORD         returnedBytes = DWORD_C(0);
    if (!DeviceIoControl(device->os_info.fd, FSCTL_LOCK_VOLUME, M_NULLPTR, 0, M_NULLPTR, 0, &returnedBytes, M_NULLPTR))
    {
        // This can fail is files are open, it's a system disk, or has a pagefile.
        ret = FAILURE;
    }
    return ret;
}

eReturnValues os_Unlock_Device(tDevice* device)
{
    eReturnValues ret           = SUCCESS;
    DWORD         returnedBytes = DWORD_C(0);
    if (!DeviceIoControl(device->os_info.fd, FSCTL_UNLOCK_VOLUME, M_NULLPTR, 0, M_NULLPTR, 0, &returnedBytes,
                         M_NULLPTR))
    {
        ret = FAILURE;
    }
    return ret;
}

eReturnValues os_Unmount_File_Systems_On_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    // If the volume bitfield is blank, then there is nothing to unmount - TJE
    if (device->os_info.volumeBitField > UINT32_C(0))
    {
        // go through each bit in the bitfield. Bit0 = A, BIT1 = B, BIT2 = C, etc
        // unmount each volume for the specified device.
        TCHAR volumeLetter = 'A'; // always start with A
        for (uint32_t volIter = UINT32_C(0); volIter < MAX_VOL_BITS && volumeLetter <= 'Z'; ++volIter, ++volumeLetter)
        {
            if (M_BitN(volIter) & device->os_info.volumeBitField)
            {
                // found a volume.
                // Steps:
                // 1. open handle to the volume
                // 2. lock the volume: FSCTL_LOCK_VOLUME
                // 3. dismount volume: FSCTL_DISMOUNT_VOLUME
                // 4. unlock the volume: FSCTL_UNLOCK_VOLUME
                // 5. close the handle to the volume
                HANDLE volumeHandle  = INVALID_HANDLE_VALUE;
                DWORD  bytesReturned = DWORD_C(0);
                DECLARE_ZERO_INIT_ARRAY(TCHAR, volumeHandleString, MAX_VOL_STR_LEN);
                _sntprintf_s(volumeHandleString, MAX_VOL_STR_LEN, _TRUNCATE, TEXT("\\\\.\\%c:"), volumeLetter);
                volumeHandle = CreateFile(volumeHandleString, GENERIC_WRITE | GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING, 0, M_NULLPTR);
                if (INVALID_HANDLE_VALUE != volumeHandle)
                {
                    BOOL ioctlResult = FALSE;
                    BOOL lockResult  = FALSE;
                    lockResult       = DeviceIoControl(volumeHandle, FSCTL_LOCK_VOLUME, M_NULLPTR, 0, M_NULLPTR, 0,
                                                       &bytesReturned, M_NULLPTR);
                    if (MSFT_BOOL_FALSE(lockResult))
                    {
                        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
                        {
                            _tprintf_s(TEXT("WARNING: Unable to lock volume: %s\n"), volumeHandleString);
                        }
                    }
                    ioctlResult = DeviceIoControl(volumeHandle, FSCTL_DISMOUNT_VOLUME, M_NULLPTR, 0, M_NULLPTR, 0,
                                                  &bytesReturned, M_NULLPTR);
                    if (MSFT_BOOL_FALSE(ioctlResult))
                    {
                        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
                        {
                            _tprintf_s(TEXT("Error: Unable to dismount volume: %s\n"), volumeHandleString);
                        }
                        ret = FAILURE;
                    }
                    if (MSFT_BOOL_TRUE(lockResult))
                    {
                        ioctlResult = DeviceIoControl(volumeHandle, FSCTL_UNLOCK_VOLUME, M_NULLPTR, 0, M_NULLPTR, 0,
                                                      &bytesReturned, M_NULLPTR);
                        if (MSFT_BOOL_FALSE(ioctlResult))
                        {
                            if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
                            {
                                _tprintf_s(TEXT("WARNING: Unable to unlock volume: %s\n"), volumeHandleString);
                            }
                        }
                    }
                    CloseHandle(volumeHandle);
                    volumeHandle = INVALID_HANDLE_VALUE;
                }
            }
        }
    }
    return ret;
}

static void wbst_Set_Sense_Data(ScsiIoCtx* scsiIoCtx, bool valid, uint8_t senseKey, uint8_t asc, uint8_t ascq)
{
    if (scsiIoCtx->psense && scsiIoCtx->senseDataSize > 0)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, 18);
        if (valid)
        {
            senseData[0] = 0x70;
            // sense key
            senseData[2] |= M_Nibble0(senseKey);
            // additional sense length
            senseData[7] = 10;
            // asc
            senseData[12] = asc;
            // ascq
            senseData[13] = ascq;
            if (scsiIoCtx->senseDataSize < 14)
            {
                // sense data overflows the available buffer
                senseData[2] |= BIT4;
            }
        }
        safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, senseData, M_Min(18, scsiIoCtx->senseDataSize));
    }
}

static eReturnValues wbst_Inquiry(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        // Check to make sure cmdDT and reserved bits aren't set
        if (scsiIoCtx->cdb[1] & 0xFE)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            // check EVPD bit
            if (scsiIoCtx->cdb[1] & BIT0)
            {
                DECLARE_ZERO_INIT_ARRAY(
                    uint8_t, vpdPage,
                    96); // this should be more than big enough for the pages supported on this basic device - TJE
                uint8_t                    peripheralDevice = UINT8_C(0);
                PSTORAGE_DEVICE_DESCRIPTOR deviceDesc = M_NULLPTR; // needed for scsi device type on all VPD pages - TJE
                vpdPage[0]                            = peripheralDevice;
                if (SUCCESS == win_Get_Device_Descriptor(scsiIoCtx->device->os_info.fd, &deviceDesc))
                {
                    uint8_t vpdPageLen = UINT8_C(0);
                    // set device type again from descriptor data
                    vpdPage[0] = deviceDesc->DeviceType;
                    // check the VPD page to set up that data correctly
                    //       the vpd pagecodes below are probably the only ones that could be implemented with the
                    //       limited capabilities we have at this point
                    switch (scsiIoCtx->cdb[2])
                    {
                    case SUPPORTED_VPD_PAGES:
                        vpdPage[4 + vpdPageLen] = SUPPORTED_VPD_PAGES;
                        vpdPageLen++;
                        vpdPage[4 + vpdPageLen] = UNIT_SERIAL_NUMBER;
                        vpdPageLen++;
                        vpdPage[3] = vpdPageLen;
                        break;
                    case UNIT_SERIAL_NUMBER:
                        vpdPage[1] = UNIT_SERIAL_NUMBER;
                        if (deviceDesc->RawPropertiesLength > 0)
                        {
                            const char* devSerial =
                                C_CAST(char*, deviceDesc->RawDeviceProperties + deviceDesc->SerialNumberOffset);
                            if (deviceDesc->SerialNumberOffset && deviceDesc->SerialNumberOffset != UINT32_MAX)
                            {
                                safe_memcpy(&vpdPage[4], 96 - 4, devSerial,
                                            M_Min(safe_strlen(devSerial),
                                                  92)); // 92 for maximum size of current remaining memory for this page
                                vpdPageLen = vpdPage[3] = C_CAST(uint8_t, M_Min(safe_strlen(devSerial), 92));
                            }
                            else
                            {
                                vpdPageLen = vpdPage[3] = 18;
                                vpdPage[4]              = ' ';
                                vpdPage[5]              = ' ';
                                vpdPage[6]              = ' ';
                                vpdPage[7]              = ' ';
                                vpdPage[8]              = ' ';
                                vpdPage[9]              = ' ';
                                vpdPage[10]             = ' ';
                                vpdPage[11]             = ' ';
                                vpdPage[12]             = ' ';
                                vpdPage[13]             = ' ';
                                vpdPage[14]             = ' ';
                                vpdPage[15]             = ' ';
                                vpdPage[16]             = ' ';
                                vpdPage[17]             = ' ';
                                vpdPage[18]             = ' ';
                                vpdPage[19]             = ' ';
                                vpdPage[20]             = ' ';
                                vpdPage[21]             = ' ';
                            }
                        }
                        else
                        {
                            vpdPageLen = vpdPage[3] = 18;
                            vpdPage[4]              = ' ';
                            vpdPage[5]              = ' ';
                            vpdPage[6]              = ' ';
                            vpdPage[7]              = ' ';
                            vpdPage[8]              = ' ';
                            vpdPage[9]              = ' ';
                            vpdPage[10]             = ' ';
                            vpdPage[11]             = ' ';
                            vpdPage[12]             = ' ';
                            vpdPage[13]             = ' ';
                            vpdPage[14]             = ' ';
                            vpdPage[15]             = ' ';
                            vpdPage[16]             = ' ';
                            vpdPage[17]             = ' ';
                            vpdPage[18]             = ' ';
                            vpdPage[19]             = ' ';
                            vpdPage[20]             = ' ';
                            vpdPage[21]             = ' ';
                        }
                        break;
                    case DEVICE_IDENTIFICATION:
                    case BLOCK_LIMITS:
                    case BLOCK_DEVICE_CHARACTERISTICS:
                    default:
                        // invalid field in CDB
                        senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                        asc          = 0x24;
                        ascq         = 0x00;
                        setSenseData = true;
                        break;
                    }
                    if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
                    {
                        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, vpdPage,
                                    M_Min(96U, M_Min(vpdPageLen + 4U, scsiIoCtx->dataLength)));
                    }
                    safe_free_device_descriptor(&deviceDesc);
                }
                else
                {
                    // aborted command. TODO: Use windows errors to set more accurate sense data
                    senseKey     = SENSE_KEY_ABORTED_COMMAND;
                    asc          = 0x00;
                    ascq         = 0x00;
                    setSenseData = true;
                }
            }
            else
            {
                // standard inquiry data - use device descriptor data
                DECLARE_ZERO_INIT_ARRAY(uint8_t, inquiryData, 96);
                uint8_t                    peripheralDevice = UINT8_C(0);
                PSTORAGE_DEVICE_DESCRIPTOR deviceDesc       = M_NULLPTR;
                if (scsiIoCtx->cdb[2] == 0)
                {
                    inquiryData[0] = peripheralDevice;
                    inquiryData[2] = 0x05; // SPC3
                    // response format
                    inquiryData[3] = 2;
                    // additional length
                    inquiryData[3] = 92;
                    if (SUCCESS == win_Get_Device_Descriptor(scsiIoCtx->device->os_info.fd, &deviceDesc))
                    {
                        // set device type again from descriptor data
                        inquiryData[0] = deviceDesc->DeviceType;
                        if (deviceDesc->RemovableMedia)
                        {
                            inquiryData[1] |= BIT7;
                        }
                        if (deviceDesc->RawPropertiesLength > 0)
                        {
                            char* devVendor =
                                C_CAST(char*, deviceDesc->RawDeviceProperties + deviceDesc->VendorIdOffset);
                            char* devModel =
                                C_CAST(char*, deviceDesc->RawDeviceProperties + deviceDesc->ProductIdOffset);
                            char* devRev =
                                C_CAST(char*, deviceDesc->RawDeviceProperties + deviceDesc->ProductRevisionOffset);
                            char* devSerial =
                                C_CAST(char*, deviceDesc->RawDeviceProperties + deviceDesc->SerialNumberOffset);
                            if (deviceDesc->VendorIdOffset && deviceDesc->VendorIdOffset != UINT32_MAX)
                            {
                                safe_memset(&inquiryData[8], 96 - 8, ' ',
                                            8); // space pad first as spec says ASCII data should be space padded
                                safe_memcpy(&inquiryData[8], 96 - 8, devVendor,
                                            M_Min(safe_strlen(devVendor), 8)); // maximum of 8 characters in length
                            }
                            else
                            {
                                // unknown vendor ID
                                inquiryData[8]  = 'U';
                                inquiryData[9]  = 'N';
                                inquiryData[10] = 'K';
                                inquiryData[11] = 'N';
                                inquiryData[12] = 'O';
                                inquiryData[13] = 'W';
                                inquiryData[14] = 'N';
                                inquiryData[15] = ' ';
                            }
                            if (deviceDesc->ProductIdOffset && deviceDesc->ProductIdOffset != UINT32_MAX)
                            {
                                safe_memset(&inquiryData[16], 96 - 16, ' ',
                                            16); // space pad first as spec says ASCII data should be space padded
                                safe_memcpy(&inquiryData[16], 96 - 16, devModel, M_Min(safe_strlen(devModel), 16));
                            }
                            else
                            {
                                inquiryData[16] = 'U';
                                inquiryData[17] = 'N';
                                inquiryData[18] = 'K';
                                inquiryData[19] = 'N';
                                inquiryData[20] = 'O';
                                inquiryData[21] = 'W';
                                inquiryData[22] = 'N';
                                inquiryData[23] = ' ';
                                inquiryData[24] = ' ';
                                inquiryData[25] = ' ';
                                inquiryData[26] = ' ';
                                inquiryData[27] = ' ';
                                inquiryData[28] = ' ';
                                inquiryData[29] = ' ';
                                inquiryData[30] = ' ';
                                inquiryData[31] = ' ';
                            }
                            if (deviceDesc->ProductRevisionOffset && deviceDesc->ProductRevisionOffset != UINT32_MAX)
                            {
                                safe_memset(&inquiryData[32], 96 - 32, ' ',
                                            4); // space pad first as spec says ASCII data should be space padded
                                safe_memcpy(&inquiryData[32], 96 - 32, devRev, M_Min(safe_strlen(devRev), 4));
                            }
                            else
                            {
                                inquiryData[32] = 'F';
                                inquiryData[33] = 'A';
                                inquiryData[34] = 'K';
                                inquiryData[35] = 'E';
                            }
                            // SN is not described in spec to be here, but it is listed as "Vendor Specific", so we're
                            // putting it here since it is a fairly common practice overall. - TJE
                            if (deviceDesc->SerialNumberOffset && deviceDesc->SerialNumberOffset != UINT32_MAX)
                            {
                                safe_memcpy(&inquiryData[36], 96 - 36, devSerial, M_Min(safe_strlen(devSerial), 20));
                            }
                        }
                        else
                        {
                            // did not return the properties. Set "Unknown" for vendor and nothing else.
                            // vendorID
                            inquiryData[8]  = 'U';
                            inquiryData[9]  = 'N';
                            inquiryData[10] = 'K';
                            inquiryData[11] = 'N';
                            inquiryData[12] = 'O';
                            inquiryData[13] = 'W';
                            inquiryData[14] = 'N';
                            inquiryData[15] = ' ';
                            inquiryData[16] = 'N';
                            inquiryData[17] = 'O';
                            inquiryData[18] = 'T';
                            inquiryData[19] = ' ';
                            inquiryData[20] = 'A';
                            inquiryData[21] = 'V';
                            inquiryData[22] = 'A';
                            inquiryData[23] = 'I';
                            inquiryData[24] = 'L';
                            inquiryData[25] = 'A';
                            inquiryData[26] = 'B';
                            inquiryData[27] = 'L';
                            inquiryData[28] = 'E';
                            inquiryData[29] = ' ';
                            inquiryData[30] = ' ';
                            inquiryData[31] = ' ';
                            inquiryData[32] = 'F';
                            inquiryData[33] = 'A';
                            inquiryData[34] = 'K';
                            inquiryData[35] = 'E';
                        }
                        safe_free_device_descriptor(&deviceDesc);
                    }
                    else
                    {
                        // Nothing came back, so fill in with emptyness...just generate a response of any kind.
                        // vendorID
                        inquiryData[8]  = 'U';
                        inquiryData[9]  = 'N';
                        inquiryData[10] = 'K';
                        inquiryData[11] = 'N';
                        inquiryData[12] = 'O';
                        inquiryData[13] = 'W';
                        inquiryData[14] = 'N';
                        inquiryData[15] = ' ';
                    }
                    if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
                    {
                        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, inquiryData,
                                    M_Min(96, scsiIoCtx->dataLength));
                    }
                }
                else
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read_Capacity_10(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 10)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (scsiIoCtx->cdb[1] != 0 || scsiIoCtx->cdb[2] != 0 || scsiIoCtx->cdb[3] != 0 || scsiIoCtx->cdb[4] != 0 ||
            scsiIoCtx->cdb[5] != 0 || scsiIoCtx->cdb[6] != 0 || scsiIoCtx->cdb[7] != 0 || scsiIoCtx->cdb[8] != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            // device geometry IOCTLs for remaining data
            DECLARE_ZERO_INIT_ARRAY(uint8_t, readCapacityData, READ_CAPACITY_10_LEN);
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN2K
            PDISK_GEOMETRY_EX    geometryEx = M_NULLPTR;
            PDISK_PARTITION_INFO partition  = M_NULLPTR;
            PDISK_DETECTION_INFO detection  = M_NULLPTR;
            if (SUCCESS ==
                win_Get_Drive_Geometry_Ex(scsiIoCtx->device->os_info.fd, &geometryEx, &partition, &detection))
            {
                if ((geometryEx->DiskSize.QuadPart / geometryEx->Geometry.BytesPerSector) > UINT32_MAX)
                {
                    readCapacityData[0] = 0xFF;
                    readCapacityData[1] = 0xFF;
                    readCapacityData[2] = 0xFF;
                    readCapacityData[3] = 0xFF;
                }
                else
                {
                    readCapacityData[0] = M_Byte3(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                  C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[1] = M_Byte2(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                  C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[2] = M_Byte1(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                  C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[3] = M_Byte0(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                  C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                }
                readCapacityData[4] = M_Byte3(geometryEx->Geometry.BytesPerSector);
                readCapacityData[5] = M_Byte2(geometryEx->Geometry.BytesPerSector);
                readCapacityData[6] = M_Byte1(geometryEx->Geometry.BytesPerSector);
                readCapacityData[7] = M_Byte0(geometryEx->Geometry.BytesPerSector);
                safe_free_disk_geometry_ex(&geometryEx);
            }
            else
#endif // WINVER >= SEA_WIN32_WINNT_WIN2K
            {
                PDISK_GEOMETRY geometry = M_NULLPTR;
                if (SUCCESS == win_Get_Drive_Geometry(scsiIoCtx->device->os_info.fd, &geometry))
                {
                    uint64_t maxLBA = C_CAST(uint64_t, geometry->Cylinders.QuadPart) * geometry->TracksPerCylinder *
                                      geometry->SectorsPerTrack;
                    if (maxLBA > UINT32_MAX)
                    {
                        readCapacityData[0] = 0xFF;
                        readCapacityData[1] = 0xFF;
                        readCapacityData[2] = 0xFF;
                        readCapacityData[3] = 0xFF;
                    }
                    else
                    {
                        readCapacityData[0] = M_Byte3(maxLBA);
                        readCapacityData[1] = M_Byte2(maxLBA);
                        readCapacityData[2] = M_Byte1(maxLBA);
                        readCapacityData[3] = M_Byte0(maxLBA);
                    }
                    readCapacityData[4] = M_Byte3(geometry->BytesPerSector);
                    readCapacityData[5] = M_Byte2(geometry->BytesPerSector);
                    readCapacityData[6] = M_Byte1(geometry->BytesPerSector);
                    readCapacityData[7] = M_Byte0(geometry->BytesPerSector);
                    safe_free_disk_geometry(&geometry);
                }
                else
                {
                    // set aborted command. Something really went wrong
                    senseKey     = SENSE_KEY_ABORTED_COMMAND;
                    asc          = 0x00;
                    ascq         = 0x00;
                    setSenseData = true;
                }
            }
            if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readCapacityData,
                            M_Min(READ_CAPACITY_10_LEN, scsiIoCtx->dataLength));
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read_Capacity_16(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength >= 16)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        // first check the service action
        if (get_bit_range_uint8(scsiIoCtx->cdb[1], 4, 0) == 0x10)
        {
            uint32_t allocationLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
            // bytes 2 - 9 are not allowed, same with 14
            if (scsiIoCtx->cdb[14] != 0 || scsiIoCtx->cdb[2] != 0 || scsiIoCtx->cdb[3] != 0 || scsiIoCtx->cdb[4] != 0 ||
                scsiIoCtx->cdb[5] != 0 || scsiIoCtx->cdb[6] != 0 || scsiIoCtx->cdb[7] != 0 || scsiIoCtx->cdb[8] != 0 ||
                scsiIoCtx->cdb[9] != 0)
            {
                // invalid field in CDB
                senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                asc          = 0x24;
                ascq         = 0x00;
                setSenseData = true;
            }
            else
            {
                // device geometry IOCTLs for remaining data
                // If Win version great enough, use the access alignment descriptor for block sizes and alignment
                // requirements
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_VISTA
                PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR accessAlignment = M_NULLPTR;
#endif // WINVER >= SEA_WIN32_WINNT_VISTA
                DECLARE_ZERO_INIT_ARRAY(uint8_t, readCapacityData, READ_CAPACITY_16_LEN);
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_WIN2K
                PDISK_GEOMETRY_EX    geometryEx = M_NULLPTR;
                PDISK_PARTITION_INFO partition  = M_NULLPTR;
                PDISK_DETECTION_INFO detection  = M_NULLPTR;
                if (SUCCESS ==
                    win_Get_Drive_Geometry_Ex(scsiIoCtx->device->os_info.fd, &geometryEx, &partition, &detection))
                {
                    readCapacityData[0]  = M_Byte7(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[1]  = M_Byte6(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[2]  = M_Byte5(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[3]  = M_Byte4(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[4]  = M_Byte3(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[5]  = M_Byte2(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[6]  = M_Byte1(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[7]  = M_Byte0(C_CAST(uint64_t, geometryEx->DiskSize.QuadPart) /
                                                   C_CAST(uint64_t, geometryEx->Geometry.BytesPerSector));
                    readCapacityData[8]  = M_Byte3(geometryEx->Geometry.BytesPerSector);
                    readCapacityData[9]  = M_Byte2(geometryEx->Geometry.BytesPerSector);
                    readCapacityData[10] = M_Byte1(geometryEx->Geometry.BytesPerSector);
                    readCapacityData[11] = M_Byte0(geometryEx->Geometry.BytesPerSector);
                    // don't set any other fields unless we can get the access alignment descriptor
                    safe_free_disk_geometry_ex(&geometryEx);
                }
                else
#endif // WINVER >= SEA_WIN32_WINNT_WIN2K
                {
                    PDISK_GEOMETRY geometry = M_NULLPTR;
                    if (SUCCESS == win_Get_Drive_Geometry(scsiIoCtx->device->os_info.fd, &geometry))
                    {
                        uint64_t maxLBA = C_CAST(uint64_t, geometry->Cylinders.QuadPart) * geometry->TracksPerCylinder *
                                          geometry->SectorsPerTrack;
                        readCapacityData[0]  = M_Byte7(maxLBA);
                        readCapacityData[1]  = M_Byte6(maxLBA);
                        readCapacityData[2]  = M_Byte5(maxLBA);
                        readCapacityData[3]  = M_Byte4(maxLBA);
                        readCapacityData[4]  = M_Byte3(maxLBA);
                        readCapacityData[5]  = M_Byte2(maxLBA);
                        readCapacityData[6]  = M_Byte1(maxLBA);
                        readCapacityData[7]  = M_Byte0(maxLBA);
                        readCapacityData[8]  = M_Byte3(geometry->BytesPerSector);
                        readCapacityData[9]  = M_Byte2(geometry->BytesPerSector);
                        readCapacityData[10] = M_Byte1(geometry->BytesPerSector);
                        readCapacityData[11] = M_Byte0(geometry->BytesPerSector);
                        safe_free_disk_geometry(&geometry);
                    }
                    else
                    {
                        // set aborted command. Something really went wrong
                        senseKey     = SENSE_KEY_ABORTED_COMMAND;
                        asc          = 0x00;
                        ascq         = 0x00;
                        setSenseData = true;
                    }
                }
#if defined(WINVER) && WINVER >= SEA_WIN32_WINNT_VISTA
                if (SUCCESS == win_Get_Access_Alignment_Descriptor(scsiIoCtx->device->os_info.fd, &accessAlignment))
                {
                    if (accessAlignment->BytesPerLogicalSector >
                        0) // making sure we don't divide by zero on bad reports from the system
                    {
                        uint8_t  logicalBlockPerPhysicalExponent = UINT8_C(0);
                        uint16_t lowestAlignedLBA = C_CAST(uint16_t, accessAlignment->BytesOffsetForSectorAlignment /
                                                                         accessAlignment->BytesPerLogicalSector);
                        // convert physical sector size to power of 2
                        switch (accessAlignment->BytesPerPhysicalSector / accessAlignment->BytesPerLogicalSector)
                        {
                        case 1:
                            logicalBlockPerPhysicalExponent = 0;
                            break;
                        case 2:
                            logicalBlockPerPhysicalExponent = 1;
                            break;
                        case 4:
                            logicalBlockPerPhysicalExponent = 2;
                            break;
                        case 8:
                            logicalBlockPerPhysicalExponent = 3;
                            break;
                        case 16:
                            logicalBlockPerPhysicalExponent = 4;
                            break;
                        default:
                            // unknown, do nothing.
                            break;
                        }
                        readCapacityData[13] = logicalBlockPerPhysicalExponent;
                        readCapacityData[14] =
                            M_Byte1(lowestAlignedLBA) &
                            0x3F; // shouldn't cause a problem as alignment shouldn't be higher than this
                        readCapacityData[15] = M_Byte0(lowestAlignedLBA);
                    }
                    safe_free_storage_access_alignment(&accessAlignment);
                }
#endif // WINVER >= SEA_WIN32_WINNT_VISTA
                if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
                {
                    safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readCapacityData,
                                M_Min(READ_CAPACITY_16_LEN, allocationLength));
                }
            }
        }
        else
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read(ScsiIoCtx* scsiIoCtx, uint64_t lba, bool fua, uint32_t transferLength)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->device && scsiIoCtx->pdata && transferLength > 0 && scsiIoCtx->dataLength > 0 &&
        (transferLength * scsiIoCtx->device->drive_info.deviceBlockSize) == scsiIoCtx->dataLength)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (fua)
        {
            ret = os_Verify(scsiIoCtx->device, lba, transferLength);
        }
        if (ret == SUCCESS)
        {
            ret = os_Read(scsiIoCtx->device, lba, 0, scsiIoCtx->pdata,
                          (transferLength * scsiIoCtx->device->drive_info.deviceBlockSize));
        }
        if (ret != SUCCESS)
        {
            // aborted command. TODO: Use windows errors to set more accurate sense data
            senseKey     = SENSE_KEY_ABORTED_COMMAND;
            asc          = 0x00;
            ascq         = 0x00;
            setSenseData = true;
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read_6(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba = M_BytesTo4ByteValue(0, (scsiIoCtx->cdb[1] & 0x1F), scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
            uint32_t transferLength = scsiIoCtx->cdb[4];
            if (transferLength == 0)
            {
                transferLength = 256;
            }
            ret = wbst_Read(scsiIoCtx, lba, false, transferLength);
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read_10(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 10)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        bool    fua          = false;
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if ((scsiIoCtx->cdb[1] &
             BIT0) // reladr bit. Obsolete.
                   //|| (scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || (scsiIoCtx->cdb[1] & BIT2) // cannot support RACR bit in this translation since we cannot do fpdma
            || (get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            uint32_t transferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            if (transferLength != 0) // if zero, do nothing
            {
                if (transferLength > 65536)
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Read(scsiIoCtx, lba, fua, transferLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read_12(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 12)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        bool    fua          = false;
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if ((scsiIoCtx->cdb[1] &
             BIT0) // reladr bit. Obsolete.
                   //|| (scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || (scsiIoCtx->cdb[1] & BIT2) // cannot support RACR bit in this translation since we cannot do fpdma
            || (get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            uint32_t transferLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            if (transferLength != 0) // if zero, do nothing
            {
                if (transferLength > 65536)
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Read(scsiIoCtx, lba, fua, transferLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Read_16(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 16)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        bool    fua          = false;
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        // sbc2 fua_nv bit can be ignored according to SAT.
        // We don't support RARC since was cannot do FPDMA in software SAT
        // We don't support DLD bits either
        if ((scsiIoCtx->cdb[1] &
             BIT0) // reladr bit. Obsolete.
                   //|| (scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || (scsiIoCtx->cdb[1] & BIT2) // cannot support RACR bit in this translation since we cannot do fpdma
            || (get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba =
                M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                    scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            uint32_t transferLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
            if (transferLength != 0) // if zero, do nothing
            {
                if (transferLength > 65536)
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Read(scsiIoCtx, lba, fua, transferLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Write(ScsiIoCtx* scsiIoCtx, uint64_t lba, bool fua, uint32_t transferLength)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->device && scsiIoCtx->pdata && transferLength > 0 && scsiIoCtx->dataLength > 0 &&
        (transferLength * scsiIoCtx->device->drive_info.deviceBlockSize) == scsiIoCtx->dataLength)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        ret                  = os_Write(scsiIoCtx->device, lba, false, scsiIoCtx->pdata,
                                        (transferLength * scsiIoCtx->device->drive_info.deviceBlockSize));
        if (fua && ret == SUCCESS)
        {
            ret = os_Verify(scsiIoCtx->device, lba, transferLength);
        }
        if (ret != SUCCESS)
        {
            // aborted command. TODO: Use windows errors to set more accurate sense data
            senseKey     = SENSE_KEY_ABORTED_COMMAND;
            asc          = 0x00;
            ascq         = 0x00;
            setSenseData = true;
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Write_6(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba = M_BytesTo4ByteValue(0, (scsiIoCtx->cdb[1] & 0x1F), scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
            uint32_t transferLength = scsiIoCtx->cdb[4];
            if (transferLength == 0) // write 6, zero means a maximum possible transfer size, which is 256
            {
                transferLength = 256;
            }
            if (transferLength > 65536)
            {
                // invalid field in CDB
                senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                asc          = 0x24;
                ascq         = 0x00;
                setSenseData = true;
            }
            else
            {
                ret = wbst_Write(scsiIoCtx, lba, false, transferLength);
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Write_10(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 10)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        bool    fua          = false;
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if ((scsiIoCtx->cdb[1] &
             BIT0) // reladr bit. Obsolete.
                   //|| (scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || (scsiIoCtx->cdb[1] & BIT2) // reserved bit
            || (get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            uint32_t transferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            if (transferLength != 0) // 0 length, means do nothing
            {
                if (transferLength > 65536)
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Write(scsiIoCtx, lba, fua, transferLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Write_12(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 12)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        bool    fua          = false;
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if ((scsiIoCtx->cdb[1] &
             BIT0) // reladr bit. Obsolete.
                   //|| (scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || (scsiIoCtx->cdb[1] & BIT2) // reserved bit
            || (get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            uint32_t transferLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            if (transferLength != 0) // 0 length, means do nothing
            {
                if (transferLength > 65536)
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Write(scsiIoCtx, lba, fua, transferLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Write_16(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 16)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        bool    fua          = false;
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        // sbc2 fua_nv bit can be ignored according to SAT.
        // We don't support DLD bits either
        if ((scsiIoCtx->cdb[1] &
             BIT0) // reladr bit. Obsolete. also now the DLD2 bit
                   //|| (scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || (scsiIoCtx->cdb[1] & BIT2) // reserved bit
            || (get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint64_t lba =
                M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                    scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            uint32_t transferLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
            if (transferLength != 0) // 0 length, means do nothing
            {
                if (transferLength > 65536)
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Write(scsiIoCtx, lba, fua, transferLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Verify(ScsiIoCtx* scsiIoCtx, uint64_t lba, uint32_t verificationLength)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->device && verificationLength > 0)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        ret                  = os_Verify(scsiIoCtx->device, lba, verificationLength);
        if (ret != SUCCESS)
        {
            // aborted command. TODO: Use windows errors to set more accurate sense data
            senseKey     = SENSE_KEY_ABORTED_COMMAND;
            asc          = 0x00;
            ascq         = 0x00;
            setSenseData = true;
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Verify_10(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 10)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if ((scsiIoCtx->cdb[1] & BIT3) || (scsiIoCtx->cdb[1] & BIT0) ||
            (get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint8_t  byteCheck = get_bit_range_uint8(scsiIoCtx->cdb[1], 2, 1);
            uint64_t lba =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            uint32_t verificationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            if (verificationLength != 0) // this is allowed and it means to validate inputs and return success
            {
                if (verificationLength > 65536 || byteCheck != 0) // limit transfer size, and only support a normal
                                                                  // verify. Not supporting compare commands - TJE
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Verify(scsiIoCtx, lba, verificationLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Verify_12(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 12)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if ((scsiIoCtx->cdb[1] & BIT3) || (scsiIoCtx->cdb[1] & BIT0) ||
            (get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint8_t  byteCheck = get_bit_range_uint8(scsiIoCtx->cdb[1], 2, 1);
            uint64_t lba =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            uint32_t verificationLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            if (verificationLength != 0) // this is allowed and it means to validate inputs and return success
            {
                if (verificationLength > 65536 || byteCheck != 0) // limit transfer size, and only support a normal
                                                                  // verify. Not supporting compare commands - TJE
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Verify(scsiIoCtx, lba, verificationLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Verify_16(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 16)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if ((scsiIoCtx->cdb[1] & BIT3) || (scsiIoCtx->cdb[1] & BIT0) ||
            (get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            uint8_t  byteCheck = get_bit_range_uint8(scsiIoCtx->cdb[1], 2, 1);
            uint64_t lba =
                M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                    scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            uint32_t verificationLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
            if (verificationLength != 0) // this is allowed and it means to validate inputs and return success
            {
                if (verificationLength > 65536 || byteCheck != 0) // limit transfer size, and only support a normal
                                                                  // verify. Not supporting compare commands - TJE
                {
                    // invalid field in CDB
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x24;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    ret = wbst_Verify(scsiIoCtx, lba, verificationLength);
                }
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Synchronize_Cache_10(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 10)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (scsiIoCtx->cdb[1] != 0 || scsiIoCtx->cdb[6] != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            ret = os_Flush(scsiIoCtx->device);
            if (ret != SUCCESS)
            {
                // aborted command. TODO: Use windows errors to set more accurate sense data
                senseKey     = SENSE_KEY_ABORTED_COMMAND;
                asc          = 0x00;
                ascq         = 0x00;
                setSenseData = true;
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Synchronize_Cache_16(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 16)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (scsiIoCtx->cdb[1] != 0 || scsiIoCtx->cdb[14] != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            ret = os_Flush(scsiIoCtx->device);
            if (ret != SUCCESS)
            {
                // aborted command. TODO: Use windows errors to set more accurate sense data
                senseKey     = SENSE_KEY_ABORTED_COMMAND;
                asc          = 0x00;
                ascq         = 0x00;
                setSenseData = true;
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Test_Unit_Ready(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (scsiIoCtx->cdb[1] != 0 || scsiIoCtx->cdb[2] != 0 || scsiIoCtx->cdb[3] != 0 || scsiIoCtx->cdb[4] != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            // Do nothing. Just return ready unless we find some IO we can send that does a TUR type of check - TJE
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Request_Sense(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        if (scsiIoCtx->cdb[1] != 0 || scsiIoCtx->cdb[2] != 0 || scsiIoCtx->cdb[3] != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            // Do nothing. Return good sense (all zeros) to the call since there is nothing that can really be checked
            // here.
            if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
            {
                explicit_zeroes(scsiIoCtx->pdata, M_Min(scsiIoCtx->cdb[4], scsiIoCtx->dataLength));
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Send_Diagnostic(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        // only allow self-test bit set to one, and return good status.
        if (scsiIoCtx->cdb[1] & 0xFB  // only allow self-test bit to be set to 1. All others are not supported.
            || scsiIoCtx->cdb[2] != 0 // reserved
            || scsiIoCtx->cdb[3] != 0 // parameter list length
            || scsiIoCtx->cdb[4] != 0 // parameter list length
        )
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            // regardless of the self test bit being 1 or 0, return good status since there is nothing that can be done
            // right here
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues wbst_Report_Luns(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 12)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        // filter out unsupported fields first
        if (scsiIoCtx->cdb[1] != 0 || scsiIoCtx->cdb[3] != 0 || scsiIoCtx->cdb[4] != 0 || scsiIoCtx->cdb[5] != 0 ||
            scsiIoCtx->cdb[10] != 0)
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x24;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, reportLunsData, REPORT_LUNS_MIN_LENGTH);
            uint32_t allocationLength =
                M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            switch (scsiIoCtx->cdb[2])
            {
            case 0x00:
                // set list length to 16 bytes
                reportLunsData[0] = M_Byte3(REPORT_LUNS_MIN_LENGTH);
                reportLunsData[1] = M_Byte2(REPORT_LUNS_MIN_LENGTH);
                reportLunsData[2] = M_Byte1(REPORT_LUNS_MIN_LENGTH);
                reportLunsData[3] = M_Byte0(REPORT_LUNS_MIN_LENGTH);
                // set lun to zero since it's zero indexed
                reportLunsData[15] = 0;
                if (scsiIoCtx->pdata)
                {
                    safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, reportLunsData,
                                M_Min(REPORT_LUNS_MIN_LENGTH, allocationLength));
                }
                break;
            case 0x01:
            case 0x02:
            case 0x10:
            case 0x11:
            case 0x12:
                // nothing to report, so just copy back the data buffer as it is
                if (scsiIoCtx->pdata)
                {
                    safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, reportLunsData,
                                M_Min(REPORT_LUNS_MIN_LENGTH, allocationLength));
                }
                break;
            default:
                // logical unit not supported
                senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                asc          = 0x25;
                ascq         = 0x00;
                setSenseData = true;
                break;
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// TODO: This can be broken down into other functions to make this shorter and easier to follow.
//       Since this is extremely unlikely to be used as "basic" devices are not very common, it is left like this for
//       now-TJE
static eReturnValues wbst_Format_Unit(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength == 6)
    {
        uint8_t senseKey     = UINT8_C(0);
        uint8_t asc          = UINT8_C(0);
        uint8_t ascq         = UINT8_C(0);
        bool    setSenseData = false;
        // NOTE: There is a format IOCTL not implemented, but it is likely only for floppy drives.
        //       It may be worth implementing if they ever return from beyond the grave...or if we can test and prove it
        //       works on HDDs
        // Ideally this is a nop and it returns that it's ready without actually doing anything
        if (get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 6) || scsiIoCtx->cdb[1] & BIT3 || scsiIoCtx->cdb[2] ||
            scsiIoCtx->cdb[3] || scsiIoCtx->cdb[4])
        {
            // invalid field in CDB
            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
            asc          = 0x20;
            ascq         = 0x00;
            setSenseData = true;
        }
        else
        {
            bool    longList         = scsiIoCtx->cdb[1] & BIT5;
            bool    formatData       = scsiIoCtx->cdb[1] & BIT4;
            uint8_t defectListFormat = get_bit_range_uint8(scsiIoCtx->cdb[1], 2, 0);
            if (formatData && scsiIoCtx->pdata && scsiIoCtx->dataLength > 4)
            {
                // Parameter header information
                // uint8_t protectionFieldUsage = get_bit_range_uint8(scsiIoCtx->pdata[0], 2, 0);
                bool formatOptionsValid = scsiIoCtx->pdata[1] & BIT7;
                // bool disablePrimary = scsiIoCtx->pdata[1] & BIT6; //ignore this bit. Commented out so we can use it
                // if we ever need to.
                bool     disableCertification        = scsiIoCtx->pdata[1] & BIT5;
                bool     stopFormat                  = scsiIoCtx->pdata[1] & BIT4;
                bool     initializationPattern       = scsiIoCtx->pdata[1] & BIT3;
                uint8_t  initializationPatternOffset = UINT8_C(0);
                bool     disableSaveParameters       = scsiIoCtx->pdata[1] & BIT2; // long obsolete
                bool     immediate                   = scsiIoCtx->pdata[1] & BIT1;
                bool     vendorSpecific              = scsiIoCtx->pdata[1] & BIT0;
                uint8_t  p_i_information             = UINT8_C(0);
                uint8_t  protectionIntervalExponent  = UINT8_C(0);
                uint32_t defectListLength            = UINT32_C(0);
                if (longList && scsiIoCtx->dataLength > 8)
                {
                    p_i_information            = M_Nibble1(scsiIoCtx->pdata[3]);
                    protectionIntervalExponent = M_Nibble0(scsiIoCtx->pdata[3]);
                    defectListLength           = M_BytesTo4ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5],
                                                                     scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                    if (initializationPattern)
                    {
                        initializationPatternOffset = 8;
                    }
                }
                else
                {
                    defectListLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
                    if (initializationPattern)
                    {
                        initializationPatternOffset = 4;
                    }
                }
                // check parameter data
                if (/*check all combinations that we don't support*/
                    (scsiIoCtx->pdata[0] !=
                     0) // we don't support protection information and reserved bytes should also be zeroed out
                    ||
                    (formatOptionsValid && stopFormat) // Doesn't make sense to support since we cannot accept a defect
                                                       // list. We could also ignore this, but this should be fine
                    || (disableSaveParameters)         // check that this obsolete bit isn't set
                    || (immediate) // we cannot support the immediate bit in this implementation right now. We would
                                   // need multi-threading to do this...
                    || (vendorSpecific)                       // We don't have a vendor specific functionality in here
                    || (longList && scsiIoCtx->pdata[2] != 0) // checking if reserved byte in long header is set
                    || (longList && p_i_information != 0) || (longList && protectionIntervalExponent != 0) ||
                    ((defectListFormat == 0 || defectListFormat == 6) &&
                     defectListLength != 0)                             // See SAT spec CDB field definitions
                    || (defectListFormat != 0 && defectListFormat != 6) // See SAT spec CDB field definitions
                )
                {
                    // invalid field in parameter list
                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                    asc          = 0x26;
                    ascq         = 0x00;
                    setSenseData = true;
                }
                else
                {
                    // need to check a few more combinations. This code will NOT support certify unlike the SATL
                    // software translation code to simplify this a bit more. - TJE Check FOV bit combinations
                    if (formatOptionsValid)
                    {
                        bool performWriteOperation   = false;
                        bool performCertifyOperation = false;
                        // Initialization pattern information. Check that initializationPattern bool is set to true!
                        bool    securityInitialize = false;
                        uint8_t initializationPatternModifier =
                            0; // This is obsolete since SBC2...we will just return an error
                        uint8_t initializationPatternByte0ReservedBits =
                            0; // so we can make sure no invalid field was set.
                        uint8_t  initializationPatternType   = UINT8_C(0);
                        uint16_t initializationPatternLength = UINT16_C(0);
                        uint8_t* initializationPatternPtr    = M_NULLPTR;
                        if (initializationPattern)
                        {
                            // Set up the initialization pattern information since we were given one
                            initializationPatternModifier =
                                get_bit_range_uint8(scsiIoCtx->pdata[initializationPatternOffset + 0], 7, 6);
                            securityInitialize = scsiIoCtx->pdata[initializationPatternOffset + 1] & BIT5;
                            initializationPatternByte0ReservedBits =
                                get_bit_range_uint8(scsiIoCtx->pdata[initializationPatternOffset + 0], 4, 0);
                            initializationPatternType = scsiIoCtx->pdata[initializationPatternOffset + 1];
                            initializationPatternLength =
                                M_BytesTo2ByteValue(scsiIoCtx->pdata[initializationPatternOffset + 2],
                                                    scsiIoCtx->pdata[initializationPatternOffset + 3]);
                            if (initializationPatternLength > 0)
                            {
                                initializationPatternPtr = &scsiIoCtx->pdata[initializationPatternOffset + 4];
                            }
                        }
                        // Check bit combinations to make sure we can do things the right way!
                        if (disableCertification && !initializationPattern)
                        {
                            // return good status and do nothing else
                        }
                        else if (!initializationPattern && !disableCertification)
                        {
                            // SATL may or may not support media certification. If not supported, then return invalid
                            // field in parameter list...otherwise we have stuff to do
                            performCertifyOperation = true;
                        }
                        else if (!disableCertification && initializationPattern)
                        {
                            // SATL may or may not support media certification
                            performCertifyOperation = true;
                            // SATL may or may not support write operation described by the initialization pattern
                            // descriptor to perform this translation
                            performWriteOperation = true;
                        }
                        else if (disableCertification && initializationPattern)
                        {
                            // SATL may or may not support write operation described by the initialization pattern
                            // descriptor to perform this translation
                            performWriteOperation = true;
                        }
                        // Before we begin writing or certification, we need to check some flags to make sure nothing
                        // invalid is set.
                        if ((initializationPattern &&       // this must be set for us to care about any of these other
                                                            // fields...
                             (initializationPatternModifier // check if obsolete bits are set
                              || securityInitialize /*not supporting this since we cannot write to the reallocated
                                                       sectors on the drive like this asks*/
                              || (initializationPatternByte0ReservedBits != 0) ||
                              ((initializationPatternType != 0x00 && initializationPatternType != 0x01)))) ||
                            performCertifyOperation // certify operation is not supported in this translation, only
                                                    // writes. -TJE
                        )
                        {
                            // invalid field in parameter list
                            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                            asc          = 0x26;
                            ascq         = 0x00;
                            setSenseData = true;
                        }
                        else
                        {
                            // If we need to do a write operation, we need to do it first!
                            if (performWriteOperation)
                            {
                                // make sure the initialization pattern is less than or equal to the logical block
                                // length
                                if (initializationPatternLength > scsiIoCtx->device->drive_info.deviceBlockSize)
                                {
                                    // invalid field in parameter list
                                    senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                                    asc          = 0x26;
                                    ascq         = 0x00;
                                    setSenseData = true;
                                }
                                else
                                {
                                    uint32_t writeSectors64K =
                                        UINT32_C(65535) / scsiIoCtx->device->drive_info.deviceBlockSize;
                                    // Write commands
                                    uint32_t writeDataLength =
                                        writeSectors64K * scsiIoCtx->device->drive_info.deviceBlockSize;
                                    uint8_t* writePattern = C_CAST(
                                        uint8_t*, safe_calloc_aligned(writeDataLength, sizeof(uint8_t),
                                                                      scsiIoCtx->device->os_info.minimumAlignment));
                                    if (writePattern)
                                    {
                                        uint32_t numberOfLBAs =
                                            writeDataLength / scsiIoCtx->device->drive_info.deviceBlockSize;
                                        if (initializationPatternLength > 0)
                                        {
                                            // copy the provided pattern into our buffer
                                            for (uint32_t copyIter = UINT32_C(0); copyIter < writeDataLength;
                                                 copyIter += scsiIoCtx->device->drive_info.deviceBlockSize)
                                            {
                                                safe_memcpy(&writePattern[copyIter], writeDataLength - copyIter,
                                                            initializationPatternPtr, initializationPatternLength);
                                            }
                                        }
                                        for (uint64_t lba = UINT64_C(0);
                                             lba < scsiIoCtx->device->drive_info.deviceMaxLba; lba += numberOfLBAs)
                                        {
                                            if ((lba + numberOfLBAs) > scsiIoCtx->device->drive_info.deviceMaxLba)
                                            {
                                                // end of range...don't over do it!
                                                numberOfLBAs =
                                                    C_CAST(uint32_t, scsiIoCtx->device->drive_info.deviceMaxLba - lba);
                                                writeDataLength =
                                                    numberOfLBAs * scsiIoCtx->device->drive_info.deviceBlockSize;
                                            }
                                            ret =
                                                os_Write(scsiIoCtx->device, lba, false, writePattern, writeDataLength);
                                            if (ret != SUCCESS)
                                            {
                                                break;
                                            }
                                        }
                                        if (ret != SUCCESS)
                                        {
                                            // invalid field in parameter list
                                            senseKey     = SENSE_KEY_MEDIUM_ERROR;
                                            asc          = 0x03;
                                            ascq         = 0x00;
                                            setSenseData = true;
                                        }
                                        safe_free_aligned(&writePattern);
                                    }
                                    else
                                    {
                                        // Fatal Error!
                                        ret = FAILURE;
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        // Not checking disablePrimary below because the SATL ignores this bit
                        if (!initializationPattern && !disableCertification)
                        {
                            // return good status and do nothing else
                        }
                        else
                        {
                            // invalid field in parameter list
                            senseKey     = SENSE_KEY_ILLEGAL_REQUEST;
                            asc          = 0x26;
                            ascq         = 0x00;
                            setSenseData = true;
                        }
                    }
                }
            }
            else
            {
                // just return success. This is similar to SAT translation, which influenced this decision
            }
        }
        wbst_Set_Sense_Data(scsiIoCtx, setSenseData, senseKey, asc, ascq);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// This basic translation is for odd or old devices that aren't supporting passthrough.
// It is set to be more basic, while still fulfilling some required SCSI commands to make
// sure that upper layer code is happy with what this is reporting.
// This may be able to be expanded, but don't count on it too much.
// Expansion could come in VPD pages, maybe mode pages (read only most likely). But that is more than needed for now.
static eReturnValues win_Basic_SCSI_Translation(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx && scsiIoCtx->cdbLength >= 6) // 6byte CDB is shortest allowed
    {
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case INQUIRY_CMD:
            ret = wbst_Inquiry(scsiIoCtx);
            break;
        case READ_CAPACITY_10:
            ret = wbst_Read_Capacity_10(scsiIoCtx);
            break;
        case READ_CAPACITY_16:
            ret = wbst_Read_Capacity_16(scsiIoCtx);
            break;
        case READ6:
            ret = wbst_Read_6(scsiIoCtx);
            break;
        case READ10:
            ret = wbst_Read_10(scsiIoCtx);
            break;
        case READ12:
            ret = wbst_Read_12(scsiIoCtx);
            break;
        case READ16:
            ret = wbst_Read_16(scsiIoCtx);
            break;
        case WRITE6:
            ret = wbst_Write_6(scsiIoCtx);
            break;
        case WRITE10:
            ret = wbst_Write_10(scsiIoCtx);
            break;
        case WRITE12:
            ret = wbst_Write_12(scsiIoCtx);
            break;
        case WRITE16:
            ret = wbst_Write_16(scsiIoCtx);
            break;
        case VERIFY10:
            ret = wbst_Verify_10(scsiIoCtx);
            break;
        case VERIFY12:
            ret = wbst_Verify_12(scsiIoCtx);
            break;
        case VERIFY16:
            ret = wbst_Verify_16(scsiIoCtx);
            break;
        case SYNCHRONIZE_CACHE_10:
            ret = wbst_Synchronize_Cache_10(scsiIoCtx);
            break;
        case SYNCHRONIZE_CACHE_16_CMD:
            ret = wbst_Synchronize_Cache_16(scsiIoCtx);
            break;
        case TEST_UNIT_READY_CMD:
            ret = wbst_Test_Unit_Ready(scsiIoCtx);
            break;
        case REQUEST_SENSE_CMD:
            ret = wbst_Request_Sense(scsiIoCtx);
            break;
        case SEND_DIAGNOSTIC_CMD:
            ret = wbst_Send_Diagnostic(scsiIoCtx);
            break;
        case REPORT_LUNS_CMD:
            ret = wbst_Report_Luns(scsiIoCtx);
            break;
        case SCSI_FORMAT_UNIT_CMD:
            ret = wbst_Format_Unit(scsiIoCtx);
            break;
        default:
            // invalid operation code
            wbst_Set_Sense_Data(scsiIoCtx, true, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0x00);
            break;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// \return SUCCESS - pass, !SUCCESS fail or something went wrong
eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("Sending command with send_IO\n");
    }
#if WINVER >= SEA_WIN32_WINNT_WINBLUE && defined(IOCTL_SCSI_MINIPORT_FIRMWARE)
    if (is_Firmware_Download_Command_Compatible_With_Win_API(scsiIoCtx))
    {
        if (scsiIoCtx->device->os_info.fwdlMiniportSupported)
        {
            ret = windows_Firmware_Download_IO_SCSI_Miniport(scsiIoCtx);
        }
#    if WINVER >= SEA_WIN32_WINNT_WIN10
        else
        {
            ret = windows_Firmware_Download_IO_SCSI(scsiIoCtx);
        }
#    endif // WINVER >= SEA_WIN32_WINNT_WIN10
    }
    else
    {
#endif // WINVER >= SEA_WIN32_WINNT_WINBLUE
        switch (scsiIoCtx->device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
            // Note: This code is problematic for ATAPI devices. Windows just passes any CDB through to the device,
            // which may not be what we want when we want identify packet device data or other things like it.
            if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_ATA_PASSTHROUGH)
            {
                if (scsiIoCtx->pAtaCmdOpts)
                {
                    ret = send_ATA_Pass_Through_IO(scsiIoCtx);
                }
                else if (scsiIoCtx->device->drive_info.drive_type == ATAPI_DRIVE)
                {
                    // ATAPI drives talk with SCSI commands...hopefully this works! If this doesn't work, then we will
                    // need to write some other function to check the request and call the Win API. - TJE
                    ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
                }
                else
                {
                    ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
                }
            }
            else if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_SCSI_PASSTHROUGH ||
                     scsiIoCtx->device->os_info.ioType ==
                         WIN_IOCTL_SCSI_PASSTHROUGH_EX) // we need this else because we CAN issue scsi pass through
                                                        // commands to an ata interface drive; Windows returns dummied
                                                        // up data for things like inquiry and read capacity which we do
                                                        // send occasionally, so do not remove this.
            {
                if (scsiIoCtx->device->drive_info.drive_type == ATAPI_DRIVE ||
                    scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_12 ||
                    scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_16)
                {
                    ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
                }
                else // not letting the low level translate other things since many of the translations are wrong or
                     // fail to report errors - TJE
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
                // In this case, we want to use the SMART IO over the IDE pass-through whenever we can since it has
                // better support. - TJE
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
        case NVME_INTERFACE: // If on this path for NVMe, then let the driver do SCSI translation
        case SCSI_INTERFACE:
            if (scsiIoCtx->device->os_info.ioType == WIN_IOCTL_BASIC)
            {
                ret = win_Basic_SCSI_Translation(scsiIoCtx);
            }
            else
            {
                ret = send_SCSI_Pass_Through_IO(scsiIoCtx);
            }
            break;
        case RAID_INTERFACE:
            if (scsiIoCtx->device->issue_io != M_NULLPTR)
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
    if (scsiIoCtx->device->delay_io)
    {
        delay_Milliseconds(scsiIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= scsiIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", scsiIoCtx->device->delay_io);
        }
    }
    return ret;
}

#if WINVER >= SEA_WIN32_WINNT_WIN10
/*
    MS Windows treats specification commands different from Vendor Unique Commands.
*/
#    define NVME_ERROR_ENTRY_LENGTH 64
static eReturnValues send_NVMe_Vendor_Unique_IO(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret                     = SUCCESS;
    uint32_t      nvmePassthroughDataSize = nvmeIoCtx->dataSize + sizeof(STORAGE_PROTOCOL_COMMAND) +
                                       STORAGE_PROTOCOL_COMMAND_LENGTH_NVME + NVME_ERROR_ENTRY_LENGTH;
    if (nvmeIoCtx->commandDirection == XFER_DATA_IN_OUT || nvmeIoCtx->commandDirection == XFER_DATA_OUT_IN)
    {
        // assuming bidirectional commands have the same amount of data transferring in each direction
        nvmePassthroughDataSize += nvmeIoCtx->dataSize;
    }
    uint8_t* commandBuffer = C_CAST(uint8_t*, _aligned_malloc(nvmePassthroughDataSize, 8));
    if (!commandBuffer)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(commandBuffer, nvmePassthroughDataSize, 0, nvmePassthroughDataSize);

    // Setup the storage protocol command structure.

    PSTORAGE_PROTOCOL_COMMAND protocolCommand = C_CAST(PSTORAGE_PROTOCOL_COMMAND, commandBuffer);

    protocolCommand->Version       = STORAGE_PROTOCOL_STRUCTURE_VERSION;
    protocolCommand->Length        = sizeof(STORAGE_PROTOCOL_COMMAND);
    protocolCommand->ProtocolType  = ProtocolTypeNvme;
    protocolCommand->ReturnStatus  = 0;
    protocolCommand->ErrorCode     = 0;
    protocolCommand->CommandLength = STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;
    if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
    {
        protocolCommand->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_ADMIN_COMMAND;
        protocolCommand->Flags           = STORAGE_PROTOCOL_COMMAND_FLAG_ADAPTER_REQUEST;
        nvmeAdminCommand* command        = C_CAST(nvmeAdminCommand*, &protocolCommand->Command);
        safe_memcpy(command, sizeof(nvmeAdminCommand), &nvmeIoCtx->cmd.adminCmd, STORAGE_PROTOCOL_COMMAND_LENGTH_NVME);
    }
    else
    {
        protocolCommand->CommandSpecific = STORAGE_PROTOCOL_SPECIFIC_NVME_NVM_COMMAND;
        nvmCommand* command              = C_CAST(nvmCommand*, &protocolCommand->Command);
        safe_memcpy(command, sizeof(nvmCommand), &nvmeIoCtx->cmd.nvmCmd, STORAGE_PROTOCOL_COMMAND_LENGTH_NVME);
    }

    // Save error info? Seems to be from NVMe error log
    protocolCommand->ErrorInfoLength = NVME_ERROR_ENTRY_LENGTH;
    protocolCommand->ErrorInfoOffset =
        FIELD_OFFSET(STORAGE_PROTOCOL_COMMAND, Command) + STORAGE_PROTOCOL_COMMAND_LENGTH_NVME;

    switch (nvmeIoCtx->commandDirection)
    {
    case XFER_DATA_IN:
        protocolCommand->DataToDeviceTransferLength   = 0;
        protocolCommand->DataFromDeviceTransferLength = nvmeIoCtx->dataSize;
        protocolCommand->DataToDeviceBufferOffset     = 0;
        protocolCommand->DataFromDeviceBufferOffset =
            protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength;
        break;
    case XFER_DATA_OUT:
        protocolCommand->DataToDeviceTransferLength   = nvmeIoCtx->dataSize;
        protocolCommand->DataFromDeviceTransferLength = 0;
        protocolCommand->DataToDeviceBufferOffset = protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength;
        protocolCommand->DataFromDeviceBufferOffset = 0;
        // copy the data we're sending into this structure to send to the device
        if (nvmeIoCtx->ptrData)
        {
            safe_memcpy(&commandBuffer[protocolCommand->DataToDeviceBufferOffset], nvmeIoCtx->dataSize,
                        nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
        break;
    case XFER_NO_DATA:
        protocolCommand->DataToDeviceTransferLength   = 0;
        protocolCommand->DataFromDeviceTransferLength = 0;
        protocolCommand->DataToDeviceBufferOffset     = 0;
        protocolCommand->DataFromDeviceBufferOffset   = 0;
        break;
    default: // Bi-directional transfers are not supported in NVMe right now.
        protocolCommand->DataToDeviceTransferLength   = nvmeIoCtx->dataSize;
        protocolCommand->DataFromDeviceTransferLength = nvmeIoCtx->dataSize;
        protocolCommand->DataToDeviceBufferOffset = protocolCommand->ErrorInfoOffset + protocolCommand->ErrorInfoLength;
        protocolCommand->DataFromDeviceBufferOffset = protocolCommand->ErrorInfoOffset +
                                                      protocolCommand->ErrorInfoLength +
                                                      protocolCommand->DataToDeviceTransferLength;
        // copy the data we're sending into this structure to send to the device
        if (nvmeIoCtx->ptrData)
        {
            safe_memcpy(&commandBuffer[protocolCommand->DataToDeviceBufferOffset], nvmeIoCtx->dataSize,
                        nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
        break;
    }

    if (nvmeIoCtx->timeout > WIN_MAX_CMD_TIMEOUT_SECONDS ||
        nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > WIN_MAX_CMD_TIMEOUT_SECONDS)
    {
        safe_free_aligned(&commandBuffer);
        return OS_TIMEOUT_TOO_LARGE;
    }

    if (nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > nvmeIoCtx->timeout)
    {
        protocolCommand->TimeOutValue = nvmeIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (nvmeIoCtx->timeout != 0)
        {
            protocolCommand->TimeOutValue = nvmeIoCtx->timeout;
        }
        else
        {
            protocolCommand->TimeOutValue = 15;
        }
    }

    // Command has been set up, so send it!
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    nvmeIoCtx->device->os_info.last_error = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    DECLARE_SEATIMER(commandTimer);
    DWORD returned_data = DWORD_C(0);
    start_Timer(&commandTimer);
    BOOL success = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_PROTOCOL_COMMAND, commandBuffer,
                                   nvmePassthroughDataSize, commandBuffer, nvmePassthroughDataSize, &returned_data,
                                   &overlappedStruct);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        nvmeIoCtx->device->os_info
            .last_error) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                         // flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
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
        if (nvmeIoCtx->commandDirection != XFER_DATA_OUT && protocolCommand->DataFromDeviceBufferOffset != 0 &&
            nvmeIoCtx->ptrData)
        {
            safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize,
                        &commandBuffer[protocolCommand->DataFromDeviceBufferOffset], nvmeIoCtx->dataSize);
        }
    }

#    if defined(_DEBUG)
    if (protocolCommand->ErrorInfoOffset > 0)
    {
        uint64_t errorCount = M_BytesTo8ByteValue(
            commandBuffer[protocolCommand->ErrorInfoOffset + 7], commandBuffer[protocolCommand->ErrorInfoOffset + 6],
            commandBuffer[protocolCommand->ErrorInfoOffset + 5], commandBuffer[protocolCommand->ErrorInfoOffset + 4],
            commandBuffer[protocolCommand->ErrorInfoOffset + 3], commandBuffer[protocolCommand->ErrorInfoOffset + 2],
            commandBuffer[protocolCommand->ErrorInfoOffset + 1], commandBuffer[protocolCommand->ErrorInfoOffset + 0]);
        uint16_t submissionQueueID      = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 9],
                                                              commandBuffer[protocolCommand->ErrorInfoOffset + 8]);
        uint16_t commandID              = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 11],
                                                              commandBuffer[protocolCommand->ErrorInfoOffset + 10]);
        uint16_t statusField            = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 13],
                                                              commandBuffer[protocolCommand->ErrorInfoOffset + 12]);
        uint16_t parameterErrorLocation = M_BytesTo2ByteValue(commandBuffer[protocolCommand->ErrorInfoOffset + 15],
                                                              commandBuffer[protocolCommand->ErrorInfoOffset + 14]);
        uint64_t lba                    = M_BytesTo8ByteValue(
            commandBuffer[protocolCommand->ErrorInfoOffset + 23], commandBuffer[protocolCommand->ErrorInfoOffset + 22],
            commandBuffer[protocolCommand->ErrorInfoOffset + 21], commandBuffer[protocolCommand->ErrorInfoOffset + 20],
            commandBuffer[protocolCommand->ErrorInfoOffset + 19], commandBuffer[protocolCommand->ErrorInfoOffset + 18],
            commandBuffer[protocolCommand->ErrorInfoOffset + 17], commandBuffer[protocolCommand->ErrorInfoOffset + 16]);
        uint32_t nsid = M_BytesTo4ByteValue(
            commandBuffer[protocolCommand->ErrorInfoOffset + 27], commandBuffer[protocolCommand->ErrorInfoOffset + 26],
            commandBuffer[protocolCommand->ErrorInfoOffset + 25], commandBuffer[protocolCommand->ErrorInfoOffset + 24]);
        uint8_t  vendorSpecific  = commandBuffer[protocolCommand->ErrorInfoOffset + 28];
        uint64_t commandSpecific = M_BytesTo8ByteValue(
            commandBuffer[protocolCommand->ErrorInfoOffset + 39], commandBuffer[protocolCommand->ErrorInfoOffset + 38],
            commandBuffer[protocolCommand->ErrorInfoOffset + 37], commandBuffer[protocolCommand->ErrorInfoOffset + 36],
            commandBuffer[protocolCommand->ErrorInfoOffset + 35], commandBuffer[protocolCommand->ErrorInfoOffset + 34],
            commandBuffer[protocolCommand->ErrorInfoOffset + 33], commandBuffer[protocolCommand->ErrorInfoOffset + 32]);
        if (errorCount > 0)
        {
            printf("Win 10 VU IO Error Info:\n");
            printf("\tError Count: %" PRIu64 "\n", errorCount);
            printf("\tSQID: %" PRIu16 "\n", submissionQueueID);
            printf("\tCID: %" PRIu16 "\n", commandID);
            printf("\tStatus: %" PRIu16 "\n", statusField);
            printf("\tParameterErrorLocation: %" PRIu16 "\n", parameterErrorLocation);
            printf("\tLBA: %" PRIu64 "\n", lba);
            printf("\tNSID: %" PRIu32 "\n", nsid);
            printf("\tVU: %" PRIX8 "\n", vendorSpecific);
            printf("\tCommand Specific: %" PRIX64 "\n", commandSpecific);
        }
    }
#    endif

    // not checking this today since we have the raw drive error response to use instead.
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
    // get the error return code
    nvmeIoCtx->commandCompletionData.commandSpecific = protocolCommand->FixedProtocolReturnData;
    nvmeIoCtx->commandCompletionData.dw0Valid        = true;
    nvmeIoCtx->commandCompletionData.statusAndCID    = protocolCommand->ErrorCode;
    nvmeIoCtx->commandCompletionData.dw3Valid        = true;
    // set last command time
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    // check how long it took to set timeout error if necessary
    if (get_Seconds(commandTimer) > protocolCommand->TimeOutValue)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    _aligned_free(commandBuffer);
    commandBuffer = M_NULLPTR;
    return ret;
}

static eReturnValues win10_Translate_Identify_Active_Namespace_ID_List(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = SUCCESS;
    // return invalid namespace or format if NSID >= FFFFFFFEh or FFFFFFFFh
    // CNTID is not used. If non-zero, return an error for invalid field in cmd???
    if (nvmeIoCtx->cmd.adminCmd.nsid >= 0xFFFFFFFE)
    {
        // dummy up invalid field in CMD status
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 0x0B);
        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = 0;
    }
    else if (get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 8) > 0 || nvmeIoCtx->cmd.adminCmd.cdw11 ||
             nvmeIoCtx->cmd.adminCmd.cdw12 || nvmeIoCtx->cmd.adminCmd.cdw13 || nvmeIoCtx->cmd.adminCmd.cdw14 ||
             nvmeIoCtx->cmd.adminCmd.cdw15)
    {
        // invalid field in cmd...assuming that this should be filtered since it is not translatable!
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 2);
        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = UINT64_C(0);
    }
    else
    {
        // NOTE: Need to only return NSID's that are greater than the requested NSID, so will need to filter report LUNS
        // output - TJE report LUNs uses 64bits for each lun. NVMe uses 32bits for each NSID. This command describes
        // 1024 NSIDs, so we can request the whole thing, then translate/filter as needed - TJE
        uint32_t reportLunsDataSize = UINT32_C(16) + (UINT32_C(1024) * UINT32_C(8)); // 131072B
        uint8_t* reportLunsData =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(reportLunsDataSize, sizeof(uint8_t),
                                                             nvmeIoCtx->device->os_info.minimumAlignment));
        if (reportLunsData)
        {
            safe_memset(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, 0, nvmeIoCtx->dataSize);
            if (SUCCESS == (ret = scsi_Report_Luns(nvmeIoCtx->device, 0, reportLunsDataSize, reportLunsData)))
            {
                // Win10 follows SCSI translation and reports LUNs starting at zero, so for each LUN in the list, add 1
                // to get a NSID. - TJE
                uint32_t lunListLength =
                    M_BytesTo4ByteValue(reportLunsData[0], reportLunsData[1], reportLunsData[2], reportLunsData[3]);
                // now go through the list and for each NSID greater than the provided NSID in the command, add it to
                // the return data (reported as LUNs so add 1!)
                for (uint32_t lunOffset = UINT32_C(8), nsidOffset = UINT32_C(0);
                     lunOffset < (UINT32_C(8) + lunListLength) && nsidOffset < nvmeIoCtx->dataSize &&
                     lunOffset < reportLunsDataSize;
                     lunOffset += UINT32_C(8))
                {
                    uint64_t lun = M_BytesTo8ByteValue(
                        reportLunsData[lunOffset + UINT32_C(0)], reportLunsData[lunOffset + UINT32_C(1)],
                        reportLunsData[lunOffset + UINT32_C(2)], reportLunsData[lunOffset + UINT32_C(3)],
                        reportLunsData[lunOffset + UINT32_C(4)], reportLunsData[lunOffset + UINT32_C(5)],
                        reportLunsData[lunOffset + UINT32_C(6)], reportLunsData[lunOffset + UINT32_C(7)]);
                    if ((lun + UINT64_C(1)) > nvmeIoCtx->cmd.adminCmd.nsid)
                    {
                        // nvme uses little endian, so get this correct!
                        nvmeIoCtx->ptrData[nsidOffset + 0] = M_Byte3(lun + UINT64_C(1));
                        nvmeIoCtx->ptrData[nsidOffset + 1] = M_Byte2(lun + UINT64_C(1));
                        nvmeIoCtx->ptrData[nsidOffset + 2] = M_Byte1(lun + UINT64_C(1));
                        nvmeIoCtx->ptrData[nsidOffset + 3] = M_Byte0(lun + UINT64_C(1));
                        nsidOffset += UINT32_C(4);
                    }
                }
                print_Data_Buffer(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, false);
            }
            else
            {
                ret = OS_PASSTHROUGH_FAILURE;
            }
            safe_free_aligned(&reportLunsData);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}

#    if !defined(NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE)
#        define NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE 0
#    endif

#    if !defined(NVME_IDENTIFY_CNS_CONTROLLER)
#        define NVME_IDENTIFY_CNS_CONTROLLER 1
#    endif

#    if !defined(NVME_IDENTIFY_CNS_ACTIVE_NAMESPACES)
#        define NVME_IDENTIFY_CNS_ACTIVE_NAMESPACES 2
#    endif

static eReturnValues send_Win_NVMe_Identify_Cmd(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret      = SUCCESS;
    uint8_t       cnsValue = M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw10);
    if (cnsValue == 2)
    {
        // need to issue SCSI translation report LUNs and dummy the result back up to the caller
        return win10_Translate_Identify_Active_Namespace_ID_List(nvmeIoCtx);
    }
    else if (cnsValue >= 3)
    {
        // NOTE: MSFT has changed their APIs a few times. This could change in the future!
        return OS_COMMAND_NOT_AVAILABLE;
    }
    else
    {
        BOOL  result         = FALSE;
        PVOID buffer         = M_NULLPTR;
        ULONG bufferLength   = ULONG_C(0);
        ULONG returnedLength = ULONG_C(0);

        PSTORAGE_PROPERTY_QUERY         query        = M_NULLPTR;
        PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = M_NULLPTR;
        // PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = M_NULLPTR;

        //
        // Allocate buffer for use.
        //
        bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) +
                       sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + NVME_IDENTIFY_DATA_LEN;
        buffer = safe_malloc(bufferLength);

        if (buffer == M_NULLPTR)
        {
#    if defined(_DEBUG)
            printf("%s: allocate buffer failed, exit", __FUNCTION__);
#    endif
            return MEMORY_FAILURE;
        }

        /*
            Initialize query data structure to get Identify Controller Data.
        */
        ZeroMemory(buffer, bufferLength);

        query = C_CAST(PSTORAGE_PROPERTY_QUERY, buffer);
        // protocolDataDescr = C_CAST(PSTORAGE_PROTOCOL_DATA_DESCRIPTOR, buffer);
        protocolData = C_CAST(PSTORAGE_PROTOCOL_SPECIFIC_DATA, query->AdditionalParameters);

        // check that the rest of dword 10 is zero!
        if ((nvmeIoCtx->cmd.adminCmd.cdw10 >> 8) != 0)
        {
            // these bytes are reserved in NVMe 1.2 which is the highest MS supports right now. - TJE
            safe_free(&buffer);
            return OS_COMMAND_NOT_AVAILABLE;
        }

        switch (cnsValue)
        {
        case 0: // for the specified namespace. If nsid = UINT32_MAX it's for all namespaces
            query->PropertyId                      = StorageDeviceProtocolSpecificProperty;
            protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE;
            break;
        case 1: // Identify controller data
            query->PropertyId                      = StorageAdapterProtocolSpecificProperty;
            protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_CONTROLLER;
            break;
        case 2: // list of 1024 active namespace IDs
            query->PropertyId                      = StorageAdapterProtocolSpecificProperty;
            protocolData->ProtocolDataRequestValue = NVME_IDENTIFY_CNS_ACTIVE_NAMESPACES;
            // NOTE: This command is documented in MSDN, but it doesn't actually work, so we are returning an error
            // instead! - TJE
            safe_free(&buffer);
            return OS_COMMAND_NOT_AVAILABLE;
            // All values below here are added in NVMe 1.3, which MS doesn't support yet! - TJE
        case 3:    // list of namespace identification descriptor structures
                   // namespace management cns values
        case 0x10: // list of up to 1024 namespace IDs with namespace identifier greater than one specified in NSID
        case 0x11: // identify namespace data for NSID specified
        case 0x12: // list of up to 2047 controller identifiers greater than or equal to the value specified in
                   // CDW10.CNTID. List contains controller identifiers that are attached to the namespace specified
        case 0x13: // list of up to 2047 controller identifiers greater than or equal to the value specified in
                   // CDW10.CNTID. List contains controller identifiers that that may or may not be attached to
                   // namespaces
        case 0x14: // primary controller capabilities
        case 0x15: // secondary controller list
        default:
            safe_free(&buffer);
            return OS_COMMAND_NOT_AVAILABLE;
        }

        query->QueryType = PropertyStandardQuery;

        protocolData->ProtocolType                = ProtocolTypeNvme;
        protocolData->DataType                    = NVMeDataTypeIdentify;
        protocolData->ProtocolDataRequestSubValue = 0;
        protocolData->ProtocolDataOffset          = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
        protocolData->ProtocolDataLength          = NVME_IDENTIFY_DATA_LEN;

        /*
        // Send request down.
        */
#    if defined(_DEBUG)
        printf("%s: Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
#    endif

        DECLARE_SEATIMER(commandTimer);
        start_Timer(&commandTimer);
        result = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_QUERY_PROPERTY, buffer, bufferLength,
                                 buffer, bufferLength, &returnedLength, M_NULLPTR);
        stop_Timer(&commandTimer);
        nvmeIoCtx->device->os_info.last_error                    = GetLastError();
        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

        if (MSFT_BOOL_FALSE(result))
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
            char* identifyControllerData =
                C_CAST(char*, C_CAST(PCHAR, protocolData) + protocolData->ProtocolDataOffset);
            safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, identifyControllerData, nvmeIoCtx->dataSize);
        }

        safe_free(&buffer);
    }
    return ret;
}

// This is a new union/struct I couldn't find anywhere but here:
// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddstor/ns-ntddstor-storage_protocol_data_subvalue_get_log_page
// So it is defined differently (name only) to prevent colisions if WinAPI version updates and has it at some point -
// TJE
typedef union u_MSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4
{
    struct
    {
        ULONG RetainAsyncEvent : 1;
        ULONG LogSpecificField : 4;
        ULONG Reserved : 27;
    };
    ULONG AsUlong;
} MSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4, *PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4;

// WIN_API_TARGET_WIN10_17763 gave offset fields to the get log page through a subvalue input.
// The documentation online states 1903(WIN_API_TARGET_WIN10_18362) and up support read buffer 16...it is unclear what
// version first supported the read buffer 16 method to read the telemetry log This function has been defined to read
// telemetry with the read buffer 16 command. NOTE: Read buffer 10 will NOT work, only 16. this function is only
// necessary for versions older than 1809...if readbuffer16 is even supported in those old versions. static
// eReturnValues get_NVMe_Telemetry_Data_With_RB16(nvmeCmdCtx* nvmeIoCtx)
//{
//     eReturnValues ret = BAD_PARAMETER;
//     uint8_t logID = M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw10);
//     if (nvmeIoCtx->commandType == NVM_ADMIN_CMD && nvmeIoCtx->cmd.adminCmd.opcode == NVME_ADMIN_CMD_GET_LOG_PAGE &&
//     (logID == NVME_LOG_TELEMETRY_CTRL_ID || logID == NVME_LOG_TELEMETRY_HOST_ID))
//     {
//         ret = SUCCESS;
//         uint8_t bufferID = UINT8_C(0x10);//Assume host as it is most common to pull
//         uint64_t numberOfDWords = M_WordsTo4ByteValue(M_Word0(nvmeIoCtx->cmd.adminCmd.cdw11),
//         M_Word1(nvmeIoCtx->cmd.adminCmd.cdw10)); uint64_t logPageOffset =
//         M_DWordsTo8ByteValue(nvmeIoCtx->cmd.adminCmd.cdw13, nvmeIoCtx->cmd.adminCmd.cdw12);
//         //bool createNewSnapshot = M_ToBool(nvmeIoCtx->cmd.adminCmd.cdw10 & BIT8);
//         if (logID == NVME_LOG_TELEMETRY_CTRL_ID)
//         {
//             bufferID = 0x11;
//         }
//         if ((numberOfDWords << 4) > UINT32_MAX)
//         {
//             ret = BAD_PARAMETER;
//             //Invalid Field in Command???
//             //Data Transfer Error???
//         }
//         else
//         {
//             //now call the SCSI read-buffer 16 command with the correct parameters to pull the data
//             //TODO: figure out how the "trigger" to create a new snapshot will work. use the commented out
//             createNewSnapshot above -TJE ret = scsi_Read_Buffer_16(nvmeIoCtx->device, 0x1C, 0 /*TODO take new
//             snapshot???*/, bufferID, logPageOffset, C_CAST(uint32_t, numberOfDWords << 4), nvmeIoCtx->ptrData);
//             //TODO: Handle any errors
//         }
//     }
//     return ret;
// }

static eReturnValues send_Win_NVMe_Get_Log_Page_Cmd(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues returnValue    = SUCCESS;
    BOOL          result         = FALSE;
    PVOID         buffer         = M_NULLPTR;
    ULONG         bufferLength   = ULONG_C(0);
    ULONG         returnedLength = ULONG_C(0);

    PSTORAGE_PROPERTY_QUERY           query             = M_NULLPTR;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA   protocolData      = M_NULLPTR;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = M_NULLPTR;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) +
                   nvmeIoCtx->dataSize;
    buffer = safe_malloc(bufferLength);

    if (buffer == M_NULLPTR)
    {
#    if defined(_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#    endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query             = C_CAST(PSTORAGE_PROPERTY_QUERY, buffer);
    protocolDataDescr = C_CAST(PSTORAGE_PROTOCOL_DATA_DESCRIPTOR, buffer);
    protocolData      = C_CAST(PSTORAGE_PROTOCOL_SPECIFIC_DATA, query->AdditionalParameters);

    query->PropertyId = StorageAdapterProtocolSpecificProperty;
    query->QueryType  = PropertyStandardQuery;

    protocolData->ProtocolType                = ProtocolTypeNvme;
    protocolData->DataType                    = NVMeDataTypeLogPage;
    protocolData->ProtocolDataRequestValue    = M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw10); // log id
    protocolData->ProtocolDataRequestSubValue = nvmeIoCtx->cmd.adminCmd.cdw12;          // offset lower 32 bits

#    if WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_20348
    // 20348 gives suv value 4 for the other bit field values
    protocolData->ProtocolDataRequestSubValue2 = nvmeIoCtx->cmd.adminCmd.cdw13;          // offset higher 32 bits
    protocolData->ProtocolDataRequestSubValue3 = M_Word1(nvmeIoCtx->cmd.adminCmd.cdw11); // log specific ID
    PSTORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE nvmeSubValue4 =
        C_CAST(PSTORAGE_PROTOCOL_DATA_SUBVALUE_GET_LOG_PAGE,
               &protocolData->ProtocolDataRequestSubValue4); // protocol data request subvalue 4 is what this is in
                                                             // newer documentation...needs a different structure to
                                                             // really see if this way, but this should be compatible.
    nvmeSubValue4->LogSpecificField = M_Nibble2(nvmeIoCtx->cmd.adminCmd.cdw10);
    nvmeSubValue4->RetainAsynEvent  = (nvmeIoCtx->cmd.adminCmd.cdw10 & BIT15) > 0 ? 1 : 0;
#    elif WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_18362
    // 18362 gives sub value 3 for log specific ID
    protocolData->ProtocolDataRequestSubValue2 = nvmeIoCtx->cmd.adminCmd.cdw13;          // offset higher 32 bits
    protocolData->ProtocolDataRequestSubValue3 = M_Word1(nvmeIoCtx->cmd.adminCmd.cdw11); // log specific ID
    PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4 nvmeSubValue4 = C_CAST(
        PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4,
        &protocolData->Reserved); // protocol data request subvalue 4 is what this is in newer documentation...needs a
                                  // different structure to really see if this way, but this should be compatible.
    nvmeSubValue4->LogSpecificField = M_Nibble2(nvmeIoCtx->cmd.adminCmd.cdw10);
    nvmeSubValue4->RetainAsyncEvent = (nvmeIoCtx->cmd.adminCmd.cdw10 & BIT15) > 0 ? 1 : 0;
#    elif WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_17763
    // 17763 gave sub value 2 for offset high bits
    protocolData->ProtocolDataRequestSubValue2 = nvmeIoCtx->cmd.adminCmd.cdw13;          // offset higher 32 bits
    protocolData->Reserved[0]                  = M_Word1(nvmeIoCtx->cmd.adminCmd.cdw11); // log specific ID
    PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4 nvmeSubValue4 = C_CAST(
        PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4,
        &protocolData->Reserved[1]); // protocol data request subvalue 4 is what this is in newer documentation...needs
                                     // a different structure to really see if this way, but this should be compatible.
    nvmeSubValue4->LogSpecificField = M_Nibble2(nvmeIoCtx->cmd.adminCmd.cdw10);
    nvmeSubValue4->RetainAsyncEvent = (nvmeIoCtx->cmd.adminCmd.cdw10 & BIT15) > 0 ? 1 : 0;
#    else
    // set fields in reserved array???
    protocolData->Reserved[0] = nvmeIoCtx->cmd.adminCmd.cdw13;          // offset higher 32 bits
    protocolData->Reserved[1] = M_Word1(nvmeIoCtx->cmd.adminCmd.cdw11); // log specific ID
    PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4 nvmeSubValue4 = C_CAST(
        PMSFT_NVME_STORAGE_PROTOCOL_DATA_GET_LOG_PAGE_SUB_VALUE_4,
        &protocolData->Reserved[2]); // protocol data request subvalue 4 is what this is in newer documentation...needs
                                     // a different structure to really see if this way, but this should be compatible.
    nvmeSubValue4->LogSpecificField = M_Nibble2(nvmeIoCtx->cmd.adminCmd.cdw10);
    nvmeSubValue4->RetainAsyncEvent = (nvmeIoCtx->cmd.adminCmd.cdw10 & BIT15) > 0 ? 1 : 0;
#    endif

    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = nvmeIoCtx->dataSize;

    //
    // Send request down.
    //
#    if defined(_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
#    endif
    DECLARE_SEATIMER(commandTimer);
    start_Timer(&commandTimer);
    result = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_QUERY_PROPERTY, buffer, bufferLength, buffer,
                             bufferLength, &returnedLength, M_NULLPTR);
    stop_Timer(&commandTimer);
    nvmeIoCtx->device->os_info.last_error                    = GetLastError();
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (MSFT_BOOL_FALSE(result) || (returnedLength == 0))
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
#    if defined(_DEBUG)
            printf("%s: Error Log - data descriptor header not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < nvmeIoCtx->dataSize))
        {
#    if defined(_DEBUG)
            printf("%s: Error Log - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        uint8_t* logData = C_CAST(uint8_t*, C_CAST(PCHAR, protocolData) + protocolData->ProtocolDataOffset);
        if (nvmeIoCtx->ptrData && protocolData->ProtocolDataLength > 0)
        {
            safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, logData,
                        M_Min(protocolData->ProtocolDataLength, nvmeIoCtx->dataSize));
        }
        nvmeIoCtx->commandCompletionData.commandSpecific =
            protocolData->FixedProtocolReturnData; // This should only be DWORD 0
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }

    free(buffer);

    return returnValue;
}

static eReturnValues send_Win_NVMe_Get_Features_Cmd(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues returnValue    = SUCCESS;
    BOOL          result         = FALSE;
    PVOID         buffer         = M_NULLPTR;
    ULONG         bufferLength   = ULONG_C(0);
    ULONG         returnedLength = ULONG_C(0);

    PSTORAGE_PROPERTY_QUERY           query             = M_NULLPTR;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA   protocolData      = M_NULLPTR;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = M_NULLPTR;

    //
    // Allocate buffer for use.
    //
    uint32_t nvmeGetFtMinSize = M_Max(
        4, nvmeIoCtx->dataSize); // This works around get features commands that are not actually transferring any data.
                                 // If you size the buffer based on a zero data transfer, windows gives an error -TJE
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) +
                   nvmeGetFtMinSize;
    buffer = safe_malloc(bufferLength);

    if (buffer == M_NULLPTR)
    {
#    if defined(_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#    endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query             = C_CAST(PSTORAGE_PROPERTY_QUERY, buffer);
    protocolDataDescr = C_CAST(PSTORAGE_PROTOCOL_DATA_DESCRIPTOR, buffer);
    protocolData      = C_CAST(PSTORAGE_PROTOCOL_SPECIFIC_DATA, query->AdditionalParameters);

    query->PropertyId = StorageAdapterProtocolSpecificProperty;
    query->QueryType  = PropertyStandardQuery;

    protocolData->ProtocolType             = ProtocolTypeNvme;
    protocolData->DataType                 = NVMeDataTypeFeature;
    protocolData->ProtocolDataRequestValue = M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw10);
    protocolData->ProtocolDataRequestSubValue =
        nvmeIoCtx->cmd.adminCmd
            .cdw11; // latest Win API shows using CDW11 in a comment, which is a feature specific value
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength = nvmeGetFtMinSize;

    //
    // Send request down.
    //
#    if defined(_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
#    endif
    DECLARE_SEATIMER(commandTimer);
    start_Timer(&commandTimer);
    result = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_QUERY_PROPERTY, buffer, bufferLength, buffer,
                             bufferLength, &returnedLength, M_NULLPTR);
    stop_Timer(&commandTimer);
    nvmeIoCtx->device->os_info.last_error                    = GetLastError();
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (MSFT_BOOL_FALSE(result) || (returnedLength == 0))
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
#    if defined(_DEBUG)
            printf("%s: Error Feature - data descriptor header not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < nvmeIoCtx->dataSize))
        {
#    if defined(_DEBUG)
            printf("%s: Error Feature - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        uint8_t* featData = C_CAST(uint8_t*, C_CAST(PCHAR, protocolData) + protocolData->ProtocolDataOffset);
        if (nvmeIoCtx->ptrData && protocolData->ProtocolDataLength > 0)
        {
            safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, featData,
                        M_Min(nvmeIoCtx->dataSize, protocolData->ProtocolDataLength));
        }
        nvmeIoCtx->commandCompletionData.commandSpecific =
            protocolData->FixedProtocolReturnData; // This should only be DWORD 0 on a get features command anyways...
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
    }

    safe_free(&buffer);

    return returnValue;
}

static eReturnValues send_Win_NVMe_Firmware_Activate_Command(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
#    if defined(_DEBUG)
    printf("%s: -->\n", __FUNCTION__);
#    endif
    // send the activate IOCTL
    STORAGE_HW_FIRMWARE_ACTIVATE downloadActivate;
    safe_memset(&downloadActivate, sizeof(STORAGE_HW_FIRMWARE_ACTIVATE), 0, sizeof(STORAGE_HW_FIRMWARE_ACTIVATE));
    downloadActivate.Version = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    downloadActivate.Size    = sizeof(STORAGE_HW_FIRMWARE_ACTIVATE);
    uint8_t activateAction   = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 5, 3);
    downloadActivate.Flags |=
        STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER; // this command must go to the controller, not the namespace
    if (activateAction == NVME_CA_ACTIVITE_ON_RST ||
        activateAction == NVME_CA_ACTIVITE_IMMEDIATE) // check the activate action
    {
        // Activate actions 2, & 3 sound like the closest match to this flag. Each of these requests switching to the a
        // firmware already on the drive. Activate action 0 & 1 say to replace a firmware image in a specified slot (and
        // to or not to activate).
        downloadActivate.Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE;
    }
    downloadActivate.Slot = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0);
#    if defined(_DEBUG)
    printf("%s: downloadActivate->Version=%ld\n\t->Size=%ld\n\t->Flags=0x%lX\n\t->Slot=%d\n", __FUNCTION__,
           downloadActivate.Version, downloadActivate.Size, downloadActivate.Flags, downloadActivate.Slot);
#    endif
    DWORD returned_data = DWORD_C(0);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    DECLARE_SEATIMER(commandTimer);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_FIRMWARE_ACTIVATE, &downloadActivate,
                                 sizeof(STORAGE_HW_FIRMWARE_ACTIVATE), M_NULLPTR, 0, &returned_data, &overlappedStruct);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        nvmeIoCtx->device->os_info
            .last_error) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                         // flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
#    if defined(_DEBUG)
    printf("%s: nvmeIoCtx->device->os_info.last_error=%lu(0x%lx)\n", __FUNCTION__,
           nvmeIoCtx->device->os_info.last_error, nvmeIoCtx->device->os_info.last_error);
#    endif
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    // dummy up sense data for end result
    if (fwdlIO)
    {
        ret                                              = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid        = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        switch (nvmeIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE:
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }
#    if defined(_DEBUG)
    printf("%s: <-- (ret=%d)\n", __FUNCTION__, ret);
#    endif
    return ret;
}

// uncomment this flag to switch to force using the older structure if we need to.
#    define DISABLE_FWDL_V2 1

static eReturnValues send_Win_NVMe_Firmware_Image_Download_Command(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
#    if defined(_DEBUG)
    printf("%s: -->\n", __FUNCTION__);
#    endif
    // send download IOCTL
#    if defined(WIN_API_TARGET_VERSION) && !defined(DISABLE_FWDL_V2) &&                                                \
        WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    DWORD downloadStructureSize = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2) + nvmeIoCtx->dataSize;
    PSTORAGE_HW_FIRMWARE_DOWNLOAD_V2 downloadIO =
        M_REINTERPRET_CAST(PSTORAGE_HW_FIRMWARE_DOWNLOAD_V2, safe_malloc(downloadStructureSize));
#        if defined(_DEBUG)
    printf("%s: sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2)=%zu+%" PRIu32 "=%ld\n", __FUNCTION__,
           sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2), nvmeIoCtx->dataSize, downloadStructureSize);
#        endif
#    else
    DWORD                         downloadStructureSize = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD) + nvmeIoCtx->dataSize;
    PSTORAGE_HW_FIRMWARE_DOWNLOAD downloadIO =
        M_REINTERPRET_CAST(PSTORAGE_HW_FIRMWARE_DOWNLOAD, safe_malloc(downloadStructureSize));
#        if defined(_DEBUG)
    printf("%s: sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD)=%zu\n", __FUNCTION__, sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD));
#        endif
#    endif
    if (!downloadIO)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(downloadIO, downloadStructureSize, 0, downloadStructureSize);
#    if defined(WIN_API_TARGET_VERSION) && !defined(DISABLE_FWDL_V2) &&                                                \
        WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    downloadIO->Version = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD_V2);
#    else
    downloadIO->Version = sizeof(STORAGE_HW_FIRMWARE_DOWNLOAD);
#    endif
    downloadIO->Size = downloadStructureSize;
    downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_CONTROLLER;
#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_15063
    if (nvmeIoCtx->fwdlLastSegment)
    {
        // This IS documented on MSDN but VS2015 can't seem to find it...
        // One website says that this flag is new in Win10 1704 - creators update (10.0.15021)
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
    }
#    endif
#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    if (nvmeIoCtx->fwdlFirstSegment)
    {
        downloadIO->Flags |= STORAGE_HW_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
    }
#    endif
    downloadIO->Slot = STORAGE_HW_FIRMWARE_INVALID_SLOT; // get_8bit_range_uint32(nvmeIoCtx->cmd, 1, 0);
    // we need to set the offset since MS uses this in the command sent to the device.
    downloadIO->Offset = C_CAST(uint64_t, nvmeIoCtx->cmd.adminCmd.cdw11) << 2; // convert #DWords to bytes for offset
    // set the size of the buffer
    downloadIO->BufferSize = nvmeIoCtx->dataSize;
#    if defined(WIN_API_TARGET_VERSION) && !defined(DISABLE_FWDL_V2) &&                                                \
        WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_16299
    downloadIO->ImageSize = nvmeIoCtx->dataSize;
#    endif
    // now copy the buffer into this IOCTL struct
    safe_memcpy(downloadIO->ImageBuffer, nvmeIoCtx->dataSize, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);

#    if defined(_DEBUG)
    printf("%s: "
           "downloadIO\n\t->Version=%ld\n\t->Size=%ld\n\t->Flags=0x%lX\n\t->Slot=%d\n\t->Offset=0x%llX\n\t->BufferSize="
           "0x%llX\n",
           __FUNCTION__, downloadIO->Version, downloadIO->Size, downloadIO->Flags, downloadIO->Slot, downloadIO->Offset,
           downloadIO->BufferSize);
    // print_Data_Buffer(downloadIO->ImageBuffer, (downloadStructureSize - FIELD_OFFSET(STORAGE_HW_FIRMWARE_DOWNLOAD,
    // ImageBuffer)), false);
#    endif

    // time to issue the IO
    DWORD returned_data = DWORD_C(0);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    DECLARE_SEATIMER(commandTimer);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    start_Timer(&commandTimer);
    int fwdlIO = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_FIRMWARE_DOWNLOAD, downloadIO,
                                 downloadStructureSize, M_NULLPTR, 0, &returned_data, &overlappedStruct);
    nvmeIoCtx->device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        nvmeIoCtx->device->os_info
            .last_error) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                         // flag, everything will work like old synchronous code.-TJE
    {
        fwdlIO = GetOverlappedResult(nvmeIoCtx->device->os_info.fd, &overlappedStruct, &returned_data, TRUE);
        nvmeIoCtx->device->os_info.last_error = GetLastError();
    }
    else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
#    if defined(_DEBUG)
    printf("%s: nvmeIoCtx->device->os_info.last_error=%lu(0x%lx)\n", __FUNCTION__,
           nvmeIoCtx->device->os_info.last_error, nvmeIoCtx->device->os_info.last_error);
#    endif
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    // dummy up sense data for end result
    if (fwdlIO)
    {
        ret                                              = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid        = true;
        nvmeIoCtx->commandCompletionData.statusAndCID    = 0;
        nvmeIoCtx->commandCompletionData.dw3Valid        = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        switch (nvmeIoCtx->device->os_info.last_error)
        {
        case ERROR_IO_DEVICE: // aborted command is the best we can do
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
    }
#    if defined(_DEBUG)
    printf("%s: <-- (ret=%d)\n", __FUNCTION__, ret);
#    endif
    return ret;
}

static eReturnValues win10_Translate_Security_Send(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret         = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    // Windows API call does not exist...need to issue a SCSI IO and let the driver translate it for us...how silly
    if (get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 0) ==
        0) // check that the nvme specific field isn't set since we can't issue that
    {
        // turn verbosity to silent since we don't need to see everything from issueing the scsi io...purpose right now
        // is to make it look like an NVM io and be transparent to the caller.
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        ret = scsi_SecurityProtocol_Out(nvmeIoCtx->device, get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 24),
                                        get_16bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 23, 8), false,
                                        nvmeIoCtx->cmd.adminCmd.cdw11, nvmeIoCtx->ptrData, 0);
        // command completed, so turn verbosity back to what it was
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

static eReturnValues win10_Translate_Security_Receive(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret         = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    // Windows API call does not exist...need to issue a SCSI IO and let the driver translate it for us...how silly
    if (get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 0) ==
        0) // check that the nvme specific field isn't set since we can't issue that
    {
        // turn verbosity to silent since we don't need to see everything from issueing the scsi io...purpose right now
        // is to make it look like an NVM io and be transparent to the caller.
        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
        ret = scsi_SecurityProtocol_In(nvmeIoCtx->device, get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 24),
                                       get_16bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 23, 8), false,
                                       nvmeIoCtx->cmd.adminCmd.cdw11, nvmeIoCtx->ptrData);
        // command completed, so turn verbosity back to what it was
        nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    }
    return ret;
}

static eReturnValues win10_Translate_Set_Error_Recovery_Time_Limit(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret         = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    bool             dulbe       = nvmeIoCtx->cmd.adminCmd.cdw11 & BIT16;
    uint16_t         nvmTimeLimitedErrorRecovery =
        M_BytesTo2ByteValue(M_Byte1(nvmeIoCtx->cmd.adminCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.adminCmd.cdw11));
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (!dulbe && !(nvmeIoCtx->cmd.adminCmd.cdw11 >> 16)) // make sure unsupported fields aren't set!!!
    {
        // use read-write error recovery MP - recovery time limit field
        uint8_t* errorRecoveryMP = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN, sizeof(uint8_t),
                                          nvmeIoCtx->device->os_info.minimumAlignment));
        if (errorRecoveryMP)
        {
            // first, read the page into memory
            if (SUCCESS == scsi_Mode_Sense_10(nvmeIoCtx->device, MP_READ_WRITE_ERROR_RECOVERY,
                                              MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN, 0, true, false,
                                              MPC_CURRENT_VALUES, errorRecoveryMP))
            {
                // modify the recovery time limit field
                errorRecoveryMP[MODE_HEADER_LENGTH10 + 10] = M_Byte1(nvmTimeLimitedErrorRecovery);
                errorRecoveryMP[MODE_HEADER_LENGTH10 + 11] = M_Byte0(nvmTimeLimitedErrorRecovery);
                // send it back to the drive
                ret = scsi_Mode_Select_10(nvmeIoCtx->device, MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN,
                                          true, false, false, errorRecoveryMP,
                                          MODE_HEADER_LENGTH10 + MP_READ_WRITE_ERROR_RECOVERY_LEN);
            }
            safe_free_aligned(&errorRecoveryMP);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

static eReturnValues win10_Translate_Set_Volatile_Write_Cache(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret               = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity       = nvmeIoCtx->device->deviceVerbosity;
    bool             wce               = nvmeIoCtx->cmd.adminCmd.cdw11 & BIT0;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    if (!(nvmeIoCtx->cmd.adminCmd.cdw11 >> 31)) // make sure unsupported fields aren't set!!!
    {
        // use caching MP - write back cache enabled field
        uint8_t* cachingMP =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(MODE_HEADER_LENGTH10 + MP_CACHING_LEN, sizeof(uint8_t),
                                                             nvmeIoCtx->device->os_info.minimumAlignment));
        if (cachingMP)
        {
            // first, read the page into memory
            if (SUCCESS == scsi_Mode_Sense_10(nvmeIoCtx->device, MP_CACHING, MODE_HEADER_LENGTH10 + MP_CACHING_LEN, 0,
                                              true, false, MPC_CURRENT_VALUES, cachingMP))
            {
                // modify the wce field
                if (wce)
                {
                    cachingMP[MODE_HEADER_LENGTH10 + 2] |= BIT2;
                }
                else
                {
                    if (cachingMP[MODE_HEADER_LENGTH10 + 2] & BIT2) // check if the bit is already set
                    {
                        cachingMP[MODE_HEADER_LENGTH10 + 2] ^= BIT2; // turn the bit off with xor
                    }
                }
                // send it back to the drive
                ret = scsi_Mode_Select_10(nvmeIoCtx->device, MODE_HEADER_LENGTH10 + MP_CACHING_LEN, true, false, false,
                                          cachingMP, MODE_HEADER_LENGTH10 + MP_CACHING_LEN);
            }
            safe_free_aligned(&cachingMP);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

static eReturnValues win10_Translate_Set_Power_Management(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret               = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity       = nvmeIoCtx->device->deviceVerbosity;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    uint8_t workloadHint               = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw11, 7, 5);
    uint8_t powerState                 = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw11, 4, 0);
    if (workloadHint == 0 &&
        get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw11, 31, 8) ==
            0) // cannot send workload hints in the API calls available, also filtering out reserved bits
    {
        double maxPowerScalar = 0.01;
        if (nvmeIoCtx->device->drive_info.IdentifyData.nvme.ctrl.psd[powerState].flags & BIT0)
        {
            maxPowerScalar = 0.0001;
        }
        double maxPowerWatts =
            le16_to_host(nvmeIoCtx->device->drive_info.IdentifyData.nvme.ctrl.psd[powerState].maxPower) *
            maxPowerScalar;

        // Need to make sure this power value can actually work in Windows! MS only supports values in milliwatts, and
        // it's possible with the code above to specify something less than 1 milliwatt...-TJE
        if ((maxPowerWatts * 1000.0) >= 1)
        {
            STORAGE_DEVICE_POWER_CAP powerCap;
            safe_memset(&powerCap, sizeof(STORAGE_DEVICE_POWER_CAP), 0, sizeof(STORAGE_DEVICE_POWER_CAP));

            powerCap.Version    = STORAGE_DEVICE_POWER_CAP_VERSION_V1;
            powerCap.Size       = sizeof(STORAGE_DEVICE_POWER_CAP);
            powerCap.Units      = StorageDevicePowerCapUnitsMilliwatts;
            powerCap.MaxPower   = C_CAST(ULONG, maxPowerWatts * 1000.0);
            DWORD returnedBytes = DWORD_C(0);
            SetLastError(NO_ERROR);
            DECLARE_SEATIMER(commandTimer);
            start_Timer(&commandTimer);
            BOOL success = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_DEVICE_POWER_CAP, &powerCap,
                                           sizeof(STORAGE_DEVICE_POWER_CAP), &powerCap,
                                           sizeof(STORAGE_DEVICE_POWER_CAP), &returnedBytes,
                                           M_NULLPTR // Overlapped support???
            );
            stop_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error                    = GetLastError();
            nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

            if (MSFT_BOOL_TRUE(success))
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
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

static eReturnValues send_NVMe_Set_Temperature_Threshold(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;

    uint8_t thsel  = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw11, 21, 20);
    uint8_t tmpsel = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw11, 19, 16);

    int32_t temperatureThreshold = C_CAST(int32_t, get_16bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw11, 15, 0));

    // TODO: check reserved fields are zero to return an error since they may communicate a different behavior we are
    // not currently
    //       supporting/implementing
    STORAGE_TEMPERATURE_THRESHOLD tempThresh;
    // STORAGE_TEMPERATURE_THRESHOLD_FLAG_ADAPTER_REQUEST
    safe_memset(&tempThresh, sizeof(STORAGE_TEMPERATURE_THRESHOLD), 0, sizeof(STORAGE_TEMPERATURE_THRESHOLD));

    tempThresh.Version = sizeof(STORAGE_TEMPERATURE_THRESHOLD);
    tempThresh.Size    = sizeof(STORAGE_TEMPERATURE_THRESHOLD);

    tempThresh.Flags |= STORAGE_TEMPERATURE_THRESHOLD_FLAG_ADAPTER_REQUEST;

    tempThresh.Index = tmpsel;
    tempThresh.Threshold =
        C_CAST(SHORT, temperatureThreshold -
                          INT32_C(273)); // NVMe does every temp in kelvin, but the API expects Celsius - TJE
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

    // now issue the IO!
    DWORD bytesReturned = DWORD_C(0);
    DECLARE_SEATIMER(commandTimer);
    start_Timer(&commandTimer);
    BOOL success = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_SET_TEMPERATURE_THRESHOLD, &tempThresh,
                                   sizeof(STORAGE_TEMPERATURE_THRESHOLD), M_NULLPTR, 0, &bytesReturned, 0);
    stop_Timer(&commandTimer);
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (MSFT_BOOL_TRUE(success))
    {
        ret                                              = SUCCESS;
        nvmeIoCtx->commandCompletionData.commandSpecific = 0;
        nvmeIoCtx->commandCompletionData.dw0Valid        = true;
    }
    else
    {
        if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
        // Todo....set a better error condition
        nvmeIoCtx->commandCompletionData.commandSpecific = 0x0E;
        nvmeIoCtx->commandCompletionData.dw0Valid        = true;
        ret                                              = FAILURE;
    }

    return ret;
}

#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_18362
static eReturnValues send_NVMe_Set_Features_Win10_Storage_Protocol(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret              = OS_COMMAND_NOT_AVAILABLE;
    uint32_t      featTransferSize = (nvmeIoCtx->dataSize >= 4096)
                                         ? nvmeIoCtx->dataSize
                                         : 4096; // get features API requires a 4096 buffer, so allocating space for that in
                                            // here too, in case this API is also expecting this data buffer size-TJE
    uint32_t bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_SET, AdditionalParameters) +
                            sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA_EXT) + featTransferSize;
    uint8_t* bufferData = M_REINTERPRET_CAST(uint8_t*, safe_calloc(bufferLength, sizeof(uint8_t)));
    if (bufferData)
    {
        PSTORAGE_PROPERTY_SET               propSet = C_CAST(PSTORAGE_PROPERTY_SET, bufferData);
        PSTORAGE_PROTOCOL_SPECIFIC_DATA_EXT protocolSpecificData =
            C_CAST(PSTORAGE_PROTOCOL_SPECIFIC_DATA_EXT, propSet->AdditionalParameters);
        PSTORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT protocolDataDescr =
            C_CAST(PSTORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT, bufferData);
        // set properties config according to microsoft documentation.
        propSet->SetType = PropertyStandardSet;
        if (nvmeIoCtx->cmd.adminCmd.nsid == 0 || nvmeIoCtx->cmd.adminCmd.nsid == UINT32_MAX)
        {
            // All namespaces/the whole device
            propSet->PropertyId = StorageAdapterProtocolSpecificProperty;
        }
        else
        {
            // set feature for specific namespace
            propSet->PropertyId = StorageDeviceProtocolSpecificProperty;
        }
        // now setup the protocol specific data
        protocolSpecificData->ProtocolType = ProtocolTypeNvme;
        protocolSpecificData->DataType     = NVMeDataTypeFeature;
        // remaining fields are for nvme dwords
        protocolSpecificData->ProtocolDataValue = M_Byte0(nvmeIoCtx->cmd.dwords.cdw10); // Where does the save bit go???
        protocolSpecificData->ProtocolDataSubValue  = nvmeIoCtx->cmd.dwords.cdw11;
        protocolSpecificData->ProtocolDataSubValue2 = nvmeIoCtx->cmd.dwords.cdw12;
        protocolSpecificData->ProtocolDataSubValue3 = nvmeIoCtx->cmd.dwords.cdw13;
        protocolSpecificData->ProtocolDataSubValue4 = nvmeIoCtx->cmd.dwords.cdw14;
        protocolSpecificData->ProtocolDataSubValue5 = nvmeIoCtx->cmd.dwords.cdw15;

        if (nvmeIoCtx->ptrData && nvmeIoCtx->dataSize)
        {
            // if this feature has a databuffer, set it up to transmit it.
            protocolSpecificData->ProtocolDataLength = nvmeIoCtx->dataSize;
            protocolSpecificData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA_EXT);
            safe_memcpy(C_CAST(uint8_t*, protocolSpecificData) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA_EXT),
                        nvmeIoCtx->dataSize, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
        else
        {
            // no additional command data is being shared for this feature.
            // Due to get features requiring a 4096 sized buffer, we are passing one in here too just to make Windows
            // happy even if the feature does no t use it.
            protocolSpecificData->ProtocolDataLength = featTransferSize;
            protocolSpecificData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA_EXT);
        }

        //
        // Send request down.
        //
#        if defined(_DEBUG)
        printf("%s Drive Path = %s", __FUNCTION__, nvmeIoCtx->device->os_info.name);
#        endif
        DECLARE_SEATIMER(commandTimer);
        start_Timer(&commandTimer);
        DWORD returnedLength = DWORD_C(0);
        BOOL  result         = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_SET_PROPERTY, bufferData,
                                               bufferLength, bufferData, bufferLength, &returnedLength, M_NULLPTR);
        start_Timer(&commandTimer);
        nvmeIoCtx->device->os_info.last_error                    = GetLastError();
        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        if (MSFT_BOOL_FALSE(result))
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
            //
            // Validate the returned data.
            //
            if ((protocolDataDescr->Version != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT)) ||
                (protocolDataDescr->Size != sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR_EXT)))
            {
#        if defined(_DEBUG)
                printf("%s: Error Feature - data descriptor header not valid\n", __FUNCTION__);
#        endif
                ret = OS_PASSTHROUGH_FAILURE;
            }

            protocolSpecificData = &protocolDataDescr->ProtocolSpecificData;

            if ((protocolSpecificData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA_EXT)) ||
                (protocolSpecificData->ProtocolDataLength < nvmeIoCtx->dataSize))
            {
#        if defined(_DEBUG)
                printf("%s: Error Feature - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#        endif
                ret = OS_PASSTHROUGH_FAILURE;
            }
            nvmeIoCtx->commandCompletionData.commandSpecific =
                protocolSpecificData
                    ->FixedProtocolReturnData; // This should only be DWORD 0 on a get features command anyways...
            nvmeIoCtx->commandCompletionData.dw0Valid = true;
        }
        safe_free(&bufferData);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}
#    endif

static eReturnValues send_NVMe_Set_Features_Win10(nvmeCmdCtx* nvmeIoCtx, bool* useNVMPassthrough)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;

#    if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_18362
    // 1903 added storage_set_property IOCTL support, so try it first before falling back on these other methods - TJE
    if (is_Windows_10_Version_1903_Or_Higher())
    {
        ret = send_NVMe_Set_Features_Win10_Storage_Protocol(nvmeIoCtx);
    }
#    endif
    if (ret == OS_COMMAND_NOT_AVAILABLE || ret == OS_PASSTHROUGH_FAILURE)
    {
        // Depending on the feature, we may need a SCSI translation, a Windows API call, or we won't be able to perform
        // any translation at all. IOCTL_STORAGE_DEVICE_POWER_CAP bool save = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT31;
        uint8_t featureID = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10);
        switch (featureID)
        {
        case 0x01: // Arbitration
            break;
        case 0x02: // Power Management
            ret = win10_Translate_Set_Power_Management(nvmeIoCtx);
            break;
        case 0x03: // LBA Range Type
            break;
        case 0x04: // Temperature Threshold
            ret = send_NVMe_Set_Temperature_Threshold(nvmeIoCtx);
            break;
        case 0x05: // Error Recovery
            ret = win10_Translate_Set_Error_Recovery_Time_Limit(nvmeIoCtx);
            break;
        case 0x06: // Volatile Write Cache
            ret = win10_Translate_Set_Volatile_Write_Cache(nvmeIoCtx);
            break;
        case 0x07: // Number of Queues
        case 0x08: // Interrupt coalescing
        case 0x09: // Interrupt Vector Configuration
        case 0x0A: // Write Atomicity Normal
        case 0x0B: // Asynchronous Event Configuration
        case 0x0C: // Autonomous Power State Transition
        case 0x0D: // Host Memroy Buffer
        case 0x0E: // Timestamp
        case 0x0F: // Keep Alive Timer
            break;
        case 0x10: // Host Controller Thermal Management
            break;
        case 0x80: // Software Progress Marker
        case 0x81: // Host Identifier
        case 0x82: // Reservation Notification Mask
            break;
        case 0x83: // Reservation Persistance
                   // SCSI Persistent reserve out?
            break;
        default:
            // 12h- 77h & 0h = reserved
            // 78h - 7Fh = NVMe Management Insterface Specification
            // 80h - BFh = command set specific
            // C0h - FFh = vendor specific
            if (featureID >= 0xC0 /* && featureID <= 0xFF */)
            {
                // call the vendor specific pass-through function to try and issue this command
                if (useNVMPassthrough)
                {
                    *useNVMPassthrough = true;
                }
            }
            break;
        }
    }
    return ret;
}

#    if defined(USE_IOCTL_REINIT_MEDIA)

#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
// This IOCTL started in Windows 10, 1607. AKA 14393 API. It only supported NVM format with crypto erase though.
// When Windows 11 was released, this API added a structure to specify a sanitize command to run, but is limited to
// block and crypto erase only.
//  This Windows 11/10 API change was 20348 and supposedly also supports Windows 10 21H1
// Win11:
//   Issuing this IOCTL with no parameters on a drive that supports crypto erase will issue sanitize crypto erase with
//   the ause bit set to 1 Issuing this IOCTL with no parameters on a drive that supports block erase will return an
//   error (1) for "incorrect function" Assumed that old drive without sanitize crypto will run the format with crypto
//   erase as this IOCTL originally would do.
// Win10:
//   This will change depending on the version of Windows 10 that is running. 1607 - 1809(?) will do format NVM.
//   Microsoft docs state 1903 and later support sanitize, but it is not clear exactly where the ending cutoff is.
//   Starting with 21H1, it is assumed that the behavior matches Windows 11 behavior. NOTE: Need to figure out if we
//   have a way to test specific versions to figure out the exact cut off. -TJE Assumed that old drive without sanitize
//   crypto will run the format with crypto erase as this IOCTL originally would do.
// Because of all the various changes in behavior, the function below has to assess the incoming command and the drive's
// santize capabilities to decide what it will do.
typedef enum eNVM_ReInit_CompatibleEnum
{
    NVM_REINIT_INCOMPATIBLE_CMD,
    NVM_REINIT_INCOMPATIBLE_FORMAT,
    NVM_REINIT_INCOMPATIBLE_SANITIZE_OLD_API_OR_OS, // compiled with API before 20348 or old version of Windows 10 that
                                                    // doesn't support sanitize.
    NVM_REINIT_INCOMPATIBLE_SANITIZE_OPTIONS,   // a bit or option specified in sanitize is not compatible/able to be
                                                // specified in this IOCTL
    NVM_REINIT_INCOMPATIBLE_SANITIZE_OVERWRITE, // overwrite is not allowed in this API
    NVM_REINIT_COMPATIBLE_FORMAT_CRYPTO,
    NVM_REINIT_COMPATIBLE_SANITIZE_CRYPTO,
    NVM_REINIT_COMPATIBLE_SANITIZE_BLOCK
} eNVM_ReInit_Compatible;

static eNVM_ReInit_Compatible is_NVMe_Cmd_Compatible_With_Reinitialize_Media_IOCTL(nvmeCmdCtx* nvmeIoCtx)
{
    eNVM_ReInit_Compatible compat = NVM_REINIT_INCOMPATIBLE_CMD;
    if (is_Windows_10_Version_1607_Or_Higher())
    {
        // check a couple basic requirements....valid pointer and an admin command
        if (nvmeIoCtx && nvmeIoCtx->commandType == NVM_ADMIN_CMD)
        {
            // first check for format crypto erase.
            if (nvmeIoCtx->cmd.adminCmd.opcode == NVME_ADMIN_CMD_FORMAT_NVM)
            {
                compat = NVM_REINIT_INCOMPATIBLE_FORMAT;
                if (is_Windows_10_Version_1903_Or_Higher() &&
                    le32_to_host(nvmeIoCtx->device->drive_info.IdentifyData.nvme.ctrl.sanicap) & BIT0)
                {
                    // TODO: Figure out exactly when this IOCTL switches to sanitize from format with crypto erase. MSDN
                    // documentatino does not specify earlier than 1903.-TJE Starting with at least 1903, possibly
                    // earlier, this IOCTL will issue sanitize crypto erase instead. because this is not necessarily a
                    // malformed command, but a completeley different than expected behavior, returning this instead.
                    return NVM_REINIT_INCOMPATIBLE_CMD;
                }
                else
                {
                    // next we need to screen other fields to make sure there aren't other things being asked while
                    // running the format.
                    if (get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 11, 9) == 2)
                    {
                        // crypto erase.
                        // Finish validating other parameters.
                        // cannot change PI, metadata, or LBA format.
                        uint32_t reservedBitsDWord10 = get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 12);
                        bool     pil                 = nvmeIoCtx->cmd.adminCmd.cdw10 & BIT8;
                        uint8_t  pi                  = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 5);
                        bool     mset                = nvmeIoCtx->cmd.adminCmd.cdw10 & BIT4;
                        uint8_t  lbaFormat           = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 3, 0);
                        nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
                        if (reservedBitsDWord10 == 0 && !pil && !mset && pi == 0)
                        {
                            // now make sure LBA size matches current settings
                            int16_t powerOfTwo = C_CAST(
                                int16_t, nvmeIoCtx->device->drive_info.IdentifyData.nvme.ns.lbaf[lbaFormat].lbaDS);
                            uint32_t lbaSize = UINT32_C(1);
                            while (powerOfTwo > 0)
                            {
                                lbaSize = lbaSize << UINT32_C(1); // multiply by 2
                                --powerOfTwo;
                            }
                            if (lbaSize == nvmeIoCtx->device->drive_info.deviceBlockSize)
                            {
                                compat = NVM_REINIT_COMPATIBLE_FORMAT_CRYPTO;
                            }
                        }
                    }
                }
            }
            else if (nvmeIoCtx->cmd.adminCmd.opcode == NVME_ADMIN_CMD_SANITIZE)
            {
                compat = NVM_REINIT_INCOMPATIBLE_SANITIZE_OLD_API_OR_OS;
                if (is_Windows_10_Version_21H1_Or_Higher())
                {
                    compat = NVM_REINIT_INCOMPATIBLE_SANITIZE_OPTIONS;
                    // cannot currently specify "no deallocate after sanitize", so if this is set, return incompatible
                    // options
                    if (nvmeIoCtx->cmd.adminCmd.cdw10 & BIT9)
                    {
                        return compat;
                    }
                    if (is_Windows_10_Version_1903_Or_Higher() && nvmeIoCtx->cmd.adminCmd.cdw10 & BIT3)
                    {
                        // microsoft is PROBABLY setting the AUSE bit since that is default behavior in newer versions,
                        // so screen for that since this version is too old to take parameters
                        return compat;
                    }
                    // ignore the following since they only apply to overwrite which isn't supported anyways
                    // ignore "overwrite invert pattern between passes"
                    // ignore "overwrite pass count"
                    // ignore "overwrite pattern"

                    // If we are here and this is a supported OS and API, check for a valid Sanitize operation.
                    switch (get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0))
                    {
                    case SANITIZE_NVM_EXIT_FAILURE_MODE:
                    case SANITIZE_NVM_OVERWRITE:
                        compat = NVM_REINIT_INCOMPATIBLE_SANITIZE_OVERWRITE;
                        break;
                    case SANITIZE_NVM_BLOCK_ERASE:
                        if (is_Windows_10_Version_21H1_Or_Higher())
                        {
                            // can only specify sanitize block erase starting Windows 10 21H1 or higher with this IOCTL
                            compat = NVM_REINIT_COMPATIBLE_SANITIZE_BLOCK;
                        }
                        break;
                    case SANITIZE_NVM_CRYPTO:
                        // 1909 and higher will likely allow sanitize crypto if all the bits are set as expected,
                        // whether the ioctl is given parameters or not. 21h1 definitely will, but between 1909 and 21h1
                        // no parameters are accepted, so it is not clear what the behavior is.
                        compat = NVM_REINIT_COMPATIBLE_SANITIZE_CRYPTO;
                        break;
                    }
                }
            }
        }
    }
    return compat;
}

static eReturnValues nvme_Ioctl_Storage_Reinitialize_Media(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    safe_memset(&nvmeIoCtx->commandCompletionData, sizeof(completionQueueEntry), 0, sizeof(completionQueueEntry));
    if (is_Windows_10_Version_1607_Or_Higher())
    {
        // Now make sure we have either format with crypto erase or sanitize block erase or sanitize crypto erase.
        eNVM_ReInit_Compatible compatIO = is_NVMe_Cmd_Compatible_With_Reinitialize_Media_IOCTL(nvmeIoCtx);
#            if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_20348
        if (is_Windows_10_Version_21H1_Or_Higher() &&
            (compatIO == NVM_REINIT_COMPATIBLE_SANITIZE_CRYPTO || compatIO == NVM_REINIT_COMPATIBLE_SANITIZE_BLOCK))
        {
            // Setup parameters to issue Sanitize block or crypto erase!
            STORAGE_REINITIALIZE_MEDIA reinitMedia;
            safe_memset(&reinitMedia, sizeof(STORAGE_REINITIALIZE_MEDIA), 0, sizeof(STORAGE_REINITIALIZE_MEDIA));
            reinitMedia.Version          = sizeof(STORAGE_REINITIALIZE_MEDIA);
            reinitMedia.Size             = sizeof(STORAGE_REINITIALIZE_MEDIA);
            reinitMedia.TimeoutInSeconds = nvmeIoCtx->timeout;
            switch (get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0))
            {
            case SANITIZE_NVM_EXIT_FAILURE_MODE:
            case SANITIZE_NVM_OVERWRITE:
                return BAD_PARAMETER;
            case SANITIZE_NVM_BLOCK_ERASE:
                reinitMedia.SanitizeOption.SanitizeMethod = StorageSanitizeMethodBlockErase;
                break;
            case SANITIZE_NVM_CRYPTO:
                reinitMedia.SanitizeOption.SanitizeMethod = StorageSanitizeMethodCryptoErase;
                break;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw10 & BIT3)
            {
                reinitMedia.SanitizeOption.DisallowUnrestrictedSanitizeExit = false;
            }
            else
            {
                reinitMedia.SanitizeOption.DisallowUnrestrictedSanitizeExit = true;
            }
            DECLARE_SEATIMER(commandTimer);
            start_Timer(&commandTimer);
            DWORD returnedLength = DWORD_C(0);
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                printf("Sending IOCTL_STORAGE_REINITIALIZE_MEDIA for Sanitize ");
                switch (reinitMedia.SanitizeOption.SanitizeMethod)
                {
                case StorageSanitizeMethodDefault:
                    printf("Default method\n");
                    break;
                case StorageSanitizeMethodBlockErase:
                    printf("Block Erase method\n");
                    break;
                case StorageSanitizeMethodCryptoErase:
                    printf("Crypto Erase method\n");
                    break;
                }
            }
            BOOL result = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_REINITIALIZE_MEDIA, &reinitMedia,
                                          sizeof(STORAGE_REINITIALIZE_MEDIA), M_NULLPTR, 0, &returnedLength, M_NULLPTR);
            stop_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error                    = GetLastError();
            nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
            if (MSFT_BOOL_FALSE(result))
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
                ret = SUCCESS;
            }
        }
        else
#            endif //(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_20348
            if (compatIO == NVM_REINIT_COMPATIBLE_FORMAT_CRYPTO || compatIO == NVM_REINIT_COMPATIBLE_SANITIZE_CRYPTO)
            {
                // Issue without the parameters for Format with crypto erase only! (in 1607)
                // At some point this switched to sanitize crypto when the drive supports it, but it is not clear when.
                // The compatibility checking code assumes this happened in 1903, but it is not clear exactly when that
                // happened. So if we get to this case and one of the two crypto copatible erases is specified, this
                // will issue the command. If we ever get more info to further refine the compatibility checks, we
                // should do that!
                DECLARE_SEATIMER(commandTimer);
                start_Timer(&commandTimer);
                DWORD returnedLength = DWORD_C(0);
                if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
                {
                    printf("Sending IOCTL_STORAGE_REINITIALIZE_MEDIA for Format/Sanitize Crypto\n");
                }
                BOOL result = DeviceIoControl(nvmeIoCtx->device->os_info.fd, IOCTL_STORAGE_REINITIALIZE_MEDIA,
                                              M_NULLPTR, 0, M_NULLPTR, 0, &returnedLength, M_NULLPTR);
                stop_Timer(&commandTimer);
                nvmeIoCtx->device->os_info.last_error                    = GetLastError();
                nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
                if (MSFT_BOOL_FALSE(result))
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
                    ret = SUCCESS;
                }
            }
    }
    return ret;
}
#        endif // WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393

#    endif // USE_IOCTL_REINIT_MEDIA

#    if defined(ENABLE_TRANSLATE_FORMAT)
// This code is currently disabled due to the format unit translation not actually working.
// Also, microsoft updated some online documentation to show this and it clarified that the sanitize CDB issues the
// sanitize command. It is possible that some earlier version of Windows 10 supported format translation or handled
// sanitize differently, but that information is not available.
static eReturnValues win10_Translate_Format(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret                 = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity         = nvmeIoCtx->device->deviceVerbosity;
    uint32_t         reservedBitsDWord10 = get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 12);
    uint8_t          secureEraseSettings = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 11, 9);
    bool             pil                 = nvmeIoCtx->cmd.adminCmd.cdw10 & BIT8;
    uint8_t          pi                  = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 5);
    bool             mset                = nvmeIoCtx->cmd.adminCmd.cdw10 & BIT4;
    uint8_t          lbaFormat           = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 3, 0);
    nvmeIoCtx->device->deviceVerbosity   = VERBOSITY_QUIET;
    if (reservedBitsDWord10 == 0 && !pil && !mset && pi == 0)
    {
        // bool issueFormatCMD = false; //While this says this command is translated, I have yet to figure out which
        // combo makes it work.
        //  I get invalid opcode every time I try issuing format unit.
        //  https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/stornvme-scsi-translation-support
        int16_t  powerOfTwo = C_CAST(int16_t, nvmeIoCtx->device->drive_info.IdentifyData.nvme.ns.lbaf[lbaFormat].lbaDS);
        uint32_t lbaSize    = UINT32_C(1);
        while (powerOfTwo > 0)
        {
            lbaSize = lbaSize << UINT32_C(1); // multiply by 2
            --powerOfTwo;
        }
        if (lbaSize != nvmeIoCtx->device->drive_info.deviceBlockSize)
        {
            // This is NOT supported from what I can tell.
            // The only mode page supported by Windows is the caching mode page. with size set to 0x0A for a total of
            // 12B of data
            //  block descriptors are never returned.
            //  only mode sense 10 works. LongLBA does NOT work.
            //  No matter which combo of bits/fields used for mode select, it is always rejected with invalid field in
            //  CDB.
            return OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            // NO LBA size change.
            // In this case we MIGHT be able to run a secure erase using the non-standard sanitize block erase or crypto
            // erase translation MSFT mentions this briefly in their documentation here:
            // https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/stornvme-command-set-support
            if (secureEraseSettings != 0)
            {
                // issueFormatCMD = false;
                // translated with sanitize
                //  NOTE: If you use sanitize block erase CDB, this translates to sanitize block erase NVM
                //        If you use sanitize crypto erase CDB, it translates to format + crypto erase from what I can
                //        tell. This is not documented, but found through trial and error testing and debugging. I've
                //        commented out the sanitize block erase since this does not issue the expected command.
                // if (secureEraseSettings == 1)
                //{
                //     //block
                //     ret = scsi_Sanitize_Block_Erase(nvmeIoCtx->device, false, false, false);
                // }
                // else
                if (secureEraseSettings == 2)
                {
#        if defined(WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
                    // crypto
                    // NOTE: the IOCTL_STORAGE_REINITIALIZE_MEDIA can be used for this too IF no buffer length is passed
                    // in win 10 1607.
                    //       By 1903, this IOCTL instead issues sanitize crypto erase instead from what I can tell from
                    //       trial and error testing and the limited documentation available. It is unknown when this
                    //       CDB translation became available, but will choose which command to issue depending on the
                    //       fields and OS version as best we can.
                    if (NVM_REINIT_COMPATIBLE_FORMAT_CRYPTO ==
                        is_NVMe_Cmd_Compatible_With_Reinitialize_Media_IOCTL(nvmeIoCtx))
                    {
                        ret = nvme_Ioctl_Storage_Reinitialize_Media(nvmeIoCtx);
                    }
                    else
#        endif // #if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
                    {
                        ret = scsi_Sanitize_Cryptographic_Erase(nvmeIoCtx->device, false, false, false);
                    }
                }
                else
                {
                    ret = OS_COMMAND_NOT_AVAILABLE;
                }
            }
            else
            {
                // I've tried the format unit command without parameters and no luck. I have tried with fmtdata and no
                // luck.
                ret = OS_COMMAND_NOT_AVAILABLE;
            }
        }
        // if (issueFormatCMD)
        //{
        //     //format unit command
        //     //set up parameter data if necessary and send the scsi format unit command to be translated and sent to
        //     the drive. The mode sense/select should have been cached to be used by the format command.
        //     //if (pi == 0)
        //     //{
        //     //    //send without parameter data
        //     //    ret = scsi_Format_Unit(nvmeIoCtx->device, 0, false, false, false, 0, 0, M_NULLPTR, 0, 0, 60);
        //     //}
        //     //else
        //     //{
        //     //send with parameter data
        //     //DECLARE_ZERO_INIT_ARRAY(uint8_t, formatParameterData, 4);//short header
        //     //uint8_t fmtpInfo = UINT8_C(0);
        //     //uint8_t piUsage = UINT8_C(0);
        //     //switch (pi)
        //     //{
        //     //case 0:
        //     //    break;
        //     //case 1:
        //     //    fmtpInfo = 0x2;
        //     //    break;
        //     //case 2:
        //     //    fmtpInfo = 0x3;
        //     //    break;
        //     //case 3:
        //     //    fmtpInfo = 0x3;
        //     //    piUsage = 1;
        //     //    break;
        //     //default:
        //     //    return OS_COMMAND_NOT_AVAILABLE;
        //     //}
        //     //formatParameterData[0] = get_bit_range_uint8(piUsage, 2, 0);
        //     ret = scsi_Format_Unit(nvmeIoCtx->device, 0, false, false, false, 0, 0, M_NULLPTR, 0, 0, 60);
        //     //}
        // }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}
#    endif // ENABLE_TRANSLATE_FORMAT

static eReturnValues win10_Translate_Write_Uncorrectable(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret               = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity       = nvmeIoCtx->device->deviceVerbosity;
    uint64_t         totalCommandTime  = UINT64_C(0);
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    uint64_t lba = M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11),
                                       M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11),
                                       M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10),
                                       M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    for (uint16_t iter = UINT16_C(0); iter < (M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1);
         ++iter) //+1 because nvme uses a zero based range value
    {
        eReturnValues individualCommandRet =
            scsi_Write_Long_16(nvmeIoCtx->device, true, false, false, lba + iter, 0, M_NULLPTR);
        if (individualCommandRet != SUCCESS)
        {
            // This is making sure we don't have a bad command followed by a good command, then missing a bad status
            // code
            ret = individualCommandRet;
        }
        totalCommandTime += nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds;
    }
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = totalCommandTime;
    nvmeIoCtx->device->deviceVerbosity                       = inVerbosity;
    return ret;
}

static eReturnValues win10_Translate_Flush(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret               = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity       = nvmeIoCtx->device->deviceVerbosity;
    nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
    // NOTE: Switched to synchronize cache 10 due to documentation from MSFT only specifying the 10byte CDB opcode - TJE
    ret                                = scsi_Synchronize_Cache_10(nvmeIoCtx->device, false, 0, 0, 0);
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

static eReturnValues win10_Translate_Read(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret         = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    // extract fields from NVMe context, then see if we can put them into a compatible SCSI command
    uint64_t startingLBA =
        M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11),
                            M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11),
                            M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10),
                            M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    bool     limitedRetry          = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT31;
    bool     fua                   = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT30;
    uint8_t  prInfo                = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw12, 29, 26);
    bool     pract                 = prInfo & BIT3;
    uint8_t  prchk                 = get_bit_range_uint8(prInfo, 2, 0);
    uint16_t numberOfLogicalBlocks = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1; // nvme is zero based!
    uint8_t  dsm                   = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw13);
    // bool incompresible = dsm & BIT7;
    // bool sequentialRequest = dsm & BIT6;
    // uint8_t accessLatency = get_bit_range_uint8(dsm, 5, 4);
    // uint8_t accessFrequency = get_bit_range_uint8(dsm, 3, 0);
    uint32_t expectedLogicalBlockAccessTag      = nvmeIoCtx->cmd.nvmCmd.cdw14;
    uint16_t expectedLogicalBlockTagMask        = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
    uint16_t expectedLogicalBlockApplicationTag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
    // now validate all the fields to see if we can send this command...
    uint8_t rdProtect                  = UINT8_C(0xFF);
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
            rdProtect = 1; // or 101b
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
            // don't do anything so we can filter out unsupported fields
            break;
        }
    }
    if (rdProtect != 0xFF && dsm == 0 &&
        !limitedRetry) // rdprotect must be a valid value AND these other fields must not be set...-TJE
    {
        // NOTE: Spec only mentions translations for read 10, 12, 16...but we may also need 32!
        // Even though it isn't in the spec, we'll attempt it anyways when we have certain fields set... - TJE
        if (expectedLogicalBlockAccessTag != 0 || expectedLogicalBlockApplicationTag != 0 ||
            expectedLogicalBlockTagMask != 0)
        {
            // read 32 command
            ret = scsi_Read_32(nvmeIoCtx->device, rdProtect, false, fua, false, startingLBA, 0, numberOfLogicalBlocks,
                               nvmeIoCtx->ptrData, expectedLogicalBlockAccessTag, expectedLogicalBlockApplicationTag,
                               expectedLogicalBlockTagMask, nvmeIoCtx->dataSize);
        }
        else
        {
            // read 16 should work
            ret = scsi_Read_16(nvmeIoCtx->device, rdProtect, false, fua, false, startingLBA, 0, numberOfLogicalBlocks,
                               nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

static eReturnValues win10_Translate_Write(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret         = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
    // extract fields from NVMe context, then see if we can put them into a compatible SCSI command
    uint64_t startingLBA =
        M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11),
                            M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11),
                            M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10),
                            M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10));
    bool     limitedRetry          = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT31;
    bool     fua                   = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT30;
    uint8_t  prInfo                = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw12, 29, 26);
    bool     pract                 = prInfo & BIT3;
    uint8_t  prchk                 = get_bit_range_uint8(prInfo, 2, 0);
    uint8_t  dtype                 = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw12, 23, 20);
    uint16_t numberOfLogicalBlocks = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1; // nvme is zero based!
    uint16_t dspec                 = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw13);
    uint8_t  dsm                   = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw13);
    // bool incompresible = dsm & BIT7;
    // bool sequentialRequest = dsm & BIT6;
    // uint8_t accessLatency = get_bit_range_uint8(dsm, 5, 4);
    // uint8_t accessFrequency = get_bit_range_uint8(dsm, 3, 0);
    uint32_t initialLogicalBlockAccessTag = nvmeIoCtx->cmd.nvmCmd.cdw14;
    uint16_t logicalBlockTagMask          = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
    uint16_t logicalBlockApplicationTag   = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
    // now validate all the fields to see if we can send this command...
    uint8_t wrProtect                  = UINT8_C(0xFF);
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
            wrProtect = 1; // or 101b
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
            // don't do anything so we can filter out unsupported fields
            break;
        }
    }
    if (wrProtect != 0xFF && dsm == 0 && !limitedRetry && dtype == 0 &&
        dspec == 0) // rdprotect must be a valid value AND these other fields must not be set...-TJE
    {
        // NOTE: Spec only mentions translations for write 10, 12, 16...but we may also need 32!
        // Even though it isn't in the spec, we'll attempt it anyways when we have certain fields set... - TJE
        if (initialLogicalBlockAccessTag != 0 || logicalBlockTagMask != 0 || logicalBlockApplicationTag != 0)
        {
            // write 32 command
            ret = scsi_Write_32(nvmeIoCtx->device, wrProtect, false, fua, startingLBA, 0, numberOfLogicalBlocks,
                                nvmeIoCtx->ptrData, initialLogicalBlockAccessTag, logicalBlockApplicationTag,
                                logicalBlockTagMask, nvmeIoCtx->dataSize);
        }
        else
        {
            // write 16 should work
            ret = scsi_Write_16(nvmeIoCtx->device, wrProtect, false, fua, startingLBA, 0, numberOfLogicalBlocks,
                                nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

// MSFT documentation does not show this translation as available. Code left here in case someone wants to test it in
// the future. static eReturnValues win10_Translate_Compare(nvmeCmdCtx *nvmeIoCtx)
//{
//     eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
//     eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
//     //extract fields from NVMe context, then see if we can put them into a compatible SCSI command
//     uint64_t startingLBA = M_BytesTo8ByteValue(M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw11),
//     M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw11), M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw11),
//     M_Byte3(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte2(nvmeIoCtx->cmd.nvmCmd.cdw10), M_Byte1(nvmeIoCtx->cmd.nvmCmd.cdw10),
//     M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10)); bool limitedRetry = nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT31; bool fua =
//     nvmeIoCtx->cmd.nvmCmd.cdw12 & BIT30; uint8_t prInfo = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw12, 29, 26);
//     bool pract = prInfo & BIT3; uint8_t prchk = get_bit_range_uint8(prInfo, 2, 0); uint16_t numberOfLogicalBlocks =
//     M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12) + 1; uint32_t expectedLogicalBlockAccessTag = nvmeIoCtx->cmd.nvmCmd.cdw14;
//     uint16_t expectedLogicalBlockTagMask = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw12);
//     uint16_t expectedLogicalBlockApplicationTag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12);
//     //now validate all the fields to see if we can send this command...
//     uint8_t vrProtect = UINT8_C(0xFF);
//     uint8_t byteCheck = UINT8_C(0xFF);
//     nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
//     if (pract)//this MUST be set for the translation to work
//     {
//         byteCheck = 0;//this should catch all possible translation cases...
//         switch (prchk)
//         {
//         case 7:
//             vrProtect = 1;//or 101b or others...
//             break;
//         case 3:
//             vrProtect = 2;
//             break;
//         case 0:
//             vrProtect = 3;
//             break;
//         case 4:
//             vrProtect = 4;
//             break;
//         default:
//             //don't do anything so we can filter out unsupported fields
//             break;
//         }
//     }
//     if (vrProtect != 0xFF && byteCheck != 0xFF && !limitedRetry && !fua)//vrProtect must be a valid value AND these
//     other fields must not be set...-TJE
//     {
//         //NOTE: Spec only mentions translations for verify 10, 12, 16...but we may also need 32!
//         //Even though it isn't in the spec, we'll attempt it anyways when we have certain fields set... - TJE
//         if (expectedLogicalBlockAccessTag != 0 || expectedLogicalBlockApplicationTag != 0 ||
//         expectedLogicalBlockTagMask != 0)
//         {
//             //verify 32 command
//             ret = scsi_Verify_32(nvmeIoCtx->device, vrProtect, false, byteCheck, startingLBA, 0,
//             numberOfLogicalBlocks, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, expectedLogicalBlockAccessTag,
//             expectedLogicalBlockApplicationTag, expectedLogicalBlockTagMask);
//         }
//         else
//         {
//             //verify 16 should work
//             ret = scsi_Verify_16(nvmeIoCtx->device, vrProtect, false, byteCheck, startingLBA, 0,
//             numberOfLogicalBlocks, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
//         }
//     }
//     nvmeIoCtx->device->deviceVerbosity = inVerbosity;
//     return ret;
// }

// uncomment this to enable returning a not supported value when a context attribute is set
// #define WIN_NVME_DEALLOCATE_CONTEXT_FAILURE

static eReturnValues win10_Translate_Data_Set_Management(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues    ret            = OS_COMMAND_NOT_AVAILABLE;
    eVerbosityLevels inVerbosity    = nvmeIoCtx->device->deviceVerbosity;
    uint16_t         numberOfRanges = M_Byte0(nvmeIoCtx->cmd.nvmCmd.cdw10) + UINT16_C(1); // this is zero based in NVMe!
    bool             deallocate     = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT2;                 // This MUST be set to 1
    bool             integralDatasetForWrite = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT1;        // cannot be supported
    bool             integralDatasetForRead  = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT0;        // cannot be supported
    nvmeIoCtx->device->deviceVerbosity       = VERBOSITY_QUIET;
    if (deallocate &&
        !(integralDatasetForWrite || integralDatasetForRead || (nvmeIoCtx->cmd.nvmCmd.cdw10 >> 8) ||
          (nvmeIoCtx->cmd.nvmCmd.cdw11 >> 3))) // checking for supported/unsupported flags and reserved bits
    {
        // Each range specified will be translated to a SCSI unmap descriptor.
        // NOTE: All context attributes will be ignored since it cannot be translated. It is also optional and the
        // controller may not do anything with it. We can optionally check for this, but I do not think it's really
        // worth it right now. I will have code in place for this, just commented out - TJE
#    if defined(WIN_NVME_DEALLOCATE_CONTEXT_FAILURE)
        bool atLeastOneContextAttributeSet = false;
#    endif // WIN_NVME_DEALLOCATE_CONTEXT_FAILURE
           // first, allocate enough memory for the Unmap command
        uint16_t unmapParameterDataLength = UINT16_C(8) + (UINT16_C(16) * numberOfRanges);
        uint8_t* unmapParameterData       = C_CAST(
            uint8_t*, safe_calloc_aligned(
                          unmapParameterDataLength, sizeof(uint8_t),
                          nvmeIoCtx->device->os_info.minimumAlignment)); // each range is 16 bytes plus an 8 byte header
        if (unmapParameterData)
        {
            // in a loop, set the unmap descriptors
            uint32_t scsiOffset = UINT32_C(8);
            uint32_t nvmOffset  = UINT32_C(0);
            for (uint16_t rangeIter = UINT16_C(0);
                 rangeIter < numberOfRanges && scsiOffset < unmapParameterDataLength && nvmOffset < nvmeIoCtx->dataSize;
                 ++rangeIter, scsiOffset += 16, nvmOffset += 16)
            {
                // get the info we need from the incomming buffer
#    if defined(WIN_NVME_DEALLOCATE_CONTEXT_FAILURE)
                uint32_t nvmContextAttributes =
                    M_BytesTo4ByteValue(nvmeIoCtx->ptrData[nvmOffset + 3], nvmeIoCtx->ptrData[nvmOffset + 2],
                                        nvmeIoCtx->ptrData[nvmOffset + 1], nvmeIoCtx->ptrData[nvmOffset + 0]);
#    endif // WIN_NVME_DEALLOCATE_CONTEXT_FAILURE
                uint32_t nvmLengthInLBAs =
                    M_BytesTo4ByteValue(nvmeIoCtx->ptrData[nvmOffset + 7], nvmeIoCtx->ptrData[nvmOffset + 6],
                                        nvmeIoCtx->ptrData[nvmOffset + 5], nvmeIoCtx->ptrData[nvmOffset + 4]);
                uint64_t nvmStartingLBA =
                    M_BytesTo8ByteValue(nvmeIoCtx->ptrData[nvmOffset + 15], nvmeIoCtx->ptrData[nvmOffset + 14],
                                        nvmeIoCtx->ptrData[nvmOffset + 13], nvmeIoCtx->ptrData[nvmOffset + 12],
                                        nvmeIoCtx->ptrData[nvmOffset + 11], nvmeIoCtx->ptrData[nvmOffset + 10],
                                        nvmeIoCtx->ptrData[nvmOffset + 9], nvmeIoCtx->ptrData[nvmOffset + 8]);
#    if defined(WIN_NVME_DEALLOCATE_CONTEXT_FAILURE)
                if (nvmContextAttributes)
                {
                    atLeastOneContextAttributeSet = true;
                }
#    endif // WIN_NVME_DEALLOCATE_CONTEXT_FAILURE
           // now translate it to a scsi unmap block descriptor
           // LBA
                unmapParameterData[scsiOffset + 0] = M_Byte7(nvmStartingLBA);
                unmapParameterData[scsiOffset + 1] = M_Byte6(nvmStartingLBA);
                unmapParameterData[scsiOffset + 2] = M_Byte5(nvmStartingLBA);
                unmapParameterData[scsiOffset + 3] = M_Byte4(nvmStartingLBA);
                unmapParameterData[scsiOffset + 4] = M_Byte3(nvmStartingLBA);
                unmapParameterData[scsiOffset + 5] = M_Byte2(nvmStartingLBA);
                unmapParameterData[scsiOffset + 6] = M_Byte1(nvmStartingLBA);
                unmapParameterData[scsiOffset + 7] = M_Byte0(nvmStartingLBA);
                // length
                unmapParameterData[scsiOffset + 8]  = M_Byte3(nvmLengthInLBAs);
                unmapParameterData[scsiOffset + 9]  = M_Byte2(nvmLengthInLBAs);
                unmapParameterData[scsiOffset + 10] = M_Byte1(nvmLengthInLBAs);
                unmapParameterData[scsiOffset + 11] = M_Byte0(nvmLengthInLBAs);
                // reserved
                unmapParameterData[scsiOffset + 11] = RESERVED;
                unmapParameterData[scsiOffset + 12] = RESERVED;
                unmapParameterData[scsiOffset + 13] = RESERVED;
                unmapParameterData[scsiOffset + 14] = RESERVED;
                unmapParameterData[scsiOffset + 15] = RESERVED;
            }
            // now set up the unmap parameter list header
            // unmap data length
            unmapParameterData[0] = M_Byte1(unmapParameterDataLength - 2);
            unmapParameterData[1] = M_Byte0(unmapParameterDataLength - 2);
            // block descriptor data length
            unmapParameterData[2] = M_Byte1(scsiOffset - 8);
            unmapParameterData[3] = M_Byte0(scsiOffset - 8);
            // reserved
            unmapParameterData[4] = RESERVED;
            unmapParameterData[5] = RESERVED;
            unmapParameterData[6] = RESERVED;
            unmapParameterData[7] = RESERVED;
#    if defined(WIN_NVME_DEALLOCATE_CONTEXT_FAILURE)
            if (!atLeastOneContextAttributeSet)
#    endif // WIN_NVME_DEALLOCATE_CONTEXT_FAILURE
            {
                // send the command
                ret = scsi_Unmap(nvmeIoCtx->device, false, 0, unmapParameterDataLength, unmapParameterData);
            }
#    if defined(WIN_NVME_DEALLOCATE_CONTEXT_FAILURE)
            else
            {
                ret = OS_COMMAND_NOT_AVAILABLE;
            }
#    endif // WIN_NVME_DEALLOCATE_CONTEXT_FAILURE
            safe_free_aligned(&unmapParameterData);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    nvmeIoCtx->device->deviceVerbosity = inVerbosity;
    return ret;
}

// Sanitize in Windows is tricky.
// For the longest time it was only available in Windows PE.
// In the Win 10, 1607 update, the IOCTL for Storage Reinitialize Media was added, which supported crypto erase.
// While this WAS working, it does not appear to work properly in the latest Windows 10 build and returns "Incorrect
// Function" meaning it is not a supported API. I did ask Microsoft a question about SCSI translation and they updated
// the docs to show the SCSI Sanitize CDB as supported for Block and Crypto erase. Since this IOCTL is not working in
// Win 10 22H2 for some unknown reason, this code will issue the SCSI CDB instead. If we need to we can update this code
// to calling that IOCTL again, but the SSCI CDB's work the same without random errors. -TJE
static eReturnValues win10_Translate_Sanitize(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    if (is_Windows_10_Version_1607_Or_Higher()) // this is for IOCTL reinitialize media. Not 100% sure when SCSI
                                                // Sanitize CDB translation was supported. Update this if we ever find
                                                // out.-TJE
    {
        // This was the original code for the reinitialize IOCTL.
        // Since we ran into a weird compatibility issue, it is commented out.
        // Will need to add some conditions for when to call it, but it will build and issue the IOCTL correctly-TJE
        // #if defined (WIN_API_TARGET_VERSION) && WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
        //     ret = nvme_Ioctl_Storage_Reinitialize_Media(nvmeIoCtx);
        // #endif //WIN_API_TARGET_VERSION >= WIN_API_TARGET_WIN10_14393
        uint8_t action = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0);
        // First check for fields that are not supported
        if (get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 31, 10) > 0 || nvmeIoCtx->cmd.adminCmd.cdw10 & BIT9 ||
            nvmeIoCtx->cmd.adminCmd.cdw10 & BIT8 || get_bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 7, 4) > 0 ||
            action == 0 || action == 1 || action == 3 || action >= 5 || nvmeIoCtx->cmd.adminCmd.cdw11 != 0)
        {
            // Command not supported for translation
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            // NOTE: immediate bit must be set to false for MSFT translation to work.
            bool             ause           = M_ToBool(nvmeIoCtx->cmd.adminCmd.cdw10 & BIT3);
            eVerbosityLevels temp           = nvmeIoCtx->device->deviceVerbosity;
            uint32_t         currentTimeout = nvmeIoCtx->device->drive_info.defaultTimeoutSeconds;
            nvmeIoCtx->device->drive_info.defaultTimeoutSeconds =
                600; // change to 10 minutes since this does not return immediately in Windows-TJE
            nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
            if (action == 4) // crypto
            {
                ret = scsi_Sanitize_Cryptographic_Erase(nvmeIoCtx->device, ause, false, false);
            }
            else if (action == 2) // block
            {
                ret = scsi_Sanitize_Block_Erase(nvmeIoCtx->device, ause, false, false);
            }
            nvmeIoCtx->device->deviceVerbosity                  = temp;
            nvmeIoCtx->device->drive_info.defaultTimeoutSeconds = currentTimeout;
        }
    }
    return ret;
}

// These commands are not supported VIA SCSI translation. There are however other Windows IOCTLs that may work
// static eReturnValues win10_Translate_Reservation_Register(nvmeCmdCtx *nvmeIoCtx)
//{
//     eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
//     eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
//     //Command inputs
//     uint8_t cptpl = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw10, 31, 30);
//     bool iekey = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT3;
//     uint8_t rrega = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw10, 2, 0);
//     //data structure inputs
//     //uint64_t crkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[0], nvmeIoCtx->ptrData[1], nvmeIoCtx->ptrData[2],
//     nvmeIoCtx->ptrData[3], nvmeIoCtx->ptrData[4], nvmeIoCtx->ptrData[5], nvmeIoCtx->ptrData[6],
//     nvmeIoCtx->ptrData[7]); uint64_t nrkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[8], nvmeIoCtx->ptrData[9],
//     nvmeIoCtx->ptrData[10], nvmeIoCtx->ptrData[11], nvmeIoCtx->ptrData[12], nvmeIoCtx->ptrData[13],
//     nvmeIoCtx->ptrData[14], nvmeIoCtx->ptrData[15]);
//     //scsi command stuff
//     DECLARE_ZERO_INIT_ARRAY(uint8_t, scsiCommandData, 24);
//     uint8_t scsiServiceAction = UINT8_C(0);
//     bool issueSCSICommand = false;
//     //now check that those can convert to SCSI...if they can, then convert it!
//     if (!iekey && (rrega == 0 || rrega == 1) && (cptpl == 2 || cptpl == 3))
//     {
//         //can translate. Service action is 0 (Register)
//         scsiServiceAction = 0;
//         //set up the data buffer
//         if (nrkey == 0)
//         {
//             //reservation key
//             safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//         }
//         else
//         {
//             //service action reservation key
//             safe_memcpy(&scsiCommandData[8], 24 - 8, &nvmeIoCtx->ptrData[8], 8);//NRKEY
//         }
//         //aptpl
//         if (cptpl == 3)
//         {
//             scsiCommandData[20] |= BIT0;
//         }
//         issueSCSICommand = true;
//     }
//     else if (iekey && (rrega == 0 || rrega == 1) && (cptpl == 2 || cptpl == 3))
//     {
//         //can translate. Service action is 6 (Register and ignore existing key)
//         scsiServiceAction = 6;
//         //set up the data buffer
//         if (nrkey == 0)
//         {
//             //reservation key
//             safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//         }
//         else
//         {
//             //service action reservation key
//             safe_memcpy(&scsiCommandData[8], 24 - 8, &nvmeIoCtx->ptrData[8], 8);//NRKEY
//         }
//         //aptpl
//         if (cptpl == 3)
//         {
//             scsiCommandData[20] |= BIT0;
//         }
//         issueSCSICommand = true;
//     }
//     else if (!iekey && rrega == 2)
//     {
//         //can translate. service action is 7 (Register and move)
//         scsiServiceAction = 7;
//         //set up the data buffer
//         //reservation key
//         safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//                                                                 //service action reservation key
//         safe_memcpy(&scsiCommandData[8], 24 - 8, &nvmeIoCtx->ptrData[8], 8);//NRKEY
//         issueSCSICommand = true;
//     }
//     if (issueSCSICommand)
//     {
//         //if none of the above checks caught the command, then it cannot be translated
//         nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
//         ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, 0, 24, scsiCommandData);
//         nvmeIoCtx->device->deviceVerbosity = inVerbosity;
//     }
//     return ret;
// }
//
// static eReturnValues win10_Translate_Reservation_Report(nvmeCmdCtx *nvmeIoCtx)
//{
//     eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
//     eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
//     bool issueSCSICommand = false;
//     //command bytes
//     uint32_t numberOfDwords = nvmeIoCtx->cmd.nvmCmd.cdw10 + 1;
//     bool eds = nvmeIoCtx->cmd.nvmCmd.cdw11 & BIT0;
//     if (issueSCSICommand)
//     {
//         //if none of the above checks caught the command, then it cannot be translated
//         nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
//         //ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, 0, 24, scsiCommandData);
//         nvmeIoCtx->device->deviceVerbosity = inVerbosity;
//     }
//     return ret;
// }
//
// static eReturnValues win10_Translate_Reservation_Acquire(nvmeCmdCtx *nvmeIoCtx)
//{
//     eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
//     eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
//     //Command inputs
//     uint8_t rtype = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw10, 15, 8);
//     bool iekey = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT3;
//     uint8_t racqa = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw10, 2, 0);
//     //data structure inputs
//     //uint64_t crkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[0], nvmeIoCtx->ptrData[1], nvmeIoCtx->ptrData[2],
//     nvmeIoCtx->ptrData[3], nvmeIoCtx->ptrData[4], nvmeIoCtx->ptrData[5], nvmeIoCtx->ptrData[6],
//     nvmeIoCtx->ptrData[7]);
//     //uint64_t prkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[8], nvmeIoCtx->ptrData[9], nvmeIoCtx->ptrData[10],
//     nvmeIoCtx->ptrData[11], nvmeIoCtx->ptrData[12], nvmeIoCtx->ptrData[13], nvmeIoCtx->ptrData[14],
//     nvmeIoCtx->ptrData[15]);
//     //scsi command stuff
//     DECLARE_ZERO_INIT_ARRAY(uint8_t, scsiCommandData, 24);
//     uint8_t scsiServiceAction = UINT8_C(0);
//     uint8_t scsiType = UINT8_C(0xF);
//     switch (rtype)
//     {
//     case 0://not a reservation holder
//         scsiType = 0;
//         break;
//     case 1:
//         scsiType = 1;
//         break;
//     case 2:
//         scsiType = 3;
//         break;
//     case 3:
//         scsiType = 5;
//         break;
//     case 4:
//         scsiType = 6;
//         break;
//     case 5:
//         scsiType = 7;
//         break;
//     case 6:
//         scsiType = 8;
//         break;
//     default:
//         //nothing to do...we'll filder out the bad SCSI type below
//         break;
//     }
//     bool issueSCSICommand = false;
//     //now check that those can convert to SCSI...if they can, then convert it!
//     if (!iekey && racqa == 0)
//     {
//         //can translate. Service action is 1 (Reserve)
//         scsiServiceAction = 1;
//         //set up the data buffer
//         //reservation key
//         safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//         issueSCSICommand = true;
//     }
//     else if (!iekey && racqa == 1)
//     {
//         //can translate. Service action is 4 (Preempt)
//         scsiServiceAction = 4;
//         //set up the data buffer
//         //reservation key
//         safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//                                                                 //service action reservation key
//         safe_memcpy(&scsiCommandData[8], 24 - 8, &nvmeIoCtx->ptrData[8], 8);//PRKEY
//         issueSCSICommand = true;
//     }
//     else if (!iekey && racqa == 2)
//     {
//         //can translate. Service action is 5 (Preempt and abort)
//         scsiServiceAction = 5;
//         //set up the data buffer
//         //reservation key
//         safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//                                                                 //service action reservation key
//         safe_memcpy(&scsiCommandData[8], 24 - 8, &nvmeIoCtx->ptrData[8], 8);//PRKEY
//         issueSCSICommand = true;
//     }
//     if (issueSCSICommand && scsiType != 0xF)
//     {
//         //if none of the above checks caught the command, then it cannot be translated
//         nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
//         ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, scsiType, 24, scsiCommandData);
//         nvmeIoCtx->device->deviceVerbosity = inVerbosity;
//     }
//     return ret;
// }
//
// static eReturnValues win10_Translate_Reservation_Release(nvmeCmdCtx *nvmeIoCtx)
//{
//     eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
//     eVerbosityLevels inVerbosity = nvmeIoCtx->device->deviceVerbosity;
//     //Command inputs
//     uint8_t rtype = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw10, 15, 8);
//     bool iekey = nvmeIoCtx->cmd.nvmCmd.cdw10 & BIT3;
//     uint8_t rrela = get_8bit_range_uint32(nvmeIoCtx->cmd.nvmCmd.cdw10, 2, 0);
//     //data structure inputs
//     //uint64_t crkey = M_BytesTo8ByteValue(nvmeIoCtx->ptrData[0], nvmeIoCtx->ptrData[1], nvmeIoCtx->ptrData[2],
//     nvmeIoCtx->ptrData[3], nvmeIoCtx->ptrData[4], nvmeIoCtx->ptrData[5], nvmeIoCtx->ptrData[6],
//     nvmeIoCtx->ptrData[7]);
//     //scsi command stuff
//     DECLARE_ZERO_INIT_ARRAY(uint8_t, scsiCommandData, 24);
//     uint8_t scsiServiceAction = UINT8_C(0);
//     uint8_t scsiType = UINT8_C(0xF);
//     switch (rtype)
//     {
//     case 0://not a reservation holder
//         scsiType = 0;
//         break;
//     case 1:
//         scsiType = 1;
//         break;
//     case 2:
//         scsiType = 3;
//         break;
//     case 3:
//         scsiType = 5;
//         break;
//     case 4:
//         scsiType = 6;
//         break;
//     case 5:
//         scsiType = 7;
//         break;
//     case 6:
//         scsiType = 8;
//         break;
//     default:
//         //nothing to do...we'll filder out the bad SCSI type below
//         break;
//     }
//     bool issueSCSICommand = false;
//     //now check that those can convert to SCSI...if they can, then convert it!
//     if (!iekey && rrela == 0)
//     {
//         //can translate. Service action is 2 (Release)
//         scsiServiceAction = 2;
//         //set up the data buffer
//         //reservation key
//         safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//         issueSCSICommand = true;
//     }
//     else if (!iekey && rrela == 1)
//     {
//         //can translate. Service action is 3 (Clear)
//         scsiServiceAction = 3;
//         //set up the data buffer
//         //reservation key
//         safe_memcpy(&scsiCommandData[0], 24, &nvmeIoCtx->ptrData[0], 8);//CRKEY
//         issueSCSICommand = true;
//     }
//     if (issueSCSICommand && scsiType != 0xF)
//     {
//         //if none of the above checks caught the command, then it cannot be translated
//         nvmeIoCtx->device->deviceVerbosity = VERBOSITY_QUIET;
//         ret = scsi_Persistent_Reserve_Out(nvmeIoCtx->device, scsiServiceAction, 0, scsiType, 24, scsiCommandData);
//         nvmeIoCtx->device->deviceVerbosity = inVerbosity;
//     }
//
//     return ret;
// }

// Windows 10 added a way to query for ATA identify data. Seems to work ok.
// Note: Any odd parameters like a change in TFRs from the spec will not work here.
eReturnValues send_Win_ATA_Identify_Cmd(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues returnValue    = SUCCESS;
    BOOL          result         = FALSE;
    PVOID         buffer         = M_NULLPTR;
    ULONG         bufferLength   = ULONG_C(0);
    ULONG         returnedLength = ULONG_C(0);

    PSTORAGE_PROPERTY_QUERY           query             = M_NULLPTR;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA   protocolData      = M_NULLPTR;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = M_NULLPTR;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) +
                   scsiIoCtx->dataLength;
    buffer = safe_malloc(bufferLength);

    if (buffer == M_NULLPTR)
    {
#    if defined(_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#    endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query             = C_CAST(PSTORAGE_PROPERTY_QUERY, buffer);
    protocolDataDescr = C_CAST(PSTORAGE_PROTOCOL_DATA_DESCRIPTOR, buffer);
    protocolData      = C_CAST(PSTORAGE_PROTOCOL_SPECIFIC_DATA, query->AdditionalParameters);

    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType  = PropertyStandardQuery;

    protocolData->ProtocolType             = ProtocolTypeAta;
    protocolData->DataType                 = AtaDataTypeIdentify; // AtaDataTypeIdentify
    protocolData->ProtocolDataRequestValue = 0;                   // scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
    protocolData->ProtocolDataRequestSubValue =
        0; // M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid);
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength =
        /*M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) * */
        512U; // sector count * 512 = number of bytes

    //
    // Send request down.
    //
#    if defined(_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, scsiIoCtx->device->os_info.name);
#    endif
    DECLARE_SEATIMER(commandTimer);
    start_Timer(&commandTimer);
    result = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_STORAGE_QUERY_PROPERTY, buffer, bufferLength, buffer,
                             bufferLength, &returnedLength, M_NULLPTR);
    stop_Timer(&commandTimer);
    scsiIoCtx->device->os_info.last_error                    = GetLastError();
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (MSFT_BOOL_FALSE(result) || (returnedLength == 0))
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
#    if defined(_DEBUG)
            printf("%s: Error Log - data descriptor header not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < scsiIoCtx->dataLength))
        {
#    if defined(_DEBUG)
            printf("%s: Error Log - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        char* logData = C_CAST(char*, C_CAST(PCHAR, protocolData) + protocolData->ProtocolDataOffset);
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, C_CAST(void*, logData), scsiIoCtx->dataLength);
    }

    safe_free(&buffer);

    return returnValue;
}

eReturnValues send_Win_ATA_Get_Log_Page_Cmd(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues returnValue    = SUCCESS;
    BOOL          result         = FALSE;
    PVOID         buffer         = M_NULLPTR;
    ULONG         bufferLength   = ULONG_C(0);
    ULONG         returnedLength = ULONG_C(0);

    PSTORAGE_PROPERTY_QUERY           query             = M_NULLPTR;
    PSTORAGE_PROTOCOL_SPECIFIC_DATA   protocolData      = M_NULLPTR;
    PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = M_NULLPTR;

    //
    // Allocate buffer for use.
    //
    bufferLength = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) +
                   scsiIoCtx->dataLength + 4096;
    buffer = safe_malloc(bufferLength);

    if (buffer == M_NULLPTR)
    {
#    if defined(_DEBUG)
        printf("%s: allocate buffer failed, exit", __FUNCTION__);
#    endif
        return MEMORY_FAILURE;
    }

    //
    // Initialize query data structure to get Identify Controller Data.
    //
    ZeroMemory(buffer, bufferLength);

    query             = C_CAST(PSTORAGE_PROPERTY_QUERY, buffer);
    protocolDataDescr = C_CAST(PSTORAGE_PROTOCOL_DATA_DESCRIPTOR, buffer);
    protocolData      = C_CAST(PSTORAGE_PROTOCOL_SPECIFIC_DATA, query->AdditionalParameters);

    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType  = PropertyStandardQuery;

    protocolData->ProtocolType             = ProtocolTypeAta;
    protocolData->DataType                 = AtaDataTypeLogPage;                 // AtaDataTypeIdentify
    protocolData->ProtocolDataRequestValue = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow; // LP
    protocolData->ProtocolDataRequestSubValue =
        M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid); // Page number
    protocolData->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocolData->ProtocolDataLength =
        M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) *
        512U; // sector count * 512 = number of bytes

    //
    // Send request down.
    //
#    if defined(_DEBUG)
    printf("%s Drive Path = %s", __FUNCTION__, scsiIoCtx->device->os_info.name);
#    endif
    DECLARE_SEATIMER(commandTimer);
    start_Timer(&commandTimer);
    result = DeviceIoControl(scsiIoCtx->device->os_info.fd, IOCTL_STORAGE_QUERY_PROPERTY, buffer, bufferLength, buffer,
                             bufferLength, &returnedLength, M_NULLPTR);
    stop_Timer(&commandTimer);
    scsiIoCtx->device->os_info.last_error                    = GetLastError();
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (MSFT_BOOL_FALSE(result) || (returnedLength == 0))
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
#    if defined(_DEBUG)
            printf("%s: Error Log - data descriptor header not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }

        protocolData = &protocolDataDescr->ProtocolSpecificData;

        if ((protocolData->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) ||
            (protocolData->ProtocolDataLength < scsiIoCtx->dataLength))
        {
#    if defined(_DEBUG)
            printf("%s: Error Log - ProtocolData Offset/Length not valid\n", __FUNCTION__);
#    endif
            returnValue = OS_PASSTHROUGH_FAILURE;
        }
        char* logData = C_CAST(char*, C_CAST(PCHAR, protocolData) + protocolData->ProtocolDataOffset);
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, C_CAST(void*, logData), scsiIoCtx->dataLength);
    }

    safe_free(&buffer);

    return returnValue;
}

#endif // WINVER >= SEA_WIN32_WINNT_WIN10

// MS NVMe requirements are listed here: https://msdn.microsoft.com/en-us/library/jj134356(v=vs.85).aspx
// Also: Here is a list of all the supported commands and features. This should help with implementing support for
// various commands. Before these links were found, everything that was implemented was based on sparse documentation of
// basic features and trial and error. SCSI Translations:
// https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/stornvme-scsi-translation-support command set
// support: https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/stornvme-command-set-support feature set
// support: https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/stornvme-feature-support Most of the code
// below has been updated according to these docs, however some things may be missing and those enhancements should be
// made to better improve support.
static eReturnValues send_Win_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    // TODO: Should we be checking the nsid in each command before issuing it? This should happen at some point, at
    // least to filter out "all namespaces" for certain commands since MS won't let us issue some of them through their
    // API - TJE
#    if WINVER >= SEA_WIN32_WINNT_WIN10 // This should wrap around anything going through the Windows API...Win 10 is
                                        // required for NVMe IOs
    if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
    {
        // this is only true when attempting the command with the generic storage protocol command IOCTL
        // which is supposed to be used for VU commands only. - TJE
        bool useNVMPassthrough = false;
        switch (nvmeIoCtx->cmd.adminCmd.opcode)
        {
        case NVME_ADMIN_CMD_IDENTIFY:
            ret = send_Win_NVMe_Identify_Cmd(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_GET_LOG_PAGE:
            // Notes about telemetry log:
            /*
            Supported through IOCTL_SCSI_PASS_THROUGH using command SCSIOP_READ_DATA_BUFF16 with buffer mode as
            READ_BUFFER_MODE_ERROR_HISTORY. Also available through
            StorageAdapterProtocolSpecificProperty/StorageDeviceProtocolSpecificProperty from
            IOCTL_STORAGE_QUERY_PROPERTY. For host telemetry, this is also available through
            IOCTL_STORAGE_GET_DEVICE_INTERNAL_LOG. Older versions of Win10 do not allow pulling in segments with storage
            query property. May need to check Windows 10 version to change between APIs to get this data.-TJE
            */
            ret = send_Win_NVMe_Get_Log_Page_Cmd(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_GET_FEATURES:
            // NOTE: I don't know if this will work for different select field values...will need to debug this to find
            // out. If different select fields are not supported with this call, then we either need to do a SCSI
            // translation or we have to return "not-supported" - TJE
            ret = send_Win_NVMe_Get_Features_Cmd(nvmeIoCtx);
            break;
        case NVME_ADMIN_CMD_DOWNLOAD_FW:
            if (nvmeIoCtx->device->os_info.fwdlMiniportSupported)
            {
                ret = send_Win_NVME_Firmware_Miniport_Download(nvmeIoCtx);
            }
            else
            {
                ret = send_Win_NVMe_Firmware_Image_Download_Command(nvmeIoCtx);
            }
            break;
        case NVME_ADMIN_CMD_ACTIVATE_FW:
            if (nvmeIoCtx->device->os_info.fwdlMiniportSupported)
            {
                ret = send_Win_NVME_Firmware_Miniport_Activate(nvmeIoCtx);
            }
            else
            {
                ret = send_Win_NVMe_Firmware_Activate_Command(nvmeIoCtx);
            }
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
            if (is_Windows_PE())
            {
                useNVMPassthrough = true;
            }
#        if defined(ENABLE_TRANSLATE_FORMAT)
            else
            {
                ret = win10_Translate_Format(nvmeIoCtx);
            }
#        endif // ENABLE_TRANSLATE_FORMAT
            break;
        case NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT:
        case NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT:
        case NVME_ADMIN_CMD_NVME_MI_SEND:
        case NVME_ADMIN_CMD_NVME_MI_RECEIVE:
            if (is_Windows_PE())
            {
                useNVMPassthrough = true;
            }
            break;
        case NVME_ADMIN_CMD_SANITIZE:
            if (is_Windows_PE())
            {
                useNVMPassthrough = true;
            }
            else
            {
                ret = win10_Translate_Sanitize(nvmeIoCtx);
            }
            break;
        case NVME_ADMIN_CMD_DEVICE_SELF_TEST:
            if (is_Windows_10_Version_1903_Or_Higher())
            {
                useNVMPassthrough = true;
            }
            break;
        default:
            // Check if it's a vendor unique op code.
            if (nvmeIoCtx->cmd.adminCmd.opcode >=
                0xC0 /*&& nvmeIoCtx->cmd.adminCmd.opcode <= 0xFF*/) // admin commands in this range are vendor unique
            {
                useNVMPassthrough = true;
            }
            break;
        }
        if (useNVMPassthrough)
        {
            // Call the function to send a VU command using STORAGE_PROTOCOL_COMMAND - TJE
            ret = send_NVMe_Vendor_Unique_IO(nvmeIoCtx);
        }
    }
    else if (nvmeIoCtx->commandType == NVM_CMD)
    {
        // this is only true when attempting the command with the generic storage protocol command IOCTL
        // which is supposed to be used for VU commands only. - TJE
        bool useNVMPassthrough = false;
        switch (nvmeIoCtx->cmd.adminCmd.opcode)
        {
        case NVME_CMD_WRITE_UNCOR: // SCSI Write long command
            ret = win10_Translate_Write_Uncorrectable(nvmeIoCtx);
            break;
        case NVME_CMD_FLUSH:
            ret = win10_Translate_Flush(nvmeIoCtx);
            break;
        case NVME_CMD_READ: // NOTE: This translation likely won't be hit since most code calls into the read function
                            // in cmds.h which will call os_Read instead. This is here for those requesting something
                            // specific...-TJE
            ret = win10_Translate_Read(nvmeIoCtx);
            break;
        case NVME_CMD_WRITE: // NOTE: This translation likely won't be hit since most code calls into the read function
                             // in cmds.h which will call os_Read instead. This is here for those requesting something
                             // specific...-TJE
            ret = win10_Translate_Write(nvmeIoCtx);
            break;
        case NVME_CMD_COMPARE: // according to MSFT documentation, this is only available in WinPE
            // ret = win10_Translate_Compare(nvmeIoCtx);
            if (is_Windows_PE())
            {
                useNVMPassthrough = true;
            }
            break;
        case NVME_CMD_DATA_SET_MANAGEMENT: // SCSI Unmap or Win API call?
            ret = win10_Translate_Data_Set_Management(nvmeIoCtx);
            break;
        // NOTE: No reservation commands are supported according to MSFT docs. Code for attempting SCSI commands is
        // available, and MSFT does have IOCTLs specific for reservations that might work, but it is not documented
        default:
            // Check if it's a vendor unique op code.
            if (nvmeIoCtx->cmd.adminCmd.opcode >=
                0x80 /* && nvmeIoCtx->cmd.adminCmd.opcode <= 0xFF */) // admin commands in this range are vendor unique
            {
                useNVMPassthrough = true;
            }
            break;
        }
        if (useNVMPassthrough)
        {
            // Call the function to send a VU command using STORAGE_PROTOCOL_COMMAND - TJE
            ret = send_NVMe_Vendor_Unique_IO(nvmeIoCtx);
        }
    }
#    else  // WINVER < SEA_WIN32_WINNT_WIN10
    M_USE_UNUSED(nvmeIoCtx);
#    endif // WINVER >= SEA_WIN32_WINNT_WIN10
    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    M_USE_UNUSED(nvmeIoCtx);
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISBALE_NVME_PASSTHROUGH
}

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    switch (nvmeIoCtx->device->drive_info.interface_type)
    {
    case NVME_INTERFACE:
#if defined(ENABLE_OFNVME)
        if (nvmeIoCtx->device->os_info.openFabricsNVMePassthroughSupported)
        {
            ret = send_OFNVME_IO(nvmeIoCtx);
        }
        else
#endif // ENABLE_OFNVME
#if defined(ENABLE_INTEL_RST)
            if (nvmeIoCtx->device->os_info.intelNVMePassthroughSupported)
        {
            ret = send_Intel_NVM_Command(nvmeIoCtx);
        }
        else
#endif // ENABLE_INTEL_RST
        {
            ret = send_Win_NVMe_IO(nvmeIoCtx);
        }
        break;
    case RAID_INTERFACE:
        if (nvmeIoCtx->device->issue_nvme_io != M_NULLPTR)
        {
            ret = nvmeIoCtx->device->issue_nvme_io(nvmeIoCtx);
        }
        else
        {
            if (VERBOSITY_QUIET < nvmeIoCtx->device->deviceVerbosity)
            {
                printf("Raid PassThrough Interface is not supported for this device - NVMe\n");
            }
            ret = NOT_SUPPORTED;
        }
        break;
    default:
        if (VERBOSITY_QUIET < nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Target Device does not have a valid interface\n");
        }
        ret = BAD_PARAMETER;
        break;
    }

    if (nvmeIoCtx->device->delay_io)
    {
        delay_Milliseconds(nvmeIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", nvmeIoCtx->device->delay_io);
        }
    }

    return ret;
}

eReturnValues os_nvme_Reset(tDevice* device)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    // This is a stub. We may not be able to do this in Windows, but want this here in case we can and to make code
    // otherwise compile without ifdefs
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Sending NVMe Reset\n");
    }
#if defined(ENABLE_OFNVME)
    if (device->os_info.openFabricsNVMePassthroughSupported)
    {
        ret = send_OFNVME_Reset(device);
    }
#endif // ENABLE_OFNVME
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        print_Return_Enum("NVMe Reset", ret);
    }
    return ret;
}

eReturnValues os_nvme_Subsystem_Reset(tDevice* device)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    // This is a stub. We may not be able to do this in Windows, but want this here in case we can and to make code
    // otherwise compile without ifdefs
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Sending NVMe Subsystem Reset\n");
    }
#if defined(ENABLE_OFNVME)
    if (device->os_info.openFabricsNVMePassthroughSupported)
    {
        ret = send_OFNVME_Reset(device);
    }
#endif // ENABLE_OFNVME
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        print_Return_Enum("NVMe Subsystem Reset", ret);
    }
    return ret;
}

eReturnValues pci_Read_Bar_Reg(M_ATTR_UNUSED tDevice* device,
                               M_ATTR_UNUSED uint8_t* pData,
                               M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

static eReturnValues open_Force_Unit_Access_Handle_For_OS_Read_OS_Write(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.forceUnitAccessRWfd == M_NULLPTR || device->os_info.forceUnitAccessRWfd == INVALID_HANDLE_VALUE)
    {
        // handle is not yet opened
        // FILE_FLAG_NO_BUFFERING - this is for the operating system/file system caching. NOT THE DISK!
        // FILE_FLAG_WRITE_THROUGH - this seems to mean FUA based on the description of the flag
        //  The following link describes the flags. It sounds like we need both of them for FUA otherwise it may go to
        //  the system cache and then be flushed, which may not be the same- TJE
        // See here for more info:
        // https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea#caching_behavior
        DECLARE_ZERO_INIT_ARRAY(TCHAR, fuaDevice, WIN_MAX_DEVICE_NAME_LENGTH);
        TCHAR* ptrFuaDevice = &fuaDevice[0];
        _stprintf_s(fuaDevice, WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs"), device->os_info.name);
        device->os_info.forceUnitAccessRWfd = CreateFile(ptrFuaDevice, GENERIC_WRITE | GENERIC_READ,
                                                         FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
                                                         FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING |
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                                             FILE_FLAG_OVERLAPPED,
#endif // WINDOWS_DISABLE_OVERLAPPED
                                                         M_NULLPTR);
        if (device->os_info.forceUnitAccessRWfd == INVALID_HANDLE_VALUE)
        {
            ret = NOT_SUPPORTED; // Or should this be set to failure??? - TJE
        }
    }
    return ret;
}

static void set_Command_Completion_For_OS_Read_Write_NVMe(tDevice* device, DWORD lastError)
{
    // For nvme, set the NVMe status as best we can, then fall through and set SCSI style sense data as well.
    // This switch case will handle many, if not all the same cases as SCSI below, but this seemed like the easier
    // way to solve this problem for now. - TJE if the DISABLE_NVME_PASSTHROUGH flag is refactored, this can
    // probably be cleaned up a lot - TJE
    device->drive_info.lastNVMeResult.lastNVMeCommandSpecific =
        0; // cannot report this as far as I know, so clear it to zero
    switch (lastError)
    {
    case ERROR_NOT_READY: // sense key not ready
        // namespace not ready
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_NS_NOT_READY_);
        break;
    case ERROR_WRITE_PROTECT:
        // attempted to write to read-only range
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, NVME_CMD_SP_SC_ATTEMPTED_WRITE_TO_READ_ONLY_RANGE);
        break;
    case ERROR_WRITE_FAULT:
        // write fault
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_WRITE_FAULT_);
        break;
    case ERROR_READ_FAULT: // should this be "Deallocated or unwritten logical block on NVME?
    case ERROR_DEVICE_HARDWARE_ERROR:
        // internal device error
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INTERNAL_);
        break;
    case ERROR_CRC: // medium error, uncorrectable data
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS, NVME_MED_ERR_SC_UNREC_READ_ERROR_);
        break;
    case ERROR_SEEK:             // cannot find area or track on disk?
        M_FALLTHROUGH;           // Fallthrough for now unless we can figure out a better, more specific error when this
                                 // happens - TJE
    case ERROR_SECTOR_NOT_FOUND: // ID not found (beyond max LBA type error)
        // lba out of range
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_LBA_RANGE_);
        break;
    case ERROR_OFFSET_ALIGNMENT_VIOLATION: // alignment error for the device
        // namespace not ready??? THere doesn't seem to be anything similar in the spec like SAS/SATA
        // have...probably because LBAs don't report differing logical and physical size
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_NS_NOT_READY_);
        break;
    case ERROR_TIMEOUT:
        // command abort requested. Assume this system asked to abort this when it took too long
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_ABORT_REQ_);
        break;
    case ERROR_DEVICE_NOT_CONNECTED: // CRC error???
        // data transfer error?
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_DATA_XFER_ERROR_);
        break;
    case ERROR_BAD_COMMAND:
        // invalid op code??? or invalid field in command? no idea...-TJE
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_INVALID_OPCODE_);
        break;
    case ERROR_INVALID_DATA: // Not sure if this is the same as CRC or something else, so this may need changing if
                             // we see it in the future.
    case ERROR_DATA_CHECKSUM_ERROR: // Not sure if this will show up for RAW IO like this is doing or not, but we
                                    // may need a case for this in the future.
        // data transfer error?
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_DATA_XFER_ERROR_);
        break;
    default:
        // setting to abort requested since we don't know what else to set...generic enough
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_ABORT_REQ_);
        break;
    }
}

static void set_Command_Completion_For_OS_Read_Write_ATA(tDevice* device, DWORD lastError)
{
    device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_ERROR;
    switch (lastError)
    {
        // Some of these are in here "just in case", but this is not a comprehensive list of what could be returned.
        // Some may never be returned, others may not be in this list and are falling to the default case - TJE The
        // only one I haven't been able to find a good answer for is an interface CRC error, which are hard to
        // create and test for - TJE
    case ERROR_WRITE_FAULT:
    case ERROR_READ_FAULT:
    case ERROR_DEVICE_HARDWARE_ERROR:
        device->drive_info.lastCommandRTFRs.status |= ATA_STATUS_BIT_DEVICE_FAULT;
        break;
    case ERROR_CRC: // medium error, uncorrectable data
        device->drive_info.lastCommandRTFRs.error |= ATA_ERROR_BIT_UNCORRECTABLE_DATA;
        break;
    case ERROR_SEEK:             // cannot find area or track on disk?
        M_FALLTHROUGH;           // Fallthrough for now unless we can figure out a better, more specific error when this
                                 // happens - TJE
    case ERROR_SECTOR_NOT_FOUND: // ID not found (beyond max LBA type error)
        device->drive_info.lastCommandRTFRs.error |= ATA_ERROR_BIT_ID_NOT_FOUND;
        break;
    case ERROR_OFFSET_ALIGNMENT_VIOLATION: // alignment error for the device
        device->drive_info.lastCommandRTFRs.status |= ATA_STATUS_BIT_ALIGNMENT_ERROR;
        break;
    default:
        // set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to
        // right now
        device->drive_info.lastCommandRTFRs.error |= ATA_ERROR_BIT_ABORT;
        break;
    }
}

static eReturnValues set_Command_Completion_For_OS_Read_Write(tDevice* device, DWORD lastError)
{
    eReturnValues ret = SUCCESS;
    // clear the last command sense data and rtfrs. We'll dummy them up in a minute
    safe_memset(&device->drive_info.lastCommandRTFRs, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
    safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0, SPC3_SENSE_LEN);
    if (lastError == ERROR_SUCCESS)
    {
        device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = 0;
        device->drive_info.lastNVMeResult.lastNVMeStatus =
            WIN_DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, NVME_GEN_SC_SUCCESS_);
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            device->drive_info.lastCommandRTFRs.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
        }
    }
    else
    {
        uint8_t senseKey = UINT8_C(0);
        uint8_t asc      = UINT8_C(0);
        uint8_t ascq     = UINT8_C(0);
        // NOLINTBEGIN(bugprone-branch-clone)
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            set_Command_Completion_For_OS_Read_Write_NVMe(device, lastError);
        }
        else if (device->drive_info.drive_type == ATA_DRIVE)
        {
            set_Command_Completion_For_OS_Read_Write_ATA(device, lastError);
        }
        // NOLINTEND(bugprone-branch-clone)

        device->drive_info.lastCommandSenseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
        switch (device->os_info.last_error)
        {
            // Some of these are in here "just in case", but this is not a comprehensive list of what could be returned.
            // Some may never be returned, others may not be in this list and are falling to the default case - TJE The
            // only one I haven't been able to find a good answer for is an interface CRC error, which are hard to
            // create and test for - TJE
        case ERROR_NOT_READY: // sense key not ready...not sure this matches anything in ATA if this even were to happen
                              // there.
            senseKey = SENSE_KEY_NOT_READY;
            // no other information can be provided
            break;
        case ERROR_WRITE_PROTECT:
            senseKey = SENSE_KEY_DATA_PROTECT;
            asc      = 0x27;
            ascq     = 0x00;
            break;
        case ERROR_WRITE_FAULT:
        case ERROR_READ_FAULT:
        case ERROR_DEVICE_HARDWARE_ERROR:
            senseKey = SENSE_KEY_HARDWARE_ERROR;
            asc      = 0x44;
            ascq     = 0;
            break;
        case ERROR_CRC: // medium error, uncorrectable data
            senseKey = SENSE_KEY_MEDIUM_ERROR;
            asc      = 0x11;
            ascq     = 0;
            break;
        case ERROR_SEEK:   // cannot find area or track on disk?
            M_FALLTHROUGH; // Fallthrough for now unless we can figure out a better, more specific error when this
                           // happens - TJE
        case ERROR_SECTOR_NOT_FOUND: // ID not found (beyond max LBA type error)
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x21;
            ascq     = 0x00;
            break;
        case ERROR_OFFSET_ALIGNMENT_VIOLATION: // alignment error for the device
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x21;
            ascq = 0x04; // technically this is "unaligned write command" which would not be accurate with a read, but
                         // this is the best I can do right now....maybe 07 for read boundary error???
            break;
        case ERROR_TIMEOUT:
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            break;
        case ERROR_DEVICE_NOT_CONNECTED: // CRC error???
            senseKey = SENSE_KEY_MEDIUM_ERROR;
            // INFORMATION UNIT iuCRC ERROR DETECTED
            asc  = 0x47;
            ascq = 0x03;
            break;
        case ERROR_BAD_COMMAND:
        case ERROR_INVALID_DATA: // Not sure if this is the same as CRC or something else, so this may need changing if
                                 // we see it in the future.
        case ERROR_DATA_CHECKSUM_ERROR: // Not sure if this will show up for RAW IO like this is doing or not, but we
                                        // may need a case for this in the future.
        default:
            // set the sense key to aborted command...don't set the asc or ascq since we don't know what to set those to
            // right now
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            break;
        }
        device->drive_info.lastCommandSenseData[2] |= senseKey;
        if (asc || ascq)
        {
            device->drive_info.lastCommandSenseData[7] =
                6; // get to bytes 12 & 13 for asc info...or should this change to a value of 7 to include fru, even
                   // though that is impossible for us to figure out??? - TJE
            device->drive_info.lastCommandSenseData[12] = asc;
            device->drive_info.lastCommandSenseData[13] = ascq;
        }
    }
    return ret;
}

// The overlapped structure used here changes it to asynchronous IO, but the synchronous portions of code are left here
// in case the device responds as a synchronous device and ignores the overlapped strucutre...it SHOULD work on any
// device like this. See here: https://msdn.microsoft.com/en-us/library/windows/desktop/aa365683(v=vs.85).aspx
eReturnValues os_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret         = UNKNOWN;
    eReturnValues openFUA     = SUCCESS;
    HANDLE        handleToUse = device->os_info.fd;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Read LBAs\n");
    }
    if (forceUnitAccess)
    {
        openFUA = open_Force_Unit_Access_Handle_For_OS_Read_OS_Write(device);
        if (openFUA == SUCCESS)
        {
            handleToUse = device->os_info.forceUnitAccessRWfd;
        }
    }
    // used for setting the timeout
    COMMTIMEOUTS comTimeout;
    safe_memset(&comTimeout, sizeof(COMMTIMEOUTS), 0, sizeof(COMMTIMEOUTS));
    /*BOOL timeoutGot = */
    GetCommTimeouts(handleToUse, &comTimeout); // get timeouts if possible before trying to change them...
    uint64_t timeoutInSeconds = UINT64_C(0);
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        comTimeout.ReadTotalTimeoutConstant = 15000; // 15 seconds
        timeoutInSeconds                    = 15;
    }
    else
    {
        comTimeout.ReadTotalTimeoutConstant =
            device->drive_info.defaultTimeoutSeconds * 1000; // convert time in seconds to milliseconds
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    /*BOOL timeoutSet = */
    SetCommTimeouts(handleToUse, &comTimeout);
    device->os_info.last_error = GetLastError();
    // for use by the setFilePointerEx function
    LARGE_INTEGER liDistanceToMove;
    safe_memset(&liDistanceToMove, sizeof(LARGE_INTEGER), 0, sizeof(LARGE_INTEGER));
    LARGE_INTEGER lpNewFilePointer;
    safe_memset(&lpNewFilePointer, sizeof(LARGE_INTEGER), 0, sizeof(LARGE_INTEGER));
    // set the distance to move in bytes
    liDistanceToMove.QuadPart = C_CAST(LONGLONG, lba * device->drive_info.deviceBlockSize);
    // set the offset here
    BOOL retStatus = SetFilePointerEx(handleToUse, liDistanceToMove, &lpNewFilePointer, FILE_BEGIN);
    if (MSFT_BOOL_FALSE(retStatus))
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            print_Return_Enum("Windows API Read", OS_PASSTHROUGH_FAILURE);
        }
        return OS_PASSTHROUGH_FAILURE;
    }
    DWORD bytesReturned = DWORD_C(0);

    // this api call will need some changes when asynchronous support is added in
    DECLARE_SEATIMER(commandTimer);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent     = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    overlappedStruct.Offset     = M_DoubleWord0(lba * device->drive_info.deviceBlockSize);
    overlappedStruct.OffsetHigh = M_DoubleWord1(lba * device->drive_info.deviceBlockSize);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    start_Timer(&commandTimer);
    if (forceUnitAccess && openFUA != SUCCESS)
    {
        // could not get a FUA handle...so emulate with a verify command before the read - TJE
        os_Verify(device, lba, dataSize / device->drive_info.deviceBlockSize);
    }
    retStatus                  = ReadFile(handleToUse, ptrData, dataSize, &bytesReturned, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        device->os_info.last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                    // overlapped flag, everything will work like old synchronous code.-TJE
    {
        retStatus = GetOverlappedResult(handleToUse, &overlappedStruct, &bytesReturned, TRUE);
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    device->os_info.last_error = GetLastError();
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (MSFT_BOOL_FALSE(retStatus)) // not successful
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
    DISABLE_NONNULL_COMPARE
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && ptrData != M_NULLPTR)
    {
        printf("\t  Data Buffer being returned:\n");
        print_Data_Buffer(ptrData, dataSize, true);
        printf("\n");
    }
    RESTORE_NONNULL_COMPARE

    if (bytesReturned != C_CAST(DWORD, dataSize))
    {
        // error, didn't get all the data
        ret = FAILURE;
    }

    ret = set_Command_Completion_For_OS_Read_Write(device, device->os_info.last_error);

    // check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) >= timeoutInSeconds)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Read", ret);
    }
    return ret;
}

eReturnValues os_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret         = UNKNOWN;
    eReturnValues openFUA     = SUCCESS;
    HANDLE        handleToUse = device->os_info.fd;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Write LBAs\n");
    }
    if (forceUnitAccess)
    {
        openFUA = open_Force_Unit_Access_Handle_For_OS_Read_OS_Write(device);
        if (openFUA == SUCCESS)
        {
            handleToUse = device->os_info.forceUnitAccessRWfd;
        }
    }
    // used for setting the timeout
    COMMTIMEOUTS comTimeout;
    safe_memset(&comTimeout, sizeof(COMMTIMEOUTS), 0, sizeof(COMMTIMEOUTS));
    /*BOOL timeoutGot = */
    GetCommTimeouts(handleToUse, &comTimeout); // get timeouts if possible before trying to change them...
    uint64_t timeoutInSeconds = UINT64_C(0);
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        comTimeout.WriteTotalTimeoutConstant = 15000; // 15 seconds
        timeoutInSeconds                     = 15;
    }
    else
    {
        comTimeout.WriteTotalTimeoutConstant =
            device->drive_info.defaultTimeoutSeconds * 1000; // convert time in seconds to milliseconds
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    /*BOOL timeoutSet = */
    SetCommTimeouts(handleToUse, &comTimeout);
    device->os_info.last_error = GetLastError();
    // for use by the setFilePointerEx function
    LARGE_INTEGER liDistanceToMove;
    safe_memset(&liDistanceToMove, sizeof(LARGE_INTEGER), 0, sizeof(LARGE_INTEGER));
    LARGE_INTEGER lpNewFilePointer;
    safe_memset(&lpNewFilePointer, sizeof(LARGE_INTEGER), 0, sizeof(LARGE_INTEGER));
    // set the distance to move in bytes
    liDistanceToMove.QuadPart = C_CAST(LONGLONG, lba * device->drive_info.deviceBlockSize);
    // set the offset here
    BOOL retStatus = SetFilePointerEx(handleToUse, liDistanceToMove, &lpNewFilePointer, FILE_BEGIN);
    if (MSFT_BOOL_FALSE(retStatus))
    {
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            print_Return_Enum("Windows API Write", OS_PASSTHROUGH_FAILURE);
        }
        return OS_PASSTHROUGH_FAILURE;
    }
    DWORD bytesReturned = DWORD_C(0);

    // this api call will need some changes when asynchronous support is added in
    DECLARE_SEATIMER(commandTimer);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent     = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    overlappedStruct.Offset     = M_DoubleWord0(lba * device->drive_info.deviceBlockSize);
    overlappedStruct.OffsetHigh = M_DoubleWord1(lba * device->drive_info.deviceBlockSize);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    DISABLE_NONNULL_COMPARE
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && ptrData != M_NULLPTR)
    {
        printf("\t  Data Buffer being sent:\n");
        print_Data_Buffer(ptrData, dataSize, true);
        printf("\n");
    }
    RESTORE_NONNULL_COMPARE
    start_Timer(&commandTimer);
    retStatus                  = WriteFile(handleToUse, ptrData, dataSize, &bytesReturned, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        device->os_info.last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                    // overlapped flag, everything will work like old synchronous code.-TJE
    {
        retStatus = GetOverlappedResult(handleToUse, &overlappedStruct, &bytesReturned, TRUE);
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    if (forceUnitAccess && openFUA != SUCCESS)
    {
        // could not get a FUA handle...so emulate with a verify command after the write - TJE
        os_Verify(device, lba, dataSize / device->drive_info.deviceBlockSize);
    }
    stop_Timer(&commandTimer);
    device->os_info.last_error = GetLastError();
    if (overlappedStruct.hEvent != M_NULLPTR && overlappedStruct.hEvent != INVALID_HANDLE_VALUE)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
    }
    overlappedStruct.hEvent                       = M_NULLPTR;
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    if (MSFT_BOOL_FALSE(retStatus)) // not successful
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
    if (bytesReturned != C_CAST(DWORD, dataSize))
    {
        // error, didn't get all the data
        ret = FAILURE;
    }

    ret = set_Command_Completion_For_OS_Read_Write(device, device->os_info.last_error);

    // check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) >= timeoutInSeconds)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Write", ret);
    }
    return ret;
}
#if WINVER >= SEA_WIN32_WINNT_WINXP
// IOCTL is for Win XP and higher
// Seems to work. Needs some enhancements with timers and checking return codes more closely to dummy up better sense
// data.
eReturnValues os_Verify(tDevice* device, uint64_t lba, uint32_t range)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Verify LBAs\n");
    }
    VERIFY_INFORMATION verifyCmd;
    safe_memset(&verifyCmd, sizeof(VERIFY_INFORMATION), 0, sizeof(VERIFY_INFORMATION));
    DECLARE_SEATIMER(verifyTimer);
    verifyCmd.StartingOffset.QuadPart =
        C_CAST(LONGLONG, lba * device->drive_info.deviceBlockSize); // LBA needs to be converted to a byte offset
    verifyCmd.Length          = range * device->drive_info.deviceBlockSize; // needs to be a range in bytes!
    uint64_t timeoutInSeconds = UINT64_C(0);
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        timeoutInSeconds = 15;
    }
    else
    {
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    DWORD      returnedBytes = DWORD_C(0);
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent     = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    overlappedStruct.Offset     = M_DoubleWord0(lba * device->drive_info.deviceBlockSize);
    overlappedStruct.OffsetHigh = M_DoubleWord1(lba * device->drive_info.deviceBlockSize);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    start_Timer(&verifyTimer);
    BOOL success = DeviceIoControl(device->os_info.fd, IOCTL_DISK_VERIFY, &verifyCmd, sizeof(VERIFY_INFORMATION),
                                   M_NULLPTR, 0, &returnedBytes, &overlappedStruct);
    device->os_info.last_error = GetLastError();
    if (ERROR_IO_PENDING ==
        device->os_info.last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                    // overlapped flag, everything will work like old synchronous code.-TJE
    {
        success = GetOverlappedResult(device->os_info.fd, &overlappedStruct, &returnedBytes, TRUE);
    }
    else if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&verifyTimer);
    device->os_info.last_error = GetLastError();
    if (MSFT_BOOL_FALSE(success)) // not successful
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(device->os_info.last_error);
        }
    }
    if (overlappedStruct.hEvent)
    {
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
    }
    ret = set_Command_Completion_For_OS_Read_Write(device, device->os_info.last_error);
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(verifyTimer);
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    // check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) >= timeoutInSeconds)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Verify", ret);
    }
    return ret;
}
#else
// verify IOTCL is not available so we need to just do a flush and a read.
eReturnValues os_Verify(tDevice* device, uint64_t lba, uint32_t range)
{
    // flush the cache first to make sure we aren't reading something that is in cache than disk (as close as we can get
    // right here)
    os_Flush(device);
    // now do a read and throw away the data
    uint8_t* readData = M_REINTERPRET_CAST(
        uint8_t*,
        safe_calloc(uint32_to_sizet(device->drive_info.deviceBlockSize) * uint32_to_sizet(range), sizeof(uint8_t)));
    if (readData)
    {
        ret = os_Read(device, lba, false, readData, device->drive_info.deviceBlockSize * range);
        safe_free(&readData);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}
#endif

// This is for Windows XP and higher. This should issue a flush cache or synchronize cache command for us
eReturnValues os_Flush(tDevice* device)
{
    eReturnValues ret = UNKNOWN;
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("Using Windows API to Flush Cache\n");
    }
    // used for setting the timeout
    COMMTIMEOUTS comTimeout;
    safe_memset(&comTimeout, sizeof(COMMTIMEOUTS), 0, sizeof(COMMTIMEOUTS));
    /*BOOL timeoutGot = */
    GetCommTimeouts(device->os_info.fd, &comTimeout); // get timeouts if possible before trying to change them...
    uint64_t timeoutInSeconds = UINT64_C(0);
    if (device->drive_info.defaultTimeoutSeconds == 0)
    {
        comTimeout.ReadTotalTimeoutConstant = 15000; // 15 seconds
        timeoutInSeconds                    = 15;
    }
    else
    {
        comTimeout.ReadTotalTimeoutConstant =
            device->drive_info.defaultTimeoutSeconds * 1000; // convert time in seconds to milliseconds
        timeoutInSeconds = device->drive_info.defaultTimeoutSeconds;
    }
    /*BOOL timeoutSet = */
    SetCommTimeouts(device->os_info.fd, &comTimeout);
    device->os_info.last_error = GetLastError();
    // DWORD bytesReturned = DWORD_C(0);

    // this api call will need some changes when asynchronous support is added in
    DECLARE_SEATIMER(commandTimer);
    SetLastError(ERROR_SUCCESS); // clear any cached errors before we try to send the command
    start_Timer(&commandTimer);
    BOOL retStatus             = FlushFileBuffers(device->os_info.fd);
    device->os_info.last_error = GetLastError();
    if (device->os_info.last_error != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    if (MSFT_BOOL_FALSE(retStatus)) // not successful
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(device->os_info.last_error);
        }
        ret = FAILURE;
    }
    else
    {
        ret = SUCCESS;
    }

    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    ret = set_Command_Completion_For_OS_Read_Write(device, device->os_info.last_error);

    // check for command timeout
    if ((device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) >= timeoutInSeconds)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        print_Return_Enum("Windows API Flush", ret);
    }
    return ret;
}
