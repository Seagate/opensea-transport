// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

#include "ata_helper_func.h"
#include "cmds.h"
#include "nix_mounts.h"
#include "posix_common_lowlevel.h"
#include "scsi_helper_func.h"
#include "usb_hacks.h"
#include "uscsi_helper.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/mnttab.h> //reading mounted partition info
#include <sys/mount.h>  //unmounting disks
#include <sys/scsi/impl/uscsi.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern bool validate_Device_Struct(versionBlock);

// If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise
// you must try MAX_CMD_TIMEOUT_SECONDS instead
OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void)
{
    return false; // Documentation does not state if an infinite timeout is supported. If it actually is, need to define
                  // the infinite timeout value properly, and set it to the correct value
}

/*
Return the device name without the path.
e.g. return c?t?d? from /dev/rdsk/c?t?d?
*/
static void set_Device_Name(const char* filename, char* name, size_t sizeOfName)
{
    const char* s = strrchr(filename, '/') + 1;
    snprintf_err_handle(name, sizeOfName, "%s", s);
}

static M_INLINE void close_mnttab(FILE** mnttab)
{
    M_STATIC_CAST(void, fclose(*mnttab));
    *mnttab = M_NULLPTR;
}

// Man page suggests not transferring more than 16MB.
// This is used to fill in adapter max transfer length by default.
// If the IOCTL for USCSIMAXXFER passes, then we use that value instead
#define MAX_REC_XFER_SIZE_16MB UINT32_C(16777216)

M_PARAM_RW(2)
OPENSEA_TRANSPORT_API eReturnValues get_Device(const char* M_NONNULL filename, tDevice* M_NONNULL device)
{
    eReturnValues ret = SUCCESS;
#if defined(USCSIMAXXFER)
    uscsi_xfer_t maxXfer = 0;
#endif // USCSIMAXXFER
    ePosixHandleFlags handleFlags = POSIX_HANDLE_FLAGS_DEFAULT;

    char* deviceHandle = M_NULLPTR;

    ret = posix_Resolve_Filename_Link(filename, &deviceHandle);
    if (ret != SUCCESS)
    {
        free_Posix_Resolved_Filename(&deviceHandle);
        return ret;
    }

    if (device->dFlags & HANDLE_REQUIRE_EXCLUSIVE_ACCESS)
    {
        handleFlags = POSIX_HANDLE_FLAGS_REQUIRE_EXCLUSIVE;
    }
    else if (device->dFlags & HANDLE_RECOMMEND_EXCLUSIVE_ACCESS)
    {
        handleFlags = POSIX_HANDLE_FLAGS_REQUEST_EXCLUSIVE;
    }

    ret = posix_Get_Device_Handle(deviceHandle, &device->os_info.fd, &handleFlags, 0);
    if (ret != SUCCESS)
    {
        free_Posix_Resolved_Filename(&deviceHandle);
        return ret;
    }
    if (handleFlags == POSIX_HANDLE_FLAGS_DEFAULT)
    {
        set_Device_Handle_Open_Flags(device, HANDLE_FLAGS_DEFAULT);
    }
    else
    {
        set_Device_Handle_Open_Flags(device, HANDLE_FLAGS_EXCLUSIVE);
    }

    device->os_info.osType = OS_SOLARIS;
    set_Device_IO_Minimum_Alignment(device, sizeof(void*));

    device->os_info.adapterMaxTransferSize = MAX_REC_XFER_SIZE_16MB;
#if defined(USCSIMAXXFER)
    if (0 >= ioctl(device->os_info.fd, USCSIMAXXFER, &maxXfer) && maxXfer > 0)
    {
        device->os_info.adapterMaxTransferSize = M_STATIC_CAST(uint32_t, M_Min(maxXfer, UINT32_MAX));
    }
#endif // USCSIMAXXFER

    // Adding support for different device discovery options.
    if (device->dFlags == OPEN_HANDLE_ONLY)
    {
        return ret;
    }

    if ((device->os_info.fd >= 0) && (ret == SUCCESS))
    {
        // set the name
        set_Device_Handle_Name(device, deviceHandle);
        // set the friendly name
        DECLARE_ZERO_INIT_ARRAY(char, friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH);
        set_Device_Name(deviceHandle, friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH);
        set_Device_Handle_Friendly_Name(device, friendlyName);
        // set the block handle
        snprintf_err_handle(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/dsk/%s",
                            get_Device_Handle_Friendly_Name(device));
        // set the partition info
        set_Device_Partition_Info(&device->os_info.fileSystemInfo, device->os_info.secondName);

        // set the OS Type
        device->os_info.osType = OS_SOLARIS;

        // uscsi documentation: http://docs.oracle.com/cd/E23824_01/html/821-1475/uscsi-7i.html
        set_Device_InterfaceType(device, SCSI_INTERFACE);
        set_Device_DriveType(device, SCSI_DRIVE);
        if (get_Device_InterfaceType(device) == USB_INTERFACE ||
            get_Device_InterfaceType(device) == IEEE_1394_INTERFACE)
        {
            // TODO: Actually get the VID and PID set before calling this.
            setup_Passthrough_Hacks_By_ID(device);
        }
        // fill in the device info
        ret = fill_Drive_Info_Data(device);
    }
    free_Posix_Resolved_Filename(&deviceHandle);

    return ret;
}

