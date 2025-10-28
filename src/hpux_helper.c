// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
//! \file based on https://www.unix.com/man_page/hpux/7/scsi_ctl/
//!       and https://docstore.mik.ua/manuals/hp-ux/en/B2355-60130/scsi_ctl.7.html
//! https://support.hpe.com/hpesc/public/docDisplay?docId=c01922540&docLocale=en_US

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
#include "nvme_helper_func.h"
#include "sat_helper_func.h"
#include "scsi_helper_func.h"
#include "sntl_helper.h"
#include "usb_hacks.h"
#include <dirent.h>
#include <libgen.h>
#include <mntent.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/scsi.h>
#include <sys/scsi_ctl.h>
//
// #include <sys/pscsi.h>
#include <sys/diskio.h>

// HPE Manual suggests SIOC_INQUIRY for standard inquiry data because it also updates the driver when this is done.
// Probably something to put in some kind of rescan type function - TJE

// device scan handles:
// persistent: /dev/disk/diskX
// legacy: /dev/dsk/c0t6d6
// legacy: for passthrough use rdsk instead as dsk is block driver.

// Device File Naming Conventions
// Standard disk device files are named according to the following conventions (see intro(7)):
// Block-mode Devices /dev/disk/diskN[_pX]
// Character-mode Devices /dev/disk/diskN[_pX]
// Legacy block-mode Devices /dev/dsk/cxtydn[sm]
// Legacy character-mode Devices /dev/rdsk/cxtydn[sm]
// Legacy device special filenames are those used on HP-UX 11i Version 2 and earlier releases. They can still
// be used for backward compatibility, but only for part of the configuration within the limits of HP-UX 11i
// Version 2.
// The component parts of the device filename are constructed as follows:
// N Required. A decimal number corresponding to the instance number assigned to the direct access
// device by the operating system.
// X Required if _p is specified. A decimal number corresponding to a partition number.
// c Required. Identifies the following hexadecimal digits as the "Instance" of the interface card.
// x Hexadecimal number identifying controlling bus interface, also known as the "Instance" of this
// interface card. The instance value is displayed in the ioscan(1M) output, column "I" for the H/W
// Type, "INTERFACE".
// Required.
// t Identifies the following hexadecimal digits as a "drive number" or "target".
// Required.
// y Hexadecimal number identifying the drive or target number (bus address).
// Required.
// d Identifies the following hexadecimal digits as a "unit number".
// Required.
// n Hexadecimal unit number within the device.
// Required.
// s Optional. Defaults to that corresponding to whole disk. Identifies the following value as a "section number".
// m Required if s is specified. Defaults to section 0 (zero), whole disk. Drive section number.
// Assignment of controller, drive, l

// NOTE: can enumerate other SCSI device types as needed. ex: /dev/rtape or /dev/rmt

extern bool validate_Device_Struct(versionBlock);

bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

static M_INLINE void close_mnttab(FILE** mnttab)
{
    M_STATIC_CAST(void, fclose(*mnttab));
    *mnttab = M_NULLPTR;
}

// This API is very similar to the linux API.
// TODO: May need to use handle without rdsk in the name
//       We use the rdsk for issuing passthrough commands, but the other handle is probably what will really be needed
//       to do this correctly I do not have a solaris machine or VM available to test this right now, so it's written as
//       best I could to prevent needing changes. - TJE
#define HPUX_MNT_ENT_BUF_SIZE 1025
#if !defined(MNT_MNTTAB)
#    define MNT_MNTTAB "/etc/mnttab"
#endif // MNT_MNTTAB
static int get_Partition_Count(const char* blockDeviceName)
{
    int   result = 0;
    FILE* mount  = M_NULLPTR;
    // we only need to know about mounted partitions. Mounted partitions
    // need to be known so that they can be unmounted when necessary. - TJE
    errno_t fileopenerr = safe_fopen(&mount, MNT_MNTTAB, "r");
    if (fileopenerr == 0 && mount)
    {
        DECLARE_ZERO_INIT_ARRAY(char, mntentbuf, HPUX_MNT_ENT_BUF_SIZE);
        struct mntent entry;
        safe_memset(&entry, sizeof(struct mntent), 0, sizeof(struct mntent));
        // NOTE: If EOF is returned, buffer is too small. We can switch to dynamically allocated memory and resize to
        // handle this if we run into issues.
        while (0 == getmntent_r(mount, &entry, mntentbuf, HPUX_MNT_ENT_BUF_SIZE))
        {
            if (strstr(entry.mnt_fsname, blockDeviceName))
            {
                // Found a match, increment result counter.
                ++result;
            }
        }
        close_mnttab(&mount);
    }
    else
    {
        result = -1; // indicate an error
    }
    return result;
}

