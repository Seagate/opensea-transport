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
#include "cam_helper.h"
#include "nvme_helper_func.h"
#include "sat_helper_func.h"
#include "scsi_helper_func.h"
#include "sntl_helper.h"
#include "usb_hacks.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/ucred.h>
#if !defined(DISABLE_NVME_PASSTHROUGH)
#    include <dev/nvme/nvme.h>
#endif // DISABLE_NVME_PASSTHROUGH

extern bool validate_Device_Struct(versionBlock);

#if !defined(CCB_CLEAR_ALL_EXCEPT_HDR)
// This is defined in newer versions of cam in FreeBSD, and is really useful.
// This is being redefined here in case it is missing for backwards compatibility with old FreeBSD versions
#    define CCB_CLEAR_ALL_EXCEPT_HDR(ccbp)                                                                             \
        bzero((char*)(ccbp) + sizeof((ccbp)->ccb_h), sizeof(*(ccbp)) - sizeof((ccbp)->ccb_h))
#endif

// If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise
// you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

static bool is_NVMe_Handle(char* handle)
{
    bool isNVMeDevice = false;
    if (handle && safe_strlen(handle))
    {
        if (strstr(handle, "nvme"))
        {
            isNVMeDevice = true;
        }
    }
    return isNVMeDevice;
}

static M_INLINE void safe_free_statfs(struct statfs** fs)
{
    safe_free_core(M_REINTERPRET_CAST(void**, fs));
}

static int get_Partition_Count(const char* blockDeviceName)
{
    int            result    = 0;
    struct statfs* mountedFS = M_NULLPTR;
    int            totalMounts =
        getmntinfo(&mountedFS,
                   MNT_WAIT); // Can switch to MNT_NOWAIT and will probably be fine, but using wait for best results-TJE
    if (totalMounts > 0 && mountedFS)
    {
        int entIter = 0;
        for (entIter = 0; entIter < totalMounts; ++entIter)
        {
            if (strstr((mountedFS + entIter)->f_mntfromname, blockDeviceName))
            {
                // found a match for the current device handle
                ++result;
            }
        }
    }
    safe_free_statfs(&mountedFS);
    return result;
}

#define PART_INFO_NAME_LENGTH (32)
#define PART_INFO_PATH_LENGTH (64)
typedef struct s_spartitionInfo
{
    char fsName[PART_INFO_NAME_LENGTH];
    char mntPath[PART_INFO_PATH_LENGTH];
} spartitionInfo, *ptrsPartitionInfo;

static M_INLINE void safe_free_spartioninfo(spartitionInfo** partinfo)
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
        // This is written using getmntinfo, which seems to wrap getfsstat.
        // https://www.freebsd.org/cgi/man.cgi?query=getmntinfo&manpath=FreeBSD+12.1-RELEASE+and+Ports
        // This was chosen because it provides the info we want, and also claims to only show mounted devices.
        // I also tried using getfsent, but that didn't return what we needed.
        // If for any reason, getmntinfo is not available, I recommend switching to getfsstat. The code is similar
        // but slightly different. I only had a VM to test with so my results showed the same between the APIs,
        // but the description of getmntinfo was more along the lines of what has been implemented for
        // other OS's we support. - TJE
        struct statfs* mountedFS   = M_NULLPTR;
        int            totalMounts = getmntinfo(
            &mountedFS,
            MNT_WAIT); // Can switch to MNT_NOWAIT and will probably be fine, but using wait for best results-TJE
        if (totalMounts > 0 && mountedFS)
        {
            int entIter = 0;
            for (entIter = 0; entIter < totalMounts; ++entIter)
            {
                if (strstr((mountedFS + entIter)->f_mntfromname, blockDeviceName))
                {
                    // found a match for the current device handle
                    // f_mntonname gives us the directory to unmount
                    // found a match, copy it to the list
                    if (matchesFound < listCount)
                    {
                        snprintf_err_handle((partitionInfoList + matchesFound)->fsName, PART_INFO_NAME_LENGTH, "%s",
                                            (mountedFS + entIter)->f_mntfromname);
                        snprintf_err_handle((partitionInfoList + matchesFound)->mntPath, PART_INFO_PATH_LENGTH, "%s",
                                            (mountedFS + entIter)->f_mntonname);
                        ++matchesFound;
                    }
                    else
                    {
                        result = MEMORY_FAILURE; // out of memory to copy all results to the list.
                    }
                }
            }
        }
        safe_free_statfs(&mountedFS);
    }
    return result;
}

static eReturnValues set_Device_Partition_Info(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    partitionCount               = get_Partition_Count(device->os_info.name);
#if defined(_DEBUG)
    printf("Partition count for %s = %d\n", device->os_info.name, partitionCount);
#endif
    if (partitionCount > 0)
    {
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;
        device->os_info.fileSystemInfo.hasActiveFileSystem = false;
        device->os_info.fileSystemInfo.isSystemDisk        = false;
        ptrsPartitionInfo parts =
            M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts != M_NULLPTR)
        {
            if (SUCCESS == get_Partition_List(device->os_info.name, parts, partitionCount))
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
                    // Should / be treated as a system disk too?
                    if (strncmp((parts + iter)->mntPath, "/boot", 5) == 0)
                    {
                        device->os_info.fileSystemInfo.isSystemDisk = true;
#if defined(_DEBUG)
                        printf("found system disk\n");
#endif
                    }
                }
            }
            safe_free_spartioninfo(&parts);
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