M_FILE_DESCRIPTOR(1)
static eReturnValues uscsi_Reset(int fd, int resetFlag)
{
    struct uscsi_cmd uscsi_io;
    eReturnValues    ret = SUCCESS;

    safe_memset(&uscsi_io, sizeof(uscsi_io), 0, sizeof(uscsi_io));

    uscsi_io.uscsi_flags |= resetFlag;
    int ioctlResult = ioctl(fd, USCSICMD, &uscsi_io);
    if (ioctlResult < 0)
    {
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    else
    {
        ret = SUCCESS;
    }
    return ret;
}

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Device_Reset(const tDevice* M_NONNULL device)
{
    // NOTE: USCSI_RESET is the same thing, but for legacy versions
    // Also USCSI_RESET_LUN is available. Maybe it would be better?
    return uscsi_Reset(device->os_info.fd, USCSI_RESET_TARGET);
}

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Bus_Reset(const tDevice* M_NONNULL device)
{
    // USCSI_RESET_ALL seems to imply a bus reset
    return uscsi_Reset(device->os_info.fd, USCSI_RESET_ALL);
}

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Controller_Reset(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

M_PARAM_RO(1) eReturnValues send_IO(ScsiIoCtx* M_NONNULL scsiIoCtx)
{
    eReturnValues ret = FAILURE;
    switch (get_Device_InterfaceType(scsiIoCtx->device))
    {
    case SCSI_INTERFACE:
    case IDE_INTERFACE:
    case USB_INTERFACE:
    case IEEE_1394_INTERFACE:
        ret = send_uscsi_io(scsiIoCtx);
        break;
    case RAID_INTERFACE:
        if (scsiIoCtx->device->issue_io != M_NULLPTR)
        {
            ret = scsiIoCtx->device->issue_io(scsiIoCtx);
        }
        else
        {
            print_tDevice_Verbose_String(scsiIoCtx->device, VERBOSITY_QUIET, "No Raid PassThrough IO Routine present for this device\n");
        }
        break;
    case NVME_INTERFACE:
        // haven't defined a way to send NVME commands yet. Need to add this in later...-TJE
        ret = send_uscsi_io(scsiIoCtx);
        break;
    default:
        print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_QUIET, "Target Device does not have a valid interface %d\n", get_Device_InterfaceType(scsiIoCtx->device));
    }

    if (scsiIoCtx->device->delay_io)
    {
        delay_Milliseconds(scsiIoCtx->device->delay_io);
        print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_COMMAND_NAMES, "Delaying between commands %d seconds to reduce IO impact\n", scsiIoCtx->device->delay_io);
    }

    return ret;
}