#define PART_INFO_NAME_LENGTH (32)
#define PART_INFO_PATH_LENGTH (64)
typedef struct s_spartitionInfo
{
    char fsName[PART_INFO_NAME_LENGTH];
    char mntPath[PART_INFO_PATH_LENGTH];
} spartitionInfo, *ptrsPartitionInfo;

static M_INLINE void safe_free_spartition_info(spartitionInfo** partinfo)
{
    safe_free_core(M_REINTERPRET_CAST(void**, partinfo));
}

// partitionInfoList is a pointer to the beginning of the list
// listCount is the number of these structures, which should be returned by get_Partition_Count
static eReturnValues get_Partition_List(const char* blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount)
{
    eReturnValues result       = SUCCESS;
    int           matchesFound = 0;
    if (listCount > 0)
    {
        // Need setmntent instead? Not sure if it does anything differently...
        FILE* mount = M_NULLPTR;
        // we only need to know about mounted partitions. Mounted partitions
        // need to be known so that they can be unmounted when necessary. - TJE
        errno_t fileopenerr = safe_fopen(&mount, MNT_MNTTAB, "r");
        if (fileopenerr == 0 && mount)
        {
            DECLARE_ZERO_INIT_ARRAY(char, mntentbuf, HPUX_MNT_ENT_BUF_SIZE);
            struct mntent entry;
            safe_memset(&entry, sizeof(struct mntent), 0, sizeof(struct mntent));
            while (0 == getmntent_r(mount, &entry, mntentbuf, HPUX_MNT_ENT_BUF_SIZE))
            {
                if (strstr(entry.mnt_fsname, blockDeviceName))
                {
                    // found a match, copy it to the list
                    if (matchesFound < listCount)
                    {
                        snprintf_err_handle((partitionInfoList + matchesFound)->fsName, PART_INFO_NAME_LENGTH, "%s",
                                            entry.mnt_fsname);
                        snprintf_err_handle((partitionInfoList + matchesFound)->mntPath, PART_INFO_PATH_LENGTH, "%s",
                                            entry.mnt_dir);
                        ++matchesFound;
                    }
                    else
                    {
                        result = MEMORY_FAILURE; // out of memory to copy all results to the list.
                    }
                }
            }
            // Need endmntent() instead???
            close_mnttab(&mount);
        }
        else
        {
            result = FAILURE;
        }
    }
    return result;
}

