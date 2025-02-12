// SPDX-License-Identifier: MPL-2.0

//! \file csmi_helper.h
//! \brief  Defines the constants structures to help with CSMI implementation. This tries to be generic for any OS, even
// though Windows is the only known supported OS (pending what driver you use)
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(ENABLE_CSMI)

#    include "common_types.h"
#    include <stdint.h>
#    if defined(_WIN32) && !defined(_NTDDSCSIH_)
#        include <ntddscsi.h>
#    endif
#    include "external/csmi/csmisas.h"
#    include "sata_types.h"

#    if defined(__cplusplus)
extern "C"
{
#    endif

    // CSMI Handles vary between Windows and non-Windows due to some interface differences and how we parse the data we
    // need to find the device. LUN will always be zero on SATA. SAS will mostly likely only have 1 LUN, but there are
    // some multi-LUN products (i.e. dual actuator) Windows: csmi<controller>:<port>:<lun> Linux :
    // csmi<controller>:<port>:<lun>:<sysHandle> where sysHandle is something like /dev/sg? or some other handle that is
    // made to the controller. Will need to find one that actually uses this interface in Linux first before we will
    // know for certain

#    define CSMI_HANDLE_BASE_NAME "csmi" // all CSMI handles coming into the utility will look like this.

#    if defined(_WIN32)
// NOTE: Would reuse definitions from win_helper.h, however that causes a circular include problem, so these are
// redefined here...if we find a better solution, it would be preferred to reuse the win_helper.h definitions
#        define WIN_CSMI_DRIVE                                                                                         \
            "\\\\.\\SCSI" // WIN_SCSI_SRB //In windows, we need to open the base SCSI SRB handle to issue CSMI IOs
#        define CSMI_WIN_MAX_DEVICE_NAME_LENGTH UINT8_C(40) // WIN_MAX_DEVICE_NAME_LENGTH
#        define CSMI_DEVIVE_NAME_MAX_LENGTH     CSMI_WIN_MAX_DEVICE_NAME_LENGTH
#    elif defined(__unix__)
#        define NIX_CSMI_DRIVE                  "/dev/hba" // This is purely an example, not really useful beyond this
#        define CSMI_NIX_MAX_DEVICE_NAME_LENGTH UINT8_C(32)
#        define CSMI_DEVIVE_NAME_MAX_LENGTH     CSMI_NIX_MAX_DEVICE_NAME_LENGTH
#    else
#        message Unknown OS... may or may not need a device prefix.
#        define CSMI_DEVIVE_NAME_MAX_LENGTH UINT8_C(32)
#    endif

#    if defined(_WIN32)
#        define CSMI_HANDLE               HANDLE
#        define CSMI_INVALID_HANDLE       INVALID_HANDLE_VALUE
#        define CSMI_HANDLE_PARAM(argnum) M_NONNULL_PARAM_LIST(argnum) M_PARAM_RW(argnum)
#    else
#        define CSMI_HANDLE               int
#        define CSMI_INVALID_HANDLE       (-1)
#        define CSMI_HANDLE_PARAM(argnum) M_FILE_DESCRIPTOR(argnum)
#    endif

    // NOTE: This may need expanding if specific versions of each driver need tracking uniquely due to significant
    // differences in reporting or behavior.
    typedef enum eKnownCSMIDriverEnum
    {
        CSMI_DRIVER_UNKNOWN = 0,
        CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY,
        CSMI_DRIVER_INTEL_VROC,
        CSMI_DRIVER_AMD_RCRAID,
        CSMI_DRIVER_HPCISS,
        CSMI_DRIVER_ARCSAS,
        CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY_VD, // iastorvd....not sure how this differs from other drivers
        CSMI_DRIVER_INTEL_GENERIC, // use this if we cannot figure out a better classification. It's a "catch-all" for
                                   // intel based on iaStor....there are a few variants above that we already catch.
        CSMI_DRIVER_HPSAMD, // HpSAMD.sys. While this responds to some CSMI IOCTLs, I cannot get any passthrough to
                            // work. Even changing the registry to CSMI=Full; does not seem to work or be supported.
                            // Setting it to none or limited appear to work, but it is unclear why full does not
                            // work.-TJE
    } eKnownCSMIDriver;

    // This is all the data, minus the IOCTL header, of the CSMI_SAS_GET_SCSI_ADDRESS_BUFFER
    // Defined here since I don't want to be responsible for adding it onto the csmisas.h file - TJE
    typedef struct s_csmiSCSIAddress
    {
        uint8_t hostIndex;
        uint8_t pathId;
        uint8_t targetId;
        uint8_t lun;
    } csmiSCSIAddress, *ptrCSMISCSIAddress;

    // This is helpful for determining if a driver is configured to allow everything or not, mostly in Windows.
    typedef enum eCSMISecurityAccessEnum
    {
        CSMI_SECURITY_ACCESS_NONE       = 0,
        CSMI_SECURITY_ACCESS_RESTRICTED = 1,
        CSMI_SECURITY_ACCESS_LIMITED    = 2,
        CSMI_SECURITY_ACCESS_FULL       = 3
    } eCSMISecurityAccess;

    // This structure is intended to be used in windows/linux for CSMI if a device supports this, whether in RAID or
    // JBOD mode. RAID implementations are suggested to include this as a substructure or have a pointer of some kind to
    // this.
    typedef struct s_csmiDeviceInfo
    {
        CSMI_HANDLE csmiDevHandle;   // This is a pointer to the OS device handle. The reason for this is because on
                                     // Windows, this can be device->os_info.fd or device->os_info.scsiSRBHandle, but on
                                     // linux, it will be only device->os_info.fd. This complication is to support using
                                     // CSMI IOCTLs on JBOD and in RAID mode without duplicating devices. - TJE
        bool    csmiDeviceInfoValid; // whole structure contains valid information
        bool    scsiAddressValid;
        bool    signatureFISValid;
        uint8_t phyIdentifier;
        uint8_t portIdentifier;
        uint8_t portProtocol;
        uint8_t sataPMPort; // for SATA devices, this is used when building the FIS since we need to route through port
                            // multipliers correctly if they are being used. Most likely this will be zero
        uint8_t lun; // This is separate from the sasLUN below because some things need a 64 bit LUN, others only need
                     // this 8 bit LUN field (SSP)
        uint8_t sasAddress[8]; // may be empty in some cases, but that is ok. Between this and the identifiers above, we
                               // should still be able to issue commands.
        uint8_t
            sasLUN[8]; // Only needed for RAID discovery and even then, probably only for a few specific circumstances
        uint16_t        csmiMajorVersion; // from driver info
        uint16_t        csmiMinorVersion;
        uint32_t        controllerFlags;  // from controller config
        uint32_t        controllerNumber; // for non-Windows since this is part of the IOCTL header
        csmiSCSIAddress scsiAddress;
        sataD2HFis      signatureFIS;
        struct
        {
            bool intelRSTSupported; // Will only be true for Intel RST ioctls that extend functionality for the Windows
                                    // driver...firmware download, nvme passthrough are currently used, but there are
                                    // some other IOCTLs too. -TJE
            bool nvmePassthrough;
            bool fwdlIOSupported;
            bool allowFlexibleUseOfAPI; // Set this to true to allow using the Win10 API for FWDL for any compatible
                                        // download commands. If this is false, the Win10 API will only be used on
                                        // IDE_INTERFACE for an ATA download command and SCSI interface for a supported
                                        // Write buffer command. If true, it will be used regardless of which command
                                        // the caller is using. This is useful for pure FW updates versus testing a
                                        // specific condition.
            uint32_t payloadAlignment; // From MSDN: The alignment of the image payload, in number of bytes. The maximum
                                       // is PAGE_SIZE. The transfer size is a mutliple of this size. Some protocols
                                       // require at least sector size. When this value is set to 0, this means that
                                       // this value is invalid.
            uint32_t maxXferSize;      // From MSDN: The image payload maximum size, this is used for a single command
        } intelRSTSupport;
        eCSMISecurityAccess securityAccess; // mostly for Windows...
        eKnownCSMIDriver
            csmiKnownDriverType; // can be used to work around specific known implementation differences/bugs. -TJE
    } csmiDeviceInfo, *ptrCsmiDeviceInfo;

#    if defined(_WIN32)
#        define CSMI_SYSTEM_IOCTL_SUCCESS TRUE
#    else /*linux*/
#        define CSMI_SYSTEM_IOCTL_SUCCESS 0
#    endif

    // These definitions should be used when printing from CSMI headers since the types may be different than expected
    // and generate warnings Also, note, these MAY vary between linux and Windows, so update these to fix warnings as
    // necessary (without breaking existing systems) Linux includes linux/types.h for these. They are defined in
    // csmisas.h for Windows NOTE: not taking into account netware because it isn't supported or used at this point.
    // NOTE: only defining the printf macros we used. Adding a "C" to the beginning to differentiating them with the
    // standards
#    ifdef __linux__
    // Define these as best we can based on linux/types.h which varies depending on architecture
    // Since this include path includes another file actually making the definition, check which one it is using the
    // definitions they define
    // TODO: if __WORDSIZE == 64 may also work to switch between, but not positive - TJE

#        if defined(_ASM_GENERIC_INT_L64_H)
#            define CPRIu8  "u"
#            define CPRIu16 "u"
#            define CPRIu32 "u"
#            define CPRIu64 "lu"

#            define CPRIX8  "X"
#            define CPRIX16 "X"
#            define CPRIX32 "X"
#            define CPRIX64 "lX"
#        elif defined(_ASM_GENERIC_INT_LL64_H)
#            define CPRIu8  "u"
#            define CPRIu16 "u"
#            define CPRIu32 "u"
#            define CPRIu64 "llu"

#            define CPRIX8  "X"
#            define CPRIX16 "X"
#            define CPRIX32 "X"
#            define CPRIX64 "llX"
#        else
#            error "Need to define CSMI printf macros for this OS"
#        endif
#    else

// match the csmisas.h definitions of the types as best we can
#        define CPRIu8  "u"
#        define CPRIu16 "u"
#        if !defined __LP64__ // ILP32 (32-bit), LLP64 (64-bit MSVC, MinGW)
#            define CPRIu32 "lu"
#        else // LP64 (64-bit Cygwin)
#            define CPRIu32 "u"
#        endif
#        define CPRIu64 "llu"

#        define CPRIX8  "X"
#        define CPRIX16 "X"
#        ifndef __LP64__ // ILP32 (32-bit), LLP64 (64-bit MSVC, MinGW)
#            define CPRIX32 "lX"
#        else // LP64 (64-bit Cygwin)
#            define CPRIX32 "X"
#        endif
#        define CPRIX64 "llX"

#    endif

#    if defined(__cplusplus)
}
#    endif

#endif // ENABLE_CSMI