M_PARAM_RO(1) eReturnValues send_uscsi_io(ScsiIoCtx* M_NONNULL scsiIoCtx)
{
    // http://docs.oracle.com/cd/E23824_01/html/821-1475/uscsi-7i.html
    struct uscsi_cmd uscsi_io;
    eReturnValues    ret = SUCCESS;

    safe_memset(&uscsi_io, sizeof(uscsi_io), 0, sizeof(uscsi_io));
    print_tDevice_Verbose_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "Sending command with send_IO\n");

    if (scsiIoCtx->timeout > USCSI_MAX_CMD_TIMEOUT_SECONDS ||
        get_tDevice_Default_Command_Timeout(scsiIoCtx->device) > USCSI_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }

    uscsi_io.uscsi_timeout       = scsiIoCtx->timeout;
    const uint32_t deviceTimeout = get_tDevice_Default_Command_Timeout(scsiIoCtx->device);
    if (deviceTimeout > UINT32_C(0) && deviceTimeout > scsiIoCtx->timeout)
    {
        uscsi_io.uscsi_timeout = deviceTimeout;
    }
    else
    {
        uscsi_io.uscsi_timeout = scsiIoCtx->timeout;
        if (scsiIoCtx->timeout == UINT32_C(0))
        {
            uscsi_io.uscsi_timeout = DEFAULT_COMMAND_TIMEOUT; // default to 15 second timeout
        }
    }
    uscsi_io.uscsi_cdb     = C_CAST(caddr_t, scsiIoCtx->cdb);
    uscsi_io.uscsi_cdblen  = scsiIoCtx->cdbLength;
    uscsi_io.uscsi_rqbuf   = C_CAST(caddr_t, scsiIoCtx->psense);
    uscsi_io.uscsi_rqlen   = scsiIoCtx->senseDataSize;
    uscsi_io.uscsi_bufaddr = C_CAST(caddr_t, scsiIoCtx->pdata);
    uscsi_io.uscsi_buflen  = scsiIoCtx->dataLength;

    // set the uscsi flags for the command
    uscsi_io.uscsi_flags = USCSI_ISOLATE | USCSI_RQENABLE; // base flags
    switch (scsiIoCtx->direction)
    {
    case XFER_NO_DATA:
        break;
        // NOLINTBEGIN(bugprone-branch-clone)
    case XFER_DATA_IN:
        uscsi_io.uscsi_flags |= USCSI_READ;
        break;
    case XFER_DATA_OUT:
        uscsi_io.uscsi_flags |= USCSI_WRITE;
        break;
        // NOLINTEND(bugprone-branch-clone)
    default:
        print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_QUIET, "%s Didn't understand direction\n", __func__);
        return BAD_PARAMETER;
    }

    // \revisit: should this be FF or something invalid than 0?
    scsiIoCtx->returnStatus.format   = UINT8_C(0xFF);
    scsiIoCtx->returnStatus.senseKey = UINT8_C(0);
    scsiIoCtx->returnStatus.asc      = UINT8_C(0);
    scsiIoCtx->returnStatus.ascq     = UINT8_C(0);

    DECLARE_SEATIMER(commandTimer);

    // issue the io
    start_Timer(&commandTimer);
    int ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, USCSICMD, &uscsi_io);
    stop_Timer(&commandTimer);
    if (ioctlResult < 0)
    {
        ret = FAILURE;
        errno_t error = M_STATIC_CAST(errno_t, get_Device_OS_Info_Last_Error(scsiIoCtx->device));
        if (error != 0)
        {
            char* errormsg = get_strerror(error);
            if (errormsg != M_NULLPTR)
            {
                print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "send_IO error: %d - %s\n", error, errormsg);
                safe_free(&errormsg);
            }
        }
    }

    print_tDevice_Verbose_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "USCSI Results\n");
    print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "\tSCSI Status: %hu\n", uscsi_io.uscsi_status);
    print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "\tResid: %zu\n", uscsi_io.uscsi_resid);
    print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "\tRQS SCSI Status: %hu\n", uscsi_io.uscsi_rqstatus);
    print_tDevice_Verbose_Formatted_String(scsiIoCtx->device, VERBOSITY_BUFFERS, "\tRQS Resid: %hhu\n", uscsi_io.uscsi_rqresid);

    set_tDevice_Last_Command_Completion_Time_NS(scsiIoCtx->device, get_Nano_Seconds(commandTimer));
    return ret;
}