eReturnValues get_Device(const char* filename, tDevice* device)
{
    struct ccb_getdev  cgd;
    struct ccb_pathinq cpi;
    union ccb*         ccb = M_NULLPTR;
    eReturnValues      ret = SUCCESS;
    // int this_drive_type = 0;
    DECLARE_ZERO_INIT_ARRAY(char, devName, 20);
    int     devUnit         = 0;
    char*   deviceHandle    = M_NULLPTR;
    errno_t duphandle       = safe_strdup(&deviceHandle, filename);
    device->os_info.cam_dev = M_NULLPTR; // initialize this to M_NULLPTR (which it already should be) just to make sure
                                         // everything else functions as expected

    if (duphandle != 0 || deviceHandle == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }

#if !defined(DISABLE_NVME_PASSTHROUGH)
    struct nvme_get_nsid gnsid;

    if (is_NVMe_Handle(deviceHandle))
    {
        if ((device->os_info.fd = open(deviceHandle, O_RDWR | O_NONBLOCK)) < 0)
        {
            perror("open");
            device->os_info.fd = errno;
            printf("open failure");
            printf("Error:");
            print_Errno_To_Screen(errno);
            if (device->os_info.fd == EACCES)
            {
                safe_free(&deviceHandle);
                return PERMISSION_DENIED;
            }
            else
            {
                safe_free(&deviceHandle);
                return FAILURE;
            }
        }

        device->os_info.minimumAlignment = sizeof(void*);

        device->drive_info.drive_type     = NVME_DRIVE;
        device->drive_info.interface_type = NVME_INTERFACE;
        device->drive_info.media_type     = MEDIA_NVM;
        // ret = ioctl(device->os_info.fd, NVME_IOCTL_ID)
        // if ( ret < 0 )
        //{
        //     perror("nvme_ioctl_id");
        //	  return ret;
        // }
        ioctl(device->os_info.fd, NVME_GET_NSID, &gnsid);
        device->drive_info.namespaceID = gnsid.nsid;
        device->os_info.osType         = OS_FREEBSD;

        char* baseLink = basename(deviceHandle);
        // Now we will set up the device name, etc fields in the os_info structure
        snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s", baseLink);
        snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", baseLink);
        set_Device_Partition_Info(device);

        ret = fill_Drive_Info_Data(device);

        safe_free(&deviceHandle);
        return ret;
    }
    else
#endif

        if (cam_get_device(filename, devName, 20, &devUnit) == -1)
    {
        ret                = FAILURE;
        device->os_info.fd = -1;
        printf("%s failed\n", __FUNCTION__);
    }
    else
    {
        // printf("%s fd %d name %s\n",__FUNCTION__, device->os_info.fd, device->os_info.name);
        device->os_info.cam_dev = cam_open_spec_device(devName, devUnit, O_RDWR, M_NULLPTR); // O_NONBLOCK is not
                                                                                             // allowed
        if (device->os_info.cam_dev != M_NULLPTR)
        {
            // Set name and friendly name
            // name
            snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
            // friendly name
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s%d", devName,
                                devUnit);

            device->os_info.fd = devUnit;

            // set the OS Type
            device->os_info.osType           = OS_FREEBSD;
            device->os_info.minimumAlignment = sizeof(void*);

            if (device->dFlags == OPEN_HANDLE_ONLY)
            {
                return ret;
            }

            // printf("%s Successfully opened\n",__FUNCTION__);
            ccb = cam_getccb(device->os_info.cam_dev);
            if (ccb != M_NULLPTR)
            {
                CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
                ccb->ccb_h.func_code = XPT_GDEV_TYPE;
                if (cam_send_ccb(device->os_info.cam_dev, ccb) >= 0)
                {
                    if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
                    {
                        safe_memcpy(&cgd, sizeof(struct ccb_getdev), &ccb->cgd, sizeof(struct ccb_getdev));

                        // default to scsi drive and scsi interface
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                        // start checking what information we got from the OS
                        if (cgd.protocol == PROTO_SCSI)
                        {
                            device->drive_info.interface_type = SCSI_INTERFACE;

                            safe_memcpy(&device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1,
                                        cgd.inq_data.vendor, SID_VENDOR_SIZE);
                            safe_memcpy(&device->drive_info.product_identification, MODEL_NUM_LEN + 1,
                                        cgd.inq_data.product, M_Min(MODEL_NUM_LEN, SID_PRODUCT_SIZE));
                            safe_memcpy(&device->drive_info.product_revision, FW_REV_LEN + 1, cgd.inq_data.revision,
                                        M_Min(FW_REV_LEN, SID_REVISION_SIZE));
                            // cgd.serial_num is max of 256B. M_Min not used because this is much bigger than our
                            // internal structure.-TJE
                            safe_memcpy(&device->drive_info.serialNumber, SERIAL_NUM_LEN + 1, cgd.serial_num,
                                        SERIAL_NUM_LEN);

                            // remove ATA vs SCSI check as that will be performed in an above layer of the code.
                        }
                        else if (cgd.protocol == PROTO_ATA || cgd.protocol == PROTO_ATAPI)
                        {
                            device->drive_info.interface_type = IDE_INTERFACE;
                            device->drive_info.drive_type     = ATA_DRIVE;
                            if (cgd.protocol == PROTO_ATAPI)
                            {
                                device->drive_info.drive_type = ATAPI_DRIVE;
                            }
                            safe_memcpy(&device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1, "ATA", 3);
                            safe_memcpy(&device->drive_info.product_identification, MODEL_NUM_LEN + 1,
                                        cgd.ident_data.model,
                                        M_Min(MODEL_NUM_LEN, 40)); // 40 comes from ata_param stuct in the ata.h
                            safe_memcpy(&device->drive_info.product_revision, FW_REV_LEN + 1, cgd.ident_data.revision,
                                        M_Min(FW_REV_LEN, 8)); // 8 comes from ata_param stuct in the ata.h
                            safe_memcpy(&device->drive_info.serialNumber, SERIAL_NUM_LEN + 1, cgd.ident_data.serial,
                                        M_Min(SERIAL_NUM_LEN, 20)); // 20 comes from ata_param stuct in the ata.h
                        }
                        else
                        {
                            printf("Unsupported interface %d\n", cgd.protocol);
                        }
                        // get interface info
                        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
                        ccb->ccb_h.func_code = XPT_PATH_INQ;
                        if (cam_send_ccb(device->os_info.cam_dev, ccb) >= 0)
                        {
                            if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
                            {
                                safe_memcpy(&cpi, sizeof(struct ccb_pathinq), &ccb->cpi, sizeof(struct ccb_pathinq));
                                // set the interface from a ccb_pathinq struct
                                switch (cpi.transport)
                                {
                                case XPORT_SATA:
                                case XPORT_ATA:
                                    device->drive_info.interface_type =
                                        IDE_INTERFACE; // Seeing IDE may look strange, but that is how old code was
                                                       // written to identify an ATA interface regardless of parallel or
                                                       // serial.
                                    break;
                                case XPORT_USB:
                                    device->drive_info.interface_type = USB_INTERFACE;
                                    break;
                                case XPORT_SPI:
                                    device->drive_info.interface_type = SCSI_INTERFACE;
                                    // firewire is reported as SPI.
                                    // Check hba_vid for "SBP" to tell the difference and set the proper interface
                                    if (strncmp(cpi.hba_vid, "SBP", 3) == 0 &&
                                        cpi.hba_misc &
                                            (PIM_NOBUSRESET |
                                             PIM_NO_6_BYTE)) // or check initiator_id? That's defined but not sure if
                                                             // that could be confused with other SPI devices - TJE
                                    {
                                        device->drive_info.interface_type = IEEE_1394_INTERFACE;
                                        // TODO: Figure out where to get device unique firewire IDs for specific device
                                        // compatibility lookups
                                    }
                                    break;
                                case XPORT_SAS:
                                case XPORT_ISCSI:
                                case XPORT_SSA:
                                case XPORT_FC:
                                case XPORT_UNSPECIFIED:
                                case XPORT_UNKNOWN:
                                default:
                                    device->drive_info.interface_type = SCSI_INTERFACE;
                                    break;
                                }
                                // TODO: Parse other flags to set hacks and capabilities to help with adapter or
                                // interface specific limitations
                                //        - target flags which may help identify device capabilities (no 6-byte
                                //        commands, group 6 & 7 command support, ata_ext request support.
                                //        - target flag that says no 6-byte commands can help uniquely identify IEEE1394
                                //        devices
                                //       These should be saved later when we run into compatibility issues or need to
                                //       make other improvements. For now, getting the interface is a huge help

#if IS_FREEBSD_VERSION(9, 0, 0)
                                if (device->drive_info.interface_type != USB_INTERFACE &&
                                    device->drive_info.interface_type != IEEE_1394_INTERFACE)
                                {
                                    // NOTE: Not entirely sure EXACTLY when this was introduced, but this is a best
                                    // guess from looking through cam_ccb.h history
                                    if (cpi.hba_vendor != 0 &&
                                        cpi.hba_device != 0) // try to filter out when information is not available
                                    {
                                        device->drive_info.adapter_info.infoType       = ADAPTER_INFO_PCI;
                                        device->drive_info.adapter_info.productIDValid = true;
                                        device->drive_info.adapter_info.vendorIDValid  = true;
                                        device->drive_info.adapter_info.productID      = cpi.hba_device;
                                        device->drive_info.adapter_info.vendorID       = cpi.hba_vendor;
                                        // NOT: Subvendor and subdevice seem to specify something further up the tree
                                        // from the adapter itself.
                                    }
                                }
#endif
                            }
                            else
                            {
                                printf("WARN: XPT_PATH_INQ I/O status failed\n");
                            }
                        }
                        // let the library now go out and set up the device struct after sending some commands.
                        if (device->drive_info.interface_type == USB_INTERFACE ||
                            device->drive_info.interface_type == IEEE_1394_INTERFACE)
                        {
                            // TODO: Actually get the VID and PID set before calling this.
                            //       This will require some more research, but we should be able to do this now that we
                            //       know the interface
                            setup_Passthrough_Hacks_By_ID(device);
                        }
                        set_Device_Partition_Info(device);
                        ret = fill_Drive_Info_Data(device);
                    }
                    else
                    {
                        printf("WARN: XPT_GDEV_TYPE I/O status failed\n");
                        ret = FAILURE;
                    }
                }
                else
                {
                    printf("WARN: XPT_GDEV_TYPE I/O failed\n");
                    ret = FAILURE;
                }
            }
            else
            {
                printf("WARN: Could not allocate CCB\n");
                ret = FAILURE;
            }
        }
        else
        {
            printf("%s Opened Failed\n", __FUNCTION__);
            ret = FAILURE;
        }
    }

    if (ccb != M_NULLPTR)
    {
        cam_freeccb(ccb);
    }

    return ret;
}

eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = FAILURE;
    // printf("%s -->\n",__FUNCTION__);

    if (scsiIoCtx->device->drive_info.interface_type == SCSI_INTERFACE)
    {
        ret = send_Scsi_Cam_IO(scsiIoCtx);
    }
    else if (scsiIoCtx->device->drive_info.interface_type == NVME_INTERFACE)
    {
        ret = sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
    }
    else if (scsiIoCtx->device->drive_info.interface_type == IDE_INTERFACE)
    {
        if (scsiIoCtx->pAtaCmdOpts)
        {
            ret = send_Ata_Cam_IO(scsiIoCtx);
        }
        else
        {
            ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        }
    }
    else if (scsiIoCtx->device->drive_info.interface_type == RAID_INTERFACE)
    {
        if (scsiIoCtx->device->issue_io != M_NULLPTR)
        {
            ret = scsiIoCtx->device->issue_io(scsiIoCtx);
        }
        else
        {
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("No Raid PassThrough IO Routine present for this device\n");
            }
        }
    }
    else
    {
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Target Device does not have a valid interface %d\n", scsiIoCtx->device->drive_info.interface_type);
        }
    }
    // printf("<-- %s\n",__FUNCTION__);
    if (scsiIoCtx->device->delay_io)
    {
        delay_Milliseconds(scsiIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= scsiIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", scsiIoCtx->device->delay_io);
        }
    }

    return ret;
}

eReturnValues send_Ata_Cam_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues     ret       = SUCCESS;
    union ccb*        ccb       = M_NULLPTR;
    struct ccb_ataio* ataio     = M_NULLPTR;
    u_int32_t         direction = UINT32_C(0);

    ccb = cam_getccb(scsiIoCtx->device->os_info.cam_dev);

    if (ccb != M_NULLPTR)
    {
        ataio = &ccb->ataio;

        /* cam_getccb cleans up the header, caller has to zero the payload */
        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

        switch (scsiIoCtx->direction)
        {
            // NOLINTBEGIN(bugprone-branch-clone)
        case XFER_NO_DATA:
            direction = CAM_DIR_NONE;
            break;
        case XFER_DATA_IN:
            direction = CAM_DIR_IN;
            break;
        case XFER_DATA_OUT:
            direction = CAM_DIR_OUT;
            break;
        case XFER_DATA_OUT_IN:
        case XFER_DATA_IN_OUT:
            direction = CAM_DIR_BOTH;
            break;
            // NOLINTEND(bugprone-branch-clone)
        }

        uint32_t camTimeout = scsiIoCtx->timeout;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
            scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            camTimeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < CAM_MAX_CMD_TIMEOUT_SECONDS)
            {
                camTimeout *= UINT32_C(1000); // convert to milliseconds
            }
            else
            {
                camTimeout = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            if (scsiIoCtx->timeout != UINT32_C(0))
            {
                camTimeout = scsiIoCtx->timeout;
                // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
                // security) that we DON'T do a conversion and leave the time as the max...
                if (scsiIoCtx->timeout < CAM_MAX_CMD_TIMEOUT_SECONDS)
                {
                    camTimeout *= UINT32_C(1000); // convert to milliseconds
                }
                else
                {
                    camTimeout = UINT32_MAX; // no timeout or maximum timeout
                }
            }
            else
            {
                camTimeout = UINT32_C(15) * UINT32_C(1000); // default to 15 second timeout
            }
        }

        cam_fill_ataio(&ccb->ataio, 0,                                        /* retry_count */
                       M_NULLPTR, direction,                                  /*flags*/
                       MSG_SIMPLE_Q_TAG, C_CAST(u_int8_t*, scsiIoCtx->pdata), /*data_ptr*/
                       scsiIoCtx->dataLength,                                 /*dxfer_len*/
                       camTimeout);

        /* Disable freezing the device queue */
        ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

        if (scsiIoCtx->pAtaCmdOpts != M_NULLPTR)
        {
            bzero(&ataio->cmd, sizeof(ataio->cmd));
            if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_TASKFILE)
            {
                // NOLINTBEGIN(bugprone-branch-clone)
                if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_QUE ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UDMA)
                {
                    ataio->cmd.flags |= CAM_ATAIO_DMA;
                }
                else if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_FPDMA)
                {
                    ataio->cmd.flags |= CAM_ATAIO_FPDMA;
                }
                // NOLINTEND(bugprone-branch-clone)
                ataio->cmd.command      = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
                ataio->cmd.features     = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
                ataio->cmd.lba_low      = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
                ataio->cmd.lba_mid      = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
                ataio->cmd.lba_high     = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
                ataio->cmd.device       = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
                ataio->cmd.sector_count = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
            }
            else if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
            {
                ataio->cmd.flags |= CAM_ATAIO_48BIT;
                // NOLINTBEGIN(bugprone-branch-clone)
                if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_QUE ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UDMA)
                {
                    ataio->cmd.flags |= CAM_ATAIO_DMA;
                }
                else if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_FPDMA)
                {
                    ataio->cmd.flags |= CAM_ATAIO_FPDMA;
                }
                // NOLINTEND(bugprone-branch-clone)
                ataio->cmd.command          = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
                ataio->cmd.lba_low          = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
                ataio->cmd.lba_mid          = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
                ataio->cmd.lba_high         = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
                ataio->cmd.device           = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
                ataio->cmd.lba_low_exp      = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;
                ataio->cmd.lba_mid_exp      = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;
                ataio->cmd.lba_high_exp     = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;
                ataio->cmd.features         = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
                ataio->cmd.features_exp     = scsiIoCtx->pAtaCmdOpts->tfr.Feature48;
                ataio->cmd.sector_count     = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
                ataio->cmd.sector_count_exp = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;
            }
            else if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
            {
#if defined(ATA_FLAG_AUX) || defined(ATA_FLAG_ICC)
                ataio->cmd.flags |= CAM_ATAIO_48BIT;
                // NOLINTBEGIN(bugprone-branch-clone)
                if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_QUE ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UDMA)
                {
                    ataio->cmd.flags |= CAM_ATAIO_DMA;
                }
                else if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_FPDMA)
                {
                    ataio->cmd.flags |= CAM_ATAIO_FPDMA;
                }
                // NOLINTEND(bugprone-branch-clone)
                ataio->cmd.command          = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
                ataio->cmd.lba_low          = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
                ataio->cmd.lba_mid          = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
                ataio->cmd.lba_high         = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
                ataio->cmd.device           = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
                ataio->cmd.lba_low_exp      = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;
                ataio->cmd.lba_mid_exp      = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;
                ataio->cmd.lba_high_exp     = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;
                ataio->cmd.features         = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
                ataio->cmd.features_exp     = scsiIoCtx->pAtaCmdOpts->tfr.Feature48;
                ataio->cmd.sector_count     = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
                ataio->cmd.sector_count_exp = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;
                if (scsiIoCtx->pAtaCmdOpts->tfr.icc)
                {
#    if defined(ATA_FLAG_ICC)
                    // can set ICC
                    ataio->ata_flags |= ATA_FLAG_ICC;
                    ataio->icc = scsiIoCtx->pAtaCmdOpts->tfr.icc;
#    else
                    // cannot set ICC field
                    ret = OS_COMMAND_NOT_AVAILABLE;
#    endif // ATA_FLAG_ICC
                }
                if (scsiIoCtx->pAtaCmdOpts->tfr.aux1 || scsiIoCtx->pAtaCmdOpts->tfr.aux2 ||
                    scsiIoCtx->pAtaCmdOpts->tfr.aux3 || scsiIoCtx->pAtaCmdOpts->tfr.aux4)
                {
#    if defined(ATA_FLAG_AUX)
                    // can set AUX
                    ataio->ata_flags |= ATA_FLAG_AUX;
                    ataio->aux =
                        M_BytesTo4ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.aux4, scsiIoCtx->pAtaCmdOpts->tfr.aux3,
                                            scsiIoCtx->pAtaCmdOpts->tfr.aux2, scsiIoCtx->pAtaCmdOpts->tfr.aux1);
