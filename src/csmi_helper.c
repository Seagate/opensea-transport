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
#if 1 // defined(ENABLE_CSMI)

#    include <assert.h>
#    include <fcntl.h>
#    include <stddef.h> // offsetof
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
#    include <sys/types.h>
#    if defined(_WIN32)
#        include "intel_rst_helper.h"
#        include "predef_env_detect.h"
#        include "windows_version_detect.h" //for WinPE check
#        include <tchar.h>
DISABLE_WARNING_4255
#        include <windows.h>
RESTORE_WARNING_4255
#    else
#        include <sys/ioctl.h>
#        include <unistd.h>
#    endif

#    include "bit_manip.h"
#    include "code_attributes.h"
#    include "error_translation.h"
#    include "io_utils.h"
#    include "math_utils.h"
#    include "memory_safety.h"
#    include "precision_timer.h"
#    include "string_utils.h"
#    include "type_conversion.h"

#    include "ata_helper_func.h"
#    include "cmds.h"
#    include "csmi_helper.h"
#    include "csmi_helper_func.h"
#    include "sat_helper_func.h"
#    include "sata_helper_func.h"
#    include "sata_types.h"
#    include "scsi_helper_func.h"

#    if defined(_DEBUG) && !defined(CSMI_DEBUG)
#        define CSMI_DEBUG
#    endif

extern bool validate_Device_Struct(versionBlock);

// functions to assist freeing csmi structures easily/safely
static M_INLINE void safe_free_csmi_raid_config(CSMI_SAS_RAID_CONFIG_BUFFER** raidconfig)
{
    safe_free_core(M_REINTERPRET_CAST(void**, raidconfig));
}

#    if defined(_WIN32)
void print_Last_Error(DWORD lastError);
#    else
void print_Last_Error(int lastError);
#    endif

static void print_IOCTL_Return_Code(uint32_t returnCode)
{
    printf("IOCTL Status: ");
    switch (returnCode)
    {
    case CSMI_SAS_STATUS_SUCCESS:
        printf("CSMI SAS STATUS SUCCESS\n");
        break;
    case CSMI_SAS_STATUS_FAILED:
        printf("CSMI SAS STATUS FAILED\n");
        break;
    case CSMI_SAS_STATUS_BAD_CNTL_CODE:
        printf("CSMI SAS BAD CNTL CODE\n");
        break;
    case CSMI_SAS_STATUS_INVALID_PARAMETER:
        printf("CSMI SAS INVALID PARAMETER\n");
        break;
    case CSMI_SAS_STATUS_WRITE_ATTEMPTED:
        printf("CSMI SAS WRITE ATTEMPTED\n");
        break;
    case CSMI_SAS_RAID_SET_OUT_OF_RANGE:
        printf("CSMI SAS RAID SET OUT OF RANGE\n");
        break;
    case CSMI_SAS_RAID_SET_BUFFER_TOO_SMALL:
        printf("CSMI SAS RAID SET BUFFER TOO SMALL\n");
        break;
    case CSMI_SAS_RAID_SET_DATA_CHANGED:
        printf("CSMI SAS RAID SET DATA CHANGED\n");
        break;
    case CSMI_SAS_PHY_INFO_NOT_CHANGEABLE:
        printf("CSMI SAS PHY INFO NOT CHANGEABLE\n");
        break;
    case CSMI_SAS_LINK_RATE_OUT_OF_RANGE:
        printf("CSMI SAS LINK RATE OUT OF RANGE\n");
        break;
    case CSMI_SAS_PHY_DOES_NOT_EXIST:
        printf("CSMI SAS PHY DOES NOT EXIST\n");
        break;
    case CSMI_SAS_PHY_DOES_NOT_MATCH_PORT:
        printf("CSMI SAS PHY DOES NOT MATCH PORT\n");
        break;
    case CSMI_SAS_PHY_CANNOT_BE_SELECTED:
        printf("CSMI SAS PHY CANNOT BE SELECTED\n");
        break;
    case CSMI_SAS_SELECT_PHY_OR_PORT:
        printf("CSMI SAS SELECT PHY OR PORT\n");
        break;
    case CSMI_SAS_PORT_DOES_NOT_EXIST:
        printf("CSMI SAS PORT DOES NOT EXIST\n");
        break;
    case CSMI_SAS_PORT_CANNOT_BE_SELECTED:
        printf("CSMI SAS PORT CANNOT BE SELECTED\n");
        break;
    case CSMI_SAS_CONNECTION_FAILED:
        printf("CSMI SAS CONNECTION FAILED\n");
        break;
    case CSMI_SAS_NO_SATA_DEVICE:
        printf("CSMI SAS NO SATA DEVICE\n");
        break;
    case CSMI_SAS_NO_SATA_SIGNATURE:
        printf("CSMI SAS NO SATA SIGNATURE\n");
        break;
    case CSMI_SAS_SCSI_EMULATION:
        printf("CSMI SAS SCSI EMULATION\n");
        break;
    case CSMI_SAS_NOT_AN_END_DEVICE:
        printf("CSMI SAS NOT AN END DEVICE\n");
        break;
    case CSMI_SAS_NO_SCSI_ADDRESS:
        printf("CSMI SAS NO SCSI ADDRESS\n");
        break;
    case CSMI_SAS_NO_DEVICE_ADDRESS:
        printf("CSMI SAS NO DEVICE ADDRESS\n");
        break;
    default:
        printf("Unknown error code %" PRIu32 "\n", returnCode);
        break;
    }
}

#    if defined(_WIN32)
void print_Last_Error(DWORD lastError)
{
    print_Windows_Error_To_Screen(lastError);
}
#    else  //_WIN32
void print_Last_Error(int lastError)
{
    print_Errno_To_Screen(lastError);
}
#    endif //_WIN32

static eReturnValues csmi_Return_To_OpenSea_Result(uint32_t returnCode)
{
    eReturnValues ret = SUCCESS;
    switch (returnCode)
    {
    case CSMI_SAS_STATUS_SUCCESS:
    case CSMI_SAS_RAID_SET_DATA_CHANGED: // not sure if this is just informative or an error
        ret = SUCCESS;
        break;
    case CSMI_SAS_STATUS_BAD_CNTL_CODE:
    case CSMI_SAS_STATUS_INVALID_PARAMETER:
    case CSMI_SAS_PHY_INFO_NOT_CHANGEABLE:
    case CSMI_SAS_PORT_CANNOT_BE_SELECTED:
    case CSMI_SAS_NO_SATA_SIGNATURE:
    case CSMI_SAS_SCSI_EMULATION:
    case CSMI_SAS_NO_SCSI_ADDRESS:
    case CSMI_SAS_NO_DEVICE_ADDRESS:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_STATUS_WRITE_ATTEMPTED:
        ret = PERMISSION_DENIED;
        break;
    case CSMI_SAS_STATUS_FAILED:
    case CSMI_SAS_RAID_SET_OUT_OF_RANGE:
    case CSMI_SAS_RAID_SET_BUFFER_TOO_SMALL:
    case CSMI_SAS_LINK_RATE_OUT_OF_RANGE:
    case CSMI_SAS_PHY_DOES_NOT_EXIST:
    case CSMI_SAS_PHY_DOES_NOT_MATCH_PORT:
    case CSMI_SAS_PHY_CANNOT_BE_SELECTED:
    case CSMI_SAS_SELECT_PHY_OR_PORT:
    case CSMI_SAS_PORT_DOES_NOT_EXIST:
    case CSMI_SAS_CONNECTION_FAILED:
    case CSMI_SAS_NO_SATA_DEVICE:
    case CSMI_SAS_NOT_AN_END_DEVICE:
    default:
        ret = FAILURE;
        break;
    }
    return ret;
}

typedef struct s_csmiIOin
{
    CSMI_HANDLE deviceHandle;
    void*       ioctlBuffer;
    uint32_t    ioctlCode; // CSMI IOCTL code. Linux needs this, Windows doesn't since it's in the header for Windows.
    uint32_t    ioctlBufferSize;
    char        ioctlSignature[8]; // Signature of the IOCTL to send
    uint32_t    timeoutInSeconds;
    uint32_t    dataLength; // The length of all the data AFTER the ioctl header. This helps track how much to
                         // send/receive. The structure trying to read or write sizeof(CSMI struct) or possibly larger
                         // for those that have variable length data
    uint32_t controllerNumber; // For Linux drivers, we need to specify the controller number since the drivers may
                               // manage more than a single controller at a time. This will be ignored in Linux
    eVerbosityLevels csmiVerbosity;
    uint16_t         ioctlDirection; // Is this sending data (set) or receiving data (get). Needed for Linux
} csmiIOin, *ptrCsmiIOin;

typedef struct s_csmiIOout
{
    unsigned long bytesReturned; // Windows only and returned because it may be needed to fully process the result. Will
                                 // be 0 for other OSs
    int         sysIoctlReturn;  // to save return from calling DeviceIoControl or Ioctl functions.
    uint32_t*   lastError;       // pointer to store last error in. Optional
    seatimer_t* ioctlTimer;      // pointer to a timer to start and stop if the IOCTL needs timing.
} csmiIOout, *ptrCsmiIOout;

// static because this should be an internal function to be reused below for getting the other data
static eReturnValues issue_CSMI_IO(ptrCsmiIOin csmiIoInParams, ptrCsmiIOout csmiIoOutParams)
{
    eReturnValues ret              = SUCCESS;
    int           localIoctlReturn = 0; // This is OK in Windows because BOOL is a typedef for int
    seatimer_t*   timer            = M_NULLPTR;
    bool          localTimer       = false;
#    if defined(_WIN32)
    OVERLAPPED overlappedStruct;
    DWORD      lastError = DWORD_C(0);
#    else
    int lastError = 0;
#    endif
    PIOCTL_HEADER ioctlHeader =
        csmiIoInParams->ioctlBuffer; // ioctl buffer should point to the beginning where the header will be.
    if (csmiIoInParams == M_NULLPTR || csmiIoOutParams == M_NULLPTR || ioctlHeader == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    timer = csmiIoOutParams->ioctlTimer;
    if (timer == M_NULLPTR)
    {
        timer      = M_REINTERPRET_CAST(seatimer_t*, safe_calloc(1, sizeof(seatimer_t)));
        localTimer = true;
    }

    // setup the IOCTL header for each OS
    // memset to zero first
    safe_memset(ioctlHeader, sizeof(IOCTL_HEADER), 0, sizeof(IOCTL_HEADER));
    // fill in common things
    ioctlHeader->Timeout    = csmiIoInParams->timeoutInSeconds;
    ioctlHeader->ReturnCode = CSMI_SAS_STATUS_SUCCESS;
    ioctlHeader->Length     = csmiIoInParams->dataLength;
    if (VERBOSITY_COMMAND_NAMES <= csmiIoInParams->csmiVerbosity)
    {
        printf("\n---Sending CSMI IO---\n");
    }
#    if defined(_WIN32)
    // finish OS specific IOHEADER setup
    ioctlHeader->ControlCode  = csmiIoInParams->ioctlCode;
    ioctlHeader->HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(ioctlHeader->Signature, 8, csmiIoInParams->ioctlSignature, 8);
    // overlapped support
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    if (!overlappedStruct.hEvent)
    {
        if (localTimer)
        {
            safe_free_seatimer(&timer);
        }
        return MEMORY_FAILURE;
    }
    // issue the IO
    start_Timer(timer);
    localIoctlReturn =
        DeviceIoControl(csmiIoInParams->deviceHandle, IOCTL_SCSI_MINIPORT, csmiIoInParams->ioctlBuffer,
                        csmiIoInParams->ioctlBufferSize, csmiIoInParams->ioctlBuffer, csmiIoInParams->ioctlBufferSize,
                        &csmiIoOutParams->bytesReturned, &overlappedStruct);
    if (ERROR_IO_PENDING ==
        GetLastError()) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                        // flag, everything will work like old synchronous code.-TJE
    {
        localIoctlReturn =
            GetOverlappedResult(csmiIoInParams->deviceHandle, &overlappedStruct, &csmiIoOutParams->bytesReturned, TRUE);
    }
    else if (GetLastError() != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(timer);
    CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = M_NULLPTR;
    lastError               = GetLastError();
    if (csmiIoOutParams->lastError)
    {
        *csmiIoOutParams->lastError = lastError;
    }
    // print_Windows_Error_To_Screen(GetLastError());
#    else  // Linux or other 'nix systems
    // finish OS specific IOHEADER setup
    ioctlHeader->IOControllerNumber = csmiIoInParams->controllerNumber;
    ioctlHeader->Direction          = csmiIoInParams->ioctlDirection;
    // issue the IO
    start_Timer(timer);
    DISABLE_WARNING_SIGN_CONVERSION;
    localIoctlReturn = ioctl(csmiIoInParams->deviceHandle, csmiIoInParams->ioctlCode, csmiIoInParams->ioctlBuffer);
    RESTORE_WARNING_SIGN_CONVERSION;
    stop_Timer(timer);
    lastError = errno;
    if (csmiIoOutParams->lastError)
    {
        *csmiIoOutParams->lastError = C_CAST(unsigned int, lastError);
    }
#    endif //_WIN32
    if (VERBOSITY_COMMAND_NAMES <= csmiIoInParams->csmiVerbosity)
    {
        printf("\tCSMI IO results:\n");
        printf("\t\tIO returned: %d\n", localIoctlReturn);
        printf("\t\tLast error meaning: ");
        print_Last_Error(lastError);
        printf("\t\tCSMI Error Code: ");
        print_IOCTL_Return_Code(ioctlHeader->ReturnCode);
        printf("\t\tCompletion time: ");
        if (timer)
        {
            print_Command_Time(get_Nano_Seconds(*timer));
        }
        else
        {
            printf("Error getting command time\n");
        }
        printf("\n");
    }
    csmiIoOutParams->sysIoctlReturn = localIoctlReturn;
    if (localTimer)
    {
        safe_free_seatimer(&timer);
    }
    return ret;
}

/*
Examples from different drivers/configurations:
====CSMI Driver Info====
    Driver Name: rcraid
    Description: AMD-RAID Controller [storport] Device Driver
    Driver Version: 9.3.0.38
    CSMI Version: 1.7

====CSMI Driver Info====
    Driver Name: iaStorE
    Description: Intel Virtual RAID on CPU
    Driver Version: 7.7.0.1260
    CSMI Version: 0.76

====CSMI Driver Info====
    Driver Name: iaStorAC
    Description: Intel(R) Rapid Storage Technology
    Driver Version: 17.8.0.1065
    CSMI Version: 0.81

====CSMI Driver Info====
    Driver Name: iaStorAC
    Description: Intel(R) Rapid Storage Technology
    Driver Version: 17.9.2.1013
    CSMI Version: 0.81

====CSMI Driver Info====
    Driver Name: HpCISSS3.sys
    Description: Smart Array SAS/SATA Controller Storport Driver
    Driver Version: 63.12.0.64
    CSMI Version: 0.82

====CSMI Driver Info====
    Driver Name: arcsas
    Description: Adaptec RAID Storport Driver
    Driver Version: 7.5.59005.0
    CSMI Version: 0.82

====CSMI Driver Info====
    Driver Name: iaStorVD
    Description: Intel(R) Rapid Storage Technology
    Driver Version: 19.2.0.1003
    CSMI Version: 0.81

*/
static eKnownCSMIDriver get_Known_CSMI_Driver_Type(PCSMI_SAS_DRIVER_INFO driverInfo)
{
    eKnownCSMIDriver csmiDriverType = CSMI_DRIVER_UNKNOWN;
    if (driverInfo)
    {
        // TODO: May need to track specific driver versions if capabilities vary significantly at any point-TJE
        if (strstr(C_CAST(const char*, driverInfo->szName), "iaStorAC"))
        {
            // classic intel rapid storage technology
            csmiDriverType = CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY;
        }
        else if (strstr(C_CAST(const char*, driverInfo->szName), "iaStorE"))
        {
            // intel virtual raid on chip (VROC)
            csmiDriverType = CSMI_DRIVER_INTEL_VROC;
        }
        else if (strstr(C_CAST(const char*, driverInfo->szName), "iaStorVD"))
        {
            // intel rapid storage technology VD driver. No idea what VD means or how it differs yet.
            csmiDriverType = CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY_VD;
        }
        // Check for a generic Intel match LAST. Check all other intel driver variants above this condition to have a
        // better intel driver classification!
        else if (strstr(C_CAST(const char*, driverInfo->szName), "iaStor"))
        {
            // some kind of intel driver. Not sure all of the capabilities, but we are unable to further discover
            // anything else about it for now.
            csmiDriverType = CSMI_DRIVER_INTEL_GENERIC;
        }
        else if (strstr(C_CAST(const char*, driverInfo->szName), "rcraid"))
        {
            // amd's CSMI compatible RAID driver for SATA raid chipsets
            csmiDriverType = CSMI_DRIVER_AMD_RCRAID;
        }
        else if (strstr(C_CAST(const char*, driverInfo->szName), "HpCISSS3") ||
                 strstr(C_CAST(const char*, driverInfo->szName),
                        "HpCISSs3")) // need to check if final s can also be lowercase
        {
            csmiDriverType = CSMI_DRIVER_HPCISS;
        }
        else if (strstr(C_CAST(const char*, driverInfo->szName), "arcsas"))
        {
            csmiDriverType = CSMI_DRIVER_ARCSAS;
        }
        else if (strstr(C_CAST(const char*, driverInfo->szName), "HpSAMD"))
        {
            csmiDriverType = CSMI_DRIVER_HPSAMD;
        }
        // As more driver names found, check them here.
    }
#    if defined(_DEBUG)
    printf("Known driver = %d\n", csmiDriverType);
#    endif //_DEBUG
    return csmiDriverType;
}

static bool is_Intel_Driver(eKnownCSMIDriver knownDriver)
{
    bool isIntel = false;
    switch (knownDriver)
    {
    case CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY:
    case CSMI_DRIVER_INTEL_VROC:
    case CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY_VD:
    case CSMI_DRIVER_INTEL_GENERIC:
        isIntel = true;
        break;
    default:
        break;
    }
    return isIntel;
}

// More for debugging than anything else
static void print_CSMI_Driver_Info(PCSMI_SAS_DRIVER_INFO driverInfo)
{
    if (driverInfo)
    {
        printf("\n====CSMI Driver Info====\n");
        printf("\tDriver Name: %s\n", driverInfo->szName);
        printf("\tDescription: %s\n", driverInfo->szDescription);
        printf("\tDriver Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", driverInfo->usMajorRevision,
               driverInfo->usMinorRevision, driverInfo->usBuildRevision, driverInfo->usReleaseRevision);
        printf("\tCSMI Version: %" CPRIu16 ".%" CPRIu16 "\n", driverInfo->usCSMIMajorRevision,
               driverInfo->usCSMIMinorRevision);
        printf("\n");
    }
}

eReturnValues csmi_Get_Driver_Info(CSMI_HANDLE                  deviceHandle,
                                   uint32_t                     controllerNumber,
                                   PCSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer,
                                   eVerbosityLevels             verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(driverInfoBuffer, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER), 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = driverInfoBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_DRIVER_INFO_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_DRIVER_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_DRIVER_INFO;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_ALL_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_ALL_SIGNATURE, safe_strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Driver Info\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(driverInfoBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Driver_Info(&driverInfoBuffer->Information);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Driver Info\n", ret);
    }

    return ret;
}

static void print_CSMI_Controller_Configuration(PCSMI_SAS_CNTLR_CONFIG config)
{
    if (config)
    {
        printf("\n====CSMI Controller Configuration====\n");
        printf("\tBase IO Address: %08" CPRIX32 "h\n", config->uBaseIoAddress);
        printf("\tBase Memory Address: %08" CPRIX32 "%08" CPRIX32 "h\n", config->BaseMemoryAddress.uHighPart,
               config->BaseMemoryAddress.uLowPart);
        printf("\tBoard ID: %08" CPRIX32 "h\n", config->uBoardID);
        printf("\t\tVendor ID: %04" CPRIX16 "h\n", M_Word0(config->uBoardID));
        printf("\t\tSubsystem ID: %04" CPRIX16 "h\n", M_Word1(config->uBoardID));
        printf("\tSlot Number: ");
        if (SLOT_NUMBER_UNKNOWN == config->usSlotNumber)
        {
            printf("Unknown\n");
        }
        else
        {
            printf("%" CPRIu16 "\n", config->usSlotNumber);
        }
        printf("\tController Class: ");
        if (CSMI_SAS_CNTLR_CLASS_HBA == config->bControllerClass)
        {
            printf("HBA\n");
        }
        else
        {
            printf("Unknown - %" CPRIu8 "\n", config->bControllerClass);
        }
        printf("\tIO Bus Type: ");
        switch (config->bIoBusType)
        {
        case CSMI_SAS_BUS_TYPE_PCI:
            printf("PCI\n");
            break;
        case CSMI_SAS_BUS_TYPE_PCMCIA:
            printf("PCMCIA\n");
            break;
        default:
            printf("Unknown - %" CPRIu8 "\n", config->bIoBusType);
            break;
        }
        printf("\tBus Address\n");
        printf("\t\tBus Number: %" CPRIu8 "\n", config->BusAddress.PciAddress.bBusNumber);
        printf("\t\tDevice Number: %" CPRIu8 "\n", config->BusAddress.PciAddress.bDeviceNumber);
        printf("\t\tFunction Number: %" CPRIu8 "\n", config->BusAddress.PciAddress.bFunctionNumber);
        printf("\tSerial Number: %s\n", config->szSerialNumber);
        printf("\tController Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", config->usMajorRevision,
               config->usMinorRevision, config->usBuildRevision, config->usReleaseRevision);
        printf("\tBIOS Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", config->usBIOSMajorRevision,
               config->usBIOSMinorRevision, config->usBIOSBuildRevision, config->usBIOSReleaseRevision);
        printf("\tController Flags (%08" CPRIX32 "h):\n", config->uControllerFlags);
        if (config->uControllerFlags & CSMI_SAS_CNTLR_SAS_HBA)
        {
            printf("\t\tSAS HBA\n");
        }
        if (config->uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID)
        {
            printf("\t\tSAS RAID\n");
        }
        if (config->uControllerFlags & CSMI_SAS_CNTLR_SATA_HBA)
        {
            printf("\t\tSATA HBA\n");
        }
        if (config->uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID)
        {
            printf("\t\tSATA RAID\n");
        }
        if (config->uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY)
        {
            printf("\t\tSmart Array\n");
        }
        printf("\tRedundant Controller Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n",
               config->usRromMajorRevision, config->usRromMinorRevision, config->usRromBuildRevision,
               config->usRromReleaseRevision);
        printf("\tRedundant BIOS Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n",
               config->usRromBIOSMajorRevision, config->usRromBIOSMinorRevision, config->usRromBIOSBuildRevision,
               config->usRromBIOSReleaseRevision);
        printf("\n");
    }
}

eReturnValues csmi_Get_Controller_Configuration(CSMI_HANDLE                   deviceHandle,
                                                uint32_t                      controllerNumber,
                                                PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer,
                                                eVerbosityLevels              verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(ctrlConfigBuffer, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER), 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = ctrlConfigBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_CNTLR_CONFIG;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_ALL_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_ALL_SIGNATURE, safe_strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Controller Configuration\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(ctrlConfigBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Controller_Configuration(&ctrlConfigBuffer->Configuration);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Controller Configuration\n", ret);
    }

    return ret;
}

static void print_CSMI_Controller_Status(PCSMI_SAS_CNTLR_STATUS status)
{
    if (status)
    {
        printf("\n====CSMI Controller Status====\n");
        printf("\tStatus: ");
        switch (status->uStatus)
        {
        case CSMI_SAS_CNTLR_STATUS_GOOD:
            printf("Good\n");
            break;
        case CSMI_SAS_CNTLR_STATUS_FAILED:
            printf("Failed\n");
            break;
        case CSMI_SAS_CNTLR_STATUS_OFFLINE:
            printf("Offline\n");
            break;
        case CSMI_SAS_CNTLR_STATUS_POWEROFF:
            printf("Powered Off\n");
            break;
        default:
            printf("Unknown\n");
            break;
        }
        if (status->uStatus == CSMI_SAS_CNTLR_STATUS_OFFLINE)
        {
            printf("\tOffline Reason: ");
            switch (status->uOfflineReason)
            {
            case CSMI_SAS_OFFLINE_REASON_NO_REASON:
                printf("No Reason\n");
                break;
            case CSMI_SAS_OFFLINE_REASON_INITIALIZING:
                printf("Initializing\n");
                break;
            case CSMI_SAS_OFFLINE_REASON_BACKSIDE_BUS_DEGRADED:
                printf("Backside Bus Degraded\n");
                break;
            case CSMI_SAS_OFFLINE_REASON_BACKSIDE_BUS_FAILURE:
                printf("Backside Bus Failure\n");
                break;
            default:
                printf("Unknown\n");
                break;
            }
        }
    }
}

eReturnValues csmi_Get_Controller_Status(CSMI_HANDLE                   deviceHandle,
                                         uint32_t                      controllerNumber,
                                         PCSMI_SAS_CNTLR_STATUS_BUFFER ctrlStatusBuffer,
                                         eVerbosityLevels              verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(ctrlStatusBuffer, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER), 0, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = ctrlStatusBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_CNTLR_STATUS;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_ALL_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_ALL_SIGNATURE, safe_strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Controller Status\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(ctrlStatusBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Controller_Status(&ctrlStatusBuffer->Status);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Controller Status\n", ret);
    }

    return ret;
}

// NOTE: This function needs the firmwareBuffer to be allocated with additional length for the firmware to send to the
// controller. In order to make this simple, we will assume the caller has already copied the controller firmware to the
// buffer, but we still need the total length
eReturnValues csmi_Controller_Firmware_Download(CSMI_HANDLE                        deviceHandle,
                                                uint32_t                           controllerNumber,
                                                PCSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER firmwareBuffer,
                                                uint32_t                           firmwareBufferTotalLength,
                                                uint32_t                           downloadFlags,
                                                eVerbosityLevels                   verbosity,
                                                uint32_t                           timeoutSeconds)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(
        firmwareBuffer, firmwareBufferTotalLength, 0,
        sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER) -
            1); // Memsetting ONLY the header and flags section so that the firmware we are sending is left untouched.

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = firmwareBuffer;
    ioIn.ioctlBufferSize  = firmwareBufferTotalLength;
    ioIn.dataLength       = C_CAST(uint32_t, firmwareBufferTotalLength - sizeof(IOCTL_HEADER));
    ioIn.ioctlCode        = CC_CSMI_SAS_FIRMWARE_DOWNLOAD;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_WRITE;
    ioIn.timeoutInSeconds = timeoutSeconds;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_ALL_SIGNATURE, safe_strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    firmwareBuffer->Information.uDownloadFlags = downloadFlags;
    firmwareBuffer->Information.uBufferLength =
        C_CAST(uint32_t, firmwareBufferTotalLength - sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER)); //-1???

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Controller Firmware Download\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(firmwareBuffer->IoctlHeader.ReturnCode);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Controller Firmware Download\n", ret);
    }

    return ret;
}

static void print_CSMI_RAID_Info(PCSMI_SAS_RAID_INFO raidInfo)
{
    if (raidInfo)
    {
        printf("\n====CSMI RAID Info====\n");
        printf("\tNumber of RAID Sets: %" CPRIu32 "\n", raidInfo->uNumRaidSets);
        printf("\tMaximum # of drives per set: %" CPRIu32 "\n", raidInfo->uMaxDrivesPerSet);
        // Check if remaining bytes are zeros. This helps track differences between original CSMI spec and some drivers
        // that added onto it.
        if (!is_Empty(M_REINTERPRET_CAST(void*, raidInfo),
                      92)) // 92 is original reserved length from original documentation...can
                           // change to something else based on actual structure size if needed
        {
            printf("\tMaximum # of RAID Sets: %" CPRIu32 "\n", raidInfo->uMaxRaidSets);
            printf("\tMaximum # of RAID Types: %" CPRIu8 "\n", raidInfo->bMaxRaidTypes);
            printf("\tMinimum RAID Set Blocks: %" PRIu64 "\n",
                   M_DWordsTo8ByteValue(raidInfo->ulMinRaidSetBlocks.uHighPart, raidInfo->ulMinRaidSetBlocks.uLowPart));
            printf("\tMaximum RAID Set Blocks: %" PRIu64 "\n",
                   M_DWordsTo8ByteValue(raidInfo->ulMaxRaidSetBlocks.uHighPart, raidInfo->ulMaxRaidSetBlocks.uLowPart));
            printf("\tMaximum Physical Drives: %" CPRIu32 "\n", raidInfo->uMaxPhysicalDrives);
            printf("\tMaximum Extents: %" CPRIu32 "\n", raidInfo->uMaxExtents);
            printf("\tMaximum Modules: %" CPRIu32 "\n", raidInfo->uMaxModules);
            printf("\tMaximum Transformational Memory: %" CPRIu32 "\n", raidInfo->uMaxTransformationMemory);
            printf("\tChange Count: %" CPRIu32 "\n", raidInfo->uChangeCount);
            // Add another is_Empty here for 44 bytes if other things get added here.
        }
    }
}

// Sample RAID info outputs from some drivers:
// AMD rcraid: - 1 non-RAID drive and 1 RAID with 2 drives.
//====CSMI RAID Info====
//	Number of RAID Sets: 3
//	Maximum # of drives per set: 16
//	Maximum # of RAID Sets: 16
//	Maximum # of RAID Types: 14
//	Minimum RAID Set Blocks: 2048
//	Maximum RAID Set Blocks: 18446744073709551615
//	Maximum Physical Drives: 16
//	Maximum Extents: 1
//	Maximum Modules: 0
//	Maximum Transformational Memory: 0
//	Change Count: 0

// Intel iaStorE: - 1 raid with 2 drives
//====CSMI RAID Info====
//	Number of RAID Sets: 1
//	Maximum # of drives per set: 2

// ArcSAS - all "raids" are individual drives. RAID config does NOT work
//====CSMI RAID Info====
//	Number of RAID Sets: 23
//	Maximum # of drives per set: 128