M_NONNULL_PARAM_LIST(1)
M_PARAM_RW(1)
static eReturnValues set_Device_Partition_Info(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    DECLARE_ZERO_INIT_ARRAY(char, blockHandle, OS_HANDLE_NAME_MAX_LENGTH);
    if (device->os_info.persistentDev)
    {
        snprintf_err_handle(blockHandle, OS_HANDLE_NAME_MAX_LENGTH, "%s", device->os_info.name);
    }
    else
    {
        // convert from rdsk to dsk using friendly name
        snprintf_err_handle(blockHandle, OS_HANDLE_NAME_MAX_LENGTH, "/dev/dsk/%s", device->os_info.friendlyName);
    }
    partitionCount = get_Partition_Count(blockHandle);
#if defined(_DEBUG)
    printf("Partition count for %s = %d\n", blockHandle, partitionCount);
#endif
    if (partitionCount > 0)
    {
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;
        device->os_info.fileSystemInfo.hasActiveFileSystem = false;
        device->os_info.fileSystemInfo.isSystemDisk        = false;
        ptrsPartitionInfo parts =
            M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts)
        {
            if (SUCCESS == get_Partition_List(blockHandle, parts, partitionCount))
            {
                int iter = 0;
                for (; iter < partitionCount; ++iter)
                {
                    // since we found a partition, set the "has file system" bool to true
                    device->os_info.fileSystemInfo.hasActiveFileSystem = true;
#if defined(_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    // check if one of the partitions is /boot and mark the system disk when this is found
                    // TODO: Should / be treated as a system disk too?
                    if (strncmp((parts + iter)->mntPath, "/boot", 5) == 0)
                    {
                        device->os_info.fileSystemInfo.isSystemDisk = true;
#if defined(_DEBUG)
                        print_str("found system disk\n");
#endif
                    }
                }
            }
            safe_free_spartition_info(&parts);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
    {
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;
        device->os_info.fileSystemInfo.hasActiveFileSystem = false;
        device->os_info.fileSystemInfo.isSystemDisk        = false;
    }
    return ret;
}

// NOTE: need to cast output to type for each ioctl.
static uint32_t get_SIOC_Timeout_Value(ScsiIoCtx* scsiIoCtx, uint32_t ioctlMaxTimeout)
{
    uint32_t timeout = UINT32_C(0);
    // set timeout
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > UINT32_C(0) &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
        // that we DON'T do a conversion and leave the time as the max...
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < ioctlMaxTimeout)
        {
            timeout *= UINT32_C(1000); // convert to milliseconds
        }
        else
        {
            timeout = UINT32_C(0); // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->timeout != UINT32_C(0))
        {
            timeout = scsiIoCtx->timeout;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
            // security) that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->timeout < ioctlMaxTimeout)
            {
                timeout *= UINT32_C(1000); // convert to milliseconds
            }
            else
            {
                timeout = UINT32_C(0); // no timeout or maximum timeout
            }
        }
        else
        {
            timeout = DEFAULT_COMMAND_TIMEOUT * UINT32_C(1000); // default to 15 second timeout
        }
    }
    return timeout;
}

static eReturnValues handle_SCSI_IO_Completion(ScsiIoCtx* scsiIoCtx,
                                               uint32_t   cdb_status,
                                               uint32_t   sense_status,
                                               uint8_t*   senseBuf,
                                               uint32_t   sense_xfer)
{
    // Between the different IOCTLs, the reported outputs are very very similar.
    // So this is a common function to handle either case to reduce code duplication.
    // This takes certain completion fields to handle the two
    eReturnValues ret                   = SUCCESS;
    bool          checkAndCopySenseData = true;
    // Clear sense data to make sure nothing is stale in this location.
    safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
    switch (cdb_status)
    {
    case S_GOOD:
        checkAndCopySenseData = false;
        break;
    case S_CHECK_CONDITION:
        if (sense_status != S_GOOD)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        break;
    case S_BUSY:
    case S_CONDITION_MET:
    case S_RESV_CONFLICT:
    case S_COMMAND_TERMINATED:
    case S_QUEUE_FULL:
        ret = FAILURE;
        break;
    case SCTL_INVALID_REQUEST:
        checkAndCopySenseData = false; // this is a software screen for the command so no sense is there
        ret                   = OS_COMMAND_BLOCKED;
        break;
    case SCTL_SELECT_TIMEOUT:
        checkAndCopySenseData = false; // this is a software screen for the command so no sense is there
        ret                   = TIMEOUT;
        break;
    case S_INTERMEDIATE:
    case S_I_CONDITION_MET:
        // fallthrough to default since these should not be returned in HPUX
    default:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    }
    if (checkAndCopySenseData)
    {
        if (sense_status == S_GOOD && sense_xfer > 0)
        {
            safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, senseBuf,
                        M_Min(sense_xfer, scsiIoCtx->senseDataSize));
        }
    }
    return ret;
}