#    else
                    // cannot set AUX field
                    ret = OS_COMMAND_NOT_AVAILABLE;
#    endif // ATA_FLAG_ICC
                }
#else  /* !AUX || !ICC*/
                // AUX and ICC are not available to be set in this version of freebsd
                ret = OS_COMMAND_NOT_AVAILABLE;
#endif /* ATA_FLAG_AUX || ATA_FLAG_ICC */
            }
            else
            {
                ret = BAD_PARAMETER;
                printf("WARN: Unsupported ATA Command type\n");
            }

            if (ret == SUCCESS)
            {
                DECLARE_SEATIMER(commandTimer);
#if defined(_DEBUG)
                printf("ATAIO: cmd=0x%02" PRIX8 " feat=0x%02" PRIX8 " lbalow=0x%02" PRIX8 " lbamid=0x%02" PRIX8
                       " lbahi=0x%02" PRIX8 " sc=0x%02" PRIX8 "\n",
                       ataio->cmd.command, ataio->cmd.features, ataio->cmd.lba_low, ataio->cmd.lba_mid,
                       ataio->cmd.lba_high, ataio->cmd.sector_count);
                printf("\tfeatext=0x%02" PRIX8 " lbalowExp=0x%02" PRIX8 " lbamidExp=0x%02" PRIX8 " lbahiExp=0x%02" PRIX8
                       " scExp=0x%02" PRIX8 "\n",
                       ataio->cmd.features_exp, ataio->cmd.lba_low_exp, ataio->cmd.lba_mid_exp, ataio->cmd.lba_high_exp,
                       ataio->cmd.sector_count_exp);

                printf("\tData Ptr %p, xfer len %d\n", ataio->data_ptr, ataio->dxfer_len);
#endif
                /* Always asking for the results at this time. */
                ccb->ataio.cmd.flags |= CAM_ATAIO_NEEDRESULT;
                start_Timer(&commandTimer);
                int ioctlResult = cam_send_ccb(scsiIoCtx->device->os_info.cam_dev, ccb);
                stop_Timer(&commandTimer);
                if (ioctlResult < 0)
                {
                    perror("error sending ATA I/O");
                    cam_error_print(scsiIoCtx->device->os_info.cam_dev, ccb, CAM_ESF_ALL /*error string flags*/,
                                    CAM_EPF_ALL, stdout);
                    ret = FAILURE;
                }
                else
                {
                    // cam_error_print(scsiIoCtx->device->os_info.cam_dev, ccb, CAM_ESF_ALL /*error string flags*/,
                    // CAM_EPF_ALL, stdout);
                    if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
                    {
                        if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_ATA_STATUS_ERROR)
                        {
                            ret = COMMAND_FAILURE;
                            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                            {
                                printf("WARN: I/O went through but drive returned status=0x%02" PRIX8
                                       " error=0x%02" PRIX8 "\n",
                                       ataio->res.status, ataio->res.error);
                            }
                        }
                        else if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_CMD_TIMEOUT)
                        {
                            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                            {
                                printf("WARN: I/O CAM_CMD_TIMEOUT occured\n");
                            }
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                            {
                                printf("WARN: I/O error occurred %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
                            }
                        }
                    }
                    else
                    {
#if defined(_DEBUG)
                        printf("I/O went through status %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
#endif
                    }
                    ret = SUCCESS;

                    // get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with
                    // the rtfrs in descriptor format
                    if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
                    {
                        if (scsiIoCtx->senseDataSize >= 22) // check that the sense data buffer is big enough to fill in
                                                            // our rtfrs using descriptor format
                        {
                            scsiIoCtx->returnStatus.format = 0x72;
                            scsiIoCtx->returnStatus.senseKey =
                                0x01; // Not setting check condition since the IO was in fact successful
                            // setting ASC/ASCQ to ATA Passthrough Information Available
                            scsiIoCtx->returnStatus.asc  = 0x00;
                            scsiIoCtx->returnStatus.ascq = 0x1D;
                            // now fill in the sens buffer
                            scsiIoCtx->psense[0] = 0x72;
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
                                if (ataio->res.flags &
                                    CAM_ATAIO_48BIT) /* check this flag to make sure we read valid data */
                                {
                                    scsiIoCtx->psense[12] = ataio->res.sector_count_exp; // Sector Count Ext
                                    scsiIoCtx->psense[14] = ataio->res.lba_low_exp;      // LBA Lo Ext
                                    scsiIoCtx->psense[16] = ataio->res.lba_mid_exp;      // LBA Mid Ext
                                    scsiIoCtx->psense[18] = ataio->res.lba_high_exp;     // LBA Hi
                                }
                            }
                            // fill in the returned 28bit registers
                            scsiIoCtx->psense[11] = ataio->res.error;        // Error
                            scsiIoCtx->psense[13] = ataio->res.sector_count; // Sector Count
                            scsiIoCtx->psense[15] = ataio->res.lba_low;      // LBA Lo
                            scsiIoCtx->psense[17] = ataio->res.lba_mid;      // LBA Mid
                            scsiIoCtx->psense[19] = ataio->res.lba_high;     // LBA Hi
                            scsiIoCtx->psense[20] = ataio->res.device;       // Device/Head
                            scsiIoCtx->psense[21] = ataio->res.status;       // Status
                        }
                    }
                }
                scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
            }
        }
        else
        {
            if (VERBOSITY_DEFAULT < scsiIoCtx->device->deviceVerbosity)
            {
                printf("WARN: Sending non-ATA commnad to ATA Drive [FreeBSD CAM driver does not support SAT "
                       "Specification]\n");
            }
            ret = BAD_PARAMETER;
        }
    }
    else
    {
        printf("WARN: couldn't allocate CCB");
    }

    return ret;
}

