// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2017-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

#include "cmds.h"
#include "sat_helper_func.h"
#include "sntl_helper.h"
#include "uefi_helper.h"
// these are EDK2 include files
#include <Bus/Ata/AtaAtapiPassThru/AtaAtapiPassThru.h> //part of mdemodulepackage. Being used to help see if ATAPassthrough is from IDE or AHCI driver.
#include <Library/UefiBootServicesTableLib.h> //to get global boot services pointer. This pointer should be checked before use, but any app using stdlib will have this set.
#include <Protocol/AtaPassThru.h>
#include <Protocol/DevicePath.h>
#include <Protocol/ScsiPassThru.h>
#include <Protocol/ScsiPassThruExt.h>
#include <Uefi.h>
#if !defined(DISABLE_NVME_PASSTHROUGH)
#    include <Protocol/NvmExpressPassthru.h>
#endif
extern bool validate_Device_Struct(versionBlock);

// Define this to turn on extra prints to the screen for debugging UEFI passthrough issues.
// #define UEFI_PASSTHRU_DEBUG_MESSAGES

#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
// This color will be used for normal debug messages that are infomative. Critical memory allocation errors among other
// things are going to be CONSOLE_COLOR_RED.
eConsoleColors uefiDebugMessageColor = CONSOLE_COLOR_BLUE;
#endif

#define IS_ALIGNED(addr, size) (((UINTN)(addr) & ((size) - 1)) == 0)

// If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise
// you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

static M_INLINE void safe_free_dev_path_protocol(EFI_DEVICE_PATH_PROTOCOL** path)
{
    safe_free_core(M_REINTERPRET_CAST(void**, path));
}

#define UEFI_HANDLE_STRING_LENGTH 64

eReturnValues get_Passthru_Protocol_Ptr(EFI_GUID ptGuid, void** pPassthru, uint32_t controllerID)
{
    eReturnValues ret        = SUCCESS;
    EFI_STATUS    uefiStatus = EFI_SUCCESS;
    EFI_HANDLE*   handle     = M_NULLPTR;
    UINTN         nodeCount  = 0;

    if (!gBS) // make sure global boot services pointer is valid before accessing it.
    {
        return MEMORY_FAILURE;
    }

    uefiStatus = gBS->LocateHandleBuffer(ByProtocol, &ptGuid, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }
    // NOTE: This code below assumes that the caller knows the controller they intend to open. Meaning they've already
    // done some sort of system scan.
    uefiStatus = gBS->OpenProtocol(handle[controllerID], &ptGuid, pPassthru, gImageHandle, M_NULLPTR,
                                   EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(uefiStatus))
    {
        ret = FAILURE;
    }
    return ret;
}

