// SPDX-License-Identifier: MPL-2.0

//! \file win_helper.h
//! \brief Defines the constants structures specific to Windows OS.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_public.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdio.h>  // for printf
#include <stdlib.h> // for size_t types
#include <string.h> // For memset

#if !defined(__MINGW32__)
    // this must be defined before including scsi.h
#    define _NTSCSI_USER_MODE_ // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#    include <scsi.h>
#    undef _NTSCSI_USER_MODE_ // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#    define INCLUDED_SCSI_DOT_H
#else
#    if !defined(SRB_TYPE_SCSI_REQUEST_BLOCK)
#        define SRB_TYPE_SCSI_REQUEST_BLOCK 0
#    endif
#endif

//
// Indicate that the existing firmware in slot should be activated immediately without
// controller reset. Only valid for IOCTL_STORAGE_FIRMWARE_ACTIVATE.
//
// added in 10.0.26100.0
#if !defined(STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_FIRMWARE_WITHOUT_RESET)
#    define STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_FIRMWARE_WITHOUT_RESET 0x10000000
#endif

//
// Indicate that any existing firmware in slot should be replaced with the downloaded image,
// and activated with controller reset. Only valid for IOCTL_STORAGE_FIRMWARE_ACTIVATE.
//
// added in 10.0.26100.0
#if !defined(STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_AND_SWITCH_UPON_RESET)
#    define STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_AND_SWITCH_UPON_RESET 0x20000000
#endif

//
// Indicate that any existing firmware in slot should be replaced with the downloaded image.
// Only valid for IOCTL_STORAGE_FIRMWARE_ACTIVATE.
//
// added in 10.0.22621.0
#if !defined(STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_EXISTING_IMAGE)
#    define STORAGE_HW_FIRMWARE_REQUEST_FLAG_REPLACE_EXISTING_IMAGE 0x40000000
#endif

//
// Indicate that the existing firmware in slot should be activated with a controller reset.
// Only valid for IOCTL_STORAGE_FIRMWARE_ACTIVATE.
//
#if !defined(STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE)
#    define STORAGE_HW_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE 0x80000000
#endif

    // Used internally to set the flags above for the new firmware update IOCTL.
    // NOTE: This versions checks win10/11 to determine when the requested mode is supporetd.
    // If not supported, it returns OS_COMMAND_NOT_AVAILABLE
    M_PARAM_RW(1)
    M_PARAM_RW(2)
    eReturnValues set_NVMe_Firmware_Activate_Flags(nvmeCmdCtx* M_NONNULL nvmeIoCtx, uint32_t* M_NONNULL currentFlags);

#define WIN_SCSI_SRB       "\\\\.\\SCSI" // can be used to issue mini port ioctls. Not really supported right now...
#define WIN_PHYSICAL_DRIVE "\\\\.\\PhysicalDrive"
#define WIN_TAPE_DRIVE     "\\\\.\\Tape"
#define WIN_CDROM_DRIVE                                                                                                \
    "\\\\.\\CDROM" // Most likely an ATAPI device, but it could be a really old SCSI interface device...
#define WIN_CHANGER_DEVICE         "\\\\.\\Changer" // This is a SCSI type device

#define WIN_SCSI_SRB_MAX_LEN       UINT8_C(15)
#define WIN_MAX_DEVICE_NAME_LENGTH UINT8_C(40)

#define DOUBLE_BUFFERED_MAX_TRANSFER_SIZE                                                                              \
    16384 // Bytes....16KiB to be exact since that is what MS documentation says. - TJE

    // This is the maximum timeout a command can use in Windows...30 hours
#define WIN_MAX_CMD_TIMEOUT_SECONDS 108000

    // Configuration manager library is not available on ARM for Windows. Library didn't exist when I went looking for
    // it - TJE NOTE: ARM requires 10.0.16299.0 API to get this library!
#if !defined(__MINGW32__) && !defined(__MINGW64__)
#    pragma comment(lib, "Cfgmgr32.lib") // make sure this get's linked in
#endif

#if defined(__cplusplus)
}
#endif