static int uscsi_filter(const struct dirent* entry)
{
    // in this folder everything will start with a c.
    int uscsiHandle = strncmp("c", entry->d_name, 1);
    if (uscsiHandle != 0)
    {
        return !uscsiHandle;
    }
    // now, we need to filter out the device names that have "p"s for the partitions and "s"s for the slices
    const char* partitionOrSlice = strpbrk(entry->d_name, "pPsS");
    if (partitionOrSlice != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !uscsiHandle;
    }
}

M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues close_Device(tDevice* M_NONNULL device)
{
    int retValue = 0;
    if (device)
    {
        retValue = close(device->os_info.fd);
        set_Device_Last_Error(device, errno);
        if (retValue == 0)
        {
            device->os_info.fd = -1;
            return SUCCESS;
        }
        else
        {
            return FAILURE;
        }
    }
    else
    {
        return MEMORY_FAILURE;
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
M_PARAM_RW(1)
OPENSEA_TRANSPORT_API eReturnValues get_Device_Count(uint32_t* M_NONNULL numberOfDevices, uint64_t flags)
{
    int num_devs = 0;

    struct dirent** namelist;
    num_devs = scandir("/dev/rdsk", &namelist, uscsi_filter, alphasort);
    for (int iter = 0; iter < num_devs; ++iter)
    {
        safe_free_dirent(&namelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));
    if (num_devs >= 0)
    {
        *numberOfDevices = C_CAST(uint32_t, num_devs);
    }
    M_USE_UNUSED(flags);
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
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
#define USCSI_NAME_LEN 80
M_PARAM_RW(1)
OPENSEA_TRANSPORT_API eReturnValues get_Device_List(tDevice* M_NONNULL const ptrToDeviceList,
                                                    uint32_t                 sizeInBytes,
                                                    versionBlock             ver,
                                                    M_ATTR_UNUSED uint64_t   flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      totalDevs             = UINT32_C(0);
    uint32_t      num_rdsk              = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    uint32_t      busyDevCount          = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, name, USCSI_NAME_LEN); // Because get device needs char
    int      fd = -1;
    tDevice* d  = M_NULLPTR;

    struct dirent** namelist;

    eVerbosityLevels listVerbosity = VERBOSITY_DEFAULT;
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES)
    {
        listVerbosity = VERBOSITY_COMMAND_NAMES;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE)
    {
        listVerbosity = VERBOSITY_COMMAND_VERBOSE;
    }
    if (flags & GET_DEVICE_FUNCS_VERBOSE_BUFFERS)
    {
        listVerbosity = VERBOSITY_BUFFERS;
    }

    int scandirres = scandir("/dev/rdsk", &namelist, uscsi_filter, alphasort);
    if (scandirres > 0)
    {
        num_rdsk = C_CAST(uint32_t, scandirres);
    }

    char**   devs = M_REINTERPRET_CAST(char**, safe_calloc(num_rdsk + 1, sizeof(char*)));
    uint32_t i    = UINT32_C(0);
    for (; i < num_rdsk; i++)
    {
        size_t handleSize = (safe_strlen("/dev/rdsk/") + safe_strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i]           = M_REINTERPRET_CAST(char*, safe_malloc(handleSize));
        snprintf_err_handle(devs[i], handleSize, "/dev/rdsk/%s", namelist[i]->d_name);
        safe_free_dirent(&namelist[i]);
    }
    devs[i] = M_NULLPTR;
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));

    totalDevs = num_rdsk;

    if (ptrToDeviceList == M_NULLPTR || sizeInBytes == UINT32_C(0))
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
        for (driveNumber = UINT32_C(0);
             ((driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < totalDevs) && (found < numberOfDevices));
             ++driveNumber)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, USCSI_NAME_LEN, 0, USCSI_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, USCSI_NAME_LEN, "%s", devs[driveNumber]);
            fd = -1;
            // lets try to open the device.
            fd = open(name, O_RDWR | O_NONBLOCK);
            if (fd >= 0)
            {
                close(fd);
                eVerbosityLevels temp = d->deviceVerbosity;
                safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size     = ver.size;
                d->sanity.version  = ver.version;
                eReturnValues ret  = get_Device(name, d);
                if (ret != SUCCESS)
                {
                    ++failedGetDeviceCount;
                }
                ++found;
                ++d;
            }
            else
            {
                if (VERBOSITY_COMMAND_NAMES <= listVerbosity)
                {
                    print_str("Failed open, reason: ");
                    print_Errno_To_Screen(errno);
                }
                ++failedGetDeviceCount;
                switch (errno)
                {
                case EACCES:
                    ++permissionDeniedCount;
                    break;
                case EBUSY:
                    ++busyDevCount;
                    break;
                default:
                    break;
                }
            }
            // free the dev[deviceNumber] since we are done with it now.
            safe_free(&devs[driveNumber]);
        }
        if (permissionDeniedCount == totalDevs)
        {
            returnValue = PERMISSION_DENIED;
        }
        else if (busyDevCount == totalDevs)
        {
            returnValue = DEVICE_BUSY;
        }
        else if (failedGetDeviceCount == totalDevs)
        {
            returnValue = FAILURE;
        }
        else if (failedGetDeviceCount > 0)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }

    safe_free(M_REINTERPRET_CAST(void**, &devs));
    if (VERBOSITY_COMMAND_NAMES <= listVerbosity)
    {
        printf("Get device list returning %d\n", returnValue);
    }
    return returnValue;
}

