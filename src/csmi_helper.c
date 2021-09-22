//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#if defined (ENABLE_CSMI)

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <tchar.h>
#include "intel_rst_helper.h"
#else
#include <sys/ioctl.h>
#endif
#include "csmi_helper.h"
#include "csmi_helper_func.h"
#include "cmds.h"
#include "sat_helper_func.h"
#include "ata_helper_func.h"
#include "scsi_helper_func.h"
#include "common_platform.h"
#include "sata_types.h"
#include "sata_helper_func.h"

extern bool validate_Device_Struct(versionBlock);

void print_IOCTL_Return_Code(uint32_t returnCode)
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
    return;
}

#if defined (_WIN32)
void print_Last_Error(DWORD lastError)
{
    print_Windows_Error_To_Screen(lastError);
}
#else
void print_Last_Error(int lastError)
{
    print_Errno_To_Screen(lastError);
}
#endif

static int csmi_Return_To_OpenSea_Result(uint32_t returnCode)
{
    int ret = SUCCESS;
    switch (returnCode)
    {
    case CSMI_SAS_STATUS_SUCCESS:
        ret = SUCCESS;
        break;
    case CSMI_SAS_STATUS_FAILED:
        ret = FAILURE;
        break;
    case CSMI_SAS_STATUS_BAD_CNTL_CODE:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_STATUS_INVALID_PARAMETER:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_STATUS_WRITE_ATTEMPTED:
        ret = PERMISSION_DENIED;
        break;
    case CSMI_SAS_RAID_SET_OUT_OF_RANGE:
        ret = FAILURE;
        break;
    case CSMI_SAS_RAID_SET_BUFFER_TOO_SMALL:
        ret = FAILURE;
        break;
    case CSMI_SAS_RAID_SET_DATA_CHANGED: //not sure if this is just informative or an error
        ret = SUCCESS;
        break;
    case CSMI_SAS_PHY_INFO_NOT_CHANGEABLE:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_LINK_RATE_OUT_OF_RANGE:
        ret = FAILURE;
        break;
    case CSMI_SAS_PHY_DOES_NOT_EXIST:
        ret = FAILURE;
        break;
    case CSMI_SAS_PHY_DOES_NOT_MATCH_PORT:
        ret = FAILURE;
        break;
    case CSMI_SAS_PHY_CANNOT_BE_SELECTED:
        ret = FAILURE;
        break;
    case CSMI_SAS_SELECT_PHY_OR_PORT:
        ret = FAILURE;
        break;
    case CSMI_SAS_PORT_DOES_NOT_EXIST:
        ret = FAILURE;
        break;
    case CSMI_SAS_PORT_CANNOT_BE_SELECTED:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_CONNECTION_FAILED:
        ret = FAILURE;
        break;
    case CSMI_SAS_NO_SATA_DEVICE:
        ret = FAILURE;
        break;
    case CSMI_SAS_NO_SATA_SIGNATURE:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_SCSI_EMULATION:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_NOT_AN_END_DEVICE:
        ret = FAILURE;
        break;
    case CSMI_SAS_NO_SCSI_ADDRESS:
        ret = NOT_SUPPORTED;
        break;
    case CSMI_SAS_NO_DEVICE_ADDRESS:
        ret = NOT_SUPPORTED;
        break;
    default:
        ret = FAILURE;
        break;
    }
    return ret;
}

typedef struct _csmiIOin
{
    CSMI_HANDLE deviceHandle;
    void *ioctlBuffer;
    uint32_t ioctlCode;//CSMI IOCTL code. Linux needs this, Windows doesn't since it's in the header for Windows.
    uint32_t ioctlBufferSize;
    char ioctlSignature[8];//Signature of the IOCTL to send
    uint32_t timeoutInSeconds;
    uint32_t dataLength;//The length of all the data AFTER the ioctl header. This helps track how much to send/receive. The structure trying to read or write sizeof(CSMI struct) or possibly larger for those that have variable length data
    uint32_t controllerNumber;//For Linux drivers, we need to specify the controller number since the drivers may manage more than a single controller at a time. This will be ignored in Linux
    eVerbosityLevels csmiVerbosity;
    uint16_t ioctlDirection;//Is this sending data (set) or receiving data (get). Needed for Linux
}csmiIOin, *ptrCsmiIOin;

typedef struct _csmiIOout
{
    unsigned long bytesReturned;//Windows only and returned because it may be needed to fully process the result. Will be 0 for other OSs
    int sysIoctlReturn;//to save return from calling DeviceIoControl or Ioctl functions.
    uint32_t *lastError;//pointer to store last error in. Optional
    seatimer_t *ioctlTimer;//pointer to a timer to start and stop if the IOCTL needs timing.
}csmiIOout, *ptrCsmiIOout;

//static because this should be an internal function to be reused below for getting the other data
static int issue_CSMI_IO(ptrCsmiIOin csmiIoInParams, ptrCsmiIOout csmiIoOutParams)
{
    int ret = SUCCESS;
    int localIoctlReturn = 0;//This is OK in Windows because BOOL is a typedef for int
    seatimer_t *timer = NULL; 
    bool localTimer = false;
#if defined (_WIN32)
    OVERLAPPED overlappedStruct;
    DWORD lastError = 0;
#else
    int lastError = 0;
#endif
    PIOCTL_HEADER ioctlHeader = csmiIoInParams->ioctlBuffer;//ioctl buffer should point to the beginning where the header will be.
    if (!(csmiIoInParams && csmiIoOutParams && ioctlHeader))
    {
        return BAD_PARAMETER;
    }
    timer = csmiIoOutParams->ioctlTimer;
    if (!timer)
    {
        timer = (seatimer_t*)calloc(1, sizeof(seatimer_t));
        localTimer = true;
    }

    //setup the IOCTL header for each OS
    //memset to zero first
    memset(ioctlHeader, 0, sizeof(IOCTL_HEADER));
    //fill in common things
    ioctlHeader->Timeout = csmiIoInParams->timeoutInSeconds;
    ioctlHeader->ReturnCode = CSMI_SAS_STATUS_SUCCESS;
    ioctlHeader->Length = csmiIoInParams->dataLength;
    if (VERBOSITY_COMMAND_NAMES <= csmiIoInParams->csmiVerbosity)
    {
        printf("\n---Sending CSMI IO---\n");
    }
#if defined (_WIN32)
    //finish OS specific IOHEADER setup
    ioctlHeader->ControlCode = csmiIoInParams->ioctlCode;
    ioctlHeader->HeaderLength = sizeof(SRB_IO_CONTROL);
    memcpy(ioctlHeader->Signature, csmiIoInParams->ioctlSignature, 8);
    //overlapped support
    memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!overlappedStruct.hEvent)
    {
        if (localTimer)
        {
            safe_Free(timer);
        }
        return MEMORY_FAILURE;
    }
    //issue the IO
    start_Timer(timer);
    localIoctlReturn = DeviceIoControl(csmiIoInParams->deviceHandle, IOCTL_SCSI_MINIPORT, csmiIoInParams->ioctlBuffer, csmiIoInParams->ioctlBufferSize, csmiIoInParams->ioctlBuffer, csmiIoInParams->ioctlBufferSize, &csmiIoOutParams->bytesReturned, &overlappedStruct);
    if (ERROR_IO_PENDING == GetLastError())//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
    {
        localIoctlReturn = GetOverlappedResult(csmiIoInParams->deviceHandle, &overlappedStruct, &csmiIoOutParams->bytesReturned, TRUE);
    }
    else if (GetLastError() != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(timer);
    CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = NULL;
    lastError = GetLastError();
    if (csmiIoOutParams->lastError)
    {
        *csmiIoOutParams->lastError = lastError;
    }
    //print_Windows_Error_To_Screen(GetLastError());
#else //Linux or other 'nix systems
    //finish OS specific IOHEADER setup
    ioctlHeader->IOControllerNumber = csmiIoInParams->controllerNumber;
    ioctlHeader->Direction = csmiIoInParams->ioctlDirection;
    //issue the IO
    start_Timer(timer);
    localIoctlReturn = ioctl(csmiIoInParams->deviceHandle, csmiIoInParams->ioctlCode, csmiIoInParams->ioctlBuffer);
    stop_Timer(timer);
    lastError = errno;
    if (csmiIoOutParams->lastError)
    {
        *csmiIoOutParams->lastError = lastError;
    }
#endif
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
        safe_Free(timer);
    }
    return ret;
}

//More for debugging than anything else
static void print_CSMI_Driver_Info(PCSMI_SAS_DRIVER_INFO driverInfo)
{
    if (driverInfo)
    {
        printf("\n====CSMI Driver Info====\n");
        printf("\tDriver Name: %s\n", driverInfo->szName);
        printf("\tDescription: %s\n", driverInfo->szDescription);
        printf("\tDriver Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", driverInfo->usMajorRevision, driverInfo->usMinorRevision, driverInfo->usBuildRevision, driverInfo->usReleaseRevision);
        printf("\tCSMI Version: %" CPRIu16 ".%" CPRIu16 "\n", driverInfo->usCSMIMajorRevision, driverInfo->usCSMIMinorRevision);
        printf("\n");
    }
    return;
}

int csmi_Get_Driver_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(driverInfoBuffer, 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = driverInfoBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_DRIVER_INFO_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_DRIVER_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_DRIVER_INFO;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_ALL_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_ALL_SIGNATURE, strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Driver Info\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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
        printf("\tBase Memory Address: %08" CPRIX32 "%08" CPRIX32 "h\n", config->BaseMemoryAddress.uHighPart, config->BaseMemoryAddress.uLowPart);
        printf("\tBoard ID: %08" CPRIX32 "h\n", config->uBoardID);
        printf("\t\tVendor ID: %04" CPRIX16 "h\n", M_DoubleWord0(config->uBoardID));
        printf("\t\tSubsystem ID: %04" CPRIX16 "h\n", M_DoubleWord1(config->uBoardID));
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
        printf("\tController Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", config->usMajorRevision, config->usMinorRevision, config->usBuildRevision, config->usReleaseRevision);
        printf("\tBIOS Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", config->usBIOSMajorRevision, config->usBIOSMinorRevision, config->usBIOSBuildRevision, config->usBIOSReleaseRevision);
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
        printf("\tRedundant Controller Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", config->usRromMajorRevision, config->usRromMinorRevision, config->usRromBuildRevision, config->usRromReleaseRevision);
        printf("\tRedundant BIOS Version: %" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 ".%" CPRIu16 "\n", config->usRromBIOSMajorRevision, config->usRromBIOSMinorRevision, config->usRromBIOSBuildRevision, config->usRromBIOSReleaseRevision);
        printf("\n");
    }
}

