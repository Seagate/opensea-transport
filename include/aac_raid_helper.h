// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file aac_raid_helper.h
// \brief Defines constants, structures, etc. necessary to communicate with physical drives behind an Adaptec RAID (aac, aacraid, arcsas)

#pragma once

#if defined (ENABLE_AAC)

#include "common_types.h"
#include "code_attributes.h"
#include <stdint.h>
#include "scsi_helper.h"
#include "aac_raid_helper.h"
#include "raid_scan_helper.h"

#if defined (_WIN32) && !defined(_NTDDSCSIH_)
    #include <ntddscsi.h>
#endif

#if defined (__cplusplus)
extern "C"
{
#endif //__cplusplus

       #define AAC_HANDLE_BASE_NAME "aac" //all aac handles coming into the utility will look like this.

        #if defined (_WIN32)
            #define AAC_HANDLE HANDLE
            #define AAC_INVALID_HANDLE INVALID_HANDLE_VALUE
        #else
            #define AAC_HANDLE int
            #define AAC_INVALID_HANDLE -1
        #endif

        #if defined (_WIN32)
            #define AAC_SYSTEM_IOCTL_SUCCESS TRUE
        #else /*linux*/
            #define AAC_SYSTEM_IOCTL_SUCCESS 0
        #endif

        typedef struct _aacDeviceInfo
        {
            AAC_HANDLE aacHandle;
            bool valid;//structure has valid data.
            uint32_t bus;
            uint32_t target;
            uint32_t lun;
            uint32_t controllerSupportedOptions;//bitfield set by driver. Use aacraid_reg definitions. Can be used to help us figure out how to issue commands to a given controller.
            uint32_t supplementalSupportedOptions;//only valid if the supplemental flag is set in previous field by controller
            //NOTE: Some versions of the driver use certain packed structures and others do not.
            //      We may need a flag for when to use this version
            uint32_t maxSGList;//some drivers may be limited to a certain number of user IO submitted sg entries in a list. FreeBSD is limited to 1 entry. Linux is limited to 256. Illumos does not appear to limit the number of entries or enforce a length
            uint32_t maxSGTransferLength;//reported by controller options/info. If not reported from controller, limited to 64K. This seems to be per sgentry from the user. Some code shows a limit of 1 sg entry, some show more than 1. NOTE: If HBA reports max sectors, it's in 512B sectors!
            //uint32_t for storing scsi method???
        }aacDeviceInfo, *ptrAacDeviceInfo;


#if defined (__cplusplus)
}
#endif //__cplusplus

#endif //ENABLE_AAC
