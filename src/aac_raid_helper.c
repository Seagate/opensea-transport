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
// \file aac_raid_helper.c
// \brief Defines constants, structures, and functions necessary to communicate with physical drives behind an Adaptec
// RAID (aac, aacraid, arcsas)

#if defined(ENABLE_AAC)

#    include "bit_manip.h"
#    include "code_attributes.h"
#    include "common_types.h"
#    include "error_translation.h"
#    include "io_utils.h"
#    include "math_utils.h"
#    include "memory_safety.h"
#    include "precision_timer.h"
#    include "string_utils.h"
#    include "type_conversion.h"

#    if defined(__unix__) // this is only done in case someone sets weird defines for Windows even though this isn't
                          // supported
#        include <ctype.h>
#        include <dirent.h>
#        include <fcntl.h>
#        include <libgen.h> //for basename function
#        include <sys/ioctl.h>
#        include <sys/stat.h>
#        include <sys/types.h>
#        include <unistd.h> // for close
#    endif                  //__unix__

#    include "external/aac/aac_ioctl.h"
#    include "external/aac/aacraid_reg.h"
// may or maynot need aacraid_reg and aacraid_var headers in external include folder -TJE

#    include "aac_raid_helper.h"
#    include "aac_raid_helper_func.h"
#    include "raid_scan_helper.h"
#    include "scsi_helper_func.h"

extern bool validate_Device_Struct(versionBlock);

// NOTES:
//   1. Need controller information to detect capabilities
//   2. Some controllers can only access memory in 32bit range
//   3. scatter gather list limited to 1 for passthrough CDBs
//   4. Internal driver code sets the address in little endian...probably need to do this too! (addr[0] = addr &
//   UINT32_MAX, addr[1] = addr >> 32)
//   5. Can issue some commands with FIB passthrough instead of the SRB passthrough. Not sure when this would be
//   preferred or not. SRB is converted to FIB anyways
//   6. SCSI 32 vs SCSI64. Need controller info to determine when each is supported.
//   7. "containers" seems to related to the RAID mode. THis might help us find devices/counts
//   8. All FIBs are a single size defined in the code to 512B in length.
//   9. Supplemental adapter info only available if in the options of the adapter info.
//  10. aac_bus_info can tell you which bus' are valid to scan them. This is a container command of VM_Ioctl, FT_DRIVE
//  object, method 1, CTLCmd GetBusInfo
//  11. DAC mode only if dma>32bit, more than 32bit memory, and controller supports 64bit sg list
//  12. There are some quirks in the code that may require using scsi32 when discovered. Need to find in driver identiy
//  with cardtype to read quirks
//  13. CT_GET_PHYDEV_LIST & CT_GET_PHYDEV_INFO look like they will help get info about physical devices. Phydev info
//  gives bus, target, slice, lun. Use VM_ContainerConfig to read this
//  14. GetDynAdapPropsFIB gives info about how the controller is configured (RAID, HBA, Simple Volume)

// Easy IOCTLs:
//   1. FSACTL_SENDFIB for more specific requests than other codes allow - struct fib_ioctl. Add additional structures
//   to read more info as needed that are not in other simple IOCTLs
//   2. FSACTL_SEND_RAW_SRB - struct user_aac_srb. If on 32bit system use 32bit sg list, otherwise check controller
//   support for 64bit sglist before using 64bits
//   3. FSACTL_GET_PCI_INFO - struct with 32bit bus and slot
//   4. FSACTL_GET_CONTAINERS - aac_get_container_count_resp??? Max 32 containers. read using FIB container command
//   5. FSACTL_GET_HBA_INFO - aac_hba_info
//   6. FSACTL_MINIPORT_REV_CHECK - struct revision
//   7. FSACTL_QUERY_DISK - aac_query_disk controller, bus, target, lun, etc
//   8. FSACTL_RESET_IOP - resets the adapter. Struct uses uint8 to determine soft/hard reset

// Windows possible IOCTL signatures:
//  Arcsas
//  ARCSAS <- smartctl defines this but does not use it
//  ARC-SAS
//  AACAPI <- smartctl uses this in Windows for passthrough using a srb driver structure. Need to check if the
//  structures I'm using will match or not... ARCIOCTL

#    if defined(_WIN32)
// Store possible signatures here so we can easily access them later as needed
static const char* arcsas_ioctl_signatures[] = {"ARCSAS", "ARC-SAS", "AACAPI", "Arcsas", "ARCIOCTL"};

// Use this enum to index the array above to get the correct signature string
typedef enum _eARCIoctlsig
{
    ARC_SIG_ARCSAS   = 0,
    ARC_SIG_ARC_SAS  = 1,
    ARC_SIG_AACAPI   = 2,
    ARC_SIG_Arcsas   = 3,
    ARC_SIG_ARCIOCTL = 4
} eARCIoctlsig;

#    endif //_WIN32

typedef enum _eAacHandleParse
{
    AAC_HANDLE_ID_STRING = 0,
    AAC_HANDLE_BUS,
    AAC_HANDLE_TARGET,
    AAC_HANDLE_LUN
} eAacHandleParse;

static void print_AAC_SRB_Status(uint32_t statusCode)
{
    print_str("AAC SRB Status: ");
    switch (statusCode)
    {
    case AAC_SRB_STS_PENDING:
        print_str("Pending\n");
        break;
    case AAC_SRB_STS_SUCCESS:
        print_str("Success\n");
        break;
    case AAC_SRB_STS_ABORTED:
        print_str("Aborted\n");
        break;
    case AAC_SRB_STS_ABORT_FAILED:
        print_str("Abort Failed\n");
        break;
    case AAC_SRB_STS_ERROR:
        print_str("Error\n");
        break;
    case AAC_SRB_STS_BUSY:
        print_str("Busy\n");
        break;
    case AAC_SRB_STS_INVALID_REQUEST:
        print_str("Invalid Request\n");
        break;
    case AAC_SRB_STS_INVALID_PATH_ID:
        print_str("Invalid Path ID\n");
        break;
    case AAC_SRB_STS_NO_DEVICE:
        print_str("No Device\n");
        break;
    case AAC_SRB_STS_TIMEOUT:
        print_str("Timeout\n");
        break;
    case AAC_SRB_STS_SELECTION_TIMEOUT:
        print_str("Selection Timeout\n");
        break;
    case AAC_SRB_STS_COMMAND_TIMEOUT:
        print_str("Command Timeout\n");
        break;
    case AAC_SRB_STS_MESSAGE_REJECTED:
        print_str("Message Rejected\n");
        break;
    case AAC_SRB_STS_BUS_RESET:
        print_str("Reset\n");
        break;
    case AAC_SRB_STS_PARITY_ERROR:
        print_str("Parity Error\n");
        break;
    case AAC_SRB_STS_REQUEST_SENSE_FAILED:
        print_str("Request Sense Failed\n");
        break;
    case AAC_SRB_STS_NO_HBA:
        print_str("No HBA\n");
        break;
    case AAC_SRB_STS_DATA_OVERRUN:
        print_str("Data Overrun\n");
        break;
    case AAC_SRB_STS_UNEXPECTED_BUS_FREE:
        print_str("Unexpected Bus Free\n");
        break;
    case AAC_SRB_STS_PHASE_SEQUENCE_FAILURE:
        print_str("Phase Sequence Failure\n");
        break;
    case AAC_SRB_STS_BAD_SRB_BLOCK_LENGTH:
        print_str("Bad SRB Block Length\n");
        break;
    case AAC_SRB_STS_REQUEST_FLUSHED:
        print_str("Request Flushed\n");
        break;
    case AAC_SRB_STS_INVALID_LUN:
        print_str("Invalid Lun\n");
        break;
    case AAC_SRB_STS_INVALID_TARGET_ID:
        print_str("Invalid Target ID\n");
        break;
    case AAC_SRB_STS_BAD_FUNCTION:
        print_str("Bad Function\n");
        break;
    case AAC_SRB_STS_ERROR_RECOVERY:
        print_str("Error Recovery\n");
        break;
    default:
        printf("Unknown status code: %" PRIu32 "\n", statusCode);
        break;
    }
    return;
}

static void print_fib_status(uint32_t fibstatus)
{
    print_str("FIB Status: ");
    switch (fibstatus)
    {
    case AAC_ERROR_NORMAL:
        print_str("Normal\n");
        break;
    case AAC_ERROR_PENDING:
        print_str("Pending\n");
        break;
    case AAC_ERROR_FATAL:
        print_str("Fatal\n");
        break;
    case AAC_ERROR_INVALID_QUEUE:
        print_str("Invalid Queue\n");
        break;
    case AAC_ERROR_NOENTRIES:
        print_str("No Entries\n");
        break;
    case AAC_ERROR_SENDFAILED:
        print_str("Send Failed\n");
        break;
    case AAC_ERROR_INVALID_QUEUE_PRIORITY:
        print_str("Invalid Queue Priority\n");
        break;
    case AAC_ERROR_FIB_ALLOCATION_FAILED:
        print_str("Allocation Failed\n");
        break;
    case AAC_ERROR_FIB_DEALLOCATION_FAILED:
        print_str("Deallocation Failed\n");
        break;
    default:
        printf("Unknown FIB status code: %" PRIu32 "\n", fibstatus);
        break;
    }
}