eReturnValues send_Scsi_Cam_IO(ScsiIoCtx* scsiIoCtx)
{
#if defined(_DEBUG)
    printf("--> %s\n", __FUNCTION__);
#endif
    eReturnValues ret = SUCCESS;
    // device * device = scsiIoCtx->device;
    struct ccb_scsiio* csio = M_NULLPTR;
    union ccb*         ccb  = M_NULLPTR;

    if (scsiIoCtx->device->os_info.cam_dev == M_NULLPTR)
    {
        printf("%s dev is M_NULLPTR\n", __FUNCTION__);
        return FAILURE;
    }
    else if (scsiIoCtx->cdbLength > IOCDBLEN)
    {
        printf("%s too big CDB\n", __FUNCTION__);
        return BAD_PARAMETER;
    }

    ccb = cam_getccb(scsiIoCtx->device->os_info.cam_dev);

    if (ccb != M_NULLPTR)
    {
        // Following is copy/paste from different funtions in camcontrol.c
        /* cam_getccb cleans up the header, caller has to zero the payload */
        bzero(&(&ccb->ccb_h)[1], sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

        csio = &ccb->csio;

        csio->ccb_h.func_code   = XPT_SCSI_IO;
        csio->ccb_h.retry_count = UINT32_C(0); // should we change it to 1?
        csio->ccb_h.cbfcnp      = M_NULLPTR;
        uint32_t camTimeout     = scsiIoCtx->timeout;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > UINT32_C(0) &&
            scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            camTimeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < CAM_MAX_CMD_TIMEOUT_SECONDS)
            {
                camTimeout *= UINT32_C(1000); // convert to milliseconds
            }
            else
            {
                camTimeout = CAM_TIME_INFINITY; // no timeout or maximum timeout
            }
        }
        else
        {
            if (scsiIoCtx->timeout != UINT32_C(0))
            {
                camTimeout = scsiIoCtx->timeout;
                // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
                // security) that we DON'T do a conversion and leave the time as the max...
                if (scsiIoCtx->timeout < CAM_MAX_CMD_TIMEOUT_SECONDS)
                {
                    camTimeout *= UINT32_C(1000); // convert to milliseconds
                }
                else
                {
                    camTimeout = CAM_TIME_INFINITY; // no timeout or maximum timeout
                }
            }
            else
            {
                camTimeout = UINT32_C(15) * UINT32_C(1000); // default to 15 second timeout
            }
        }
        csio->ccb_h.timeout = camTimeout;
        csio->cdb_len       = scsiIoCtx->cdbLength;
        csio->sense_len  = scsiIoCtx->senseDataSize; // So it seems the csio has it's own buffer for Sense...so revist.
        csio->tag_action = MSG_SIMPLE_Q_TAG;         // TODO: will we have anything else ever?

        switch (scsiIoCtx->direction)
        {
            // NOLINTBEGIN(bugprone-branch-clone)
        case XFER_NO_DATA:
            csio->ccb_h.flags = CAM_DIR_NONE;
            break;
        case XFER_DATA_IN:
            csio->ccb_h.flags = CAM_DIR_IN;
            break;
        case XFER_DATA_OUT:
            csio->ccb_h.flags = CAM_DIR_OUT;
            break;
        case XFER_DATA_OUT_IN:
        case XFER_DATA_IN_OUT:
            csio->ccb_h.flags = CAM_DIR_BOTH;
            break;
            // case SG_DXFER_UNKNOWN:
            // io_hdr.dxfer_direction = SG_DXFER_UNKNOWN;
            // break;
            // NOLINTEND(bugprone-branch-clone)
        default:
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("%s Didn't understand direction\n", __FUNCTION__);
            }
            return BAD_PARAMETER;
        }

        csio->dxfer_len = scsiIoCtx->dataLength;
        csio->data_ptr  = scsiIoCtx->pdata;

        /* Disable freezing the device queue */
        ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
        // ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER; // Needed?

        safe_memcpy(&csio->cdb_io.cdb_bytes[0], IOCDBLEN, &scsiIoCtx->cdb[0], IOCDBLEN);
#if defined(_DEBUG)
        printf("%s cdb [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x] \n\t \
               [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x]\n",
               __FUNCTION__, csio->cdb_io.cdb_bytes[0], csio->cdb_io.cdb_bytes[1], csio->cdb_io.cdb_bytes[2],
               csio->cdb_io.cdb_bytes[3], csio->cdb_io.cdb_bytes[4], csio->cdb_io.cdb_bytes[5],
               csio->cdb_io.cdb_bytes[6], csio->cdb_io.cdb_bytes[7], csio->cdb_io.cdb_bytes[8],
               csio->cdb_io.cdb_bytes[9], csio->cdb_io.cdb_bytes[10], csio->cdb_io.cdb_bytes[11],
               csio->cdb_io.cdb_bytes[12], csio->cdb_io.cdb_bytes[13], csio->cdb_io.cdb_bytes[14],
               csio->cdb_io.cdb_bytes[15]);