int csmi_Get_Controller_Configuration(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(ctrlConfigBuffer, 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = ctrlConfigBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_CNTLR_CONFIG;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_ALL_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_ALL_SIGNATURE, strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Controller Configuration\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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
        if (status->uStatus)
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
    return;
}

int csmi_Get_Controller_Status(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CNTLR_STATUS_BUFFER ctrlStatusBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(ctrlStatusBuffer, 0, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = ctrlStatusBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_CNTLR_STATUS;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_ALL_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_ALL_SIGNATURE, strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Controller Status\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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

//NOTE: This function needs the firmwareBuffer to be allocated with additional length for the firmware to send to the controller.
//In order to make this simple, we will assume the caller has already copied the controller firmware to the buffer, but we still need the total length
int csmi_Controller_Firmware_Download(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER firmwareBuffer, uint32_t firmwareBufferTotalLength, uint32_t downloadFlags, eVerbosityLevels verbosity, uint32_t timeoutSeconds)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(firmwareBuffer, 0, sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER) - 1);//Memsetting ONLY the header and flags section so that the firmware we are sending is left untouched.

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = firmwareBuffer;
    ioIn.ioctlBufferSize = firmwareBufferTotalLength;
    ioIn.dataLength = firmwareBufferTotalLength - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_FIRMWARE_DOWNLOAD;
    ioIn.ioctlDirection = CSMI_SAS_DATA_WRITE;
    ioIn.timeoutInSeconds = timeoutSeconds;
    memcpy(ioIn.ioctlSignature, CSMI_ALL_SIGNATURE, strlen(CSMI_ALL_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    firmwareBuffer->Information.uDownloadFlags = downloadFlags;
    firmwareBuffer->Information.uBufferLength = firmwareBufferTotalLength - sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER);//-1???

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Controller Firmware Download\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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
        //Check if remaining bytes are zeros. This helps track differences between original CSMI spec and some drivers that added onto it.
        if (!is_Empty(&raidInfo->uMaxRaidSets, 92))//92 is original reserved length from original documentation...can change to something else based on actual structure size if needed
        {
            printf("\tMaximum # of RAID Sets: %" CPRIu32 "\n", raidInfo->uMaxRaidSets);
            printf("\tMaximum # of RAID Types: %" CPRIu8 "\n", raidInfo->bMaxRaidTypes);
            printf("\tMinimum RAID Set Blocks: %" PRIu64 "\n", M_DWordsTo8ByteValue(raidInfo->ulMinRaidSetBlocks.uHighPart, raidInfo->ulMinRaidSetBlocks.uLowPart));
            printf("\tMaximum RAID Set Blocks: %" PRIu64 "\n", M_DWordsTo8ByteValue(raidInfo->ulMaxRaidSetBlocks.uHighPart, raidInfo->ulMaxRaidSetBlocks.uLowPart));
            printf("\tMaximum Physical Drives: %" CPRIu32 "\n", raidInfo->uMaxPhysicalDrives);
            printf("\tMaximum Extents: %" CPRIu32 "\n", raidInfo->uMaxExtents);
            printf("\tMaximum Modules: %" CPRIu32 "\n", raidInfo->uMaxModules);
            printf("\tMaximum Transformational Memory: %" CPRIu32 "\n", raidInfo->uMaxTransformationMemory);
            printf("\tChange Count: %" CPRIu32 "\n", raidInfo->uChangeCount);
            //Add another is_Empty here for 44 bytes if other things get added here.
        }
    }
    return;
}

int csmi_Get_RAID_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_INFO_BUFFER raidInfoBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(raidInfoBuffer, 0, sizeof(CSMI_SAS_RAID_INFO_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = raidInfoBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_RAID_INFO_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_RAID_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_RAID_INFO;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_RAID_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_RAID_SIGNATURE, strlen(CSMI_RAID_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get RAID Info\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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

static void print_CSMI_RAID_Config(PCSMI_SAS_RAID_CONFIG config, uint32_t configLength)
{
    if (config)
    {
        printf("\n====CSMI RAID Configuration====\n");
        printf("\tRAID Set Index: %" CPRIu32 "\n", config->uRaidSetIndex);
        printf("\tCapacity (MB): %" CPRIu32 "\n", config->uCapacity);
        printf("\tStripe Size (KB): %" CPRIu32 "\n", config->uStripeSize);
        printf("\tRAID Type: ");
        switch (config->bRaidType)
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
        //Use DataType to switch between what was reported back
        if (!is_Empty(&config->bDataType, 20))//this is being checked since failure code and change count were added later
        {
            printf("\tFailure Code: %" CPRIu32 "\n", config->uFailureCode);
            printf("\tChange Count: %" CPRIu32 "\n", config->uChangeCount);
        }
        switch (config->bDataType)
        {
        case CSMI_SAS_RAID_DATA_DRIVES:
            if (config->bDriveCount < 0xF1)
            {
                uint32_t totalDrives = (uint32_t)((configLength - 36) / sizeof(CSMI_SAS_RAID_DRIVES));//36 bytes prior to drive data
                for (uint32_t iter = 0; iter < totalDrives && iter < config->bDriveCount; ++iter)
                {
                    char model[41] = { 0 };
                    char firmware[9] = { 0 };
                    char serialNumber[41] = { 0 };
                    memcpy(model, config->Drives[iter].bModel, 40);
                    memcpy(firmware, config->Drives[iter].bFirmware, 8);
                    memcpy(serialNumber, config->Drives[iter].bSerialNumber, 40);
                    printf("\t----RAID Drive %" PRIu32 "----\n", iter);
                    printf("\t\tModel #: %s\n", model);
                    printf("\t\tFirmware: %s\n", firmware);
                    printf("\t\tSerial #: %s\n", serialNumber);
                    printf("\t\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", config->Drives[iter].bSASAddress[0], config->Drives[iter].bSASAddress[1], config->Drives[iter].bSASAddress[2], config->Drives[iter].bSASAddress[3], config->Drives[iter].bSASAddress[4], config->Drives[iter].bSASAddress[5], config->Drives[iter].bSASAddress[6], config->Drives[iter].bSASAddress[7]);
                    printf("\t\tSAS LUN: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", config->Drives[iter].bSASLun[0], config->Drives[iter].bSASLun[1], config->Drives[iter].bSASLun[2], config->Drives[iter].bSASLun[3], config->Drives[iter].bSASLun[4], config->Drives[iter].bSASLun[5], config->Drives[iter].bSASLun[6], config->Drives[iter].bSASLun[7]);
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
                    case CSMI_SAS_DRIVE_CONFIG_SRT_CACHE://Unique to Intel!
                        printf("SRT Cache\n");
                        break;
                    case CSMI_SAS_DRIVE_CONFIG_SRT_DATA://Unique to Intel!
                        printf("SRT Data\n");
                        break;
                    default:
                        printf("Unknown - %" CPRIu8 "\n", config->Drives[iter].bDriveUsage);
                        break;
                    }
                    //end of original RAID drive data in spec. Check if empty
                    if (!is_Empty(&config->Drives[iter].usBlockSize, 30))//original spec says 22 reserved bytes, however I count 30 more bytes to check...-TJE
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
                        printf("\t\tTotal User Blocks: %" PRIu64 "\n", M_DWordsTo8ByteValue(config->Drives[iter].ulTotalUserBlocks.uHighPart, config->Drives[iter].ulTotalUserBlocks.uLowPart));
                    }
                }
            }
            break;
        case CSMI_SAS_RAID_DATA_DEVICE_ID:
            //TODO: Print this out...device identification VPD page
            printf("Device ID (Debug info not supported at this time)\n");
            break;
        case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
            //TODO: Print this out
            printf("Additional Data (Debug info not supported at this time)\n");
            break;
        default:
            printf("Unknown data type.\n");
            break;
        }
    }
    return;
}

//NOTE: This buffer should be allocated as sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) + (raidInfo.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES)) at minimum. If the device identification VPD page is returned instead, it may be longer
//      RAID set index must be lower than the number of raid sets listed as supported by RAID INFO
//NOTE: Dataype field may not be supported depending on which version of CSMI is supported. Intel RST will not support this.
int csmi_Get_RAID_Config(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_CONFIG_BUFFER raidConfigBuffer, uint32_t raidConfigBufferTotalSize, uint32_t raidSetIndex, uint8_t dataType, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(raidConfigBuffer, 0, raidConfigBufferTotalSize);

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = raidConfigBuffer;
    ioIn.ioctlBufferSize = raidConfigBufferTotalSize;
    ioIn.dataLength = raidConfigBufferTotalSize - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_RAID_CONFIG;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_RAID_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_RAID_SIGNATURE, strlen(CSMI_RAID_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    raidConfigBuffer->Configuration.uRaidSetIndex = raidSetIndex;
    raidConfigBuffer->Configuration.bDataType = dataType;//NOTE: This may only be implemented on SOME CSMI implementations. Not supported by Intel RST as they only support up to .77 changes, but this is a newer field.

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get RAID Config\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(raidConfigBuffer->IoctlHeader.ReturnCode);
        if (VERBOSITY_COMMAND_VERBOSE <= verbosity)
        {
            print_CSMI_RAID_Config(&raidConfigBuffer->Configuration, raidConfigBufferTotalSize - sizeof(IOCTL_HEADER));
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

//TODO: Get RAID Features
//      Set RAID Control
//      Get RAID Element
//      Set RAID Operation

static void print_CSMI_Port_Protocol(uint8_t portProtocol)
{
    switch (portProtocol)
    {
    case CSMI_SAS_PROTOCOL_SATA:
        printf("SATA\n");
        break;
    case CSMI_SAS_PROTOCOL_SMP:
        printf("SMP\n");
        break;
    case CSMI_SAS_PROTOCOL_STP:
        printf("STP\n");
        break;
    case CSMI_SAS_PROTOCOL_SSP:
        printf("SSP\n");
        break;
    default:
        printf("Unknown\n");
        break;
    }
    return;
}

static void print_CSMI_SAS_Identify(PCSMI_SAS_IDENTIFY identify)
{
    if (identify)
    {
        //everything printed with 3 tabs to fit with print function below since this is only used by Phy info data
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
        printf("\t\t\tRestricted 2: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", identify->bRestricted2[0], identify->bRestricted2[1], identify->bRestricted2[2], identify->bRestricted2[3], identify->bRestricted2[4], identify->bRestricted2[5], identify->bRestricted2[6], identify->bRestricted2[7]);
        printf("\t\t\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", identify->bSASAddress[0], identify->bSASAddress[1], identify->bSASAddress[2], identify->bSASAddress[3], identify->bSASAddress[4], identify->bSASAddress[5], identify->bSASAddress[6], identify->bSASAddress[7]);
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
    return;
}

static void print_CSMI_Link_Rate(uint8_t linkRate)
{
    switch (linkRate)
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
    case CSMI_SAS_LINK_VIRTUAL:
        printf("Virtual\n");
        break;
    default:
        printf("Unknown - %02" CPRIX8 "h\n", linkRate);
        break;
    }
}

static void print_CSMI_Phy_Info(PCSMI_SAS_PHY_INFO phyInfo)
{
    if (phyInfo)
    {
        printf("\n====CSMI Phy Info====\n");
        printf("\tNumber Of Phys: %" CPRIu8 "\n", phyInfo->bNumberOfPhys);
        for (uint8_t phyIter = 0; phyIter < phyInfo->bNumberOfPhys && phyIter < 32; ++phyIter)
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
    return;
}
//Caller allocated full buffer, then we fill in the rest and send it. Data length not needed since this one is a fixed size
int csmi_Get_Phy_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_PHY_INFO_BUFFER phyInfoBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(phyInfoBuffer, 0, sizeof(CSMI_SAS_PHY_INFO_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = phyInfoBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_PHY_INFO_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_PHY_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_PHY_INFO;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Phy Info\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
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

    return ret;
}

int csmi_Set_Phy_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_SET_PHY_INFO_BUFFER phyInfoBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(phyInfoBuffer, 0, sizeof(IOCTL_HEADER));//only clear out the header...caller should setup the changes they want to make

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = phyInfoBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_SET_PHY_INFO;
    ioIn.ioctlDirection = CSMI_SAS_DATA_WRITE;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Set Phy Info\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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

int csmi_Get_Link_Errors(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_LINK_ERRORS_BUFFER linkErrorsBuffer, uint8_t phyIdentifier, bool resetCounts, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(linkErrorsBuffer, 0, sizeof(CSMI_SAS_LINK_ERRORS_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = linkErrorsBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_LINK_ERRORS_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_LINK_ERRORS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_LINK_ERRORS;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    linkErrorsBuffer->Information.bPhyIdentifier = phyIdentifier;
    linkErrorsBuffer->Information.bResetCounts = CSMI_SAS_LINK_ERROR_DONT_RESET_COUNTS;
    if (resetCounts)
    {
        linkErrorsBuffer->Information.bResetCounts = CSMI_SAS_LINK_ERROR_RESET_COUNTS;
    }

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Link Errors\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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

//TODO: SMP Passthrough function

//SSP Passthrough function (just for internal use, other functions should parse tdevice and scsiIoCtx to issue this
typedef struct _csmiSSPIn
{
    uint8_t phyIdentifier;//can set CSMI_SAS_USE_PORT_IDENTIFIER
    uint8_t portIdentifier;//can set CSMI_SAS_IGNORE_PORT
    uint8_t connectionRate;//strongly recommend leaving as negotiated
    uint8_t destinationSASAddress[8];
    uint8_t lun[8];
    uint8_t flags;//read, write, unspecified, head of queue, simple, ordered, aca, etc. Must set read/write/unspecified at minimum. Simple attribute is recommended
    uint8_t cdbLength;
    uint8_t *cdb;
    uint8_t *ptrData;//pointer to buffer to use as source for writes. This will be used for reads as well.
    uint32_t dataLength;//length of data to read or write
    uint32_t timeoutSeconds;
}csmiSSPIn, *ptrCsmiSSPIn;

typedef struct _csmiSSPOut
{
    seatimer_t *sspTimer;//may be null, but incredibly useful for knowing how long a command took
    uint8_t *senseDataPtr;//Should not be null. In case of a drive error, this gives you what happened
    uint32_t senseDataLength;//length of memory pointed to by senseDataPtr
    uint8_t connectionStatus;
}csmiSSPOut, *ptrCsmiSSPOut;

int csmi_SSP_Passthrough(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, ptrCsmiSSPIn sspInputs, ptrCsmiSSPOut sspOutputs, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    PCSMI_SAS_SSP_PASSTHRU_BUFFER sspPassthrough = NULL;
    uint32_t sspPassthroughBufferLength = 0;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));

    if (!sspInputs || !sspOutputs || !sspInputs->cdb)//NOTE: other validation is done below.
    {
        return BAD_PARAMETER;
    }
    sspPassthroughBufferLength = sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER) + sspInputs->dataLength;
    sspPassthrough = (PCSMI_SAS_SSP_PASSTHRU_BUFFER)calloc_aligned(sizeof(uint8_t), sspPassthroughBufferLength, sizeof(void*));
    if (!sspPassthrough)
    {
        return MEMORY_FAILURE;
    }

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = sspPassthrough;
    ioIn.ioctlBufferSize = sspPassthroughBufferLength;
    ioIn.dataLength = sspPassthroughBufferLength - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_SSP_PASSTHRU;
    //ioIn.ioctlDirection = CSMI_SAS_DATA_READ;//This is set below, however it may only need to be set one way....will only knwo when testing on linux since this is used there.
    ioIn.timeoutInSeconds = sspInputs->timeoutSeconds;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;
    ioOut.ioctlTimer = sspOutputs->sspTimer;

    //setup ssp specific parameters
    if (sspInputs->cdbLength > 16)
    {
        if (sspInputs->cdbLength > 40)
        {
            safe_Free_aligned(sspPassthrough);
            return OS_COMMAND_NOT_AVAILABLE;
        }
        //copy to cdb, then additional CDB
        memcpy(sspPassthrough->Parameters.bCDB, sspInputs->cdb, 16);
        memcpy(sspPassthrough->Parameters.bAdditionalCDB, sspInputs->cdb + 16, sspInputs->cdbLength - 16);
        sspPassthrough->Parameters.bCDBLength = 16;
        sspPassthrough->Parameters.bAdditionalCDBLength = (sspInputs->cdbLength - 16) / sizeof(uint32_t);//this is in dwords according to the spec
    }
    else
    {
        memcpy(sspPassthrough->Parameters.bCDB, sspInputs->cdb, sspInputs->cdbLength);
        sspPassthrough->Parameters.bCDBLength = sspInputs->cdbLength;
    }

    sspPassthrough->Parameters.bConnectionRate = sspInputs->connectionRate;
    memcpy(sspPassthrough->Parameters.bDestinationSASAddress, sspInputs->destinationSASAddress, 8);
    memcpy(sspPassthrough->Parameters.bLun, sspInputs->lun, 8);
    sspPassthrough->Parameters.bPhyIdentifier = sspInputs->phyIdentifier;
    sspPassthrough->Parameters.bPortIdentifier = sspInputs->portIdentifier;
    sspPassthrough->Parameters.uDataLength = sspInputs->dataLength;
    sspPassthrough->Parameters.uFlags = sspInputs->flags;

    if (sspPassthrough->Parameters.uFlags & CSMI_SAS_SSP_WRITE)
    {
        ioIn.ioctlDirection = CSMI_SAS_DATA_WRITE;
        if (sspInputs->ptrData)
        {
            memcpy(sspPassthrough->bDataBuffer, sspInputs->ptrData, sspInputs->dataLength);
        }
        else
        {
            safe_Free_aligned(sspPassthrough);
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
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(sspPassthrough->IoctlHeader.ReturnCode);
        //if (sspPassthrough->IoctlHeader.ReturnCode == CSMI_SAS_STATUS_SUCCESS)

        if (sspPassthrough->Parameters.uFlags & CSMI_SAS_SSP_READ)
        {
            //clear read data ptr first
            memset(sspInputs->ptrData, 0, sspInputs->dataLength);
            if (sspPassthrough->Status.uDataBytes)
            {
                //copy what we got
                memcpy(sspInputs->ptrData, sspPassthrough->bDataBuffer, M_Min(sspInputs->dataLength, sspPassthrough->Status.uDataBytes));
            }
        }
        //TODO: Response data versus sense data. Sense data is obvious, but what does it mean by response data???
        if(/*sspPassthrough->Status.bDataPresent == CSMI_SAS_SSP_RESPONSE_DATA_PRESENT ||*/ sspPassthrough->Status.bDataPresent == CSMI_SAS_SSP_SENSE_DATA_PRESENT)
        {
            //copy back sense data
            if (sspOutputs->senseDataPtr)
            {
                //clear sense first
                memset(sspOutputs->senseDataPtr, 0, sspOutputs->senseDataLength);
                //now copy what we can
                memcpy(sspOutputs->senseDataPtr, sspPassthrough->Status.bResponse, M_Min(M_BytesTo2ByteValue(sspPassthrough->Status.bResponseLength[0], sspPassthrough->Status.bResponseLength[1]), sspOutputs->senseDataLength));
            }
        }
        else //no data
        {
            if (sspInputs->ptrData && sspPassthrough->Parameters.uFlags & CSMI_SAS_SSP_READ)
            {
                memset(sspInputs->ptrData, 0, sspInputs->dataLength);
            }
            if (sspOutputs->senseDataPtr)
            {
                memset(sspOutputs->senseDataPtr, 0, sspOutputs->senseDataLength);
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

    safe_Free_aligned(sspPassthrough);

    return ret;
}

//TODO: STP Passthrough function (retry and switch to SSP if error is SCSI EMULATION...may need to try SAT and legacy E0h CDB methods)
typedef struct _csmiSTPIn
{
    uint8_t phyIdentifier;//can set CSMI_SAS_USE_PORT_IDENTIFIER
    uint8_t portIdentifier;//can set CSMI_SAS_IGNORE_PORT
    uint8_t connectionRate;//strongly recommend leaving as negotiated
    uint8_t destinationSASAddress[8];
    uint8_t flags;//read, write, unspecified, must also specify pio, dma, etc for the protocol of the command being issued.
    void *commandFIS;//pointer to a 20 byte array for a H2D fis.
    uint8_t *ptrData;//pointer to buffer to use as source for writes. This will be used for reads as well.
    uint32_t dataLength;//length of data to read or write
    uint32_t timeoutSeconds;
}csmiSTPIn, *ptrCsmiSTPIn;

typedef struct _csmiSTPOut
{
    seatimer_t *stpTimer;//may be null, but incredibly useful for knowing how long a command took
    uint8_t *statusFIS;//Should not be null. In case of a drive error, this gives you what happened. This should point to a 20 byte array to store the result. May be D2H or PIO Setup depend on the command issued.
    uint32_t *scrPtr;//Optional. This is the current status and control registers value. See SATA spec for more details on the registers that these map to. These are not writable through this interface.
    uint8_t connectionStatus;
    bool retryAsSSPPassthrough;//This may be set, but will only be set, if the driver does not support STP passthrough, but DOES support taking a SCSI translatable CDB. This cannot tell whether to use SAT or legacy CSMI passthrough though...that's a trial and error thing unless we figure out which drivers and versions require that. -TJE
}csmiSTPOut, *ptrCsmiSTPOut;

int csmi_STP_Passthrough(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, ptrCsmiSTPIn stpInputs, ptrCsmiSTPOut stpOutputs, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    PCSMI_SAS_STP_PASSTHRU_BUFFER stpPassthrough = NULL;
    uint32_t stpPassthroughBufferLength = 0;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));

    if (!stpInputs || !stpOutputs || !stpInputs->commandFIS || !stpOutputs->statusFIS)//NOTE: other validation is done below.
    {
        return BAD_PARAMETER;
    }
    stpPassthroughBufferLength = sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER) + stpInputs->dataLength;
    stpPassthrough = (PCSMI_SAS_STP_PASSTHRU_BUFFER)calloc_aligned(sizeof(uint8_t), stpPassthroughBufferLength, sizeof(void*));
    if (!stpPassthrough)
    {
        return MEMORY_FAILURE;
    }

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = stpPassthrough;
    ioIn.ioctlBufferSize = stpPassthroughBufferLength;
    ioIn.dataLength = stpPassthroughBufferLength - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_STP_PASSTHRU;
    //ioIn.ioctlDirection = CSMI_SAS_DATA_READ;//This is set below, however it may only need to be set one way....will only knwo when testing on linux since this is used there.
    ioIn.timeoutInSeconds = stpInputs->timeoutSeconds;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;
    ioOut.ioctlTimer = stpOutputs->stpTimer;

    //setup stp specific parameters
    //command FIS

    stpPassthrough->Parameters.bConnectionRate = stpInputs->connectionRate;
    memcpy(stpPassthrough->Parameters.bDestinationSASAddress, stpInputs->destinationSASAddress, 8);
    stpPassthrough->Parameters.bPhyIdentifier = stpInputs->phyIdentifier;
    stpPassthrough->Parameters.bPortIdentifier = stpInputs->portIdentifier;
    stpPassthrough->Parameters.uDataLength = stpInputs->dataLength;
    stpPassthrough->Parameters.uFlags = stpInputs->flags;
    memcpy(stpPassthrough->Parameters.bCommandFIS, stpInputs->commandFIS, H2D_FIS_LENGTH);

    if (stpPassthrough->Parameters.uFlags & CSMI_SAS_STP_WRITE)
    {
        ioIn.ioctlDirection = CSMI_SAS_DATA_WRITE;
        if (stpInputs->ptrData)
        {
            memcpy(stpPassthrough->bDataBuffer, stpInputs->ptrData, stpInputs->dataLength);
        }
        else
        {
            safe_Free_aligned(stpPassthrough);
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
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
    {
        ret = csmi_Return_To_OpenSea_Result(stpPassthrough->IoctlHeader.ReturnCode);
        if (stpPassthrough->IoctlHeader.ReturnCode == CSMI_SAS_SCSI_EMULATION)
        {
            //this is a special case to say "retry with SSP and a CDB".
            stpOutputs->retryAsSSPPassthrough = true;
        }
        //if (sspPassthrough->IoctlHeader.ReturnCode == CSMI_SAS_STATUS_SUCCESS)

        if (stpPassthrough->Parameters.uFlags & CSMI_SAS_STP_READ)
        {
            //clear read data ptr first
            memset(stpInputs->ptrData, 0, stpInputs->dataLength);
            if (stpPassthrough->Status.uDataBytes)
            {
                //copy what we got
                memcpy(stpInputs->ptrData, stpPassthrough->bDataBuffer, M_Min(stpInputs->dataLength, stpPassthrough->Status.uDataBytes));
            }
        }
        
        //copy back result
        memcpy(stpOutputs->statusFIS, stpPassthrough->Status.bStatusFIS, D2H_FIS_LENGTH);

        if (stpOutputs->scrPtr)
        {
            //if the caller allocted memory for the SCR data, then copy it back for them
            memcpy(stpOutputs->scrPtr, stpPassthrough->Status.uSCR, 16 * sizeof(uint32_t));
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

    safe_Free_aligned(stpPassthrough);

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
    return;
}

//TODO: consider using a pointer to a FIS to fill in on completion instead...
int csmi_Get_SATA_Signature(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_SATA_SIGNATURE_BUFFER sataSignatureBuffer, uint8_t phyIdentifier, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(sataSignatureBuffer, 0, sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = sataSignatureBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_SATA_SIGNATURE;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    sataSignatureBuffer->Signature.bPhyIdentifier = phyIdentifier;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get SATA Signature\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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
    if(scsiAddress)
    {
        printf("\n====CSMI Get SCSI Address====\n");
        printf("\tProvided SAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", scsiAddress->bSASAddress[0], scsiAddress->bSASAddress[1], scsiAddress->bSASAddress[2], scsiAddress->bSASAddress[3], scsiAddress->bSASAddress[4], scsiAddress->bSASAddress[5], scsiAddress->bSASAddress[6], scsiAddress->bSASAddress[7]);
        printf("\tProvided SAS Lun: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", scsiAddress->bSASLun[0], scsiAddress->bSASLun[1], scsiAddress->bSASLun[2], scsiAddress->bSASLun[3], scsiAddress->bSASLun[4], scsiAddress->bSASLun[5], scsiAddress->bSASLun[6], scsiAddress->bSASLun[7]);
        printf("\tHost Index: %" CPRIu8 "\n", scsiAddress->bHostIndex);
        printf("\tPath ID: %" CPRIu8 "\n", scsiAddress->bPathId);
        printf("\tTarget ID: %" CPRIu8 "\n", scsiAddress->bTargetId);
        printf("\tLUN: %" CPRIu8 "\n", scsiAddress->bLun);
    }
    return;
}

//TODO: input/output structures for this instead???
int csmi_Get_SCSI_Address(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddressBuffer, uint8_t sasAddress[8], uint8_t lun[8], eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(scsiAddressBuffer, 0, sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = scsiAddressBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_SCSI_ADDRESS;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    memcpy(scsiAddressBuffer->bSASAddress, sasAddress, 8);
    memcpy(scsiAddressBuffer->bSASLun, lun, 8);

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get SCSI Address\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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
        printf("\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", address->bSASAddress[0], address->bSASAddress[1], address->bSASAddress[2], address->bSASAddress[3], address->bSASAddress[4], address->bSASAddress[5], address->bSASAddress[6], address->bSASAddress[7]);
        printf("\tSAS Lun: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", address->bSASLun[0], address->bSASLun[1], address->bSASLun[2], address->bSASLun[3], address->bSASLun[4], address->bSASLun[5], address->bSASLun[6], address->bSASLun[7]);
    }
    return;
}

//TODO: input/output structures for this instead???
int csmi_Get_Device_Address(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_GET_DEVICE_ADDRESS_BUFFER deviceAddressBuffer, uint8_t hostIndex, uint8_t path, uint8_t target, uint8_t lun, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(deviceAddressBuffer, 0, sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = deviceAddressBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_DEVICE_ADDRESS;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    deviceAddressBuffer->bHostIndex = hostIndex;
    deviceAddressBuffer->bPathId = path;
    deviceAddressBuffer->bTargetId = target;
    deviceAddressBuffer->bLun = lun;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Device Address\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
    if (ioOut.sysIoctlReturn == CSMI_SYSTEM_IOCTL_SUCCESS)
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

//TODO: SAS Task management function (can be used for a few things, but hard reset is most interesting
//CC_CSMI_SAS_TASK_MANAGEMENT

static void print_CSMI_Connector_Info(PCSMI_SAS_CONNECTOR_INFO_BUFFER connectorInfo)
{
    if (connectorInfo)
    {
        //need to loop through, but only print out non-zero structures since this data doesn't give us a count.
        //It is intended to be used alongside the phy info data which does provide a count, but we don't want to be passing that count in for this right now.
        printf("\n====CSMI Connector Info====\n");
        for (uint8_t iter = 0; iter < 32; ++iter)
        {
            if (is_Empty(&connectorInfo->Reference[iter], 36))
            {
                break;
            }
            printf("\t----Connector %" CPRIu8 "----\n", iter);
            printf("\t\tConnector: %s\n", connectorInfo->Reference[iter].bConnector);
            printf("\t\tPinout: \n");
            if(connectorInfo->Reference[iter].uPinout == 0)
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
                    (connectorInfo->Reference[iter].uPinout & CSMI_SAS_CON_RESERVED_C)
                    )
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
    return;
}

int csmi_Get_Connector_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CONNECTOR_INFO_BUFFER connectorInfoBuffer, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
    csmiIOin ioIn;
    csmiIOout ioOut;
    memset(&ioIn, 0, sizeof(csmiIOin));
    memset(&ioOut, 0, sizeof(csmiIOout));
    memset(connectorInfoBuffer, 0, sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER));

    //setup inputs
    ioIn.controllerNumber = controllerNumber;
    ioIn.deviceHandle = deviceHandle;
    ioIn.ioctlBuffer = connectorInfoBuffer;
    ioIn.ioctlBufferSize = sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER);
    ioIn.dataLength = sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    ioIn.ioctlCode = CC_CSMI_SAS_GET_CONNECTOR_INFO;
    ioIn.ioctlDirection = CSMI_SAS_DATA_READ;
    ioIn.timeoutInSeconds = CSMI_SAS_TIMEOUT;
    memcpy(ioIn.ioctlSignature, CSMI_SAS_SIGNATURE, strlen(CSMI_SAS_SIGNATURE));
    ioIn.csmiVerbosity = verbosity;

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("Sending CSMI Get Connector Info\n");
    }
    //issue command
    ret = issue_CSMI_IO(&ioIn, &ioOut);
    //validate result
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

static int csmi_Get_Driver_And_Controller_Data(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_DRIVER_INFO_BUFFER driverInfo, PCSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig, eVerbosityLevels verbosity)
{
    int ret = SUCCESS;
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
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

//Function to check for CSMI IO support on non-RAID devices (from Windows mostly since this can get around Win passthrough restrictions in some cases)
//TODO: need a way to make sure we are only checking this on drives not configured as a raid member.
bool handle_Supports_CSMI_IO(CSMI_HANDLE deviceHandle, eVerbosityLevels verbosity)
{
    bool csmiSupported = false;
    if (deviceHandle != CSMI_INVALID_HANDLE)
    {
        //Send the following 2 IOs to check if CSMI passthrough is supported on a device that is NOT a RAID device, meaning it is not configured as a member of a RAID.
        //int csmi_Get_Driver_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer, eVerbosityLevels verbosity)
        //int csmi_Get_Controller_Configuration(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer, eVerbosityLevels verbosity)
        CSMI_SAS_DRIVER_INFO_BUFFER driverInfo;
        CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
        memset(&driverInfo, 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
        memset(&controllerConfig, 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
        if (SUCCESS == csmi_Get_Driver_And_Controller_Data(deviceHandle, 0, &driverInfo, &controllerConfig, verbosity))
        {
            csmiSupported = true;
        }
    }
    return csmiSupported;
}

#if defined (_WIN32)
bool device_Supports_CSMI_With_RST(tDevice *device)
{
    bool csmiWithRSTSupported = false;
    if (handle_Supports_CSMI_IO(device->os_info.scsiSRBHandle, device->deviceVerbosity))
    {
        //check for FWDL IOCTL support. If this works, then the Intel Additions are supported. (TODO: try NVMe passthrough???)
        //TODO: Based on driver name, only check this for known intel drivers???
#if defined (ENABLE_INTEL_RST)
        if (supports_Intel_Firmware_Download(device))
        {
            csmiWithRSTSupported = true;
        }
#endif
    }
    return csmiWithRSTSupported;
}
#endif
//This is really only here for Windows, but could be used under Linux if you wanted to use CSMI instead of SGIO, but that really is unnecessary
//controller number is to target the CSMI IOCTL inputs on non-windows. hostController is a SCSI address number, which may or may not be different...If these end up the same on Linux, this should be update to remove the duplicate parameters. If not, delete part of this comment.
//NOTE: this does not handle Intel NVMe devices in JBOD mode right now. These devices will be handled separately from this function which focuses on SATA/SAS
int jbod_Setup_CSMI_Info(M_ATTR_UNUSED CSMI_HANDLE deviceHandle, tDevice *device, uint8_t controllerNumber, uint8_t hostController, uint8_t pathidBus, uint8_t targetID, uint8_t lun)
{
    int ret = SUCCESS;
    device->os_info.csmiDeviceData = (ptrCsmiDeviceInfo)calloc(1, sizeof(csmiDeviceInfo));
    if (device->os_info.csmiDeviceData)
    {
#if defined (_WIN32)
        device->os_info.csmiDeviceData->csmiDevHandle = device->os_info.scsiSRBHandle;
#else
        device->os_info.csmiDeviceData->csmiDevHandle = device->os_info.fd;
#endif
        device->os_info.csmiDeviceData->controllerNumber = controllerNumber;
        device->os_info.csmiDeviceData->csmiDeviceInfoValid = true;
        //Read controller info, driver info, get phy info for this device too...in non-RAID mode, Windows scsi address should match the csmi scsi address
        CSMI_SAS_DRIVER_INFO_BUFFER driverInfo;
        CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
        if (SUCCESS == csmi_Get_Driver_And_Controller_Data(device->os_info.csmiDeviceData->csmiDevHandle, 0, &driverInfo, &controllerConfig, device->deviceVerbosity))
        {
            bool gotSASAddress = false;
            device->os_info.csmiDeviceData->csmiMajorVersion = driverInfo.Information.usMajorRevision;
            device->os_info.csmiDeviceData->csmiMinorVersion = driverInfo.Information.usMinorRevision;
            device->os_info.csmiDeviceData->controllerFlags = controllerConfig.Configuration.uControllerFlags;
            device->os_info.csmiDeviceData->lun = lun;
            //set CSMI scsi address based on what was passed in since it may be needed later
            device->os_info.csmiDeviceData->scsiAddress.hostIndex = hostController;
            device->os_info.csmiDeviceData->scsiAddress.pathId = pathidBus;
            device->os_info.csmiDeviceData->scsiAddress.targetId = targetID;
            device->os_info.csmiDeviceData->scsiAddress.lun = lun;
            device->os_info.csmiDeviceData->scsiAddressValid = true;
                
            //get SAS Address
            CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER addressBuffer;
            if (SUCCESS == csmi_Get_Device_Address(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &addressBuffer, hostController, pathidBus, targetID, lun, device->deviceVerbosity))
            {
                memcpy(device->os_info.csmiDeviceData->sasAddress, addressBuffer.bSASAddress, 8);
                memcpy(device->os_info.csmiDeviceData->sasLUN, addressBuffer.bSASLun, 8);
                gotSASAddress = true;
            }
            else
            {
                //Need to figure out the device another way to get the SAS address IF this is a SAS drive. If this is SATA, this is less important overall unless it's on a SAS HBA, but a driver should be able to handle this call already.
                //The only other place to find a SAS Address is in the RAID Config, but the drive may or may not be listed there...try it anyways...if we still don't find it, we'll only get the sasAddress from the phy info.
                //RAID info/config will only be available if it's a SAS or SATA RAID capable controller
                if (controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID || controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID || controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY)
                {
                    CSMI_SAS_RAID_INFO_BUFFER raidInfo;
                    if (SUCCESS == csmi_Get_RAID_Info(device->os_info.csmiDeviceData->csmiDevHandle, 0, &raidInfo, device->deviceVerbosity))
                    {
                        for (uint32_t raidSet = 0; !gotSASAddress && raidSet < raidInfo.Information.uNumRaidSets; ++raidSet)
                        {
                            //with the RAID info, now we can allocate and read the RAID config
                            uint32_t raidConfigLength = sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) - 1 + (raidInfo.Information.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES));
                            PCSMI_SAS_RAID_CONFIG_BUFFER raidConfig = (PCSMI_SAS_RAID_CONFIG_BUFFER)calloc(raidConfigLength, sizeof(uint8_t));
                            if (raidConfig)
                            {
                                if (SUCCESS == csmi_Get_RAID_Config(device->os_info.csmiDeviceData->csmiDevHandle, 0, raidConfig, raidConfigLength, raidSet, CSMI_SAS_RAID_DATA_DRIVES, device->deviceVerbosity))
                                {
                                    if (raidConfig->Configuration.bDriveCount < CSMI_SAS_RAID_DRIVE_COUNT_TOO_BIG)
                                    {
                                        for (uint32_t driveIter = 0; !gotSASAddress && driveIter < raidInfo.Information.uMaxDrivesPerSet && driveIter < raidConfig->Configuration.bDriveCount; ++driveIter)
                                        {
                                            switch (raidConfig->Configuration.bDataType)
                                            {
                                            case CSMI_SAS_RAID_DATA_DRIVES:
                                                if (strstr((const char*)raidConfig->Configuration.Drives[driveIter].bModel, device->drive_info.product_identification) && strstr((const char*)raidConfig->Configuration.Drives[driveIter].bSerialNumber, device->drive_info.serialNumber))
                                                {
                                                    //Found the match!!!
                                                    gotSASAddress = true;
                                                    memcpy(device->os_info.csmiDeviceData->sasAddress, raidConfig->Configuration.Drives[driveIter].bSASAddress, 8);
                                                    memcpy(device->os_info.csmiDeviceData->sasLUN, raidConfig->Configuration.Drives[driveIter].bSASLun, 8);
                                                }
                                                break;
                                            default:
                                                break;
                                            }
                                        }
                                    }
                                }
                                safe_Free(raidConfig);
                            }
                        }
                    }
                }
            }

            //Attempt to read phy info to get phy identifier and port identifier data...this may not work on RST NVMe if this code is hit...that's OK since we are unlikely to see that.
            CSMI_SAS_PHY_INFO_BUFFER phyInfo;
            bool foundPhyInfo = false;
            if (SUCCESS == csmi_Get_Phy_Info(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &phyInfo, device->deviceVerbosity))
            {
                //TODO: Is there a better way to match against the port identifier with the address information provided? match to attached port or phy identifier???
                for (uint8_t phyIter = 0; !foundPhyInfo && phyIter < phyInfo.Information.bNumberOfPhys && phyIter < 32; ++phyIter)
                {
                    if (phyInfo.Information.Phy[phyIter].Attached.bDeviceType != CSMI_SAS_NO_DEVICE_ATTACHED)
                    {
                        if (gotSASAddress && memcmp(phyInfo.Information.Phy[phyIter].Attached.bSASAddress, device->os_info.csmiDeviceData->sasAddress, 8) == 0)
                        {
                            //Found it. We can save the portID and phyID to use to issue commands :)
                            device->os_info.csmiDeviceData->portIdentifier = phyInfo.Information.Phy[phyIter].bPortIdentifier;
                            device->os_info.csmiDeviceData->phyIdentifier = phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier;
                            device->os_info.csmiDeviceData->portProtocol = phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol;
                            foundPhyInfo = true;
                        }
                        else if (!gotSASAddress)
                        {
                            ScsiIoCtx csmiPTCmd;
                            memset(&csmiPTCmd, 0, sizeof(ScsiIoCtx));
                            csmiPTCmd.device = device;
                            csmiPTCmd.timeout = 15;
                            csmiPTCmd.direction = XFER_DATA_IN;
                            csmiPTCmd.psense = device->drive_info.lastCommandSenseData;
                            csmiPTCmd.senseDataSize = SPC3_SENSE_LEN;
                            //Don't have a SAS Address to match to, so we need to send an identify or inquiry to the device to see if it is the same MN, then check the SN.
                            //NOTE: This will not work if we don't already know the sasLUN for SAS drives. SATA will be ok though.
                            device->os_info.csmiDeviceData->portIdentifier = phyInfo.Information.Phy[phyIter].bPortIdentifier;
                            device->os_info.csmiDeviceData->phyIdentifier = phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier;
                            device->os_info.csmiDeviceData->portProtocol = phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol;
                            memcpy(&device->os_info.csmiDeviceData->sasAddress[0], phyInfo.Information.Phy[phyIter].Attached.bSASAddress, 8);
                            //Attempt passthrough command and compare identifying data.
                            //for this to work, SCSIIoCTX structure must be manually defined for what we want to do right now and call the CSMI IO directly...not great, but don't want to have other force flags elsewhere at the moment- TJE
                            if (phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol & CSMI_SAS_PROTOCOL_SATA || phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol & CSMI_SAS_PROTOCOL_STP)
                            {
                                //ATA identify
                                uint8_t identifyData[512] = { 0 };
                                ataPassthroughCommand identify;
                                memset(&identify, 0, sizeof(ataPassthroughCommand));
                                identify.ataCommandLengthLocation = ATA_PT_LEN_SECTOR_COUNT;
                                identify.ataTransferBlocks = ATA_PT_512B_BLOCKS;
                                identify.commadProtocol = ATA_PROTOCOL_PIO;
                                identify.commandDirection = XFER_DATA_IN;
                                identify.commandType = ATA_CMD_TYPE_TASKFILE;
                                identify.timeout = 15;
                                csmiPTCmd.pdata = identify.ptrData = identifyData;
                                csmiPTCmd.dataLength = identify.dataSize = 512;
                                csmiPTCmd.pAtaCmdOpts = &identify;
                                identify.tfr.CommandStatus = ATA_IDENTIFY;
                                identify.tfr.SectorCount = 1;
                                identify.tfr.DeviceHead = DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
                                if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                {
                                    //compare MN and SN...if match, then we have found the drive!
                                    char ataMN[41] = { 0 };
                                    char ataSN[41] = { 0 };
                                    //char ataFW[9] = { 0 };
                                    //copy strings
                                    memcpy(ataSN, &identifyData[20], 40);
                                    //memcpy(ataFW, &identifyData[46], 8);
                                    memcpy(ataMN, &identifyData[54], 40);
                                    //byte-swap due to ATA string silliness.
                                    byte_Swap_String(ataSN);
                                    byte_Swap_String(ataMN);
                                    //byte_Swap_String(ataFW);
                                    //remove whitespace
                                    remove_Leading_And_Trailing_Whitespace(ataSN);
                                    remove_Leading_And_Trailing_Whitespace(ataMN);
                                    //remove_Leading_And_Trailing_Whitespace(ataFW);
                                    //check for a match
                                    if (strcmp(ataMN, device->drive_info.product_identification) == 0 && strcmp(ataSN, device->drive_info.serialNumber) == 0)
                                    {
                                        //found a match!
                                        foundPhyInfo = true;
                                    }
                                }
                            }
                            else if (phyInfo.Information.Phy[phyIter].Attached.bTargetPortProtocol & CSMI_SAS_PROTOCOL_SSP)
                            {
                                //SCSI Inquiry and read unit serial number VPD page
                                uint8_t inqData[96] = { 0 };
                                uint8_t cdb[CDB_LEN_6] = { 0 };
                                cdb[OPERATION_CODE] = INQUIRY_CMD;
                                /*if (evpd)
                                {
                                    cdb[1] |= BIT0;
                                }*/
                                cdb[2] = 0;// pageCode;
                                cdb[3] = M_Byte1(96);
                                cdb[4] = M_Byte0(96);
                                cdb[5] = 0;//control

                                csmiPTCmd.cdbLength = CDB_LEN_6;
                                memcpy(csmiPTCmd.cdb, cdb, 6);
                                csmiPTCmd.dataLength = 96;
                                csmiPTCmd.pdata = inqData;
                                
                                if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                {
                                    //TODO: If this is a multi-LUN device, this won't currently work and it may not be possible to make this work if we got to this case in the first place. HOPEFULLY the other CSMI translation IOCTLs just work and this is unnecessary. - TJE
                                    //If MN matches, send inquiry to unit SN vpd page to confirm we have a matching SN
                                    char inqVendor[9] = { 0 };
                                    char inqProductID[17] = { 0 };
                                    //char inqProductRev[5] = { 0 };
                                    //copy the strings
                                    memcpy(inqVendor, &inqData[8], 8);
                                    memcpy(inqProductID, &inqData[16], 16);
                                    //memcpy(inqProductRev, &inqData[32], 4);
                                    //remove whitespace
                                    remove_Leading_And_Trailing_Whitespace(inqVendor);
                                    remove_Leading_And_Trailing_Whitespace(inqProductID);
                                    //remove_Leading_And_Trailing_Whitespace(inqProductRev);
                                    //compare to tDevice
                                    if (strcmp(inqVendor, device->drive_info.T10_vendor_ident) == 0 && strcmp(inqProductID, device->drive_info.product_identification) == 0)
                                    {
                                        //now read the unit SN VPD page since this matches so far that way we can compare the serial number. Not checking SCSI 2 since every SAS drive *SHOULD* support this.
                                        memset(inqData, 0, 96);
                                        //change CDB to read unit SN page
                                        cdb[1] |= BIT0;
                                        cdb[2] = UNIT_SERIAL_NUMBER;
                                        if (SUCCESS == send_CSMI_IO(&csmiPTCmd))
                                        {
                                            //check the SN
                                            uint16_t serialNumberLength = M_Min(M_BytesTo2ByteValue(inqData[2], inqData[3]), 96) + 1;
                                            char *serialNumber = (char*)calloc(serialNumberLength, sizeof(char));
                                            if (serialNumber)
                                            {
                                                memcpy(serialNumber, &inqData[4], serialNumberLength - 1);//minus 1 to leave null terminator in tact at the end
                                                if (strcmp(serialNumber, device->drive_info.serialNumber) == 0)
                                                {
                                                    //found a match!
                                                    foundPhyInfo = true;
                                                    //TODO: To help prevent multiport or multi-lun issues, we should REALLY check the device identification VPD page, but that can be a future enhancement
                                                }
                                                safe_Free(serialNumber);
                                            }
                                        }
                                        //else...catastrophic failure? Not sure what to do here since this should be really rare to begin with.
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!foundPhyInfo)
            {
                //We don't have enough information to use CSMI passthrough on this device. Free memory and return NOT_SUPPORTED
                safe_Free(device->os_info.csmiDeviceData);
                ret = NOT_SUPPORTED;
            }

#if defined (_WIN32) && defined (ENABLE_INTEL_RST)
            //Check if Intel Driver and if FWDL IOs are supported or not. version 14.8+
            if (strncmp((const char*)driverInfo.Information.szName, "iaStor", 6) == 0)
            {
                //Intel driver, check for Additional IOCTLs by trying to read FWDL info
                if (supports_Intel_Firmware_Download(device))
                {
                    //No need to do anything here right now since the function above will fill in parameters as necessary.
                }
            }
#endif
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
int close_CSMI_RAID_Device(tDevice *device)
{
    if (device)
    {
        CloseHandle(device->os_info.fd);
        device->os_info.last_error = GetLastError();
        safe_Free(device->os_info.csmiDeviceData);
        device->os_info.last_error = 0;
#if defined (_WIN32)
        device->os_info.fd = INVALID_HANDLE_VALUE;
#else
        device->os_info.fd = -1;
#endif
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

//TODO: Accept SASAddress and SASLun inputs
int get_CSMI_RAID_Device(const char *filename, tDevice *device)
{
    int ret = FAILURE;
    uint32_t controllerNum = 0, portID = 0, phyID = 0, lun = 0;
    uint32_t *intelPathID = &portID, *intelTargetID = &phyID, *intelLun = &lun;
    bool intelNVMe = false;
    //Need to open this handle and setup some information then fill in the device information.
    if (!(validate_Device_Struct(device->sanity)))
    {
        return LIBRARY_MISMATCH;
    }
    //set the handle name first...since the tokenizing below will break it apart
    memcpy(device->os_info.name, filename, strlen(filename));
#if defined (_WIN32)
    //Check if it's Intel NVMe PTL format
    int sscanfret = sscanf(filename, "csmi:%" SCNu32 ":N:%" SCNu32 ":%" SCNu32 ":%" SCNu32 "", &controllerNum, intelPathID, intelTargetID, intelLun);
    if (sscanfret != 0 && sscanfret != EOF && sscanfret == 4)
    {
        intelNVMe = true;
        snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH,  CSMI_HANDLE_BASE_NAME ":%" PRIu32 ":N:%" PRIu32 ":%" PRIu32 ":%" PRIu32, controllerNum, *intelPathID, *intelTargetID, *intelLun);
    }
    else
    {
        sscanfret = sscanf(filename, "csmi:%" SCNu32 ":%" SCNu32 ":%" SCNu32 ":%" SCNu32 "", &controllerNum, &portID, &phyID, &lun);
        if (sscanfret != 0 && sscanfret != EOF)
        {
            snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, CSMI_HANDLE_BASE_NAME ":%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%" PRIu32, controllerNum, portID, phyID, lun);
        }
        else
        {
            return BAD_PARAMETER;
        }
    }
#else
    //TODO: handle non-Windows OS with CSMI
    char nixBaseHandleBuf[10] = { 0 };
    char *nixBaseHandle = &nixBaseHandleBuf[0];
    int sscanfret = sscanf(filename, "csmi:%" SCNu32 ":%" SCNu32 ":%" SCNu32 " :%s", &controllerNum, &portNum, &lun, nixBaseHandle);
    if (sscanfret != 0 && sscanfret != EOF)
    {
        snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, CSMI_HANDLE_BASE_NAME ":%" PRIu32 ":%" PRIu32 ":%" PRIu32 ":%s", controllerNum, portNum, lun, nixBaseHandle);
    }
    else
    {
        return BAD_PARAMETER;
    }
#endif

#if defined(_WIN32)
    TCHAR device_name[CSMI_WIN_MAX_DEVICE_NAME_LENGTH] = { 0 };
    CONST TCHAR *ptrDeviceName = &device_name[0];
#if defined (_MSC_VER) && _MSC_VER < SEA_MSC_VER_VS2015
    _stprintf_s(device_name, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("\\\\.\\SCSI") TEXT("%") TEXT("lu") TEXT(":"), controllerNum);
#else
    _stprintf_s(device_name, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("\\\\.\\SCSI") TEXT("%") TEXT(PRIu32) TEXT(":"), controllerNum);
#endif
    //lets try to open the device.
    device->os_info.fd = CreateFile(ptrDeviceName,
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
    //DWORD lastError = GetLastError();
    if (device->os_info.fd != INVALID_HANDLE_VALUE)
#else
    if ((device->os_info.fd = open(nixBaseHandle, O_RDWR | O_NONBLOCK)) >= 0)
#endif
    {
        device->os_info.minimumAlignment = sizeof(void *);//setting alignment this way to be compatible across OSs since CSMI doesn't really dictate an alignment, but we should set something. - TJE
        device->issue_io = (issue_io_func)send_CSMI_IO;
        device->drive_info.drive_type = SCSI_DRIVE;//assume SCSI for now. Can be changed later
        device->drive_info.interface_type = RAID_INTERFACE;//TODO: Only set RAID interface for one that needs a function pointer and is in a RAID!!!
        device->os_info.csmiDeviceData = (ptrCsmiDeviceInfo)calloc(1, sizeof(csmiDeviceInfo));
        if (!device->os_info.csmiDeviceData)
        {
            return MEMORY_FAILURE;
        }
        device->os_info.csmiDeviceData->csmiDevHandle = device->os_info.fd;
        device->os_info.csmiDeviceData->controllerNumber = controllerNum;
        device->os_info.csmiDeviceData->csmiDeviceInfoValid = true;
        //we were able to open the requested handle...now it's time to collect some information we'll need to save for this device so we can talk to it later.
        //get some controller/driver into then start checking for connected ports and increment the counter.
        CSMI_SAS_DRIVER_INFO_BUFFER driverInfo;
        if (SUCCESS == csmi_Get_Driver_Info(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &driverInfo, device->deviceVerbosity))
        {
            device->os_info.csmiDeviceData->csmiMajorVersion = driverInfo.Information.usCSMIMajorRevision;
            device->os_info.csmiDeviceData->csmiMinorVersion = driverInfo.Information.usCSMIMinorRevision;
            //TODO: If this is an Intel RST driver, check the name and additionally check to see if it supports the Intel IOCTLs
            //NOTE: If it's an Intel NVMe, then we need to special case some of the below IOCTLs since it won't respond the same...
            device->os_info.csmiDeviceData->securityAccess = get_CSMI_Security_Access((char*)driverInfo.Information.szName);//With this, we could add some intelligence to when commands are supported or not, at least under Windows, but mostly just a placeholder today. - TJE
        }
        else
        {
            ret = FAILURE;//TODO: should this fail here??? This IOCTL is required...
        }
        CSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfig;
        if (SUCCESS == csmi_Get_Controller_Configuration(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &ctrlConfig, device->deviceVerbosity))
        {
            device->os_info.csmiDeviceData->controllerFlags = ctrlConfig.Configuration.uControllerFlags;
        }
        else
        {
            ret = FAILURE;//TODO: should this fail here??? This IOCTL is required...
        }

#if defined (_WIN32) && defined (ENABLE_INTEL_RST) && !defined (DISABLE_NVME_PASSTHROUGH)
        if (intelNVMe)
        {
            device->drive_info.drive_type = NVME_DRIVE;
            device->issue_io = (issue_io_func)send_Intel_NVM_SCSI_Command;
            device->issue_nvme_io = (issue_io_func)send_Intel_NVM_Command;
            device->os_info.csmiDeviceData->intelRSTSupport.intelRSTSupported = true;
            device->os_info.csmiDeviceData->intelRSTSupport.nvmePassthrough = true;
            device->os_info.csmiDeviceData->scsiAddressValid = true;
            device->os_info.csmiDeviceData->scsiAddress.hostIndex = C_CAST(uint8_t, controllerNum);
            device->os_info.csmiDeviceData->scsiAddress.lun = C_CAST(uint8_t, *intelLun);
            device->os_info.csmiDeviceData->scsiAddress.pathId = C_CAST(uint8_t, *intelPathID);
            device->os_info.csmiDeviceData->portIdentifier = C_CAST(uint8_t, portID);
            device->os_info.csmiDeviceData->phyIdentifier = C_CAST(uint8_t, phyID);
            device->os_info.csmiDeviceData->scsiAddress.targetId = C_CAST(uint8_t, *intelTargetID);
            device->drive_info.namespaceID = *intelLun + 1;//LUN is 0 indexed, whereas namespaces start at 1.
        }
        else
#endif //_WIN32
        {
            device->os_info.csmiDeviceData->portIdentifier = C_CAST(uint8_t, portID);
            device->os_info.csmiDeviceData->phyIdentifier = C_CAST(uint8_t, phyID);
            //read phy info and match the provided port and phy identifier values with the phy data to store sasAddress since it may be needed later.
            CSMI_SAS_PHY_INFO_BUFFER phyInfo;
            if (SUCCESS == csmi_Get_Phy_Info(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &phyInfo, device->deviceVerbosity))
            {
                //Using the data we've already gotten, we need to save phy identifier, port identifier, port protocol, and SAS address.
                //TODO: Check if we should be using the Identify or Attached structure information to populate the support fields.
                //Identify appears to contain initiator data, and attached seems to include target data...
                //bool foundPhyInfoForDevice = false;
                for(uint8_t portNum = 0; portNum < 32 && portNum < phyInfo.Information.bNumberOfPhys; ++portNum)
                {
                    if (phyInfo.Information.Phy[portNum].bPortIdentifier == portID && phyInfo.Information.Phy[portNum].Attached.bPhyIdentifier == phyID)
                    {
                        device->os_info.csmiDeviceData->portProtocol = phyInfo.Information.Phy[portNum].Attached.bTargetPortProtocol;
                        memcpy(device->os_info.csmiDeviceData->sasAddress, phyInfo.Information.Phy[portNum].Attached.bSASAddress, 8);
                        //foundPhyInfoForDevice = true;
                        break;
                    }
                }
            }

            //Need to get SASLun from RAID config IF SSP is supported since we need the SAS LUN value for issuing commands
            //This is not needed for other protocols.
            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SSP)
            {
                //Read the RAID config and find the matching device from SAS address. This is needed because SSP passthrough will need the SAS LUN
                CSMI_SAS_RAID_INFO_BUFFER raidInfo;
                if (SUCCESS == csmi_Get_RAID_Info(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &raidInfo, device->deviceVerbosity))
                {
                    bool foundDrive = false;
                    for (uint32_t raidSet = 0; raidSet < raidInfo.Information.uNumRaidSets && !foundDrive; ++raidSet)
                    {
                        //need to parse the RAID info to figure out how much memory to allocate and read the 
                        uint32_t raidConfigLength = sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) - 1 + (raidInfo.Information.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES));
                        PCSMI_SAS_RAID_CONFIG_BUFFER raidConfig = (PCSMI_SAS_RAID_CONFIG_BUFFER)calloc(raidConfigLength, sizeof(uint8_t));
                        if (!raidConfig)
                        {
                            return MEMORY_FAILURE;
                        }
                        if (SUCCESS == csmi_Get_RAID_Config(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, raidConfig, raidConfigLength, raidSet, CSMI_SAS_RAID_DATA_DRIVES, device->deviceVerbosity))
                        {
                            //iterate through the drives and find a matching SAS address.
                            //If we find a matching SAS address, we need to check the LUN....since we are only doing this for SSP, we should be able to use the get SCSI address function and validate that we have the correct lun.
                            for (uint32_t driveIter = 0; driveIter < raidConfig->Configuration.bDriveCount && driveIter < raidInfo.Information.uMaxDrivesPerSet && !foundDrive; ++driveIter)
                            {
                                if (memcmp(raidConfig->Configuration.Drives[driveIter].bSASAddress, device->os_info.csmiDeviceData->sasAddress, 8) == 0)
                                {
                                    //take the SAS Address and SAS Lun and convert to SCSI Address...this should be supported IF we find a SAS drive.
                                    CSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddress;
                                    if (SUCCESS == csmi_Get_SCSI_Address(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &scsiAddress, raidConfig->Configuration.Drives[driveIter].bSASAddress, raidConfig->Configuration.Drives[driveIter].bSASLun, device->deviceVerbosity))
                                    {
                                        if (scsiAddress.bLun == lun)
                                        {
                                            device->os_info.csmiDeviceData->scsiAddress.hostIndex = scsiAddress.bHostIndex;
                                            device->os_info.csmiDeviceData->scsiAddress.pathId = scsiAddress.bPathId;
                                            device->os_info.csmiDeviceData->scsiAddress.targetId = scsiAddress.bTargetId;
                                            device->os_info.csmiDeviceData->scsiAddress.lun = scsiAddress.bLun;
                                            memcpy(device->os_info.csmiDeviceData->sasLUN, raidConfig->Configuration.Drives[driveIter].bSASLun, 8);
                                            device->os_info.csmiDeviceData->scsiAddressValid = true;
                                            foundDrive = true;
                                        }
                                    }
                                }
                            }
                        }
                        safe_Free(raidConfig);
                    }
                }
            }

            if ((device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA) == 0 && !device->os_info.csmiDeviceData->scsiAddressValid)//TODO: Need to test this. May just want to always try it
            {
                //get scsi address
                //TODO: Need to figure out how we get the LUN...it's part of RAID config drive information, but it is not part of other reported information...only need this under RAID most likely...-TJE
                //TODO: Check to see if SMP requests can somehow figure out the 8 byte SAS Lun values. Will only need it on non-SATA devices. SATA has only a singe LUN, but SAS may have multiple LUNs
                CSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddress;
                if (SUCCESS == csmi_Get_SCSI_Address(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &scsiAddress, device->os_info.csmiDeviceData->sasAddress, device->os_info.csmiDeviceData->sasLUN, device->deviceVerbosity))
                {
                    device->os_info.csmiDeviceData->scsiAddressValid = true;
                    device->os_info.csmiDeviceData->scsiAddress.hostIndex = scsiAddress.bHostIndex;
                    device->os_info.csmiDeviceData->scsiAddress.pathId = scsiAddress.bPathId;
                    device->os_info.csmiDeviceData->scsiAddress.targetId = scsiAddress.bTargetId;
                    device->os_info.csmiDeviceData->scsiAddress.lun = scsiAddress.bLun;
                }
            }

            if (device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA || device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_STP)
            {
                CSMI_SAS_SATA_SIGNATURE_BUFFER signature;
                device->drive_info.drive_type = ATA_DRIVE;
                //get sata signature fis and set pmport
                if (SUCCESS == csmi_Get_SATA_Signature(device->os_info.csmiDeviceData->csmiDevHandle, device->os_info.csmiDeviceData->controllerNumber, &signature, device->os_info.csmiDeviceData->phyIdentifier, device->deviceVerbosity))
                {
                    memcpy(&device->os_info.csmiDeviceData->signatureFIS, &signature.Signature.bSignatureFIS, sizeof(sataD2HFis));
                    device->os_info.csmiDeviceData->signatureFISValid = true;
                    device->os_info.csmiDeviceData->sataPMPort = M_Nibble0(device->os_info.csmiDeviceData->signatureFIS.byte1);//lower nibble of this byte holds the port multiplier port that the device is attached to...this can help route the FIS properly
                }
            }

            //Need to check for Intel IOCTL support on SATA drives so we can send the Intel FWDL ioctls instead of passthrough.
            if (strncmp((const char*)driverInfo.Information.szName, "iaStorA", 7) == 0)
            {
                //This is an intel driver.
                //There is a way to get path-target-lun data from the SAS address if the other IOCTLs didn't work (which they don't seem to support this translation anyways)
                if (!device->os_info.csmiDeviceData->scsiAddressValid)
                {
                    //convert SAS address to SCSI address using proprietary intel formatting since IOCTLs above didn't work or weren't used.
                    //NOTE: This is only valid for the noted driver. Previous versions used different formats for sasAddress that don't support firmware update IOCTLs, and are not supported - TJE
                    device->os_info.csmiDeviceData->scsiAddress.lun = device->os_info.csmiDeviceData->sasAddress[0];
                    device->os_info.csmiDeviceData->scsiAddress.targetId = device->os_info.csmiDeviceData->sasAddress[1];
                    device->os_info.csmiDeviceData->scsiAddress.pathId = device->os_info.csmiDeviceData->sasAddress[2];
                    device->os_info.csmiDeviceData->scsiAddressValid = true;
                }
            }
        }

        ret = fill_Drive_Info_Data(device);
    }
    return ret;
}

bool is_CSMI_Handle(const char * filename)
{
    bool isCSMI = false;
    //TODO: Expand this check to make sure all necessary parts of handle are present???
    if (strstr(filename, CSMI_HANDLE_BASE_NAME))
    {
        isCSMI = true;
    }
    return isCSMI;
}

eCSMISecurityAccess get_CSMI_Security_Access(char *driverName)
{
    eCSMISecurityAccess access = CSMI_SECURITY_ACCESS_NONE;
#if defined (_WIN32)
    HKEY keyHandle;
    TCHAR *baseRegKeyPath = TEXT("SYSTEM\\CurrentControlSet\\Services\\");
    TCHAR *paramRegKeyPath = TEXT("\\Parameters");
    size_t tdriverNameLength = (strlen(driverName) + 1) * sizeof(TCHAR);
    size_t registryKeyStringLength = _tcslen(baseRegKeyPath) + tdriverNameLength + _tcslen(paramRegKeyPath);
    TCHAR *registryKey = (TCHAR*)calloc(registryKeyStringLength, sizeof(TCHAR));
    TCHAR *tdriverName = (TCHAR*)calloc(tdriverNameLength, sizeof(TCHAR));
    if (tdriverName)
    {
        _stprintf_s(tdriverName, tdriverNameLength, TEXT("%hs"), driverName);
    }
    if (registryKey)
    {
        _stprintf_s(registryKey, registryKeyStringLength, TEXT("%s%s%s"), baseRegKeyPath, tdriverName, paramRegKeyPath);
    }
    if (tdriverName && registryKey && _tcslen(tdriverName) > 0 && _tcslen(registryKey) > 0)
    {
        if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, registryKey, 0, KEY_READ, &keyHandle))
        {
            //Found the driver's parameters. Now search for CSMI DWORD
            DWORD dataLen = 4;
            BYTE regData[4] = { 0 };
            TCHAR *valueName = TEXT("CSMI");
            DWORD valueType = REG_DWORD;
            if (ERROR_SUCCESS == RegQueryValueEx(keyHandle, valueName, NULL, &valueType, regData, &dataLen))
            {
                int32_t dwordVal = M_BytesTo4ByteValue(regData[3], regData[2], regData[1], regData[0]);
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
            }
            else
            {
                //This key doesn't exist. It is not entirely clear what this means when it is not present, so for now, this returns "FULL" since that matches what I see for intel drivers - TJE
                access = CSMI_SECURITY_ACCESS_FULL;
            }
        }
        else
        {
            //This shouldn't happen, but it could happen...setting Limited for now - TJE
            access = CSMI_SECURITY_ACCESS_LIMITED;
        }
    }
    safe_Free(tdriverName);
    safe_Free(registryKey);
#else //not windows, need root, otherwise not available at all. Return FULL if running as root
    if (is_Running_Elevated())
    {
        access = CSMI_SECURITY_ACCESS_FULL;
    }
#endif
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
//!   \param[in] beginningOfList = list of handles to use to check the count. This can prevent duplicate devices if we know some handles should not be looked at.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_CSMI_RAID_Device_Count(uint32_t * numberOfDevices, M_ATTR_UNUSED uint64_t flags, ptrRaidHandleToScan *beginningOfList)
{
    CSMI_HANDLE fd = CSMI_INVALID_HANDLE;
#if defined (_WIN32)
    TCHAR deviceName[CSMI_WIN_MAX_DEVICE_NAME_LENGTH] = { 0 };
#else
    char deviceName[CSMI_WIN_MAX_DEVICE_NAME_LENGTH] = { 0 };
#endif
    eVerbosityLevels csmiCountVerbosity = VERBOSITY_DEFAULT;//change this if debugging
    ptrRaidHandleToScan raidList = NULL;
    ptrRaidHandleToScan previousRaidListEntry = NULL;
    int controllerNumber = 0, found = 0;

    if (!beginningOfList || !*beginningOfList)
    {
        //don't do anything. Only scan when we get a list to use.
        //Each OS that want's to do this should generate a list of handles to look for.
        return SUCCESS;
    }

    raidList = *beginningOfList;

    //On non-Windows systems, we also have to check controller numbers...so there is one extra top-level loop for this on these systems.
    
    while(raidList)
    {
        bool handleRemoved = false;
        if (raidList->raidHint.csmiRAID || raidList->raidHint.unknownRAID)
        {
#if defined (_WIN32)
            _stprintf_s(deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs"), raidList->handle);
            //lets try to open the controller.
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
#else
            snprintf(deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, "%s", raidList->handle);
            if ((fd = open(filename, O_RDWR | O_NONBLOCK)) >= 0)
#endif
            {
#if !defined (_WIN32)
                for (controllerNumber = 0; controllerNumber < OPENSEA_MAX_CONTROLLERS; ++controllerNumber)
                {
#endif
                    //first, check if this handle supports CSMI before we try anything else
                    CSMI_SAS_DRIVER_INFO_BUFFER driverInfo;
                    CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
                    memset(&driverInfo, 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
                    memset(&controllerConfig, 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
                    if (SUCCESS == csmi_Get_Driver_And_Controller_Data(fd, controllerNumber, &driverInfo, &controllerConfig, csmiCountVerbosity))
                    {
                        //Check if it's a RAID capable controller. We only want to enumerate devices on those in this function
                        if (controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID
                            || controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID
                            || controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY)
                        {
                            //Get RAID info
                            CSMI_SAS_RAID_INFO_BUFFER csmiRAIDInfo;
                            csmi_Get_RAID_Info(fd, controllerNumber, &csmiRAIDInfo, csmiCountVerbosity);
                            //Get RAID config
                            for (uint32_t raidSet = 0; raidSet < csmiRAIDInfo.Information.uNumRaidSets; ++raidSet)
                            {
                                //start with a length that adds no padding for extra drives, then reallocate to a new size when we know the new size
                                uint32_t raidConfigLength = sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) + csmiRAIDInfo.Information.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES);
                                PCSMI_SAS_RAID_CONFIG_BUFFER csmiRAIDConfig = (PCSMI_SAS_RAID_CONFIG_BUFFER)calloc(raidConfigLength, sizeof(uint8_t));
                                if (csmiRAIDConfig)
                                {
                                    if (SUCCESS == csmi_Get_RAID_Config(fd, controllerNumber, csmiRAIDConfig, raidConfigLength, raidSet, CSMI_SAS_RAID_DATA_DRIVES, csmiCountVerbosity))
                                    {
                                        //make sure we got all the drive information...if now, we need to reallocate with some more memory
                                        for (uint32_t iter = 0; iter < csmiRAIDConfig->Configuration.bDriveCount && iter < csmiRAIDInfo.Information.uMaxDrivesPerSet; ++iter)
                                        {
                                            switch (csmiRAIDConfig->Configuration.bDataType)
                                            {
                                            case CSMI_SAS_RAID_DATA_DRIVES:
                                                switch (csmiRAIDConfig->Configuration.Drives[iter].bDriveUsage)
                                                {
                                                case CSMI_SAS_DRIVE_CONFIG_NOT_USED:
                                                    //Don't count drives with this flag, because they are not configured in a RAID at this time. We only want those configured in a RAID/RAID-like scenario.
                                                    break;
                                                case CSMI_SAS_DRIVE_CONFIG_MEMBER:
                                                case CSMI_SAS_DRIVE_CONFIG_SPARE:
                                                case CSMI_SAS_DRIVE_CONFIG_SPARE_ACTIVE:
                                                case CSMI_SAS_DRIVE_CONFIG_SRT_CACHE:
                                                case CSMI_SAS_DRIVE_CONFIG_SRT_DATA:
                                                    ++found;
                                                    break;
                                                default:
                                                    break;
                                                }
                                                break;
                                            case CSMI_SAS_RAID_DATA_DEVICE_ID:
                                            case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
                                            default:
                                                break;
                                            }
                                        }
                                    }
                                    safe_Free(csmiRAIDConfig);
                                }
                            }
                        }
                        //printf("Found CSMI Handle: %s\tRemoving from list.\n", raidList->handle);
                        //This was a CSMI handle, remove it from the list!
                        //This will also increment us to the next handle
                        bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                        raidList = remove_RAID_Handle(raidList, previousRaidListEntry);
                        if (pointerAtBeginningOfRAIDList)
                        {
                            //if the first entry in the list was removed, we need up update the pointer before we exit so that the code that called here won't have an invalid pointer
                            *beginningOfList = raidList;
                        }
                        handleRemoved = true;
                        //printf("Handle removed successfully. raidList = %p\n", raidList);
                    }
#if !defined (_WIN32) //loop through controller numbers
                }
#endif
            }
            //close handle to the controller
#if defined (_WIN32)
            if (fd != INVALID_HANDLE_VALUE)
            {
                CloseHandle(fd);
            }
#else
            if (fd < 0)
            {
                close(fd);
            }
#endif
        }
        if (!handleRemoved)
        {
            previousRaidListEntry = raidList;//store handle we just looked at in case we need to remove one from the list
            //increment to next element in the list
            raidList = raidList->next;
        }
    }
    *numberOfDevices = found;
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
int get_CSMI_RAID_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags, ptrRaidHandleToScan *beginningOfList)
{
    int returnValue = SUCCESS;
    int numberOfDevices = 0;
    CSMI_HANDLE fd = CSMI_INVALID_HANDLE;
#if defined (_WIN32)
    TCHAR deviceName[CSMI_WIN_MAX_DEVICE_NAME_LENGTH] = { 0 };
#else
    char deviceName[CSMI_WIN_MAX_DEVICE_NAME_LENGTH] = { 0 };
#endif
    eVerbosityLevels csmiListVerbosity = VERBOSITY_DEFAULT;//If debugging, change this and down below where this is set per device will also need changing
    
    if (!beginningOfList || !*beginningOfList)
    {
        //don't do anything. Only scan when we get a list to use.
        //Each OS that want's to do this should generate a list of handles to look for.
        return SUCCESS;
    }

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
        tDevice * d = NULL;
        ptrRaidHandleToScan raidList = *beginningOfList;
        ptrRaidHandleToScan previousRaidListEntry = NULL;
        int controllerNumber = 0, found = 0, failedGetDeviceCount = 0;
        numberOfDevices = sizeInBytes / sizeof(tDevice);
        d = ptrToDeviceList;

        //On non-Windows systems, we also have to check controller numbers...so there is one extra top-level loop for this on these systems.
        
        while (raidList && found < numberOfDevices)
        {
            bool handleRemoved = false;
            if (raidList->raidHint.csmiRAID || raidList->raidHint.unknownRAID)
            {
                eCSMISecurityAccess csmiAccess = CSMI_SECURITY_ACCESS_NONE;//only really needed in Windows - TJE
#if defined (_WIN32)
                //Get the controller number from the scsi handle since we need it later!
                int ret = sscanf(raidList->handle, "\\\\.\\SCSI%d:", &controllerNumber);
                if (ret == 0 || ret != EOF)
                {
                    printf("WARNING: Unable to scan controller number!\n");
                }

                _stprintf_s(deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, TEXT("%hs"), raidList->handle);
                //lets try to open the controller.
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
#else
                snprintf(deviceName, CSMI_WIN_MAX_DEVICE_NAME_LENGTH, "%s", raidList->handle);
                if ((fd = open(filename, O_RDWR | O_NONBLOCK)) >= 0)
#endif
                {
#if !defined (_WIN32)
                    for (controllerNumber = 0; controllerNumber < OPENSEA_MAX_CONTROLLERS && found < numberOfDevices; ++controllerNumber)
                    {
#endif
                        //first, check if this handle supports CSMI before we try anything else
                        CSMI_SAS_DRIVER_INFO_BUFFER driverInfo;
                        CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfig;
                        memset(&driverInfo, 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));
                        memset(&controllerConfig, 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));
                        csmiListVerbosity = d->deviceVerbosity;//this is to preserve any verbosity set when coming into this function
                        if (SUCCESS == csmi_Get_Driver_And_Controller_Data(fd, controllerNumber, &driverInfo, &controllerConfig, csmiListVerbosity))
                        {
                            csmiAccess = get_CSMI_Security_Access((char*)driverInfo.Information.szName);
                            switch (csmiAccess)
                            {
                            case CSMI_SECURITY_ACCESS_NONE:
                                printf("CSMI Security access set to none! Won't be able to properly communicate with the device(s)!\n");
                                break;
                            case CSMI_SECURITY_ACCESS_RESTRICTED:
                                printf("CSMI Security access set to restricted! Won't be able to properly communicate with the device(s)!\n");
                                break;
                            case CSMI_SECURITY_ACCESS_LIMITED:
                                printf("CSMI Security access set to limited! Won't be able to properly communicate with the device(s)!\n");
                                break;
                            case CSMI_SECURITY_ACCESS_FULL:
                            default:
                                break;
                            }
                            //Check if it's a RAID capable controller. We only want to enumerate devices on those in this function
                            if (controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SAS_RAID
                                || controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SATA_RAID
                                || controllerConfig.Configuration.uControllerFlags & CSMI_SAS_CNTLR_SMART_ARRAY)
                            {
                                //Get RAID info & Phy info. Need to match the RAID config (below) to some of the phy info as best we can...-TJE
#if defined (_WIN32)
                                bool isIntelDriver = false;
#endif
                                CSMI_SAS_PHY_INFO_BUFFER phyInfo;
                                CSMI_SAS_RAID_INFO_BUFFER csmiRAIDInfo;
                                csmi_Get_RAID_Info(fd, controllerNumber, &csmiRAIDInfo, csmiListVerbosity);
                                csmi_Get_Phy_Info(fd, controllerNumber, &phyInfo, csmiListVerbosity);
#if defined (_WIN32)
                                if (strncmp((const char*)driverInfo.Information.szName, "iaStor", 6) == 0)
                                {
                                    isIntelDriver = true;
                                }
#endif
                                //Get RAID config
                                for (uint32_t raidSet = 0; raidSet < csmiRAIDInfo.Information.uNumRaidSets && found < numberOfDevices; ++raidSet)
                                {
                                    //start with a length that adds no padding for extra drives, then reallocate to a new size when we know the new size
                                    uint32_t raidConfigLength = sizeof(CSMI_SAS_RAID_CONFIG_BUFFER) + csmiRAIDInfo.Information.uMaxDrivesPerSet * sizeof(CSMI_SAS_RAID_DRIVES);
                                    PCSMI_SAS_RAID_CONFIG_BUFFER csmiRAIDConfig = (PCSMI_SAS_RAID_CONFIG_BUFFER)calloc(raidConfigLength, sizeof(uint8_t));
                                    if (csmiRAIDConfig)
                                    {
                                        if (SUCCESS == csmi_Get_RAID_Config(fd, controllerNumber, csmiRAIDConfig, raidConfigLength, raidSet, CSMI_SAS_RAID_DATA_DRIVES, csmiListVerbosity))
                                        {
                                            //make sure we got all the drive information...if now, we need to reallocate with some more memory
                                            for (uint32_t iter = 0; iter < csmiRAIDConfig->Configuration.bDriveCount && iter < csmiRAIDInfo.Information.uMaxDrivesPerSet && found < numberOfDevices; ++iter)
                                            {
                                                bool foundDevice = false;
                                                char handle[20] = { 0 };
                                                switch (csmiRAIDConfig->Configuration.bDataType)
                                                {
                                                case CSMI_SAS_RAID_DATA_DRIVES:
                                                    switch (csmiRAIDConfig->Configuration.Drives[iter].bDriveUsage)
                                                    {
                                                    case CSMI_SAS_DRIVE_CONFIG_NOT_USED:
                                                        //Don't count drives with this flag, because they are not configured in a RAID at this time. We only want those configured in a RAID/RAID-like scenario.
                                                        break;
                                                    case CSMI_SAS_DRIVE_CONFIG_MEMBER:
                                                    case CSMI_SAS_DRIVE_CONFIG_SPARE:
                                                    case CSMI_SAS_DRIVE_CONFIG_SPARE_ACTIVE:
                                                    case CSMI_SAS_DRIVE_CONFIG_SRT_CACHE:
                                                    case CSMI_SAS_DRIVE_CONFIG_SRT_DATA:
                                                        //Need to setup a handle and try get_Device to see if it works.
                                                        //NOTE: Need to know if on intel AND if model contains "NVMe" because we need to setup that differently to discover it properly
#if defined (_WIN32)
                                                        if (isIntelDriver && strncmp((const char*)csmiRAIDConfig->Configuration.Drives[iter].bModel, "NVMe", 4) == 0)
                                                        {
                                                            //This should only happen on Intel Drivers using SRT
                                                            //The SAS Address holds port-target-lun data in it. NOTE: This is correct for this version of the driver, but this is not necessarily true for previous RST drivers according to documentation received from Intel. -TJE
                                                            uint8_t path = 0, target = 0, lun = 0;
                                                            lun = csmiRAIDConfig->Configuration.Drives[iter].bSASAddress[0];
                                                            target = csmiRAIDConfig->Configuration.Drives[iter].bSASAddress[1];
                                                            path = csmiRAIDConfig->Configuration.Drives[iter].bSASAddress[2];
                                                            //TODO: don't know which bytes hold target and lun...leaving as zero since they are TECHNICALLY reserved in the documentation
                                                            //\\.\SCSI?: number is needed in windows, this is the controllerNumber in Windows.
                                                            snprintf(handle, 20, "csmi:%" CPRIu8 ":N:%" CPRIu8 ":%" CPRIu8 ":%" CPRIu8, controllerNumber, path, target, lun);
                                                            foundDevice = true;
                                                        }
                                                        else //SAS or SATA drive
#endif
                                                        {
                                                            //Compare this drive info to phy info as best we can using SASAddress field. 
                                                            //NOTE: If this doesn't work on some controllers, then this will get even more complicated as we will need to try other CSMI commands and attempt reading drive identify or inquiry data to make the match correctly!!!
                                                            //Loop through phy info and find matching SAS address...should only occur ONCE even with multiple Luns since they attach to the same Phy
                                                            for (uint8_t phyIter = 0; !foundDevice && phyIter < 32 && phyIter < phyInfo.Information.bNumberOfPhys; ++phyIter)
                                                            {
                                                                if (memcmp(phyInfo.Information.Phy[phyIter].Attached.bSASAddress, csmiRAIDConfig->Configuration.Drives[iter].bSASAddress, 8) == 0)
                                                                {
                                                                    uint8_t lun = 0;
                                                                    if (!is_Empty(csmiRAIDConfig->Configuration.Drives[iter].bSASLun, 8))//Check if there is a lun value...should be zero on SATA and single Lun SAS drives...otherwise we'll need to convert it!
                                                                    {
                                                                        //This would be a multi-lun SAS drive. This device and the driver should actually be able to translate SASAddress and SASLun to a SCSI address for us.
                                                                        CSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddress;
                                                                        if (SUCCESS == csmi_Get_SCSI_Address(fd, controllerNumber, &scsiAddress, csmiRAIDConfig->Configuration.Drives[iter].bSASAddress, csmiRAIDConfig->Configuration.Drives[iter].bSASLun, VERBOSITY_DEFAULT))
                                                                        {
                                                                            lun = scsiAddress.bLun;
                                                                        }
                                                                        else
                                                                        {
                                                                            printf("Error converting SASLun to SCSI Address lun!\n");
                                                                            //TODO: This is likely actually enough for a SCSI drive to work, but we would need to change more code to accept a full SAS Address and SAS Lun style handle.
                                                                            break;
                                                                        }
                                                                    }
                                                                    switch (phyInfo.Information.Phy[phyIter].Attached.bDeviceType)
                                                                    {
                                                                    case CSMI_SAS_END_DEVICE:
                                                                        foundDevice = true;
                                                                        snprintf(handle, 20, "csmi:%" CPRIu8 ":%" CPRIu8 ":%" CPRIu8 ":%" CPRIu8, controllerNumber, phyInfo.Information.Phy[phyIter].bPortIdentifier, phyInfo.Information.Phy[phyIter].Attached.bPhyIdentifier, lun);
                                                                        break;
                                                                    case CSMI_SAS_NO_DEVICE_ATTACHED:
                                                                    case CSMI_SAS_EDGE_EXPANDER_DEVICE:
                                                                    case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
                                                                    default:
                                                                        break;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                        if (foundDevice)
                                                        {
                                                            memset(d, 0, sizeof(tDevice));
                                                            d->sanity.size = ver.size;
                                                            d->sanity.version = ver.version;
                                                            d->dFlags = flags;
                                                            returnValue = get_CSMI_RAID_Device(handle, d);
                                                            if (returnValue != SUCCESS)
                                                            {
                                                                failedGetDeviceCount++;
                                                            }
                                                            ++d;
                                                            //If we were unable to open the device using get_CSMI_Device, then  we need to increment the failure counter. - TJE
                                                            ++found;
                                                        }
                                                        break;
                                                    default:
                                                        break;
                                                    }
                                                    break;
                                                case CSMI_SAS_RAID_DATA_DEVICE_ID:
                                                case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
                                                default:
                                                    break;
                                                }
                                            }
                                        }
                                        safe_Free(csmiRAIDConfig);
                                    }
                                }
                            }
                        }
                        //This was a CSMI handle, remove it from the list!
                        //This will also increment us to the next handle
                        raidList = remove_RAID_Handle(raidList, previousRaidListEntry);
                        handleRemoved = true;
#if !defined (_WIN32) //loop through controller numbers
                    }
#endif
                }
                //close handle to the controller
#if defined (_WIN32)
                if (fd != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(fd);
                }
#else
                if (fd < 0)
                {
                    close(fd);
                }
#endif
                if (!handleRemoved)
                {
                    previousRaidListEntry = raidList;//store handle we just looked at in case we need to remove one from the list
                    //increment to next element in the list
                    raidList = raidList->next;
                }
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
    }
    return returnValue;
}

static int send_SSP_Passthrough_Command(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (!scsiIoCtx)
    {
        return BAD_PARAMETER;
    }
    csmiSSPIn sspInputs;
    csmiSSPOut sspOutputs;
    seatimer_t sspTimer;
    memset(&sspInputs, 0, sizeof(csmiSSPIn));
    memset(&sspOutputs, 0, sizeof(csmiSSPOut));
    memset(&sspTimer, 0, sizeof(seatimer_t));

    sspInputs.cdb = scsiIoCtx->cdb;
    sspInputs.cdbLength = scsiIoCtx->cdbLength;
    sspInputs.connectionRate = CSMI_SAS_LINK_RATE_NEGOTIATED;
    sspInputs.dataLength = scsiIoCtx->dataLength;
    memcpy(sspInputs.destinationSASAddress, scsiIoCtx->device->os_info.csmiDeviceData->sasAddress, 8);
    memcpy(sspInputs.lun, scsiIoCtx->device->os_info.csmiDeviceData->sasLUN, 8);
    sspInputs.phyIdentifier = scsiIoCtx->device->os_info.csmiDeviceData->phyIdentifier;
    sspInputs.portIdentifier = scsiIoCtx->device->os_info.csmiDeviceData->portIdentifier;
    sspInputs.ptrData = scsiIoCtx->pdata;
    sspInputs.timeoutSeconds = scsiIoCtx->timeout;

    sspInputs.flags = CSMI_SAS_SSP_TASK_ATTRIBUTE_SIMPLE;//start with this. don't really care about other attributes
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

    sspOutputs.sspTimer = &sspTimer;
    sspOutputs.senseDataLength = scsiIoCtx->senseDataSize;
    sspOutputs.senseDataPtr = scsiIoCtx->psense;

    //issue the command
    ret = csmi_SSP_Passthrough(scsiIoCtx->device->os_info.csmiDeviceData->csmiDevHandle, scsiIoCtx->device->os_info.csmiDeviceData->controllerNumber, &sspInputs, &sspOutputs, scsiIoCtx->device->deviceVerbosity);

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(sspTimer);

    return ret;
}

static int send_STP_Passthrough_Command(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (!scsiIoCtx || !scsiIoCtx->pAtaCmdOpts)
    {
        return BAD_PARAMETER;
    }
    csmiSTPIn stpInputs;
    csmiSTPOut stpOutputs;
    sataH2DFis h2dFis;
    uint8_t statusFIS[20] = { 0 };
    seatimer_t stpTimer;
    memset(&stpInputs, 0, sizeof(csmiSTPIn));
    memset(&stpOutputs, 0, sizeof(csmiSTPOut));
    memset(&stpTimer, 0, sizeof(seatimer_t));

    //setup the FIS
    build_H2D_FIS_From_ATA_PT_Command(&h2dFis, &scsiIoCtx->pAtaCmdOpts->tfr, scsiIoCtx->device->os_info.csmiDeviceData->sataPMPort);
    stpInputs.commandFIS = &h2dFis;
    stpInputs.dataLength = scsiIoCtx->pAtaCmdOpts->dataSize;
    stpInputs.ptrData = scsiIoCtx->pAtaCmdOpts->ptrData;
    stpInputs.connectionRate = CSMI_SAS_LINK_RATE_NEGOTIATED;
    stpInputs.timeoutSeconds = scsiIoCtx->pAtaCmdOpts->timeout;

    //Setup CSMI info to route command to the device.
    memcpy(stpInputs.destinationSASAddress, scsiIoCtx->device->os_info.csmiDeviceData->sasAddress, 8);
    stpInputs.phyIdentifier = scsiIoCtx->device->os_info.csmiDeviceData->phyIdentifier;
    stpInputs.portIdentifier = scsiIoCtx->device->os_info.csmiDeviceData->portIdentifier;

    //setup command flags
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
        stpInputs.flags |= CSMI_SAS_STP_EXECUTE_DIAG;
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

    //setup some stuff for output results
    stpOutputs.statusFIS = &statusFIS[0];
    stpOutputs.stpTimer = &stpTimer;

    //send the IO
    ret = csmi_STP_Passthrough(scsiIoCtx->device->os_info.csmiDeviceData->csmiDevHandle, scsiIoCtx->device->os_info.csmiDeviceData->controllerNumber, &stpInputs, &stpOutputs, scsiIoCtx->device->deviceVerbosity);

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(stpTimer);

    //Check result and copy back additional info. 
    if (stpOutputs.retryAsSSPPassthrough)
    {
        //STP is not supported by this controller/driver for this device.
        //So now we need to send the IO as SSP.
        //First, try SAT (no changes), if that fails for invalid operation code, then try legacy CSMI...after that, we are done retrying.
        ret = send_SSP_Passthrough_Command(scsiIoCtx);//This is all we should have to do since SAT style CDBs are always created by default.
        //Check the result. If it was a invalid op code, we could have passed
        if (ret == SUCCESS)
        {
            //check the sense data to see if it is an invalid command or not
            uint8_t senseKey = 0, asc = 0, ascq = 0, fru = 0;
            get_Sense_Key_ASC_ASCQ_FRU(scsiIoCtx->psense, scsiIoCtx->senseDataSize, &senseKey, &asc, &ascq, &fru);
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x20 && ascq == 0x00)//TODO: Check if A1h vs 85h SAT opcodes for retry???
            {
                scsiIoCtx->device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_CSMI;//change to the legacy passthrough
                ret = ata_Passthrough_Command(scsiIoCtx->device, scsiIoCtx->pAtaCmdOpts);
            }
        }
        else
        {
            //something else is wrong....call it a passthrough failure
            ret = OS_PASSTHROUGH_FAILURE;
        }
    }
    else if (ret == SUCCESS)
    {
        //check the status FIS, and set up the proper response
        //This FIS should be either D2H or possibly PIO Setup
        ptrSataD2HFis d2h = (ptrSataD2HFis)&statusFIS[0];
        ptrSataPIOSetupFis pioSet = (ptrSataPIOSetupFis)&statusFIS[0];
        ataReturnTFRs rtfrs;//create this temporarily to save fis output results, then we'll pack it into sense data
        memset(&rtfrs, 0, sizeof(ataReturnTFRs));
        switch (statusFIS[0])
        {
        case FIS_TYPE_REG_D2H:
            rtfrs.status = d2h->status;
            rtfrs.error = d2h->error;
            rtfrs.device = d2h->device;
            rtfrs.lbaLow = d2h->lbaLow;
            rtfrs.lbaMid = d2h->lbaMid;
            rtfrs.lbaHi = d2h->lbaHi;
            rtfrs.lbaLowExt = d2h->lbaLowExt;
            rtfrs.lbaMidExt = d2h->lbaMidExt;
            rtfrs.lbaHiExt = d2h->lbaHiExt;
            rtfrs.secCnt = d2h->sectorCount;
            rtfrs.secCntExt = d2h->sectorCountExt;
            break;
        case FIS_TYPE_PIO_SETUP:
            rtfrs.status = pioSet->eStatus;//TODO: This should be good, but there is a possibility of something not going right here if the data didn't make it properly. can we add more intelligence to select status vs estatus?
            rtfrs.error = pioSet->error;
            rtfrs.device = pioSet->device;
            rtfrs.lbaLow = pioSet->lbaLow;
            rtfrs.lbaMid = pioSet->lbaMid;
            rtfrs.lbaHi = pioSet->lbaHi;
            rtfrs.lbaLowExt = pioSet->lbaLowExt;
            rtfrs.lbaMidExt = pioSet->lbaMidExt;
            rtfrs.lbaHiExt = pioSet->lbaHiExt;
            rtfrs.secCnt = pioSet->sectorCount;
            rtfrs.secCntExt = pioSet->sectorCountExt;
            break;
        default:
            //Unknown FIS response type
            ret = UNKNOWN;
            break;
        }
        //dummy up sense data since that is what the above layers look for. Use descriptor format.
        if (scsiIoCtx->psense)//check that the pointer is valid
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
                    scsiIoCtx->psense[12] = rtfrs.secCntExt;// Sector Count Ext
                    scsiIoCtx->psense[14] = rtfrs.lbaLowExt;// LBA Lo Ext
                    scsiIoCtx->psense[16] = rtfrs.lbaMidExt;// LBA Mid Ext
                    scsiIoCtx->psense[18] = rtfrs.lbaHiExt;// LBA Hi
                }
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = rtfrs.error;// Error
                scsiIoCtx->psense[13] = rtfrs.secCnt;// Sector Count
                scsiIoCtx->psense[15] = rtfrs.lbaLow;// LBA Lo
                scsiIoCtx->psense[17] = rtfrs.lbaMid;// LBA Mid
                scsiIoCtx->psense[19] = rtfrs.lbaHi;// LBA Hi
                scsiIoCtx->psense[20] = rtfrs.device;// Device/Head
                scsiIoCtx->psense[21] = rtfrs.status;// Status
            }
        }
    }
    
    return ret;
}

int send_CSMI_IO(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SATA || scsiIoCtx->device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_STP))
    {
        ret = send_STP_Passthrough_Command(scsiIoCtx);
    }
    else if (scsiIoCtx->device->os_info.csmiDeviceData->portProtocol & CSMI_SAS_PROTOCOL_SSP)
    {
        ret = send_SSP_Passthrough_Command(scsiIoCtx);
    }
    //Need case to translate SCSI CDB to ATA command!
    else if (scsiIoCtx->device->drive_info.drive_type == ATA_DRIVE)
    {
        //Software SAT translation
        ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
    }
    else
    {
        return BAD_PARAMETER;
    }
    return ret;
}

void print_CSMI_Device_Info(tDevice *device)
{
    if (device->os_info.csmiDeviceData && device->os_info.csmiDeviceData->csmiDeviceInfoValid)
    {
        //print the things we stored since those are what we currently care about. Can add printing other things out later if they are determined to be of use. - TJE
        printf("\n=====CSMI Info=====\n");
        printf("\tCSMI Version: %" CPRIu16 ".%" CPRIu16 "\n", device->os_info.csmiDeviceData->csmiMajorVersion, device->os_info.csmiDeviceData->csmiMinorVersion);
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
        if (device->os_info.csmiDeviceData->intelRSTSupport.intelRSTSupported && device->os_info.csmiDeviceData->intelRSTSupport.nvmePassthrough)
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

            printf("\tSAS Address: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", device->os_info.csmiDeviceData->sasAddress[0], device->os_info.csmiDeviceData->sasAddress[1], device->os_info.csmiDeviceData->sasAddress[2], device->os_info.csmiDeviceData->sasAddress[3], device->os_info.csmiDeviceData->sasAddress[4], device->os_info.csmiDeviceData->sasAddress[5], device->os_info.csmiDeviceData->sasAddress[6], device->os_info.csmiDeviceData->sasAddress[7]);
            printf("\tSAS Lun: %02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "%02" CPRIX8 "h\n", device->os_info.csmiDeviceData->sasLUN[0], device->os_info.csmiDeviceData->sasLUN[1], device->os_info.csmiDeviceData->sasLUN[2], device->os_info.csmiDeviceData->sasLUN[3], device->os_info.csmiDeviceData->sasLUN[4], device->os_info.csmiDeviceData->sasLUN[5], device->os_info.csmiDeviceData->sasLUN[6], device->os_info.csmiDeviceData->sasLUN[7]);
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
    return;
}

#endif //ENABLE_CSMI