#    define OS_AAC_HANDLE_MAX_LENGTH RAID_HANDLE_STRING_MAX_LEN
#    define OS_AAC_HANDLE_MAX_FIELDS 4
#    define ACC_PARSE_COUNT_SUCCESS  4
static uint8_t parse_AAC_Handle(const char* devName, uint32_t* bus, uint32_t* target, uint32_t* lun)
{
    uint8_t parseCount = 0;
    char* dup = M_NULLPTR;
    if (safe_strdup(&dup, devName) == 0)
    {
        if (strstr(dup, AAC_HANDLE_BASE_NAME) == dup)
        {
            eAacHandleParse counter = AAC_HANDLE_ID_STRING;
            char*           saveptr = M_NULLPTR;
            rsize_t         duplen  = safe_strlen(dup);
            char*           token   = safe_String_Token(dup, &duplen, ":", &saveptr);
            while (token && counter <= AAC_HANDLE_LUN)
            {
                unsigned long temp = 0UL;
                switch (counter)
                {
                case AAC_HANDLE_ID_STRING: // aac - already been validated above
                    ++parseCount;
                    break;
                case AAC_HANDLE_BUS: // bus
                    if (safe_isdigit(token[0]))
                    {
                        if (0 == safe_strtoul(&temp, token, M_NULLPTR, BASE_10_DECIMAL))
                        {
                            if (bus)
                            {
                                *bus = temp;
                            }
                            ++parseCount;
                        }
                    }
                    break;
                case AAC_HANDLE_TARGET:
                    if (safe_isdigit(token[0]))
                    {
                        if (0 == safe_strtoul(&temp, token, M_NULLPTR, BASE_10_DECIMAL))
                        {
                            if (target)
                            {
                                *target = temp;
                            }
                            ++parseCount;
                        }
                    }
                    break;
                case AAC_HANDLE_LUN:
                    if (safe_isdigit(token[0]))
                    {
                        if (0 == safe_strtoul(&temp, token, M_NULLPTR, BASE_10_DECIMAL))
                        {
                            if (lun)
                            {
                                *lun = temp;
                            }
                            ++parseCount;
                        }
                    }
                    break;
                }
                ++counter;
                token = safe_String_Token(M_NULLPTR, &duplen, ":", &saveptr);
            }
        }
        safe_free(&dup);
    }
    return parseCount;
}

bool is_Supported_aacraid_Dev(const char* devName)
{
    bool     supported = false;
    uint32_t bus       = 0;
    uint32_t target    = 0;
    uint32_t lun       = 0;
    if (ACC_PARSE_COUNT_SUCCESS == parse_AAC_Handle(devName, &bus, &target, &lun))
    {
        supported = true;
    }
    return supported;
}

// This is used internally for issuing raw FIB commands that aren't handled by other IOCTLs in this code -TJE
// NOTE: This will be used synchronously!
static eReturnValues send_aac_raid_fib(AAC_HANDLE       fd,
                                       AAC_FibCommands  fibCommand,
                                       const void*      dataRequestIn,
                                       uint32_t         dataRequestInSize,
                                       void*            dataRequestOut,
                                       uint32_t         dataRequestOutSize,
                                       eVerbosityLevels verbosity)
{
    eReturnValues ret              = OS_COMMAND_NOT_AVAILABLE;
    int           localIoctlReturn = 0;
    aac_fib       fib;
    uint32_t      fibxferLen = sizeof(aac_fib); // + dataRequestInSize;
    if (dataRequestInSize > AAC_FIB_DATASIZE || dataRequestOutSize > AAC_FIB_DATASIZE)
    {
        // This request is too large for a FIB and is likely incorrectly being used.
        return OS_COMMAND_NOT_AVAILABLE;
    }
    safe_memset(&fib, sizeof(aac_fib), 0, sizeof(aac_fib));
    // TODO: Verify these flags
    fib.Header.XferState = AAC_FIBSTATE_APIFIB | AAC_FIBSTATE_HOSTOWNED | AAC_FIBSTATE_INITIALISED | AAC_FIBSTATE_EMPTY;
    fib.Header.Command   = M_STATIC_CAST(uint16_t, fibCommand);
    fib.Header.StructType = AAC_FIBTYPE_TFIB; // if flag AAC_FLAGS_NEW_COMM_TYPE2 use TFIB2, AAC_FIBTYPE_TFIB2_64 if
                                              // setting SenderFibHighAddress
    fib.Header.Size             = sizeof(aac_fib_header) + dataRequestInSize;
    fib.Header.SenderSize       = sizeof(aac_fib);
    fib.Header.SenderFibAddress = 0;     // Guessing based on what I have read in driver code to do this ourselves - TJE
    fib.Header.u.ReceiverFibAddress = 0; // Not sure - TJE
    // copy data in as it may contain info to issue the FIB to the controller
    safe_memcpy(fib.data, AAC_FIB_DATASIZE, dataRequestIn, dataRequestInSize);

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("---Sending AAC RAID FIB---\n");
        print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, &fib), sizeof(aac_fib), true);
    }

// Issue the IO
#    if defined(_WIN32)
    DWORD           lastError     = 0;
    PSRB_IO_CONTROL srbControl    = M_NULLPTR;
    ULONG           srbBufferSize = sizeof(SRB_IO_CONTROL) + fibxferLen;
    PUCHAR          srbbuf        = safe_calloc_aligned(srbBufferSize, sizeof(UCHAR), 4096);
    if (srbbuf == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    srbControl               = M_REINTERPRET_CAST(PSRB_IO_CONTROL, srbbuf);
    srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(srbControl->Signature, 8, arcsas_ioctl_signatures[ARC_SIG_AACAPI],
                safe_strnlen(arcsas_ioctl_signatures[ARC_SIG_AACAPI], 8));
    srbControl->Timeout       = 15;
    srbControl->ControlCode   = FSACTL_SENDFIB;
    srbControl->Length        = srbBufferSize - sizeof(SRB_IO_CONTROL);
    ULONG      returnedLength = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    if (overlappedStruct.hEvent == M_NULLPTR)
    {
        safe_free_aligned(&srbbuf);
        return OS_PASSTHROUGH_FAILURE;
    }
    SetLastError(ERROR_SUCCESS);
    safe_memcpy(srbbuf + sizeof(SRB_IO_CONTROL), srbBufferSize - sizeof(SRB_IO_CONTROL), &fib, fibxferLen);
    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("...SRB IO Control buffer...\n");
        print_Data_Buffer(srbbuf, srbBufferSize, true);
    }
    localIoctlReturn = DeviceIoControl(fd, IOCTL_SCSI_MINIPORT, srbbuf, srbBufferSize, srbbuf, srbBufferSize,
                                       &returnedLength, &overlappedStruct);
    lastError        = GetLastError();
    if (ERROR_IO_PENDING == lastError) // This will only happen for overlapped commands. If the drive is opened without
                                       // the overlapped flag, everything will work like old synchronous code.-TJE
    {
        localIoctlReturn = GetOverlappedResult(fd, &overlappedStruct, &returnedLength, TRUE);
    }
    else if (lastError != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = M_NULLPTR;
    if (localIoctlReturn == TRUE)
    {
        ret = SUCCESS;
    }
    else
    {
        ret = FAILURE;
    }
    safe_memcpy(&fib, fibxferLen, srbbuf + 1, srbBufferSize - sizeof(SRB_IO_CONTROL));
    safe_free_aligned(&srbbuf);

    if (verbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        printf("Windows Error: ");
        print_Windows_Error_To_Screen(lastError);
    }

#    else  // POSIX type systems
    int lastError = 0;
    DISABLE_WARNING_SIGN_CONVERSION
    localIoctlReturn = ioctl(fd, FSACTL_SENDFIB, &fib);
    RESTORE_WARNING_SIGN_CONVERSION
    lastError = errno;
    if (localIoctlReturn < 0)
    {
        ret = FAILURE;
    }
    else
    {
        ret = SUCCESS;
    }
#    endif //_WIN32 vs other systems

    if (VERBOSITY_COMMAND_NAMES <= verbosity)
    {
        printf("\tAAC FIB IO results:\n");
        printf("\t\tIO returned: %d\n", localIoctlReturn);
        #if defined (_WIN32)
        printf("\t\tAAC Error Code: %lu\n", srbControl->ReturnCode);
        #endif
        // Fib error code???
        if (VERBOSITY_COMMAND_NAMES <= verbosity)
        {
            printf("Fib Response\n");
            print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, &fib), sizeof(aac_fib), true);
        }

        printf("\n");
    }

    // copy out response
    safe_memcpy(dataRequestOut, dataRequestOutSize, fib.data, dataRequestOutSize);
    return ret;
}