#endif
        DECLARE_SEATIMER(commandTimer);
        start_Timer(&commandTimer);
        int ioctlResult = cam_send_ccb(scsiIoCtx->device->os_info.cam_dev, ccb);
        stop_Timer(&commandTimer);
        if (ioctlResult < 0)
        {
            perror("cam_send_cdb");
        }

        if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
        {
#if defined(_DEBUG)
            printf("%s success with ret %d & valid sense=%d\n", __FUNCTION__, ret,
                   (ccb->ccb_h.status & CAM_AUTOSNS_VALID));
            printf("%s error code %d, sense [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x] \n\t \
               [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x]\n",
                   __FUNCTION__, csio->sense_data.error_code, csio->sense_data.sense_buf[0],
                   csio->sense_data.sense_buf[1], csio->sense_data.sense_buf[2], csio->sense_data.sense_buf[3],
                   csio->sense_data.sense_buf[4], csio->sense_data.sense_buf[5], csio->sense_data.sense_buf[6],
                   csio->sense_data.sense_buf[7], csio->sense_data.sense_buf[8], csio->sense_data.sense_buf[9],
                   csio->sense_data.sense_buf[10], csio->sense_data.sense_buf[11], csio->sense_data.sense_buf[12],
                   csio->sense_data.sense_buf[13], csio->sense_data.sense_buf[14], csio->sense_data.sense_buf[15]);
#endif
            scsiIoCtx->returnStatus.senseKey = csio->scsi_status;

            // if ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) == 0)
            // {
            //     // Since we have no sense data fake it for ATA
            //     if (scsiIoCtx->device->drive_info.drive_type == ATA_DRIVE)
            //     {
            //         if (scsiIoCtx->returnStatus.senseKey == SCSI_STATUS_OK)
            //         {
            //             // scsiIoCtx->rtfrs.status = ATA_GOOD_STATUS;
            //             // scsiIoCtx->rtfrs.error = 0;
            //         }
            //         else
            //         {
            //             // scsiIoCtx->rtfrs.status = 0x51;
            //             // scsiIoCtx->rtfrs.error = 0x4;
            //         }
            //     }
            // }
            // else // we have some valid sense?
            // {
            // }
        }
        else
        {
            ret = COMMAND_FAILURE;

            if (VERBOSITY_DEFAULT < scsiIoCtx->device->deviceVerbosity)
            {
                printf("%s cam error %d, scsi error %d\n", __FUNCTION__, (ccb->ccb_h.status & CAM_STATUS_MASK),
                       ccb->csio.scsi_status);
            }

            if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR) &&
                (ccb->csio.scsi_status == SCSI_STATUS_CHECK_COND) && ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) != 0))
            {
                safe_memcpy(scsiIoCtx->psense, (scsiIoCtx->senseDataSize), &csio->sense_data.error_code,
                            sizeof(uint8_t));
                safe_memcpy(scsiIoCtx->psense + 1, (scsiIoCtx->senseDataSize) - 1, &csio->sense_data.sense_buf[0],
                            (scsiIoCtx->senseDataSize) - 1);
#if defined(_DEBUG)
                printf("%s error code %d, sense [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x] \n\t \
                   [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x]\n",
                       __FUNCTION__, csio->sense_data.error_code, csio->sense_data.sense_buf[0],
                       csio->sense_data.sense_buf[1], csio->sense_data.sense_buf[2], csio->sense_data.sense_buf[3],
                       csio->sense_data.sense_buf[4], csio->sense_data.sense_buf[5], csio->sense_data.sense_buf[6],
                       csio->sense_data.sense_buf[7], csio->sense_data.sense_buf[8], csio->sense_data.sense_buf[9],
                       csio->sense_data.sense_buf[10], csio->sense_data.sense_buf[11], csio->sense_data.sense_buf[12],
                       csio->sense_data.sense_buf[13], csio->sense_data.sense_buf[14], csio->sense_data.sense_buf[15]);
#endif
            }
        }
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    }
    else
    {
        printf("ccb is Null\n");
        ret = BAD_PARAMETER; // Should this be MEMORY FAILURE?
    }

    cam_freeccb(ccb);

#if defined(_DEBUG)
    printf("<-- %s ret=[%d]\n", __FUNCTION__, ret);
#endif

    return ret;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
static int nvme_filter(const struct dirent* entry)
{
    int nvmeHandle = strncmp("nvme", entry->d_name, 3);
    if (nvmeHandle != 0)
    {
        return !nvmeHandle;
    }
    char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !nvmeHandle;
    }
}
#endif

static int da_filter(const struct dirent* entry)
{
    int daHandle = strncmp("da", entry->d_name, 2);
    if (daHandle != 0)
    {
        return !daHandle;
    }
    char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !daHandle;
    }
}

static int ada_filter(const struct dirent* entry)
{
    int adaHandle = strncmp("ada", entry->d_name, 3);
    if (adaHandle != 0)
    {
        return !adaHandle;
    }
    char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !adaHandle;
    }
}

eReturnValues close_Device(tDevice* dev)
{
    if (dev->os_info.cam_dev)
    {
        cam_close_device(dev->os_info.cam_dev);
        dev->os_info.cam_dev = M_NULLPTR;
    }
    return SUCCESS;
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
eReturnValues get_Device_Count(uint32_t* numberOfDevices, M_ATTR_UNUSED uint64_t flags)
{
    int num_da_devs   = 0;
    int num_ada_devs  = 0;
    int num_nvme_devs = 0;

    struct dirent** danamelist;
    struct dirent** adanamelist;
    struct dirent** nvmenamelist;

    num_da_devs   = scandir("/dev", &danamelist, da_filter, alphasort);
    num_ada_devs  = scandir("/dev", &adanamelist, ada_filter, alphasort);
    num_nvme_devs = scandir("/dev", &nvmenamelist, nvme_filter, alphasort);

    // free the list of names to not leak memory
    for (int iter = 0; iter < num_da_devs; ++iter)
    {
        safe_free_dirent(&danamelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &danamelist));
    // free the list of names to not leak memory
    for (int iter = 0; iter < num_ada_devs; ++iter)
    {
        safe_free_dirent(&adanamelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &adanamelist));

    // free the list of names to not leak memory
    for (int iter = 0; iter < num_nvme_devs; ++iter)
    {
        safe_free_dirent(&nvmenamelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &nvmenamelist));
    if (num_da_devs > 0)
    {
        *numberOfDevices += C_CAST(uint32_t, num_da_devs);
    }
    if (num_ada_devs > 0)
    {
        *numberOfDevices += C_CAST(uint32_t, num_ada_devs);
    }
    if (num_nvme_devs > 0)
    {
        *numberOfDevices += C_CAST(uint32_t, num_nvme_devs);
    }

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
#define CAM_DEV_NAME_LEN 80
eReturnValues get_Device_List(tDevice* const         ptrToDeviceList,
                              uint32_t               sizeInBytes,
                              versionBlock           ver,
                              M_ATTR_UNUSED uint64_t flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, name, CAM_DEV_NAME_LEN);
    int      fd            = 0;
    tDevice* d             = M_NULLPTR;
    int      scandirres    = 0;
    uint32_t num_da_devs   = UINT32_C(0);
    uint32_t num_ada_devs  = UINT32_C(0);
    uint32_t num_nvme_devs = UINT32_C(0);

    struct dirent** danamelist;
    struct dirent** adanamelist;
    struct dirent** nvmenamelist;

    scandirres = scandir("/dev", &danamelist, da_filter, alphasort);
    if (scandirres > 0)
    {
        num_da_devs = C_CAST(uint32_t, scandirres);
    }
    scandirres = scandir("/dev", &adanamelist, ada_filter, alphasort);
    if (scandirres > 0)
    {
        num_ada_devs = C_CAST(uint32_t, scandirres);
    }
    scandirres = scandir("/dev", &nvmenamelist, nvme_filter, alphasort);
    if (scandirres > 0)
    {
        num_nvme_devs = C_CAST(uint32_t, scandirres);
    }
    uint32_t totalDevs = num_da_devs + num_ada_devs + num_nvme_devs;

    char**   devs = M_REINTERPRET_CAST(char**, safe_calloc(totalDevs + 1, sizeof(char*)));
    uint32_t i    = UINT32_C(0);
    uint32_t j    = UINT32_C(0);
    uint32_t k    = UINT32_C(0);
    for (i = 0; i < num_da_devs; ++i)
    {
        size_t devNameStringLength = (safe_strlen("/dev/") + safe_strlen(danamelist[i]->d_name) + 1) * sizeof(char);
        devs[i]                    = M_REINTERPRET_CAST(char*, safe_malloc(devNameStringLength));
        snprintf_err_handle(devs[i], devNameStringLength, "/dev/%s", danamelist[i]->d_name);
        safe_free_dirent(&danamelist[i]);
    }
    for (j = 0; i < (num_da_devs + num_ada_devs) && j < num_ada_devs; ++i, j++)
    {
        size_t devNameStringLength = (safe_strlen("/dev/") + safe_strlen(adanamelist[j]->d_name) + 1) * sizeof(char);
        devs[i]                    = M_REINTERPRET_CAST(char*, safe_malloc(devNameStringLength));
        snprintf_err_handle(devs[i], devNameStringLength, "/dev/%s", adanamelist[j]->d_name);
        safe_free_dirent(&adanamelist[j]);
    }

    for (k = 0; i < (totalDevs) && k < num_nvme_devs; ++i, ++j, ++k)
    {
        size_t devNameStringLength = (safe_strlen("/dev/") + safe_strlen(nvmenamelist[k]->d_name) + 1) * sizeof(char);
        devs[i]                    = M_REINTERPRET_CAST(char*, safe_malloc(devNameStringLength));
        snprintf_err_handle(devs[i], devNameStringLength, "/dev/%s", nvmenamelist[k]->d_name);
        safe_free_dirent(&nvmenamelist[k]);
    }

    devs[i] = M_NULLPTR; // Added this so the for loop down doesn't cause a segmentation fault.
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &danamelist));
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &adanamelist));
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &nvmenamelist));

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
             ((driveNumber >= UINT32_C(0) && driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < totalDevs) &&
              found < numberOfDevices);
             ++driveNumber)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, CAM_DEV_NAME_LEN, 0, CAM_DEV_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, CAM_DEV_NAME_LEN, "%s", devs[driveNumber]);
            fd = -1;
            // lets try to open the device.
            fd = cam_get_device(name, d->os_info.name, sizeof(d->os_info.name), &d->os_info.fd);
            if (fd >= 0)
            {
                // Not sure this is necessary, but add back in if we find any issues - TJE
                /*if (d->os_info.cam_dev)
                {
                    cam_close_device(d->os_info.cam_dev);
                    d->os_info.cam_dev = M_NULLPTR;
                }*/
                eVerbosityLevels temp = d->deviceVerbosity;
                safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size     = ver.size;
                d->sanity.version  = ver.version;
                eReturnValues ret  = get_Device(name, d);
                if (ret != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                found++;
                d++;
            }
            else if (errno == EACCES) // quick fix for opening drives without sudo
            {
                ++permissionDeniedCount;
                failedGetDeviceCount++;
            }
            else
            {
                failedGetDeviceCount++;
            }
            // free the dev[deviceNumber] since we are done with it now.
            safe_free(&devs[driveNumber]);
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (permissionDeniedCount == (num_da_devs + num_ada_devs + num_nvme_devs))
        {
            returnValue = PERMISSION_DENIED;
        }
        else if (failedGetDeviceCount && returnValue != PERMISSION_DENIED)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    RESTORE_NONNULL_COMPARE
    safe_free(M_REINTERPRET_CAST(void**, &devs));
    return returnValue;
}

