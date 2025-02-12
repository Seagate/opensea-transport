// SPDX-License-Identifier: MPL-2.0

//! \file intel_rst_defs.h
//! \brief Defines the structures, ioctls, etc for Intel's RST driver specific IOCTLs for additional
//! support beyond CSMI spec
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once
#if defined(_WIN32)
#    include "predef_env_detect.h"
DISABLE_WARNING_4255
#    include <windows.h>
RESTORE_WARNING_4255
#    if !defined(_NTDDSCSIH_)
#        include <ntddscsi.h>
#    endif

#    if defined(__cplusplus)
extern "C"
{
#    endif

    // Intel RST NVMe passthrough definitions. RST Version 14.6, 14.8, 15.x, 16.x, 17.x 18.x

#    define INTELNVM_SIGNATURE "IntelNvm"

    // NOTE: Upsupported NVMe commands return SRB_STATUS_INVALID_REQUEST

#    define IOCTL_NVME_PASSTHROUGH    CTL_CODE(0xF000, 0xA02, METHOD_BUFFERED, FILE_ANY_ACCESS)

#    define NVME_PASS_THROUGH_VERSION 1

    typedef struct s_GENERIC_COMMAND
    {
        ULONG DWord0;
        ULONG DWord1;
        ULONG DWord2;
        ULONG DWord3;
        ULONG DWord4;
        ULONG DWord5;
        ULONG DWord6;
        ULONG DWord7;
        ULONG DWord8;
        ULONG DWord9;
        ULONG DWord10;
        ULONG DWord11;
        ULONG DWord12;
        ULONG DWord13;
        ULONG DWord14;
        ULONG DWord15;
    } GENERIC_COMMAND;

    typedef struct s_COMPLETION_QUEUE_ENTRY
    {
        ULONG completion0;
        ULONG completion1;
        ULONG completion2;
        ULONG completion3;
    } COMPLETION_QUEUE_ENTRY;

    typedef struct s_NVME_PASS_THROUGH_PARAMETERS
    {
        GENERIC_COMMAND        Command;
        BOOLEAN                IsIOCommandSet; // False for Admin queue
        COMPLETION_QUEUE_ENTRY Completion;
        ULONG                  DataBufferOffset; // Must be DWORD aligned offset from beginning of SRB_IO_CONTROL
        ULONG                  DataBufferLength;
        ULONG                  Reserved[10];
    } NVME_PASS_THROUGH_PARAMETERS;

    typedef struct s_NVME_IOCTL_PASS_THROUGH
    {
        SRB_IO_CONTROL Header;
        UCHAR          Version; // Set to NVME_PASS_THROUGH_VERSION
        UCHAR PathId;   // Port number from Windows (non-RAID) or CSMI port number (GET_RAID_INFO and GET_RAID_CONFIG)
        UCHAR TargetId; // Reserved for now
        UCHAR Lun;      // Reserved for now
        NVME_PASS_THROUGH_PARAMETERS Parameters;
        UCHAR data[1]; // So that is is easy to select the data buffer offset when building the command.
    } NVME_IOCTL_PASS_THROUGH;

    // AER API Support starting RST versino 17.5

    /////////////////////////////////////End of NVME Passthrough API//////////////////////////////////////////

    // Firmware Download API (Starting RST version 14.8)
    // For NVMe and SATA drives. SATA Drives must meet Win10 requirements (deferred download support)
    // Lots redefined from WinAPI to make sure this is more portable - TJE
    // Process should follow this, but using the below Intel & RAID FW update functions&structs:
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/upgrading-firmware-for-an-nvme-device?redirectedfrom=MSDN

#    define INTEL_RAID_FW_SIGNATURE             "IntelFw "

#    define IOCTL_RAID_FIRMWARE                 CTL_CODE(0xF000, 0x010, METHOD_BUFFERED, FILE_ANY_ACCESS)

#    define RAID_FIRMWARE_REQUEST_BLOCK_VERSION 1

    // Redefined from FIRMWARE_REQUEST_BLOCK so that up to date Windows API is not necessary for compilation
#    define INTEL_FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION 0x1

    // firmware functions (matches windows API, but added separate name in case this is used in a Windows API that
    // doesn't define these
#    define INTEL_FIRMWARE_FUNCTION_GET_INFO 0x01
#    define INTEL_FIRMWARE_FUNCTION_DOWNLOAD 0x02
#    define INTEL_FIRMWARE_FUNCTION_ACTIVATE 0x03

//
// The request is for Controller if this flag is set. Otherwise, it's for Device/Unit.
//
#    define INTEL_FIRMWARE_REQUEST_FLAG_CONTROLLER 0x00000001

//
// Indicate that current FW image segment is the last one.
//
#    define INTEL_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT 0x00000002

//
// Indicate that current FW image segment is the first one.
//
#    define INTEL_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT 0x00000004

//
// Indicate that the existing firmware in slot should be activated.
// This flag is only valid for fimrware_activate request. It's ignored for other requests.
//
#    define INTEL_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE 0x80000000

    typedef struct s_INTEL_FIRMWARE_REQUEST_BLOCK
    {
        ULONG Version;  // FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION
        ULONG Size;     // Size of the data structure.
        ULONG Function; // Function code
        ULONG Flags;

        ULONG
        DataBufferOffset; // the offset is from the beginning of buffer. e.g. from beginning of SRB_IO_CONTROL. The
                          // value should be multiple of sizeof(PVOID); Value 0 means that there is no data buffer.
        ULONG DataBufferLength; // length of the buffer
    } INTEL_FIRMWARE_REQUEST_BLOCK, *PINTEL_FIRMWARE_REQUEST_BLOCK;

    typedef struct s_RAID_FIRMWARE_REQUEST_BLOCK
    {
        UCHAR                        Version;
        UCHAR                        PathId;
        UCHAR                        TargetId;
        UCHAR                        Lun;
        INTEL_FIRMWARE_REQUEST_BLOCK FwRequestBlock;
    } RAID_FIRMWARE_REQUEST_BLOCK;

    DISABLE_WARNING_ZERO_LENGTH_ARRAY
    typedef struct s_IOCTL_RAID_FIRMWARE_BUFFER
    {
        SRB_IO_CONTROL              Header;
        RAID_FIRMWARE_REQUEST_BLOCK Request;
        UCHAR                       ioctlBuffer[0];
    } IOCTL_RAID_FIRMWARE_BUFFER;
    RESTORE_WARNING_ZERO_LENGTH_ARRAY

    // The RAID FW ioctl can send only STORAGE_FIRMWARE_INFO_V2, STORAGE_FIRMWARE_DOWNLOAD_V2, STORAGE_FIRMWARE_ACTIVATE
    // These are redefined here so that an up to date Windows API is not required to compile this code.

#    define INTEL_STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2    0x2

#    define INTEL_STORAGE_FIRMWARE_INFO_INVALID_SLOT            0xFF

#    define INTEL_STORAGE_FIRMWARE_SLOT_INFO_V2_REVISION_LENGTH 16

    typedef struct s_INTEL_STORAGE_FIRMWARE_SLOT_INFO_V2
    {

        UCHAR   SlotNumber;
        BOOLEAN ReadOnly;
        UCHAR   Reserved[6];

        UCHAR Revision[INTEL_STORAGE_FIRMWARE_SLOT_INFO_V2_REVISION_LENGTH];

    } INTEL_STORAGE_FIRMWARE_SLOT_INFO_V2, *PINTEL_STORAGE_FIRMWARE_SLOT_INFO_V2;

    DISABLE_WARNING_ZERO_LENGTH_ARRAY
    typedef struct s_INTEL_STORAGE_FIRMWARE_INFO_V2
    {

        ULONG Version; // INTEL_STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2
        ULONG Size;    // sizeof(INTEL_STORAGE_FIRMWARE_INFO_V2)

        BOOLEAN UpgradeSupport;
        UCHAR   SlotCount;
        UCHAR   ActiveSlot;
        UCHAR   PendingActivateSlot;

        BOOLEAN FirmwareShared; // The firmware applies to both device and adapter. For example: PCIe SSD.
        UCHAR   Reserved[3];

        ULONG
        ImagePayloadAlignment;     // Number of bytes. Max: PAGE_SIZE. The transfer size should be multiple of this unit
                                   // size. Some protocol requires at least sector size. 0 means the value is not valid.
        ULONG ImagePayloadMaxSize; // for a single command.

        INTEL_STORAGE_FIRMWARE_SLOT_INFO_V2 Slot[0];

    } INTEL_STORAGE_FIRMWARE_INFO_V2, *PINTEL_STORAGE_FIRMWARE_INFO_V2;

#    define INTEL_STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2 0x2

    typedef struct s_INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2
    {

        ULONG Version; // INTEL_STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2
        ULONG Size;    // sizeof(INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2)

        ULONGLONG Offset;     // image file offset, should be aligned to value of "ImagePayloadAlignment" from
                              // STORAGE_FIRMWARE_INFO.
        ULONGLONG BufferSize; // should be multiple of value of "ImagePayloadAlignment" from STORAGE_FIRMWARE_INFO

        UCHAR Slot; // Must be set to INVALID SLOT definition above according to Intel documentation
        UCHAR Reserved[3];
        ULONG ImageSize;

        UCHAR ImageBuffer[0]; // firmware image file.

    } INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2, *PINTEL_STORAGE_FIRMWARE_DOWNLOAD_V2;

    RESTORE_WARNING_ZERO_LENGTH_ARRAY

#    define INTEL_STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION 0x1

    typedef struct s_INTEL_STORAGE_FIRMWARE_ACTIVATE
    {

        ULONG Version;
        ULONG Size;

        UCHAR SlotToActivate;
        UCHAR Reserved0[3];

    } INTEL_STORAGE_FIRMWARE_ACTIVATE, *PINTEL_STORAGE_FIRMWARE_ACTIVATE;

    // Error codes for firmware updates - compatible with WinAPI
#    define INTEL_FIRMWARE_STATUS_SUCCESS                  0x0
#    define INTEL_FIRMWARE_STATUS_ERROR                    0x1
#    define INTEL_FIRMWARE_STATUS_ILLEGAL_REQUEST          0x2
#    define INTEL_FIRMWARE_STATUS_INVALID_PARAMETER        0x3
#    define INTEL_FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG     0x4
#    define INTEL_FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL  0x5
#    define INTEL_FIRMWARE_STATUS_INVALID_SLOT             0x6
#    define INTEL_FIRMWARE_STATUS_INVALID_IMAGE            0x7
#    define INTEL_FIRMWARE_STATUS_CONTROLLER_ERROR         0x10
#    define INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED     0x20
#    define INTEL_FIRMWARE_STATUS_DEVICE_ERROR             0x40
#    define INTEL_FIRMWARE_STATUS_INTERFACE_CRC_ERROR      0x80
#    define INTEL_FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR 0x81
#    define INTEL_FIRMWARE_STATUS_MEDIA_CHANGE             0x82
#    define INTEL_FIRMWARE_STATUS_ID_NOT_FOUND             0x83
#    define INTEL_FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST     0x84
#    define INTEL_FIRMWARE_STATUS_COMMAND_ABORT            0x85
#    define INTEL_FIRMWARE_STATUS_END_OF_MEDIA             0x86
#    define INTEL_FIRMWARE_STATUS_ILLEGAL_LENGTH           0x87

    /////////////////////////////////////End of FWDL API//////////////////////////////////////////

    // AER API is for setting/receiving asynchronous notifications on NVMe devices. Available starting in RST 17.5
    // RAID configured devices only. Will not work with devices setup as separate non-RAID passthrough devices according
    // to Intel doc. Use the event mask to set and clear notifications based on bits being set to 1 or 0. This IOCTL
    // should go to a SCSI Port, not a specific device. Use CSMI CC_CSMI_SAS_GET_DRIVER_INFO to check if a port is
    // controlled by the Intel RST driver before using this IOCTL Use the NVMe passthrough IOCTL to receive Admin
    // Identify data to check if the device on the port is an NVMe device, and then this AER IOCTL can also be used.
    // Event names should be unique (Use something like a GUID). This is used to help the applicatio track events it
    // requests and separate them from other events Keep the event handle after creating the event handle with
    // CreateEvent(). This can be used for notifications when an even completes on a given SCSI port, which can then be
    // read for the data when the event occurs.

#    define INTELNVM_AER_SIGNATURE    "IntelNvm "
#    define INTEL_NVME_REGISTER_AER   CTL_CODE(0xF000, 0xC00, METHOD_BUFFERED, FILE_ANY_ACCESS)
#    define INTEL_NVME_GET_AER_DATA   CTL_CODE(0xF000, 0xE00, METHOD_BUFFERED, FILE_ANY_ACCESS)

#    define AEN_MAX_EVENT_NAME_LENGTH 32
#    define NVME_GET_AEN_DATA_MAX_COMPLETIONS                                                                          \
        10 // Up to 10 events can be stored, and must call GET DATA method multiple times to read each one.

    // Defined events (Only 2 so far according to Intel documentation). These can be OR'd together since the event mask
    // is a bit field
#    define AEN_EVENT_ERROR_INFO_LOG_PAGE 1
#    define AEN_EVENT_HEALTH_LOG_WARNING  2

    typedef struct s_RAIDPORT_REGISTER_SHARED_EVENT
    {
        CHAR      EventName[AEN_MAX_EVENT_NAME_LENGTH];
        UCHAR     Reserved;
        ULONGLONG EventMask;
    } RAIDPORT_REGISTER_SHARED_EVENT;

    typedef struct s_NVME_IOCTL_REGISTER_AER
    {
        SRB_IO_CONTROL                 Header;
        RAIDPORT_REGISTER_SHARED_EVENT EventData;
    } NVME_IOCTL_REGISTER_AER;

    typedef struct s_ADMIN_ASYNCHRONOUS_EVENT_COMPLETION_DW0
    {
        union
        {
            struct
            {
                ULONG AsynchronousEventType : 3;
                ULONG Reserved1 : 5;
                ULONG AsynchronousEventInformation : 8;
                ULONG AssociatedLogPage : 8;
                ULONG Reserved2 : 8;
            };
            ULONG Raw;
        };

    } ADMIN_ASYNCHRONOUS_EVENT_COMPLETION_DW0;

    typedef struct s_NVME_AER_DATA
    {
        UCHAR                                   EventName[AEN_MAX_EVENT_NAME_LENGTH];
        UCHAR                                   Reserved;
        ADMIN_ASYNCHRONOUS_EVENT_COMPLETION_DW0 Completions[NVME_GET_AEN_DATA_MAX_COMPLETIONS];
        ULONG                                   CompletionsCount;
    } NVME_AER_DATA;

    typedef struct s_NVME_IOCTL_GET_AER_DATA
    {
        SRB_IO_CONTROL Header;
        NVME_AER_DATA  Data;
    } NVME_IOCTL_GET_AER_DATA;

    /////////////////////////////////////////////////End of AER IOCTL/////////////////////////////////////////////////

    // Intel device location API (RST driver starting 18.0)
    // This should be used alongside CSMI CC_CSMI_SAS_GET_DRIVER_INFO, CC_CSMI_SAS_GET_RAID_INFO,
    // CC_CSMI_SAS_GET_RAID_CONFIG

#    define INTEL_RMP_SIGNATURE "IntelRmp"
#    define INTEL_REMAPPORT_GET_DEVICE_LOCATION                                                                        \
        0x80000D05 // CTL_CODE(0x8000, 0x341, METHOD_IN_DIRECT, FILE_ANY_ACCESS);

#    pragma pack(push, remapport_ioctl, 1)
    typedef struct s_REMAPPORT_LOCATION_PCI_BDF
    {
        UCHAR Bus;
        UCHAR Device : 5;
        UCHAR Function : 3;
    } REMAPPORT_LOCATION_PCI_BDF;

    typedef enum eREMAPPORT_LOCATION_TYPE
    {
        RemapPortLocationTypeSata    = 0,
        RemapPortLocationTypeCR      = 1,
        RemapPortLocationTypeSwRemap = 2,
        RemapPortLocationTypeVmd     = 3
    } REMAPPORT_LOCATION_TYPE;

    typedef struct s_REMAPPORT_LOCATION_SATA
    {
        UCHAR PortNumber;
    } REMAPPORT_LOCATION_SATA;

    typedef struct s_REMAPPORT_LOCATION_CR
    {
        UCHAR CycleRouterNumber;
    } REMAPPORT_LOCATION_CR;

    typedef struct s_REMAPPORT_LOCATION_SW_REMAP
    {
        REMAPPORT_LOCATION_PCI_BDF PciAddress;
    } REMAPPORT_LOCATION_SW_REMAP;

    typedef struct s_REMAPPORT_LOCATION_VMD
    {
        USHORT SocketNumber;
        USHORT ControllerNumber;
        // Device's root port Bus/Device/Function
        REMAPPORT_LOCATION_PCI_BDF RootPortAddress;
        // Physical slot number from SLCAP.PSN field in
        // device root port's PCI Express Capability
        USHORT PhysicalSlotNumber;
    } REMAPPORT_LOCATION_VMD;

#    define REMAPPORT_IOCTL_GET_DEVICE_LOCATION_VERSION 1

    enum REMAPPORT_GET_DEVICE_LOCATION_STATUS
    {
        GetLocationSuccess        = 1,
        ErrorNoPciDevice          = 2,
        ErrorNoBridgeDevice       = 3,
        ErrorNoBridgeCapabilities = 4,
    };

    typedef struct s_REMAPPORT_IOCTL_GET_DEVICE_LOCATION
    {
        SRB_IO_CONTROL          Header;
        UCHAR                   Version;
        UCHAR                   Size;
        UCHAR                   PathId;
        UCHAR                   TargetId;
        UCHAR                   Lun;
        REMAPPORT_LOCATION_TYPE LocationType;
        union
        {
            REMAPPORT_LOCATION_SATA     Sata;
            REMAPPORT_LOCATION_CR       CR;
            REMAPPORT_LOCATION_SW_REMAP SwRemap;
            REMAPPORT_LOCATION_VMD      Vmd;
        } Location;
    } REMAPPORT_IOCTL_GET_DEVICE_LOCATION;
#    pragma pack(pop, remapport_ioctl)

////////////////////////////////////////////////////End of Remap Port IOCTL stuff (device
/// location)///////////////////////////////////////////////////////////

// SRB Status matches Windows, redefined here to reduce includes, like the FWDL structures and flags were redefined (see
// srb.h)
#    define INTEL_SRB_STATUS_PENDING                0x00
#    define INTEL_SRB_STATUS_SUCCESS                0x01
#    define INTEL_SRB_STATUS_ABORTED                0x02
#    define INTEL_SRB_STATUS_ABORT_FAILED           0x03
#    define INTEL_SRB_STATUS_ERROR                  0x04
#    define INTEL_SRB_STATUS_BUSY                   0x05
#    define INTEL_SRB_STATUS_INVALID_REQUEST        0x06
#    define INTEL_SRB_STATUS_INVALID_PATH_ID        0x07
#    define INTEL_SRB_STATUS_NO_DEVICE              0x08
#    define INTEL_SRB_STATUS_TIMEOUT                0x09
#    define INTEL_SRB_STATUS_SELECTION_TIMEOUT      0x0A
#    define INTEL_SRB_STATUS_COMMAND_TIMEOUT        0x0B
#    define INTEL_SRB_STATUS_MESSAGE_REJECTED       0x0D
#    define INTEL_SRB_STATUS_BUS_RESET              0x0E
#    define INTEL_SRB_STATUS_PARITY_ERROR           0x0F
#    define INTEL_SRB_STATUS_REQUEST_SENSE_FAILED   0x10
#    define INTEL_SRB_STATUS_NO_HBA                 0x11
#    define INTEL_SRB_STATUS_DATA_OVERRUN           0x12
#    define INTEL_SRB_STATUS_UNEXPECTED_BUS_FREE    0x13
#    define INTEL_SRB_STATUS_PHASE_SEQUENCE_FAILURE 0x14
#    define INTEL_SRB_STATUS_BAD_SRB_BLOCK_LENGTH   0x15
#    define INTEL_SRB_STATUS_REQUEST_FLUSHED        0x16
#    define INTEL_SRB_STATUS_INVALID_LUN            0x20
#    define INTEL_SRB_STATUS_INVALID_TARGET_ID      0x21
#    define INTEL_SRB_STATUS_BAD_FUNCTION           0x22
#    define INTEL_SRB_STATUS_ERROR_RECOVERY         0x23
#    define INTEL_SRB_STATUS_NOT_POWERED            0x24
#    define INTEL_SRB_STATUS_LINK_DOWN              0x25
#    define INTEL_SRB_STATUS_INSUFFICIENT_RESOURCES 0x26
#    define INTEL_SRB_STATUS_THROTTLED_REQUEST      0x27
#    define INTEL_SRB_STATUS_INVALID_PARAMETER      0x28

// TODO: Are these the SRB status's for firmware, similar to the miniport in Windows to use when doing firmware updates
// instead???-TJE
#    define INTEL_FIRMWARE_STATUS_SUCCESS                  0x0
#    define INTEL_FIRMWARE_STATUS_ERROR                    0x1
#    define INTEL_FIRMWARE_STATUS_ILLEGAL_REQUEST          0x2
#    define INTEL_FIRMWARE_STATUS_INVALID_PARAMETER        0x3
#    define INTEL_FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG     0x4
#    define INTEL_FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL  0x5
#    define INTEL_FIRMWARE_STATUS_INVALID_SLOT             0x6
#    define INTEL_FIRMWARE_STATUS_INVALID_IMAGE            0x7
#    define INTEL_FIRMWARE_STATUS_CONTROLLER_ERROR         0x10
#    define INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED     0x20
#    define INTEL_FIRMWARE_STATUS_DEVICE_ERROR             0x40
#    define INTEL_FIRMWARE_STATUS_INTERFACE_CRC_ERROR      0x80
#    define INTEL_FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR 0x81
#    define INTEL_FIRMWARE_STATUS_MEDIA_CHANGE             0x82
#    define INTEL_FIRMWARE_STATUS_ID_NOT_FOUND             0x83
#    define INTEL_FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST     0x84
#    define INTEL_FIRMWARE_STATUS_COMMAND_ABORT            0x85
#    define INTEL_FIRMWARE_STATUS_END_OF_MEDIA             0x86
#    define INTEL_FIRMWARE_STATUS_ILLEGAL_LENGTH           0x87

#    if defined(__cplusplus)
}
#    endif

#endif //_WIN32