static eReturnValues aac_Get_Adapter_Info(AAC_HANDLE fd, aac_adapter_info* info, eVerbosityLevels verbosity)
{
    eReturnValues ret = SUCCESS;
    printf("aac_Get_Adapter_Info\n");
    ret = send_aac_raid_fib(fd, RequestAdapterInfo, M_NULLPTR, 0, info, sizeof(aac_adapter_info), verbosity);
    print_Return_Enum("aac_Get_Adapter_Info\n", ret);
    return ret;
}

static eReturnValues aac_Get_Adapter_Supplemental_Info(AAC_HANDLE                   fd,
                                                       aac_supplement_adapter_info* info,
                                                       eVerbosityLevels             verbosity)
{
    eReturnValues ret = SUCCESS;
    ret = send_aac_raid_fib(fd, RequestSupplementAdapterInfo, info, sizeof(aac_supplement_adapter_info), info,
                            sizeof(aac_supplement_adapter_info), verbosity);
    print_Return_Enum("aac_Get_Adapter_Supplemental_Info\n", ret);
    return ret;
}

// CT_GET_CONFIG_STATUS

// This is used as "MethId in vmioctl"
static eReturnValues aac_VM_Container_Get_SCSI_Method(AAC_HANDLE fd, uint32_t* scsiMethod, eVerbosityLevels verbosity)
{
    eReturnValues  ret = SUCCESS;
    aac_ctcfg      containerCommand;
    aac_ctcfg_resp containerCommandResponse;
    safe_memset(&containerCommand, sizeof(aac_ctcfg), 0, sizeof(aac_ctcfg));
    safe_memset(&containerCommandResponse, sizeof(aac_ctcfg_resp), 0, sizeof(aac_ctcfg_resp));
    containerCommand.Command = VM_ContainerConfig;
    containerCommand.cmd     = CT_GET_SCSI_METHOD;
    containerCommand.param   = 0;
    ret = send_aac_raid_fib(fd, ContainerCommand, &containerCommand, sizeof(aac_ctcfg), &containerCommandResponse,
                            sizeof(aac_ctcfg_resp), verbosity);
    *scsiMethod = containerCommandResponse.param;
    print_Return_Enum("aac_VM_Container_Get_SCSI_Method\n", ret);
    return ret;
}

// For container ID 0, use the MntRespCount to get how many containers are present-TJEVonEric
static eReturnValues aac_VM_Container_Get_Mount_Info(AAC_HANDLE       fd,
                                                     uint32_t         containerID,
                                                     bool             ctrlVarBlkSzSupported,
                                                     bool             ctrl64bitLBASupported,
                                                     aac_mntinforesp* mntInfo,
                                                     uint32_t         mntInfoLen,
                                                     eVerbosityLevels verbosity)
{
    eReturnValues ret = SUCCESS;
    aac_mntinfo   mntinfocmd;
    safe_memset(&mntinfocmd, sizeof(aac_mntinfo), 0, sizeof(aac_mntinfo));
    if (ctrlVarBlkSzSupported)
    {
        mntinfocmd.Command = VM_NameServeAllBlk;
    }
    else if (ctrl64bitLBASupported)
    {
        mntinfocmd.Command = VM_NameServe64;
    }
    else
    {
        mntinfocmd.Command = VM_NameServe;
    }
    mntinfocmd.MntType  = FT_FILESYS;
    mntinfocmd.MntCount = containerID;
    ret = send_aac_raid_fib(fd, ContainerCommand, &mntinfocmd, sizeof(aac_mntinfo), &mntInfo, mntInfoLen, verbosity);
    print_Return_Enum("aac_VM_Container_Get_Mount_Info\n", ret);
    return ret;
}

// CID to UID??? this can be done using container config fib, but not sure if needed.

static eReturnValues send_VM_Container_Get_Bus_Info(AAC_HANDLE           fd,
                                                    uint32_t             scsiMethod,
                                                    aac_vmi_businf_resp* businfo,
                                                    eVerbosityLevels     verbosity)
{
    eReturnValues ret = SUCCESS;
    aac_vmioctl   vmio;
    safe_memset(&vmio, sizeof(aac_vmioctl), 0, sizeof(aac_vmioctl));
    vmio.Command  = VM_Ioctl;
    vmio.ObjType  = FT_DRIVE;
    vmio.MethId   = scsiMethod;
    vmio.ObjId    = UINT32_C(0);
    vmio.IoctlCmd = GetBusInfo;
    ret = send_aac_raid_fib(fd, ContainerCommand, &vmio, sizeof(aac_vmioctl), businfo, sizeof(aac_vmi_businf_resp),
                            verbosity);
    print_Return_Enum("send_VM_Container_Get_Bus_Info\n", ret);
    return ret;
}

// Unable to find code to issue this, but trying to figure out how to do it-TJE
// This can be helpful to figure out what information is already known by the controller before we begin issuing
// commands
static eReturnValues send_VM_Container_Get_DevInfo(AAC_HANDLE            fd,
                                                   uint32_t              scsiMethod,
                                                   uint32_t              bus,
                                                   uint32_t              target,
                                                   uint32_t              lun,
                                                   struct aac_vmi_devinfo_resp* devinfo,
                                                   eVerbosityLevels      verbosity)
{
    eReturnValues ret = SUCCESS;
    aac_vmioctl   vmio;
    safe_memset(&vmio, sizeof(aac_vmioctl), 0, sizeof(aac_vmioctl));
    vmio.Command = VM_Ioctl;
    vmio.ObjType = FT_DRIVE;
    vmio.MethId  = scsiMethod;
    vmio.ObjId   = AAC_BTL_TO_HANDLE(bus, target, lun); // Not sure if this goes here or somewhere else in the buffer.
                                                      // It's possible it's all in IOCTL_BUF but I don't know yet - TJE
    vmio.IoctlCmd = GetDeviceProbeInfo;
    ret = send_aac_raid_fib(fd, ContainerCommand, &vmio, sizeof(aac_vmioctl), devinfo, sizeof( struct aac_vmi_devinfo_resp),
                            verbosity);
    return ret;
}

static eReturnValues send_aac_get_pci_info(AAC_HANDLE fd, struct aac_pci_info* pciInfo, eVerbosityLevels verbosity)
{
    eReturnValues ret = SUCCESS;
    if (pciInfo != M_NULLPTR)
    {
        int localIoctlReturn = 0;
#    if defined(_WIN32)
        DWORD           lastError     = 0;
        PSRB_IO_CONTROL srbControl    = M_NULLPTR;
        ULONG           srbBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(struct aac_pci_info);
        PUCHAR          srbbuf        = safe_calloc_aligned(srbBufferSize, sizeof(UCHAR), sizeof(PVOID));
        if (srbbuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        srbControl               = M_REINTERPRET_CAST(PSRB_IO_CONTROL, srbbuf);
        srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
        safe_memcpy(srbControl->Signature, 8, arcsas_ioctl_signatures[ARC_SIG_AACAPI],
                    safe_strnlen(arcsas_ioctl_signatures[ARC_SIG_AACAPI], 8));
        srbControl->Timeout       = 15;
        srbControl->ControlCode   = FSACTL_GET_PCI_INFO;
        srbControl->Length        = srbBufferSize - sizeof(SRB_IO_CONTROL);
        ULONG      returnedLength = 0;
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        if (overlappedStruct.hEvent == M_NULLPTR)
        {
            safe_free_aligned(&srbbuf);
            return OS_PASSTHROUGH_FAILURE;
        }
        if (VERBOSITY_COMMAND_NAMES <= verbosity)
        {
            printf("---Sending AAC RAID Get PCI Info---\n");
            print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, &srbControl), srbBufferSize, true);
        }
        SetLastError(ERROR_SUCCESS);
        localIoctlReturn = DeviceIoControl(fd, IOCTL_SCSI_MINIPORT, srbbuf, srbBufferSize, srbbuf, srbBufferSize,
                                           &returnedLength, &overlappedStruct);
        lastError        = GetLastError();
        if (ERROR_IO_PENDING ==
            lastError) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                       // flag, everything will work like old synchronous code.-TJE
        {
            localIoctlReturn = GetOverlappedResult(fd, &overlappedStruct, &returnedLength, TRUE);
        }
        else if (lastError != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
        if (localIoctlReturn == TRUE)
        {
            safe_memcpy(pciInfo, sizeof(struct aac_pci_info), srbControl + 1, sizeof(struct aac_pci_info));
            if (VERBOSITY_COMMAND_NAMES <= verbosity)
            {
                printf("AAC PCI Info:\n");
                print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, &pciInfo), sizeof(struct aac_pci_info), true);
            }
        }
        else
        {
            ret = FAILURE;
        }
        safe_free_aligned(&srbbuf);
        if (verbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(lastError);
        }