eReturnValues os_Read(M_ATTR_UNUSED tDevice* device,
                      M_ATTR_UNUSED uint64_t lba,
                      M_ATTR_UNUSED bool     forceUnitAccess,
                      M_ATTR_UNUSED uint8_t* ptrData,
                      M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Write(M_ATTR_UNUSED tDevice* device,
                       M_ATTR_UNUSED uint64_t lba,
                       M_ATTR_UNUSED bool     forceUnitAccess,
                       M_ATTR_UNUSED uint8_t* ptrData,
                       M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Verify(M_ATTR_UNUSED tDevice* device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Flush(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Device_Reset(tDevice* device)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    union ccb*    ccb = cam_getccb(device->os_info.cam_dev);
    if (ccb != M_NULLPTR)
    {
        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
        ccb->ccb_h.func_code = XPT_RESET_DEV;
        if (cam_send_ccb(device->os_info.cam_dev, ccb) >= 0)
        {
            if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) // maybe also this? CAM_SCSI_BUS_RESET
            {
                ret = SUCCESS;
            }
        }
        cam_freeccb(ccb);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

eReturnValues os_Bus_Reset(tDevice* device)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    union ccb*    ccb = cam_getccb(device->os_info.cam_dev);
    if (ccb != M_NULLPTR)
    {
        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
        ccb->ccb_h.func_code = XPT_RESET_BUS;
        if (cam_send_ccb(device->os_info.cam_dev, ccb) >= 0)
        {
            if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) // maybe also this? CAM_SCSI_BUS_RESET
            {
                ret = SUCCESS;
            }
        }
        cam_freeccb(ccb);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

eReturnValues os_Controller_Reset(M_ATTR_UNUSED tDevice* device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
#if defined(DISABLE_NVME_PASSTHROUGH)
    return OS_COMMAND_NOT_AVAILABLE;
#else // DISABLE_NVME_PASSTHROUGH
    eReturnValues ret         = SUCCESS;
    int           ioctlResult = 0;
    DECLARE_SEATIMER(commandTimer);
    struct nvme_get_nsid   gnsid;
    struct nvme_pt_command pt;
    safe_memset(&pt, sizeof(pt), 0, sizeof(pt));

    switch (nvmeIoCtx->commandType)
    {
        // NOLINTBEGIN(bugprone-branch-clone)
    case NVM_ADMIN_CMD:

        pt.cmd.opc   = nvmeIoCtx->cmd.adminCmd.opcode;
        pt.cmd.cdw10 = nvmeIoCtx->cmd.adminCmd.cdw10;
        pt.cmd.nsid  = nvmeIoCtx->cmd.adminCmd.nsid;
        pt.buf       = nvmeIoCtx->ptrData;
        pt.len       = nvmeIoCtx->dataSize;
        if (nvmeIoCtx->commandDirection == XFER_DATA_IN)
        {
            pt.is_read = 1;
        }
        else
        {
            pt.is_read = 0;
        }
        // pt.nvme_sqe.flags = nvmeIoCtx->cmd.adminCmd.flags;
        pt.cpl.rsvd1 = nvmeIoCtx->cmd.adminCmd.rsvd1;
        pt.cmd.rsvd2 = nvmeIoCtx->cmd.adminCmd.cdw2;
        pt.cmd.rsvd3 = nvmeIoCtx->cmd.adminCmd.cdw3;
        pt.cmd.mptr  = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.metadata));

        pt.cmd.cdw10 = nvmeIoCtx->cmd.adminCmd.cdw10;
        pt.cmd.cdw11 = nvmeIoCtx->cmd.adminCmd.cdw11;
        pt.cmd.cdw12 = nvmeIoCtx->cmd.adminCmd.cdw12;
        pt.cmd.cdw13 = nvmeIoCtx->cmd.adminCmd.cdw13;
        pt.cmd.cdw14 = nvmeIoCtx->cmd.adminCmd.cdw14;
        pt.cmd.cdw15 = nvmeIoCtx->cmd.adminCmd.cdw15;
        break;
    case NVM_CMD:
        pt.cmd.opc   = nvmeIoCtx->cmd.nvmCmd.opcode;
        pt.cmd.cdw10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
        ioctl(nvmeIoCtx->device->os_info.fd, NVME_GET_NSID, &gnsid);
        pt.cmd.nsid = gnsid.nsid;
        pt.buf      = nvmeIoCtx->ptrData;
        pt.len      = nvmeIoCtx->dataSize;
        if (nvmeIoCtx->commandDirection == XFER_DATA_IN)
        {
            pt.is_read = 1;
        }
        else
        {
            pt.is_read = 0;
        }
        // pt.nvme_sqe.flags = nvmeIoCtx->cmd.adminCmd.flags;
        pt.cpl.rsvd1 = nvmeIoCtx->cmd.nvmCmd.commandId;
        pt.cmd.rsvd2 = nvmeIoCtx->cmd.nvmCmd.cdw2;
        pt.cmd.rsvd3 = nvmeIoCtx->cmd.nvmCmd.cdw3;
        pt.cmd.mptr  = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.metadata));

        pt.cmd.cdw10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
        pt.cmd.cdw11 = nvmeIoCtx->cmd.nvmCmd.cdw11;
        pt.cmd.cdw12 = nvmeIoCtx->cmd.nvmCmd.cdw12;
        pt.cmd.cdw13 = nvmeIoCtx->cmd.nvmCmd.cdw13;
        pt.cmd.cdw14 = nvmeIoCtx->cmd.nvmCmd.cdw14;
        pt.cmd.cdw15 = nvmeIoCtx->cmd.nvmCmd.cdw15;
        break;
        // NOLINTEND(bugprone-branch-clone)
    default:
        return BAD_PARAMETER;
        break;
    }

    start_Timer(&commandTimer);
    ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_PASSTHROUGH_CMD, &pt);
    stop_Timer(&commandTimer);
    if (ioctlResult < 0)
    {
        nvmeIoCtx->device->os_info.last_error = errno;
        ret                                   = OS_PASSTHROUGH_FAILURE;
        printf("\nError : %d", nvmeIoCtx->device->os_info.last_error);
        printf("Error %s\n", strerror(C_CAST(int, nvmeIoCtx->device->os_info.last_error)));
        printf("\n OS_PASSTHROUGH_FAILURE. ");
        print_Errno_To_Screen(C_CAST(int, nvmeIoCtx->device->os_info.last_error));
    }
    else
    {
        // Fill the nvme CommandCompletionData
        nvmeIoCtx->commandCompletionData.dw0 = pt.cpl.cdw0;
        nvmeIoCtx->commandCompletionData.dw1 = pt.cpl.rsvd1;
        nvmeIoCtx->commandCompletionData.dw2 = M_WordsTo4ByteValue(pt.cpl.sqid, pt.cpl.sqhd);
        // NOTE: This ifdef may require more finite tuning using these version values:
        // https://docs.freebsd.org/en_US.ISO8859-1/books/porters-handbook/versions-11.html
#    if IS_FREEBSD_VERSION(12, 0, 0)
        // FreeBSD 11.4 and later didn't use a structure for the status completion data, but a uint16 type which made
        // this easy
        nvmeIoCtx->commandCompletionData.dw3 = M_WordsTo4ByteValue(pt.cpl.status, pt.cpl.cid);
#    else
        // FreeBSD 11.3 or earlier with NVMe support used a structure, so we need to copy to a temp variable to get
        // around this compiler error. This is not portable, but according to the FreeBSD source tree, the change away
        // from a bitfield struct was done to support big endian FreeBSD, so this SHOULD be ok to keep like this.
        uint16_t temp = UINT16_C(0);
        safe_memcpy(&temp, sizeof(uint16_t), &pt.cpl.status, sizeof(uint16_t));
        nvmeIoCtx->commandCompletionData.dw3 = M_WordsTo4ByteValue(temp, pt.cpl.cid);
#    endif
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
        nvmeIoCtx->commandCompletionData.dw1Valid = true;
        nvmeIoCtx->commandCompletionData.dw2Valid = true;
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
    }

    if (nvmeIoCtx->device->delay_io)
    {
        delay_Milliseconds(nvmeIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", nvmeIoCtx->device->delay_io);
        }
    }
    return ret;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Reset(tDevice* device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret           = OS_PASSTHROUGH_FAILURE;
    int           handleToReset = device->os_info.fd;
    DECLARE_SEATIMER(commandTimer);
    int ioRes = 0;

    start_Timer(&commandTimer);
    ioRes = ioctl(handleToReset, NVME_RESET_CONTROLLER);
    stop_Timer(&commandTimer);

    device->drive_info.lastCommandTimeNanoSeconds             = get_Nano_Seconds(commandTimer);
    device->drive_info.lastNVMeResult.lastNVMeStatus          = 0;
    device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = 0;

    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }

    if (ioRes < 0)
    {
        // failed
        device->os_info.last_error = errno;
        if (device->deviceVerbosity > VERBOSITY_COMMAND_VERBOSE && device->os_info.last_error != 0)
        {
            printf("Error :");
            print_Errno_To_Screen(C_CAST(int, device->os_info.last_error));
        }
    }
    else
    {
        // success
        ret = SUCCESS;
    }

    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Subsystem_Reset(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues pci_Read_Bar_Reg(M_ATTR_UNUSED tDevice* device,
                               M_ATTR_UNUSED uint8_t* pData,
                               M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Lock_Device(M_ATTR_UNUSED tDevice* device)
{
    // There is nothing to lock since you cannot open a CAM device with O_NONBLOCK
    return SUCCESS;
}

eReturnValues os_Unlock_Device(M_ATTR_UNUSED tDevice* device)
{
    // There is nothing to unlock since you cannot open a CAM device with O_NONBLOCK
    return SUCCESS;
}

// For the file syste cache update and unmount, these two functions may be useful:
// getfsstat ???
// https://www.freebsd.org/cgi/man.cgi?query=getfsstat&sektion=2&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports statfs
// ??? https://www.freebsd.org/cgi/man.cgi?query=statfs&sektion=2&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports fstab
// ??? https://www.freebsd.org/cgi/man.cgi?query=fstab&sektion=5&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports This
// looks very similar to the Linux getmntent: getfsent
// ???https://www.freebsd.org/cgi/man.cgi?query=getfsent&sektion=3&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports

eReturnValues os_Update_File_System_Cache(M_ATTR_UNUSED tDevice* device)
{
    // TODO: I have not found an analog to Linux which is usually the most helpful for figuring out what to do.
    //       I haven't found any other API or IOCTL that reloads the partition table on the disk (which is pretty close)
    //       Only thing that might work is a reload of an already mounted FS? At least that's a best guess.
    //       I will need a better test setup to validate this than I currently have access to - TJE
    // MNT_RELOAD ???
    // sys/disk.h provides lots of info about the device. One of the IOCTLs may allow for a reread of the partition
    // table.
    return NOT_SUPPORTED;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Unmount_File_Systems_On_Device(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    partitionCount               = get_Partition_Count(device->os_info.name);
#if defined(_DEBUG)
    printf("Partition count for %s = %d\n", device->os_info.name, partitionCount);
#endif
    if (partitionCount > 0)
    {
        ptrsPartitionInfo parts =
            M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts != M_NULLPTR)
        {
            if (SUCCESS == get_Partition_List(device->os_info.name, parts, partitionCount))
            {
                int iter = 0;
                for (; iter < partitionCount; ++iter)
                {
                    // since we found a partition, set the "has file system" bool to true
#if defined(_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    // Now that we have a name, unmount the file system
                    // unmount is more line Linux unmount2
                    // https://www.freebsd.org/cgi/man.cgi?query=unmount&sektion=2&apropos=0&manpath=FreeBSD+13.0-RELEASE
                    if (0 > unmount((parts + iter)->mntPath, MNT_FORCE))
                    {
                        ret                        = FAILURE;
                        device->os_info.last_error = errno;
                        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
                        {
                            printf("Unable to unmount %s: \n", (parts + iter)->mntPath);
                            print_Errno_To_Screen(errno);
                            printf("\n");
                        }
                    }
                }
            }
            safe_free_spartioninfo(&parts);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}