eReturnValues send_SIOC_IO(ScsiIoCtx* scsiIoCtx)
{
    /* Structure for SIOC_IO ioctl */
    // struct sctl_io
    // {
    //     unsigned      flags;
    //     unsigned char cdb_length;
    //     unsigned char cdb[16];
    //     void*         data;
    //     unsigned      data_length;
    //     unsigned      max_msecs;
    //     unsigned      data_xfer; //amount of data that was transferred
    //     unsigned      cdb_status;
    //     unsigned char sense[256];
    //     unsigned      sense_status;
    //     unsigned char sense_xfer;
    //     unsigned char reserved[64];
    // } sctl_io_t;
    eReturnValues ret = SUCCESS;
    DECLARE_SEATIMER(commandTimer);

    struct sctl_io scsicmd;
    safe_memset(&scsicmd, sizeof(struct sctl_io), 0, sizeof(struct sctl_io));

    scsicmd.cdb_length = scsiIoCtx->cdbLength;
    safe_memcpy(&scsicmd.cdb[0], 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

    scsicmd.data        = scsiIoCtx->pdata;
    scsicmd.data_length = scsiIoCtx->dataLength;

    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        scsicmd.flags |= SCTL_READ;
        break;
    case XFER_DATA_OUT:
    case XFER_NO_DATA:
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return OS_COMMAND_NOT_AVAILABLE;
    }

    scsicmd.max_msecs = M_STATIC_CAST(unsigned, get_SIOC_Timeout_Value(scsiIoCtx, UINT_MAX));

    // Other flags necessary???

    start_Timer(&commandTimer);
    int iocres = ioctl(scsiIoCtx->device->os_info.fd, SIOC_IO, &scsicmd);
    stop_Timer(&commandTimer);

    if (iocres < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        perror("error sending SCSI I/O");
    }
    else
    {
        ret = handle_SCSI_IO_Completion(scsiIoCtx, scsicmd.cdb_status, scsicmd.sense_status, scsicmd.sense,
                                        scsi_cmd.sense_xfer);
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

#if defined(SIOC_IO_EXT)
// HP-UX 11i V3 and up. All others should use the other IOCTL which only is supported for legacy devices
eReturnValues send_SIOC_IO_EXT(ScsiIoCtx* scsiIoCtx)
{
    /* Structure for SIOC_IO_EXT ioctl */
    // typedef struct
    // {
    //     int                   version;
    //     escsi_sctl_io_flags_t flags;
    //     int                   max_msecs;
    //     uint32_t              cdb_length;
    //     uint32_t              data_length;
    //     ptr64_t               data;
    //     union sense_data      sense;
    //     escsi_hw_path_t       lpt_hwp;
    //     uint32_t              data_xfer;
    //     uint32_t              sense_xfer;
    //     uint32_t              cdb_status;
    //     uint32_t              sense_status;
    //     uint8_t               cdb[ESCSI_MAX_CDB_LEN];
    //     uint32_t              rsvd[32]; /* Reserved for
    //                                      * future use
    //                                      */
    // } esctl_io_t;
    eReturnValues ret = SUCCESS;
    DECLARE_SEATIMER(commandTimer);

    esctl_io_t scsicmd;
    safe_memset(&scsicmd, sizeof(esctl_io_t), 0, sizeof(esctl_io_t));

    scsicmd.cdb_length = scsiIoCtx->cdbLength;
    safe_memcpy(&scsicmd.cdb[0], ESCSI_MAX_CDB_LEN, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

    scsicmd.data        = scsiIoCtx->pdata;
    scsicmd.data_length = scsiIoCtx->dataLength;

    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        scsicmd.flags |= SCTL_READ;
        break;
    case XFER_DATA_OUT:
    case XFER_NO_DATA:
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return OS_COMMAND_NOT_AVAILABLE;
    }

    scsicmd.max_msecs = M_STATIC_CAST(int, get_SIOC_Timeout_Value(scsiIoCtx, INT_MAX));

    start_Timer(&commandTimer);
    int iocres = ioctl(scsiIoCtx->device->os_info.fd, SIOC_IO_EXT, &scsicmd);
    stop_Timer(&commandTimer);

    if (iocres < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        perror("error sending SCSI I/O");
    }
    else
    {
        ret = handle_SCSI_IO_Completion(scsiIoCtx, scsicmd.cdb_status, scsicmd.sense_status,
                                        M_REINTERPRET_CAST(uint8_t*, &scsicmd.sense), scsi_cmd.sense_xfer);
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}
#endif // SIOC_IO_EXT

eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
#if defined(SIOC_IO_EXT)
    // check if legacy device type or persistent to select IOCTL
    if (scsiIoCtx->device->os_info.persistentDev)
    {
        ret = send_SIOC_IO_EXT(scsiIoCtx);
    }
    else
#endif
    {
        ret = send_SIOC_IO(scsiIoCtx);
    }
    return ret;
}

eReturnValues send_NVMe_IO(M_ATTR_UNUSED nvmeCmdCtx* nvmeIoCtx)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Reset(M_ATTR_UNUSED const tDevice* device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Subsystem_Reset(M_ATTR_UNUSED const tDevice* device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues pci_Read_Bar_Reg(M_ATTR_UNUSED const tDevice* device,
                               M_ATTR_UNUSED uint8_t*       pData,
                               M_ATTR_UNUSED uint32_t       dataSize)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

// Might need PSIOC_RESET_DEV instead
eReturnValues os_Device_Reset(const tDevice* device)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    if (ioctl(device->os_info.fd, SIOC_RESET_DEV) < 0)
    {
        set_Device_Last_Error(M_CONST_CAST(tDevice*, device), errno);
    }
    else
    {
        ret = SUCCESS;
    }
    return ret;
}

eReturnValues os_Bus_Reset(const tDevice* device)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    if (ioctl(device->os_info.fd, SIOC_RESET_BUS) < 0)
    {
        set_Device_Last_Error(M_CONST_CAST(tDevice*, device), errno);
    }
    else
    {
        ret = SUCCESS;
    }
    return ret;
}

eReturnValues os_Controller_Reset(M_ATTR_UNUSED const tDevice* device)
{
    // DIOC_RST_CLR?
    // or PDIOC_RSTCLR?
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Read(M_ATTR_UNUSED const tDevice* device,
                      M_ATTR_UNUSED uint64_t       lba,
                      M_ATTR_UNUSED bool           forceUnitAccess,
                      M_ATTR_UNUSED uint8_t*       ptrData,
                      M_ATTR_UNUSED uint32_t       dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Write(M_ATTR_UNUSED const tDevice* device,
                       M_ATTR_UNUSED uint64_t       lba,
                       M_ATTR_UNUSED bool           forceUnitAccess,
                       M_ATTR_UNUSED uint8_t*       ptrData,
                       M_ATTR_UNUSED uint32_t       dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Verify(M_ATTR_UNUSED const tDevice* device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Flush(M_ATTR_UNUSED const tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED const tDevice* device)
{
    return NOT_SUPPORTED;
}

static bool is_Persistent_Disk_Handle(const char* filename)
{
    bool isPersistent = false;
    if (0 == strncmp("/dev/disk/disk", filename, 14))
    {
        isPersistent = true;
    }
    return isPersistent;
}

static void set_Device_Name(const char* filename, char* name, size_t sizeOfName)
{
    char* s = strrchr(filename, '/') + 1;
    snprintf_err_handle(name, sizeOfName, "%s", s);
}

eReturnValues get_Device(const char* filename, tDevice* device)
{
    eReturnValues ret         = SUCCESS;
    int           handleFlags = O_RDWR | O_NONBLOCK;
    int           attempts    = 0;
#define HPUX_OPEN_ATTEMPTS_MAX 2
    if (device->dFlags & HANDLE_RECOMMEND_EXCLUSIVE_ACCESS || device->dFlags & HANDLE_REQUIRE_EXCLUSIVE_ACCESS)
    {
        handleFlags |= O_EXCL;
    }
    do
    {
        ++attempts;
        if ((device->os_info.fd = open(filename, handleFlags)) < 0)
        {
            if (device->dFlags & HANDLE_RECOMMEND_EXCLUSIVE_ACCESS)
            {
                handleFlags &= ~O_EXCL;
                continue;
            }
            perror("open");
            set_Device_Last_Error(device, errno);
            print_str("open failure\n");
            print_str("Error: ");
            print_Errno_To_Screen(errno);
            if (device->os_info.last_error == EACCES)
            {
                return PERMISSION_DENIED;
            }
            else if (device->os_info.last_error == EBUSY)
            {
                return DEVICE_BUSY;
            }
            else if (device->os_info.last_error == ENOENT || device->os_info.last_error == ENODEV)
            {
                return DEVICE_INVALID;
            }
            else
            {
                return FAILURE;
            }
        }
        else
        {
            break;
        }
    } while (attempts < HPUX_OPEN_ATTEMPTS_MAX);

    if (handleFlags & O_EXCL)
    {
        device->os_info.handleFlags = HANDLE_FLAGS_EXCLUSIVE;
    }
    else
    {
        device->os_info.handleFlags = HANDLE_FLAGS_DEFAULT;
    }

    device->os_info.osType = OS_HPUX;
    device->os_info.minimumAlignment =
        sizeof(void*); // setting to be compatible with certain aligned memory allocation functions.

    if (is_Persistent_Disk_Handle(filename))
    {
        device->os_info.persistentDev = true;
    }
    else
    {
        device->os_info.persistentDev = false;
    }

    if (device->dFlags == OPEN_HANDLE_ONLY)
    {
        return ret;
    }

    if ((device->os_info.fd >= 0) && (ret == SUCCESS))
    {
        // set the name
        snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
        set_Device_Name(filename, device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH);
        // must call this after friendly name has been set!
        set_Device_Partition_Info(device);

        device->drive_info.interface_type = SCSI_INTERFACE;
        device->drive_info.drive_type     = SCSI_DRIVE;
        // if (device->drive_info.interface_type == USB_INTERFACE ||
        //     device->drive_info.interface_type == IEEE_1394_INTERFACE)
        // {
        //     // TODO: Actually get the VID and PID set before calling this.
        //     setup_Passthrough_Hacks_By_ID(device);
        // }
        // fill in the device info
        ret = fill_Drive_Info_Data(device);
    }

    return ret;
}

// Block-mode Devices /dev/disk/diskN[_pX]
// Character-mode Devices /dev/disk/diskN[_pX]
// Legacy block-mode Devices /dev/dsk/cxtydn[sm]
// Legacy character-mode Devices /dev/rdsk/cxtydn[sm]

static int persistentDisk_filter(const struct dirent* entry)
{
    int valid = strncmp("disk/disk", entry->d_name, 9);
    if (valid != 0)
    {
        return !valid;
    }
    // all persistent disks with partitions use an underscore, so easy to filter out.
    char* partition = strpbrk(entry->d_name, "_");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !valid;
    }
}

static int legacyDisk_filter(const struct dirent* entry)
{
    // in this folder everything will start with a c.
    int valid = strncmp("rdsk/c", entry->d_name, 6);
    if (valid != 0)
    {
        return !valid;
    }
    // filter out the "sections" designated with optional s.
    // If this doesn't find ANY disks, then we should catch only section 0 for whole disks.
    char* partitionOrSlice = strpbrk(entry->d_name, "s");
    if (partitionOrSlice != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !valid;
    }
}

eReturnValues get_Device_Count(uint32_t* numberOfDevices, M_ATTR_UNUSED uint64_t flags)
{
    int num_devs = 0;

    struct dirent** namelist = M_NULLPTR;

    num_devs = scandir("/dev", &namelist, persistentDisk_filter, alphasort);
    if (num_devs == 0)
    {
        num_devs = scandir("/dev", &namelist, legacyDisk_filter, alphasort);
    }

    // free the list of names to not leak memory
    for (int iter = 0; iter < num_devs; ++iter)
    {
        safe_free_dirent(&namelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));

    if (num_devs > 0)
    {
        *numberOfDevices += C_CAST(uint32_t, num_devs);
    }

    return SUCCESS;
}

#define HPUX_NAME_LEN 80
eReturnValues get_Device_List(tDevice* const         ptrToDeviceList,
                              uint32_t               sizeInBytes,
                              versionBlock           ver,
                              M_ATTR_UNUSED uint64_t flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      totalDevs             = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    uint32_t      busyDevCount          = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, name, HPUX_NAME_LEN); // Because get device needs char
    int      fd       = -1;
    int      num_devs = 0;
    tDevice* d        = M_NULLPTR;

    struct dirent** namelist = M_NULLPTR;

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

    num_devs = scandir("/dev", &namelist, persistentDisk_filter, alphasort);
    if (num_devs <= 0)
    {
        num_devs = scandir("/dev", &namelist, legacyDisk_filter, alphasort);
    }

    char**   devs = M_REINTERPRET_CAST(char**, safe_calloc(num_devs + 1, sizeof(char*)));
    uint32_t i    = UINT32_C(0);
    for (; i < num_devs; i++)
    {
        size_t handleSize = (safe_strlen("/dev/") + safe_strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i]           = M_REINTERPRET_CAST(char*, safe_malloc(handleSize));
        snprintf_err_handle(devs[i], handleSize, "/dev/%s", namelist[i]->d_name);
        safe_free_dirent(&namelist[i]);
    }
    devs[i] = M_NULLPTR;
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));

    totalDevs = num_devs;

    DISABLE_NONNULL_COMPARE
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
            safe_memset(name, HPUX_NAME_LEN, 0, HPUX_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, HPUX_NAME_LEN, "%s", devs[driveNumber]);
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
                    printf("Failed open, reason: ");
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
    RESTORE_NONNULL_COMPARE
    safe_free(M_REINTERPRET_CAST(void**, &devs));
    if (VERBOSITY_COMMAND_NAMES <= listVerbosity)
    {
        printf("Get device list returning %d\n", returnValue);
    }
    return returnValue;
}

eReturnValues close_Device(tDevice* dev)
{
    int retValue = 0;
    DISABLE_NONNULL_COMPARE
    if (dev != M_NULLPTR)
    {
        retValue                = close(dev->os_info.fd);
        dev->os_info.last_error = errno;

        // if (dev->os_info.secondHandleValid && dev->os_info.secondHandleOpened)
        // {
        //     if (close(dev->os_info.fd2) == 0)
        //     {
        //         dev->os_info.fd2 = -1;
        //     }
        // }

        if (retValue == 0)
        {
            dev->os_info.fd = -1;
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
    RESTORE_NONNULL_COMPARE
}

eReturnValues os_Lock_Device(const tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.lockCount == UINT16_C(0))
    {
        if (ioctl(device->os_info.fd, SIOC_EXCLUSIVE, SIOC_SET_LUN_EXCL) < 0)
        {
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

eReturnValues os_Unlock_Device(const tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.lockCount == UINT16_C(1))
    {
        // only unlock once the number of requests gets back down to here so it will decrement to zero
        if (ioctl(device->os_info.fd, SIOC_EXCLUSIVE, SIOC_REL_LUN_EXCL) < 0)
        {
            ret = FAILURE;
        }
    }
    if (ret == SUCCESS && device->os_info.lockCount > 0)
    {
        --M_CONST_CAST(tDevice*, device)->os_info.lockCount;
    }
    return ret;
}

eReturnValues os_Get_Exclusive(M_ATTR_UNUSED const tDevice* device)
{
    // TODO: Unimplemented because not sure if O_EXCL is allowed or not. -TJE
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Update_File_System_Cache(M_ATTR_UNUSED const tDevice* device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Unmount_File_Systems_On_Device(M_ATTR_UNUSED const tDevice* device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}
