//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2020 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file csmi_helper.h
// \brief Defines the constants structures to help with CSMI implementation. This tries to be generic for any OS, even though Windows is the only known supported OS (pending what driver you use)

#pragma once

#if defined (ENABLE_CSMI)

#include "common.h"
#include <stdint.h>
#include "csmisas.h"
#include "sata_types.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //CSMI Handles vary between Windows and non-Windows due to some interface differences and how we parse the data we need to find the device.
    //LUN will always be zero on SATA. SAS will mostly likely only have 1 LUN, but there are some multi-LUN products (i.e. dual actuator)
    //Windows: csmi<controller>:<port>:<lun>
    //Linux : csmi<controller>:<port>:<lun>:<sysHandle> where sysHandle is something like /dev/sg? or some other handle that is made to the controller. Will need to find one that actually uses this interface in Linux first before we will know for certain

    #define CSMI_HANDLE_BASE_NAME "csmi" //all CSMI handles coming into the utility will look like this.

    #if defined(_WIN32)
        //NOTE: Would reuse definitions from win_helper.h, however that causes a circular include problem, so these are redefined here...if we find a better solution, it would be preferred to reuse the win_helper.h definitions
        #define WIN_CSMI_DRIVE  "\\\\.\\SCSI" // WIN_SCSI_SRB //In windows, we need to open the base SCSI SRB handle to issue CSMI IOs
        #define CSMI_WIN_MAX_DEVICE_NAME_LENGTH UINT8_C(40) //WIN_MAX_DEVICE_NAME_LENGTH
    #elif defined (__unix__)
        #define NIX_CSMI_DRIVE  "/dev/hba" //This is purely an example, not really useful beyond this
    #else
        #message Unknown OS...may or may not need a device prefix.
    #endif

    #if defined (_WIN32)
        #define CSMI_HANDLE HANDLE
        #define CSMI_INVALID_HANDLE INVALID_HANDLE_VALUE
    #else
        #define CSMI_HANDLE int
        #define CSMI_INVALID_HANDLE -1
    #endif


    //This is all the data, minus the IOCTL header, of the CSMI_SAS_GET_SCSI_ADDRESS_BUFFER
    //Defined here since I don't want to be responsible for adding it onto the csmisas.h file - TJE
    typedef struct _csmiSCSIAddress
    {
        uint8_t hostIndex;
        uint8_t pathId;
        uint8_t targetId;
        uint8_t lun;
    }csmiSCSIAddress, *ptrCSMISCSIAddress;

    //This structure is intended to be used in windows/linux for CSMI if a device supports this, whether in RAID or JBOD mode.
    //RAID implementations are suggested to include this as a substructure or have a pointer of some kind to this.
    typedef struct _csmiDeviceInfo
    {
        CSMI_HANDLE *csmiDevHandle;//This is a pointer to the OS device handle. The reason for this is because on Windows, this can be device->os_info.fd or device->os_info.scsiSRBHandle, but on linux, it will be only device->os_info.fd. This complication is to support using CSMI IOCTLs on JBOD and in RAID mode without duplicating devices. - TJE
        bool csmiDeviceInfoValid;//whole structure contains valid information
        bool scsiAddressValid;
        bool signatureFISValid;
        uint8_t phyIdentifier;
        uint8_t portIdentifier;
        uint8_t portProtocol;
        uint8_t sataPMPort;//for SATA devices, this is used when building the FIS since we need to route through port multipliers correctly if they are being used. Most likely this will be zero
        uint8_t lun;//This is separate from the sasLUN below because some things need a 64 bit LUN, others only need this 8 bit LUN field (SSP)
        uint8_t sasAddress[8];//may be empty in some cases, but that is ok. Between this and the identifiers above, we should still be able to issue commands.
        uint8_t sasLUN[8];//Only needed for RAID discovery and even then, probably only for a few specific circumstances
        uint16_t csmiMajorVersion;//from driver info
        uint16_t csmiMinorVersion;
        uint32_t controllerFlags;//from controller config
        uint32_t controllerNumber;//for non-Windows since this is part of the IOCTL header
        csmiSCSIAddress scsiAddress;
        sataD2HFis signatureFIS;
        struct {
            bool intelRSTSupported;//Will only be true for Intel RST ioctls that extend functionality for the Windows driver...firmware download, nvme passthrough are currently used, but there are some other IOCTLs too. -TJE
            bool nvmePassthrough;
            bool fwdlIOSupported;
            bool allowFlexibleUseOfAPI;//Set this to true to allow using the Win10 API for FWDL for any compatible download commands. If this is false, the Win10 API will only be used on IDE_INTERFACE for an ATA download command and SCSI interface for a supported Write buffer command. If true, it will be used regardless of which command the caller is using. This is useful for pure FW updates versus testing a specific condition.
            uint32_t payloadAlignment; //From MSDN: The alignment of the image payload, in number of bytes. The maximum is PAGE_SIZE. The transfer size is a mutliple of this size. Some protocols require at least sector size. When this value is set to 0, this means that this value is invalid.
            uint32_t maxXferSize; //From MSDN: The image payload maximum size, this is used for a single command
        }intelRSTSupport;
    }csmiDeviceInfo, *ptrCsmiDeviceInfo;

    #if defined (_WIN32)
        #define CSMI_SYSTEM_IOCTL_SUCCESS TRUE
    #else /*linux*/
        #define CSMI_SYSTEM_IOCTL_SUCCESS 0
    #endif

#if defined (__cplusplus)
}
#endif
#endif