void close_Passthru_Protocol_Ptr(EFI_GUID ptGuid, void** pPassthru, uint32_t controllerID)
{
    EFI_STATUS  uefiStatus = EFI_SUCCESS;
    EFI_HANDLE* handle     = M_NULLPTR;
    UINTN       nodeCount  = 0;

    if (!gBS) // make sure global boot services pointer is valid before accessing it.
    {
        return;
    }

    uefiStatus = gBS->LocateHandleBuffer(ByProtocol, &ptGuid, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return;
    }
    // NOTE: This code below assumes that we only care to change color output on node 0. This seems to work from a quick
    // test, but may not be correct. Not sure what the other 2 nodes are for...serial?
    uefiStatus = gBS->CloseProtocol(handle[controllerID], &ptGuid, gImageHandle, M_NULLPTR);
    if (EFI_ERROR(uefiStatus))
    {
        perror("Failed to close simple text output protocol\n");
    }
    else
    {
        *pPassthru = M_NULLPTR; // this pointer is no longer valid!
    }
}
// ATA PT since UDK 2010
eReturnValues get_ATA_Passthru_Protocol_Ptr(EFI_ATA_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(ataPtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

void close_ATA_Passthru_Protocol_Ptr(EFI_ATA_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(ataPtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

eReturnValues get_SCSI_Passthru_Protocol_Ptr(EFI_SCSI_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(scsiPtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

void close_SCSI_Passthru_Protocol_Ptr(EFI_SCSI_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(scsiPtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

eReturnValues get_Ext_SCSI_Passthru_Protocol_Ptr(EFI_EXT_SCSI_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(scsiPtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

void close_Ext_SCSI_Passthru_Protocol_Ptr(EFI_EXT_SCSI_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(scsiPtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
// NVMe since UDK 2015
eReturnValues get_NVMe_Passthru_Protocol_Ptr(EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;
    return get_Passthru_Protocol_Ptr(nvmePtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}

void close_NVMe_Passthru_Protocol_Ptr(EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL** pPassthru, uint32_t controllerID)
{
    EFI_GUID nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;
    return close_Passthru_Protocol_Ptr(nvmePtGUID, M_REINTERPRET_CAST(void**, pPassthru), controllerID);
}
#endif //! DISABLE_NVME_PASSTHROUGH

// filename (handle) formats:
// ata:<controllerID>:<port>:<portMultiplierPort> //16, 16, 16
// scsi:<controllerID>:<target>:<lun> //16, 32, 64
// scsiEx:<controllerID>:<target>:<lun> //16, 128, 64
// nvme:<controllerID>:<namespaceID> //16, 32

static bool get_ATA_Device_Handle(const char* filename, uint16_t* controllerID, uint16_t* port, uint16_t* pmport)
{
    bool success = false;
    if (filename && controllerID && port && pmport)
    {
        // before tokenizing, always duplicate the original string since tokenizing will modify it.
        char* dup = M_NULLPTR;
        if (safe_strdup(&dup, filename) == 0 && dup != M_NULLPTR)
        {
#define MAX_ATA_HANDLE_FIELDS UINT8_C(4) // ata:<controllerID>:<port>:<portMultiplierPort>
            uint8_t       count   = UINT8_C(0);
            rsize_t       duplen  = safe_strlen(dup);
            char*         saveptr = M_NULLPTR;
            char*         token   = safe_String_Token(dup, &duplen, ":", &saveptr);
            char*         endptr  = M_NULLPTR;
            unsigned long temp    = 0UL;
            success = true; // set to true so we can exit the loop quickly if an error is detected during parsing
            while (success && token && count < MAX_ATA_HANDLE_FIELDS)
            {
                switch (count)
                {
                case 0: // ata
                    if (strcmp(token, "ata") != 0)
                    {
                        success = false;
                    }
                    break;
                case 1: // controller ID
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT16_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *controllerID = C_CAST(uint16_t, temp);
                    }
                    break;
                case 2: // port
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT16_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *port = C_CAST(uint16_t, temp);
                    }
                    break;
                case 3: // portMP
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT16_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *pmport = C_CAST(uint16_t, temp);
                    }
                    break;
                }
                ++count;
                token = safe_String_Token(M_NULLPTR, &duplen ":", &saveptr);
            }
        }
        safe_free(&dup);
    }
    return success;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
static bool get_NVMe_Device_Handle(const char* filename, uint16_t* controllerID, uint32_t* nsid)
{
    bool success = false;
    if (filename && controllerID && nsid)
    {
        // before tokenizing, always duplicate the original string since tokenizing will modify it.
        char* dup = M_NULLPTR;
        if (safe_strdup(&dup, filename) == 0 && dup != M_NULLPTR)
        {
#    define MAX_NVME_HANDLE_FIELDS UINT8_C(3) // nvme:<controllerID>:<namespaceID>
            uint8_t       count   = UINT8_C(0);
            char*         saveptr = M_NULLPTR;
            rsize_t       duplen  = safe_strlen(dup);
            char*         token   = safe_String_Token(dup, &duplen, ":", &saveptr);
            char*         endptr  = M_NULLPTR;
            unsigned long temp    = 0UL;
            success = true; // set to true so we can exit the loop quickly if an error is detected during parsing
            while (success && token && count < MAX_NVME_HANDLE_FIELDS)
            {
                switch (count)
                {
                case 0: // nvme
                    if (strcmp(token, "nvme") != 0)
                    {
                        success = false;
                    }
                    break;
                case 1: // controller ID
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT16_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *controllerID = C_CAST(uint16_t, temp);
                    }
                    break;
                case 2: // nsid
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT32_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *nsid = C_CAST(uint32_t, temp);
                    }
                    break;
                }
                ++count;
                token = safe_String_Token(M_NULLPTR, &duplen, ":", &saveptr);
            }
        }
        safe_free(&dup);
    }
    return success;
}
#endif //! DISABLE_NVME_PASSTHROUGH

static bool get_SCSI_Device_Handle(const char* filename, uint16_t* controllerID, uint32_t* target, uint64_t* lun)
{
    bool success = false;
    if (filename && controllerID && target && lun)
    {
        // before tokenizing, always duplicate the original string since tokenizing will modify it.
        char* dup = M_NULLPTR;
        if (safe_strdup(&dup, filename) == 0 && dup != M_NULLPTR)
        {
#define MAX_SCSI_HANDLE_FIELDS UINT8_C(4) // scsi:<controllerID>:<target>:<lun>
            uint8_t            count   = UINT8_C(0);
            char*              saveptr = M_NULLPTR;
            rsize_t            duplen  = safe_strlen(dup);
            char*              token   = safe_String_Token(dup, &duplen, ":", &saveptr);
            char*              endptr  = M_NULLPTR;
            unsigned long      temp    = 0UL;
            unsigned long long btemp   = 0ULL;
            success = true; // set to true so we can exit the loop quickly if an error is detected during parsing
            while (success && token && count < MAX_NVME_HANDLE_FIELDS)
            {
                switch (count)
                {
                case 0: // scsi
                    if (strcmp(token, "scsi") != 0)
                    {
                        success = false;
                    }
                    break;
                case 1: // controller ID
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT16_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *controllerID = C_CAST(uint16_t, temp);
                    }
                    break;
                case 2: // target
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT32_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *target = C_CAST(uint32_t, temp);
                    }
                    break;
                case 3: // lun
                    if (0 != safe_strtoull(&btemp, token, &endptr, BASE_16_HEX) || (btemp > UINT64_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *lun = C_CAST(uint64_t, btemp);
                    }
                    break;
                }
                ++count;
                token = safe_String_Token(M_NULLPTR, &duplen, ":", &saveptr);
            }
        }
        safe_free(&dup);
    }
    return success;
}

static bool get_SCSIEX_Device_Handle(const char* filename,
                                     uint16_t*   controllerID,
                                     uint8_t     target[TARGET_MAX_BYTES],
                                     uint64_t*   lun)
{
    bool success = false;
    if (filename && controllerID && target && lun)
    {
        // before tokenizing, always duplicate the original string since tokenizing will modify it.
        char* dup = M_NULLPTR;
        if (safe_strdup(&dup, filename) == 0 && dup != M_NULLPTR)
        {
#define MAX_SCSI_HANDLE_FIELDS UINT8_C(4) // scsiEx:<controllerID>:<target>:<lun> //16, 128, 64
            uint8_t            count   = UINT8_C(0);
            char*              saveptr = M_NULLPTR;
            rsize_t            duplen  = safe_strlen(dup);
            char*              token   = safe_String_Token(dup, &duplen, ":", &saveptr);
            char*              endptr  = M_NULLPTR;
            unsigned long      temp    = 0UL;
            unsigned long long btemp   = 0ULL;
            success = true; // set to true so we can exit the loop quickly if an error is detected during parsing
            while (success && token && count < MAX_NVME_HANDLE_FIELDS)
            {
                switch (count)
                {
                case 0: // scsi
                    if (strcmp(token, "scsiEx") != 0)
                    {
                        success = false;
                    }
                    break;
                case 1: // controller ID
                    if (0 != safe_strtoul(&temp, token, &endptr, BASE_16_HEX) || (temp > UINT16_MAX))
                    {
                        success = false;
                    }
                    else
                    {
                        *controllerID = C_CAST(uint16_t, temp);
                    }
                    break;
                case 2: // target //FIXME: Does not handle full 128bit targetID
                    // first try seeing if the target is less than 128bits and a 64bit conversion will be enough
                    if (0 != safe_strtoull(&btemp, token, &endptr, BASE_16_HEX))
                    {
                        // Either the target is a much larget value than 64bits can hold, or there was a parsing error.
                        // First see if we can seperate it into two pieces to parse before deciding this was a failure
                        char* targetstr = M_NULLPTR;
                        if (safe_strdup(&targetstr, token) == 0 && targetstr != M_NULLPTR)
                        {
                            char* delimiter = strstr(targetstr, ":");
                            if (delimiter)
                            {
                                // change the next : to a null so it's easier to break the string into two parts
                                *delimiter           = '\0';
                                size_t halftargetlen = safe_strlen(targetstr) / SIZE_T_C(2);
                                if (safe_strlen(targetstr) % SIZE_T_C(2))
                                {
                                    // this is supposed to be evenly divisible by 2 as we require the user to type the
                                    // full string!
                                    success = false;
                                }
                                else
                                {
                                    char* firstHalf  = M_NULLPTR;
                                    char* secondHalf = M_NULLPTR;
                                    if (0 == safe_strndup(&firstHalf, targetstr, halftargetlen) &&
                                        0 == safe_strndup(&secondHalf, targetstr + halftargetlen, halftargetlen) &&
                                        firstHalf != M_NULLPTR && secondHalf != M_NULLPTR)
                                    {
                                        if (0 != safe_strtoull(&btemp, firstHalf, &endptr, BASE_16_HEX))
                                        {
                                            success = false;
                                        }
                                        else
                                        {
                                            // set it into the buffer
                                            target[0] = M_Byte7(btemp);
                                            target[1] = M_Byte6(btemp);
                                            target[2] = M_Byte5(btemp);
                                            target[3] = M_Byte4(btemp);
                                            target[4] = M_Byte3(btemp);
                                            target[5] = M_Byte2(btemp);
                                            target[6] = M_Byte1(btemp);
                                            target[7] = M_Byte0(btemp);
                                            if (0 != safe_strtoull(&btemp, secondHalf, &endptr, BASE_16_HEX))
                                            {
                                                success = false;
                                            }
                                            else
                                            {
                                                target[8]  = M_Byte7(btemp);
                                                target[9]  = M_Byte6(btemp);
                                                target[10] = M_Byte5(btemp);
                                                target[11] = M_Byte4(btemp);
                                                target[12] = M_Byte3(btemp);
                                                target[13] = M_Byte2(btemp);
                                                target[14] = M_Byte1(btemp);
                                                target[15] = M_Byte0(btemp);
                                            }
                                        }
                                    }
                                    else
                                    {
                                        success = false;
                                    }
                                    safe_free(&firstHalf);
                                    safe_free(&secondHalf);
                                }
                            }
                            else
                            {
                                // missing the next delimiter, which is not supposed to happen, so this is a failure
                                success = false;
                            }
                            safe_free(&targetstr);
                        }
                        else
                        {
                            success = false;
                        }
                    }
                    else
                    {
                        // value was less than 128bits and was parsed.
                        // So set it into the buffer
                        target[0] = M_Byte7(btemp);
                        target[1] = M_Byte6(btemp);
                        target[2] = M_Byte5(btemp);
                        target[3] = M_Byte4(btemp);
                        target[4] = M_Byte3(btemp);
                        target[5] = M_Byte2(btemp);
                        target[6] = M_Byte1(btemp);
                        target[7] = M_Byte0(btemp);
                    }
                    break;
                case 3: // lun
                    if (0 != safe_strtoull(&btemp, token, &endptr, BASE_16_HEX))
                        (btemp > UINT64_MAX))
                        {
                            success = false;
                        }
                    else
                    {
                        *target = C_CAST(uint64_t, btemp);
                    }
                    break;
                }
                ++count;
                token = safe_String_Token(M_NULLPTR, &duplen, ":", &saveptr);
            }
        }
        safe_free(&dup);
    }
    return success;
}

eReturnValues get_Device(const char* filename, tDevice* device)
{
    snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
    snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", filename);
    device->os_info.osType = OS_UEFI;
    if (strstr(filename, "ata") == filename) // this can only be at the beginning of the string
    {
        if (get_ATA_Device_Handle(filename, &device->os_info.controllerNum, &device->os_info.address.ata.port,
                                  &device->os_info.address.ata.portMultiplierPort))
        {
            device->drive_info.interface_type     = IDE_INTERFACE;
            device->drive_info.drive_type         = ATA_DRIVE;
            device->os_info.passthroughType       = UEFI_PASSTHROUGH_ATA;
            EFI_ATA_PASS_THRU_PROTOCOL* pPassthru = M_NULLPTR;
            if (SUCCESS == get_ATA_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath =
                    pPassthru->BuildDevicePath(pPassthru, device->os_info.address.ata.port,
                                               device->os_info.address.ata.portMultiplierPort, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    safe_memcpy(&device->os_info.devicePath, sizeof(EFI_DEV_PATH), devicePath,
                                M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                    ATA_ATAPI_PASS_THRU_INSTANCE* instance = ATA_PASS_THRU_PRIVATE_DATA_FROM_THIS(pPassthru);
                    if (instance->Mode == EfiAtaIdeMode && device->os_info.address.ata.portMultiplierPort > 0)
                    {
                        // If the driver is running in IDE mode, it will set the pmport value to non-zero for device 1.
                        // Because of this, and some sample EDK2 code, we need to make sure we set the device 1 bit when
                        // issuing commands!
                        device->drive_info.ata_Options.isDevice1 = true;
                    }
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                    set_Console_Colors(true, CONSOLE_COLOR_GREEN);
                    printf("Protocol Mode = %d\n",
                           instance
                               ->Mode); // 0 means IDE, 1 means AHCI, 2 means RAID, but we shouldn't see RAID here ever.
                    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                }
                else
                {
                    // device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_free(&devicePath);
                // close the protocol
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
    else if (strstr(filename, "scsiEx") == filename)
    {
        if (get_SCSIEX_Device_Handle(filename, &device->os_info.controllerNum, device->os_info.address.scsiEx.target,
                                     &device->os_info.address.scsiEx.lun))
        {
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.drive_type     = SCSI_DRIVE;
            device->os_info.passthroughType   = UEFI_PASSTHROUGH_SCSI_EXT;
            EFI_EXT_SCSI_PASS_THRU_PROTOCOL* pPassthru;
            if (SUCCESS == get_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL* devicePath = M_NULLPTR; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath =
                    pPassthru->BuildDevicePath(pPassthru, C_CAST(uint8_t*, &device->os_info.address.scsiEx.target),
                                               device->os_info.address.scsiEx.lun, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    safe_memcpy(&device->os_info.devicePath, sizeof(EFI_DEV_PATH), devicePath,
                                M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                }
                else
                {
                    // device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_free(&devicePath);
                close_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum);
            }
            else
            {
                return FAILURE;
            }
        }
    }
    else if (strstr(filename, "scsi") == filename)
    {
        if (get_SCSI_Device_Handle(filename, &device->os_info.controllerNum, &device->os_info.address.scsi.target,
                                   &device->os_info.address.scsi.lun))
        {
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.drive_type     = SCSI_DRIVE;
            device->os_info.passthroughType   = UEFI_PASSTHROUGH_SCSI;
            EFI_SCSI_PASS_THRU_PROTOCOL* pPassthru;
            if (SUCCESS == get_SCSI_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL* devicePath = M_NULLPTR; // will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, device->os_info.address.scsi.target,
                                                                  device->os_info.address.scsi.lun, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    safe_memcpy(&device->os_info.devicePath, sizeof(EFI_DEV_PATH), devicePath,
                                M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                }
                else
                {
                    // device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_free(&devicePath);
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
#if !defined(DISABLE_NVME_PASSTHROUGH)
    else if (strstr(filename, "nvme") == filename)
    {
        if (get_NVMe_Device_Handle(filename, &device->os_info.controllerNum, &device->os_info.address.nvme.namespaceID))
        {
            device->drive_info.interface_type = NVME_INTERFACE;
            device->drive_info.drive_type     = NVME_DRIVE;
            device->os_info.passthroughType   = UEFI_PASSTHROUGH_NVME;
            EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL* pPassthru;
            if (SUCCESS == get_NVMe_Passthru_Protocol_Ptr(&pPassthru, device->os_info.controllerNum))
            {
                EFI_DEVICE_PATH_PROTOCOL* devicePath = M_NULLPTR; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath =
                    pPassthru->BuildDevicePath(pPassthru, device->os_info.address.nvme.namespaceID, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    safe_memcpy(&device->os_info.devicePath, sizeof(EFI_DEV_PATH), devicePath,
                                M_BytesTo2ByteValue(devicePath->Length[1], devicePath->Length[0]));
                    device->os_info.minimumAlignment = pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1;
                }
                else
                {
                    // device doesn't exist, so we cannot talk to it
                    return FAILURE;
                }
                safe_free(&devicePath);
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
    // fill in the drive info
    return fill_Drive_Info_Data(device);
}

eReturnValues device_Reset(ScsiIoCtx* scsiIoCtx)
{
    // need to investigate if there is a way to do this in uefi
    return NOT_SUPPORTED;
}

eReturnValues bus_Reset(ScsiIoCtx* scsiIoCtx)
{
    // need to investigate if there is a way to do this in uefi
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

eReturnValues send_UEFI_SCSI_Passthrough(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues                ret    = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS                   Status = EFI_SUCCESS;
    EFI_SCSI_PASS_THRU_PROTOCOL* pPassthru;
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI SCSI Passthru Command\n");
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    if (SUCCESS == get_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum))
    {
        DECLARE_SEATIMER(commandTimer);
        uint8_t*                                alignedPointer     = scsiIoCtx->pdata;
        uint8_t*                                alignedCDB         = scsiIoCtx->cdb;
        uint8_t*                                alignedSensePtr    = scsiIoCtx->psense;
        uint8_t*                                localBuffer        = M_NULLPTR;
        uint8_t*                                localCDB           = M_NULLPTR;
        uint8_t*                                localSensePtr      = M_NULLPTR;
        bool                                    localAlignedBuffer = false;
        bool                                    localSenseBuffer   = false;
        EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET* srp; // scsi request packet

        srp = C_CAST(EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET*,
                     safe_calloc_aligned(1, sizeof(EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET),
                                         pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));

#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
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
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif

        if (scsiIoCtx->timeout == UINT32_MAX)
        {
            srp->Timeout = UINT64_C(0); // value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            srp->Timeout =
                scsiIoCtx->timeout * UINT64_C(10000000); // value is in 100ns units. zero means wait indefinitely
            if (scsiIoCtx->timeout == UINT32_C(0))
            {
                srp->Timeout = UINT64_C(15) *
                               UINT64_C(10000000); // 15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->pdata, pPassthru->Mode->IoAlign))
        {
            // allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer        = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localBuffer)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                safe_memcpy(alignedPointer, scsiIoCtx->dataLength, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->cdb, pPassthru->Mode->IoAlign))
        {
            // allocate an aligned buffer here!
            localCDB = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->cdbLength, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localCDB)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned CDB pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedCDB = localCDB;
            // copy CDB into aligned CDB memory pointer
            safe_memcpy(alignedCDB, scsiIoCtx->cdbLength, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->psense, pPassthru->Mode->IoAlign))
        {
            // allocate an aligned buffer here!
            localSenseBuffer = true;
            localSensePtr    = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->senseDataSize, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localSensePtr)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned sense data pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedSensePtr = localSensePtr;
        }

        srp->DataBuffer     = alignedPointer;
        srp->TransferLength = scsiIoCtx->dataLength;
        switch (scsiIoCtx->direction)
        {
            // NOLINTBEGIN(bugprone-branch-clone)
        case XFER_DATA_OUT:
            srp->DataDirection = 1;
            break;
        case XFER_DATA_IN:
            srp->DataDirection = 0;
            break;
        case XFER_NO_DATA:
            srp->DataDirection = 0;
            break;
            // NOLINTEND(bugprone-branch-clone)
        case XFER_DATA_OUT_IN: // bidirectional command support not allowed with this type of passthru
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, uefiDebugMessageColor);
            printf("Send UEFI SCSI PT CMD NOT AVAILABLE\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
            return OS_COMMAND_NOT_AVAILABLE;
        default:
            return BAD_PARAMETER;
        }
        srp->SenseData       = alignedSensePtr; // Need to verify is this correct or not
        srp->CdbLength       = scsiIoCtx->cdbLength;
        srp->SenseDataLength = scsiIoCtx->senseDataSize;
        srp->Cdb             = alignedCDB;

#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending SCSI Passthru command\n");
        printf("\t->TransferLength = %" PRIu32 "\n", srp->TransferLength);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
        start_Timer(&commandTimer);
        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.scsi.target,
                                     scsiIoCtx->device->os_info.address.scsi.lun, srp, M_NULLPTR);
        stop_Timer(&commandTimer);
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SCSI Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-TransferLength = %" PRIu32 "\n", srp->TransferLength);
        print_UEFI_SCSI_Adapter_Status(srp->HostAdapterStatus);
        print_UEFI_SCSI_Target_Status(srp->TargetStatus);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        scsiIoCtx->device->os_info.last_error                    = Status;

        if (Status == EFI_SUCCESS)
        {
            ret = SUCCESS;
            if (localSenseBuffer)
            {
                safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, alignedSensePtr,
                            M_Min(scsiIoCtx->senseDataSize, srp->SenseDataLength));
            }
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(scsiIoCtx->device->os_info.last_error);
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
            if (Status == EFI_WRITE_PROTECTED)
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
        safe_free_aligned(&localBuffer);
        safe_free_aligned(&localCDB);
        safe_free_aligned(&localSensePtr);
        safe_free_aligned_core(C_CAST(void**, &srp));
        close_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSI Passthru function returning %d\n", ret);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
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

// TODO: ifdef for EDK/UDK version?
eReturnValues send_UEFI_SCSI_Passthrough_Ext(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues                    ret       = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS                       Status    = EFI_SUCCESS;
    EFI_EXT_SCSI_PASS_THRU_PROTOCOL* pPassthru = M_NULLPTR;
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI SCSIEx Passthru Command\n");
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    if (SUCCESS == get_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum))
    {
        DECLARE_SEATIMER(commandTimer);
        uint8_t*                                    alignedPointer     = scsiIoCtx->pdata;
        uint8_t*                                    alignedCDB         = scsiIoCtx->cdb;
        uint8_t*                                    alignedSensePtr    = scsiIoCtx->psense;
        uint8_t*                                    localBuffer        = M_NULLPTR;
        uint8_t*                                    localCDB           = M_NULLPTR;
        uint8_t*                                    localSensePtr      = M_NULLPTR;
        bool                                        localAlignedBuffer = false;
        bool                                        localSenseBuffer   = false;
        EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET* srp; // Extended scsi request packet

        srp = M_REINTERPRET_CAST(EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET*,
                                 safe_calloc_aligned(1, sizeof(EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET),
                                                     pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));

#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
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
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif

        if (scsiIoCtx->timeout == UINT32_MAX)
        {
            srp->Timeout = UINT64_C(0); // value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            srp->Timeout =
                scsiIoCtx->timeout * UINT64_C(10000000); // value is in 100ns units. zero means wait indefinitely
            if (scsiIoCtx->timeout == UINT32_C(0))
            {
                srp->Timeout = UINT64_C(0) *
                               UINT64_C(10000000); // 15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->pdata, pPassthru->Mode->IoAlign))
        {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoTcx->pdata is not aligned! Creating local aligned buffer\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
            // allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer        = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localBuffer)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                safe_memcpy(alignedPointer, scsiIoCtx->dataLength, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
#endif
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->cdb, pPassthru->Mode->IoAlign))
        {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoTcx->cdb is not aligned! Creating local cdb buffer\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
            // allocate an aligned buffer here!
            localCDB = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->cdbLength, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localCDB)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned CDB pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedCDB = localCDB;
            // copy CDB into aligned CDB memory pointer
            safe_memcpy(alignedCDB, scsiIoCtx->cdbLength, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedCDB, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! AlignedCDB is still not properly aligned\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
#endif
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->psense, pPassthru->Mode->IoAlign))
        {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoTcx->psense is not aligned! Creating local sense buffer\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
            // allocate an aligned buffer here!
            localSenseBuffer = true;
            localSensePtr    = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->senseDataSize, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localSensePtr)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned sense data pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedSensePtr = localSensePtr;
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedsenseptr is still not properly aligned\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
#endif
        }

        switch (scsiIoCtx->direction)
        {
        // NOLINTBEGIN(bugprone-branch-clone)
        case XFER_DATA_OUT:
            srp->OutDataBuffer     = alignedPointer;
            srp->InDataBuffer      = M_NULLPTR;
            srp->OutTransferLength = scsiIoCtx->dataLength;
            srp->DataDirection     = 1;
            break;
        case XFER_DATA_IN:
            srp->InDataBuffer     = alignedPointer;
            srp->OutDataBuffer    = M_NULLPTR;
            srp->InTransferLength = scsiIoCtx->dataLength;
            srp->DataDirection    = 0;
            break;
        case XFER_NO_DATA:
            srp->OutDataBuffer     = M_NULLPTR;
            srp->OutDataBuffer     = M_NULLPTR;
            srp->DataDirection     = 0;
            srp->InTransferLength  = 0;
            srp->OutTransferLength = 0;
            break;
        // case XFER_DATA_OUT_IN: //TODO: bidirectional command support
        // srp->DataDirection = 2;//bidirectional command
        // NOLINTEND(bugprone-branch-clone)
        default:
            return BAD_PARAMETER;
        }
        srp->SenseData       = alignedSensePtr;
        srp->CdbLength       = scsiIoCtx->cdbLength;
        srp->SenseDataLength = scsiIoCtx->senseDataSize;
        srp->Cdb             = alignedCDB;

#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending SCSIEx Passthru command\n");
        printf("\t->InTransferLength = %" PRIu32 "\tOutTransferLength = %" PRIu32 "\n", srp->InTransferLength,
               srp->OutTransferLength);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
        start_Timer(&commandTimer);
        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.scsiEx.target,
                                     scsiIoCtx->device->os_info.address.scsiEx.lun, srp, M_NULLPTR);
        stop_Timer(&commandTimer);
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SCSIEx Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-InTransferLength = %" PRIu32 "\tOutTransferLength = %" PRIu32 "\n", srp->InTransferLength,
               srp->OutTransferLength);
        print_UEFI_SCSI_Ex_Adapter_Status(srp->HostAdapterStatus);
        print_UEFI_SCSI_Ex_Target_Status(srp->TargetStatus);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif

        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        scsiIoCtx->device->os_info.last_error                    = Status;

        if (Status == EFI_SUCCESS)
        {
            ret = SUCCESS;
            if (localSenseBuffer)
            {
                safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, alignedSensePtr,
                            M_Min(scsiIoCtx->senseDataSize, srp->SenseDataLength));
            }
            if (localAlignedBuffer && scsiIoCtx->direction == XFER_DATA_IN)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, alignedPointer, scsiIoCtx->dataLength);
            }
        }
        else
        {
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(scsiIoCtx->device->os_info.last_error);
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
            if (Status == EFI_WRITE_PROTECTED)
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
        safe_free_aligned(&localBuffer);
        safe_free_aligned(&localCDB);
        safe_free_aligned(&localSensePtr);
        safe_free_aligned_core(C_CAST(void**, &srp));
        close_Ext_SCSI_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSIEx Passthru function returning %d\n", ret);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    return ret;
}

// TODO: ifdef for EDK/UDK version?
eReturnValues send_UEFI_ATA_Passthrough(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues               ret    = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS                  Status = EFI_SUCCESS;
    EFI_ATA_PASS_THRU_PROTOCOL* pPassthru;
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI ATA Passthru command\n");
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    if (scsiIoCtx->pAtaCmdOpts == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    if (SUCCESS == get_ATA_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum))
    {
        DECLARE_SEATIMER(commandTimer);
        uint8_t*                          alignedPointer     = scsiIoCtx->pAtaCmdOpts->ptrData;
        uint8_t*                          localBuffer        = M_NULLPTR;
        bool                              localAlignedBuffer = false;
        EFI_ATA_PASS_THRU_COMMAND_PACKET* ataPacket          = M_NULLPTR; // ata command packet
        EFI_ATA_COMMAND_BLOCK*            ataCommand         = C_CAST(
            EFI_ATA_COMMAND_BLOCK*, safe_calloc_aligned(1, sizeof(EFI_ATA_COMMAND_BLOCK),
                                                        pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
        EFI_ATA_STATUS_BLOCK* ataStatus = C_CAST(
            EFI_ATA_STATUS_BLOCK*, safe_calloc_aligned(1, sizeof(EFI_ATA_STATUS_BLOCK),
                                                       pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));

        ataPacket = C_CAST(EFI_ATA_PASS_THRU_COMMAND_PACKET*,
                           safe_calloc_aligned(1, sizeof(EFI_ATA_PASS_THRU_COMMAND_PACKET),
                                               pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));

#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
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
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif

        if (scsiIoCtx->timeout == UINT32_MAX)
        {
            ataPacket->Timeout = 0; // value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            ataPacket->Timeout = scsiIoCtx->pAtaCmdOpts->timeout *
                                 UINT64_C(10000000); // value is in 100ns units. zero means wait indefinitely
            if (scsiIoCtx->pAtaCmdOpts->timeout == UINT32_C(0))
            {
                ataPacket->Timeout =
                    15 * UINT64_C(10000000); // 15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(scsiIoCtx->pAtaCmdOpts->ptrData, pPassthru->Mode->IoAlign))
        {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("scsiIoCtx->pAtaCmdOpts->ptrData is not aligned! Creating local aligned buffer\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
            // allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer        = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(scsiIoCtx->pAtaCmdOpts->dataSize, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localBuffer)
            {
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (scsiIoCtx->direction == XFER_DATA_OUT)
            {
                safe_memcpy(alignedPointer, scsiIoCtx->pAtaCmdOpts->dataSize, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
#endif
        }

        switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
        {
        // NOLINTBEGIN(bugprone-branch-clone)
        case XFER_DATA_OUT:
            ataPacket->OutDataBuffer     = alignedPointer;
            ataPacket->InDataBuffer      = M_NULLPTR;
            ataPacket->OutTransferLength = scsiIoCtx->pAtaCmdOpts->dataSize;
            break;
        case XFER_DATA_IN:
            ataPacket->InDataBuffer     = alignedPointer;
            ataPacket->OutDataBuffer    = M_NULLPTR;
            ataPacket->InTransferLength = scsiIoCtx->pAtaCmdOpts->dataSize;
            break;
        case XFER_NO_DATA:
            ataPacket->OutDataBuffer     = M_NULLPTR;
            ataPacket->OutDataBuffer     = M_NULLPTR;
            ataPacket->InTransferLength  = 0;
            ataPacket->OutTransferLength = 0;
            break;
        // NOLINTEND(bugprone-branch-clone)
        default:
            return BAD_PARAMETER;
        }
        // set status block and command block
        ataCommand->AtaCommand         = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
        ataCommand->AtaFeatures        = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
        ataCommand->AtaSectorNumber    = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
        ataCommand->AtaCylinderLow     = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
        ataCommand->AtaCylinderHigh    = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
        ataCommand->AtaDeviceHead      = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
        ataCommand->AtaSectorNumberExp = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;
        ataCommand->AtaCylinderLowExp  = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;
        ataCommand->AtaCylinderHighExp = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;
        ataCommand->AtaFeaturesExp     = scsiIoCtx->pAtaCmdOpts->tfr.Feature48;
        ataCommand->AtaSectorCount     = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
        ataCommand->AtaSectorCountExp  = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;

        ataPacket->Asb = ataStatus;
        ataPacket->Acb = ataCommand;

        // Set the protocol
        switch (scsiIoCtx->pAtaCmdOpts->commadProtocol)
        {
            // NOLINTBEGIN(bugprone-branch-clone)
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
            else // data in
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
            else // data in
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
            // NOLINTEND(bugprone-branch-clone)
        default:
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("\nProtocol Not Supported in ATA Pass Through.\n");
            }
            return NOT_SUPPORTED;
            break;
        }

        // Set the passthrough length data (where it is, bytes, etc) (essentially building an SAT ATA pass-through
        // command)
        ataPacket->Length |= EFI_ATA_PASS_THRU_LENGTH_BYTES; // ALWAYS set this. We will always set the transfer length
                                                             // as a number of bytes to transfer.
        // Setting 512B vs 4096 vs anything else doesn't matter in UEFI since we can set number of bytes for our
        // transferlength anytime.

        switch (scsiIoCtx->pAtaCmdOpts->ataCommandLengthLocation)
        {
            // NOLINTBEGIN(bugprone-branch-clone)
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
            // NOLINTEND(bugprone-branch-clone)
        }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending ATA Passthru command\n");
        printf("\t->InTransferLength = %" PRIu32 "\t OutTransferLength = %" PRIu32 "\n", ataPacket->InTransferLength,
               ataPacket->OutTransferLength);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
        start_Timer(&commandTimer);
        Status = pPassthru->PassThru(pPassthru, scsiIoCtx->device->os_info.address.ata.port,
                                     scsiIoCtx->device->os_info.address.ata.portMultiplierPort, ataPacket, M_NULLPTR);
        stop_Timer(&commandTimer);
        // convert return status from sending the command into a return value for opensea-transport
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("ATA Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-InTransferLength = %" PRIu32 "\t OutTransferLength = %" PRIu32 "\n", ataPacket->InTransferLength,
               ataPacket->OutTransferLength);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif

        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        scsiIoCtx->device->os_info.last_error                    = Status;

        if (Status == EFI_SUCCESS)
        {
            if (localAlignedBuffer && scsiIoCtx->pAtaCmdOpts->commandDirection == XFER_DATA_IN)
            {
                // memcpy the data back to the user's pointer since we had to allocate one locally.
                safe_memcpy(scsiIoCtx->pAtaCmdOpts->ptrData, scsiIoCtx->pAtaCmdOpts->dataSize, alignedPointer,
                            scsiIoCtx->pAtaCmdOpts->dataSize);
            }
            ret = SUCCESS;
            // convert RTFRs to sense data since the above layer is using SAT for everthing to make it easy to port
            // across systems
            scsiIoCtx->returnStatus.senseKey = 0;
            scsiIoCtx->returnStatus.asc      = 0x00; // might need to change this later
            scsiIoCtx->returnStatus.ascq     = 0x00; // might need to change this later
            if (scsiIoCtx->psense != M_NULLPTR)      // check that the pointer is valid
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
                        scsiIoCtx->psense[12] = ataStatus->AtaSectorCountExp;  // Sector Count Ext
                        scsiIoCtx->psense[14] = ataStatus->AtaSectorNumberExp; // LBA Lo Ext
                        scsiIoCtx->psense[16] = ataStatus->AtaCylinderLowExp;  // LBA Mid Ext
                        scsiIoCtx->psense[18] = ataStatus->AtaCylinderHighExp; // LBA Hi
                    }
                    // fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = ataStatus->AtaError;        // Error
                    scsiIoCtx->psense[13] = ataStatus->AtaSectorCount;  // Sector Count
                    scsiIoCtx->psense[15] = ataStatus->AtaSectorNumber; // LBA Lo
                    scsiIoCtx->psense[17] = ataStatus->AtaCylinderLow;  // LBA Mid
                    scsiIoCtx->psense[19] = ataStatus->AtaCylinderHigh; // LBA Hi
                    scsiIoCtx->psense[20] = ataStatus->AtaDeviceHead;   // Device/Head
                    scsiIoCtx->psense[21] = ataStatus->AtaStatus;       // Status
                }
            }
        }
        else // error, set appropriate return code
        {
            if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(scsiIoCtx->device->os_info.last_error);
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
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
        safe_free_aligned_core(C_CAST(void**, &ataPacket));
        safe_free_aligned(&localBuffer);
        safe_free_aligned_core(C_CAST(void**, &ataStatus));
        safe_free_aligned_core(C_CAST(void**, &ataCommand));
        close_ATA_Passthru_Protocol_Ptr(&pPassthru, scsiIoCtx->device->os_info.controllerNum);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("ATA Passthru function returning %d\n", ret);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    return ret;
}

eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
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
    case UEFI_PASSTHROUGH_NVME:
        ret = sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        break;
    default:
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("SendIO CMD NOT AVAILABLE: %d\n", scsiIoCtx->device->os_info.passthroughType);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
        ret = OS_COMMAND_NOT_AVAILABLE;
        break;
    }
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

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues                       ret    = OS_PASSTHROUGH_FAILURE;
    EFI_STATUS                          Status = EFI_SUCCESS;
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL* pPassthru;
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("Sending UEFI NVMe Passthru Command\n");
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
    if (SUCCESS == get_NVMe_Passthru_Protocol_Ptr(&pPassthru, nvmeIoCtx->device->os_info.controllerNum))
    {
        DECLARE_SEATIMER(commandTimer);
        uint8_t*                                  alignedPointer     = nvmeIoCtx->ptrData;
        uint8_t*                                  localBuffer        = M_NULLPTR;
        bool                                      localAlignedBuffer = false;
        EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET* nrp                = M_NULLPTR;
        EFI_NVM_EXPRESS_COMMAND*                  nvmCommand         = C_CAST(
            EFI_NVM_EXPRESS_COMMAND*, safe_calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_COMMAND),
                                                          pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
        EFI_NVM_EXPRESS_COMPLETION* nvmCompletion =
            C_CAST(EFI_NVM_EXPRESS_COMPLETION*,
                   safe_calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_COMPLETION),
                                       pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));

        if (!nvmCommand || !nvmCompletion)
        {
            printf("Error allocating command or completion for nrp\n");
            return MEMORY_FAILURE;
        }

        nrp = C_CAST(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET*,
                     safe_calloc_aligned(1, sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET),
                                         pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));

        if (!nrp)
        {
            printf("Error allocating NRP\n");
            return MEMORY_FAILURE;
        }

#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
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
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif

        if (nvmeIoCtx->timeout == UINT32_MAX)
        {
            nrp->CommandTimeout = UINT64_C(0); // value is in 100ns units. zero means wait indefinitely
        }
        else
        {
            nrp->CommandTimeout =
                nvmeIoCtx->timeout * UINT64_C(10000000); // value is in 100ns units. zero means wait indefinitely
            if (nvmeIoCtx->timeout == UINT32_C(0))
            {
                nrp->CommandTimeout =
                    UINT64_C(15) *
                    UINT64_C(10000000); // 15 seconds. value is in 100ns units. zero means wait indefinitely
            }
        }

        // This is a hack for now. We should be enforcing pointers and data transfer size on in, our, or bidirectional
        // commands up above even if nothing is expected in the return data buffer - TJE
        if (nvmeIoCtx->commandDirection != XFER_NO_DATA && (nvmeIoCtx->dataSize == 0 || !nvmeIoCtx->ptrData))
        {
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, YELLOW);
            printf("WARNING! Data transfer command specifying zero length!\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
            localAlignedBuffer = true;
            localBuffer        = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(M_Max(512, nvmeIoCtx->dataSize), sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localBuffer)
            {
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf(
                    "Failed to allocate memory for an aligned data pointer - missing buffer on data xfer command!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
                return MEMORY_FAILURE;
            }
            alignedPointer      = localBuffer;
            nvmeIoCtx->dataSize = M_Max(512, nvmeIoCtx->dataSize);
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
            }
#    endif
        }

        if (pPassthru->Mode->IoAlign > 1 && !IS_ALIGNED(nvmeIoCtx->ptrData, pPassthru->Mode->IoAlign))
        {
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
            set_Console_Colors(true, MAGENTA);
            printf("nvmeIoCtx->ptrData is not aligned! Creating local aligned buffer\n");
            set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
            // allocate an aligned buffer here!
            localAlignedBuffer = true;
            localBuffer        = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(nvmeIoCtx->dataSize, sizeof(uint8_t),
                                              pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1));
            if (!localBuffer)
            {
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("Failed to allocate memory for an aligned data pointer!\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
                return MEMORY_FAILURE;
            }
            alignedPointer = localBuffer;
            if (nvmeIoCtx->commandDirection == XFER_DATA_OUT)
            {
                safe_memcpy(alignedPointer, nvmeIoCtx->dataSize, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
            }
            if (!IS_ALIGNED(alignedPointer, pPassthru->Mode->IoAlign))
            {
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
                set_Console_Colors(true, MAGENTA);
                printf("WARNING! Alignedpointer is still not properly aligned\n");
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
            }
        }

        // set transfer information
        nrp->TransferBuffer = alignedPointer;
        nrp->TransferLength = nvmeIoCtx->dataSize;

        // TODO: Handle metadata pointer...right now attempting to allocate something locally here just to get things
        // working.
        nrp->MetadataBuffer =
            safe_calloc_aligned(16, sizeof(uint8_t), pPassthru->Mode->IoAlign > 0 ? pPassthru->Mode->IoAlign : 1);
        nrp->MetadataLength = 16;

        // set queue type & command
        switch (nvmeIoCtx->commandType)
        {
        case NVM_ADMIN_CMD:
            nrp->QueueType                  = NVME_ADMIN_QUEUE;
            nvmCommand->Cdw0.Opcode         = nvmeIoCtx->cmd.adminCmd.opcode;
            nvmCommand->Cdw0.FusedOperation = NORMAL_CMD;
            nvmCommand->Cdw0.Reserved       = RESERVED;
            nvmCommand->Nsid                = nvmeIoCtx->cmd.adminCmd.nsid;
            if (nvmeIoCtx->cmd.adminCmd.cdw2)
            {
                nvmCommand->Cdw2 = nvmeIoCtx->cmd.adminCmd.cdw2;
                nvmCommand->Flags |= CDW2_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw3)
            {
                nvmCommand->Cdw3 = nvmeIoCtx->cmd.adminCmd.cdw3;
                nvmCommand->Flags |= CDW3_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw10)
            {
                nvmCommand->Cdw10 = nvmeIoCtx->cmd.adminCmd.cdw10;
                nvmCommand->Flags |= CDW10_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw11)
            {
                nvmCommand->Cdw11 = nvmeIoCtx->cmd.adminCmd.cdw11;
                nvmCommand->Flags |= CDW11_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw12)
            {
                nvmCommand->Cdw12 = nvmeIoCtx->cmd.adminCmd.cdw12;
                nvmCommand->Flags |= CDW12_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw13)
            {
                nvmCommand->Cdw13 = nvmeIoCtx->cmd.adminCmd.cdw13;
                nvmCommand->Flags |= CDW13_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw14)
            {
                nvmCommand->Cdw14 = nvmeIoCtx->cmd.adminCmd.cdw14;
                nvmCommand->Flags |= CDW14_VALID;
            }
            if (nvmeIoCtx->cmd.adminCmd.cdw15)
            {
                nvmCommand->Cdw15 = nvmeIoCtx->cmd.adminCmd.cdw15;
                nvmCommand->Flags |= CDW15_VALID;
            }
            break;
        case NVM_CMD:
            nrp->QueueType                  = NVME_IO_QUEUE;
            nvmCommand->Cdw0.Opcode         = nvmeIoCtx->cmd.nvmCmd.opcode;
            nvmCommand->Cdw0.FusedOperation = NORMAL_CMD;
            nvmCommand->Cdw0.Reserved       = RESERVED;
            nvmCommand->Nsid                = nvmeIoCtx->cmd.nvmCmd.nsid;
            if (nvmeIoCtx->cmd.nvmCmd.cdw2)
            {
                nvmCommand->Cdw2 = nvmeIoCtx->cmd.nvmCmd.cdw2;
                nvmCommand->Flags |= CDW2_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw3)
            {
                nvmCommand->Cdw3 = nvmeIoCtx->cmd.nvmCmd.cdw3;
                nvmCommand->Flags |= CDW3_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw10)
            {
                nvmCommand->Cdw10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
                nvmCommand->Flags |= CDW10_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw11)
            {
                nvmCommand->Cdw11 = nvmeIoCtx->cmd.nvmCmd.cdw11;
                nvmCommand->Flags |= CDW11_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw12)
            {
                nvmCommand->Cdw12 = nvmeIoCtx->cmd.nvmCmd.cdw12;
                nvmCommand->Flags |= CDW12_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw13)
            {
                nvmCommand->Cdw13 = nvmeIoCtx->cmd.nvmCmd.cdw13;
                nvmCommand->Flags |= CDW13_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw14)
            {
                nvmCommand->Cdw14 = nvmeIoCtx->cmd.nvmCmd.cdw14;
                nvmCommand->Flags |= CDW14_VALID;
            }
            if (nvmeIoCtx->cmd.nvmCmd.cdw15)
            {
                nvmCommand->Cdw15 = nvmeIoCtx->cmd.nvmCmd.cdw15;
                nvmCommand->Flags |= CDW15_VALID;
            }
            break;
        default:
            return BAD_PARAMETER;
            break;
        }

        nrp->NvmeCmd        = nvmCommand;
        nrp->NvmeCompletion = nvmCompletion;

#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("Sending NVMe Passthru command\n");
        printf("\t->TransferLength = %" PRIu32 "\n", nrp->TransferLength);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
        // NOLINTBEGIN(bugprone-branch-clone)
        if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
        {
            // printf("Sending ADMIN with NSID = %" PRIX32 "h\n", nvmeIoCtx->cmd.adminCmd.nsid);
            start_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = Status =
                pPassthru->PassThru(pPassthru, nvmeIoCtx->cmd.adminCmd.nsid, nrp, M_NULLPTR);
            stop_Timer(&commandTimer);
            // printf("\tAdmin command returned %d\n", Status);
        }
        else
        {
            start_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = Status =
                pPassthru->PassThru(pPassthru, nvmeIoCtx->device->os_info.address.nvme.namespaceID, nrp, M_NULLPTR);
            stop_Timer(&commandTimer);
        }
        // NOLINTEND(bugprone-branch-clone)
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
        set_Console_Colors(true, uefiDebugMessageColor);
        printf("NVMe Passthru command returned ");
        print_EFI_STATUS_To_Screen(Status);
        printf("\t<-TransferLength = %" PRIu32 "\n", nrp->TransferLength);
        set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
        nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

        if (Status == EFI_SUCCESS)
        {
            ret                                              = SUCCESS;
            nvmeIoCtx->commandCompletionData.commandSpecific = nvmCompletion->DW0;
            nvmeIoCtx->commandCompletionData.dw1Reserved     = nvmCompletion->DW1;
            nvmeIoCtx->commandCompletionData.sqIDandHeadPtr  = nvmCompletion->DW2;
            nvmeIoCtx->commandCompletionData.statusAndCID    = nvmCompletion->DW3;
            nvmeIoCtx->commandCompletionData.dw0Valid        = true;
            nvmeIoCtx->commandCompletionData.dw1Valid        = true;
            nvmeIoCtx->commandCompletionData.dw2Valid        = true;
            nvmeIoCtx->commandCompletionData.dw3Valid        = true;
            if (localAlignedBuffer && nvmeIoCtx->commandDirection == XFER_DATA_IN && nvmeIoCtx->ptrData)
            {
                safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, alignedPointer, nvmeIoCtx->dataSize);
            }
        }
        else
        {
            if (nvmeIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                set_Console_Colors(true, CONSOLE_COLOR_RED);
                printf("EFI Status: ");
                print_EFI_STATUS_To_Screen(nvmeIoCtx->device->os_info.last_error);
                set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
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
        safe_free_aligned_core(nrp->MetadataBuffer) safe_free_aligned_core(C_CAST(void**, &nrp));
        safe_free_aligned(&localBuffer);
        safe_free_aligned_core(C_CAST(void**, &nvmCommand));
        safe_free_aligned_core(C_CAST(void**, &nvmCompletion));
        close_NVMe_Passthru_Protocol_Ptr(&pPassthru, nvmeIoCtx->device->os_info.controllerNum);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("NVMe Passthru function returning %d\n", ret);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif

    if (nvmeIoCtx->device->delay_io)
    {
        delay_Milliseconds(nvmeIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", nvmeIoCtx->device->delay_io);
        }
    }

    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues pci_Read_Bar_Reg(tDevice* device, uint8_t* pData, uint32_t dataSize)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Reset(tDevice* device)
{
    // This is a stub. We may not be able to do this in Windows, but want this here in case we can and to make code
    // otherwise compile without ifdefs
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

eReturnValues os_nvme_Subsystem_Reset(tDevice* device)
{
    // This is a stub. We may not be able to do this in Windows, but want this here in case we can and to make code
    // otherwise compile without ifdefs
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

eReturnValues close_Device(tDevice* device)
{
    return NOT_SUPPORTED;
}

uint32_t get_ATA_Device_Count()
{
    uint32_t                    deviceCount = UINT32_C(0);
    EFI_STATUS                  uefiStatus  = EFI_SUCCESS;
    EFI_ATA_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                 handle;
    EFI_GUID                    ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &ataPtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return 0;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &ataPtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                       gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        uint16_t port = UINT16_MAX; // start here since this will make the api find the first available ata port
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextPort(pPassthru, &port);
            if (uefiStatus == EFI_SUCCESS && port != UINT16_MAX)
            {
                // need to call get next device now
                uint16_t   pmport        = UINT16_MAX; // start here so we can find the first port multiplier port
                EFI_STATUS getNextDevice = EFI_SUCCESS;
                while (getNextDevice == EFI_SUCCESS)
                {
                    getNextDevice = pPassthru->GetNextDevice(pPassthru, port, &pmport);
                    if (getNextDevice == EFI_SUCCESS)
                    {
                        // we have a valid port - port multiplier port combination. Try "probing" it to make sure there
                        // is a device by using build device path
                        EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                        EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, port, pmport, &devicePath);
                        if (buildPath == EFI_SUCCESS)
                        {
                            // found a device!!!
                            ++deviceCount;
                        }
                        // EFI_NOT_FOUND means no device at this place.
                        // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                        // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                        safe_free_dev_path_protocol(&devicePath);
                    }
                }
            }
        }
        // close the protocol
        gBS->CloseProtocol(handle[counter], &ataPtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("ATA Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    return deviceCount;
}

eReturnValues get_ATA_Devices(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t* index)
{
    eReturnValues               ret         = NOT_SUPPORTED;
    uint32_t                    deviceCount = UINT32_C(0);
    EFI_STATUS                  uefiStatus  = EFI_SUCCESS;
    EFI_ATA_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                 handle;
    EFI_GUID                    ataPtGUID = EFI_ATA_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &ataPtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &ataPtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                       gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        uint16_t port = UINT16_MAX; // start here since this will make the api find the first available ata port
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextPort(pPassthru, &port);
            if (uefiStatus == EFI_SUCCESS && port != UINT16_MAX)
            {
                // need to call get next device now
                uint16_t   pmport        = UINT16_MAX; // start here so we can find the first port multiplier port
                EFI_STATUS getNextDevice = EFI_SUCCESS;
                while (getNextDevice == EFI_SUCCESS)
                {
                    getNextDevice = pPassthru->GetNextDevice(pPassthru, port, &pmport);
                    if (getNextDevice == EFI_SUCCESS)
                    {
                        // we have a valid port - port multiplier port combination. Try "probing" it to make sure there
                        // is a device by using build device path
                        EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                        EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, port, pmport, &devicePath);
                        if (buildPath == EFI_SUCCESS)
                        {
                            // found a device!!!
                            DECLARE_ZERO_INIT_ARRAY(char, ataHandle, UEFI_HANDLE_STRING_LENGTH);
                            snprintf_err_handle(ataHandle, UEFI_HANDLE_STRING_LENGTH,
                                                "ata:%" PRIx16 ":%" PRIx16 ":%" PRIx16, counter, port, pmport);
                            eReturnValues result = get_Device(ataHandle, &ptrToDeviceList[*index]);
                            if (result != SUCCESS)
                            {
                                ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                            }
                            ++(*index);
                            ++deviceCount;
                        }
                        // EFI_NOT_FOUND means no device at this place.
                        // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                        // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                        safe_free_dev_path_protocol(&devicePath);
                    }
                }
            }
        }
        // close the protocol
        gBS->CloseProtocol(handle[counter], &ataPtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
    if (uefiStatus == EFI_NOT_FOUND)
    {
        // loop finished and we found a port/device
        if (deviceCount > 0)
        {
            // assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}

uint32_t get_SCSI_Device_Count()
{
    uint32_t                     deviceCount = UINT32_C(0);
    EFI_STATUS                   uefiStatus  = EFI_SUCCESS;
    EFI_SCSI_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                  handle;
    EFI_GUID                     scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &scsiPtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return 0;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &scsiPtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                       gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        uint32_t target = UINT32_MAX; // start here since this will make the api find the first available scsi target
        uint64_t lun    = UINT64_MAX; // doesn't specify what we should start with for this.
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextDevice(pPassthru, &target, &lun);
            if (uefiStatus == EFI_SUCCESS && target != UINT16_MAX && lun != UINT64_MAX)
            {
                // we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a
                // device by using build device path
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    ++deviceCount;
                }
                // EFI_NOT_FOUND means no device at this place.
                // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_free_dev_path_protocol(&devicePath);
            }
        }
        // close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSI Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    return deviceCount;
}

eReturnValues get_SCSI_Devices(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t* index)
{
    eReturnValues                ret         = NOT_SUPPORTED;
    uint32_t                     deviceCount = UINT32_C(0);
    EFI_STATUS                   uefiStatus  = EFI_SUCCESS;
    EFI_SCSI_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                  handle;
    EFI_GUID                     scsiPtGUID = EFI_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &scsiPtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &scsiPtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                       gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        uint32_t target = UINT32_MAX; // start here since this will make the api find the first available scsi target
        uint64_t lun    = UINT64_MAX; // doesn't specify what we should start with for this.
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextDevice(pPassthru, &target, &lun);
            if (uefiStatus == EFI_SUCCESS && target != UINT32_MAX && lun != UINT64_MAX)
            {
                // we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a
                // device by using build device path
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    // found a device!!!
                    DECLARE_ZERO_INIT_ARRAY(char, scsiHandle, UEFI_HANDLE_STRING_LENGTH);
                    snprintf_err_handle(scsiHandle, UEFI_HANDLE_STRING_LENGTH, "scsi:%" PRIx16 ":%" PRIx32 ":%" PRIx64,
                                        counter, target, lun);
                    eReturnValues result = get_Device(scsiHandle, &ptrToDeviceList[*index]);
                    if (result != SUCCESS)
                    {
                        ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                    }
                    ++(*index);
                    ++deviceCount;
                }
                // EFI_NOT_FOUND means no device at this place.
                // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_free_dev_path_protocol(&devicePath);
            }
        }
        // close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
    if (uefiStatus == EFI_NOT_FOUND)
    {
        // loop finished and we found a port/device
        if (deviceCount > 0)
        {
            // assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}

uint32_t get_SCSIEx_Device_Count()
{
    uint32_t                         deviceCount = UINT32_C(0);
    EFI_STATUS                       uefiStatus  = EFI_SUCCESS;
    EFI_EXT_SCSI_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                      handle;
    EFI_GUID                         scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &scsiPtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return 0;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, invalidTarget, TARGET_MAX_BYTES);
        DECLARE_ZERO_INIT_ARRAY(uint8_t, target, TARGET_MAX_BYTES);
        uint8_t* targetPtr = &target[0];
        uint64_t lun       = UINT64_MAX; // doesn't specify what we should start with for this.
        uefiStatus         = gBS->OpenProtocol(handle[counter], &scsiPtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                               gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        safe_memset(target, TARGET_MAX_BYTES, 0xFF, TARGET_MAX_BYTES);
        safe_memset(invalidTarget, TARGET_MAX_BYTES, 0xFF, TARGET_MAX_BYTES);
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextTargetLun(pPassthru, &targetPtr, &lun);
            if (uefiStatus == EFI_SUCCESS && memcmp(target, invalidTarget, TARGET_MAX_BYTES) != 0 && lun != UINT64_MAX)
            {
                // we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a
                // device by using build device path
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS buildPath = pPassthru->BuildDevicePath(pPassthru, targetPtr, lun, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    // found a device!!!
                    ++deviceCount;
                }
                // EFI_NOT_FOUND means no device at this place.
                // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_free_dev_path_protocol(&devicePath);
            }
        }
        // close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
#if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("SCSIEx Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#endif
    return deviceCount;
}

eReturnValues get_SCSIEx_Devices(tDevice* const ptrToDeviceList,
                                 uint32_t       sizeInBytes,
                                 versionBlock   ver,
                                 uint32_t*      index)
{
    eReturnValues                    ret         = NOT_SUPPORTED;
    uint32_t                         deviceCount = UINT32_C(0);
    EFI_STATUS                       uefiStatus  = EFI_SUCCESS;
    EFI_EXT_SCSI_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                      handle;
    EFI_GUID                         scsiPtGUID = EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &scsiPtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }

    UINTN counter = 0;
    while (counter < nodeCount)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, invalidTarget, TARGET_MAX_BYTES);
        DECLARE_ZERO_INIT_ARRAY(uint8_t, target, TARGET_MAX_BYTES);
        uint8_t* targetPtr = &target[0];
        uint64_t lun       = UINT64_MAX; // doesn't specify what we should start with for this.
        uefiStatus         = gBS->OpenProtocol(handle[counter], &scsiPtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                               gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        safe_memset(target, TARGET_MAX_BYTES, 0xFF, TARGET_MAX_BYTES);
        safe_memset(invalidTarget, TARGET_MAX_BYTES, 0xFF, TARGET_MAX_BYTES);
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextTargetLun(pPassthru, &targetPtr, &lun);
            if (uefiStatus == EFI_SUCCESS && memcmp(target, invalidTarget, TARGET_MAX_BYTES) != 0 && lun != UINT64_MAX)
            {
                // we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a
                // device by using build device path
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath = pPassthru->BuildDevicePath(pPassthru, target, lun, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    // found a device!!!
                    DECLARE_ZERO_INIT_ARRAY(char, scsiExHandle, UEFI_HANDLE_STRING_LENGTH);
                    snprintf_err_handle(scsiExHandle, UEFI_HANDLE_STRING_LENGTH,
                                        "scsiEx:%" PRIx16 ":%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8
                                        "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8
                                        "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 "%02" PRIx8 ":%" PRIx64,
                                        counter, target[0], target[1], target[2], target[3], target[4], target[5],
                                        target[6], target[7], target[8], target[9], target[10], target[11], target[12],
                                        target[13], target[14], target[15], lun);
                    eReturnValues result = get_Device(scsiExHandle, &ptrToDeviceList[*index]);
                    if (result != SUCCESS)
                    {
                        ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                    }
                    ++(*index);
                    ++deviceCount;
                }
                // EFI_NOT_FOUND means no device at this place.
                // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_free_dev_path_protocol(&devicePath);
            }
        }
        // close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &scsiPtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
    if (uefiStatus == EFI_NOT_FOUND)
    {
        // loop finished and we found a port/device
        if (deviceCount > 0)
        {
            // assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
}

uint32_t get_NVMe_Device_Count()
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    uint32_t                            deviceCount = UINT32_C(0);
    EFI_STATUS                          uefiStatus  = EFI_SUCCESS;
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                         handle;
    EFI_GUID                            nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &nvmePtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return 0;
    }
    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &nvmePtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                       gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        uint32_t namespaceID =
            UINT32_MAX; // start here since this will make the api find the first available nvme namespace
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextNamespace(pPassthru, &namespaceID);
            if (uefiStatus == EFI_SUCCESS && namespaceID != UINT32_MAX)
            {
                // we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a
                // device by using build device path
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath = pPassthru->BuildDevicePath(pPassthru, namespaceID, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    // found a device!!!
                    ++deviceCount;
                }
                // EFI_NOT_FOUND means no device at this place.
                // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_free_dev_path_protocol(&devicePath);
            }
        }
        // close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &nvmePtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
#    if defined(UEFI_PASSTHRU_DEBUG_MESSAGES)
    set_Console_Colors(true, uefiDebugMessageColor);
    printf("NVMe Device count = %" PRIu32 "\n", deviceCount);
    set_Console_Colors(true, CONSOLE_COLOR_DEFAULT);
#    endif
    return deviceCount;
#else  // DISABLE_NVME_PASSTHROUGH
    return 0;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues get_NVMe_Devices(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint32_t* index)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues                       ret         = NOT_SUPPORTED;
    uint32_t                            deviceCount = UINT32_C(0);
    EFI_STATUS                          uefiStatus  = EFI_SUCCESS;
    EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL* pPassthru;
    EFI_HANDLE*                         handle;
    EFI_GUID                            nvmePtGUID = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID;

    UINTN nodeCount = 0;
    uefiStatus      = gBS->LocateHandleBuffer(ByProtocol, &nvmePtGUID, M_NULLPTR, &nodeCount, &handle);
    if (EFI_ERROR(uefiStatus))
    {
        return FAILURE;
    }
    UINTN counter = 0;
    while (counter < nodeCount)
    {
        uefiStatus = gBS->OpenProtocol(handle[counter], &nvmePtGUID, M_REINTERPRET_CAST(void**, &pPassthru),
                                       gImageHandle, M_NULLPTR, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
        if (EFI_ERROR(uefiStatus))
        {
            continue;
        }
        uint32_t namespaceID =
            UINT32_MAX; // start here since this will make the api find the first available nvme namespace
        while (uefiStatus == EFI_SUCCESS)
        {
            uefiStatus = pPassthru->GetNextNamespace(pPassthru, &namespaceID);
            if (uefiStatus == EFI_SUCCESS && namespaceID != UINT32_MAX)
            {
                // we have a valid port - port multiplier port combination. Try "probing" it to make sure there is a
                // device by using build device path
                EFI_DEVICE_PATH_PROTOCOL* devicePath; // will be allocated in the call to the uefi systen
                EFI_STATUS                buildPath = pPassthru->BuildDevicePath(pPassthru, namespaceID, &devicePath);
                if (buildPath == EFI_SUCCESS)
                {
                    // found a device!!!
                    DECLARE_ZERO_INIT_ARRAY(char, nvmeHandle, UEFI_HANDLE_STRING_LENGTH);
                    snprintf_err_handle(nvmeHandle, UEFI_HANDLE_STRING_LENGTH, "nvme:%" PRIx16 ":%" PRIx32, counter,
                                        namespaceID);
                    eReturnValues result = get_Device(nvmeHandle, &ptrToDeviceList[*index]);
                    if (result != SUCCESS)
                    {
                        ret = WARN_NOT_ALL_DEVICES_ENUMERATED;
                    }
                    ++(*index);
                    ++deviceCount;
                }
                // EFI_NOT_FOUND means no device at this place.
                // EFI_INVALID_PARAMETER means DevicePath is null (this function should allocate the path for us
                // according to the API) EFI_OUT_OF_RESOURCES means cannot allocate memory.
                safe_free_dev_path_protocol(&devicePath);
            }
        }
        // close the protocol since we're going to open this again in getdevice
        gBS->CloseProtocol(handle[counter], &nvmePtGUID, gImageHandle, M_NULLPTR);
        ++counter;
    }
    if (uefiStatus == EFI_NOT_FOUND)
    {
        // loop finished and we found a port/device
        if (deviceCount > 0)
        {
            // assuming that since we enumerated something, that everything worked and we are able to talk to something
            ret = SUCCESS;
        }
    }
    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    return NOT_SUPPORTED;
#endif // DISABLE_NVME_PASSTHROUGH
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
//!						 NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
eReturnValues get_Device_Count(uint32_t* numberOfDevices, M_ATTR_UNUSED uint64_t flags)
{
    *numberOfDevices += get_ATA_Device_Count();
    *numberOfDevices += get_SCSI_Device_Count();
    *numberOfDevices += get_SCSIEx_Device_Count();
    *numberOfDevices += get_NVMe_Device_Count();
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
eReturnValues get_Device_List(tDevice* const         ptrToDeviceList,
                              uint32_t               sizeInBytes,
                              versionBlock           ver,
                              M_ATTR_UNUSED uint64_t flags)
{
    uint32_t index = UINT32_C(0);
    get_ATA_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    get_SCSI_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    get_SCSIEx_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    get_NVMe_Devices(ptrToDeviceList, sizeInBytes, ver, &index);
    return SUCCESS;
}

eReturnValues os_Read(M_ATTR_UNUSED tDevice* device,
                      M_ATTR_UNUSED uint64_t lba,
                      M_ATTR_UNUSED bool     forceUnitAccess,
                      M_ATTR_UNUSED uint8_t* ptrData,
                      M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Write(M_ATTR_UNUSED tDevice* device,
                       M_ATTR_UNUSED uint64_t lba,
                       M_ATTR_UNUSED bool     forceUnitAccess,
                       M_ATTR_UNUSED uint8_t* ptrData,
                       M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Verify(M_ATTR_UNUSED tDevice* device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Flush(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Lock_Device(M_ATTR_UNUSED tDevice* device)
{
    return SUCCESS;
}

eReturnValues os_Unlock_Device(M_ATTR_UNUSED tDevice* device)
{
    return SUCCESS;
}

eReturnValues os_Update_File_System_Cache(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    // edk2 might have some kind of partition function we can call for the device to erase it
    return NOT_SUPPORTED;
}

eReturnValues os_Unmount_File_Systems_On_Device(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}