eReturnValues csmi_Get_RAID_Info(CSMI_HANDLE                deviceHandle,
                                 uint32_t                   controllerNumber,
                                 PCSMI_SAS_RAID_INFO_BUFFER raidInfoBuffer,
                                 eVerbosityLevels           verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(raidInfoBuffer, sizeof(CSMI_SAS_RAID_INFO_BUFFER), 0, sizeof(CSMI_SAS_RAID_INFO_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = raidInfoBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_RAID_INFO_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_RAID_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_RAID_INFO;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_RAID_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_RAID_SIGNATURE, safe_strlen(CSMI_RAID_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get RAID Info\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(raidInfoBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_RAID_Info(&raidInfoBuffer->Information);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get RAID Info\n", ret);
    }

    return ret;
}

static void print_CSMI_RaidType(__u8 bRaidType)
{
    switch (bRaidType)
    {
    case CSMI_SAS_RAID_TYPE_NONE:
        printf("None\n");
        break;
    case CSMI_SAS_RAID_TYPE_0:
        printf("0\n");
        break;
    case CSMI_SAS_RAID_TYPE_1:
        printf("1\n");
        break;
    case CSMI_SAS_RAID_TYPE_10:
        printf("10\n");
        break;
    case CSMI_SAS_RAID_TYPE_5:
        printf("5\n");
        break;
    case CSMI_SAS_RAID_TYPE_15:
        printf("15\n");
        break;
    case CSMI_SAS_RAID_TYPE_6:
        printf("6\n");
        break;
    case CSMI_SAS_RAID_TYPE_50:
        printf("50\n");
        break;
    case CSMI_SAS_RAID_TYPE_VOLUME:
        printf("Volume\n");
        break;
    case CSMI_SAS_RAID_TYPE_1E:
        printf("1E\n");
        break;
    case CSMI_SAS_RAID_TYPE_OTHER:
        printf("Other\n");
        break;
    default:
        printf("Unknown\n");
        break;
    }
}

static void print_CSMI_RAID_Failure_Code(__u32 uFailureCode)
{
    switch (uFailureCode)
    {
    case CSMI_SAS_FAIL_CODE_OK:
        printf("No Error\n");
        break;
    case CSMI_SAS_FAIL_CODE_PARAMETER_INVALID:
        printf("Parameter Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_TRANSFORM_PRIORITY_INVALID:
        printf("Transform Priority Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_REBUILD_PRIORITY_INVALID:
        printf("Rebuild Priority Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_CACHE_RATIO_INVALID:
        printf("Cache Ratio Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_SURFACE_SCAN_INVALID:
        printf("Surface Scan Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_CLEAR_CONFIGURATION_INVALID:
        printf("Clear Configuration Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_ELEMENT_INDEX_INVALID:
        printf("Element Index Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_SUBELEMENT_INDEX_INVALID:
        printf("Subelement Index Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_EXTENT_INVALID:
        printf("Extent Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_BLOCK_COUNT_INVALID:
        printf("Block Count Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_DRIVE_INDEX_INVALID:
        printf("Drive Index Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_EXISTING_LUN_INVALID:
        printf("Existing LUN Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_RAID_TYPE_INVALID:
        printf("RAID Type Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_STRIPE_SIZE_INVALID:
        printf("Stripe Size Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_TRANSFORMATION_INVALID:
        printf("Transformation Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_CHANGE_COUNT_INVALID:
        printf("Change Count Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_ENUMERATION_TYPE_INVALID:
        printf("Enumeration Type Invalid\n");
        break;
    case CSMI_SAS_FAIL_CODE_EXCEEDED_RAID_SET_COUNT:
        printf("Exceeded RAID Set Count\n");
        break;
    case CSMI_SAS_FAIL_CODE_DUPLICATE_LUN:
        printf("Duplicate LUN\n");
        break;
    case CSMI_SAS_FAIL_CODE_WAIT_FOR_OPERATION:
        printf("Wait For Operation\n");
        break;
    default:
        printf("Unknown Failure Code: %" CPRIu32 "\n", uFailureCode);
        break;
    }
}

// TODO: Need to pass in CSMI version information
static void print_CSMI_RAID_Config(PCSMI_SAS_RAID_CONFIG config, uint32_t configLength)
{
    if (config)
    {
        printf("\n====CSMI RAID Configuration====\n");
        printf("\tRAID Set Index: %" CPRIu32 "\n", config->uRaidSetIndex);
        printf("\tCapacity (MB): %" CPRIu32 "\n", config->uCapacity);
        printf("\tStripe Size (KB): %" CPRIu32 "\n", config->uStripeSize);
        printf("\tRAID Type: ");
        print_CSMI_RaidType(config->bRaidType);
        printf("\tStatus: ");
        switch (config->bStatus)
        {
        case CSMI_SAS_RAID_SET_STATUS_OK:
            printf("OK\n");
            break;
        case CSMI_SAS_RAID_SET_STATUS_DEGRADED:
            printf("Degraded\n");
            printf("\tFailed Drive Index: %" CPRIu8 "\n", config->bInformation);
            break;
        case CSMI_SAS_RAID_SET_STATUS_REBUILDING:
            printf("Rebuilding - %" CPRIu8 "%%\n", config->bInformation);
            break;
        case CSMI_SAS_RAID_SET_STATUS_FAILED:
            printf("Failed - %" CPRIu8 "\n", config->bInformation);
            break;
        case CSMI_SAS_RAID_SET_STATUS_OFFLINE:
            printf("Offline\n");
            break;
        case CSMI_SAS_RAID_SET_STATUS_TRANSFORMING:
            printf("Transforming - %" CPRIu8 "%%\n", config->bInformation);
            break;
        case CSMI_SAS_RAID_SET_STATUS_QUEUED_FOR_REBUILD:
            printf("Queued for Rebuild\n");
            break;
        case CSMI_SAS_RAID_SET_STATUS_QUEUED_FOR_TRANSFORMATION:
            printf("Queued for Transformation\n");
            break;
        default:
            printf("Unknown\n");
            break;
        }
        printf("\tDrive Count: ");
        if (config->bDriveCount > 0xF0)
        {
            switch (config->bDriveCount)
            {
            case CSMI_SAS_RAID_DRIVE_COUNT_TOO_BIG:
                printf("Too Big\n");
                break;
            case CSMI_SAS_RAID_DRIVE_COUNT_SUPRESSED:
                printf("Supressed\n");
                break;
            default:
                printf("Unknown reserved value - %" CPRIX8 "h\n", config->bDriveCount);
                break;
            }
        }
        else
        {
            printf("%" CPRIu8 "\n", config->bDriveCount);
        }
        // Use DataType to switch between what was reported back
        // this is being checked since failure code and change count were added later
        if (!is_Empty(config, 20))
        {
            printf("\tFailure Code: ");
            print_CSMI_RAID_Failure_Code(config->uFailureCode);
            printf("\tChange Count: %" CPRIu32 "\n", config->uChangeCount);
        }
        bool driveDataValid = true;
        // If an ASCII character is in the bDataType offset, this is Intel's driver
        // at some point, need to switch to using CSMI version information....somehow
        // driverInfo.Information.usCSMIMajorRevision > 0 || driverInfo.Information.usCSMIMinorRevision > 81
        if (!safe_isascii(config->bDataType))
        {
            switch (config->bDataType)
            {
            case CSMI_SAS_RAID_DATA_DRIVES:
                printf("RAID Drive Data\n");
                break;
            case CSMI_SAS_RAID_DATA_DEVICE_ID:
                // TODO: Print this out...device identification VPD page
                printf("Device ID (Debug info not supported at this time)\n");
                driveDataValid = false;
                break;
            case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
                // TODO: Print this out
                printf("Additional Data (Debug info not supported at this time)\n");
                driveDataValid = false;
                break;
            default:
                printf("Unknown data type.\n");
                driveDataValid = false;
                break;
            }
        }
        if (driveDataValid)
        {
            if (config->bDriveCount < 0xF1)
            {
                uint32_t totalDrives =
                    C_CAST(uint32_t, (configLength - UINT32_C(36)) /
                                         sizeof(CSMI_SAS_RAID_DRIVES)); // 36 bytes prior to drive data
                for (uint32_t iter = UINT32_C(0); iter < totalDrives && iter < config->bDriveCount; ++iter)
                {
                    DECLARE_ZERO_INIT_ARRAY(char, model, 41);
                    DECLARE_ZERO_INIT_ARRAY(char, firmware, 9);
                    DECLARE_ZERO_INIT_ARRAY(char, serialNumber, 41);
                    safe_memcpy(model, 41, config->Drives[iter].bModel, 40);
                    safe_memcpy(firmware, 9, config->Drives[iter].bFirmware, 8);
                    safe_memcpy(serialNumber, 41, config->Drives[iter].bSerialNumber, 40);
                    printf("\t----RAID Drive %" PRIu32 "----\n", iter);
                    printf("\t\tModel #: %s\n", model);
                    printf("\t\tFirmware: %s\n", firmware);
                    printf("\t\tSerial #: %s\n", serialNumber);
                    printf("\t\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
                           "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n",
                           config->Drives[iter].bSASAddress[0], config->Drives[iter].bSASAddress[1],
                           config->Drives[iter].bSASAddress[2], config->Drives[iter].bSASAddress[3],
                           config->Drives[iter].bSASAddress[4], config->Drives[iter].bSASAddress[5],
                           config->Drives[iter].bSASAddress[6], config->Drives[iter].bSASAddress[7]);
                    printf("\t\tSAS LUN: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
                           "%02" CPRIX8 "%02" CPRIX8 "h\n",
                           config->Drives[iter].bSASLun[0], config->Drives[iter].bSASLun[1],
                           config->Drives[iter].bSASLun[2], config->Drives[iter].bSASLun[3],
                           config->Drives[iter].bSASLun[4], config->Drives[iter].bSASLun[5],
                           config->Drives[iter].bSASLun[6], config->Drives[iter].bSASLun[7]);
                    printf("\t\tDrive Status: ");
                    switch (config->Drives[iter].bDriveStatus)
                    {
                    case CSMI_SAS_DRIVE_STATUS_OK:
                        printf("OK\n");
                        break;
                    case CSMI_SAS_DRIVE_STATUS_REBUILDING:
                        printf("Rebuilding\n");
                        break;
                    case CSMI_SAS_DRIVE_STATUS_FAILED:
                        printf("Failed\n");
                        break;
                    case CSMI_SAS_DRIVE_STATUS_DEGRADED:
                        printf("Degraded\n");
                        break;
                    case CSMI_SAS_DRIVE_STATUS_OFFLINE:
                        printf("Offline\n");
                        break;
                    case CSMI_SAS_DRIVE_STATUS_QUEUED_FOR_REBUILD:
                        printf("Queued for Rebuild\n");
                        break;
                    default:
                        printf("Unknown\n");
                        break;
                    }
                    printf("\t\tDrive Usage: ");
                    switch (config->Drives[iter].bDriveUsage)
                    {
                    case CSMI_SAS_DRIVE_CONFIG_NOT_USED:
                        printf("Not Used\n");
                        break;
                    case CSMI_SAS_DRIVE_CONFIG_MEMBER:
                        printf("Member\n");
                        break;
                    case CSMI_SAS_DRIVE_CONFIG_SPARE:
                        printf("Spare\n");
                        break;
                    case CSMI_SAS_DRIVE_CONFIG_SPARE_ACTIVE:
                        printf("Spare - Active\n");
                        break;
                    case CSMI_SAS_DRIVE_CONFIG_SRT_CACHE: // Unique to Intel!
                        printf("SRT Cache\n");
                        break;
                    case CSMI_SAS_DRIVE_CONFIG_SRT_DATA: // Unique to Intel!
                        printf("SRT Data\n");
                        break;
                    default:
                        printf("Unknown - %" CPRIu8 "\n", config->Drives[iter].bDriveUsage);
                        break;
                    }
                    // end of original RAID drive data in spec. Check if empty
                    size_t previouslyReservedBytes =
                        sizeof(CSMI_SAS_RAID_DRIVES) - offsetof(CSMI_SAS_RAID_DRIVES, usBlockSize);
                    // original spec says 22 reserved bytes, however I count 30 more bytes to check...-TJE
                    if (!is_Empty(&config->Drives[iter], previouslyReservedBytes))
                    {
                        printf("\t\tBlock Size: %" CPRIu16 "\n", config->Drives[iter].usBlockSize);
                        printf("\t\tDrive Type: ");
                        switch (config->Drives[iter].bDriveType)
                        {
                        case CSMI_SAS_DRIVE_TYPE_UNKNOWN:
                            printf("Unknown\n");
                            break;
                        case CSMI_SAS_DRIVE_TYPE_SINGLE_PORT_SAS:
                            printf("Single Port SAS\n");
                            break;
                        case CSMI_SAS_DRIVE_TYPE_DUAL_PORT_SAS:
                            printf("Dual Port SAS\n");
                            break;
                        case CSMI_SAS_DRIVE_TYPE_SATA:
                            printf("SATA\n");
                            break;
                        case CSMI_SAS_DRIVE_TYPE_SATA_PS:
                            printf("SATA Port Selector\n");
                            break;
                        case CSMI_SAS_DRIVE_TYPE_OTHER:
                            printf("Other\n");
                            break;
                        default:
                            printf("Unknown - %" CPRIu8 "\n", config->Drives[iter].bDriveType);
                            break;
                        }
                        printf("\t\tDrive Index: %" CPRIu32 "\n", config->Drives[iter].uDriveIndex);
                        printf("\t\tTotal User Blocks: %" PRIu64 "\n",
                               M_DWordsTo8ByteValue(config->Drives[iter].ulTotalUserBlocks.uHighPart,
                                                    config->Drives[iter].ulTotalUserBlocks.uLowPart));
                    }
                }
            }
        }
    }
}

// example RAID config output from different drives:

// AMD Rcraid:
//====CSMI RAID Configuration====
//	RAID Set Index: 0 <- non-raid individual drive
//	Capacity (MB): 122104
//	Stripe Size (KB): 0
//	RAID Type: Other
//	Status: OK
//	Drive Count: 1
//	----RAID Drive 0----
//		Model #:
//		Firmware:
//		Serial #:
//		SAS Address: 0000000000000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member
//		Block Size: 512
//		Drive Type: SATA
//		Drive Index: 1
//		Total User Blocks: 0
//====CSMI RAID Configuration====
//	RAID Set Index: 1 <-raid 1 of 2 drives
//	Capacity (MB): 17165814
//	Stripe Size (KB): 0
//	RAID Type: 1
//	Status: OK
//	Drive Count: 2
//	----RAID Drive 0----
//		Model #:
//		Firmware:
//		Serial #:
//		SAS Address: 0000000000000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member
//		Block Size: 512
//		Drive Type: SATA
//		Drive Index: 0
//		Total User Blocks: 0
//	----RAID Drive 1----
//		Model #:
//		Firmware:
//		Serial #:
//		SAS Address: 0000000000000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member
//		Block Size: 512
//		Drive Type: SATA
//		Drive Index: 2
//		Total User Blocks: 0
//====CSMI RAID Configuration====
//	RAID Set Index: 2 <-appears to be duplicate of index 1 for some reason
//	Capacity (MB): 17165814
//	Stripe Size (KB): 0
//	RAID Type: 1
//	Status: OK
//	Drive Count: 2
//	----RAID Drive 0----
//		Model #:
//		Firmware:
//		Serial #:
//		SAS Address: 0000000000000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member
//		Block Size: 512
//		Drive Type: SATA
//		Drive Index: 0
//		Total User Blocks: 0
//	----RAID Drive 1----
//		Model #:
//		Firmware:
//		Serial #:
//		SAS Address: 0000000000000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member
//		Block Size: 512
//		Drive Type: SATA
//		Drive Index: 2
//		Total User Blocks: 0

// Intel isStorE:
//====CSMI RAID Configuration====
//	RAID Set Index: 0 <- raid of 2 drives
//	Capacity (MB): 16308008
//	Stripe Size (KB): 64
//	RAID Type: 1
//	Status: OK
//	Drive Count: 2
//	Failure Code: 0
//	Change Count: 0
//	----RAID Drive 0----
//		Model #: ST18000NM000J-2TV103
//		Firmware: SN02
//		Serial #:             WR507HEG
//		SAS Address: 0000040400000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member
//	----RAID Drive 1----
//		Model #: ST18000NM000J-2TV103
//		Firmware: SN02
//		Serial #:             WR5081B1
//		SAS Address: 0000050500000000h
//		SAS LUN: 0000000000000000h
//		Drive Status: OK
//		Drive Usage: Member

// NOTE: This buffer should be allocated as sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) + (raidInfo.uMaxDrivesPerSet *
// sizeof(CSMI_SAS_RAID_DRIVES)) at minimum. If the device identification VPD page is returned instead, it may be longer
//       RAID set index must be lower than the number of raid sets listed as supported by RAID INFO
// NOTE: Dataype field may not be supported depending on which version of CSMI is supported. Intel RST will not support
// this.
eReturnValues csmi_Get_RAID_Config(CSMI_HANDLE                  deviceHandle,
                                   uint32_t                     controllerNumber,
                                   PCSMI_SAS_RAID_CONFIG_BUFFER raidConfigBuffer,
                                   uint32_t                     raidConfigBufferTotalSize,
                                   uint32_t                     raidSetIndex,
                                   uint8_t                      dataType,
                                   eVerbosityLevels             verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(raidConfigBuffer, raidConfigBufferTotalSize, 0, raidConfigBufferTotalSize);

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = raidConfigBuffer;
    ioIn.ioctlBufferSize  = raidConfigBufferTotalSize;
    ioIn.dataLength       = C_CAST(uint32_t, raidConfigBufferTotalSize - sizeof(IOCTL_HEADER));
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_RAID_CONFIG;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_RAID_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_RAID_SIGNATURE, safe_strlen(CSMI_RAID_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    raidConfigBuffer->Configuration.uRaidSetIndex = raidSetIndex;
    raidConfigBuffer->Configuration.bDataType =
        dataType; // NOTE: This may only be implemented on SOME CSMI implementations. Not supported by Intel RST as they
                  // only support up to .77 changes, but this is a newer field.

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get RAID Config\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(raidConfigBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_RAID_Config(&raidConfigBuffer->Configuration,
                                   C_CAST(uint32_t, raidConfigBufferTotalSize - sizeof(IOCTL_HEADER)));
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get RAID Config\n", ret);
    }

    return ret;
}

// Get RAID Features

static void print_CSMI_RAID_Features(PCSMI_SAS_RAID_FEATURES features)
{
    if (features)
    {
        printf("\n====CSMI RAID Features====\n");
        printf("\tFeatures:\n");
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_TRANSFORMATION)
        {
            printf("\t\tTransformational\n");
            printf("\t\tDefault Transform Priority:\n");
            if (features->bDefaultTransformPriority == CSMI_SAS_PRIORITY_UNCHANGED)
            {
                printf("\t\t\tUnchanged\n");
            }
            if (features->bDefaultTransformPriority == CSMI_SAS_PRIORITY_AUTO)
            {
                printf("\t\t\tAuto\n");
            }
            if (features->bDefaultTransformPriority == CSMI_SAS_PRIORITY_OFF)
            {
                printf("\t\t\tOff\n");
            }
            if (features->bDefaultTransformPriority == CSMI_SAS_PRIORITY_LOW)
            {
                printf("\t\t\tLow\n");
            }
            if (features->bDefaultTransformPriority == CSMI_SAS_PRIORITY_MEDIUM)
            {
                printf("\t\t\tMedium\n");
            }
            if (features->bDefaultTransformPriority == CSMI_SAS_PRIORITY_HIGH)
            {
                printf("\t\t\tHigh\n");
            }
            printf("\t\tTransform Priority:\n");
            if (features->bTransformPriority == CSMI_SAS_PRIORITY_UNCHANGED)
            {
                printf("\t\t\tUnchanged\n");
            }
            if (features->bTransformPriority == CSMI_SAS_PRIORITY_AUTO)
            {
                printf("\t\t\tAuto\n");
            }
            if (features->bTransformPriority == CSMI_SAS_PRIORITY_OFF)
            {
                printf("\t\t\tOff\n");
            }
            if (features->bTransformPriority == CSMI_SAS_PRIORITY_LOW)
            {
                printf("\t\t\tLow\n");
            }
            if (features->bTransformPriority == CSMI_SAS_PRIORITY_MEDIUM)
            {
                printf("\t\t\tMedium\n");
            }
            if (features->bTransformPriority == CSMI_SAS_PRIORITY_HIGH)
            {
                printf("\t\t\tHigh\n");
            }
            printf("\t\tRAID Set Transformational Rules:\n");
            if (features->uRaidSetTransformationRules & CSMI_SAS_RAID_RULE_AVAILABLE_MEMORY)
            {
                printf("\t\t\tAvailable Memory\n");
            }
            if (features->uRaidSetTransformationRules & CSMI_SAS_RAID_RULE_OVERLAPPED_EXTENTS)
            {
                printf("\t\t\tOverlapped Extents\n");
            }
        }
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_REBUILD)
        {
            printf("\t\tRebuild\n");
            printf("\t\tDefault Rebuild Priority:\n");
            if (features->bDefaultRebuildPriority == CSMI_SAS_PRIORITY_UNCHANGED)
            {
                printf("\t\t\tUnchanged\n");
            }
            if (features->bDefaultRebuildPriority == CSMI_SAS_PRIORITY_AUTO)
            {
                printf("\t\t\tAuto\n");
            }
            if (features->bDefaultRebuildPriority == CSMI_SAS_PRIORITY_OFF)
            {
                printf("\t\t\tOff\n");
            }
            if (features->bDefaultRebuildPriority == CSMI_SAS_PRIORITY_LOW)
            {
                printf("\t\t\tLow\n");
            }
            if (features->bDefaultRebuildPriority == CSMI_SAS_PRIORITY_MEDIUM)
            {
                printf("\t\t\tMedium\n");
            }
            if (features->bDefaultRebuildPriority == CSMI_SAS_PRIORITY_HIGH)
            {
                printf("\t\t\tHigh\n");
            }
            printf("\t\tRebuild Priority:\n");
            if (features->bRebuildPriority == CSMI_SAS_PRIORITY_UNCHANGED)
            {
                printf("\t\t\tUnchanged\n");
            }
            if (features->bRebuildPriority == CSMI_SAS_PRIORITY_AUTO)
            {
                printf("\t\t\tAuto\n");
            }
            if (features->bRebuildPriority == CSMI_SAS_PRIORITY_OFF)
            {
                printf("\t\t\tOff\n");
            }
            if (features->bRebuildPriority == CSMI_SAS_PRIORITY_LOW)
            {
                printf("\t\t\tLow\n");
            }
            if (features->bRebuildPriority == CSMI_SAS_PRIORITY_MEDIUM)
            {
                printf("\t\t\tMedium\n");
            }
            if (features->bRebuildPriority == CSMI_SAS_PRIORITY_HIGH)
            {
                printf("\t\t\tHigh\n");
            }
        }
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_SPLIT_MIRROR)
        {
            printf("\t\tSplit Mirror\n");
        }
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_MERGE_MIRROR)
        {
            printf("\t\tMerge Mirror\n");
        }
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_LUN_RENUMBER)
        {
            printf("\t\tLUN Renumber\n");
        }
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_SURFACE_SCAN)
        {
            printf("\t\tSurface Scan\n");
            printf("\t\tDefault Surface Scan Priority:\n");
            if (features->bDefaultSurfaceScanPriority == CSMI_SAS_PRIORITY_UNCHANGED)
            {
                printf("\t\t\tUnchanged\n");
            }
            if (features->bDefaultSurfaceScanPriority == CSMI_SAS_PRIORITY_AUTO)
            {
                printf("\t\t\tAuto\n");
            }
            if (features->bDefaultSurfaceScanPriority == CSMI_SAS_PRIORITY_OFF)
            {
                printf("\t\t\tOff\n");
            }
            if (features->bDefaultSurfaceScanPriority == CSMI_SAS_PRIORITY_LOW)
            {
                printf("\t\t\tLow\n");
            }
            if (features->bDefaultSurfaceScanPriority == CSMI_SAS_PRIORITY_MEDIUM)
            {
                printf("\t\t\tMedium\n");
            }
            if (features->bDefaultSurfaceScanPriority == CSMI_SAS_PRIORITY_HIGH)
            {
                printf("\t\t\tHigh\n");
            }
            printf("\t\tSurface Scan Priority:\n");
            if (features->bSurfaceScanPriority == CSMI_SAS_PRIORITY_UNCHANGED)
            {
                printf("\t\t\tUnchanged\n");
            }
            if (features->bSurfaceScanPriority == CSMI_SAS_PRIORITY_AUTO)
            {
                printf("\t\t\tAuto\n");
            }
            if (features->bSurfaceScanPriority == CSMI_SAS_PRIORITY_OFF)
            {
                printf("\t\t\tOff\n");
            }
            if (features->bSurfaceScanPriority == CSMI_SAS_PRIORITY_LOW)
            {
                printf("\t\t\tLow\n");
            }
            if (features->bSurfaceScanPriority == CSMI_SAS_PRIORITY_MEDIUM)
            {
                printf("\t\t\tMedium\n");
            }
            if (features->bSurfaceScanPriority == CSMI_SAS_PRIORITY_HIGH)
            {
                printf("\t\t\tHigh\n");
            }
        }
        if (features->uFeatures & CSMI_SAS_RAID_FEATURE_SPARES_SHARED)
        {
            printf("\t\tSpares Shared\n");
        }
        // TODO: Check reserved features 32bytes
        printf("\tRAID Type Description(s):\n");
        for (uint8_t raidTypeIter = UINT8_C(0); raidTypeIter < 24; ++raidTypeIter)
        {
            if (!is_Empty(&features->RaidType[raidTypeIter], sizeof(CSMI_SAS_RAID_TYPE_DESCRIPTION)))
            {
                printf("\t\tRAID Type: ");
                // bRaidType
                print_CSMI_RaidType(features->RaidType[raidTypeIter].bRaidType);
                // uSupportedStripeSizeMap
                printf("Supported Stripe Size Map: %" CPRIu32 "\n",
                       features->RaidType[raidTypeIter].uSupportedStripeSizeMap);
            }
        }
        // Cache ratios supported. 104 possible, some special values

        printf("\tChange Count: %" CPRIu32 "\n", features->uChangeCount);
        printf("\tFailure Code: ");
        print_CSMI_RAID_Failure_Code(features->uFailureCode);
    }
}

eReturnValues csmi_Get_RAID_Features(CSMI_HANDLE                    deviceHandle,
                                     uint32_t                       controllerNumber,
                                     PCSMI_SAS_RAID_FEATURES_BUFFER raidFeaturesBuffer,
                                     eVerbosityLevels               verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(raidFeaturesBuffer, sizeof(CSMI_SAS_RAID_FEATURES_BUFFER), 0, sizeof(CSMI_SAS_RAID_FEATURES_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = raidFeaturesBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_RAID_FEATURES_BUFFER);
    ioIn.dataLength       = C_CAST(uint32_t, sizeof(CSMI_SAS_RAID_FEATURES_BUFFER) - sizeof(IOCTL_HEADER));
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_RAID_FEATURES;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_RAID_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_RAID_SIGNATURE, safe_strlen(CSMI_RAID_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get RAID Features\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(raidFeaturesBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_RAID_Features(&raidFeaturesBuffer->Information);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get RAID Features\n", ret);
    }

    return ret;
}

// TODO:
//       Set RAID Control
//       Get RAID Element
//       Set RAID Operation

static void print_CSMI_Port_Protocol(uint8_t portProtocol)
{
    bool needComma = false;
    if (portProtocol & CSMI_SAS_PROTOCOL_SATA)
    {
        printf("SATA");
        needComma = true;
    }
    if (portProtocol & CSMI_SAS_PROTOCOL_SMP)
    {
        if (needComma)
        {
            printf(", ");
        }
        printf("SMP");
        needComma = true;
    }
    if (portProtocol & CSMI_SAS_PROTOCOL_STP)
    {
        if (needComma)
        {
            printf(", ");
        }
        printf("STP");
        needComma = true;
    }
    if (portProtocol & CSMI_SAS_PROTOCOL_SSP)
    {
        if (needComma)
        {
            printf(", ");
        }
        printf("SSP");
        needComma = true;
    }
    printf("\n");
}

static void print_CSMI_SAS_Identify(PCSMI_SAS_IDENTIFY identify)
{
    if (identify)
    {
        // everything printed with 3 tabs to fit with print function below since this is only used by Phy info data
        printf("\t\t\tDevice Type: ");
        switch (identify->bDeviceType)
        {
        case CSMI_SAS_PHY_UNUSED:
            printf("Unused or No Device Attached\n");
            break;
        case CSMI_SAS_END_DEVICE:
            printf("End Device\n");
            break;
        case CSMI_SAS_EDGE_EXPANDER_DEVICE:
            printf("Edge Expander Device\n");
            break;
        case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
            printf("Fanout Expander Device\n");
            break;
        default:
            printf("Unknown\n");
            break;
        }
        printf("\t\t\tRestricted: %02" CPRIX8 "h\n", identify->bRestricted);
        printf("\t\t\tInitiator Port Protocol: ");
        print_CSMI_Port_Protocol(identify->bInitiatorPortProtocol);
        printf("\t\t\tTarget Port Protocol: ");
        print_CSMI_Port_Protocol(identify->bTargetPortProtocol);
        printf("\t\t\tRestricted 2: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
               "%02" CPRIX8 "%02" CPRIX8 "h\n",
               identify->bRestricted2[0], identify->bRestricted2[1], identify->bRestricted2[2],
               identify->bRestricted2[3], identify->bRestricted2[4], identify->bRestricted2[5],
               identify->bRestricted2[6], identify->bRestricted2[7]);
        printf("\t\t\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
               "%02" CPRIX8 "%02" CPRIX8 "h\n",
               identify->bSASAddress[0], identify->bSASAddress[1], identify->bSASAddress[2], identify->bSASAddress[3],
               identify->bSASAddress[4], identify->bSASAddress[5], identify->bSASAddress[6], identify->bSASAddress[7]);
        printf("\t\t\tPhy Identifier: %" CPRIu8 "\n", identify->bPhyIdentifier);
        printf("\t\t\tSignal Class: ");
        switch (identify->bSignalClass)
        {
        case CSMI_SAS_SIGNAL_CLASS_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_DIRECT:
            printf("Direct\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_SERVER:
            printf("Server\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_ENCLOSURE:
            printf("Enclosure\n");
            break;
        default:
            printf("Unknown - %" CPRIX8 "h\n", identify->bSignalClass);
            break;
        }
    }
}

static void print_CSMI_Link_Rate(uint8_t linkRate)
{
    // low nibble is rate. High nibble is flags
    switch (M_Nibble0(linkRate))
    {
    case CSMI_SAS_LINK_RATE_UNKNOWN:
        printf("Unknown\n");
        break;
    case CSMI_SAS_PHY_DISABLED:
        printf("Phy Disabled\n");
        break;
    case CSMI_SAS_LINK_RATE_FAILED:
        printf("Link Rate Failed\n");
        break;
    case CSMI_SAS_SATA_SPINUP_HOLD:
        printf("SATA Spinup Hold\n");
        break;
    case CSMI_SAS_SATA_PORT_SELECTOR:
        printf("SATA Port Selector\n");
        break;
    case CSMI_SAS_LINK_RATE_1_5_GBPS:
        printf("1.5 Gb/s\n");
        break;
    case CSMI_SAS_LINK_RATE_3_0_GBPS:
        printf("3.0 Gb/s\n");
        break;
    case CSMI_SAS_LINK_RATE_6_0_GBPS:
        printf("6.0 Gb/s\n");
        break;
    case CSMI_SAS_LINK_RATE_12_0_GBPS:
        printf("12.0 Gb/s\n");
        break;
    default:
        printf("Unknown - %02" CPRIX8 "h\n", linkRate);
        break;
    }
    // now review known flags
    if (linkRate & CSMI_SAS_LINK_VIRTUAL)
    {
        printf("\tVirtual link rate\n");
    }
}

static void print_CSMI_Phy_Info(PCSMI_SAS_PHY_INFO phyInfo)
{
    if (phyInfo)
    {
        printf("\n====CSMI Phy Info====\n");
        printf("\tNumber Of Phys: %" CPRIu8 "\n", phyInfo->bNumberOfPhys);
        for (uint8_t phyIter = UINT8_C(0); phyIter < phyInfo->bNumberOfPhys && phyIter < 32; ++phyIter)
        {
            printf("\t----Phy %" CPRIu8 "----\n", phyIter);
            printf("\t\tIdentify:\n");
            print_CSMI_SAS_Identify(&phyInfo->Phy[phyIter].Identify);
            printf("\t\tPort Identifier: %" CPRIu8 "\n", phyInfo->Phy[phyIter].bPortIdentifier);
            printf("\t\tNegotiated Link Rate: ");
            print_CSMI_Link_Rate(phyInfo->Phy[phyIter].bNegotiatedLinkRate);
            printf("\t\tMinimum Link Rate: ");
            print_CSMI_Link_Rate(phyInfo->Phy[phyIter].bMinimumLinkRate);
            printf("\t\tMaximum Link Rate: ");
            print_CSMI_Link_Rate(phyInfo->Phy[phyIter].bMaximumLinkRate);
            printf("\t\tPhy Change Count: %" CPRIu8 "\n", phyInfo->Phy[phyIter].bPhyChangeCount);
            printf("\t\tAuto Discover: ");
            switch (phyInfo->Phy[phyIter].bAutoDiscover)
            {
            case CSMI_SAS_DISCOVER_NOT_SUPPORTED:
                printf("Not Supported\n");
                break;
            case CSMI_SAS_DISCOVER_NOT_STARTED:
                printf("Not Started\n");
                break;
            case CSMI_SAS_DISCOVER_IN_PROGRESS:
                printf("In Progress\n");
                break;
            case CSMI_SAS_DISCOVER_COMPLETE:
                printf("Complete\n");
                break;
            case CSMI_SAS_DISCOVER_ERROR:
                printf("Error\n");
                break;
            default:
                printf("Unknown\n");
                break;
            }
            printf("\t\tPhy Features: ");
            if (phyInfo->Phy[phyIter].bPhyFeatures == 0)
            {
                printf("None");
            }
            else
            {
                if (phyInfo->Phy[phyIter].bPhyFeatures & CSMI_SAS_PHY_VIRTUAL_SMP)
                {
                    printf("Virtual SMP");
                }
            }
            printf("\n");
            printf("\t\tAttached:\n");
            print_CSMI_SAS_Identify(&phyInfo->Phy[phyIter].Attached);
            printf("\n");
        }
    }
}

// NOTE: AMD's rcraid driver seems to treat non-raid drives slightly different in the phy info output. In the case I
// observed, the raid drives were in the phy info, but the
//       non-RAID drive was not shown here at all. However in the RAID IOCTLs, it does show as a separate single drive
//       raid (but no MN or SN info to match to it). So it does not appear possible to issue a CSMI passthrough command
//       to non-raid drives. This is completely opposite of what the Intel drivers do. Intel's drivers show every drive
//       attached, RAID or non-RAID in the phy info. This may be something we want to detect in the future to reduce the
//       number of IOCTLs sent, but for now, it works ok. We can optimize this more later. -TJE

// Below are some sample outputs of the Phy Info:

// AMD rcraid:
//====CSMI Phy Info====
//	Number Of Phys: 2
//	----Phy 0----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//		Port Identifier: 0
//		Negotiated Link Rate: 6.0 Gb/s
//		Minimum Link Rate: 6.0 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Complete
//		Phy Features: Virtual SMP
//		Attached:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 1----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 1
//			Signal Class: Unknown
//		Port Identifier: 5
//		Negotiated Link Rate: 6.0 Gb/s
//		Minimum Link Rate: 6.0 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Complete
//		Phy Features: Virtual SMP
//		Attached:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown

// Intel iaStorE - 7.7.0.1260
//====CSMI Phy Info====
//	Number Of Phys: 8
//	----Phy 0----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//		Port Identifier: 0
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: Unknown
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 1----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 1
//			Signal Class: Unknown
//		Port Identifier: 1
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: Unknown
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000010000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 2----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 2
//			Signal Class: Unknown
//		Port Identifier: 2
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: Unknown
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000020000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 3----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 3
//			Signal Class: Unknown
//		Port Identifier: 3
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: Unknown
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000030000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 4----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 4
//			Signal Class: Unknown
//		Port Identifier: 4
//		Negotiated Link Rate: 6.0 Gb/s
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000040000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 5----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 5
//			Signal Class: Unknown
//		Port Identifier: 5
//		Negotiated Link Rate: 6.0 Gb/s
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000050000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 6----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 6
//			Signal Class: Unknown
//		Port Identifier: 6
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: Unknown
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000060000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 7----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA, SMP, STP, SSP
//			Target Port Protocol:
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 7
//			Signal Class: Unknown
//		Port Identifier: 7
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: Unknown
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol:
//			Target Port Protocol: SATA, SMP, STP, SSP
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000070000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown

// Intel iaStorAC phy info:
//====CSMI Phy Info====
//	Number Of Phys: 8
//	----Phy 0----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//		Port Identifier: 0
//		Negotiated Link Rate: 6.0 Gb/s
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 1----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 1
//			Signal Class: Unknown
//		Port Identifier: 1
//		Negotiated Link Rate: 6.0 Gb/s
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0001000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 2----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 2
//			Signal Class: Unknown
//		Port Identifier: 2
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0002000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 3----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 3
//			Signal Class: Unknown
//		Port Identifier: 3
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0003000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 4----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 4
//			Signal Class: Unknown
//		Port Identifier: 4
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0004000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 5----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 5
//			Signal Class: Unknown
//		Port Identifier: 5
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0005000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 6----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 6
//			Signal Class: Unknown
//		Port Identifier: 6
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0006000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown
//
//	----Phy 7----
//		Identify:
//			Device Type: End Device
//			Restricted: 00h
//			Initiator Port Protocol: SATA
//			Target Port Protocol: Unknown
//			Restricted 2: 0000000000000000h
//			SAS Address: 0000000000000000h
//			Phy Identifier: 7
//			Signal Class: Unknown
//		Port Identifier: 7
//		Negotiated Link Rate: Unknown
//		Minimum Link Rate: 1.5 Gb/s
//		Maximum Link Rate: 6.0 Gb/s
//		Phy Change Count: 0
//		Auto Discover: Not Supported
//		Phy Features: None
//		Attached:
//			Device Type: Unused or No Device Attached
//			Restricted: 00h
//			Initiator Port Protocol: Unknown
//			Target Port Protocol: SATA
//			Restricted 2: 0000000000000000h
//			SAS Address: 0007000000000000h
//			Phy Identifier: 0
//			Signal Class: Unknown

// ARCSAS Driver: For some unknown reason this driver needs an additional 484 bytes after the
// sizeof(CSMI_SAS_PHY_INFO_BUFFER) to respond.
//                I do not know why since it's all zeroes in my case, but that may also depend on RAID configuration
//                setup. But this seems to be the minimum number of bytes required for this driver to respond to this
//                IOCTL. It may be some alignment issue or appending extra vendor unique data somewhere.-TJE

// Caller allocated full buffer, then we fill in the rest and send it. Data length not needed since this one is a fixed
// size
eReturnValues csmi_Get_Phy_Info(CSMI_HANDLE               deviceHandle,
                                uint32_t                  controllerNumber,
                                PCSMI_SAS_PHY_INFO_BUFFER phyInfoBuffer,
                                eVerbosityLevels          verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    uint32_t      phyInfoSize = sizeof(CSMI_SAS_PHY_INFO_BUFFER) +
                           484; // ARCSAS Workaround. Easier to handle by over-allocating as this does not break
                                // compatibility with other drivers. Unknown what extra space is needed for.
    PCSMI_SAS_PHY_INFO_BUFFER temp = safe_malloc(phyInfoSize); // allocating out own internal version to a larger size
    if (temp == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(temp, phyInfoSize, 0, phyInfoSize);

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = temp;
    ioIn.ioctlBufferSize  = phyInfoSize;
    ioIn.dataLength       = phyInfoSize - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_PHY_INFO;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Phy Info\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        safe_memcpy(phyInfoBuffer, sizeof(CSMI_SAS_PHY_INFO_BUFFER), temp, sizeof(CSMI_SAS_PHY_INFO_BUFFER));
        ret = csmi_Return_To_OpenSea_Result(phyInfoBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Phy_Info(&phyInfoBuffer->Information);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Phy Info\n", ret);
    }
    safe_free_core(M_REINTERPRET_CAST(
        void**, &temp)); // temp holds the passed in phyInfoBuffer and that has already been updated when this succeeds

    return ret;
}

eReturnValues csmi_Set_Phy_Info(CSMI_HANDLE                   deviceHandle,
                                uint32_t                      controllerNumber,
                                PCSMI_SAS_SET_PHY_INFO_BUFFER phyInfoBuffer,
                                eVerbosityLevels              verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(phyInfoBuffer, sizeof(IOCTL_HEADER), 0,
                sizeof(IOCTL_HEADER)); // only clear out the header...caller should setup the changes they want to make

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = phyInfoBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_SET_PHY_INFO;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_WRITE;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Set Phy Info\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(phyInfoBuffer->IoctlHeader.ReturnCode);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Set Phy Info\n", ret);
    }

    return ret;
}

eReturnValues csmi_Get_Link_Errors(CSMI_HANDLE                  deviceHandle,
                                   uint32_t                     controllerNumber,
                                   PCSMI_SAS_LINK_ERRORS_BUFFER linkErrorsBuffer,
                                   uint8_t                      phyIdentifier,
                                   bool                         resetCounts,
                                   eVerbosityLevels             verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(linkErrorsBuffer, sizeof(CSMI_SAS_LINK_ERRORS_BUFFER), 0, sizeof(CSMI_SAS_LINK_ERRORS_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = linkErrorsBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_LINK_ERRORS_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_LINK_ERRORS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_LINK_ERRORS;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    linkErrorsBuffer->Information.bPhyIdentifier = phyIdentifier;
    linkErrorsBuffer->Information.bResetCounts   = CSMI_SAS_LINK_ERROR_DONT_RESET_COUNTS;
    if (resetCounts)
    {
        linkErrorsBuffer->Information.bResetCounts = CSMI_SAS_LINK_ERROR_RESET_COUNTS;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Link Errors\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(linkErrorsBuffer->IoctlHeader.ReturnCode);
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Link Errors\n", ret);
    }

    return ret;
}

// TODO: SMP Passthrough function

// SSP Passthrough function (just for internal use, other functions should parse tdevice and scsiIoCtx to issue this
typedef struct s_csmiSSPIn
{
    uint8_t phyIdentifier;  // can set CSMI_SAS_USE_PORT_IDENTIFIER
    uint8_t portIdentifier; // can set CSMI_SAS_IGNORE_PORT
    uint8_t connectionRate; // strongly recommend leaving as negotiated
    uint8_t destinationSASAddress[8];
    uint8_t lun[8];
    uint8_t flags; // read, write, unspecified, head of queue, simple, ordered, aca, etc. Must set
                   // read/write/unspecified at minimum. Simple attribute is recommended
    uint8_t  cdbLength;
    uint8_t* cdb;
    uint8_t* ptrData;    // pointer to buffer to use as source for writes. This will be used for reads as well.
    uint32_t dataLength; // length of data to read or write
    uint32_t timeoutSeconds;
} csmiSSPIn, *ptrCsmiSSPIn;

typedef struct s_csmiSSPOut
{
    seatimer_t* sspTimer;        // may be null, but incredibly useful for knowing how long a command took
    uint8_t*    senseDataPtr;    // Should not be null. In case of a drive error, this gives you what happened
    uint32_t    senseDataLength; // length of memory pointed to by senseDataPtr
    uint8_t     connectionStatus;
} csmiSSPOut, *ptrCsmiSSPOut;

static eReturnValues csmi_SSP_Passthrough(CSMI_HANDLE      deviceHandle,
                                          uint32_t         controllerNumber,
                                          ptrCsmiSSPIn     sspInputs,
                                          ptrCsmiSSPOut    sspOutputs,
                                          eVerbosityLevels verbosity)
{
    eReturnValues                 ret = SUCCESS;
    csmiIOin                      ioIn;
    csmiIOout                     ioOut;
    PCSMI_SAS_SSP_PASSTHRU_BUFFER sspPassthrough             = M_NULLPTR;
    uint32_t                      sspPassthroughBufferLength = UINT32_C(0);
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));

    if (!sspInputs || !sspOutputs || !sspInputs->cdb) // NOTE: other validation is done below.
    {
        return BAD_PARAMETER;
    }
    sspPassthroughBufferLength = sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER) + sspInputs->dataLength;
    sspPassthrough             = C_CAST(PCSMI_SAS_SSP_PASSTHRU_BUFFER,
                                        safe_calloc_aligned(sspPassthroughBufferLength, sizeof(uint8_t), sizeof(void*)));
    if (!sspPassthrough)
    {
        return MEMORY_FAILURE;
    }

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = sspPassthrough;
    ioIn.ioctlBufferSize  = sspPassthroughBufferLength;
    ioIn.dataLength       = C_CAST(uint32_t, sspPassthroughBufferLength - sizeof(IOCTL_HEADER));
    ioIn.ioctlCode        = CC_CSMI_SAS_SSP_PASSTHRU;
    // ioIn.ioctlDirection = CSMI_SAS_DATA_READ;//This is set below, however it may only need to be set one way....will
    // only knwo when testing on linux since this is used there.
    ioIn.timeoutInSeconds = sspInputs->timeoutSeconds;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;
    ioOut.ioctlTimer   = sspOutputs->sspTimer;

    // setup ssp specific parameters
    if (sspInputs->cdbLength > 16)
    {
        if (sspInputs->cdbLength > 40)
        {
            safe_free_aligned_core(C_CAST(void**, &sspPassthrough));
            return OS_COMMAND_NOT_AVAILABLE;
        }
        // copy to cdb, then additional CDB
        safe_memcpy(sspPassthrough->Parameters.bCDB, 16, sspInputs->cdb, 16);
        safe_memcpy(sspPassthrough->Parameters.bAdditionalCDB, 24, sspInputs->cdb + 16, sspInputs->cdbLength - 16);
        sspPassthrough->Parameters.bCDBLength = 16;
        sspPassthrough->Parameters.bAdditionalCDBLength =
            C_CAST(uint8_t, (sspInputs->cdbLength - 16) / sizeof(uint32_t)); // this is in dwords according to the spec
    }
    else
    {
        safe_memcpy(sspPassthrough->Parameters.bCDB, 16, sspInputs->cdb, sspInputs->cdbLength);
        sspPassthrough->Parameters.bCDBLength = sspInputs->cdbLength;
    }

    sspPassthrough->Parameters.bConnectionRate = sspInputs->connectionRate;
    safe_memcpy(sspPassthrough->Parameters.bDestinationSASAddress, 8, sspInputs->destinationSASAddress, 8);
    safe_memcpy(sspPassthrough->Parameters.bLun, 8, sspInputs->lun, 8);
    sspPassthrough->Parameters.bPhyIdentifier  = sspInputs->phyIdentifier;
    sspPassthrough->Parameters.bPortIdentifier = sspInputs->portIdentifier;
    sspPassthrough->Parameters.uDataLength     = sspInputs->dataLength;
    sspPassthrough->Parameters.uFlags          = sspInputs->flags;

    if (sspPassthrough->Parameters.uFlags & CSMI_SAS_SSP_WRITE)
    {
        ioIn.ioctlDirection = CSMI_SAS_DATA_WRITE;
        if (sspInputs->ptrData)
        {
            safe_memcpy(sspPassthrough->bDataBuffer, sspInputs->dataLength, sspInputs->ptrData, sspInputs->dataLength);
        }
        else
        {
            safe_free_aligned_core(C_CAST(void**, &sspPassthrough));
            return BAD_PARAMETER;
        }
    }
    else
    {
        ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI SSP Passthrough\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(sspPassthrough->IoctlHeader.ReturnCode);
        // if (sspPassthrough->IoctlHeader.ReturnCode == CSMI_SAS_STATUS_SUCCESS)

        // check if response data is present in the status before trying to go any further copying it back
        if (sspPassthrough->Parameters.uFlags & CSMI_SAS_SSP_READ &&
            sspPassthrough->Status.bDataPresent & CSMI_SAS_SSP_RESPONSE_DATA_PRESENT)
        {
            // clear read data ptr first
            safe_memset(sspInputs->ptrData, sspInputs->dataLength, 0, sspInputs->dataLength);
            if (sspPassthrough->Status.uDataBytes)
            {
                // copy what we got
                safe_memcpy(sspInputs->ptrData, sspInputs->dataLength, sspPassthrough->bDataBuffer,
                            M_Min(sspInputs->dataLength, sspPassthrough->Status.uDataBytes));
            }
        }
        if (sspPassthrough->Status.bDataPresent & CSMI_SAS_SSP_SENSE_DATA_PRESENT)
        {
            // copy back sense data
            if (sspOutputs->senseDataPtr)
            {
                // clear sense first
                safe_memset(sspOutputs->senseDataPtr, sspOutputs->senseDataLength, 0, sspOutputs->senseDataLength);
                // now copy what we can
                safe_memcpy(sspOutputs->senseDataPtr, sspOutputs->senseDataLength, sspPassthrough->Status.bResponse,
                            M_Min(M_BytesTo2ByteValue(sspPassthrough->Status.bResponseLength[0],
                                                      sspPassthrough->Status.bResponseLength[1]),
                                  sspOutputs->senseDataLength));
            }
        }
        else // no data
        {
            if (sspInputs->ptrData && sspPassthrough->Parameters.uFlags & CSMI_SAS_SSP_READ)
            {
                safe_memset(sspInputs->ptrData, sspInputs->dataLength, 0, sspInputs->dataLength);
            }
            if (sspOutputs->senseDataPtr)
            {
                safe_memset(sspOutputs->senseDataPtr, sspOutputs->senseDataLength, 0, sspOutputs->senseDataLength);
            }
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI SSP Passthrough\n", ret);
    }

    safe_free_aligned_core(C_CAST(void**, &sspPassthrough));

    return ret;
}

typedef struct s_csmiSTPIn
{
    uint8_t  phyIdentifier;  // can set CSMI_SAS_USE_PORT_IDENTIFIER
    uint8_t  portIdentifier; // can set CSMI_SAS_IGNORE_PORT
    uint8_t  connectionRate; // strongly recommend leaving as negotiated
    uint8_t  destinationSASAddress[8];
    uint32_t flags; // read, write, unspecified, must also specify pio, dma, etc for the protocol of the command being
                    // issued.
    void*    commandFIS; // pointer to a 20 byte array for a H2D fis.
    uint32_t commandFISLen;
    uint8_t* ptrData;    // pointer to buffer to use as source for writes. This will be used for reads as well.
    uint32_t dataLength; // length of data to read or write
    uint32_t timeoutSeconds;
} csmiSTPIn, *ptrCsmiSTPIn;

typedef struct s_csmiSTPOut
{
    seatimer_t* stpTimer; // may be null, but incredibly useful for knowing how long a command took
    uint8_t* statusFIS; // Should not be null. In case of a drive error, this gives you what happened. This should point
                        // to a 20 byte array to store the result. May be D2H or PIO Setup depend on the command issued.
    uint32_t  statusFISLen;
    uint32_t* scrPtr; // Optional. This is the current status and control registers value. See SATA spec for more
                      // details on the registers that these map to. These are not writable through this interface.
    uint32_t scrLen;  // uint32's not bytes.
    uint8_t  connectionStatus;
    bool     retryAsSSPPassthrough; // This may be set, but will only be set, if the driver does not support STP
                                    // passthrough, but DOES support taking a SCSI translatable CDB. This cannot tell
    // whether to use SAT or legacy CSMI passthrough though...that's a trial and error thing
    // unless we figure out which drivers and versions require that. -TJE
} csmiSTPOut, *ptrCsmiSTPOut;

static eReturnValues csmi_STP_Passthrough(CSMI_HANDLE      deviceHandle,
                                          uint32_t         controllerNumber,
                                          ptrCsmiSTPIn     stpInputs,
                                          ptrCsmiSTPOut    stpOutputs,
                                          eVerbosityLevels verbosity)
{
    eReturnValues                 ret = SUCCESS;
    csmiIOin                      ioIn;
    csmiIOout                     ioOut;
    PCSMI_SAS_STP_PASSTHRU_BUFFER stpPassthrough             = M_NULLPTR;
    uint32_t                      stpPassthroughBufferLength = UINT32_C(0);
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));

    if (!stpInputs || !stpOutputs || !stpInputs->commandFIS ||
        !stpOutputs->statusFIS) // NOTE: other validation is done below.
    {
        return BAD_PARAMETER;
    }
    stpPassthroughBufferLength = sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER) + stpInputs->dataLength;
    stpPassthrough             = C_CAST(PCSMI_SAS_STP_PASSTHRU_BUFFER,
                                        safe_calloc_aligned(stpPassthroughBufferLength, sizeof(uint8_t), sizeof(void*)));
    if (!stpPassthrough)
    {
        return MEMORY_FAILURE;
    }

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = stpPassthrough;
    ioIn.ioctlBufferSize  = stpPassthroughBufferLength;
    ioIn.dataLength       = C_CAST(uint32_t, stpPassthroughBufferLength - sizeof(IOCTL_HEADER));
    ioIn.ioctlCode        = CC_CSMI_SAS_STP_PASSTHRU;
    // ioIn.ioctlDirection = CSMI_SAS_DATA_READ;//This is set below, however it may only need to be set one way....will
    // only knwo when testing on linux since this is used there.
    ioIn.timeoutInSeconds = stpInputs->timeoutSeconds;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;
    ioOut.ioctlTimer   = stpOutputs->stpTimer;

    // setup stp specific parameters
    // command FIS

    stpPassthrough->Parameters.bConnectionRate = stpInputs->connectionRate;
    safe_memcpy(stpPassthrough->Parameters.bDestinationSASAddress, 8, stpInputs->destinationSASAddress, 8);
    stpPassthrough->Parameters.bPhyIdentifier  = stpInputs->phyIdentifier;
    stpPassthrough->Parameters.bPortIdentifier = stpInputs->portIdentifier;
    stpPassthrough->Parameters.uDataLength     = stpInputs->dataLength;
    stpPassthrough->Parameters.uFlags          = stpInputs->flags;
    safe_memcpy(stpPassthrough->Parameters.bCommandFIS, 20, stpInputs->commandFIS, H2D_FIS_LENGTH);

    if (stpPassthrough->Parameters.uFlags & CSMI_SAS_STP_WRITE)
    {
        ioIn.ioctlDirection = CSMI_SAS_DATA_WRITE;
        if (stpInputs->ptrData)
        {
            safe_memcpy(stpPassthrough->bDataBuffer, stpInputs->dataLength, stpInputs->ptrData, stpInputs->dataLength);
        }
        else
        {
            safe_free_aligned_core(C_CAST(void**, &stpPassthrough));
            return BAD_PARAMETER;
        }
    }
    else
    {
        ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI STP Passthrough\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(stpPassthrough->IoctlHeader.ReturnCode);
        if (stpPassthrough->IoctlHeader.ReturnCode == CSMI_SAS_SCSI_EMULATION)
        {
            // this is a special case to say "retry with SSP and a CDB".
            stpOutputs->retryAsSSPPassthrough = true;
        }
        // if (sspPassthrough->IoctlHeader.ReturnCode == CSMI_SAS_STATUS_SUCCESS)

        if (stpPassthrough->Parameters.uFlags & CSMI_SAS_STP_READ)
        {
            // clear read data ptr first
            safe_memset(stpInputs->ptrData, stpInputs->dataLength, 0, stpInputs->dataLength);
            if (stpPassthrough->Status.uDataBytes)
            {
                // copy what we got
                safe_memcpy(stpInputs->ptrData, stpInputs->dataLength, stpPassthrough->bDataBuffer,
                            M_Min(stpInputs->dataLength, stpPassthrough->Status.uDataBytes));
            }
        }

        // copy back result
        safe_memcpy(stpOutputs->statusFIS, stpOutputs->statusFISLen, stpPassthrough->Status.bStatusFIS, D2H_FIS_LENGTH);

        if (stpOutputs->scrPtr)
        {
            // if the caller allocated memory for the SCR data, then copy it back for them
            safe_memcpy(stpOutputs->scrPtr, stpOutputs->scrLen * sizeof(uint32_t), stpPassthrough->Status.uSCR,
                        16 * sizeof(uint32_t));
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI STP Passthrough\n", ret);
    }

    safe_free_aligned_core(C_CAST(void**, &stpPassthrough));

    return ret;
}

static void print_CSMI_SATA_Signature(PCSMI_SAS_SATA_SIGNATURE signature)
{
    if (signature)
    {
        printf("\n====CSMI SATA Signature====\n");
        printf("\tPhy Identifier: %" CPRIu8 "\n", signature->bPhyIdentifier);
        printf("\tSignature FIS:\n");
        print_FIS(signature->bSignatureFIS, 20);
        printf("\n");
    }
}

// TODO: consider using a pointer to a FIS to fill in on completion instead...
eReturnValues csmi_Get_SATA_Signature(CSMI_HANDLE                     deviceHandle,
                                      uint32_t                        controllerNumber,
                                      PCSMI_SAS_SATA_SIGNATURE_BUFFER sataSignatureBuffer,
                                      uint8_t                         phyIdentifier,
                                      eVerbosityLevels                verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(sataSignatureBuffer, sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER), 0, sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = sataSignatureBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_SATA_SIGNATURE;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    sataSignatureBuffer->Signature.bPhyIdentifier = phyIdentifier;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get SATA Signature\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(sataSignatureBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_SATA_Signature(&sataSignatureBuffer->Signature);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get SATA Signature\n", ret);
    }

    return ret;
}

static void print_CSMI_Get_SCSI_Address(PCSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddress)
{
    if (scsiAddress)
    {
        printf("\n====CSMI Get SCSI Address====\n");
        printf("\tProvided SAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
               "%02" CPRIX8 "%02" CPRIX8 "h\n",
               scsiAddress->bSASAddress[0], scsiAddress->bSASAddress[1], scsiAddress->bSASAddress[2],
               scsiAddress->bSASAddress[3], scsiAddress->bSASAddress[4], scsiAddress->bSASAddress[5],
               scsiAddress->bSASAddress[6], scsiAddress->bSASAddress[7]);
        printf("\tProvided SAS Lun: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
               "%02" CPRIX8 "%02" CPRIX8 "h\n",
               scsiAddress->bSASLun[0], scsiAddress->bSASLun[1], scsiAddress->bSASLun[2], scsiAddress->bSASLun[3],
               scsiAddress->bSASLun[4], scsiAddress->bSASLun[5], scsiAddress->bSASLun[6], scsiAddress->bSASLun[7]);
        printf("\tHost Index: %" CPRIu8 "\n", scsiAddress->bHostIndex);
        printf("\tPath ID: %" CPRIu8 "\n", scsiAddress->bPathId);
        printf("\tTarget ID: %" CPRIu8 "\n", scsiAddress->bTargetId);
        printf("\tLUN: %" CPRIu8 "\n", scsiAddress->bLun);
    }
}

eReturnValues csmi_Get_SCSI_Address(CSMI_HANDLE                       deviceHandle,
                                    uint32_t                          controllerNumber,
                                    PCSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddressBuffer,
                                    uint8_t                           sasAddress[8],
                                    uint8_t                           lun[8],
                                    eVerbosityLevels                  verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(scsiAddressBuffer, sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER), 0,
                sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = scsiAddressBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_SCSI_ADDRESS;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    safe_memcpy(scsiAddressBuffer->bSASAddress, 8, sasAddress, 8);
    safe_memcpy(scsiAddressBuffer->bSASLun, 8, lun, 8);

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get SCSI Address\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(scsiAddressBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Get_SCSI_Address(scsiAddressBuffer);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get SCSI Address\n", ret);
    }

    return ret;
}

static void print_CSMI_Device_Address(PCSMI_SAS_GET_DEVICE_ADDRESS_BUFFER address)
{
    if (address)
    {
        printf("\n====CSMI Get Device Address====\n");
        printf("\tProvided Host Index: %" CPRIu8 "\n", address->bHostIndex);
        printf("\tProvided Path ID: %" CPRIu8 "\n", address->bPathId);
        printf("\tProvided Target ID: %" CPRIu8 "\n", address->bTargetId);
        printf("\tProvided LUN: %" CPRIu8 "\n", address->bLun);
        printf("\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
               "%02" CPRIX8 "h\n",
               address->bSASAddress[0], address->bSASAddress[1], address->bSASAddress[2], address->bSASAddress[3],
               address->bSASAddress[4], address->bSASAddress[5], address->bSASAddress[6], address->bSASAddress[7]);
        printf("\tSAS Lun: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
               "%02" CPRIX8 "h\n",
               address->bSASLun[0], address->bSASLun[1], address->bSASLun[2], address->bSASLun[3], address->bSASLun[4],
               address->bSASLun[5], address->bSASLun[6], address->bSASLun[7]);
    }
}

eReturnValues csmi_Get_Device_Address(CSMI_HANDLE                         deviceHandle,
                                      uint32_t                            controllerNumber,
                                      PCSMI_SAS_GET_DEVICE_ADDRESS_BUFFER deviceAddressBuffer,
                                      uint8_t                             hostIndex,
                                      uint8_t                             path,
                                      uint8_t                             target,
                                      uint8_t                             lun,
                                      eVerbosityLevels                    verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(deviceAddressBuffer, sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER), 0,
                sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = deviceAddressBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_DEVICE_ADDRESS;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    deviceAddressBuffer->bHostIndex = hostIndex;
    deviceAddressBuffer->bPathId    = path;
    deviceAddressBuffer->bTargetId  = target;
    deviceAddressBuffer->bLun       = lun;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Device Address\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ret == SUCCESS && ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(deviceAddressBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Device_Address(deviceAddressBuffer);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Device Address\n", ret);
    }

    return ret;
}

// TODO: SAS Task management function (can be used for a few things, but hard reset is most interesting
// CC_CSMI_SAS_TASK_MANAGEMENT

static void print_CSMI_Connector_Info(PCSMI_SAS_CONNECTOR_INFO_BUFFER connectorInfo)
{
    if (connectorInfo)
    {
        // need to loop through, but only print out non-zero structures since this data doesn't give us a count.
        // It is intended to be used alongside the phy info data which does provide a count, but we don't want to be
        // passing that count in for this right now.
        printf("\n====CSMI Connector Info====\n");
        for (uint8_t iter = UINT8_C(0); iter < 32; ++iter)
        {
            if (is_Empty(&connectorInfo->Reference[iter], 36))
            {
                break;
            }
            printf("\t----Connector %" CPRIu8 "----\n", iter);
            printf("\t\tConnector: %s\n", connectorInfo->Reference[iter].bConnector);
            printf("\t\tPinout: \n");
            if (connectorInfo->Reference[iter].uPinout == 0)
            {
                printf("\t\t\tNot Reported\n");
            }
            else
            {
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_UNKNOWN)
                {
                    printf("\t\t\tUnknown\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8482)
                {
                    printf("\t\t\tSFF-8482\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8470_LANE_1)
                {
                    printf("\t\t\tSFF-8470 - Lane 1\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8470_LANE_2)
                {
                    printf("\t\t\tSFF-8470 - Lane 2\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8470_LANE_3)
                {
                    printf("\t\t\tSFF-8470 - Lane 3\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8470_LANE_4)
                {
                    printf("\t\t\tSFF-8470 - Lane 4\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8484_LANE_1)
                {
                    printf("\t\t\tSFF-8484 - Lane 1\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8484_LANE_2)
                {
                    printf("\t\t\tSFF-8484 - Lane 2\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8484_LANE_3)
                {
                    printf("\t\t\tSFF-8484 - Lane 3\n");
                }
                if (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_SFF_8484_LANE_4)
                {
                    printf("\t\t\tSFF-8484 - Lane 4\n");
                }
                if ((connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_1) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_2) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_3) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_4) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_5) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_6) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_7) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_8) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_9) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_A) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_B) ||
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_C))
                {
                    printf("\t\t\tReserved - %08" CPRIX32 "\n", connectorInfo->Reference[iter].uPinout);
                }
            }
            printf("\t\tLocation: \n");
            if (connectorInfo->Reference[iter].bLocation == 0)
            {
            }
            else
            {
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_UNKNOWN)
                {
                    printf("\t\t\tUnknown\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_INTERNAL)
                {
                    printf("\t\t\tInternal\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_EXTERNAL)
                {
                    printf("\t\t\tExternal\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_SWITCHABLE)
                {
                    printf("\t\t\tSwitchable\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_AUTO)
                {
                    printf("\t\t\tAuto\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_NOT_PRESENT)
                {
                    printf("\t\t\tNot Present\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_RESERVED)
                {
                    printf("\t\t\tReserved\n");
                }
                if (connectorInfo->Reference[iter].bLocation & CSMI_SAS_CON_NOT_CONNECTED)
                {
                    printf("\t\t\tNot Connected\n");
                }
            }
        }
        printf("\n");
    }
}

eReturnValues csmi_Get_Connector_Info(CSMI_HANDLE                     deviceHandle,
                                      uint32_t                        controllerNumber,
                                      PCSMI_SAS_CONNECTOR_INFO_BUFFER connectorInfoBuffer,
                                      eVerbosityLevels                verbosity)
{
    eReturnValues ret = SUCCESS;
    csmiIOin      ioIn;
    csmiIOout     ioOut;
    safe_memset(&ioIn, sizeof(csmiIOin), 0, sizeof(csmiIOin));
    safe_memset(&ioOut, sizeof(csmiIOout), 0, sizeof(csmiIOout));
    safe_memset(connectorInfoBuffer, sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER), 0, sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER));

    // setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle     = deviceHandle;
    ioIn.ioctlBuffer      = connectorInfoBuffer;
    ioIn.ioctlBufferSize  = sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER);
    ioIn.dataLength       = sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode        = CC_CSMI_SAS_GET_CONNECTOR_INFO;
    ioIn.ioctlDirection   = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    safe_memcpy(ioIn.ioctlSignature, 8, CSMI_SAS_SIGNATURE, safe_strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Connector Info\n");
    }
    // issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    // validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(connectorInfoBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_Connector_Info(connectorInfoBuffer);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        print_Return_Enum("CSMI Get Connector Info\n", ret);
    }

    return ret;
}

static eReturnValues csmi_Get_Basic_Info(CSMI_HANDLE                   deviceHandle,
                                         uint32_t                      controllerNumber,
                                         PCSMI_SAS_DRIVER_INFO_BUFFER  driverInfo,
                                         PCSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig,
                                         PCSMI_SAS_CNTLR_STATUS_BUFFER controllerStatus,
                                         eVerbosityLevels              verbosity)
{
    eReturnValues ret = SUCCESS;
    if (deviceHandle != CSMI_INVALID_HANDLE && driverInfo && controllerConfig)
    {
        if (SUCCESS != csmi_Get_Driver_Info(deviceHandle, controllerNumber, driverInfo, verbosity))
        {
            ret = FAILURE;
        }
        if (SUCCESS != csmi_Get_Controller_Configuration(deviceHandle, controllerNumber, controllerConfig, verbosity))
        {
            ret = FAILURE;
        }
        if (SUCCESS != csmi_Get_Controller_Status(deviceHandle, controllerNumber, controllerStatus, verbosity))
        {
            ret = FAILURE;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// Function to check for CSMI IO support on non-RAID devices (from Windows mostly since this can get around Win
// passthrough restrictions in some cases)
// TODO: need a way to make sure we are only checking this on drives not configured as a raid member.
bool handle_Supports_CSMI_IO(CSMI_HANDLE deviceHandle, eVerbosityLevels verbosity)
{
    bool csmiSupported = false;
    if (deviceHandle != CSMI_INVALID_HANDLE)
    {
        // Send the following 2 IOs to check if CSMI passthrough is supported on a device that is NOT a RAID device,
        // meaning it is not configured as a member of a RAID. eReturnValues csmi_Get_Driver_Info(CSMI_HANDLE
        // deviceHandle, uint32_t controllerNumber, PCSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer, eVerbosityLevels
        // verbosity) eReturnValues csmi_Get_Controller_Configuration(CSMI_HANDLE deviceHandle, uint32_t
        // controllerNumber, PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer, eVerbosityLevels verbosity)
        CSMI_SAS_DRIVER_INFO_BUFFER  driverInfo;
        CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
        CSMI_SAS_CNTLR_STATUS_BUFFER controllerStatus;
        safe_memset(&driverInfo, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER), 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
        safe_memset(&controllerConfig, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER), 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
        safe_memset(&controllerStatus, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER), 0, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER));
        if (SUCCESS ==
            csmi_Get_Basic_Info(deviceHandle, 0, &driverInfo, &controllerConfig, &controllerStatus, verbosity))
        {
            csmiSupported = true;
        }
    }
    return csmiSupported;
}

#    if defined(_WIN32)
bool device_Supports_CSMI_With_RST(tDevice* device)
{
    bool csmiWithRSTSupported = false;
    if (handle_Supports_CSMI_IO(device->os_info.scsiSRBHandle, device->deviceVerbosity))
    {
        // check for FWDL IOCTL support. If this works, then the Intel Additions are supported.
#        if defined(ENABLE_INTEL_RST)
        if (supports_Intel_Firmware_Download(device))
        {
            csmiWithRSTSupported = true;
        }
#        endif // ENABLE_INTEL_RST
    }
    return csmiWithRSTSupported;
}
#    endif //_WIN32
// This is really only here for Windows, but could be used under Linux if you wanted to use CSMI instead of SGIO, but
// that really is unnecessary controller number is to target the CSMI IOCTL inputs on non-windows. hostController is a
// SCSI address number, which may or may not be different...If these end up the same on Linux, this should be update to
// remove the duplicate parameters. If not, delete part of this comment. NOTE: this does not handle Intel NVMe devices
// in JBOD mode right now. These devices will be handled separately from this function which focuses on SATA/SAS
eReturnValues jbod_Setup_CSMI_Info(M_ATTR_UNUSED CSMI_HANDLE deviceHandle,
                                   tDevice*                  device,
                                   uint8_t                   controllerNumber,
                                   uint8_t                   hostController,
                                   uint8_t                   pathidBus,
                                   uint8_t                   targetID,
                                   uint8_t                   lun)
{
    eReturnValues ret              = SUCCESS;
    device->os_info.csmiDeviceData = M_REINTERPRET_CAST(ptrCsmiDeviceInfo, safe_calloc(1, sizeof(csmiDeviceInfo)));
    if (device->os_info.csmiDeviceData)
    {
#    if defined(_WIN32)
        device->os_info.csmiDeviceData->csmiDevHandle = device->os_info.scsiSRBHandle;
#    else  //_WIN32
        device->os_info.csmiDeviceData->csmiDevHandle = device->os_info.fd;
#    endif //_WIN32
        device->os_info.csmiDeviceData->controllerNumber    = controllerNumber;
        device->os_info.csmiDeviceData->csmiDeviceInfoValid = true;
        // Read controller info, driver info, get phy info for this device too...in non-RAID mode, Windows scsi address
        // should match the csmi scsi address
        CSMI_SAS_DRIVER_INFO_BUFFER  driverInfo;
        CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
        CSMI_SAS_CNTLR_STATUS_BUFFER controllerStatus;
        safe_memset(&driverInfo, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER), 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
        safe_memset(&controllerConfig, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER), 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
        safe_memset(&controllerStatus, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER), 0, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER));
#    if defined(CSMI_DEBUG)
        printf("JSCI: Getting driver Info, controller config, and controller status\n");
#    endif // CSMI_DEBUG
        if (SUCCESS == csmi_Get_Basic_Info(device->os_info.csmiDeviceData->csmiDevHandle, 0, &driverInfo,
                                           &controllerConfig, &controllerStatus, device->deviceVerbosity))
        {
            bool gotSASAddress                               = false;
            device->os_info.csmiDeviceData->csmiMajorVersion = driverInfo.Information.usCSMIMajorRevision;
            device->os_info.csmiDeviceData->csmiMinorVersion = driverInfo.Information.usCSMIMinorRevision;
            device->os_info.csmiDeviceData->controllerFlags  = controllerConfig.Configuration.uControllerFlags;
            device->os_info.csmiDeviceData->lun              = lun;
            // set CSMI scsi address based on what was passed in since it may be needed later
            device->os_info.csmiDeviceData->scsiAddress.hostIndex = hostController;
            device->os_info.csmiDeviceData->scsiAddress.pathId    = pathidBus;
            device->os_info.csmiDeviceData->scsiAddress.targetId  = targetID;
            device->os_info.csmiDeviceData->scsiAddress.lun       = lun;
            device->os_info.csmiDeviceData->scsiAddressValid      = true;

            // before continuing, check if this is a known driver to work around known issues.
            device->os_info.csmiDeviceData->csmiKnownDriverType = get_Known_CSMI_Driver_Type(&driverInfo.Information);

            // get SAS Address
#    if defined(CSMI_DEBUG)
            printf("JSCI: Getting SAS Address\n");
#    endif // CSMI_DEBUG
            CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER addressBuffer;
            // skip this on HPCISS as it causes a hang for some unknown reason.
            if (device->os_info.csmiDeviceData->csmiKnownDriverType != CSMI_DRIVER_HPCISS &&
                SUCCESS == csmi_Get_Device_Address(device->os_info.csmiDeviceData->csmiDevHandle,
                                                   device->os_info.csmiDeviceData->controllerNumber, &addressBuffer,
                                                   hostController, pathidBus, targetID, lun, device->deviceVerbosity))
            {
                safe_memcpy(device->os_info.csmiDeviceData->sasAddress, 8, addressBuffer.bSASAddress, 8);
                safe_memcpy(device->os_info.csmiDeviceData->sasLUN, 8, addressBuffer.bSASLun, 8);
                gotSASAddress = true;
#    if defined(CSMI_DEBUG)
                printf("JSCI: Successfully got SAS address\n");
#    endif // CSMI_DEBUG
            }
            else
            {
                // Need to figure out the device another way to get the SAS address IF this is a SAS drive. If this is
                // SATA, this is less important overall unless it's on a SAS HBA, but a driver should be able to handle
                // this call already. The only other place to find a SAS Address is in the RAID Config, but the drive
                // may or may not be listed there...try it anyways...if we still don't find it, we'll only get the
                // sasAddress from the phy info. RAID info/config will only be available if it's a SAS or SATA RAID
                // capable controller
#    if defined(CSMI_DEBUG)
                printf("JSCI: Using alternate method to get SAS address. Checking for compatible controller flags "
                       "first.\n");
#    endif // CSMI_DEBUG
                if (controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID ||
                    controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID ||
                    controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY)
                {
                    CSMI_SAS_RAID_INFO_BUFFER raidInfo;
#    if defined(CSMI_DEBUG)
                    printf("JSCI: Getting RAID info\n");
#    endif // CSMI_DEBUG
                    if (SUCCESS == csmi_Get_RAID_Info(device->os_info.csmiDeviceData->csmiDevHandle, 0, &raidInfo,
                                                      device->deviceVerbosity))
                    {
#    if defined(CSMI_DEBUG)
                        printf("JSCI: Checking RAID sets. Number of RAID sets: %" CPRIu32 "\n",
                               raidInfo.Information.uNumRaidSets);
#    endif // CSMI_DEBUG
                        for (uint32_t raidSet = UINT32_C(0);
                             !gotSASAddress && raidSet < raidInfo.Information.uNumRaidSets; ++raidSet)
                        {
                            // with the RAID info, now we can allocate and read the RAID config
                            uint32_t raidConfigLength = C_CAST(
                                uint32_t, sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) +
                                              (raidInfo.Information.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES)));
                            PCSMI_SAS_RAID_CONFIG_BUFFER raidConfig = M_REINTERPRET_CAST(
                                PCSMI_SAS_RAID_CONFIG_BUFFER, safe_calloc(raidConfigLength, sizeof(uint8_t)));
                            if (raidConfig)
                            {
#    if defined(CSMI_DEBUG)
                                printf("JSCI: Getting RAID config for RAID set %" PRIu32 "\n", raidSet);
#    endif // CSMI_DEBUG
                                if (SUCCESS == csmi_Get_RAID_Config(device->os_info.csmiDeviceData->csmiDevHandle, 0,
                                                                    raidConfig, raidConfigLength, raidSet,
                                                                    CSMI_SAS_RAID_DATA_DRIVES, device->deviceVerbosity))
                                {
#    if defined(CSMI_DEBUG)
                                    printf("JSCI: Checking drive count (%" CPRIu8 ")\n",
                                           raidConfig->Configuration.bDriveCount);
#    endif // CSMI_DEBUG
                                    if (raidConfig->Configuration.bDriveCount < CSMI_SAS_RAID_DRIVE_COUNT_TOO_BIG)
                                    {
                                        for (uint32_t driveIter = UINT32_C(0);
                                             !gotSASAddress && driveIter < raidInfo.Information.uMaxDrivesPerSet &&
                                             driveIter < raidConfig->Configuration.bDriveCount;
                                             ++driveIter)
                                        {
                                            bool driveInfoValid =
                                                true; // for version 81 and earlier, assume this is true.
#    if defined(CSMI_DEBUG)
                                            printf("JSCI: Checking CSMI Revision: %" CPRIu16 ".%" CPRIu16 "\n",
                                                   driverInfo.Information.usCSMIMajorRevision,
                                                   driverInfo.Information.usCSMIMinorRevision);
#    endif // CSMI_DEBUG
                                            if (driverInfo.Information.usCSMIMajorRevision > 0 ||
                                                driverInfo.Information.usCSMIMinorRevision > 81)
                                            {
#    if defined(CSMI_DEBUG)
                                                printf("JSCI: CSMI Minor rev > 81, so checking bDataType\n");
#    endif // CSMI_DEBUG
                                                switch (raidConfig->Configuration.bDataType)
                                                {
                                                case CSMI_SAS_RAID_DATA_DRIVES:
                                                    break;
                                                case CSMI_SAS_RAID_DATA_DEVICE_ID:
                                                case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
                                                default:
                                                    driveInfoValid = false;
                                                    break;
                                                }
                                            }
                                            if (driveInfoValid)
                                            {
#    if defined(CSMI_DEBUG)
                                                printf("JSCI: Checking for matching device information\n");
#    endif // CSMI_DEBUG
                                                if (strstr(C_CAST(const char*,
                                                                  raidConfig->Configuration.Drives[driveIter].bModel),
                                                           device->drive_info.product_identification) &&
                                                    strstr(
                                                        C_CAST(
                                                            const char*,
                                                            raidConfig->Configuration.Drives[driveIter].bSerialNumber),
                                                        device->drive_info.serialNumber))
                                                {
                                                    // Found the match!!!
                                                    safe_memcpy(device->os_info.csmiDeviceData->sasAddress, 8,
                                                                raidConfig->Configuration.Drives[driveIter].bSASAddress,
                                                                8);
                                                    safe_memcpy(device->os_info.csmiDeviceData->sasLUN, 8,
                                                                raidConfig->Configuration.Drives[driveIter].bSASLun, 8);
                                                    // Intel drivers are known to support a SASAddress that is all
                                                    // zeroes as a valid address, so trust this result for these drivers
                                                    // -TJE
                                                    if (is_Intel_Driver(
                                                            device->os_info.csmiDeviceData->csmiKnownDriverType) ||
                                                        !is_Empty(device->os_info.csmiDeviceData->sasAddress,
                                                                  8)) // an empty (all zeros) SAS address means it is
                                                                      // not valid, such as on a SATA controller. NOTE:
                                                                      // Some SATA controllers will fill this is, but
                                                                      // this is not guaranteed-TJE
                                                    {
#    if defined(CSMI_DEBUG)
                                                        printf("JSCI: Found matching drive data. Can send CSMI IOs in "
                                                               "addition to normal system IOs\n");
#    endif // CSMI_DEBUG
                                                        gotSASAddress = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                safe_free_csmi_raid_config(&raidConfig);
                            }
                        }
                    }
                }
            }

            // Attempt to read phy info to get phy identifier and port identifier data...this may not work on RST NVMe
            // if this code is hit...that's OK since we are unlikely to see that. NOTE: AMD's rcraid driver seems to
            // treat non-raid drives slightly different in the phy info output. In the case I observed, the raid drives
            // were in the phy info, but the
            //       non-RAID drive was not shown here at all. However in the RAID IOCTLs, it does show as a separate
            //       single drive raid (but no MN or SN info to match to it). So it does not appear possible to issue a
            //       CSMI passthrough command to non-raid drives. This is completely opposite of what the Intel drivers
            //       do. Intel's drivers show every drive attached, RAID or non-RAID in the phy info. This may be
            //       something we want to detect in the future to reduce the number of IOCTLs sent, but for now, it
            //       works ok. We can optimize this more later. -TJE
            CSMI_SAS_PHY_INFO_BUFFER phyInfo;
            bool                     foundPhyInfo = false;
#    if defined(CSMI_DEBUG)
            printf("JSCI: Getting Phy info\n");
#    endif // CSMI_DEBUG
            if (SUCCESS == csmi_Get_Phy_Info(device->os_info.csmiDeviceData->csmiDevHandle,
                                             device->os_info.csmiDeviceData->controllerNumber, &phyInfo,
                                             device->deviceVerbosity))
            {
                // TODO: Is there a better way to match against the port identifier with the address information
                // provided? match to attached port or phy identifier???
#    if defined(CSMI_DEBUG)
                printf("JSCI: Checking Phy info for a match. Number of phys: %" CPRIu8 "\n",
                       phyInfo.Information.bNumberOfPhys);
#    endif // CSMI_DEBUG
                for (uint8_t phyIter = UINT8_C(0), physFound = UINT8_C(0);
                     !foundPhyInfo && physFound < phyInfo.Information.bNumberOfPhys && phyIter < UINT8_C(32); ++phyIter)
                {
                    if (phyInfo.Information.Phy[phyIter].Attached.bDeviceType != CSMI_SAS_NO_DEVICE_ATTACHED)
                    {
                        ++physFound;
#    if defined(CSMI_DEBUG)
                        printf("JSCI: Checking SAS address match\n");
#    endif // CSMI_DEBUG
                        if (gotSASAddress && memcmp(phyInfo.Information.Phy[phyIter].Attached.bSASAddress,
                                                    device->os_info.csmiDeviceData->sasAddress, 8) == 0)
                        {
                            // Found it. We can save the portID and phyID to use to issue commands :)
                            device->os_info.csmiDeviceData->portIdentifier =
                                phyInfo.Information.Phy[phyIter].bPortIdentifier;
                            device->os_info.csmiDeviceData->phyIdentifier =
                                phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier;
                            device->os_info.csmiDeviceData->portProtocol =
                                phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol;
                            foundPhyInfo = true;
#    if defined(CSMI_DEBUG)
                            printf("JSCI: Found a matching SAS address!\n");
#    endif // CSMI_DEBUG
                        }
                        else if (!gotSASAddress)
                        {
#    if defined(CSMI_DEBUG)
                            printf("JSCI: No SAS address to match. Attempting passthrough commands to match\n");
#    endif // CSMI_DEBUG
                            ScsiIoCtx csmiPTCmd;
                            safe_memset(&csmiPTCmd, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
                            csmiPTCmd.device        = device;
                            csmiPTCmd.timeout       = 15;
                            csmiPTCmd.direction     = XFER_DATA_IN;
                            csmiPTCmd.psense        = device->drive_info.lastCommandSenseData;
                            csmiPTCmd.senseDataSize = SPC3_SENSE_LEN;
                            // Don't have a SAS Address to match to, so we need to send an identify or inquiry to the
                            // device to see if it is the same MN, then check the SN. NOTE: This will not work if we
                            // don't already know the sasLUN for SAS drives. SATA will be ok though.
                            device->os_info.csmiDeviceData->portIdentifier =
                                phyInfo.Information.Phy[phyIter].bPortIdentifier;
                            device->os_info.csmiDeviceData->phyIdentifier =
                                phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier;
                            device->os_info.csmiDeviceData->portProtocol =
                                phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol;
                            safe_memcpy(&device->os_info.csmiDeviceData->sasAddress[0], 8,
                                        phyInfo.Information.Phy[phyIter].Attached.bSASAddress, 8);
                            // Attempt passthrough command and compare identifying data.
                            // for this to work, SCSIIoCTX structure must be manually defined for what we want to do
                            // right now and call the CSMI IO directly...not great, but don't want to have other force
                            // flags elsewhere at the moment- TJE
                            if (phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol &
                                    CSMI_SAS_PROTOCOL_SATA ||
                                phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol & CSMI_SAS_PROTOCOL_STP)
                            {
                                // ATA identify
                                DECLARE_ZERO_INIT_ARRAY(uint8_t, identifyData, 512);
                                ataPassthroughCommand identify =
                                    create_ata_pio_in_cmd(device, ATA_IDENTIFY, false, 1, identifyData, 512);
                                csmiPTCmd.pdata       = identifyData;
                                csmiPTCmd.dataLength  = 512;
                                csmiPTCmd.pAtaCmdOpts = &identify;
#    if defined(CSMI_DEBUG)
                                printf("JSCI: Detected SATA protocol. Attempting Identify CMD\n");
#    endif // CSMI_DEBUG
                                if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                {
                                    // compare MN and SN...if match, then we have found the drive!
                                    DECLARE_ZERO_INIT_ARRAY(char, ataMN, ATA_IDENTIFY_MN_LENGTH + 1);
                                    DECLARE_ZERO_INIT_ARRAY(char, ataSN, ATA_IDENTIFY_SN_LENGTH + 1);
                                    DECLARE_ZERO_INIT_ARRAY(char, ataFW, ATA_IDENTIFY_FW_LENGTH + 1);
                                    fill_ATA_Strings_From_Identify_Data(identifyData, ataMN, ataSN, ataFW);

                                    // check for a match
#    if defined(CSMI_DEBUG)
                                    printf("JSCI: Identify Successful\n");
#    endif // CSMI_DEBUG
                                    if (strcmp(ataMN, device->drive_info.product_identification) == 0 &&
                                        strcmp(ataSN, device->drive_info.serialNumber) == 0)
                                    {
                                        // found a match!
#    if defined(CSMI_DEBUG)
                                        printf("JSCI: Found a matching MN/SN!\n");
#    endif // CSMI_DEBUG
                                        foundPhyInfo = true;
                                    }
                                }
                            }
                            else if (phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol &
                                     CSMI_SAS_PROTOCOL_SSP)
                            {
                                // SCSI Inquiry and read unit serial number VPD page
                                DECLARE_ZERO_INIT_ARRAY(uint8_t, inqData, 96);
                                DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
                                cdb[OPERATION_CODE] = INQUIRY_CMD;
                                /*if (evpd)
                                {
                                    cdb[1] |= BIT0;
                                }*/
                                cdb[2] = 0; // pageCode;
                                cdb[3] = M_Byte1(96);
                                cdb[4] = M_Byte0(96);
                                cdb[5] = 0; // control

                                csmiPTCmd.cdbLength = CDB_LEN_6;
                                safe_memcpy(csmiPTCmd.cdb, SCSI_IO_CTX_MAX_CDB_LEN, cdb, 6);
                                csmiPTCmd.dataLength = 96;
                                csmiPTCmd.pdata      = inqData;
#    if defined(CSMI_DEBUG)
                                printf("JSCI: Detected SSP protocol. Attempting Inquiry\n");
#    endif // CSMI_DEBUG
                                if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                {
                                    // TODO: If this is a multi-LUN device, this won't currently work and it may not be
                                    // possible to make this work if we got to this case in the first place. HOPEFULLY
                                    // the other CSMI translation IOCTLs just work and this is unnecessary. - TJE If MN
                                    // matches, send inquiry to unit SN vpd page to confirm we have a matching SN
                                    DECLARE_ZERO_INIT_ARRAY(char, inqVendor, 9);
                                    DECLARE_ZERO_INIT_ARRAY(char, inqProductID, 17);
                                    // DECLARE_ZERO_INIT_ARRAY(char, inqProductRev, 5);
                                    // copy the strings
                                    safe_memcpy(inqVendor, 9, &inqData[8], 8);
                                    safe_memcpy(inqProductID, 17, &inqData[16], 16);
                                    // safe_memcpy(inqProductRev, 5, &inqData[32], 4);
                                    // remove whitespace
                                    remove_Leading_And_Trailing_Whitespace_Len(inqVendor, 9);
                                    remove_Leading_And_Trailing_Whitespace_Len(inqProductID, 17);
                                    // remove_Leading_And_Trailing_Whitespace_Len(inqProductRev, 5);
                                    // compare to tDevice
#    if defined(CSMI_DEBUG)
                                    printf("JSCI: Inquiry Successful\n");
#    endif // CSMI_DEBUG
                                    if (strcmp(inqVendor, device->drive_info.T10_vendor_ident) == 0 &&
                                        strcmp(inqProductID, device->drive_info.product_identification) == 0)
                                    {
#    if defined(CSMI_DEBUG)
                                        printf("JSCI: MN/Vendor match. Checking SN\n");
#    endif // CSMI_DEBUG
           // now read the unit SN VPD page since this matches so far that way we can compare the serial number. Not
           // checking SCSI 2 since every SAS drive *SHOULD* support this.
                                        safe_memset(inqData, 96, 0, 96);
                                        // change CDB to read unit SN page
                                        cdb[1] |= BIT0;
                                        cdb[2] = UNIT_SERIAL_NUMBER;
#    if defined(CSMI_DEBUG)
                                        printf("JSCI: Requesting Unit SN page\n");
#    endif // CSMI_DEBUG
                                        if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                        {
                                            // check the SN
                                            uint16_t serialNumberLength =
                                                M_Min(M_BytesTo2ByteValue(inqData[2], inqData[3]), 96) + 1;
                                            char* serialNumber = M_REINTERPRET_CAST(
                                                char*, safe_calloc(serialNumberLength, sizeof(char)));
                                            if (serialNumber)
                                            {
                                                safe_memcpy(
                                                    serialNumber, serialNumberLength, &inqData[4],
                                                    serialNumberLength -
                                                        1); // minus 1 to leave null terminator in tact at the end
                                                if (strcmp(serialNumber, device->drive_info.serialNumber) == 0)
                                                {
#    if defined(CSMI_DEBUG)
                                                    printf("JSCI: Found a matching SN!\n");
#    endif // CSMI_DEBUG
           // found a match!
                                                    foundPhyInfo = true;
                                                    // TODO: To help prevent multiport or multi-lun issues, we should
                                                    // REALLY check the device identification VPD page, but that can be
                                                    // a future enhancement
                                                }
                                                safe_free(&serialNumber);
                                            }
                                        }
                                        // else...catastrophic failure? Not sure what to do here since this should be
                                        // really rare to begin with.
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
#    if defined(CSMI_DEBUG)
                        printf("JSCI: Skipping phy %" PRIu8 " as it is marked no device attached\n", phyIter);
#    endif // CSMI_DEBUG
                    }
                }
            }

            if (!foundPhyInfo)
            {
#    if defined(CSMI_DEBUG)
                printf("JSCI: No phy info. Not enough information to use CSMI passthrough\n");
#    endif // CSMI_DEBUG
           // We don't have enough information to use CSMI passthrough on this device. Free memory and return
           // NOT_SUPPORTED
                safe_free_csmi_dev_info(&device->os_info.csmiDeviceData);
                ret = NOT_SUPPORTED;
            }

#    if defined(_WIN32) && defined(ENABLE_INTEL_RST)
            // Check if Intel Driver and if FWDL IOs are supported or not. version 14.8+
            if (strncmp(C_CAST(const char*, driverInfo.Information.szName), "iaStor", 6) == 0)
            {
#        if defined(CSMI_DEBUG)
                printf("JSCI: Detected intel driver\n");
#        endif // CSMI_DEBUG
               // Intel driver, check for Additional IOCTLs by trying to read FWDL info
                if (supports_Intel_Firmware_Download(device))
                {
                    // No need to do anything here right now since the function above will fill in parameters as
                    // necessary.
#        if defined(CSMI_DEBUG)
                    printf("JSCI: Intel driver supports unique FWDL IOCTLs\n");
#        endif // CSMI_DEBUG
                }
            }
#    endif //_WIN32 && ENABLE_INTEL_RST
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
#    if defined(CSMI_DEBUG)
    printf("JSCI: Returning %d\n", ret);
#    endif // CSMI_DEBUG
    return ret;
}

//-----------------------------------------------------------------------------
//
//  close_CSMI_RAID_Device()
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
eReturnValues close_CSMI_RAID_Device(tDevice* device)
{
    DISABLE_NONNULL_COMPARE
    if (device != M_NULLPTR)
    {
#    if defined(_WIN32)
        CloseHandle(device->os_info.fd);
        device->os_info.last_error = GetLastError();
        safe_free_csmi_dev_info(&device->os_info.csmiDeviceData);
        device->os_info.last_error = 0;
        device->os_info.fd         = INVALID_HANDLE_VALUE;
#    else  //_WIN32
        if (close(device->os_info.fd))
        {
            device->os_info.last_error = errno;
        }
        else
        {
            device->os_info.last_error = 0;
        }
        device->os_info.fd = -1;
#    endif //_WIN32
        safe_free_csmi_dev_info(&device->os_info.csmiDeviceData);
        device->os_info.last_error = 0;
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
    RESTORE_NONNULL_COMPARE
}

static bool get_CSMI_Handle_Fields_From_Input(const char* filename,
                                              bool*       isIntelFormat,
                                              uint32_t*   field1,
                                              uint32_t*   field2,
                                              uint32_t*   field3,
                                              uint32_t*   field4,
                                              char**      nixbasehandle)
{
    if (filename && isIntelFormat && field1 && field2 && field3 && field4)
    {
        char* end = M_NULLPTR;
        // need to update str pointer as we scan the string, but not actually modifying data
        char* str = M_CONST_CAST(char*, filename);
        if (strstr(filename, "csmi:") == str) // must begin with this
        {
            str += safe_strlen("csmi:");
            unsigned long value = 0UL;
            if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
            {
                return false;
            }
            else
            {
                *field1 = C_CAST(uint32_t, value);
                end += 1; // move past next :
                // now check if end ptr begins with N to detect if intel format or not
                if (end[0] == 'N' && end[1] == ':')
                {
                    *isIntelFormat = true;
                    end += 2; // move past : after N
                }
                else
                {
                    *isIntelFormat = false;
                }
                str = end;
                if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
                {
                    return false;
                }
                else
                {
                    *field2 = C_CAST(uint32_t, value);
                    end += 1; // move past next :
                    str = end;
                    if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
                    {
                        return false;
                    }
                    else
                    {
                        *field3 = C_CAST(uint32_t, value);
                        end += 1; // move past next :
                        str = end;
                        if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
                        {
                            return false;
                        }
                        else
                        {
                            *field4 = C_CAST(uint32_t, value);
                            if (strcmp(end, "") == 0)
                            {
                                return true;
                            }
                            else
                            {
                                // Linux can have a string at the end.
                                // If this parameter was provided, duplicate it to pass out
                                if (nixbasehandle)
                                {
                                    if (0 == safe_strdup(nixbasehandle, end))
                                    {
                                        return true;
                                    }
                                    else
                                    {
                                        return false;
                                    }
                                }
                                else
                                {
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

// TODO: Accept SASAddress and SASLun inputs
eReturnValues get_CSMI_RAID_Device(const char* filename, tDevice* device)
{
    eReturnValues ret           = FAILURE;
    uint32_t      controllerNum = UINT32_C(0);
    uint32_t      portID        = UINT32_C(0);
    uint32_t      phyID         = UINT32_C(0);
    uint32_t      lun           = UINT32_C(0);
    // Need to open this handle and setup some information then fill in the device information.
    if (!(validate_Device_Struct(device->sanity)))
    {
#    if defined(CSMI_DEBUG)
        printf("GRD: Failure validating device struct\n");
#    endif // CSMI_DEBUG
        return LIBRARY_MISMATCH;
    }
    // set the handle name first...since the tokenizing below will break it apart
    safe_memcpy(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, filename, safe_strlen(filename));
    bool      intelNVMe   = false;
    uint32_t *intelPathID = &portID, *intelTargetID = &phyID, *intelLun = &lun;
    char*     baseHandle = M_NULLPTR;
    if (!get_CSMI_Handle_Fields_From_Input(filename, &intelNVMe, &controllerNum, &portID, &phyID, &lun, &baseHandle))
    {
#    if defined(CSMI_DEBUG)
        printf("GRD: Handle doesn't match std csmi format or Intel NVMe csmi format!\n");
#    endif // CSMI_DEBUG
        safe_free(&baseHandle);
        return BAD_PARAMETER;
    }
#    if defined(_WIN32)
    if (baseHandle && safe_strlen(baseHandle) > 0)
    {
        safe_free(&baseHandle);
        return BAD_PARAMETER;
    }
    else
    {
        int snprintfres = 0;
        if (intelNVMe)
        {
            snprintfres = snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH,
                                              CSMI_HANDLE_BASE_NAME ":%" PRIu32 ":N:%" PRIu32 ":%" PRIu32 ":%" PRIu32,
                                              controllerNum, portID, phyID, lun);
        }
        else
        {
            snprintfres = snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH,
                                              CSMI_HANDLE_BASE_NAME ":%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%" PRIu32,
                                              controllerNum, portID, phyID, lun);
        }
        if (snprintfres < 1 || snprintfres > OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH)
        {
            safe_free(&baseHandle);
            return BAD_PARAMETER;
        }
    }
#    else  //_WIN32
    M_USE_UNUSED(intelLun);
    M_USE_UNUSED(intelPathID);
    M_USE_UNUSED(intelTargetID);
    if (baseHandle && safe_strlen(baseHandle) > 0 && !intelNVMe)
    {
        int snprintfres =
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH,
                                CSMI_HANDLE_BASE_NAME ":%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%s",
                                controllerNum, portID, phyID, lun, baseHandle);
        if (snprintfres < 1 || snprintfres > OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH)
        {
            safe_free(&baseHandle);
            return BAD_PARAMETER;
        }
    }
    else
    {
        safe_free(&baseHandle);
        return BAD_PARAMETER;
    }
#    endif //_WIN32
    safe_free(&baseHandle);
#    if defined(CSMI_DEBUG)
    printf("GRD: Opening low-level device handle\n");
#    endif // CSMI_DEBUG
#    if defined(_WIN32)
    DECLARE_ZERO_INIT_ARRAY(TCHAR, device_name, CSMI_WIN_MAX_DEVICE_NAME_LENGTH);
    CONST TCHAR* ptrDeviceName = &device_name[0];
#        if IS_MSVC_VERSION(MSVC_2015)
    _stprintf_s(device_name, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("\\\\.\\SCSI") TEXT("%") TEXT(PRIu32) TEXT(":"),
                controllerNum);
#        else
    _stprintf_s(device_name, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("\\\\.\\SCSI") TEXT("%") TEXT("I32u") TEXT(":"),
                controllerNum);
#        endif //_MSC_VER && _MSC_VER < VS2015
    // lets try to open the device.
    device->os_info.fd = CreateFile(ptrDeviceName,
                                    GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#        if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                    FILE_FLAG_OVERLAPPED,
#        else  //! WINDOWS_DISABLE_OVERLAPPED
                                    0,
#        endif // WINDOWS_DISABLE_OVERLAPPED
                                    M_NULLPTR);
    // DWORD lastError = GetLastError();
    if (device->os_info.fd != INVALID_HANDLE_VALUE)
#    else  //_WIN32
    if (baseHandle != M_NULLPTR && (device->os_info.fd = open(baseHandle, O_RDWR | O_NONBLOCK)) >= 0)
#    endif //_WIN32
    {
#    if defined(CSMI_DEBUG)
        printf("GRD: Successfully opened handle. Setting up default CSMI info.\n");
#    endif // CSMI_DEBUG
        device->os_info.minimumAlignment =
            sizeof(void*); // setting alignment this way to be compatible across OSs since CSMI doesn't really dictate
                           // an alignment, but we should set something. - TJE
        device->issue_io                  = C_CAST(issue_io_func, send_CSMI_IO);
        device->drive_info.drive_type     = SCSI_DRIVE; // assume SCSI for now. Can be changed later
        device->drive_info.interface_type = RAID_INTERFACE;
        device->os_info.csmiDeviceData = M_REINTERPRET_CAST(ptrCsmiDeviceInfo, safe_calloc(1, sizeof(csmiDeviceInfo)));
        if (!device->os_info.csmiDeviceData)
        {
#    if defined(CSMI_DEBUG)
            printf("GRD: Failed to allocate csmiDeviceInfo structure\n");
#    endif // CSMI_DEBUG
            return MEMORY_FAILURE;
        }
        device->os_info.csmiDeviceData->csmiDevHandle       = device->os_info.fd;
        device->os_info.csmiDeviceData->controllerNumber    = controllerNum;
        device->os_info.csmiDeviceData->csmiDeviceInfoValid = true;
        // we were able to open the requested handle...now it's time to collect some information we'll need to save for
        // this device so we can talk to it later. get some controller/driver into then start checking for connected
        // ports and increment the counter.
        CSMI_SAS_DRIVER_INFO_BUFFER driverInfo;
#    if defined(CSMI_DEBUG)
        printf("GRD: Getting driver info\n");
#    endif // CSMI_DEBUG
        if (SUCCESS == csmi_Get_Driver_Info(device->os_info.csmiDeviceData->csmiDevHandle,
                                            device->os_info.csmiDeviceData->controllerNumber, &driverInfo,
                                            device->deviceVerbosity))
        {
            device->os_info.csmiDeviceData->csmiMajorVersion = driverInfo.Information.usCSMIMajorRevision;
            device->os_info.csmiDeviceData->csmiMinorVersion = driverInfo.Information.usCSMIMinorRevision;
            // TODO: If this is an Intel RST driver, check the name and additionally check to see if it supports the
            // Intel IOCTLs NOTE: If it's an Intel NVMe, then we need to special case some of the below IOCTLs since it
            // won't respond the same...
            device->os_info.csmiDeviceData->securityAccess = get_CSMI_Security_Access(C_CAST(
                char*, driverInfo.Information
                           .szName)); // With this, we could add some intelligence to when commands are supported or
                                      // not, at least under Windows, but mostly just a placeholder today. - TJE
        }
        else
        {
#    if defined(CSMI_DEBUG)
            printf("GRD: CSMI get device failure due to driver info failure\n");
#    endif // CSMI_DEBUG
            ret = FAILURE;
        }
        CSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfig;
#    if defined(CSMI_DEBUG)
        printf("GRD: Getting controller config\n");
#    endif // CSMI_DEBUG
        if (SUCCESS == csmi_Get_Controller_Configuration(device->os_info.csmiDeviceData->csmiDevHandle,
                                                         device->os_info.csmiDeviceData->controllerNumber, &ctrlConfig,
                                                         device->deviceVerbosity))
        {
            device->os_info.csmiDeviceData->controllerFlags = ctrlConfig.Configuration.uControllerFlags;
        }
        else
        {
#    if defined(CSMI_DEBUG)
            printf("GRD: CSMI get device failure due to controller config failure\n");
#    endif // CSMI_DEBUG
            ret = FAILURE;
        }

#    if defined(_WIN32) && defined(ENABLE_INTEL_RST)
        if (intelNVMe)
        {
#        if defined(CSMI_DEBUG)
            printf("GRD: Setting Intel NVMe function pointers\n");
#        endif // CSMI_DEBUG
            device->drive_info.drive_type = NVME_DRIVE;
            device->issue_io              = C_CAST(issue_io_func, send_Intel_NVM_SCSI_Command);
            device->issue_nvme_io         = C_CAST(issue_io_func, send_Intel_NVM_Command);
            device->os_info.csmiDeviceData->intelRSTSupport.intelRSTSupported = true;
            device->os_info.csmiDeviceData->intelRSTSupport.nvmePassthrough   = true;
            device->os_info.csmiDeviceData->scsiAddressValid                  = true;
            device->os_info.csmiDeviceData->scsiAddress.hostIndex             = C_CAST(uint8_t, controllerNum);
            device->os_info.csmiDeviceData->scsiAddress.lun                   = C_CAST(uint8_t, *intelLun);
            device->os_info.csmiDeviceData->scsiAddress.pathId                = C_CAST(uint8_t, *intelPathID);
            device->os_info.csmiDeviceData->portIdentifier                    = C_CAST(uint8_t, portID);
            device->os_info.csmiDeviceData->phyIdentifier                     = C_CAST(uint8_t, phyID);
            device->os_info.csmiDeviceData->scsiAddress.targetId              = C_CAST(uint8_t, *intelTargetID);
            device->drive_info.namespaceID = *intelLun + 1; // LUN is 0 indexed, whereas namespaces start at 1.
        }
        else
#    endif //_WIN32 && ENABLE_INTEL_RST
        {
            device->os_info.csmiDeviceData->portIdentifier = C_CAST(uint8_t, portID);
            device->os_info.csmiDeviceData->phyIdentifier  = C_CAST(uint8_t, phyID);
            // read phy info and match the provided port and phy identifier values with the phy data to store sasAddress
            // since it may be needed later.
            CSMI_SAS_PHY_INFO_BUFFER phyInfo;
#    if defined(CSMI_DEBUG)
            printf("GRD: Getting phy info\n");
#    endif // CSMI_DEBUG
           // NOTE: AMD's rcraid driver seems to treat non-raid drives slightly different in the phy info output. In the
           // case I observed, the raid drives were in the phy info, but the
           //       non-RAID drive was not shown here at all. However in the RAID IOCTLs, it does show as a separate
           //       single drive raid (but no MN or SN info to match to it). So it does not appear possible to issue a
           //       CSMI passthrough command to non-raid drives. This is completely opposite of what the Intel drivers
           //       do. Intel's drivers show every drive attached, RAID or non-RAID in the phy info. This may be
           //       something we want to detect in the future to reduce the number of IOCTLs sent, but for now, it works
           //       ok. We can optimize this more later. -TJE
            if (SUCCESS == csmi_Get_Phy_Info(device->os_info.csmiDeviceData->csmiDevHandle,
                                             device->os_info.csmiDeviceData->controllerNumber, &phyInfo,
                                             device->deviceVerbosity))
            {
                // Using the data we've already gotten, we need to save phy identifier, port identifier, port protocol,
                // and SAS address.
                // TODO: Check if we should be using the Identify or Attached structure information to populate the
                // support fields. Identify appears to contain initiator data, and attached seems to include target
                // data... bool foundPhyInfoForDevice = false;
#    if defined(CSMI_DEBUG)
                printf("GRD: Number of phys: %" CPRIu8 "\n", phyInfo.Information.bNumberOfPhys);
#    endif // CSMI_DEBUG
                for (uint8_t portNum = UINT8_C(0); portNum < 32 && portNum < phyInfo.Information.bNumberOfPhys;
                     ++portNum)
                {
#    if defined(CSMI_DEBUG)
                    printf("GRD: Checking for portID and phyID match for port %" PRIu8 "\n", portNum);
                    printf("GRD: phyInfo portID = %" PRIu8 " == %" PRIu32 "\n",
                           phyInfo.Information.Phy[portNum].bPortIdentifier, portID);
                    printf("GRD: phyInfo phyID  = %" PRIu8 " == %" PRIu32 "\n",
                           phyInfo.Information.Phy[portNum].Attached.bPhyIdentifier, phyID);
#    endif // CSMI_DEBUG
                    if ((phyInfo.Information.Phy[portNum].bPortIdentifier == portID &&
                         phyInfo.Information.Phy[portNum].Attached.bPhyIdentifier == phyID) ||
                        (portID == CSMI_SAS_IGNORE_PORT &&
                         phyInfo.Information.Phy[portNum].Attached.bPhyIdentifier == phyID) ||
                        (phyInfo.Information.Phy[portNum].bPortIdentifier == portID &&
                         phyID == CSMI_SAS_USE_PORT_IDENTIFIER))
                    {
#    if defined(CSMI_DEBUG)
                        printf("GRD: Port and phy ID match found\n");
#    endif // CSMI_DEBUG
                        device->os_info.csmiDeviceData->portProtocol =
                            phyInfo.Information.Phy[portNum].Attached.bTargetPortProtocol;
                        safe_memcpy(device->os_info.csmiDeviceData->sasAddress, 8,
                                    phyInfo.Information.Phy[portNum].Attached.bSASAddress, 8);
                        // foundPhyInfoForDevice = true;
                        break;
                    }
                }
            }

#    if defined(CSMI_DEBUG)
            printf("GRD: Done matching phyinfo. Setting up stuff based on protocol\n");
#    endif // CSMI_DEBUG

            // Need to get SASLun from RAID config IF SSP is supported since we need the SAS LUN value for issuing
            // commands This is not needed for other protocols.
            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SSP)
            {
                // Read the RAID config and find the matching device from SAS address. This is needed because SSP
                // passthrough will need the SAS LUN
#    if defined(CSMI_DEBUG)
                printf("GRD: SSP protocol, looking up SASLun value in RAID config\n");
#    endif // CSMI_DEBUG
                CSMI_SAS_RAID_INFO_BUFFER raidInfo;
#    if defined(CSMI_DEBUG)
                printf("GRD: Getting RAID info\n");
#    endif // CSMI_DEBUG
                if (SUCCESS == csmi_Get_RAID_Info(device->os_info.csmiDeviceData->csmiDevHandle,
                                                  device->os_info.csmiDeviceData->controllerNumber, &raidInfo,
                                                  device->deviceVerbosity))
                {
                    bool foundDrive = false;
#    if defined(CSMI_DEBUG)
                    printf("GRD: Got RAID info, checking RAID sets. Number of RAID sets: %" CPRIu32 "\n",
                           raidInfo.Information.uNumRaidSets);
#    endif // CSMI_DEBUG
                    for (uint32_t raidSet = UINT32_C(0); raidSet < raidInfo.Information.uNumRaidSets && !foundDrive;
                         ++raidSet)
                    {
                        // need to parse the RAID info to figure out how much memory to allocate and read the
                        uint32_t raidConfigLength = C_CAST(
                            uint32_t, sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) +
                                          (raidInfo.Information.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES)));
                        PCSMI_SAS_RAID_CONFIG_BUFFER raidConfig = M_REINTERPRET_CAST(
                            PCSMI_SAS_RAID_CONFIG_BUFFER, safe_calloc(raidConfigLength, sizeof(uint8_t)));
                        if (!raidConfig)
                        {
                            return MEMORY_FAILURE;
                        }
#    if defined(CSMI_DEBUG)
                        printf("GRD: Reading RAID config for raid set %" PRIu32 "\n", raidSet);
#    endif // CSMI_DEBUG
                        if (SUCCESS == csmi_Get_RAID_Config(device->os_info.csmiDeviceData->csmiDevHandle,
                                                            device->os_info.csmiDeviceData->controllerNumber,
                                                            raidConfig, raidConfigLength, raidSet,
                                                            CSMI_SAS_RAID_DATA_DRIVES, device->deviceVerbosity))
                        {
                            // iterate through the drives and find a matching SAS address.
                            // If we find a matching SAS address, we need to check the LUN....since we are only doing
                            // this for SSP, we should be able to use the get SCSI address function and validate that we
                            // have the correct lun.
#    if defined(CSMI_DEBUG)
                            printf("GRD: Iterating through drives in RAID set\n");
#    endif // CSMI_DEBUG
                            for (uint32_t driveIter = UINT32_C(0);
                                 driveIter < raidConfig->Configuration.bDriveCount &&
                                 driveIter < raidInfo.Information.uMaxDrivesPerSet && !foundDrive;
                                 ++driveIter)
                            {
#    if defined(CSMI_DEBUG)
                                printf("GRD: Checking for matching SAS address\n");
#    endif // CSMI_DEBUG
                                if (memcmp(raidConfig->Configuration.Drives[driveIter].bSASAddress,
                                           device->os_info.csmiDeviceData->sasAddress, 8) == 0)
                                {
#    if defined(CSMI_DEBUG)
                                    printf("GRD: Match found!\n");
#    endif // CSMI_DEBUG
           // take the SAS Address and SAS Lun and convert to SCSI Address...this should be supported IF we find a SAS
           // drive.
                                    CSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddress;
#    if defined(CSMI_DEBUG)
                                    printf("GRD: Calling get SCSI Address\n");
#    endif // CSMI_DEBUG
                                    if (SUCCESS == csmi_Get_SCSI_Address(
                                                       device->os_info.csmiDeviceData->csmiDevHandle,
                                                       device->os_info.csmiDeviceData->controllerNumber, &scsiAddress,
                                                       raidConfig->Configuration.Drives[driveIter].bSASAddress,
                                                       raidConfig->Configuration.Drives[driveIter].bSASLun,
                                                       device->deviceVerbosity))
                                    {
#    if defined(CSMI_DEBUG)
                                        printf("GRD: Got SCSI address\n");
#    endif // CSMI_DEBUG
                                        if (scsiAddress.bLun == lun)
                                        {
#    if defined(CSMI_DEBUG)
                                            printf("GRD: Matching LUN found\n");
#    endif // CSMI_DEBUG
                                            device->os_info.csmiDeviceData->scsiAddress.hostIndex =
                                                scsiAddress.bHostIndex;
                                            device->os_info.csmiDeviceData->scsiAddress.pathId = scsiAddress.bPathId;
                                            device->os_info.csmiDeviceData->scsiAddress.targetId =
                                                scsiAddress.bTargetId;
                                            device->os_info.csmiDeviceData->scsiAddress.lun = scsiAddress.bLun;
                                            safe_memcpy(device->os_info.csmiDeviceData->sasLUN, 8,
                                                        raidConfig->Configuration.Drives[driveIter].bSASLun, 8);
                                            device->os_info.csmiDeviceData->scsiAddressValid = true;
                                            foundDrive                                       = true;
                                        }
                                    }
                                }
                            }
                        }
                        safe_free_csmi_raid_config(&raidConfig);
                    }
                }
            }

            if ((device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA) == 0 &&
                !device->os_info.csmiDeviceData->scsiAddressValid)
            {
                // get scsi address
#    if defined(CSMI_DEBUG)
                printf("GRD: Calling get SCSI address since it is not valid\n");
#    endif // CSMI_DEBUG
                CSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddress;
                if (SUCCESS == csmi_Get_SCSI_Address(device->os_info.csmiDeviceData->csmiDevHandle,
                                                     device->os_info.csmiDeviceData->controllerNumber, &scsiAddress,
                                                     device->os_info.csmiDeviceData->sasAddress,
                                                     device->os_info.csmiDeviceData->sasLUN, device->deviceVerbosity))
                {
#    if defined(CSMI_DEBUG)
                    printf("GRD: Got valid SCSI address\n");
#    endif // CSMI_DEBUG
                    device->os_info.csmiDeviceData->scsiAddressValid      = true;
                    device->os_info.csmiDeviceData->scsiAddress.hostIndex = scsiAddress.bHostIndex;
                    device->os_info.csmiDeviceData->scsiAddress.pathId    = scsiAddress.bPathId;
                    device->os_info.csmiDeviceData->scsiAddress.targetId  = scsiAddress.bTargetId;
                    device->os_info.csmiDeviceData->scsiAddress.lun       = scsiAddress.bLun;
                }
            }

            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA ||
                device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_STP)
            {
#    if defined(CSMI_DEBUG)
                printf("GRD: Getting SATA signature\n");
#    endif // CSMI_DEBUG
                CSMI_SAS_SATA_SIGNATURE_BUFFER signature;
                device->drive_info.drive_type = ATA_DRIVE;
                // get sata signature fis and set pmport
                if (SUCCESS == csmi_Get_SATA_Signature(device->os_info.csmiDeviceData->csmiDevHandle,
                                                       device->os_info.csmiDeviceData->controllerNumber, &signature,
                                                       device->os_info.csmiDeviceData->phyIdentifier,
                                                       device->deviceVerbosity))
                {
#    if defined(CSMI_DEBUG)
                    printf("GRD: Got SATA signature\n");
#    endif // CSMI_DEBUG
                    safe_memcpy(&device->os_info.csmiDeviceData->signatureFIS, sizeof(sataD2HFis),
                                &signature.Signature.bSignatureFIS, sizeof(sataD2HFis));
                    device->os_info.csmiDeviceData->signatureFISValid = true;
                    device->os_info.csmiDeviceData->sataPMPort =
                        M_Nibble0(device->os_info.csmiDeviceData->signatureFIS
                                      .byte1); // lower nibble of this byte holds the port multiplier port that the
                                               // device is attached to...this can help route the FIS properly
                }
            }

            // Need to check for Intel IOCTL support on SATA drives so we can send the Intel FWDL ioctls instead of
            // passthrough.
            if (strncmp(C_CAST(const char*, driverInfo.Information.szName), "iaStorA", 7) == 0)
            {
                // This is an intel driver.
#    if defined(CSMI_DEBUG)
                printf("GRD: Detected Intel driver.\n");
#    endif // CSMI_DEBUG
           // There is a way to get path-target-lun data from the SAS address if the other IOCTLs didn't work (which
           // they don't seem to support this translation anyways)
                if (!device->os_info.csmiDeviceData->scsiAddressValid)
                {
#    if defined(CSMI_DEBUG)
                    printf("GRD: Manually setting SCSI address for Intel driver\n");
#    endif // CSMI_DEBUG
           // convert SAS address to SCSI address using proprietary intel formatting since IOCTLs above didn't work or
           // weren't used. NOTE: This is only valid for the noted driver. Previous versions used different formats for
           // sasAddress that don't support firmware update IOCTLs, and are not supported - TJE
                    device->os_info.csmiDeviceData->scsiAddress.lun = device->os_info.csmiDeviceData->sasAddress[0];
                    device->os_info.csmiDeviceData->scsiAddress.targetId =
                        device->os_info.csmiDeviceData->sasAddress[1];
                    device->os_info.csmiDeviceData->scsiAddress.pathId = device->os_info.csmiDeviceData->sasAddress[2];
                    device->os_info.csmiDeviceData->scsiAddressValid   = true;
                }
            }
        }
#    if defined(CSMI_DEBUG)
        printf("GRD: Initialization of structures completed. Calling fill drive info\n");
#    endif // CSMI_DEBUG
        ret = fill_Drive_Info_Data(device);
#    if defined(CSMI_DEBUG)
        printf("GRD: Fill drive info returned %d\n", ret);
#    endif // CSMI_DEBUG
    }
    return ret;
}

bool is_CSMI_Handle(const char* filename)
{
    bool isCSMI = false;
    // TODO: Expand this check to make sure all necessary parts of handle are present???
    if (strstr(filename, CSMI_HANDLE_BASE_NAME))
    {
        isCSMI = true;
    }
    return isCSMI;
}

// There are 2 possible keys depending on if this is a port or miniport driver.
// Miniport will use an ASCII string in DriverParameter for CSMI=accessLevel
// Storport will use a DWORD named CSMI set to a value between 0 and 3 (0=no access, 3 = full access)
// The following link describes where to find the DriverParameter key:
// https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/registry-entries-for-storport-miniport-drivers
// Miniport scope: HKLM\System\CurrentControlSet\Services<miniport name>\Parameters\Device
// Adapter scope:  HKLM\System\CurrentControlSet\Services<miniport name>\Parameters\Device<adapter#>
// Storport key location
// HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\<PortDriverName>\Parameters\CSMI = dword value
eCSMISecurityAccess get_CSMI_Security_Access(const char* driverName)
{
    eCSMISecurityAccess access = CSMI_SECURITY_ACCESS_NONE;
#    if defined(_WIN32)
    if (strstr(driverName, "iaStor")) // Expand this list as other drives not using these registry keys are found-TJE
    {
        // Intel's driver does not use these registry keys
        access = CSMI_SECURITY_ACCESS_FULL;
    }
    else
    {
        HKEY         keyHandle;
        const TCHAR* baseRegKeyPath          = TEXT("SYSTEM\\CurrentControlSet\\Services\\");
        const TCHAR* paramRegKeyPath         = TEXT("\\Parameters");
        size_t       tdriverNameLength       = (safe_strlen(driverName) + 1) * sizeof(TCHAR);
        size_t       registryKeyStringLength = _tcslen(baseRegKeyPath) + tdriverNameLength + _tcslen(paramRegKeyPath);
        TCHAR*       registryKey = M_REINTERPRET_CAST(TCHAR*, safe_calloc(registryKeyStringLength, sizeof(TCHAR)));
        TCHAR*       tdriverName = M_REINTERPRET_CAST(TCHAR*, safe_calloc(tdriverNameLength, sizeof(TCHAR)));
        if (tdriverName)
        {
            _stprintf_s(tdriverName, tdriverNameLength, TEXT("%hs"), driverName);
        }
        if (registryKey)
        {
            _stprintf_s(registryKey, registryKeyStringLength, TEXT("%s%s%s"), baseRegKeyPath, tdriverName,
                        paramRegKeyPath);
        }
        if (tdriverName && registryKey && _tcslen(tdriverName) > 0 && _tcslen(registryKey) > 0)
        {
            LSTATUS openKeyStatus = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registryKey, 0, KEY_READ, &keyHandle);
            if (ERROR_SUCCESS == openKeyStatus)
            {
                // Found the driver's parameters. Now search for CSMI DWORD for port drivers
                DWORD storportdataLen = DWORD_C(4);
                DECLARE_ZERO_INIT_ARRAY(BYTE, storportregData, 4);
                const TCHAR* storportvalueName = TEXT("CSMI");
                DWORD        storportvalueType = REG_DWORD;
                LSTATUS regQueryStatus = RegQueryValueEx(keyHandle, storportvalueName, M_NULLPTR, &storportvalueType,
                                                         storportregData, &storportdataLen);
                if (ERROR_SUCCESS == regQueryStatus)
                {
                    int32_t dwordVal = C_CAST(int32_t, M_BytesTo4ByteValue(storportregData[3], storportregData[2],
                                                                           storportregData[1], storportregData[0]));
                    switch (dwordVal)
                    {
                    case 0:
                        access = CSMI_SECURITY_ACCESS_NONE;
                        break;
                    case 1:
                        access = CSMI_SECURITY_ACCESS_RESTRICTED;
                        break;
                    case 2:
                        access = CSMI_SECURITY_ACCESS_LIMITED;
                        break;
                    case 3:
                    default:
                        access = CSMI_SECURITY_ACCESS_FULL;
                        break;
                    }
                    RegCloseKey(keyHandle);
                }
                else if (ERROR_FILE_NOT_FOUND == regQueryStatus)
                {
                    RegCloseKey(keyHandle);
                    // the CSMI key did not exist. Check for miniport/adapter keys
                    // No port driver key found. Check for the miniport driver key in DriverParameter for Device or
                    // Device#
                    const TCHAR* paramDeviceKeyPath       = TEXT("\\Device");
                    size_t       paramDeviceKeyPathLength = _tcsclen(paramDeviceKeyPath);
                    registryKeyStringLength +=
                        paramDeviceKeyPathLength +
                        3; // Adding 3 for if we need to check for a device number (adapter number)
                    TCHAR* temp = safe_realloc(registryKey, registryKeyStringLength * sizeof(TCHAR));
                    if (temp)
                    {
                        registryKey = temp;
                        _stprintf_s(registryKey, registryKeyStringLength, TEXT("%s%s%s%s"), baseRegKeyPath, tdriverName,
                                    paramRegKeyPath, paramDeviceKeyPath);
                        if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, registryKey, 0, KEY_READ, &keyHandle))
                        {
                            DWORD        dataLen   = DWORD_C(0);
                            BYTE*        regData   = M_NULLPTR; // will be allocated to correct length
                            const TCHAR* valueName = TEXT("DriverParameter");
                            DWORD        valueType = REG_SZ;
                            regQueryStatus =
                                RegQueryValueEx(keyHandle, valueName, M_NULLPTR, &valueType, regData, &dataLen);
                            if (regQueryStatus ==
                                ERROR_SUCCESS) // since we had no memory allocated, this returned success rather than
                                               // ERROR_MORE_DATA so we can go and allocate then read it.-TJE
                            {
                                // found, now allocate memory
                                regData = safe_calloc(dataLen, sizeof(BYTE));
                                if (regData)
                                {
                                    regQueryStatus =
                                        RegQueryValueEx(keyHandle, valueName, M_NULLPTR, &valueType, regData, &dataLen);
                                    if (regQueryStatus == ERROR_SUCCESS)
                                    {
                                        // now interpret the regData as a string
                                        TCHAR* driverParamterVal = C_CAST(TCHAR*, regData);
                                        TCHAR* csmiConfig        = _tcsstr(driverParamterVal, TEXT("CSMI="));
                                        if (csmiConfig)
                                        {
                                            // interpret the value and set proper level
                                            // Possible values are "None, Restricted, Limited, Full"
                                            // Note: Case sensitive match for now. Switch to _tcsnicmp if needing to
                                            // eliminate case sensitivity-TJE
                                            if (0 == _tcsncmp(csmiConfig, TEXT("CSMI=None"), 9))
                                            {
                                                access = CSMI_SECURITY_ACCESS_NONE;
                                            }
                                            else if (0 == _tcsncmp(csmiConfig, TEXT("CSMI=Restricted"), 15))
                                            {
                                                access = CSMI_SECURITY_ACCESS_RESTRICTED;
                                            }
                                            else if (0 == _tcsncmp(csmiConfig, TEXT("CSMI=Limited"), 12))
                                            {
                                                access = CSMI_SECURITY_ACCESS_LIMITED;
                                            }
                                            else if (0 == _tcsncmp(csmiConfig, TEXT("CSMI=Full"), 9))
                                            {
                                                access = CSMI_SECURITY_ACCESS_FULL;
                                            }
                                            else
                                            {
                                                access = CSMI_SECURITY_ACCESS_LIMITED;
                                            }
                                        }
                                        else
                                        {
                                            // No CSMI level specified
                                            access = CSMI_SECURITY_ACCESS_LIMITED;
                                        }
                                        safe_free(&regData);
                                    }
                                }
                            }
                            else if (regQueryStatus == ERROR_FILE_NOT_FOUND)
                            {
                                // No driver parameter
                                access = CSMI_SECURITY_ACCESS_LIMITED;
                            }
                            RegCloseKey(keyHandle);
                        }
                    }
                }
                else
                {
                    access = CSMI_SECURITY_ACCESS_LIMITED;
                }
            }
            else
            {
                // This shouldn't happen, but it could happen...setting Limited for now - TJE
                access = CSMI_SECURITY_ACCESS_LIMITED;
            }
        }
        safe_free(&tdriverName);
        safe_free(&registryKey);
    }
#    else // not windows, need root, otherwise not available at all. Return FULL if running as root
    M_USE_UNUSED(driverName);
    if (is_Running_Elevated())
    {
        access = CSMI_SECURITY_ACCESS_FULL;
    }
#    endif
    return access;
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
//!   \param[in] beginningOfList = list of handles to use to check the count. This can prevent duplicate devices if we
//!   know some handles should not be looked at.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
eReturnValues get_CSMI_RAID_Device_Count(uint32_t*            numberOfDevices,
                                         uint64_t             flags,
                                         ptrRaidHandleToScan* beginningOfList)
{
    CSMI_HANDLE fd = CSMI_INVALID_HANDLE;
#    if defined(_WIN32)
    DECLARE_ZERO_INIT_ARRAY(TCHAR, deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH);
#    else                                                          //_WIN32
    DECLARE_ZERO_INIT_ARRAY(char, deviceName, CSMI_NIX_MAX_DEVICE_NAME_LENGTH);
#    endif                                                         //_WIN32
    eVerbosityLevels    csmiCountVerbosity    = VERBOSITY_DEFAULT; // change this if debugging
    ptrRaidHandleToScan raidList              = M_NULLPTR;
    ptrRaidHandleToScan previousRaidListEntry = M_NULLPTR;
    uint32_t            controllerNumber      = UINT32_C(0);
    uint32_t            found                 = UINT32_C(0);
    uint32_t            raidConfigDrivesFound = UINT32_C(0);
    uint32_t            phyInfoDrivesFound    = UINT32_C(0);

    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES)
    {
        csmiCountVerbosity = VERBOSITY_COMMAND_NAMES;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE)
    {
        csmiCountVerbosity = VERBOSITY_COMMAND_VERBOSE;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_BUFFERS)
    {
        csmiCountVerbosity = VERBOSITY_BUFFERS;
    }

#    if defined(CSMI_DEBUG)
    printf("GDC: Begin\n");
#    endif // CSMI_DEBUG
    if (!beginningOfList || !*beginningOfList)
    {
        // don't do anything. Only scan when we get a list to use.
        // Each OS that want's to do this should generate a list of handles to look for.
#    if defined(CSMI_DEBUG)
        printf("GDC: no list provided\n");
#    endif // CSMI_DEBUG
        return SUCCESS;
    }

    raidList = *beginningOfList;

    // On non-Windows systems, we also have to check controller numbers...so there is one extra top-level loop for this
    // on these systems.
#    if defined(CSMI_DEBUG)
    printf("GDC: Beginning iteration of raidList\n");
#    endif // CSMI_DEBUG
    while (raidList)
    {
        bool handleRemoved = false;
        if (raidList->raidHint.csmiRAID || raidList->raidHint.unknownRAID)
        {
#    if defined(_WIN32)
            _stprintf_s(deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs"), raidList->handle);
            // lets try to open the controller.
            fd = CreateFile(deviceName,
                            GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#        if !defined(WINDOWS_DISABLE_OVERLAPPED)
                            FILE_FLAG_OVERLAPPED,
#        else
                            0,
#        endif
                            M_NULLPTR);
            if (fd != INVALID_HANDLE_VALUE)
#    else
            snprintf_err_handle(deviceName, SIZE_OF_STACK_ARRAY(deviceName), "%s", raidList->handle);
            if ((fd = open(deviceName, O_RDWR | O_NONBLOCK)) >= 0)
#    endif
            {
#    if defined(CSMI_DEBUG)
                printf("GDC: Handle valid and opened\n");
#    endif // CSMI_DEBUG
#    if !defined(_WIN32)
                for (controllerNumber = 0; controllerNumber < OPENSEA_MAX_CONTROLLERS; ++controllerNumber)
                {
#    endif
                    // first, check if this handle supports CSMI before we try anything else
                    CSMI_SAS_DRIVER_INFO_BUFFER  driverInfo;
                    CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
                    CSMI_SAS_CNTLR_STATUS_BUFFER controllerStatus;
                    safe_memset(&driverInfo, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER), 0,
                                sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
                    safe_memset(&controllerConfig, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER), 0,
                                sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
                    safe_memset(&controllerStatus, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER), 0,
                                sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER));
#    if defined(CSMI_DEBUG)
                    printf("GDC: Getting controller config, controller status, and driver info\n");
#    endif // CSMI_DEBUG
                    if (SUCCESS == csmi_Get_Basic_Info(fd, controllerNumber, &driverInfo, &controllerConfig,
                                                       &controllerStatus, csmiCountVerbosity))
                    {
#    if defined(CSMI_DEBUG)
                        printf("GDC: Checking controller flags: %" CPRIX32 "h\n",
                               controllerConfig.Configuration.uControllerFlags);
#    endif // CSMI_DEBUG
                        eKnownCSMIDriver knownDriver = get_Known_CSMI_Driver_Type(&driverInfo.Information);
                        // Check if it's a RAID capable controller. We only want to enumerate devices on those in this
                        // function
                        if ((controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID ||
                             controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID ||
                             controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY)
                            //&& knownDriver != CSMI_DRIVER_ARCSAS
                        )
                        {
#    if defined(CSMI_DEBUG)
                            printf("GDC: Getting RAID info\n");
#    endif // CSMI_DEBUG
           // Get RAID info
           // NOTE Adaptec's API doesn't seem to like this. May need to pull phy info instead if this fails. -TJE
                            CSMI_SAS_RAID_INFO_BUFFER csmiRAIDInfo;
                            csmi_Get_RAID_Info(fd, controllerNumber, &csmiRAIDInfo, csmiCountVerbosity);
                            // Get RAID config
#    if defined(CSMI_DEBUG)
                            printf("GDC: Number of RAID sets: %" CPRIu32 "\n", csmiRAIDInfo.Information.uNumRaidSets);
#    endif // CSMI_DEBUG
                            bool raidConfigIncomplete =
                                false; // if a driver has not filled in SASAddress or MN/SN info, then set this to true
                                       // to use the PhyInfo instead. This may also happen for specific drivers in the
                                       // future - TJE
                            // note: not checking raidConfigIncomplete in this loop to assist with some debug output
                            // later. This allows for warning about possible duplicates here. ARCSAS note: Raidsets
                            // start at 1 instead of zero!
                            uint32_t raidSet     = UINT32_C(0);
                            uint32_t numRaidSets = csmiRAIDInfo.Information.uNumRaidSets;
                            if (knownDriver == CSMI_DRIVER_ARCSAS)
                            {
                                raidSet = UINT32_C(1);
                                numRaidSets += UINT32_C(1);
                            }
                            for (; raidSet < numRaidSets; ++raidSet)
                            {
                                // start with a length that adds no padding for extra drives, then reallocate to a new
                                // size when we know the new size
                                uint32_t raidConfigLength = C_CAST(
                                    uint32_t,
                                    sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) +
                                        csmiRAIDInfo.Information.uMaxDrivesPerSet *
                                            sizeof(CSMI_SAS_RAID_DRIVES)); // Intel driver recommends allocating for 8
                                                                           // drives to make sure nothing is missed.
                                                                           // Maybe check if maxdriverperset less than
                                                                           // this to allcoate for 8???
                                PCSMI_SAS_RAID_CONFIG_BUFFER csmiRAIDConfig = C_CAST(
                                    PCSMI_SAS_RAID_CONFIG_BUFFER, safe_calloc(raidConfigLength, sizeof(uint8_t)));
                                if (csmiRAIDConfig)
                                {
#    if defined(CSMI_DEBUG)
                                    printf("GDC: Getting raid config\n");
#    endif // CSMI_DEBUG
                                    if (SUCCESS == csmi_Get_RAID_Config(fd, controllerNumber, csmiRAIDConfig,
                                                                        raidConfigLength, raidSet,
                                                                        CSMI_SAS_RAID_DATA_DRIVES, csmiCountVerbosity))
                                    {
                                        // make sure we got all the drive information...if now, we need to reallocate
                                        // with some more memory
#    if defined(CSMI_DEBUG)
                                        printf("GDC: Checking drives. Max drives per set: %" CPRIu32
                                               "\tDrive Count: %" CPRIu8 "\n",
                                               csmiRAIDInfo.Information.uMaxDrivesPerSet,
                                               csmiRAIDConfig->Configuration.bDriveCount);
#    endif // CSMI_DEBUG
                                        for (uint32_t iter = UINT32_C(0);
                                             iter < csmiRAIDConfig->Configuration.bDriveCount &&
                                             iter < csmiRAIDInfo.Information.uMaxDrivesPerSet;
                                             ++iter)
                                        {
                                            bool driveInfoValid =
                                                true; // for version 81 and earlier, assume this is true.
#    if defined(CSMI_DEBUG)
                                            printf("GDC: Checking CSMI Revision: %" CPRIu16 ".%" CPRIu16 "\n",
                                                   driverInfo.Information.usCSMIMajorRevision,
                                                   driverInfo.Information.usCSMIMinorRevision);
#    endif // CSMI_DEBUG
                                            if (driverInfo.Information.usCSMIMajorRevision > 0 ||
                                                driverInfo.Information.usCSMIMinorRevision > 81)
                                            {
#    if defined(CSMI_DEBUG)
                                                printf("GDC: CSMI Minor rev > 81, so checking bDataType\n");
#    endif // CSMI_DEBUG
                                                switch (csmiRAIDConfig->Configuration.bDataType)
                                                {
                                                case CSMI_SAS_RAID_DATA_DRIVES:
                                                    break;
                                                case CSMI_SAS_RAID_DATA_DEVICE_ID:
                                                case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
                                                default:
                                                    driveInfoValid = false;
                                                    break;
                                                }
                                            }
                                            if (driveInfoValid)
                                            {
                                                switch (csmiRAIDConfig->Configuration.Drives[iter].bDriveUsage)
                                                {
                                                case CSMI_SAS_DRIVE_CONFIG_NOT_USED:
                                                    // Don't count drives with this flag, because they are not
                                                    // configured in a RAID at this time. We only want those configured
                                                    // in a RAID/RAID-like scenario.
#    if defined(CSMI_DEBUG)
                                                    printf("GDC: Drive set as not used. Ignoring...\n");
#    endif // CSMI_DEBUG
                                                    break;
                                                case CSMI_SAS_DRIVE_CONFIG_MEMBER:
                                                case CSMI_SAS_DRIVE_CONFIG_SPARE:
                                                case CSMI_SAS_DRIVE_CONFIG_SPARE_ACTIVE:
                                                case CSMI_SAS_DRIVE_CONFIG_SRT_CACHE:
                                                case CSMI_SAS_DRIVE_CONFIG_SRT_DATA:
                                                    ++raidConfigDrivesFound;
                                                    // check if SAS address is non-Zero or if MN/SN are available.
                                                    // removed special intel case since it appears to only work in some
                                                    // configurations for some driver versions
                                                    if (!raidConfigIncomplete &&
                                                        ((is_Empty(csmiRAIDConfig->Configuration.Drives[iter].bModel,
                                                                   40) ||
                                                          is_Empty(
                                                              csmiRAIDConfig->Configuration.Drives[iter].bSerialNumber,
                                                              40)) ||
                                                         is_Empty(
                                                             csmiRAIDConfig->Configuration.Drives[iter].bSASAddress,
                                                             8)))
                                                    {
                                                        raidConfigIncomplete = true;
#    if defined(CSMI_DEBUG)
                                                        printf("GDC: Detected incomplete raid config data. Will need "
                                                               "phy info to complete count\n");
#    endif // CSMI_DEBUG
                                                    }

#    if defined(CSMI_DEBUG)
                                                    printf("GDC: Found a drive\n");
#    endif // CSMI_DEBUG
                                                    break;
                                                default:
#    if defined(CSMI_DEBUG)
                                                    printf("GDC: Unknown drive usage: %u. Ignoring...\n",
                                                           csmiRAIDConfig->Configuration.Drives[iter].bDriveUsage);
#    endif // CSMI_DEBUG
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    else // didn't successfully read a RAID config, which is most likely a driver
                                         // problem. This code requests data that SHOULD be available.
                                    {
                                        raidConfigIncomplete = true;
                                    }
                                    safe_free_csmi_raid_config(&csmiRAIDConfig);
                                }
                            }
                            if (raidConfigIncomplete)
                            {
                                // Need to check the phy info as a fall back since there is not enough information to
                                // uniquely identify all drives attached to this CSMI driver.
                                CSMI_SAS_PHY_INFO_BUFFER phyInfo;
                                safe_memset(&phyInfo, sizeof(CSMI_SAS_PHY_INFO_BUFFER), 0,
                                            sizeof(CSMI_SAS_PHY_INFO_BUFFER));
#    if defined(CSMI_DEBUG)
                                printf("GDC: Getting phy info due to incomplete RAID configuration info.\n");
#    endif // CSMI_DEBUG
           // NOTE: AMD's rcraid driver seems to treat non-raid drives slightly different in the phy info output. In the
           // case I observed, the raid drives were in the phy info, but the
           //       non-RAID drive was not shown here at all. However in the RAID IOCTLs, it does show as a separate
           //       single drive raid (but no MN or SN info to match to it). So it does not appear possible to issue a
           //       CSMI passthrough command to non-raid drives. This is completely opposite of what the Intel drivers
           //       do. Intel's drivers show every drive attached, RAID or non-RAID in the phy info. This may be
           //       something we want to detect in the future to reduce the number of IOCTLs sent, but for now, it works
           //       ok. We can optimize this more later. -TJE
                                if (SUCCESS == csmi_Get_Phy_Info(fd, controllerNumber, &phyInfo, csmiCountVerbosity))
                                {
                                    for (uint8_t phyIter = UINT8_C(0);
                                         phyIter < 32 && phyInfoDrivesFound < phyInfo.Information.bNumberOfPhys;
                                         ++phyIter)
                                    {
                                        if (phyInfo.Information.Phy[phyIter].Attached.bDeviceType ==
                                            CSMI_SAS_NO_DEVICE_ATTACHED)
                                        {
                                            continue;
                                        }
#    if defined(CSMI_DEBUG)
                                        printf("GDC: target port protocol(s): ");
                                        print_CSMI_Port_Protocol(
                                            phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol);
#    endif // CSMI_DEBUG
           // Creating a temporary tDevice structure to use for the passthrough commands.-TJE
                                        tDevice tempDevice;
                                        safe_memset(&tempDevice, sizeof(tDevice), 0, sizeof(tDevice));
                                        tempDevice.os_info.minimumAlignment =
                                            sizeof(void*); // setting alignment this way to be compatible across OSs
                                                           // since CSMI doesn't really dictate an alignment, but we
                                                           // should set something. - TJE
                                        tempDevice.issue_io = C_CAST(issue_io_func, send_CSMI_IO);
                                        tempDevice.drive_info.drive_type =
                                            SCSI_DRIVE; // assume SCSI for now. Can be changed later
                                        tempDevice.drive_info.interface_type = RAID_INTERFACE;
                                        tempDevice.os_info.csmiDeviceData    = M_REINTERPRET_CAST(
                                            ptrCsmiDeviceInfo, safe_calloc(1, sizeof(csmiDeviceInfo)));
                                        if (!tempDevice.os_info.csmiDeviceData)
                                        {
#    if defined(CSMI_DEBUG)
                                            printf("GDC: Failed to allocate csmiDeviceInfo structure\n");
#    endif // CSMI_DEBUG
                                            continue;
                                        }
                                        tempDevice.os_info.csmiDeviceData->csmiDevHandle       = fd;
                                        tempDevice.os_info.csmiDeviceData->controllerNumber    = controllerNumber;
                                        tempDevice.os_info.csmiDeviceData->csmiDeviceInfoValid = true;
                                        ScsiIoCtx csmiPTCmd;
                                        safe_memset(&csmiPTCmd, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
                                        csmiPTCmd.device        = &tempDevice;
                                        csmiPTCmd.timeout       = 15;
                                        csmiPTCmd.direction     = XFER_DATA_IN;
                                        csmiPTCmd.psense        = tempDevice.drive_info.lastCommandSenseData;
                                        csmiPTCmd.senseDataSize = SPC3_SENSE_LEN;
                                        // Don't have a SAS Address to match to, so we need to send an identify or
                                        // inquiry to the device to see if it is the same MN, then check the SN. NOTE:
                                        // This will not work if we don't already know the sasLUN for SAS drives. SATA
                                        // will be ok though.
                                        tempDevice.os_info.csmiDeviceData->portIdentifier =
                                            phyInfo.Information.Phy[phyIter].bPortIdentifier;
                                        tempDevice.os_info.csmiDeviceData->phyIdentifier =
                                            phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier;
                                        tempDevice.os_info.csmiDeviceData->portProtocol =
                                            phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol;
                                        safe_memcpy(&tempDevice.os_info.csmiDeviceData->sasAddress[0], 8,
                                                    phyInfo.Information.Phy[phyIter].Attached.bSASAddress, 8);
                                        // Attempt passthrough command and compare identifying data.
                                        // for this to work, SCSIIoCTX structure must be manually defined for what we
                                        // want to do right now and call the CSMI IO directly...not great, but don't
                                        // want to have other force flags elsewhere at the moment- TJE
                                        if (phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol &
                                                CSMI_SAS_PROTOCOL_SATA ||
                                            phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol &
                                                CSMI_SAS_PROTOCOL_STP)
                                        {
                                            // ATA identify
                                            DECLARE_ZERO_INIT_ARRAY(uint8_t, identifyData, 512);
                                            ataPassthroughCommand identify = create_ata_pio_in_cmd(
                                                &tempDevice, ATA_IDENTIFY, false, 1, identifyData, 512);
                                            csmiPTCmd.pdata       = identifyData;
                                            csmiPTCmd.dataLength  = 512;
                                            csmiPTCmd.pAtaCmdOpts = &identify;
#    if defined(CSMI_DEBUG)
                                            printf("GDC: Detected SATA protocol. Attempting Identify CMD\n");
#    endif // CSMI_DEBUG
                                            if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                            {
#    if defined(CSMI_DEBUG)
                                                printf("GDC: Identify Successful. Adding to count.\n");
#    endif // CSMI_DEBUG
                                                ++phyInfoDrivesFound;
                                            }
                                            else
                                            {
                                                // possibly an ATAPI drive
                                                identify.tfr.CommandStatus = ATAPI_IDENTIFY;
                                                if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                                {
#    if defined(CSMI_DEBUG)
                                                    printf("GDC: ATAPI Identify Successful\n");
                                                    printf("GDC: Not adding to the count since ATAPI should use system "
                                                           "handle instead.\n");
#    endif // CSMI_DEBUG
                                                }
                                            }
                                        }
                                        else if (phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol &
                                                 CSMI_SAS_PROTOCOL_SSP)
                                        {
                                            // SCSI Inquiry and read unit serial number VPD page
                                            DECLARE_ZERO_INIT_ARRAY(uint8_t, inqData, 96);
                                            DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
                                            cdb[OPERATION_CODE] = INQUIRY_CMD;
                                            /*if (evpd)
                                            {
                                                cdb[1] |= BIT0;
                                            }*/
                                            cdb[2] = 0; // pageCode;
                                            cdb[3] = M_Byte1(96);
                                            cdb[4] = M_Byte0(96);
                                            cdb[5] = 0; // control

                                            csmiPTCmd.cdbLength = CDB_LEN_6;
                                            safe_memcpy(csmiPTCmd.cdb, SCSI_IO_CTX_MAX_CDB_LEN, cdb, 6);
                                            csmiPTCmd.dataLength = 96;
                                            csmiPTCmd.pdata      = inqData;
#    if defined(CSMI_DEBUG)
                                            printf("GDC: Detected SSP protocol. Attempting Inquiry\n");
#    endif // CSMI_DEBUG
                                            if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                            {
#    if defined(CSMI_DEBUG)
                                                printf("GDC: Inquiry Successful\n");
#    endif // CSMI_DEBUG
                                                ++phyInfoDrivesFound;
                                            }
                                        }
                                    }
                                }
                                else
                                {
#    if defined(CSMI_DEBUG)
                                    printf("GDC: Unable to get Phy info! Unrecoverable error.\n");
#    endif // CSMI_DEBUG
                                }
                            }
                        }
                        // printf("Found CSMI Handle: %s\tRemoving from list.\n", raidList->handle);
                        // This was a CSMI handle, remove it from the list!
                        // This will also increment us to the next handle
#    if defined(CSMI_DEBUG)
                        printf("GDC: Updating the raid list\n");
#    endif // CSMI_DEBUG
                        bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                        raidList                          = remove_RAID_Handle(raidList, previousRaidListEntry);
                        if (pointerAtBeginningOfRAIDList)
                        {
                            // if the first entry in the list was removed, we need up update the pointer before we exit
                            // so that the code that called here won't have an invalid pointer
                            *beginningOfList = raidList;
                        }
                        handleRemoved = true;
                        // printf("Handle removed successfully. raidList = %p\n", raidList);
                    }
#    if !defined(_WIN32) // loop through controller numbers
                }
#    endif
            }
            // close handle to the controller
#    if defined(_WIN32)
            if (fd != INVALID_HANDLE_VALUE)
            {
                CloseHandle(fd);
            }
#    else
            if (fd > 0)
            {
                close(fd);
            }
#    endif
        }
        if (!handleRemoved)
        {
            previousRaidListEntry =
                raidList; // store handle we just looked at in case we need to remove one from the list
            // increment to next element in the list
            raidList = raidList->next;
        }
    }
    found = raidConfigDrivesFound;
    if (phyInfoDrivesFound > raidConfigDrivesFound)
    {
        printf("WARNING: Possible duplicate devices found due to incomplete RAID config response from CSMI driver\n");
        found = phyInfoDrivesFound;
    }
    *numberOfDevices = found;
#    if defined(CSMI_DEBUG)
    printf("GDC: Returning CSMI count as %d\n", found);
#    endif // CSMI_DEBUG
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
eReturnValues get_CSMI_RAID_Device_List(tDevice* const       ptrToDeviceList,
                                        uint32_t             sizeInBytes,
                                        versionBlock         ver,
                                        uint64_t             flags,
                                        ptrRaidHandleToScan* beginningOfList)
{
    eReturnValues returnValue     = SUCCESS;
    uint32_t      numberOfDevices = UINT32_C(0);
    CSMI_HANDLE   fd              = CSMI_INVALID_HANDLE;
#    if defined(_WIN32)
    DECLARE_ZERO_INIT_ARRAY(TCHAR, deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH);
#    else
    DECLARE_ZERO_INIT_ARRAY(char, deviceName, CSMI_NIX_MAX_DEVICE_NAME_LENGTH);
#    endif
    eVerbosityLevels csmiListVerbosity = VERBOSITY_DEFAULT; // If debugging, change this and down below where this is
                                                            // set per device will also need changing

    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES)
    {
        csmiListVerbosity = VERBOSITY_COMMAND_NAMES;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE)
    {
        csmiListVerbosity = VERBOSITY_COMMAND_VERBOSE;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_BUFFERS)
    {
        csmiListVerbosity = VERBOSITY_BUFFERS;
    }

#    if defined(CSMI_DEBUG)
    printf("GDL: Begin\n");
#    endif // CSMI_DEBUG
    if (!beginningOfList || !*beginningOfList)
    {
        // don't do anything. Only scan when we get a list to use.
        // Each OS that want's to do this should generate a list of handles to look for.
#    if defined(CSMI_DEBUG)
        printf("GDL: no list provided\n");
#    endif // CSMI_DEBUG
        return SUCCESS;
    }
    DISABLE_NONNULL_COMPARE
    if (ptrToDeviceList == M_NULLPTR || sizeInBytes == 0)
    {
#    if defined(CSMI_DEBUG)
        printf("GDL: Invalid size for list\n");
#    endif // CSMI_DEBUG
        returnValue = BAD_PARAMETER;
    }
    else if ((!(validate_Device_Struct(ver))))
    {
#    if defined(CSMI_DEBUG)
        printf("GDL: Invalid device structure\n");
#    endif // CSMI_DEBUG
        returnValue = LIBRARY_MISMATCH;
    }
    else
    {
        tDevice*            d                     = M_NULLPTR;
        ptrRaidHandleToScan raidList              = *beginningOfList;
        ptrRaidHandleToScan previousRaidListEntry = M_NULLPTR;
        uint32_t            controllerNumber      = UINT32_C(0);
        uint32_t            found                 = UINT32_C(0);
        uint32_t            failedGetDeviceCount  = UINT32_C(0);
        numberOfDevices                           = sizeInBytes / sizeof(tDevice);
        d                                         = ptrToDeviceList;

        // On non-Windows systems, we also have to check controller numbers...so there is one extra top-level loop for
        // this on these systems.
#    if defined(CSMI_DEBUG)
        printf("GDL: beginning scan of raid list\n");
#    endif // CSMI_DEBUG
        while (raidList && found < numberOfDevices)
        {
            bool handleRemoved = false;
            if (raidList->raidHint.csmiRAID || raidList->raidHint.unknownRAID)
            {
                eCSMISecurityAccess csmiAccess = CSMI_SECURITY_ACCESS_NONE; // only really needed in Windows - TJE
#    if defined(_WIN32)
                // Get the controller number from the scsi handle since we need it later!
                char* endHandle      = M_NULLPTR;
                char* scanhandle     = raidList->handle;
                char* scsiPortHandle = strstr(scanhandle, "\\\\.\\SCSI");
                if (scsiPortHandle)
                {
                    scanhandle += safe_strlen("\\\\.\\SCSI");
                    unsigned long ctrlnum = 0UL;
                    if (0 != safe_strtoul(&ctrlnum, scanhandle, &endHandle, BASE_10_DECIMAL))
                    {
                        return FAILURE;
                    }
                    if (endHandle && safe_strlen(endHandle) >= SIZE_T_C(1))
                    {
                        if (strcmp(endHandle, ":") != 0)
                        {
                            return FAILURE;
                        }
                    }
                    controllerNumber = C_CAST(uint32_t, ctrlnum);
                }
                else
                {
                    printf("WARNING: Unable to scan controller number! raid handle = %s\n", raidList->handle);
                }

                _stprintf_s(deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs"), raidList->handle);
                // lets try to open the controller.
                fd = CreateFile(deviceName,
                                GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#        if !defined(WINDOWS_DISABLE_OVERLAPPED)
                                FILE_FLAG_OVERLAPPED,
#        else  // WINDOWS_DISABLE_OVERLAPPED
                                0,
#        endif // WINDOWS_DISABLE_OVERLAPPED
                                M_NULLPTR);
                if (fd != INVALID_HANDLE_VALUE)
#    else  //_WIN32
                snprintf_err_handle(deviceName, CSMI_NIX_MAX_DEVICE_NAME_LENGTH, "%s", raidList->handle);
                if ((fd = open(deviceName, O_RDWR | O_NONBLOCK)) >= 0)
#    endif //_WIN32
                {
#    if defined(CSMI_DEBUG)
                    printf("GDL: Handle valid and opened\n");
#    endif // CSMI_DEBUG
#    if !defined(_WIN32)
                    for (controllerNumber = 0; controllerNumber < OPENSEA_MAX_CONTROLLERS && found < numberOfDevices;
                         ++controllerNumber)
                    {
#    endif //_WIN32
           // first, check if this handle supports CSMI before we try anything else
                        CSMI_SAS_DRIVER_INFO_BUFFER  driverInfo;
                        CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
                        CSMI_SAS_CNTLR_STATUS_BUFFER controllerStatus;
                        safe_memset(&driverInfo, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER), 0,
                                    sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
                        safe_memset(&controllerConfig, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER), 0,
                                    sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
                        safe_memset(&controllerStatus, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER), 0,
                                    sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER));
                        csmiListVerbosity =
                            d->deviceVerbosity; // this is to preserve any verbosity set when coming into this function
#    if defined(CSMI_DEBUG)
                        printf("GDL: Getting controller config, controller status, and driver info\n");
#    endif // CSMI_DEBUG
                        if (SUCCESS == csmi_Get_Basic_Info(fd, controllerNumber, &driverInfo, &controllerConfig,
                                                           &controllerStatus, csmiListVerbosity))
                        {
                            eKnownCSMIDriver knownCSMIDriver = get_Known_CSMI_Driver_Type(&driverInfo.Information);
#    if defined(CSMI_DEBUG)
                            printf("GDL: Getting driver security access\n");
#    endif // CSMI_DEBUG
                            csmiAccess = get_CSMI_Security_Access(C_CAST(char*, driverInfo.Information.szName));
                            switch (csmiAccess)
                            {
                            case CSMI_SECURITY_ACCESS_NONE:
                                printf("CSMI Security access set to none! Won't be able to properly communicate with "
                                       "the device(s)!\n");
                                break;
                            case CSMI_SECURITY_ACCESS_RESTRICTED:
                                printf("CSMI Security access set to restricted! Won't be able to properly communicate "
                                       "with the device(s)!\n");
                                break;
                            case CSMI_SECURITY_ACCESS_LIMITED:
                                printf("CSMI Security access set to limited! Won't be able to properly communicate "
                                       "with the device(s)!\n");
                                break;
                            case CSMI_SECURITY_ACCESS_FULL:
                            default:
#    if defined(CSMI_DEBUG)
                                printf("GDL: Full security access available for this driver\n");
#    endif // CSMI_DEBUG
                                break;
                            }
#    if defined(CSMI_DEBUG)
                            printf("GDL: Checking controller flags\n");
#    endif // CSMI_DEBUG
           // Check if it's a RAID capable controller. We only want to enumerate devices on those in this function
                            if ((controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID ||
                                 controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID ||
                                 controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY) &&
                                knownCSMIDriver != CSMI_DRIVER_ARCSAS)
                            {
                                // Get RAID info & Phy info. Need to match the RAID config (below) to some of the phy
                                // info as best we can...-TJE
#    if defined(_WIN32)
                                bool isIntelDriver = false;
#    endif //_WIN32
                                CSMI_SAS_PHY_INFO_BUFFER  phyInfo;
                                CSMI_SAS_RAID_INFO_BUFFER csmiRAIDInfo;
#    if defined(CSMI_DEBUG)
                                printf("GDL: getting RAID info\n");
#    endif // CSMI_DEBUG
                                csmi_Get_RAID_Info(fd, controllerNumber, &csmiRAIDInfo, csmiListVerbosity);
#    if defined(CSMI_DEBUG)
                                printf("GDL: Getting phy info\n");
#    endif // CSMI_DEBUG
           // NOTE: AMD's rcraid driver seems to treat non-raid drives slightly different in the phy info output. In the
           // case I observed, the raid drives were in the phy info, but the
           //       non-RAID drive was not shown here at all. However in the RAID IOCTLs, it does show as a separate
           //       single drive raid (but no MN or SN info to match to it). So it does not appear possible to issue a
           //       CSMI passthrough command to non-raid drives. This is completely opposite of what the Intel drivers
           //       do. Intel's drivers show every drive attached, RAID or non-RAID in the phy info. This may be
           //       something we want to detect in the future to reduce the number of IOCTLs sent, but for now, it works
           //       ok. We can optimize this more later. -TJE
                                csmi_Get_Phy_Info(fd, controllerNumber, &phyInfo, csmiListVerbosity);
#    if defined(_WIN32)
                                if (knownCSMIDriver == CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY ||
                                    knownCSMIDriver == CSMI_DRIVER_INTEL_VROC)
                                {
                                    isIntelDriver = true;
                                }
#    endif //_WIN32
           // Get RAID config
#    if defined(CSMI_DEBUG)
                                printf("GDL: Checking RAID configs. Number of RAID sets: %" CPRIu32 "\n",
                                       csmiRAIDInfo.Information.uNumRaidSets);
#    endif // CSMI_DEBUG
                                bool raidInfoIncomplete = false;
                                // array is used to track which phys have already been scanned and matched correctly
                                bool     matchedPhys[32] = {false, false, false, false, false, false, false, false,
                                                            false, false, false, false, false, false, false, false,
                                                            false, false, false, false, false, false, false, false,
                                                            false, false, false, false, false, false, false, false};
                                uint32_t raidSet         = UINT32_C(0);
                                uint32_t numRaidSets     = csmiRAIDInfo.Information.uNumRaidSets;
                                if (knownCSMIDriver == CSMI_DRIVER_ARCSAS)
                                {
                                    raidSet = UINT32_C(1);
                                    numRaidSets += UINT32_C(1);
                                }
                                for (; raidSet < numRaidSets && found < numberOfDevices; ++raidSet)
                                {
                                    // start with a length that adds no padding for extra drives, then reallocate to a
                                    // new size when we know the new size
                                    uint32_t raidConfigLength =
                                        C_CAST(uint32_t, sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) +
                                                             csmiRAIDInfo.Information.uMaxDrivesPerSet *
                                                                 sizeof(CSMI_SAS_RAID_DRIVES));
                                    PCSMI_SAS_RAID_CONFIG_BUFFER csmiRAIDConfig = C_CAST(
                                        PCSMI_SAS_RAID_CONFIG_BUFFER, safe_calloc(raidConfigLength, sizeof(uint8_t)));
                                    if (csmiRAIDConfig)
                                    {
#    if defined(CSMI_DEBUG)
                                        printf("GDL: getting RAID config\n");
#    endif // CSMI_DEBUG
                                        if (SUCCESS ==
                                            csmi_Get_RAID_Config(fd, controllerNumber, csmiRAIDConfig, raidConfigLength,
                                                                 raidSet, CSMI_SAS_RAID_DATA_DRIVES, csmiListVerbosity))
                                        {
#    if defined(CSMI_DEBUG)
                                            printf("GDL: Checking drive usage in each RAID config. Max Drives per set: "
                                                   "%" CPRIu32 "\tDrive Count: %" CPRIu8 "\n",
                                                   csmiRAIDInfo.Information.uMaxDrivesPerSet,
                                                   csmiRAIDConfig->Configuration.bDriveCount);
#    endif // CSMI_DEBUG
           // make sure we got all the drive information...if now, we need to reallocate with some more memory
                                            for (uint32_t iter = UINT32_C(0);
                                                 iter < csmiRAIDConfig->Configuration.bDriveCount &&
                                                 iter < csmiRAIDInfo.Information.uMaxDrivesPerSet &&
                                                 found < numberOfDevices;
                                                 ++iter)
                                            {
                                                bool foundDevice = false;
                                                DECLARE_ZERO_INIT_ARRAY(char, handle, RAID_HANDLE_STRING_MAX_LEN);
                                                bool driveInfoValid =
                                                    true; // for version 81 and earlier, assume this is true.
#    if defined(CSMI_DEBUG)
                                                printf("GDL: Checking CSMI Revision: %" CPRIu16 ".%" CPRIu16 "\n",
                                                       driverInfo.Information.usCSMIMajorRevision,
                                                       driverInfo.Information.usCSMIMinorRevision);
#    endif // CSMI_DEBUG
                                                if (driverInfo.Information.usCSMIMajorRevision > 0 ||
                                                    driverInfo.Information.usCSMIMinorRevision > 81)
                                                {
#    if defined(CSMI_DEBUG)
                                                    printf("GDL: CSMI Minor rev > 81, so checking bDataType\n");
#    endif // CSMI_DEBUG
                                                    switch (csmiRAIDConfig->Configuration.bDataType)
                                                    {
                                                    case CSMI_SAS_RAID_DATA_DRIVES:
                                                        break;
                                                    case CSMI_SAS_RAID_DATA_DEVICE_ID:
                                                    case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
                                                    default:
                                                        driveInfoValid = false;
                                                        break;
                                                    }
                                                }
                                                if (driveInfoValid)
                                                {
                                                    switch (csmiRAIDConfig->Configuration.Drives[iter].bDriveUsage)
                                                    {
                                                    case CSMI_SAS_DRIVE_CONFIG_NOT_USED:
                                                        // Don't count drives with this flag, because they are not
                                                        // configured in a RAID at this time. We only want those
                                                        // configured in a RAID/RAID-like scenario.
#    if defined(CSMI_DEBUG)
                                                        printf("GDL: Not used. Skipping...\n");
#    endif // CSMI_DEBUG
                                                        break;
                                                    case CSMI_SAS_DRIVE_CONFIG_MEMBER:
                                                    case CSMI_SAS_DRIVE_CONFIG_SPARE:
                                                    case CSMI_SAS_DRIVE_CONFIG_SPARE_ACTIVE:
                                                    case CSMI_SAS_DRIVE_CONFIG_SRT_CACHE:
                                                    case CSMI_SAS_DRIVE_CONFIG_SRT_DATA:
                                                        // Need to setup a handle and try get_Device to see if it works.
                                                        // NOTE: Need to know if on intel AND if model contains "NVMe"
                                                        // because we need to setup that differently to discover it
                                                        // properly
#    if defined(CSMI_DEBUG)
                                                        printf("GDL: Valid drive to use.\n");
#    endif // CSMI_DEBUG
#    if defined(_WIN32)
                                                        if (isIntelDriver &&
                                                            strncmp(
                                                                C_CAST(
                                                                    const char*,
                                                                    csmiRAIDConfig->Configuration.Drives[iter].bModel),
                                                                "NVMe", 4) == 0)
                                                        {
                                                            // This should only happen on Intel Drivers using SRT
                                                            // The SAS Address holds port-target-lun data in it. NOTE:
                                                            // This is correct for this version of the driver, but this
                                                            // is not necessarily true for previous RST drivers
                                                            // according to documentation received from Intel. -TJE
                                                            uint8_t path   = UINT8_C(0);
                                                            uint8_t target = UINT8_C(0);
                                                            uint8_t lun    = UINT8_C(0);
                                                            lun            = csmiRAIDConfig->Configuration.Drives[iter]
                                                                      .bSASAddress[0];
                                                            target = csmiRAIDConfig->Configuration.Drives[iter]
                                                                         .bSASAddress[1];
                                                            path = csmiRAIDConfig->Configuration.Drives[iter]
                                                                       .bSASAddress[2];
                                                            // don't know which bytes hold target and lun...leaving as
                                                            // zero since they are TECHNICALLY reserved in the
                                                            // documentation
                                                            //\\.\SCSI?: number is needed in windows, this is the
                                                            // controllerNumber in Windows.
                                                            snprintf_err_handle(handle, RAID_HANDLE_STRING_MAX_LEN,
                                                                                "csmi:%" CPRIu8 ":N:%" CPRIu8
                                                                                ":%" CPRIu8 ":%" CPRIu8,
                                                                                controllerNumber, path, target, lun);
                                                            foundDevice = true;
#        if defined(CSMI_DEBUG)
                                                            printf(
                                                                "GDL: Intel NVMe detected, setting up handle as %s\n",
                                                                handle);
#        endif // CSMI_DEBUG
                                                        }
                                                        else // SAS or SATA drive
#    endif                                                   //_WIN32
                                                        {
#    if defined(CSMI_DEBUG)
                                                            printf("GDL: Standard CSMI detected. Checking phy info. "
                                                                   "Number of Phys: %" CPRIu8 "\n",
                                                                   phyInfo.Information.bNumberOfPhys);
#    endif // CSMI_DEBUG
           // Compare this drive info to phy info as best we can using SASAddress field.
           // NOTE: If this doesn't work on some controllers, then this will get even more complicated as we will need
           // to try other CSMI commands and attempt reading drive identify or inquiry data to make the match
           // correctly!!! Loop through phy info and find matching SAS address...should only occur ONCE even with
           // multiple Luns since they attach to the same Phy
                                                            for (uint8_t phyIter = UINT8_C(0), physFound = UINT8_C(0);
                                                                 !foundDevice && phyIter < UINT8_C(32) &&
                                                                 physFound < phyInfo.Information.bNumberOfPhys;
                                                                 ++phyIter)
                                                            {
#    if defined(CSMI_DEBUG)
                                                                printf("GDL: Comparing SAS address to RAID config SAS "
                                                                       "address\n");
#    endif // CSMI_DEBUG
                                                                if (phyInfo.Information.Phy[phyIter]
                                                                        .Attached.bDeviceType ==
                                                                    CSMI_SAS_NO_DEVICE_ATTACHED)
                                                                {
                                                                    // nothing here, so continue
#    if defined(CSMI_DEBUG)
                                                                    printf("GDL: skipping %" PRIu8
                                                                           " as attached data shows no device "
                                                                           "connected.\n",
                                                                           phyIter);
#    endif // CSMI_DEBUG
                                                                    continue;
                                                                }
                                                                ++physFound; // increment since we have found a valid
                                                                             // phy to check information on.
                                                                if (matchedPhys[phyIter] == false)
                                                                {
                                                                    // NOTE: SATA controllers will set SASAddress to
                                                                    // zero (unless It's Intel, they fill this in
                                                                    // anyways), so this is not enough of a check.
                                                                    //       If there is a non-zero SASAddress, use it.
                                                                    //       Otherwise, we need to roll back to matching
                                                                    //       MN, SN, with an Identify command -TJE
                                                                    // Removed special case for Intel since that seems
                                                                    // to only work for specific versions of the drive
                                                                    // and specific configurations.
                                                                    if ((!is_Empty(
                                                                             csmiRAIDConfig->Configuration.Drives[iter]
                                                                                 .bSASAddress,
                                                                             8) &&
                                                                         !is_Empty(phyInfo.Information.Phy[phyIter]
                                                                                       .Attached.bSASAddress,
                                                                                   8) &&
                                                                         memcmp(
                                                                             phyInfo.Information.Phy[phyIter]
                                                                                 .Attached.bSASAddress,
                                                                             csmiRAIDConfig->Configuration.Drives[iter]
                                                                                 .bSASAddress,
                                                                             8) == 0))
                                                                    {
#    if defined(CSMI_DEBUG)
                                                                        printf("GDL: Matching SAS address in Phy info "
                                                                               "found\n");
#    endif // CSMI_DEBUG
                                                                        uint8_t lun = UINT8_C(0);
                                                                        if (!is_Empty(csmiRAIDConfig->Configuration
                                                                                          .Drives[iter]
                                                                                          .bSASLun,
                                                                                      8)) // Check if there is a lun
                                                                                          // value...should be zero on
                                                                                          // SATA and single Lun SAS
                                                                                          // drives...otherwise we'll
                                                                                          // need to convert it!
                                                                        {
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: Converting SASLun value\n");
#    endif // CSMI_DEBUG
           // This would be a multi-lun SAS drive. This device and the driver should actually be able to translate
           // SASAddress and SASLun to a SCSI address for us.
                                                                            CSMI_SAS_GET_SCSI_ADDRESS_BUFFER
                                                                            scsiAddress;
                                                                            if (SUCCESS ==
                                                                                csmi_Get_SCSI_Address(
                                                                                    fd, controllerNumber, &scsiAddress,
                                                                                    csmiRAIDConfig->Configuration
                                                                                        .Drives[iter]
                                                                                        .bSASAddress,
                                                                                    csmiRAIDConfig->Configuration
                                                                                        .Drives[iter]
                                                                                        .bSASLun,
                                                                                    VERBOSITY_DEFAULT))
                                                                            {
                                                                                lun = scsiAddress.bLun;
#    if defined(CSMI_DEBUG)
                                                                                printf("GDL: lun converted to %" PRIu8
                                                                                       "\n",
                                                                                       lun);
#    endif // CSMI_DEBUG
                                                                            }
                                                                            else
                                                                            {
#    if defined(CSMI_DEBUG)
                                                                                printf("GDL: Error converting SASLun "
                                                                                       "to SCSI Address lun!\n");
#    endif // CSMI_DEBUG
                                                                            }
                                                                        }
                                                                        switch (phyInfo.Information.Phy[phyIter]
                                                                                    .Attached.bDeviceType)
                                                                        {
                                                                        case CSMI_SAS_END_DEVICE:
                                                                            foundDevice = true;
                                                                            snprintf_err_handle(
                                                                                handle, RAID_HANDLE_STRING_MAX_LEN,
                                                                                "csmi:%" CPRIu8 ":%" CPRIu8 ":%" CPRIu8
                                                                                ":%" CPRIu8,
                                                                                controllerNumber,
                                                                                phyInfo.Information.Phy[phyIter]
                                                                                    .bPortIdentifier,
                                                                                phyInfo.Information.Phy[phyIter]
                                                                                    .Attached.bPhyIdentifier,
                                                                                lun);
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: End device handle found and "
                                                                                   "set as %s\n",
                                                                                   handle);
#    endif // CSMI_DEBUG
                                                                            matchedPhys[phyIter] = true;
                                                                            break;
                                                                            // NOLINTBEGIN(bugprone-branch-clone)
                                                                        case CSMI_SAS_NO_DEVICE_ATTACHED:
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: No device attached. "
                                                                                   "Skipping...\n");
#    endif // CSMI_DEBUG
                                                                            break;
                                                                        case CSMI_SAS_EDGE_EXPANDER_DEVICE:
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: Edge expander. Skipping...\n");
#    endif // CSMI_DEBUG
                                                                            break;
                                                                        case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: Fanout Expander Device. "
                                                                                   "Skipping...\n");
#    endif // CSMI_DEBUG
                                                                            break;
                                                                        default:
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: Unknown device type: %" CPRIu8
                                                                                   "\n",
                                                                                   phyInfo.Information.Phy[phyIter]
                                                                                       .Attached.bDeviceType);
#    endif // CSMI_DEBUG
                                                                            break;
                                                                            // NOLINTEND(bugprone-branch-clone)
                                                                        }
                                                                    }
                                                                    else if ((is_Empty(csmiRAIDConfig->Configuration
                                                                                           .Drives[iter]
                                                                                           .bSASAddress,
                                                                                       8) ||
                                                                              is_Empty(phyInfo.Information.Phy[phyIter]
                                                                                           .Attached.bSASAddress,
                                                                                       8)) // SAS address is empty
                                                                             && !is_Empty(csmiRAIDConfig->Configuration
                                                                                              .Drives[iter]
                                                                                              .bModel,
                                                                                          40) &&
                                                                             !is_Empty(csmiRAIDConfig->Configuration
                                                                                           .Drives[iter]
                                                                                           .bSerialNumber,
                                                                                       40)) // MN and SN are NOT empty
                                                                    {
                                                                        // This is most likely a SATA drive on a SATA
                                                                        // controller. Since we do not have a SAS
                                                                        // address to use for matching, we need to issue
                                                                        // an identify command and match the MN and SN.
#    if defined(CSMI_DEBUG)
                                                                        printf("GDL: No SASAddress, so matching with "
                                                                               "identify command\n");
#    endif // CSMI_DEBUG
                                                                        DECLARE_ZERO_INIT_ARRAY(char, csmiRaidDevModel,
                                                                                                41);
                                                                        DECLARE_ZERO_INIT_ARRAY(char, csmiRaidDevSerial,
                                                                                                41);
                                                                        snprintf_err_handle(
                                                                            csmiRaidDevModel, 41, "%s",
                                                                            csmiRAIDConfig->Configuration.Drives[iter]
                                                                                .bModel);
                                                                        snprintf_err_handle(
                                                                            csmiRaidDevSerial, 41, "%s",
                                                                            csmiRAIDConfig->Configuration.Drives[iter]
                                                                                .bSerialNumber);
                                                                        remove_Leading_And_Trailing_Whitespace(
                                                                            csmiRaidDevModel);
                                                                        remove_Leading_And_Trailing_Whitespace(
                                                                            csmiRaidDevSerial);
                                                                        // Creating a temporary tDevice structure to use
                                                                        // for the passthrough commands.-TJE
                                                                        tDevice tempDevice;
                                                                        tempDevice.os_info.minimumAlignment = sizeof(
                                                                            void*); // setting alignment this way to be
                                                                                    // compatible across OSs since CSMI
                                                                                    // doesn't really dictate an
                                                                                    // alignment, but we should set
                                                                                    // something. - TJE
                                                                        tempDevice.issue_io =
                                                                            C_CAST(issue_io_func, send_CSMI_IO);
                                                                        tempDevice.drive_info.drive_type =
                                                                            SCSI_DRIVE; // assume SCSI for now. Can be
                                                                                        // changed later
                                                                        tempDevice.drive_info.interface_type =
                                                                            RAID_INTERFACE;
                                                                        tempDevice.os_info.csmiDeviceData = C_CAST(
                                                                            ptrCsmiDeviceInfo,
                                                                            safe_calloc(1, sizeof(csmiDeviceInfo)));
                                                                        if (!tempDevice.os_info.csmiDeviceData)
                                                                        {
#    if defined(CSMI_DEBUG)
                                                                            printf("GRL: Failed to allocate "
                                                                                   "csmiDeviceInfo structure\n");
#    endif // CSMI_DEBUG
                                                                            return MEMORY_FAILURE;
                                                                        }
                                                                        tempDevice.os_info.csmiDeviceData
                                                                            ->csmiDevHandle = fd;
                                                                        tempDevice.os_info.csmiDeviceData
                                                                            ->controllerNumber = controllerNumber;
                                                                        tempDevice.os_info.csmiDeviceData
                                                                            ->csmiDeviceInfoValid = true;
                                                                        ScsiIoCtx csmiPTCmd;
                                                                        safe_memset(&csmiPTCmd, sizeof(ScsiIoCtx), 0,
                                                                                    sizeof(ScsiIoCtx));
                                                                        csmiPTCmd.device    = &tempDevice;
                                                                        csmiPTCmd.timeout   = 15;
                                                                        csmiPTCmd.direction = XFER_DATA_IN;
                                                                        csmiPTCmd.psense =
                                                                            tempDevice.drive_info.lastCommandSenseData;
                                                                        csmiPTCmd.senseDataSize = SPC3_SENSE_LEN;
                                                                        // Don't have a SAS Address to match to, so we
                                                                        // need to send an identify or inquiry to the
                                                                        // device to see if it is the same MN, then
                                                                        // check the SN. NOTE: This will not work if we
                                                                        // don't already know the sasLUN for SAS drives.
                                                                        // SATA will be ok though.
                                                                        tempDevice.os_info.csmiDeviceData
                                                                            ->portIdentifier =
                                                                            phyInfo.Information.Phy[phyIter]
                                                                                .bPortIdentifier;
                                                                        tempDevice.os_info.csmiDeviceData
                                                                            ->phyIdentifier =
                                                                            phyInfo.Information.Phy[phyIter]
                                                                                .Attached.bPhyIdentifier;
                                                                        tempDevice.os_info.csmiDeviceData
                                                                            ->portProtocol =
                                                                            phyInfo.Information.Phy[phyIter]
                                                                                .Attached.bTargetPortProtocol;
                                                                        safe_memcpy(&tempDevice.os_info.csmiDeviceData
                                                                                         ->sasAddress[0],
                                                                                    8,
                                                                                    phyInfo.Information.Phy[phyIter]
                                                                                        .Attached.bSASAddress,
                                                                                    8);
                                                                        // Attempt passthrough command and compare
                                                                        // identifying data. for this to work, SCSIIoCTX
                                                                        // structure must be manually defined for what
                                                                        // we want to do right now and call the CSMI IO
                                                                        // directly...not great, but don't want to have
                                                                        // other force flags elsewhere at the moment-
                                                                        // TJE
                                                                        if (phyInfo.Information.Phy[phyIter]
                                                                                    .Attached.bTargetPortProtocol &
                                                                                CSMI_SAS_PROTOCOL_SATA ||
                                                                            phyInfo.Information.Phy[phyIter]
                                                                                    .Attached.bTargetPortProtocol &
                                                                                CSMI_SAS_PROTOCOL_STP)
                                                                        {
                                                                            // ATA identify
                                                                            DECLARE_ZERO_INIT_ARRAY(uint8_t,
                                                                                                    identifyData, 512);
                                                                            ataPassthroughCommand identify =
                                                                                create_ata_pio_in_cmd(
                                                                                    &tempDevice, ATA_IDENTIFY, false, 1,
                                                                                    identifyData, 512);
                                                                            csmiPTCmd.pdata       = identifyData;
                                                                            csmiPTCmd.dataLength  = 512;
                                                                            csmiPTCmd.pAtaCmdOpts = &identify;
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: Detected SATA protocol. "
                                                                                   "Attempting Identify CMD\n");
#    endif // CSMI_DEBUG
                                                                            if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                                                            {
                                                                                // compare MN and SN...if match, then we
                                                                                // have found the drive!
                                                                                DECLARE_ZERO_INIT_ARRAY(
                                                                                    char, ataMN,
                                                                                    ATA_IDENTIFY_MN_LENGTH + 1);
                                                                                DECLARE_ZERO_INIT_ARRAY(
                                                                                    char, ataSN,
                                                                                    ATA_IDENTIFY_SN_LENGTH + 1);
                                                                                DECLARE_ZERO_INIT_ARRAY(
                                                                                    char, ataFW,
                                                                                    ATA_IDENTIFY_FW_LENGTH + 1);
                                                                                fill_ATA_Strings_From_Identify_Data(
                                                                                    identifyData, ataMN, ataSN, ataFW);
                                                                                // check for a match
#    if defined(CSMI_DEBUG)
                                                                                printf("GDL: Identify Successful\n");
#    endif // CSMI_DEBUG
                                                                                if (strstr(ataMN, csmiRaidDevModel) &&
                                                                                    strstr(ataSN, csmiRaidDevSerial))
                                                                                {
                                                                                    // found a match!
#    if defined(CSMI_DEBUG)
                                                                                    printf("GDL: Found a matching "
                                                                                           "MN/SN!\n");
#    endif // CSMI_DEBUG
                                                                                    snprintf_err_handle(
                                                                                        handle,
                                                                                        RAID_HANDLE_STRING_MAX_LEN,
                                                                                        "csmi:%" CPRIu8 ":%" CPRIu8
                                                                                        ":%" CPRIu8 ":%" CPRIu8,
                                                                                        controllerNumber,
                                                                                        phyInfo.Information.Phy[phyIter]
                                                                                            .bPortIdentifier,
                                                                                        phyInfo.Information.Phy[phyIter]
                                                                                            .Attached.bPhyIdentifier,
                                                                                        0);
                                                                                    matchedPhys[phyIter] = true;
#    if defined(CSMI_DEBUG)
                                                                                    printf("GDL: End device handle "
                                                                                           "found and set as %s\n",
                                                                                           handle);
#    endif // CSMI_DEBUG
                                                                                    foundDevice = true;
                                                                                }
                                                                            }
                                                                            else
                                                                            {
                                                                                // possibly an ATAPI drive
                                                                                identify.tfr.CommandStatus =
                                                                                    ATAPI_IDENTIFY;
                                                                                if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                                                                {
                                                                                    // compare MN and SN...if match,
                                                                                    // then we have found the drive!
                                                                                    DECLARE_ZERO_INIT_ARRAY(
                                                                                        char, ataMN,
                                                                                        ATA_IDENTIFY_MN_LENGTH + 1);
                                                                                    DECLARE_ZERO_INIT_ARRAY(
                                                                                        char, ataSN,
                                                                                        ATA_IDENTIFY_SN_LENGTH + 1);
                                                                                    DECLARE_ZERO_INIT_ARRAY(
                                                                                        char, ataFW,
                                                                                        ATA_IDENTIFY_FW_LENGTH + 1);
                                                                                    fill_ATA_Strings_From_Identify_Data(
                                                                                        identifyData, ataMN, ataSN,
                                                                                        ataFW);
                                                                                    // check for a match
#    if defined(CSMI_DEBUG)
                                                                                    printf("GDL: ATAPI Identify "
                                                                                           "Successful\n");
                                                                                    printf(
                                                                                        "GDL: Not adding to the list "
                                                                                        "since ATAPI should use system "
                                                                                        "handle instead.\n");
#    endif // CSMI_DEBUG
                                                                                    if (strstr(ataMN,
                                                                                               csmiRaidDevModel) &&
                                                                                        strstr(ataSN,
                                                                                               csmiRaidDevSerial))
                                                                                    {
                                                                                        // found a match!
#    if defined(CSMI_DEBUG)
                                                                                        printf("GDL: Found a matching "
                                                                                               "MN/SN!\n");
#    endif // CSMI_DEBUG
                                                                                        snprintf_err_handle(
                                                                                            handle,
                                                                                            RAID_HANDLE_STRING_MAX_LEN,
                                                                                            "csmi:%" CPRIu8 ":%" CPRIu8
                                                                                            ":%" CPRIu8 ":%" CPRIu8,
                                                                                            controllerNumber,
                                                                                            phyInfo.Information
                                                                                                .Phy[phyIter]
                                                                                                .bPortIdentifier,
                                                                                            phyInfo.Information
                                                                                                .Phy[phyIter]
                                                                                                .Attached
                                                                                                .bPhyIdentifier,
                                                                                            0);
                                                                                        matchedPhys[phyIter] = true;
#    if defined(CSMI_DEBUG)
                                                                                        printf("GDL: End device handle "
                                                                                               "found and set as %s\n",
                                                                                               handle);
#    endif // CSMI_DEBUG
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                        else if (phyInfo.Information.Phy[phyIter]
                                                                                     .Attached.bTargetPortProtocol &
                                                                                 CSMI_SAS_PROTOCOL_SSP)
                                                                        {
                                                                            // SCSI Inquiry and read unit serial number
                                                                            // VPD page
                                                                            DECLARE_ZERO_INIT_ARRAY(uint8_t, inqData,
                                                                                                    96);
                                                                            DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb,
                                                                                                    CDB_LEN_6);
                                                                            cdb[OPERATION_CODE] = INQUIRY_CMD;
                                                                            /*if (evpd)
                                                                            {
                                                                                cdb[1] |= BIT0;
                                                                            }*/
                                                                            cdb[2] = 0; // pageCode;
                                                                            cdb[3] = M_Byte1(96);
                                                                            cdb[4] = M_Byte0(96);
                                                                            cdb[5] = 0; // control

                                                                            csmiPTCmd.cdbLength = CDB_LEN_6;
                                                                            safe_memcpy(csmiPTCmd.cdb,
                                                                                        SCSI_IO_CTX_MAX_CDB_LEN, cdb,
                                                                                        6);
                                                                            csmiPTCmd.dataLength = 96;
                                                                            csmiPTCmd.pdata      = inqData;
#    if defined(CSMI_DEBUG)
                                                                            printf("GDL: Detected SSP protocol. "
                                                                                   "Attempting Inquiry\n");
#    endif // CSMI_DEBUG
                                                                            if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                                                            {
                                                                                // TODO: If this is a multi-LUN device,
                                                                                // this won't currently work and it may
                                                                                // not be possible to make this work if
                                                                                // we got to this case in the first
                                                                                // place. HOPEFULLY the other CSMI
                                                                                // translation IOCTLs just work and this
                                                                                // is unnecessary. - TJE If MN matches,
                                                                                // send inquiry to unit SN vpd page to
                                                                                // confirm we have a matching SN
                                                                                DECLARE_ZERO_INIT_ARRAY(char, inqVendor,
                                                                                                        9);
                                                                                DECLARE_ZERO_INIT_ARRAY(
                                                                                    char, inqProductID, 17);
                                                                                DECLARE_ZERO_INIT_ARRAY(char, vidCatPid,
                                                                                                        41);
                                                                                // DECLARE_ZERO_INIT_ARRAY(char,
                                                                                // inqProductRev, 5); copy the strings
                                                                                safe_memcpy(inqVendor, 9, &inqData[8],
                                                                                            8);
                                                                                safe_memcpy(inqProductID, 17,
                                                                                            &inqData[16], 16);
                                                                                // safe_memcpy(inqProductRev, 5,
                                                                                // &inqData[32], 4);
                                                                                snprintf_err_handle(
                                                                                    vidCatPid, 41, "%s%s", inqVendor,
                                                                                    inqProductID); // concatenate now
                                                                                                   // before removing
                                                                                                   // spaces-TJE
                                                                                // remove whitespace
                                                                                remove_Leading_And_Trailing_Whitespace(
                                                                                    inqVendor);
                                                                                remove_Leading_And_Trailing_Whitespace(
                                                                                    inqProductID);
                                                                                // remove_Leading_And_Trailing_Whitespace(inqProductRev);
                                                                                remove_Leading_And_Trailing_Whitespace(
                                                                                    vidCatPid);
#    if defined(CSMI_DEBUG)
                                                                                printf("GDL: Inquiry Successful\n");
#    endif // CSMI_DEBUG
           // For SAS drives, the model is the concatenation of the vendor
           // identification and product identification fields from a standard INQUIRY
#    if defined(CSMI_DEBUG)
                                                                                printf("GDL: Comparing Inq vid cat pid "
                                                                                       "(%s) with csmi model (%s)\n",
                                                                                       vidCatPid, csmiRaidDevSerial);
#    endif // CSMI_DEBUG
                                                                                if (strstr(inqVendor,
                                                                                           csmiRaidDevModel) &&
                                                                                    strstr(
                                                                                        inqProductID,
                                                                                        csmiRaidDevModel)) // check with
                                                                                                           // strstr
                                                                                                           // that both
                                                                                                           // the
                                                                                                           // vendorID
                                                                                                           // and model
                                                                                                           // are found
                                                                                                           // so we
                                                                                                           // don't need
                                                                                                           // to guess
                                                                                                           // on the
                                                                                                           // concatenation-TJE
                                                                                {
#    if defined(CSMI_DEBUG)
                                                                                    printf("GDL: MN/Vendor match. "
                                                                                           "Checking SN\n");
#    endif // CSMI_DEBUG
           // now read the unit SN VPD page since this matches so far that way we can compare the serial number. Not
           // checking SCSI 2 since every SAS drive *SHOULD* support this.
                                                                                    safe_memset(inqData, 96, 0, 96);
                                                                                    // change CDB to read unit SN page
                                                                                    cdb[1] |= BIT0;
                                                                                    cdb[2] = UNIT_SERIAL_NUMBER;
#    if defined(CSMI_DEBUG)
                                                                                    printf("GDL: Requesting Unit SN "
                                                                                           "page\n");
#    endif // CSMI_DEBUG
                                                                                    if (SUCCESS ==
                                                                                        send_CSMI_IO(&csmiPTCmd))
                                                                                    {
                                                                                        // check the SN
                                                                                        uint16_t serialNumberLength =
                                                                                            M_Min(M_BytesTo2ByteValue(
                                                                                                      inqData[2],
                                                                                                      inqData[3]),
                                                                                                  96) +
                                                                                            1;
                                                                                        char* serialNumber = C_CAST(
                                                                                            char*,
                                                                                            safe_calloc(
                                                                                                serialNumberLength,
                                                                                                sizeof(char)));
                                                                                        if (serialNumber)
                                                                                        {
                                                                                            safe_memcpy(
                                                                                                serialNumber,
                                                                                                serialNumberLength,
                                                                                                &inqData[4],
                                                                                                serialNumberLength -
                                                                                                    1); // minus 1 to
                                                                                                        // leave null
                                                                                                        // terminator in
                                                                                                        // tact at the
                                                                                                        // end
                                                                                            if (strcmp(
                                                                                                    serialNumber,
                                                                                                    csmiRaidDevSerial) ==
                                                                                                0)
                                                                                            {
#    if defined(CSMI_DEBUG)
                                                                                                printf(
                                                                                                    "GDL: Found a "
                                                                                                    "matching SN!\n");
#    endif // CSMI_DEBUG
           // found a match!
                                                                                                foundDevice = true;
                                                                                                snprintf_err_handle(
                                                                                                    handle,
                                                                                                    RAID_HANDLE_STRING_MAX_LEN,
                                                                                                    "csmi:%" CPRIu8
                                                                                                    ":%" CPRIu8
                                                                                                    ":%" CPRIu8
                                                                                                    ":%" CPRIu8,
                                                                                                    controllerNumber,
                                                                                                    phyInfo.Information
                                                                                                        .Phy[phyIter]
                                                                                                        .bPortIdentifier,
                                                                                                    phyInfo.Information
                                                                                                        .Phy[phyIter]
                                                                                                        .Attached
                                                                                                        .bPhyIdentifier,
                                                                                                    0);
                                                                                                matchedPhys[phyIter] =
                                                                                                    true;
#    if defined(CSMI_DEBUG)
                                                                                                printf(
                                                                                                    "GDL: End device "
                                                                                                    "handle found and "
                                                                                                    "set as %s\n",
                                                                                                    handle);
#    endif // CSMI_DEBUG
                                                                                            }
                                                                                            safe_free(&serialNumber);
                                                                                        }
                                                                                    }
                                                                                    // else...catastrophic failure? Not
                                                                                    // sure what to do here since this
                                                                                    // should be really rare to begin
                                                                                    // with.
                                                                                }
                                                                            }
                                                                        }
                                                                        safe_free_csmi_dev_info(
                                                                            &tempDevice.os_info.csmiDeviceData);
                                                                    }
                                                                    else if ((is_Empty(csmiRAIDConfig->Configuration
                                                                                           .Drives[iter]
                                                                                           .bSASAddress,
                                                                                       8) ||
                                                                              is_Empty(phyInfo.Information.Phy[phyIter]
                                                                                           .Attached.bSASAddress,
                                                                                       8)) // SAS address is empty
                                                                             && is_Empty(csmiRAIDConfig->Configuration
                                                                                             .Drives[iter]
                                                                                             .bModel,
                                                                                         40) &&
                                                                             !is_Empty(
                                                                                 csmiRAIDConfig->Configuration
                                                                                     .Drives[iter]
                                                                                     .bSerialNumber,
                                                                                 40)) // MN is empty, but SN is not.
                                                                                      // Missing drive from the set.
                                                                    {
                                                                        // in this case the MN is blank, but there is a
                                                                        // SN. So this is likely a case of a missing
                                                                        // drive, so do NOT scan the phyinfo
#    if defined(CSMI_DEBUG)
                                                                        printf("GDL: No SAS address and MN is blank, "
                                                                               "but has a SN, so likely a missing "
                                                                               "drive from the RAID set.\n");
#    endif // CSMI_DEBUG
                                                                    }
                                                                    else
                                                                    {
                                                                        raidInfoIncomplete   = true;
                                                                        matchedPhys[phyIter] = false;
#    if defined(CSMI_DEBUG)
                                                                        printf("GDL: Logging error for iter = %" PRIu8
                                                                               "\n",
                                                                               phyIter);
                                                                        printf("GDL: Cannot use SASAddress or MN+SN to "
                                                                               "match drives. Trying final "
                                                                               "possibility: PhyInfo\n");
#    endif // CSMI_DEBUG
                                                                    }
                                                                }
                                                            }
                                                        }
                                                        if (foundDevice)
                                                        {
                                                            safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                                                            d->sanity.size    = ver.size;
                                                            d->sanity.version = ver.version;
                                                            d->dFlags         = flags;
#    if defined(CSMI_DEBUG)
                                                            printf("GDL: Calling get_CSMI_RAID_Device\n");
#    endif // CSMI_DEBUG
                                                            returnValue = get_CSMI_RAID_Device(handle, d);
                                                            if (returnValue != SUCCESS)
                                                            {
#    if defined(CSMI_DEBUG)
                                                                printf("GDL: Failed to get CSMI RAID device\n");
#    endif // CSMI_DEBUG
                                                                failedGetDeviceCount++;
                                                            }
                                                            ++d;
                                                            // If we were unable to open the device using
                                                            // get_CSMI_Device, then  we need to increment the failure
                                                            // counter. - TJE
                                                            ++found;
                                                        }
                                                        break;
                                                    default:
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                        safe_free_csmi_raid_config(&csmiRAIDConfig);
                                    }
                                }
                                if (raidInfoIncomplete)
                                {
                                    // So there is not any other method that will work to match devices. So this will
                                    // rely strictly on the PhyInfo. This has a lot of draw backs. There is nothing that
                                    // can be done to ensure we find all drives, we also may end up finding duplicates,
                                    // or even extras. Solving these problems is probably possible by removing
                                    // duplicates at the end, but that is far from optimal -TJE
                                    for (uint8_t phyIter = UINT8_C(0), physFound = UINT8_C(0);
                                         phyIter < UINT8_C(32) && physFound < phyInfo.Information.bNumberOfPhys &&
                                         found < numberOfDevices;
                                         ++phyIter)
                                    {
                                        if (phyInfo.Information.Phy[phyIter].Attached.bDeviceType ==
                                            CSMI_SAS_NO_DEVICE_ATTACHED)
                                        {
                                            // nothing here, so continue
#    if defined(CSMI_DEBUG)
                                            printf("GDL: skipping %" PRIu8
                                                   " as attached data shows no device connected.\n",
                                                   phyIter);
#    endif // CSMI_DEBUG
                                            continue;
                                        }
                                        ++physFound; // increment since we have found a valid phy to check information
                                                     // on.
                                        if (!matchedPhys[phyIter]) // only do this for phys we did not already scan
                                                                   // successfully above-TJE
                                        {
#    if defined(CSMI_DEBUG)
                                            printf("Checking phy error list with phy %" PRIu8 "\n", phyIter);
#    endif // CSMI_DEBUG
           // Each attached device will be considered a "found device" in this case.
                                            DECLARE_ZERO_INIT_ARRAY(char, handle, RAID_HANDLE_STRING_MAX_LEN);
                                            snprintf_err_handle(
                                                handle, RAID_HANDLE_STRING_MAX_LEN,
                                                "csmi:%" CPRIu8 ":%" CPRIu8 ":%" CPRIu8 ":%" CPRIu8, controllerNumber,
                                                phyInfo.Information.Phy[phyIter].bPortIdentifier,
                                                phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier, 0);
#    if defined(CSMI_DEBUG)
                                            printf("GDL: Phy Info last resort device handle found and set as %s\n",
                                                   handle);
#    endif // CSMI_DEBUG
                                            safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                                            d->sanity.size     = ver.size;
                                            d->sanity.version  = ver.version;
                                            d->dFlags          = flags;
                                            d->deviceVerbosity = 4;
#    if defined(CSMI_DEBUG)
                                            printf("GDL: Calling get_CSMI_RAID_Device\n");
#    endif // CSMI_DEBUG
                                            returnValue = get_CSMI_RAID_Device(handle, d);
                                            if (returnValue != SUCCESS)
                                            {
#    if defined(CSMI_DEBUG)
                                                printf("GDL: Failed to get CSMI RAID device\n");
#    endif // CSMI_DEBUG
                                                failedGetDeviceCount++;
                                            }
                                            else if (d->drive_info.drive_type == ATAPI_DRIVE ||
                                                     d->drive_info.drive_type == UNKNOWN_DRIVE)
                                            {
                                                // ATAPI drives can show up, but we do not need to scan them with CSMI,
                                                // so check if it is ATAPI or not! ATAPI devices will not be part of a
                                                // RAID, so we do not need to add them in CSMI code. If we want to show
                                                // them in the software, windows can scan \\.\CDROM<id> handles
#    if defined(CSMI_DEBUG)
                                                printf("GDL: Found ATAPI drive. Skipping.\n");
#    endif // CSMI_DEBUG
           // memset the device because it can still show up and we do not want it to leave anything behind if we are
           // reusing the device structure for a different device.
                                                safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                                                continue;
                                            }
                                            ++d;
                                            // If we were unable to open the device using get_CSMI_Device, then  we need
                                            // to increment the failure counter. - TJE
                                            ++found;
                                        }
                                    }
                                }
                            }
                        }
#    if defined(CSMI_DEBUG)
                        printf("GDL: Updating raid list\n");
#    endif // CSMI_DEBUG
                        bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                        raidList                          = remove_RAID_Handle(raidList, previousRaidListEntry);
                        if (pointerAtBeginningOfRAIDList)
                        {
                            // if the first entry in the list was removed, we need up update the pointer before we exit
                            // so that the code that called here won't have an invalid pointer
                            *beginningOfList = raidList;
                        }
                        handleRemoved = true;
#    if !defined(_WIN32) // loop through controller numbers
                    }
#    endif //_WIN32
                }
                // close handle to the controller
#    if defined(_WIN32)
                if (fd != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(fd);
                }
#    else  //_WIN32
                if (fd < 0)
                {
                    close(fd);
                }
#    endif //_WIN32
                if (!handleRemoved)
                {
                    previousRaidListEntry =
                        raidList; // store handle we just looked at in case we need to remove one from the list
                    // increment to next element in the list
                    raidList = raidList->next;
                }
            }
        }
        if (found == failedGetDeviceCount)
        {
#    if defined(CSMI_DEBUG)
            printf("GDL: Setting failure as no RAID devices could be opened\n");
#    endif // CSMI_DEBUG
            returnValue = FAILURE;
        }
        else if (failedGetDeviceCount)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
#    if defined(CSMI_DEBUG)
            printf("GDL: Setting warning that not all device enumerated properly\n");
#    endif // CSMI_DEBUG
        }
    }
    RESTORE_NONNULL_COMPARE
    return returnValue;
}

static eReturnValues send_SSP_Passthrough_Command(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (!scsiIoCtx)
    {
        return BAD_PARAMETER;
    }
    csmiSSPIn  sspInputs;
    csmiSSPOut sspOutputs;
    DECLARE_SEATIMER(sspTimer);
    safe_memset(&sspInputs, sizeof(csmiSSPIn), 0, sizeof(csmiSSPIn));
    safe_memset(&sspOutputs, sizeof(csmiSSPOut), 0, sizeof(csmiSSPOut));

    sspInputs.cdb            = scsiIoCtx->cdb;
    sspInputs.cdbLength      = scsiIoCtx->cdbLength;
    sspInputs.connectionRate = CSMI_SAS_LINK_RATE_NEGOTIATED;
    sspInputs.dataLength     = scsiIoCtx->dataLength;
    safe_memcpy(sspInputs.destinationSASAddress, 8, scsiIoCtx->device->os_info.csmiDeviceData->sasAddress, 8);
    safe_memcpy(sspInputs.lun, 8, scsiIoCtx->device->os_info.csmiDeviceData->sasLUN, 8);
    sspInputs.phyIdentifier  = scsiIoCtx->device->os_info.csmiDeviceData->phyIdentifier;
    sspInputs.portIdentifier = scsiIoCtx->device->os_info.csmiDeviceData->portIdentifier;
    sspInputs.ptrData        = scsiIoCtx->pdata;
    sspInputs.timeoutSeconds = scsiIoCtx->timeout;

    sspInputs.flags = CSMI_SAS_SSP_TASK_ATTRIBUTE_SIMPLE; // start with this. don't really care about other attributes
    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        sspInputs.flags |= CSMI_SAS_SSP_READ;
        break;
    case XFER_DATA_OUT:
        sspInputs.flags |= CSMI_SAS_SSP_WRITE;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        sspInputs.flags |= CSMI_SAS_SSP_READ | CSMI_SAS_SSP_WRITE;
        break;
    case XFER_NO_DATA:
    default:
        sspInputs.flags |= CSMI_SAS_SSP_UNSPECIFIED;
        break;
    }

    sspOutputs.sspTimer        = &sspTimer;
    sspOutputs.senseDataLength = scsiIoCtx->senseDataSize;
    sspOutputs.senseDataPtr    = scsiIoCtx->psense;

    // issue the command
    ret = csmi_SSP_Passthrough(scsiIoCtx->device->os_info.csmiDeviceData->csmiDevHandle,
                               scsiIoCtx->device->os_info.csmiDeviceData->controllerNumber, &sspInputs, &sspOutputs,
                               scsiIoCtx->device->deviceVerbosity);

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(sspTimer);

    return ret;
}

static eReturnValues send_STP_Passthrough_Command(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (!scsiIoCtx || !scsiIoCtx->pAtaCmdOpts)
    {
        return BAD_PARAMETER;
    }
    csmiSTPIn  stpInputs;
    csmiSTPOut stpOutputs;
    sataH2DFis h2dFis;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, statusFIS, 20);
    DECLARE_SEATIMER(stpTimer);
    safe_memset(&stpInputs, sizeof(csmiSTPIn), 0, sizeof(csmiSTPIn));
    safe_memset(&stpOutputs, sizeof(csmiSTPOut), 0, sizeof(csmiSTPOut));

    // setup the FIS
    build_H2D_FIS_From_ATA_PT_Command(&h2dFis, &scsiIoCtx->pAtaCmdOpts->tfr,
                                      scsiIoCtx->device->os_info.csmiDeviceData->sataPMPort);
    stpInputs.commandFIS     = &h2dFis;
    stpInputs.dataLength     = scsiIoCtx->pAtaCmdOpts->dataSize;
    stpInputs.ptrData        = scsiIoCtx->pAtaCmdOpts->ptrData;
    stpInputs.connectionRate = CSMI_SAS_LINK_RATE_NEGOTIATED;
    stpInputs.timeoutSeconds = scsiIoCtx->pAtaCmdOpts->timeout;

    // Setup CSMI info to route command to the device.
    safe_memcpy(stpInputs.destinationSASAddress, 8, scsiIoCtx->device->os_info.csmiDeviceData->sasAddress, 8);
    stpInputs.phyIdentifier  = scsiIoCtx->device->os_info.csmiDeviceData->phyIdentifier;
    stpInputs.portIdentifier = scsiIoCtx->device->os_info.csmiDeviceData->portIdentifier;

    // setup command flags
    switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
    {
    case XFER_DATA_IN:
        stpInputs.flags |= CSMI_SAS_STP_READ;
        break;
    case XFER_DATA_OUT:
        stpInputs.flags |= CSMI_SAS_STP_WRITE;
        break;
    case XFER_NO_DATA:
    default:
        stpInputs.flags |= CSMI_SAS_STP_UNSPECIFIED;
        break;
    }

    switch (scsiIoCtx->pAtaCmdOpts->commadProtocol)
    {
    case ATA_PROTOCOL_PIO:
        stpInputs.flags |= CSMI_SAS_STP_PIO;
        break;
    case ATA_PROTOCOL_UDMA:
    case ATA_PROTOCOL_DMA:
        stpInputs.flags |= CSMI_SAS_STP_DMA;
        break;
    case ATA_PROTOCOL_DEV_DIAG:
        stpInputs.flags |= CSMI_SAS_STP_EXECUTE_DIAG; // note: cast is to remove a warning that only shows up on this
                                                      // flag due to its value.
        break;
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PACKET_DMA:
        stpInputs.flags |= CSMI_SAS_STP_PACKET;
        break;
    case ATA_PROTOCOL_DMA_QUE:
    case ATA_PROTOCOL_DMA_FPDMA:
        stpInputs.flags |= CSMI_SAS_STP_DMA_QUEUED;
        break;
    case ATA_PROTOCOL_NO_DATA:
    case ATA_PROTOCOL_DEV_RESET:
    default:
        break;
    }

    // setup some stuff for output results
    stpOutputs.statusFIS    = &statusFIS[0];
    stpOutputs.statusFISLen = 20;
    stpOutputs.stpTimer     = &stpTimer;

    // send the IO
    ret = csmi_STP_Passthrough(scsiIoCtx->device->os_info.csmiDeviceData->csmiDevHandle,
                               scsiIoCtx->device->os_info.csmiDeviceData->controllerNumber, &stpInputs, &stpOutputs,
                               scsiIoCtx->device->deviceVerbosity);

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(stpTimer);

    // Check result and copy back additional info.
    if (stpOutputs.retryAsSSPPassthrough)
    {
        // STP is not supported by this controller/driver for this device.
        // So now we need to send the IO as SSP.
        // First, try SAT (no changes), if that fails for invalid operation code, then try legacy CSMI...after that, we
        // are done retrying.
        ret = send_SSP_Passthrough_Command(
            scsiIoCtx); // This is all we should have to do since SAT style CDBs are always created by default.
        // Check the result. If it was a invalid op code, we could have passed
        if (ret == SUCCESS)
        {
            // check the sense data to see if it is an invalid command or not
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(scsiIoCtx->psense, scsiIoCtx->senseDataSize, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 &&
                ascq == 0x00) // TODO: Check if A1h vs 85h SAT opcodes for retry???
            {
                scsiIoCtx->device->drive_info.passThroughHacks.passthroughType =
                    ATA_PASSTHROUGH_CSMI; // change to the legacy passthrough
                ret = ata_Passthrough_Command(scsiIoCtx->device, scsiIoCtx->pAtaCmdOpts);
            }
        }
        else
        {
            // something else is wrong....call it a passthrough failure
            ret = OS_PASSTHROUGH_FAILURE;
        }
    }
    else if (ret == SUCCESS)
    {
        // check the status FIS, and set up the proper response
        // This FIS should be either D2H or possibly PIO Setup
        ptrSataD2HFis      d2h    = C_CAST(ptrSataD2HFis, &statusFIS[0]);
        ptrSataPIOSetupFis pioSet = C_CAST(ptrSataPIOSetupFis, &statusFIS[0]);
        ataReturnTFRs rtfrs; // create this temporarily to save fis output results, then we'll pack it into sense data
        safe_memset(&rtfrs, sizeof(ataReturnTFRs), 0, sizeof(ataReturnTFRs));
        switch (statusFIS[0])
        {
        case FIS_TYPE_REG_D2H:
            rtfrs.status    = d2h->status;
            rtfrs.error     = d2h->error;
            rtfrs.device    = d2h->device;
            rtfrs.lbaLow    = d2h->lbaLow;
            rtfrs.lbaMid    = d2h->lbaMid;
            rtfrs.lbaHi     = d2h->lbaHi;
            rtfrs.lbaLowExt = d2h->lbaLowExt;
            rtfrs.lbaMidExt = d2h->lbaMidExt;
            rtfrs.lbaHiExt  = d2h->lbaHiExt;
            rtfrs.secCnt    = d2h->sectorCount;
            rtfrs.secCntExt = d2h->sectorCountExt;
            break;
        case FIS_TYPE_PIO_SETUP:
            rtfrs.status    = pioSet->eStatus;
            rtfrs.error     = pioSet->error;
            rtfrs.device    = pioSet->device;
            rtfrs.lbaLow    = pioSet->lbaLow;
            rtfrs.lbaMid    = pioSet->lbaMid;
            rtfrs.lbaHi     = pioSet->lbaHi;
            rtfrs.lbaLowExt = pioSet->lbaLowExt;
            rtfrs.lbaMidExt = pioSet->lbaMidExt;
            rtfrs.lbaHiExt  = pioSet->lbaHiExt;
            rtfrs.secCnt    = pioSet->sectorCount;
            rtfrs.secCntExt = pioSet->sectorCountExt;
            break;
        default:
            // Unknown FIS response type
            ret = UNKNOWN;
            break;
        }
        // dummy up sense data since that is what the above layers look for. Use descriptor format.
        if (scsiIoCtx->psense) // check that the pointer is valid
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
                    scsiIoCtx->psense[12] = rtfrs.secCntExt; // Sector Count Ext
                    scsiIoCtx->psense[14] = rtfrs.lbaLowExt; // LBA Lo Ext
                    scsiIoCtx->psense[16] = rtfrs.lbaMidExt; // LBA Mid Ext
                    scsiIoCtx->psense[18] = rtfrs.lbaHiExt;  // LBA Hi
                }
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = rtfrs.error;  // Error
                scsiIoCtx->psense[13] = rtfrs.secCnt; // Sector Count
                scsiIoCtx->psense[15] = rtfrs.lbaLow; // LBA Lo
                scsiIoCtx->psense[17] = rtfrs.lbaMid; // LBA Mid
                scsiIoCtx->psense[19] = rtfrs.lbaHi;  // LBA Hi
                scsiIoCtx->psense[20] = rtfrs.device; // Device/Head
                scsiIoCtx->psense[21] = rtfrs.status; // Status
            }
        }
    }

    return ret;
}

eReturnValues send_CSMI_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA ||
                                   scsiIoCtx->device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_STP))
    {
        ret = send_STP_Passthrough_Command(scsiIoCtx);
    }
    else if (scsiIoCtx->device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SSP)
    {
        ret = send_SSP_Passthrough_Command(scsiIoCtx);
    }
    // Need case to translate SCSI CDB to ATA command!
    else if (scsiIoCtx->device->drive_info.drive_type == ATA_DRIVE)
    {
        // Software SAT translation
        ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
    }
    else
    {
        return BAD_PARAMETER;
    }
    return ret;
}

void print_CSMI_Device_Info(tDevice* device)
{
    if (device->os_info.csmiDeviceData && device->os_info.csmiDeviceData->csmiDeviceInfoValid)
    {
        // print the things we stored since those are what we currently care about. Can add printing other things out
        // later if they are determined to be of use. - TJE
        printf("\n=====CSMI Info=====\n");
        printf("\tCSMI Version: %" CPRIu16 ".%" CPRIu16 "\n", device->os_info.csmiDeviceData->csmiMajorVersion,
               device->os_info.csmiDeviceData->csmiMinorVersion);
        printf("\tSecurity Access: ");
        switch (device->os_info.csmiDeviceData->securityAccess)
        {
        case CSMI_SECURITY_ACCESS_NONE:
            printf("None\n");
            break;
        case CSMI_SECURITY_ACCESS_RESTRICTED:
            printf("Restricted\n");
            break;
        case CSMI_SECURITY_ACCESS_LIMITED:
            printf("Limited\n");
            break;
        case CSMI_SECURITY_ACCESS_FULL:
            printf("Full\n");
            break;
        }
        printf("\tCSMI Known driver type: ");
        switch (device->os_info.csmiDeviceData->csmiKnownDriverType)
        {
        case CSMI_DRIVER_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY:
            printf("Intel Rapid Storage Technology\n");
            break;
        case CSMI_DRIVER_INTEL_VROC:
            printf("Intel VROC\n");
            break;
        case CSMI_DRIVER_AMD_RCRAID:
            printf("AMD RCRAID\n");
            break;
        case CSMI_DRIVER_HPCISS:
            printf("HPCISS\n");
            break;
        case CSMI_DRIVER_ARCSAS:
            printf("ARCSAS\n");
            break;
        case CSMI_DRIVER_INTEL_RAPID_STORAGE_TECHNOLOGY_VD:
            printf("Intel RST VD\n");
            break;
        case CSMI_DRIVER_INTEL_GENERIC:
            printf("Generic Intel\n");
            break;
        case CSMI_DRIVER_HPSAMD:
            printf("HP SAMD\n");
            break;
        }
        if (device->os_info.csmiDeviceData->intelRSTSupport.intelRSTSupported &&
            device->os_info.csmiDeviceData->intelRSTSupport.nvmePassthrough)
        {
            printf("\tIntel RST NVMe device.\n");
            printf("\t\tPath ID  : %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.pathId);
            printf("\t\tTarget ID: %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.targetId);
            printf("\t\tLUN      : %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.lun);
        }
        else
        {
            printf("\tPHY ID: %" CPRIX8 "h\n", device->os_info.csmiDeviceData->phyIdentifier);
            printf("\tPort ID: %" CPRIX8 "h\n", device->os_info.csmiDeviceData->portIdentifier);
            printf("\tSupported Port Protocols:\n");
            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA)
            {
                printf("\t\tSATA\n");
            }
            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SMP)
            {
                printf("\t\tSMP\n");
            }
            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_STP)
            {
                printf("\t\tSTP\n");
            }
            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SSP)
            {
                printf("\t\tSSP\n");
            }

            printf("\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
                   "%02" CPRIX8 "%02" CPRIX8 "h\n",
                   device->os_info.csmiDeviceData->sasAddress[0], device->os_info.csmiDeviceData->sasAddress[1],
                   device->os_info.csmiDeviceData->sasAddress[2], device->os_info.csmiDeviceData->sasAddress[3],
                   device->os_info.csmiDeviceData->sasAddress[4], device->os_info.csmiDeviceData->sasAddress[5],
                   device->os_info.csmiDeviceData->sasAddress[6], device->os_info.csmiDeviceData->sasAddress[7]);
            printf("\tSAS Lun: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8
                   "%02" CPRIX8 "h\n",
                   device->os_info.csmiDeviceData->sasLUN[0], device->os_info.csmiDeviceData->sasLUN[1],
                   device->os_info.csmiDeviceData->sasLUN[2], device->os_info.csmiDeviceData->sasLUN[3],
                   device->os_info.csmiDeviceData->sasLUN[4], device->os_info.csmiDeviceData->sasLUN[5],
                   device->os_info.csmiDeviceData->sasLUN[6], device->os_info.csmiDeviceData->sasLUN[7]);
            printf("\tSCSI Address: ");
            if (device->os_info.csmiDeviceData->scsiAddressValid)
            {
                printf("\t\tHost Index: %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.hostIndex);
                printf("\t\tPath ID  : %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.pathId);
                printf("\t\tTarget ID: %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.targetId);
                printf("\t\tLUN      : %" CPRIu8 "\n", device->os_info.csmiDeviceData->scsiAddress.lun);
            }
            else
            {
                printf("Not Valid\n");
            }
            if (device->os_info.csmiDeviceData->signatureFISValid)
            {
                printf("\tSATA Signature FIS:\n");
                print_FIS(&device->os_info.csmiDeviceData->signatureFIS, H2D_FIS_LENGTH);
            }
        }
    }
    else
    {
        printf("No CSMI info, not a CSMI supporting device.\n");
    }
}

#endif // ENABLE_CSMI