OPENSEA_TRANSPORT_API eReturnValues os_Read(M_ATTR_UNUSED const tDevice* M_NONNULL device,
                                            M_ATTR_UNUSED uint64_t                 lba,
                                            M_ATTR_UNUSED bool                     forceUnitAccess,
                                            M_ATTR_UNUSED uint8_t* M_NONNULL       ptrData,
                                            M_ATTR_UNUSED uint32_t                 dataSize)
{
    return NOT_SUPPORTED;
}

OPENSEA_TRANSPORT_API eReturnValues os_Write(M_ATTR_UNUSED const tDevice* M_NONNULL device,
                                             M_ATTR_UNUSED uint64_t                 lba,
                                             M_ATTR_UNUSED bool                     forceUnitAccess,
                                             M_ATTR_UNUSED uint8_t* M_NONNULL       ptrData,
                                             M_ATTR_UNUSED uint32_t                 dataSize)
{
    return NOT_SUPPORTED;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues os_Verify(M_ATTR_UNUSED const tDevice* M_NONNULL device,
                                              M_ATTR_UNUSED uint64_t                 lba,
                                              M_ATTR_UNUSED uint32_t                 range)
{
    return NOT_SUPPORTED;
}

M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues os_Flush(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return NOT_SUPPORTED;
}

M_PARAM_RW(1) eReturnValues send_NVMe_IO(M_ATTR_UNUSED nvmeCmdCtx* M_NONNULL nvmeIoCtx)
{
    return NOT_SUPPORTED;
}

M_PARAM_WO_SIZE(2, 3)
eReturnValues pci_Read_Bar_Reg(M_ATTR_UNUSED const tDevice* M_NONNULL device,
                               M_ATTR_UNUSED uint8_t* M_NONNULL       pData,
                               M_ATTR_UNUSED uint32_t                 dataSize)
{
    return NOT_SUPPORTED;
}

M_PARAM_RO(1) eReturnValues os_nvme_Reset(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return NOT_SUPPORTED;
}

M_PARAM_RO(1) eReturnValues os_nvme_Subsystem_Reset(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return NOT_SUPPORTED;
}

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Get_Exclusive(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

#define DRIVE_HANDLE_LOCK_RANGE_START  (0)
#define DRIVE_HANDLE_LOCK_RANGE_LENGTH (0) // 0 means full drive/file
OPENSEA_TRANSPORT_API M_PARAM_RW(1) eReturnValues os_Lock_Device(const tDevice* M_NONNULL device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.lockCount == UINT16_C(1))
    {
        struct flock locks;
        safe_memset(&locks, sizeof(struct flock), 0, sizeof(struct flock));
        locks.l_type   = F_WRLCK;
        locks.l_whence = SEEK_SET;
        locks.l_start  = DRIVE_HANDLE_LOCK_RANGE_START;
        locks.l_len    = DRIVE_HANDLE_LOCK_RANGE_LENGTH;
        if (fcntl(device->os_info.fd, F_SETLK, &locks) < 0)
        {
            set_Device_Last_Error(M_CONST_CAST(tDevice*, device), errno);
            errno_t error = M_STATIC_CAST(errno_t, get_Device_OS_Info_Last_Error(device));
            if (error != 0)
            {
                char* errormsg = get_strerror(error);
                if (errormsg != M_NULLPTR)
                {
                    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Failed to set POSIX F_SETLK %s flags with fcntl: %d - %s\n", "lock", error, errormsg);
                    safe_free(&errormsg);
                }
            }
            ret = FAILURE;
        }
    }
    if (ret == SUCCESS && device->os_info.lockCount < UINT16_MAX)
    {
        // Always increment this so we know how many times we've been requested to lock
        ++M_CONST_CAST(tDevice*, device)->os_info.lockCount;
    }
    return ret;
}

OPENSEA_TRANSPORT_API M_PARAM_RW(1) eReturnValues os_Unlock_Device(const tDevice* M_NONNULL device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.lockCount == UINT16_C(1))
    {
        struct flock locks;
        safe_memset(&locks, sizeof(struct flock), 0, sizeof(struct flock));
        locks.l_type   = F_UNLCK;
        locks.l_whence = SEEK_SET;
        locks.l_start  = DRIVE_HANDLE_LOCK_RANGE_START;
        locks.l_len    = DRIVE_HANDLE_LOCK_RANGE_LENGTH;
        if (fcntl(device->os_info.fd, F_SETLK, &locks) < 0)
        {
            set_Device_Last_Error(M_CONST_CAST(tDevice*, device), errno);
            errno_t error = M_STATIC_CAST(errno_t, get_Device_OS_Info_Last_Error(device));
            if (error != 0)
            {
                char* errormsg = get_strerror(error);
                if (errormsg != M_NULLPTR)
                {
                    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "Failed to set POSIX F_SETLK %s flags with fcntl: %d - %s\n", "unlock", error, errormsg);
                    safe_free(&errormsg);
                }
            }
            ret = FAILURE;
        }
    }
    if (ret == SUCCESS && device->os_info.lockCount > 0)
    {
        --M_CONST_CAST(tDevice*, device)->os_info.lockCount;
    }
    return ret;
}

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues
    os_Update_File_System_Cache(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return NOT_SUPPORTED;
}

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return NOT_SUPPORTED;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues os_Unmount_File_Systems_On_Device(const tDevice* M_NONNULL device)
{
    return unmount_Partitions_From_Device(device->os_info.secondHandleValid ? device->os_info.secondName
                                                                            : get_Device_Handle_Name(device));
}