#    else
        int lastError = 0;
        DISABLE_WARNING_SIGN_CONVERSION
        localIoctlReturn = ioctl(fd, FSACTL_GET_PCI_INFO, pciInfo);
        RESTORE_WARNING_SIGN_CONVERSION
        lastError = errno;
        if (localIoctlReturn < 0)
        {
            ret = FAILURE;
        }
        else
        {
            printf("Got PCI info!\n");
        }
#    endif
        printf("\tAAC PCI Info IO results:\n");
        printf("\t\tIO returned: %d\n", localIoctlReturn);
        #if defined (_WIN32)
        printf("\t\tAAC Error Code: %lu\n", srbControl->ReturnCode);
        #endif
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static eReturnValues send_aac_miniport_rev_check(AAC_HANDLE          fd,
                                                 struct aac_rev_check*      inrev,
                                                 struct aac_rev_check_resp* outrev,
                                                 eVerbosityLevels    verbosity)
{
    eReturnValues ret = SUCCESS;
    if (inrev != M_NULLPTR && outrev != M_NULLPTR)
    {
        int localIoctlReturn = 0;
#    if defined(_WIN32)
        DWORD           lastError     = 0;
        PSRB_IO_CONTROL srbControl    = M_NULLPTR;
        ULONG           srbBufferSize = sizeof(SRB_IO_CONTROL) + sizeof(struct aac_rev_check);
        PUCHAR          srbbuf        = safe_calloc_aligned(srbBufferSize, sizeof(UCHAR), sizeof(PVOID));
        if (srbbuf == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        srbControl               = M_REINTERPRET_CAST(PSRB_IO_CONTROL, srbbuf);
        srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
        safe_memcpy(srbControl->Signature, 8, arcsas_ioctl_signatures[ARC_SIG_AACAPI],
                    safe_strnlen(arcsas_ioctl_signatures[ARC_SIG_AACAPI], 8));
        srbControl->Timeout       = 15;
        srbControl->ControlCode   = FSACTL_MINIPORT_REV_CHECK;
        srbControl->Length        = srbBufferSize - sizeof(SRB_IO_CONTROL);
        ULONG      returnedLength = 0;
        OVERLAPPED overlappedStruct;
        safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
        if (overlappedStruct.hEvent == M_NULLPTR)
        {
            safe_free_aligned(&srbbuf);
            return OS_PASSTHROUGH_FAILURE;
        }
        safe_memcpy(srbbuf + sizeof(SRB_IO_CONTROL), srbBufferSize - sizeof(SRB_IO_CONTROL), inrev,
                    sizeof(struct aac_rev_check));
        if (VERBOSITY_COMMAND_NAMES <= verbosity)
        {
            printf("---Sending AAC RAID Miniport Rev Check---\n");
            print_Data_Buffer(srbbuf, srbBufferSize, true);
        }
        SetLastError(ERROR_SUCCESS);
        localIoctlReturn = DeviceIoControl(fd, IOCTL_SCSI_MINIPORT, srbbuf, srbBufferSize, srbbuf, srbBufferSize,
                                           &returnedLength, &overlappedStruct);
        lastError        = GetLastError();
        if (ERROR_IO_PENDING ==
            lastError) // This will only happen for overlapped commands. If the drive is opened without the overlapped
                       // flag, everything will work like old synchronous code.-TJE
        {
            localIoctlReturn = GetOverlappedResult(fd, &overlappedStruct, &returnedLength, TRUE);
        }
        else if (lastError != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = M_NULLPTR;
        if (VERBOSITY_COMMAND_NAMES <= verbosity)
        {
            printf("---SRB AAC RAID Miniport Rev Check Raw Response---\n");
            print_Data_Buffer(srbbuf, srbBufferSize, true);
        }
        if (localIoctlReturn == TRUE)
        {
            safe_memcpy(outrev, sizeof(struct aac_rev_check_resp), srbbuf + sizeof(SRB_IO_CONTROL),
                        sizeof(struct aac_rev_check_resp));
            if (VERBOSITY_COMMAND_NAMES <= verbosity)
            {
                printf("AAC Miniport Rev Response:\n");
                print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, &outrev), sizeof(struct aac_rev_check_resp), true);
            }
        }
        else
        {
            ret = FAILURE;
        }
        safe_free_aligned(&srbbuf);
        if (verbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            printf("Windows Error: ");
            print_Windows_Error_To_Screen(lastError);
        }
#    else
        int lastError = 0;
        DISABLE_WARNING_SIGN_CONVERSION
        localIoctlReturn = ioctl(fd, FSACTL_MINIPORT_REV_CHECK, inrev);
        RESTORE_WARNING_SIGN_CONVERSION
        lastError = errno;
        if (localIoctlReturn < 0)
        {
            ret = FAILURE;
        }
        else
        {
            printf("Got Miniport Rev response!\n");
            safe_memcpy(outrev, sizeof(struct aac_rev_check_resp), inrev, sizeof(struct aac_rev_check));
        }
#    endif
        printf("\tAAC Miniport Rev IO results:\n");
        printf("\t\tIO returned: %d\n", localIoctlReturn);
        #if defined (_WIN32)
        printf("\t\tAAC Error Code: %lu\n", srbControl->ReturnCode);
        #endif
        if (VERBOSITY_COMMAND_NAMES <= verbosity)
        {
            printf("AAC Miniport Rev Response:\n");
            print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, outrev), sizeof(struct aac_rev_check_resp), true);
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static bool supports_aac_raid_ioctls(AAC_HANDLE fd)
{
    bool         supported = false;
    struct aac_pci_info pciInfo;
    safe_memset(&pciInfo, sizeof(struct aac_pci_info), 0, sizeof(struct aac_pci_info));
    if (SUCCESS == send_aac_get_pci_info(fd, &pciInfo, VERBOSITY_QUIET))
    {
        supported = true;
    }
    return supported;
}

static size_t get_sg_list64_length(uint32_t cmdTransferLen, uint32_t maxsgelements, uint32_t maxsgelementlen)
{
    size_t sgsize = sizeof(aac_sg_table) - sizeof(aac_sg_entry64); // just the "top" level structure for now
    // Need to take into account max elements and max element len to figure out how many need to be part of the
    // allocation to pass to the driver
    uint32_t sgcounter = UINT32_C(0);
    for (; sgcounter < maxsgelements && sgcounter < UINT32_MAX && cmdTransferLen > UINT32_C(0); ++sgcounter)
    {
        if (cmdTransferLen > maxsgelementlen)
        {
            // subtract what the maxsgelementlen allows and continue to increase the counter further
            cmdTransferLen -= maxsgelementlen;
        }
        else
        {
            // in this case we need to set cmdTransferLen to zero since it's a final "partial" transfer based on what is
            // allowed in the sg element len This allows the counter to increment one final time then exit the loop
            cmdTransferLen = 0;
        }
    }
    sgsize += sizeof(aac_sg_entry64) * sgcounter;
    if (sgcounter == UINT32_C(0))
    {
        // if for some reason we got a zero then most likely someone called this with transferlen set to zero in the
        // first place and we do not need any sglist
        sgsize = 0;
    }
    return sgsize;
}

static size_t get_sg_list_length(uint32_t cmdTransferLen, uint32_t maxsgelements, uint32_t maxsgelementlen)
{
    size_t sgsize = sizeof(aac_sg_table) - sizeof(aac_sg_entry); // just the "top" level structure for now
    // Need to take into account max elements and max element len to figure out how many need to be part of the
    // allocation to pass to the driver
    uint32_t sgcounter = UINT32_C(0);
    for (; sgcounter < maxsgelements && sgcounter < UINT32_MAX && cmdTransferLen > UINT32_C(0); ++sgcounter)
    {
        if (cmdTransferLen > maxsgelementlen)
        {
            // subtract what the maxsgelementlen allows and continue to increase the counter further
            cmdTransferLen -= maxsgelementlen;
        }
        else
        {
            // in this case we need to set cmdTransferLen to zero since it's a final "partial" transfer based on what is
            // allowed in the sg element len This allows the counter to increment one final time then exit the loop
            cmdTransferLen = 0;
        }
    }
    sgsize += sizeof(aac_sg_entry) * sgcounter;
    if (sgcounter == UINT32_C(0))
    {
        // if for some reason we got a zero then most likely someone called this with transferlen set to zero in the
        // first place and we do not need any sglist
        sgsize = 0;
    }
    return sgsize;
}

// The caller of this function must have already allocated memory based on the length returned by get_sg_list_length
static void setup_sg_list(aac_sg_table* sgtable,
                          uint8_t*      ptrdata,
                          uint32_t      cmdTransferLen,
                          uint32_t      maxsgelements,
                          uint32_t      maxsgelementlen)
{
    if (sgtable != M_NULLPTR && ptrdata != M_NULLPTR && cmdTransferLen > UINT32_C(0))
    {
        uintptr_t sgdataptr = M_REINTERPRET_CAST(uintptr_t, ptrdata);
        sgtable->SgCount    = 0;
        for (uint32_t sgcounter = UINT32_C(0);
             sgcounter < maxsgelements && sgcounter < UINT32_MAX && cmdTransferLen > UINT32_C(0); ++sgcounter)
        {
            if (cmdTransferLen > maxsgelementlen)
            {
                sgtable->SgEntry[sgcounter].SgAddress   = sgdataptr;
                sgtable->SgEntry[sgcounter].SgByteCount = maxsgelementlen;
                // subtract what the maxsgelementlen allows and continue to increase the counter further
                cmdTransferLen -= maxsgelementlen;
                sgdataptr += maxsgelementlen;
            }
            else
            {
                sgtable->SgEntry[sgcounter].SgAddress   = sgdataptr;
                sgtable->SgEntry[sgcounter].SgByteCount = cmdTransferLen;
                // in this case we need to set cmdTransferLen to zero since it's a final "partial" transfer based on
                // what is allowed in the sg element len This allows the counter to increment one final time then exit
                // the loop
                cmdTransferLen = 0;
                sgdataptr = 0; // if the loop were to run again we have setup a null pointer with zero length to be
                               // "used" next time
            }
            sgtable->SgCount += 1;
        }
    }
    return;
}

// The caller of this function must have already allocated memory based on the length returned by get_sg_list_length
static void setup_sg_list64(aac_sg_table64* sgtable,
                            uint8_t*        ptrdata,
                            uint32_t        cmdTransferLen,
                            uint32_t        maxsgelements,
                            uint32_t        maxsgelementlen)
{
    if (sgtable != M_NULLPTR && ptrdata != M_NULLPTR && cmdTransferLen > UINT32_C(0))
    {
        uintptr_t sgdataptr = M_REINTERPRET_CAST(uintptr_t, ptrdata);
        sgtable->SgCount    = 0;
        for (uint32_t sgcounter = UINT32_C(0);
             sgcounter < maxsgelements && sgcounter < UINT32_MAX && cmdTransferLen > UINT32_C(0); ++sgcounter)
        {
            if (cmdTransferLen > maxsgelementlen)
            {
                sgtable->SgEntry64[sgcounter].SgAddress   = sgdataptr;
                sgtable->SgEntry64[sgcounter].SgByteCount = maxsgelementlen;
                // subtract what the maxsgelementlen allows and continue to increase the counter further
                cmdTransferLen -= maxsgelementlen;
                sgdataptr += maxsgelementlen;
            }
            else
            {
                sgtable->SgEntry64[sgcounter].SgAddress   = sgdataptr;
                sgtable->SgEntry64[sgcounter].SgByteCount = cmdTransferLen;
                // in this case we need to set cmdTransferLen to zero since it's a final "partial" transfer based on
                // what is allowed in the sg element len This allows the counter to increment one final time then exit
                // the loop
                cmdTransferLen = 0;
                sgdataptr = 0; // if the loop were to run again we have setup a null pointer with zero length to be
                               // "used" next time
            }
            sgtable->SgCount += 1;
        }
    }
    return;
}

static M_INLINE void safe_free_aac_srb(aac_srb** srb)
{
    safe_free_core(M_REINTERPRET_CAST(void**, srb));
}

static M_INLINE void safe_free_aac_srb64(aac_srb64** srb)
{
    safe_free_core(M_REINTERPRET_CAST(void**, srb));
}

static eReturnValues issue_io_aacraid_Dev64(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
#    if defined(_WIN32)
    DWORD lastError = 0;
#    else
    int lastError = 0;
#    endif
    int localIoctlReturn = 0;
    // 32bit structure instead
    // must dynamically allocate to add number of sg entries into this structure.
    // Note: FreeBSD is limited to 1 entry. Linux is limited to 256 entries and entry length limited by controller or
    // 64K, Illumos has no limits in the driver code unless dma alloc fails.
    size_t     srblen    = sizeof(aac_srb) + sizeof(aac_srb_response);
    size_t     sglistlen = 0;
    seatimer_t commandTimer;
    if (scsiIoCtx->direction != XFER_NO_DATA)
    {
        sglistlen = get_sg_list64_length(scsiIoCtx->dataLength, scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                                         scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        srblen += sglistlen;
    }
    aac_srb64* aaccmd = safe_malloc(srblen);
    if (aaccmd == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(&commandTimer, sizeof(seatimer_t), 0, sizeof(seatimer_t));
    safe_memset(aaccmd, sizeof(aac_srb), 0, sizeof(aac_srb));
    aaccmd->function = AAC_SRB_FUNC_EXECUTE_SCSI;
    // set bus target lun for the device
    aaccmd->bus    = scsiIoCtx->device->os_info.aacDeviceData->bus;
    aaccmd->target = scsiIoCtx->device->os_info.aacDeviceData->target;
    aaccmd->lun    = scsiIoCtx->device->os_info.aacDeviceData->lun;
    // set timeout
    aaccmd->timeout = scsiIoCtx->timeout;
    if (aaccmd->timeout == 0)
    {
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0)
        {
            aaccmd->timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        }
        else
        {
            aaccmd->timeout = 15;
        }
    }
    // set flags
    // SRB_DisableAutosense		can be used to disable automatic request sense.
    // Not currently using this, but it can be used to allow us to issue the request sense which might be something we
    // want to do in the future-TJE
    switch (scsiIoCtx->direction)
    {
    case XFER_NO_DATA:
        aaccmd->flags |= AAC_SRB_FLAGS_NO_DATA_XFER;
        break;
    case XFER_DATA_IN:
        aaccmd->flags |= AAC_SRB_FLAGS_DATA_IN;
        setup_sg_list64(&aaccmd->sg_map, scsiIoCtx->pdata, scsiIoCtx->dataLength,
                        scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                        scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        break;
    case XFER_DATA_OUT:
        aaccmd->flags |= AAC_SRB_FLAGS_DATA_OUT;
        setup_sg_list64(&aaccmd->sg_map, scsiIoCtx->pdata, scsiIoCtx->dataLength,
                        scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                        scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        aaccmd->flags |= AAC_SRB_FLAGS_UNSPECIFIED_DIRECTION;
        setup_sg_list64(&aaccmd->sg_map, scsiIoCtx->pdata, scsiIoCtx->dataLength,
                        scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                        scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        break;
    }
    aaccmd->retry_limit = OBSOLETE; // This is marked as an obsolete parameter in the driver source so setting to zero
    aaccmd->cdb_len     = scsiIoCtx->cdbLength;
    safe_memcpy(aaccmd->cdb, 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    aaccmd->data_len =
        srblen; // NOTE: This field is a little confusing but reading the driver code this seems to be the size of the
                // allocated structure, not the amount of data this command will transfer.
#    if defined(_WIN32)
    PSRB_IO_CONTROL srbControl    = M_NULLPTR;
    ULONG           srbBufferSize = sizeof(SRB_IO_CONTROL) + srblen;
    PUCHAR          srbbuf        = safe_calloc_aligned(srbBufferSize, sizeof(UCHAR), sizeof(PVOID));
    if (srbbuf == M_NULLPTR)
    {
        safe_free_aac_srb64(&aaccmd);
        return MEMORY_FAILURE;
    }
    srbControl               = M_REINTERPRET_CAST(PSRB_IO_CONTROL, srbbuf);
    srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(srbControl->Signature, 8, arcsas_ioctl_signatures[ARC_SIG_AACAPI],
                safe_strlen(arcsas_ioctl_signatures[ARC_SIG_AACAPI]));
    srbControl->Timeout     = aaccmd->timeout;
    srbControl->ControlCode = FSACTL_SEND_RAW_SRB;
    srbControl->Length      = srbBufferSize - sizeof(SRB_IO_CONTROL);
    safe_memcpy(&srbbuf[sizeof(SRB_IO_CONTROL)], srbBufferSize - sizeof(SRB_IO_CONTROL), aaccmd, srblen);
    ULONG      returnedLength = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    if (overlappedStruct.hEvent == M_NULLPTR)
    {
        safe_free_aac_srb64(&aaccmd);
        safe_free_aligned(&srbbuf);
        return OS_PASSTHROUGH_FAILURE;
    }
    SetLastError(ERROR_SUCCESS);
    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
    {
        printf("---AAC SRB IO---\n");
        print_Data_Buffer(srbbuf, srbBufferSize, true);
    }
    start_Timer(&commandTimer);
    localIoctlReturn = DeviceIoControl(scsiIoCtx->device->os_info.aacDeviceData->aacHandle, IOCTL_SCSI_MINIPORT, srbbuf,
                                       srbBufferSize, srbbuf, srbBufferSize, &returnedLength, &overlappedStruct);
    lastError        = GetLastError();
    if (ERROR_IO_PENDING == lastError) // This will only happen for overlapped commands. If the drive is opened without
                                       // the overlapped flag, everything will work like old synchronous code.-TJE
    {
        localIoctlReturn = GetOverlappedResult(scsiIoCtx->device->os_info.aacDeviceData->aacHandle, &overlappedStruct,
                                               &returnedLength, TRUE);
    }
    else if (lastError != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = M_NULLPTR;
    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
    {
        printf("---AAC SRB IO Result---\n");
        print_Data_Buffer(srbbuf, srbBufferSize, true);
    }
    safe_memcpy(aaccmd, srblen, srbbuf + sizeof(SRB_IO_CONTROL), srblen);
#    else
    start_Timer(&commandTimer);
    DISABLE_WARNING_SIGN_CONVERSION
    localIoctlReturn = ioctl(scsiIoCtx->device->os_info.aacDeviceData->aacHandle, FSACTL_SEND_RAW_SRB, aaccmd);
    RESTORE_WARNING_SIGN_CONVERSION
    lastError = errno;
#    endif
    stop_Timer(&commandTimer);

    aac_srb_response* response =
        M_REINTERPRET_CAST(aac_srb_response*, M_STATIC_CAST(uintptr_t, aaccmd) + sizeof(aac_srb64) +
                                                  sglistlen); // response is after the cmd and sglist

    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
    {
        printf("Copied srb response\n");
        print_Data_Buffer(M_REINTERPRET_CAST(const uint8_t*, aaccmd), srblen, true);
        print_fib_status(response->fib_status);
        print_AAC_SRB_Status(response->srb_status);
    }

    // copy sense data back
    if (scsiIoCtx->psense)
    {
        safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, response->sense,
                    M_Min(M_Min(response->sense_len, AAC_HOST_SENSE_DATA_MAX), scsiIoCtx->senseDataSize));
    }

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    safe_free_aac_srb64(&aaccmd);
    return ret;
}

static eReturnValues issue_io_aacraid_Dev32(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
#    if defined(_WIN32)
    DWORD lastError = 0;
#    else
    int lastError = 0;
#    endif
    int localIoctlReturn = 0;
    // 32bit structure instead
    // must dynamically allocate to add number of sg entries into this structure.
    // Note: FreeBSD is limited to 1 entry. Linux is limited to 256 entries and entry length limited by controller or
    // 64K, Illumos has no limits in the driver code unless dma alloc fails.
    size_t     srblen    = sizeof(aac_srb) + sizeof(aac_srb_response);
    size_t     sglistlen = 0;
    seatimer_t commandTimer;
    if (scsiIoCtx->direction != XFER_NO_DATA)
    {
        sglistlen = get_sg_list_length(scsiIoCtx->dataLength, scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                                       scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        srblen += sglistlen;
    }
    aac_srb* aaccmd = safe_malloc(srblen);
    if (aaccmd == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    safe_memset(&commandTimer, sizeof(seatimer_t), 0, sizeof(seatimer_t));
    safe_memset(aaccmd, sizeof(aac_srb), 0, sizeof(aac_srb));
    aaccmd->function = AAC_SRB_FUNC_EXECUTE_SCSI;
    // set bus target lun for the device
    aaccmd->bus    = scsiIoCtx->device->os_info.aacDeviceData->bus;
    aaccmd->target = scsiIoCtx->device->os_info.aacDeviceData->target;
    aaccmd->lun    = scsiIoCtx->device->os_info.aacDeviceData->lun;
    // set timeout
    aaccmd->timeout = scsiIoCtx->timeout;
    if (aaccmd->timeout == 0)
    {
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0)
        {
            aaccmd->timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        }
        else
        {
            aaccmd->timeout = 15;
        }
    }
    // set flags
    // SRB_DisableAutosense		can be used to disable automatic request sense.
    // Not currently using this, but it can be used to allow us to issue the request sense which might be something we
    // want to do in the future-TJE
    switch (scsiIoCtx->direction)
    {
    case XFER_NO_DATA:
        aaccmd->flags |= AAC_SRB_FLAGS_NO_DATA_XFER;
        break;
    case XFER_DATA_IN:
        aaccmd->flags |= AAC_SRB_FLAGS_DATA_IN;
        setup_sg_list(&aaccmd->sg_map, scsiIoCtx->pdata, scsiIoCtx->dataLength,
                      scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                      scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        break;
    case XFER_DATA_OUT:
        aaccmd->flags |= AAC_SRB_FLAGS_DATA_OUT;
        setup_sg_list(&aaccmd->sg_map, scsiIoCtx->pdata, scsiIoCtx->dataLength,
                      scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                      scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        aaccmd->flags |= AAC_SRB_FLAGS_UNSPECIFIED_DIRECTION;
        setup_sg_list(&aaccmd->sg_map, scsiIoCtx->pdata, scsiIoCtx->dataLength,
                      scsiIoCtx->device->os_info.aacDeviceData->maxSGList,
                      scsiIoCtx->device->os_info.aacDeviceData->maxSGTransferLength);
        break;
    }
    aaccmd->retry_limit = OBSOLETE; // This is marked as an obsolete parameter in the driver source so setting to zero
    aaccmd->cdb_len     = scsiIoCtx->cdbLength;
    safe_memcpy(aaccmd->cdb, 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    aaccmd->data_len =
        srblen; // NOTE: This field is a little confusing but reading the driver code this seems to be the size of the
                // allocated structure, not the amount of data this command will transfer.
#    if defined(_WIN32)
    PSRB_IO_CONTROL srbControl    = M_NULLPTR;
    ULONG           srbBufferSize = sizeof(SRB_IO_CONTROL) + srblen;
    PUCHAR          srbbuf        = safe_calloc_aligned(srbBufferSize, sizeof(UCHAR), sizeof(PVOID));
    if (srbbuf == M_NULLPTR)
    {
        safe_free_aac_srb(&aaccmd);
        return MEMORY_FAILURE;
    }
    srbControl               = M_REINTERPRET_CAST(PSRB_IO_CONTROL, srbbuf);
    srbControl->HeaderLength = sizeof(SRB_IO_CONTROL);
    safe_memcpy(srbControl->Signature, 8, arcsas_ioctl_signatures[ARC_SIG_ARCSAS],
                safe_strlen(arcsas_ioctl_signatures[ARC_SIG_ARCSAS]));
    srbControl->Timeout     = aaccmd->timeout;
    srbControl->ControlCode = FSACTL_SEND_RAW_SRB;
    srbControl->Length      = srbBufferSize - sizeof(SRB_IO_CONTROL);
    safe_memcpy(&srbbuf[sizeof(SRB_IO_CONTROL)], srbBufferSize - sizeof(SRB_IO_CONTROL), aaccmd, srblen);
    ULONG      returnedLength = 0;
    OVERLAPPED overlappedStruct;
    safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
    overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
    if (overlappedStruct.hEvent == M_NULLPTR)
    {
        safe_free_aac_srb(&aaccmd);
        safe_free_aligned(&srbbuf);
        return OS_PASSTHROUGH_FAILURE;
    }
    SetLastError(ERROR_SUCCESS);
    start_Timer(&commandTimer);
    localIoctlReturn = DeviceIoControl(scsiIoCtx->device->os_info.aacDeviceData->aacHandle, IOCTL_SCSI_MINIPORT, srbbuf,
                                       srbBufferSize, srbbuf, srbBufferSize, &returnedLength, &overlappedStruct);
    lastError        = GetLastError();
    if (ERROR_IO_PENDING == lastError) // This will only happen for overlapped commands. If the drive is opened without
                                       // the overlapped flag, everything will work like old synchronous code.-TJE
    {
        localIoctlReturn = GetOverlappedResult(scsiIoCtx->device->os_info.aacDeviceData->aacHandle, &overlappedStruct,
                                               &returnedLength, TRUE);
    }
    else if (lastError != ERROR_SUCCESS)
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    stop_Timer(&commandTimer);
    CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
    overlappedStruct.hEvent = M_NULLPTR;
#    else
    start_Timer(&commandTimer);
    DISABLE_WARNING_SIGN_CONVERSION
    localIoctlReturn = ioctl(scsiIoCtx->device->os_info.aacDeviceData->aacHandle, FSACTL_SEND_RAW_SRB, aaccmd);
    RESTORE_WARNING_SIGN_CONVERSION
    lastError = errno;
#    endif
    stop_Timer(&commandTimer);

    aac_srb_response* response =
        M_REINTERPRET_CAST(aac_srb_response*, (aaccmd + 1) + sglistlen); // response is after the cmd and sglist

    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
    {
        print_fib_status(response->fib_status);
        print_AAC_SRB_Status(response->srb_status);
    }

    // copy sense data back
    if (scsiIoCtx->psense)
    {
        safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, response->sense,
                    M_Min(M_Min(response->sense_len, AAC_HOST_SENSE_DATA_MAX), scsiIoCtx->senseDataSize));
    }

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

    safe_free_aac_srb(&aaccmd);
    return ret;
}

eReturnValues issue_io_aacraid_Dev(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (scsiIoCtx && scsiIoCtx->device && scsiIoCtx->device->os_info.aacDeviceData)
    {
        // Only 16B CDB's are supported
        if (scsiIoCtx->cdbLength > 16)
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            // Some controllers support the 64bit structure but only can output to 32bit addresses!!!
            if (true)
            {
                // check if 32bit or 64bit pointers are supported. Some controllers can only access 32bit address space
                ret = issue_io_aacraid_Dev64(scsiIoCtx);
            }
            else
            {
                ret = issue_io_aacraid_Dev32(scsiIoCtx);
            }
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

eReturnValues get_AAC_RAID_Device(const char* filename, tDevice* device)
{
    eReturnValues ret    = FAILURE;
    uint32_t      bus    = 0;
    uint32_t      target = 0;
    uint32_t      lun    = 0;
    DECLARE_ZERO_INIT_ARRAY(char, osHandle, OS_AAC_HANDLE_MAX_LENGTH);
    char* handlePtr = &osHandle[0]; // this is done to prevent warnings
    // Need to open this handle and setup some information then fill in the device information.
    if (!(validate_Device_Struct(device->sanity)))
    {
        return LIBRARY_MISMATCH;
    }
    // set the name that was provided for other display.
    safe_memcpy(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, filename, safe_strlen(filename));
    if (ACC_PARSE_COUNT_SUCCESS == parse_AAC_Handle(filename, &bus, &target, &lun))
    {
        device->os_info.fd            = AAC_INVALID_HANDLE;
        device->os_info.aacDeviceData = safe_calloc(1, sizeof(aacDeviceInfo));
        if (device->os_info.aacDeviceData)
        {
// now open the handle
// In Windows, open \\.\SCSIbus: since that is equivalent to the portnumber windows wants
#    if defined(_WIN32)
            snprintf(handlePtr, OS_AAC_HANDLE_MAX_LENGTH, "\\\\.\\SCSI%" PRIu32 ":", bus);
            device->os_info.fd = CreateFileA(handlePtr,
                                             GENERIC_WRITE | GENERIC_READ, // FILE_ALL_ACCESS,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, M_NULLPTR, OPEN_EXISTING,
#        if 0 //! defined(WINDOWS_DISABLE_OVERLAPPED)
                    FILE_FLAG_OVERLAPPED,
#        else //! WINDOWS_DISABLE_OVERLAPPED
                                             0,
#        endif // WINDOWS_DISABLE_OVERLAPPED
                                             M_NULLPTR);
            if (device->os_info.fd != INVALID_HANDLE_VALUE)
#    else // POSIX
            // TODO: implement support for other systems
            ret = NOT_SUPPORTED;
            safe_free_aac_dev_info(&device->os_info.aacDeviceData);
            if (device->os_info.fd != AAC_INVALID_HANDLE)
#    endif
            {
                // valid handle created, now check that IOCTLs work as expected....try reading PCI info as a quick test
                device->os_info.minimumAlignment         = get_System_Pagesize();
                device->issue_io                         = M_REINTERPRET_CAST(issue_io_func, issue_io_aacraid_Dev);
                device->drive_info.drive_type            = SCSI_DRIVE; // start as SCSI type
                device->drive_info.interface_type        = RAID_INTERFACE;
                device->os_info.aacDeviceData->aacHandle = device->os_info.fd;
                if (supports_aac_raid_ioctls(device->os_info.aacDeviceData->aacHandle))
                {
                    device->os_info.aacDeviceData->bus    = bus;
                    device->os_info.aacDeviceData->target = target;
                    device->os_info.aacDeviceData->lun    = lun;
                    // TODO: Set max sglist and max sgtransferlength
                    // NOTE: Setting 64K per sglist entry for now since that is noted in the linux driver code as a
                    // limit.
                    //       We will try reading the controller supported info to adjust that limit as well.
                    device->os_info.aacDeviceData->maxSGTransferLength = UINT32_C(65536);
#    if defined(__FreeBSD__)
                    device->os_info.aacDeviceData->maxSGList = 1;
                    // No max limit in the driver code
                    device->os_info.aacDeviceData->maxSGTransferLength = UINT32_MAX;
#    elif defined(__linux__)
                    device->os_info.aacDeviceData->maxSGList = 256;
#    else
                    // TODO: What should we set this limit to...if anything?
                    //       Note: leaving as 64KiB for now for 1 entry. Can work around this case by case later
                    device->os_info.aacDeviceData->maxSGList = 1;
#    endif
                    aac_adapter_info ainfo;
                    safe_memset(&ainfo, sizeof(aac_adapter_info), 0, sizeof(aac_adapter_info));
                    if (SUCCESS == aac_Get_Adapter_Info(device->os_info.fd, &ainfo, device->deviceVerbosity))
                    {
                        device->os_info.aacDeviceData->controllerSupportedOptions = ainfo.SupportedOptions;
                    }
                    else
                    {
                        ret = FAILURE;
                    }
                    if (device->os_info.aacDeviceData->controllerSupportedOptions &
                        AAC_SUPPORTED_SUPPLEMENT_ADAPTER_INFO)
                    {
                        // get supplemental info
                        aac_supplement_adapter_info supinfo;
                        safe_memset(&supinfo, sizeof(aac_supplement_adapter_info), 0,
                                    sizeof(aac_supplement_adapter_info));
                        if (SUCCESS ==
                            aac_Get_Adapter_Supplemental_Info(device->os_info.fd, &supinfo, device->deviceVerbosity))
                        {
                            device->os_info.aacDeviceData->supplementalSupportedOptions = supinfo.SupportedOptions2;
                        }
                    }
                    else
                    {
                        device->os_info.aacDeviceData->supplementalSupportedOptions = UINT32_C(0);
                    }
                }
                else
                {
                    close_AAC_RAID_Device(device);
                    ret = NOT_SUPPORTED;
                }
            }
        }
    }
    return ret;
}

eReturnValues close_AAC_RAID_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device)
    {
#    if defined(_WIN32)
        // call Windows code to close the handle
        // ret = Close_Win_Device(device);
#    else
        // TODO: Cloase handle
#    endif
        // Free this structure since we are done with it. The AACHande is also os_info.fd so no need to attempt closing
        // it twice
        safe_free_aac_dev_info(&device->os_info.aacDeviceData);
    }
    return ret;
}

eReturnValues get_AAC_RAID_Device_Count(uint32_t* numberOfDevices, uint64_t flags, ptrRaidHandleToScan* beginningOfList)
{
    eReturnValues ret = SUCCESS;
    // first try PCI info to see if any response will happen at all
    // From the provided handle, query adapter info and supplemental info.
    // Query aac_VM_Container_Get_SCSI_Method
    // Get Mount info for container 0 to figure out how many RAIDs the controller has enabled that we need to scan, then
    // go through them and figure out how many disks are in each one. Use get bus info to figure out which bus's are
    // valid or not before attempting to scan through the device list and how many targets per bus are supported
    // send_VM_Container_Get_DevInfo to each bus-target-lun to figure out what devices are there or not and count these.
    AAC_HANDLE fd = AAC_INVALID_HANDLE;
    DECLARE_ZERO_INIT_ARRAY(char, aacHandleStr, OS_AAC_HANDLE_MAX_LENGTH);
    ptrRaidHandleToScan raidList              = M_NULLPTR;
    ptrRaidHandleToScan previousRaidListEntry = M_NULLPTR;
    uint32_t            found                 = 0;
    eVerbosityLevels    aacCountVerbosity     = VERBOSITY_DEFAULT;

    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES)
    {
        aacCountVerbosity = VERBOSITY_COMMAND_NAMES;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE)
    {
        aacCountVerbosity = VERBOSITY_COMMAND_VERBOSE;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_BUFFERS)
    {
        aacCountVerbosity = VERBOSITY_BUFFERS;
    }

    if (!beginningOfList || !*beginningOfList)
    {
        // No more devices in the list to scan for
        return SUCCESS;
    }

    raidList = *beginningOfList;

    while (raidList)
    {
        bool handleRemoved = false;
        if (raidList->raidHint.adaptecRAID || raidList->raidHint.unknownRAID)
        {
            snprintf(aacHandleStr, OS_AAC_HANDLE_MAX_LENGTH, "%s", raidList->handle);
            printf("Opening %s\n", aacHandleStr);
#    if defined(_WIN32)
            fd = CreateFileA(aacHandleStr,
                             GENERIC_WRITE | GENERIC_READ,       // GENERIC_EXECUTE. GENERIC_ALL
                             FILE_SHARE_READ | FILE_SHARE_WRITE, // FILE_ALL_ACCESS
                             M_NULLPTR, OPEN_EXISTING,
#        if 0 //! defined (WINDOWS_DISABLE_OVERLAPPED)
                    FILE_FLAG_OVERLAPPED,
#        else
                             0,
#        endif
                             M_NULLPTR);
            if (fd != INVALID_HANDLE_VALUE)
#    else  //_WIN32
            if ((fd = open(aacHandleStr, O_RDWR | O_NONBLOCK)) >= 0)
#    endif //_WIN32
#    if defined(AAC_USE_FIBS)
            {
                // first check if these IOCTLs are even supported
                aac_pci_info pciInfo;
                safe_memset(&pciInfo, sizeof(aac_pci_info), 0, sizeof(aac_pci_info));
                if (SUCCESS == send_aac_get_pci_info(fd, &pciInfo, aacCountVerbosity) || true)
                {
                    uint32_t         scsimode = 0;
                    aac_adapter_info ainfo;
                    aac_supplement_adapter_info
                        asupinfo; // check ainfo for flag that this is supported, otherwise it will be empty
                    safe_memset(&ainfo, sizeof(aac_adapter_info), 0, sizeof(aac_adapter_info));
                    safe_memset(&asupinfo, sizeof(aac_supplement_adapter_info), 0, sizeof(aac_supplement_adapter_info));
                    aac_rev_check revcheck;
                    safe_memset(&revcheck, sizeof(aac_rev_check), 0, sizeof(aac_rev_check));
                    revcheck.callingComponent                    = RevApplication;
                    revcheck.callingRevision.external.comp.major = 1;
                    aac_rev_check_resp revresponse;
                    safe_memset(&revresponse, sizeof(aac_rev_check_resp), 0, sizeof(aac_rev_check_resp));
                    send_aac_miniport_rev_check(fd, &revcheck, &revresponse, aacCountVerbosity);
                    if (SUCCESS == aac_Get_Adapter_Info(fd, &ainfo, aacCountVerbosity))
                    {
                        if (ainfo.SupportedOptions & AAC_SUPPORTED_SUPPLEMENT_ADAPTER_INFO)
                        {
                            aac_Get_Adapter_Supplemental_Info(fd, &asupinfo, aacCountVerbosity);
                        }
                        //
                        aac_VM_Container_Get_SCSI_Method(fd, &scsimode, aacCountVerbosity);
                        aac_mntinforesp mountinfo;
                        safe_memset(&mountinfo, sizeof(aac_mntinforesp), 0, sizeof(aac_mntinforesp));
                        uint32_t maxContainers = AAC_MAX_CONTAINERS;
                        if (SUCCESS == aac_VM_Container_Get_Mount_Info(
                                           fd, 0, asupinfo.SupportedOptions2 & AAC_SUPPORTED_VARIABLE_BLOCK_SIZE,
                                           ainfo.SupportedOptions & AAC_SUPPORTED_64BIT_DATA, &mountinfo,
                                           sizeof(aac_mntinforesp), aacCountVerbosity))
                        {
                            // This should tell us how many raids the controller is managing so we can scan each one
                            maxContainers = mountinfo.MntRespCount;
                        }
                        /* changes for Smart HBA */
                        // #define	AAC_MAX_NATIVE_TARGETS		1024
                        /* Thor: 5 phys. buses: #0: empty, 1-4: 256 targets each */
                        // #define AAC_MAX_BUSES			5
                        // #define AAC_MAX_TARGETS			256
                        //
                        for (uint32_t containerIter = UINT32_C(1); containerIter <= maxContainers; ++containerIter)
                        {
                            // need to figure out how many drives are in each container!
                            if (SUCCESS == aac_VM_Container_Get_Mount_Info(
                                               fd, containerIter,
                                               asupinfo.SupportedOptions2 & AAC_SUPPORTED_VARIABLE_BLOCK_SIZE,
                                               ainfo.SupportedOptions & AAC_SUPPORTED_64BIT_DATA, &mountinfo,
                                               sizeof(aac_mntinforesp), aacCountVerbosity))
                            {
                                printf("Container %" PRIu32 "\tCount: %" PRIu32 "\n", containerIter,
                                       mountinfo.MntRespCount);
                            }
                            uint32_t            maxTargets = AAC_MAX_TARGETS;
                            uint32_t            maxBuses   = AAC_SCSI_MAX_PORTS;
                            aac_vmi_businf_resp businfo;
                            safe_memset(&businfo, sizeof(aac_vmi_businf_resp), 0, sizeof(aac_vmi_businf_resp));
                            if (SUCCESS == send_VM_Container_Get_Bus_Info(fd, scsimode, &businfo, aacCountVerbosity))
                            {
                                maxTargets = businfo.BusInf.TargetsPerBus;
                                maxBuses   = businfo.BusInf.BusCount;
                            }
                            // now loop through the valid bus's and try getting drive info. If we get good info, then
                            // count that as a device
                            for (uint32_t busIter = 0; busIter < maxBuses && busIter < AAC_SCSI_MAX_PORTS; ++busIter)
                            {
                                if (businfo.BusInf.BusValid[busIter] == AAC_BUS_VALID)
                                {
                                    uint32_t busID = businfo.BusInf.InitiatorBusId[busIter];
                                    for (uint32_t targetIter = 0; targetIter < maxTargets; ++targetIter)
                                    {
                                        // we also need to check for luns...typically only one lun (zero), but SAS may
                                        // have multiple luns, so we need to check for them. As soon as we fail from
                                        // checking luns, exit the loop.
                                        for (uint32_t lunIter = 0; lunIter < UINT8_MAX; ++lunIter)
                                        {
                                            aac_vmi_devinfo_resp deviceInfo;
                                            if (SUCCESS ==
                                                send_VM_Container_Get_DevInfo(fd, scsimode, busID, targetIter, lunIter,
                                                                              &deviceInfo, aacCountVerbosity))
                                            {
                                                printf("Vendor: %8s\tProduct: %16s\tRev:%4s\n", deviceInfo.VendorId,
                                                       deviceInfo.ProductId, deviceInfo.ProductRev);
                                                // dev info also outputs btl....use this for list/handle generation
                                                ++found;
                                            }
                                            else
                                            {
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        // This MUST complete otherwise something is really wrong
                        ret = FAILURE;
                    }
                }
                else
                {
                    // AAC Raid IOCTLs not supported
                    ret = FAILURE;
                }
            }
#    else  // AAC_USE_FIBS
            {
                // Try issuing test unit ready to each bus-target-lun to get a response and figure out what devices are
                // available
                tDevice   dummydev;
                ScsiIoCtx inquiry;
                DECLARE_ZERO_INIT_ARRAY(uint8_t, tursense, 30);
                DECLARE_ZERO_INIT_ARRAY(uint8_t, stdinq, 96);
                safe_memset(&dummydev, sizeof(tDevice), 0, sizeof(tDevice));
                safe_memset(&inquiry, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
                inquiry.cdbLength        = 6;
                inquiry.device           = &dummydev;
                inquiry.direction        = XFER_DATA_IN;
                inquiry.psense           = &tursense[0];
                inquiry.senseDataSize    = 30;
                inquiry.pdata            = &stdinq[0];
                inquiry.dataLength       = 96;
                dummydev.os_info.fd      = fd;
                dummydev.deviceVerbosity = aacCountVerbosity;
                dummydev.os_info.aacDeviceData =
                    M_REINTERPRET_CAST(ptrAacDeviceInfo, safe_calloc(1, sizeof(aacDeviceInfo)));
                dummydev.os_info.aacDeviceData->maxSGList           = 1;
                dummydev.os_info.aacDeviceData->maxSGTransferLength = 65536;
                // TODO: Figured out better maximums for bus, target, and lun. Currently limiting to 255 since that is
                // reasonable-TJE for (uint16_t busval = 0; busval < UINT8_MAX; ++busval)
                {
                    for (uint16_t targetval = 0; targetval < UINT8_MAX; ++targetval)
                    {
                        for (uint16_t lunval = 0; lunval < UINT8_MAX; ++lunval)
                        {
                            dummydev.os_info.aacDeviceData->bus    = 0; // busval;
                            dummydev.os_info.aacDeviceData->target = targetval;
                            dummydev.os_info.aacDeviceData->lun    = lunval;
                            if (SUCCESS == issue_io_aacraid_Dev(&inquiry))
                            {
                                ++found;
                                printf("Test Unit Ready Success on BTL: %" PRIu16 ":%" PRIu16 ":%" PRIu16 "!\n",
                                       dummydev.os_info.aacDeviceData->bus, dummydev.os_info.aacDeviceData->target,
                                       dummydev.os_info.aacDeviceData->lun);
                            }
                            printf("STD INQ OUT:\n");
                            print_Data_Buffer(stdinq, 96, true);
                        }
                    }
                }
            }
#    endif // AAC_USE_FIBS
            else
            {
                printf("Failed to open %s\n", aacHandleStr);
            }
        }
        if (!handleRemoved)
        {
            previousRaidListEntry =
                raidList; // store handle we just looked at in case we need to remove one from the list
            // increment to next element in the list
            raidList = raidList->next;
        }
    }
    printf("AAC Found = %" PRIu32 "\n", found);
    return ret;
}

eReturnValues get_AAC_RAID_Device_List(tDevice* const       ptrToDeviceList,
                                       uint32_t             sizeInBytes,
                                       versionBlock         ver,
                                       uint64_t             flags,
                                       ptrRaidHandleToScan* beginningOfList)
{
    return NOT_SUPPORTED;
}

#endif // ENABLE_AAC
