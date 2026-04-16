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
#include "cam_helper.h"
#include "nix_mounts.h"
#include "nvme_helper_func.h"
#include "posix_common_lowlevel.h"
#include "sat_helper_func.h"
#include "scsi_helper_func.h"
#include "sntl_helper.h"
#include "usb_hacks.h"
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/ucred.h>
#if !defined(DISABLE_NVME_PASSTHROUGH)
#    include <dev/nvme/nvme.h>
#endif // DISABLE_NVME_PASSTHROUGH

#if defined(__DragonFly__)
#    include <sys/nata.h>
#else
#    include <sys/ata.h>
#endif

extern bool validate_Device_Struct(versionBlock);

#if !defined(CCB_CLEAR_ALL_EXCEPT_HDR)
// This is defined in newer versions of cam in FreeBSD, and is really useful.
// This is being redefined here in case it is missing for backwards compatibility with old FreeBSD versions
#    define CCB_CLEAR_ALL_EXCEPT_HDR(ccbp)                                                                             \
        safe_memset((char*)(ccbp) + sizeof((ccbp)->ccb_h), sizeof(*(ccbp)) - sizeof((ccbp)->ccb_h), 0,                 \
                    sizeof(*(ccbp)) - sizeof((ccbp)->ccb_h))
#endif

// If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise
// you must try MAX_CMD_TIMEOUT_SECONDS instead
OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

static bool is_NVMe_Handle(const char* handle)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    bool isNVMeDevice = false;
    if (handle && safe_strlen(handle))
    {
        if (strstr(handle, "nvme"))
        {
            isNVMeDevice = true;
        }
    }
    return isNVMeDevice;
#else
    M_USE_UNUSED(handle);
    return false;
#endif // !DISABLE_NVME_PASSTHROUGH
}

#if defined(IOCATAREQUEST)
static bool is_ad_device(const char* handle)
{
    if (strncmp("/dev/ad", handle, 7) == 0 && strncmp("/dev/ada", handle, 7) != 0)
    {
        return true;
    }
    return false;
}
#endif // IOCATAREQUEST

static bool is_ATA_CAM_Available(void)
{
    static bool checked   = false;
    static bool available = false;
#if IS_FREEBSD_VERSION(8, 0, 0)
    // https://people.freebsd.org/~mav/ata-cam_final_en.pdf
    if (!checked)
    {
        if (feature_present("ata_cam"))
        {
            available = true;
        }
        checked = true;
    }
#else
    M_USE_UNUSED(checked);
    M_USE_UNUSED(available);
#endif
    return available;
}

#if defined(IOCATAREQUEST)
#    define AD_OPEN_ATTEMPTS_MAX 2

static eReturnValues get_Legacy_ATA_Device(const char* filename, tDevice* device)
{
    eReturnValues ret          = SUCCESS;
    char*         deviceHandle = M_NULLPTR;

    ret = posix_Resolve_Filename_Link(filename, &deviceHandle);
    if (ret != SUCCESS)
    {
        free_Posix_Resolved_Filename(&deviceHandle);
        return ret;
    }

    ePosixHandleFlags handleFlags = POSIX_HANDLE_FLAGS_DEFAULT;
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

    set_Device_IO_Minimum_Alignment(device, sizeof(void*));

    set_Device_DriveType(device, ATA_DRIVE);
    set_Device_InterfaceType(device, IDE_INTERFACE);
    set_Device_MediaType(device, MEDIA_HDD);
#    if defined(__DragonFly__)
    device->os_info.osType = OS_DRAGONFLYBSD;
#    else
    device->os_info.osType = OS_FREEBSD;
#    endif

    device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported   = true;
    device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = INT_MAX;

    if (device->dFlags == OPEN_HANDLE_ONLY)
    {
        return ret;
    }

    free_Posix_Resolved_Filename(&deviceHandle);

    set_Device_Partition_Info(&device->os_info.fileSystemInfo, get_Device_Handle_Name(device));
    ret = fill_Drive_Info_Data(device);
    return ret;
}
#endif // IOCATAREQUEST

#if !defined(DISABLE_NVME_PASSTHROUGH)
static eReturnValues get_NVMe_Device(const char* filename, tDevice* M_NONNULL device)
{
    struct nvme_get_nsid gnsid;
    eReturnValues        ret          = SUCCESS;
    char*                deviceHandle = M_NULLPTR;

    ret = posix_Resolve_Filename_Link(filename, &deviceHandle);
    if (ret != SUCCESS)
    {
        free_Posix_Resolved_Filename(&deviceHandle);
        return ret;
    }

    device->os_info.cam_dev       = M_NULLPTR;
    ePosixHandleFlags handleFlags = POSIX_HANDLE_FLAGS_DEFAULT;
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

    set_Device_IO_Minimum_Alignment(device, sizeof(void*));

    set_Device_DriveType(device, NVME_DRIVE);
    set_Device_InterfaceType(device, NVME_INTERFACE);
    set_Device_MediaType(device, MEDIA_NVM);
    // ret = ioctl(device->os_info.fd, NVME_IOCTL_ID)
    // if ( ret < 0 )
    //{
    //     perror("nvme_ioctl_id");
    //	  return ret;
    // }
    ioctl(device->os_info.fd, NVME_GET_NSID, &gnsid);
    device->drive_info.namespaceID = gnsid.nsid;
#    if defined(__DragonFly__)
    device->os_info.osType = OS_DRAGONFLYBSD;
#    else
    device->os_info.osType = OS_FREEBSD;
#    endif

    char* baseLink = basename(deviceHandle);
    // Now we will set up the device name, etc fields in the os_info structure
    set_Device_Handle_Name(device, deviceHandle);
    set_Device_Handle_Friendly_Name(device, baseLink);
    set_Device_Partition_Info(&device->os_info.fileSystemInfo, get_Device_Handle_Name(device));

    ret = fill_Drive_Info_Data(device);

    safe_free(&deviceHandle);
    return ret;
}
#endif // DISABLE_NVME_PASSTHROUGH

static eReturnValues get_CAM_Device(const char* filename, tDevice* M_NONNULL device)
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

    if (cam_get_device(filename, devName, 20, &devUnit) == -1)
    {
        ret                = FAILURE;
        device->os_info.fd = -1;
        printf("%s failed\n", __FUNCTION__);
    }
    else
    {
        // O_NONBLOCK is not allowed
        int handleFlags = O_RDWR;
        int attempts    = 0;
#define CAM_OPEN_MAX_ATTEMPTS 2
        if (device->dFlags & HANDLE_RECOMMEND_EXCLUSIVE_ACCESS || device->dFlags & HANDLE_REQUIRE_EXCLUSIVE_ACCESS)
        {
            handleFlags |= O_EXCL;
        }
        do
        {
            ++attempts;
            device->os_info.cam_dev = cam_open_spec_device(devName, devUnit, handleFlags, M_NULLPTR);
            // NOTE: Checking for errno like we have below is not really documented.
            //       If you read through the cam code in the kernel though, you can find that it calls "open"
            //       within this function. Open can report these errors and if we do get a failure it is
            //       most likely from the open failing. It can fail for other things inside this function
            //       but this will be ok. -TJE
            if (device->os_info.cam_dev == M_NULLPTR)
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
                    safe_free(&deviceHandle);
                    return PERMISSION_DENIED;
                }
                else if (device->os_info.last_error == EBUSY)
                {
                    safe_free(&deviceHandle);
                    return DEVICE_BUSY;
                }
                else if (device->os_info.last_error == ENOENT || device->os_info.last_error == ENODEV)
                {
                    safe_free(&deviceHandle);
                    return DEVICE_INVALID;
                }
                else
                {
                    safe_free(&deviceHandle);
                    return FAILURE;
                }
            }
            else
            {
                break;
            }
        } while (attempts < CAM_OPEN_MAX_ATTEMPTS);

        if (device->os_info.cam_dev != M_NULLPTR)
        {
            // Set name and friendly name
            // name
            set_Device_Handle_Name(device, filename);
            // friendly name
            DECLARE_ZERO_INIT_ARRAY(char, formatFriendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH)
            snprintf_err_handle(formatFriendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s%d", devName, devUnit);
            set_Device_Handle_Friendly_Name(device, formatFriendlyName);

            device->os_info.fd = devUnit;

            if (handleFlags & O_EXCL)
            {
                set_Device_Handle_Open_Flags(device, HANDLE_FLAGS_EXCLUSIVE);
            }
            else
            {
                set_Device_Handle_Open_Flags(device, HANDLE_FLAGS_DEFAULT);
            }

// set the OS Type
#if defined(__DragonFly__)
            device->os_info.osType = OS_DRAGONFLYBSD;
#else
            device->os_info.osType = OS_FREEBSD;
#endif
            set_Device_IO_Minimum_Alignment(device, sizeof(void*));

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
                        set_Device_DriveType(device, SCSI_DRIVE);
                        set_Device_InterfaceType(device, SCSI_INTERFACE);
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
                                    set_Device_DriveType(device, ATA_DRIVE);
                                    set_Device_InterfaceType(
                                        device, IDE_INTERFACE); // Seeing IDE may look strange, but that is how old code
                                                                // was written to identify an ATA interface regardless
                                                                // of parallel or serial.
                                    break;
                                case XPORT_USB:
                                    set_Device_InterfaceType(device, USB_INTERFACE);
                                    break;
                                case XPORT_SPI:
                                    set_Device_InterfaceType(device, SCSI_INTERFACE);
                                    // firewire is reported as SPI.
                                    // Check hba_vid for "SBP" to tell the difference and set the proper interface
                                    if (strncmp(cpi.hba_vid, "SBP", 3) == 0 &&
                                        cpi.hba_misc &
                                            (PIM_NOBUSRESET | PIM_NO_6_BYTE)) // or check initiator_id? That's defined
                                                                              // but not sure if that could be confused
                                                                              // with other SPI devices - TJE
                                    {
                                        set_Device_InterfaceType(device, IEEE_1394_INTERFACE);
                                        // TODO: Figure out where to get device unique firewire IDs for specific
                                        // device compatibility lookups
                                    }
                                    break;
#if defined(XPORT_IS_NVME)
                                case XPORT_NVME:
                                    set_Device_DriveType(device, NVME_DRIVE);
                                    set_Device_InterfaceType(device, NVME_INTERFACE);
                                    device->drive_info.namespaceID = cpi.xport_specific.nvme.nsid;
                                    break;
#    if IS_FREEBSD_VERSION(15, 0, 0)
                                case XPORT_NVMF:
                                    set_Device_DriveType(device, NVME_DRIVE);
                                    set_Device_InterfaceType(device, NVME_INTERFACE);
                                    device->drive_info.namespaceID = cpi.xport_specific.nvmf.nsid;
                                    break;
#    endif // IS_FREEBSD_VERSION(15,0,0)
#endif     // XPORT_IS_NVME
                                case XPORT_SAS:
                                // case XPORT_ISCSI: //Only in freeBSD. Since this falls into default, just
                                // commenting it out - TJE
                                case XPORT_SSA:
                                case XPORT_FC:
                                case XPORT_UNSPECIFIED:
                                case XPORT_UNKNOWN:
                                default:
                                    set_Device_InterfaceType(device, SCSI_INTERFACE);
                                    break;
                                }
                                // TODO: Parse other flags to set hacks and capabilities to help with adapter or
                                // interface specific limitations
                                //        - target flags which may help identify device capabilities (no 6-byte
                                //        commands, group 6 & 7 command support, ata_ext request support.
                                //        - target flag that says no 6-byte commands can help uniquely identify
                                //        IEEE1394 devices
                                //       These should be saved later when we run into compatibility issues or need
                                //       to make other improvements. For now, getting the interface is a huge help

#if IS_FREEBSD_VERSION(9, 0, 0)
                                if (get_Device_InterfaceType(device) != USB_INTERFACE &&
                                    get_Device_InterfaceType(device) != IEEE_1394_INTERFACE)
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
                                        // NOT: Subvendor and subdevice seem to specify something further up the
                                        // tree from the adapter itself.
                                    }
                                }
#endif
                            }
                            else
                            {
                                print_str("WARN: XPT_PATH_INQ I/O status failed\n");
                            }
                        }
                        // let the library now go out and set up the device struct after sending some commands.
                        if (get_Device_InterfaceType(device) == USB_INTERFACE ||
                            get_Device_InterfaceType(device) == IEEE_1394_INTERFACE)
                        {
                            // TODO: Actually get the VID and PID set before calling this.
                            //       This will require some more research, but we should be able to do this now that
                            //       we know the interface
                            setup_Passthrough_Hacks_By_ID(device);
                        }
                        set_Device_Partition_Info(&device->os_info.fileSystemInfo, get_Device_Handle_Name(device));
                        ret = fill_Drive_Info_Data(device);
                    }
                    else
                    {
                        print_str("WARN: XPT_GDEV_TYPE I/O status failed\n");
                        ret = FAILURE;
                    }
                }
                else
                {
                    print_str("WARN: XPT_GDEV_TYPE I/O failed\n");
                    ret = FAILURE;
                }
            }
            else
            {
                print_str("WARN: Could not allocate CCB\n");
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

M_PARAM_RW(2)
OPENSEA_TRANSPORT_API eReturnValues get_Device(const char* M_NONNULL filename, tDevice* M_NONNULL device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    if (is_NVMe_Handle(filename))
    {
        return get_NVMe_Device(filename, device);
    }
#endif //! DISABLE_NVME_PASSTHROUGH
#if defined(IOCATAREQUEST)
    if (is_ad_device(filename) && !is_ATA_CAM_Available())
    {
        return get_Legacy_ATA_Device(filename, device);
    }
#endif // IOCATAREQUEST
    return get_CAM_Device(filename, device);
}

// This function depends on the caller already putting the RTFR's into the ata structure.
// This just creates a single simple place to translate the error back for the upper layers that expect a sense data
// structure -TJE
static M_INLINE void set_ATA_PT_Sense_Data(ScsiIoCtx* scsiIoCtx)
{
    if (scsiIoCtx != M_NULLPTR)
    {
        if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >= 22) // check that the sense data buffer is big enough to fill in
                                                // our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format   = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = SENSE_KEY_RECOVERED_ERROR;
                // setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc  = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                // now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = SENSE_KEY_RECOVERED_ERROR;
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
                    scsiIoCtx->psense[12] = scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt; // Sector Count Ext
                    scsiIoCtx->psense[14] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLowExt; // LBA Lo Ext
                    scsiIoCtx->psense[16] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMidExt; // LBA Mid Ext
                    scsiIoCtx->psense[18] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHiExt;  // LBA Hi
                }
                // fill in the returned 28bit registers
                scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;  // Error
                scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt; // Sector Count
                scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow; // LBA Lo
                scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid; // LBA Mid
                scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;  // LBA Hi
                scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device; // Device/Head
                scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status; // Status
            }
        }
    }
}

#if defined(IOCATAREQUEST)
// For legacy ATA disk devices only right now.
// If we wanted to support ATAPI we need to check protocol for packet to copy the CDB into place instead.
// This is not done right now since this code focusses on disks. - TJE
static eReturnValues send_Legacy_ATA_PT(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues          ret = SUCCESS;
    struct ata_ioc_request atareq;
    DECLARE_SEATIMER(commandTimer);
    if ((scsiIoCtx->pAtaCmdOpts->commandType > ATA_CMD_TYPE_TASKFILE && scsiIoCtx->pAtaCmdOpts->tfr.Feature48 != 0) ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_FPDMA ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_QUE ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DEV_RESET ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_HARD_RESET ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET_DMA ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UDMA ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UNKNOWN ||
        scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_MAX_VALUE || scsiIoCtx->pAtaCmdOpts->dataSize > INT_MAX)
    {
        // There is some support for ext commands.
        // in the ata-lowlevel.c it checks and converts certain 28bit commands to 48 bit as needed in
        // ata_modify_if_48bit() which is in ata-all.c This is part of the same path that handles the IOCTL, so extended
        // commands should be possible, but only PIO mode since it is not possible to set the DMA flag with the IOCTL.
        // https://github.com/freebsd/freebsd-src/blob/release/8.0.0/sys/dev/ata/ata-all.c#L799
        // The other option is to set the 28 bit command field and make the kernel do the conversion.
        // Not sure whether that is a better option or not, so for now just leaving this as is to try sending the 48 bit
        // commands -TJE
        return OS_COMMAND_NOT_AVAILABLE;
    }
    safe_memset(&atareq, sizeof(struct ata_ioc_request), 0, sizeof(struct ata_ioc_request));
    atareq.u.ata.command = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
    atareq.u.ata.feature = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
    if (scsiIoCtx->pAtaCmdOpts->commandType > ATA_CMD_TYPE_TASKFILE)
    {
        atareq.u.ata.lba =
            M_BytesTo8ByteValue(0, 0, scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48,
                                scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48, scsiIoCtx->pAtaCmdOpts->tfr.LbaHi,
                                scsiIoCtx->pAtaCmdOpts->tfr.LbaMid, scsiIoCtx->pAtaCmdOpts->tfr.LbaLow);
        atareq.u.ata.count =
            M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount);
    }
    else
    {
        // for 28bit commands this has no separate register for device/head
        // so need to take the LBA value from device head to append to the "LBA" field and let the kernel handle setting
        // the register correctly
        atareq.u.ata.lba =
            M_BytesTo4ByteValue(M_Nibble0(scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead), scsiIoCtx->pAtaCmdOpts->tfr.LbaHi,
                                scsiIoCtx->pAtaCmdOpts->tfr.LbaMid, scsiIoCtx->pAtaCmdOpts->tfr.LbaLow);
        atareq.u.ata.count = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
    }

    atareq.flags                 = ATA_CMD_CONTROL;
    uint32_t       ataTimeout    = scsiIoCtx->pAtaCmdOpts->timeout;
    const uint32_t deviceTimeout = get_tDevice_Default_Command_Timeout(scsiIoCtx->device);
    if (deviceTimeout > 0 && deviceTimeout > scsiIoCtx->pAtaCmdOpts->timeout)
    {
        ataTimeout = deviceTimeout;
        // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
        // that we DON'T do a conversion and leave the time as the max...
        if (deviceTimeout >= INT_MAX)
        {
            ataTimeout = INT_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->pAtaCmdOpts->timeout != UINT32_C(0))
        {
            ataTimeout = scsiIoCtx->pAtaCmdOpts->timeout;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
            // security) that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->pAtaCmdOpts->timeout >= INT_MAX)
            {
                ataTimeout = INT_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            ataTimeout = DEFAULT_COMMAND_TIMEOUT; // default to 15 second timeout
        }
    }
    atareq.timeout = M_STATIC_CAST(int, ataTimeout);

    switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
    {
    case XFER_NO_DATA:
        atareq.data  = M_NULLPTR;
        atareq.count = 0;
        break;
    case XFER_DATA_IN:
        atareq.flags |= ATA_CMD_READ;
        atareq.data  = M_REINTERPRET_CAST(caddr_t, scsiIoCtx->pAtaCmdOpts->ptrData);
        atareq.count = M_STATIC_CAST(int, scsiIoCtx->pAtaCmdOpts->dataSize);
        break;
    case XFER_DATA_OUT:
        atareq.flags |= ATA_CMD_WRITE;
        atareq.data  = M_REINTERPRET_CAST(caddr_t, scsiIoCtx->pAtaCmdOpts->ptrData);
        atareq.count = M_STATIC_CAST(int, scsiIoCtx->pAtaCmdOpts->dataSize);
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return OS_COMMAND_NOT_AVAILABLE;
    }

    start_Timer(&commandTimer);
    int iocres = ioctl(scsiIoCtx->device->os_info.fd, IOCATAREQUEST, &atareq);
    stop_Timer(&commandTimer);
    if (iocres < 0)
    {
        perror("error sending legacy ATA I/O");
        ret = OS_PASSTHROUGH_FAILURE;
        set_Device_Last_Error(scsiIoCtx->device, errno);
    }
    else
    {
        // copy back output registers
        scsiIoCtx->pAtaCmdOpts->rtfr.status = atareq.u.ata.command;
        scsiIoCtx->pAtaCmdOpts->rtfr.error  = atareq.u.ata.feature;
        if (scsiIoCtx->pAtaCmdOpts->commandType > ATA_CMD_TYPE_TASKFILE)
        {
            scsiIoCtx->pAtaCmdOpts->rtfr.lbaHiExt  = M_Byte5(atareq.u.ata.lba);
            scsiIoCtx->pAtaCmdOpts->rtfr.lbaMidExt = M_Byte4(atareq.u.ata.lba);
            scsiIoCtx->pAtaCmdOpts->rtfr.lbaLowExt = M_Byte3(atareq.u.ata.lba);
            scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt = M_Byte1(atareq.u.ata.count);
        }
        else
        {
            scsiIoCtx->pAtaCmdOpts->rtfr.lbaHiExt  = UINT8_C(0);
            scsiIoCtx->pAtaCmdOpts->rtfr.lbaMidExt = UINT8_C(0);
            scsiIoCtx->pAtaCmdOpts->rtfr.lbaLowExt = UINT8_C(0);
            scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt = UINT8_C(0);
            scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt = UINT8_C(0);
            // 28 bit commands put head/last 4 bits of LBA here so need to get them back this way - TJE
            scsiIoCtx->pAtaCmdOpts->rtfr.device = M_Nibble0(M_Byte3(atareq.u.ata.lba));
        }
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi  = M_Byte2(atareq.u.ata.lba);
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid = M_Byte1(atareq.u.ata.lba);
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow = M_Byte0(atareq.u.ata.lba);
        scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = M_Byte0(atareq.u.ata.count);
        set_ATA_PT_Sense_Data(scsiIoCtx);
    }
    set_tDevice_Last_Command_Completion_Time_NS(scsiIoCtx->device, get_Nano_Seconds(commandTimer));
    return ret;
}
#endif // IOCATAREQUEST

#if !defined(__DragonFly__)
OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues send_Ata_Cam_IO(ScsiIoCtx* M_NONNULL scsiIoCtx)
{
    eReturnValues     ret       = SUCCESS;
    union ccb*        ccb       = M_NULLPTR;
    struct ccb_ataio* ataio     = M_NULLPTR;
    u_int32_t         direction = UINT32_C(0);

    ccb = cam_getccb(scsiIoCtx->device->os_info.cam_dev);

    if (ccb != M_NULLPTR)
    {
        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
        ataio = &ccb->ataio;

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

        uint32_t       camTimeout    = scsiIoCtx->timeout;
        const uint32_t deviceTimeout = get_tDevice_Default_Command_Timeout(scsiIoCtx->device);
        if (deviceTimeout > 0 && deviceTimeout > scsiIoCtx->timeout)
        {
            camTimeout = deviceTimeout;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (deviceTimeout < CAM_MAX_CMD_TIMEOUT_SECONDS)
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
                camTimeout = DEFAULT_COMMAND_TIMEOUT * UINT32_C(1000); // default to 15 second timeout
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
#    if defined(ATA_FLAG_AUX) || defined(ATA_FLAG_ICC)
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
#        if defined(ATA_FLAG_ICC)
                    // can set ICC
                    ataio->ata_flags |= ATA_FLAG_ICC;
                    ataio->icc = scsiIoCtx->pAtaCmdOpts->tfr.icc;
#        else
                    // cannot set ICC field
                    ret = OS_COMMAND_NOT_AVAILABLE;
#        endif // ATA_FLAG_ICC
                }
                if (scsiIoCtx->pAtaCmdOpts->tfr.aux1 || scsiIoCtx->pAtaCmdOpts->tfr.aux2 ||
                    scsiIoCtx->pAtaCmdOpts->tfr.aux3 || scsiIoCtx->pAtaCmdOpts->tfr.aux4)
                {
#        if defined(ATA_FLAG_AUX)
                    // can set AUX
                    ataio->ata_flags |= ATA_FLAG_AUX;
                    ataio->aux =
                        M_BytesTo4ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.aux4, scsiIoCtx->pAtaCmdOpts->tfr.aux3,
                                            scsiIoCtx->pAtaCmdOpts->tfr.aux2, scsiIoCtx->pAtaCmdOpts->tfr.aux1);
#        else
                    // cannot set AUX field
                    ret = OS_COMMAND_NOT_AVAILABLE;
#        endif // ATA_FLAG_ICC
                }
#    else  /* !AUX || !ICC*/
                // AUX and ICC are not available to be set in this version of freebsd
                ret = OS_COMMAND_NOT_AVAILABLE;
#    endif /* ATA_FLAG_AUX || ATA_FLAG_ICC */
            }
            else
            {
                ret = BAD_PARAMETER;
                print_str("WARN: Unsupported ATA Command type\n");
            }

            if (ret == SUCCESS)
            {
                DECLARE_SEATIMER(commandTimer);
#    if defined(_DEBUG)
                printf("ATAIO: cmd=0x%02" PRIX8 " feat=0x%02" PRIX8 " lbalow=0x%02" PRIX8 " lbamid=0x%02" PRIX8
                       " lbahi=0x%02" PRIX8 " sc=0x%02" PRIX8 "\n",
                       ataio->cmd.command, ataio->cmd.features, ataio->cmd.lba_low, ataio->cmd.lba_mid,
                       ataio->cmd.lba_high, ataio->cmd.sector_count);
                printf("\tfeatext=0x%02" PRIX8 " lbalowExp=0x%02" PRIX8 " lbamidExp=0x%02" PRIX8 " lbahiExp=0x%02" PRIX8
                       " scExp=0x%02" PRIX8 "\n",
                       ataio->cmd.features_exp, ataio->cmd.lba_low_exp, ataio->cmd.lba_mid_exp, ataio->cmd.lba_high_exp,
                       ataio->cmd.sector_count_exp);

                printf("\tData Ptr %p, xfer len %d\n", ataio->data_ptr, ataio->dxfer_len);
#    endif
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
                                print_str("WARN: I/O CAM_CMD_TIMEOUT occured\n");
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
#    if defined(_DEBUG)
                        printf("I/O went through status %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
#    endif
                    }
                    ret = SUCCESS;
                    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
                    {
                        if (ataio->res.flags & CAM_ATAIO_48BIT) /* check this flag to make sure we read valid data */
                        {
                            scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt = ataio->res.sector_count_exp; // Sector Count Ext
                            scsiIoCtx->pAtaCmdOpts->rtfr.lbaLowExt = ataio->res.lba_low_exp;      // LBA Lo Ext
                            scsiIoCtx->pAtaCmdOpts->rtfr.lbaMidExt = ataio->res.lba_mid_exp;      // LBA Mid Ext
                            scsiIoCtx->pAtaCmdOpts->rtfr.lbaHiExt  = ataio->res.lba_high_exp;     // LBA Hi
                        }
                        else
                        {
                            scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt = UINT8_C(0);
                            scsiIoCtx->pAtaCmdOpts->rtfr.lbaLowExt = UINT8_C(0);
                            scsiIoCtx->pAtaCmdOpts->rtfr.lbaMidExt = UINT8_C(0);
                            scsiIoCtx->pAtaCmdOpts->rtfr.lbaHiExt  = UINT8_C(0);
                        }
                    }
                    // fill in the returned 28bit registers
                    scsiIoCtx->pAtaCmdOpts->rtfr.error  = ataio->res.error;        // Error
                    scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = ataio->res.sector_count; // Sector Count
                    scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow = ataio->res.lba_low;      // LBA Lo
                    scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid = ataio->res.lba_mid;      // LBA Mid
                    scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi  = ataio->res.lba_high;     // LBA Hi
                    scsiIoCtx->pAtaCmdOpts->rtfr.device = ataio->res.device;       // Device/Head
                    scsiIoCtx->pAtaCmdOpts->rtfr.status = ataio->res.status;       // Status
                    set_ATA_PT_Sense_Data(scsiIoCtx);
                }
                set_tDevice_Last_Command_Completion_Time_NS(scsiIoCtx->device, get_Nano_Seconds(commandTimer));
            }
        }
        else
        {
            if (VERBOSITY_DEFAULT < scsiIoCtx->device->deviceVerbosity)
            {
                print_str("WARN: Sending non-ATA commnad to ATA Drive [FreeBSD CAM driver does not support SAT "
                          "Specification]\n");
            }
            ret = BAD_PARAMETER;
        }
        if (ccb != M_NULLPTR)
        {
            cam_freeccb(ccb);
        }
    }
    else
    {
        print_str("WARN: couldn't allocate CCB");
    }

    return ret;
}
#endif //__DragonFly__

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues send_Scsi_Cam_IO(ScsiIoCtx* M_NONNULL scsiIoCtx)
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
        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

        csio = &ccb->csio;

        csio->ccb_h.func_code        = XPT_SCSI_IO;
        csio->ccb_h.retry_count      = UINT32_C(0); // should we change it to 1?
        csio->ccb_h.cbfcnp           = M_NULLPTR;
        uint32_t       camTimeout    = scsiIoCtx->timeout;
        const uint32_t deviceTimeout = get_tDevice_Default_Command_Timeout(scsiIoCtx->device);
        if (deviceTimeout > UINT32_C(0) && deviceTimeout > scsiIoCtx->timeout)
        {
            camTimeout = deviceTimeout;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (deviceTimeout < CAM_MAX_CMD_TIMEOUT_SECONDS)
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
                camTimeout = DEFAULT_COMMAND_TIMEOUT * UINT32_C(1000); // default to 15 second timeout
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
#if !defined(__DragonFly__)
        case XFER_DATA_OUT_IN:
        case XFER_DATA_IN_OUT:
            csio->ccb_h.flags = CAM_DIR_BOTH;
            break;
#endif // __DragonFly
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

        safe_memcpy(&csio->cdb_io.cdb_bytes[0], IOCDBLEN, &scsiIoCtx->cdb[CDB_OPERATION_CODE], IOCDBLEN);
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
            //     if (get_Device_DriveType(scsiIoCtx->device) == ATA_DRIVE)
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
                // NOTE: for portability between dragonfly and freebsd, point to the structure rather than the contents
                // to copy
                safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, &csio->sense_data, SSD_FULL_SIZE);
            }
        }
        set_tDevice_Last_Command_Completion_Time_NS(scsiIoCtx->device, get_Nano_Seconds(commandTimer));
    }
    else
    {
        print_str("ccb is Null\n");
        ret = BAD_PARAMETER; // Should this be MEMORY FAILURE?
    }

    cam_freeccb(ccb);

#if defined(_DEBUG)
    printf("<-- %s ret=[%d]\n", __FUNCTION__, ret);
#endif

    return ret;
}

M_PARAM_RO(1) eReturnValues send_IO(ScsiIoCtx* M_NONNULL scsiIoCtx)
{
    eReturnValues ret = FAILURE;
    // printf("%s -->\n",__FUNCTION__);

    switch (get_Device_InterfaceType(scsiIoCtx->device))
    {
    case NVME_INTERFACE:
        ret = sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        break;
    case IDE_INTERFACE:
#if defined(__DragonFly__)
#    if defined(IOCATAREQUEST)
        if (is_ad_device(get_Device_Handle_Name(scsiIoCtx->device)))
        {
            ret = send_Legacy_ATA_PT(scsiIoCtx);
            break;
        }
#    endif // IOCATAREQUEST
        // Dragonfly BSD has SCSI translation in ahci_cam.c
        M_FALLTHROUGH;
#else
        if (scsiIoCtx->pAtaCmdOpts)
        {
#    if defined(IOCATAREQUEST)
            if (is_ad_device(get_Device_Handle_Name(scsiIoCtx->device)) && !is_ATA_CAM_Available())
            {
                ret = send_Legacy_ATA_PT(scsiIoCtx);
            }
            else
#    endif // IOCATAREQUEST
            {
                ret = send_Ata_Cam_IO(scsiIoCtx);
            }
        }
        else
        {
            ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        }
        break;
#endif     //__DragonFly__
    case IEEE_1394_INTERFACE:
    case USB_INTERFACE:
    case SCSI_INTERFACE:
        ret = send_Scsi_Cam_IO(scsiIoCtx);
        break;
    case RAID_INTERFACE:
        if (scsiIoCtx->device->issue_io != M_NULLPTR)
        {
            ret = scsiIoCtx->device->issue_io(scsiIoCtx);
        }
        else
        {
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                print_str("No Raid PassThrough IO Routine present for this device\n");
            }
        }
        break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Target Device does not have a valid interface %d\n", scsiIoCtx->get_Device_InterfaceType(device));
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

#if !defined(DISABLE_NVME_PASSTHROUGH)
#    if !defined(XPORT_IS_NVME)
static int nvme_filter(const struct dirent* entry)
{
    int nvmeHandle = strncmp("nvme", entry->d_name, 4);
    if (nvmeHandle != 0)
    {
        return !nvmeHandle;
    }
    const char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !nvmeHandle;
    }
}
#    else
static int nda_filter(const struct dirent* entry)
{
    int nvmeHandle = strncmp("nda", entry->d_name, 3);
    if (nvmeHandle != 0)
    {
        return !nvmeHandle;
    }
    const char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !nvmeHandle;
    }
}
#    endif // !defined(XPORT_IS_NVME)
#endif     // !defined(DISABLE_NVME_PASSTHROUGH)

static int da_filter(const struct dirent* entry)
{
    int daHandle = strncmp("da", entry->d_name, 2);
    if (daHandle != 0)
    {
        return !daHandle;
    }
    const char* partition = strpbrk(entry->d_name, "pPsS");
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
    const char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !adaHandle;
    }
}

#if defined(IOCATAREQUEST)
// Legacy ATA Passthrough filter
// Only checking for ATA disks.
// adX = ata disk
// acdX = ata CD/DVD
// afd = ata floppy
// ast = ata tape
static int ad_filter(const struct dirent* entry)
{
    int adaHandle = strncmp("ad", entry->d_name, 2);
    if (adaHandle != 0)
    {
        return !adaHandle;
    }
    const char* partition = strpbrk(entry->d_name, "pPsS");
    if (partition != M_NULLPTR)
    {
        return 0;
    }
    else
    {
        return !adaHandle;
    }
}
#endif // IOCATAREQUEST

M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues close_Device(tDevice* dev)
{
    if (dev->os_info.cam_dev)
    {
        cam_close_device(dev->os_info.cam_dev);
        dev->os_info.cam_dev = M_NULLPTR;
    }
    else if (dev->os_info.fd > 0)
    {
        close(dev->os_info.fd);
        dev->os_info.last_error = errno;
        dev->os_info.fd         = -1;
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
M_PARAM_RW(1)
OPENSEA_TRANSPORT_API eReturnValues get_Device_Count(uint32_t* M_NONNULL numberOfDevices, M_ATTR_UNUSED uint64_t flags)
{
    int num_da_devs   = 0;
    int num_ada_devs  = 0;
    int num_nvme_devs = 0;

    struct dirent** danamelist   = M_NULLPTR;
    struct dirent** adanamelist  = M_NULLPTR;
    struct dirent** nvmenamelist = M_NULLPTR;

    num_da_devs = scandir("/dev", &danamelist, da_filter, alphasort);
    if (is_ATA_CAM_Available())
    {
        num_ada_devs = scandir("/dev", &adanamelist, ada_filter, alphasort);
    }
#if defined(IOCATAREQUEST)
    else
    {
        num_ada_devs = scandir("/dev", &adanamelist, ad_filter, alphasort);
    }
#endif // IOCATAREQUEST
#if !defined(DISABLE_NVME_PASSTHROUGH)
#    if defined(XPORT_IS_NVME)
    num_nvme_devs = scandir("/dev", &nvmenamelist, nda_filter, alphasort);
    if (num_nvme_devs == 0)
#    else
    {
        num_nvme_devs = scandir("/dev", &nvmenamelist, nvme_filter, alphasort);
    }
#    endif
#endif // DISABLE_NVME_PASSTHROUGH

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
M_PARAM_RW(1)
OPENSEA_TRANSPORT_API eReturnValues get_Device_List(tDevice* M_NONNULL const ptrToDeviceList,
                                                    uint32_t                 sizeInBytes,
                                                    versionBlock             ver,
                                                    M_ATTR_UNUSED uint64_t   flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    uint32_t      busyDevCount          = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, name, CAM_DEV_NAME_LEN);
    int      fd            = 0;
    tDevice* d             = M_NULLPTR;
    int      scandirres    = 0;
    uint32_t num_da_devs   = UINT32_C(0);
    uint32_t num_ada_devs  = UINT32_C(0);
    uint32_t num_nvme_devs = UINT32_C(0);

    struct dirent** danamelist   = M_NULLPTR;
    struct dirent** adanamelist  = M_NULLPTR;
    struct dirent** nvmenamelist = M_NULLPTR;

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

    scandirres = scandir("/dev", &danamelist, da_filter, alphasort);
    if (scandirres > 0)
    {
        num_da_devs = C_CAST(uint32_t, scandirres);
    }
    if (is_ATA_CAM_Available())
    {
        scandirres = scandir("/dev", &adanamelist, ada_filter, alphasort);
    }
#if defined(IOCATAREQUEST)
    else
    {
        scandirres = scandir("/dev", &adanamelist, ad_filter, alphasort);
    }
#endif // IOCATAREQUEST
    if (scandirres > 0)
    {
        num_ada_devs = C_CAST(uint32_t, scandirres);
    }
#if !defined(DISABLE_NVME_PASSTHROUGH)
#    if defined(XPORT_IS_NVME)
    scandirres = scandir("/dev", &nvmenamelist, nda_filter, alphasort);
    if (scandirres == 0)
#    else
    {
        scandirres = scandir("/dev", &nvmenamelist, nvme_filter, alphasort);
    }
#    endif
        if (scandirres > 0)
        {
            num_nvme_devs = C_CAST(uint32_t, scandirres);
        }
#endif // DISABLE_NVME_PASSTHROUGH
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
             ((driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < totalDevs) && found < numberOfDevices); ++driveNumber)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, CAM_DEV_NAME_LEN, 0, CAM_DEV_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, CAM_DEV_NAME_LEN, "%s", devs[driveNumber]);
            fd = -1;
            // lets try to open the device.
            if (is_NVMe_Handle(name)
#if defined(IOCATAREQUEST)
                || (is_ad_device(name) && !is_ATA_CAM_Available())
#endif // IOCATAREQUEST
            )
            {
                fd = open(name, O_RDWR);
            }
            else
            {
                fd = cam_get_device(name, d->os_info.name, sizeof(d->os_info.name), &d->os_info.fd);
            }
            if (fd >= 0)
            {
                if (is_NVMe_Handle(name)
#if defined(IOCATAREQUEST)
                    || (is_ad_device(name) && !is_ATA_CAM_Available())
#endif // IOCATAREQUEST
                )
                {
                    close(fd);
                }
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

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Device_Reset(const tDevice* M_NONNULL device)
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

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Bus_Reset(const tDevice* M_NONNULL device)
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

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Controller_Reset(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
#    if defined(XPORT_IS_NVME)

#        if !IS_FREEBSD_VERSION(14, 0, 0)
#            define CAM_NVME_STATUS_ERROR 0x20
#        endif

static eReturnValues send_CAM_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_SEATIMER(commandTimer);
    union ccb*         ccb    = M_NULLPTR;
    struct ccb_nvmeio* nvmeio = M_NULLPTR;

    ccb = cam_getccb(nvmeIoCtx->device->os_info.cam_dev);

    if (ccb != M_NULLPTR)
    {
        nvmeio = &ccb->nvmeio;
        CCB_CLEAR_ALL_EXCEPT_HDR(ccb);
        uint32_t       camFlags      = CAM_DEV_QFRZDIS;
        uint32_t       camTimeout    = nvmeIoCtx->timeout;
        const uint32_t deviceTimeout = get_tDevice_Default_Command_Timeout(nvmeIoCtx->device);
        if (deviceTimeout > 0 && deviceTimeout > nvmeIoCtx->timeout)
        {
            camTimeout = deviceTimeout;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (deviceTimeout < CAM_MAX_CMD_TIMEOUT_SECONDS)
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
            if (nvmeIoCtx->timeout != UINT32_C(0))
            {
                camTimeout = nvmeIoCtx->timeout;
                // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
                // security) that we DON'T do a conversion and leave the time as the max...
                if (nvmeIoCtx->timeout < CAM_MAX_CMD_TIMEOUT_SECONDS)
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
                camTimeout = DEFAULT_COMMAND_TIMEOUT * UINT32_C(1000); // default to 15 second timeout
            }
        }

        switch (nvmeIoCtx->commandDirection)
        {
        case XFER_DATA_IN:
            camFlags |= CAM_DIR_IN;
            break;
        case XFER_DATA_OUT:
            camFlags |= CAM_DIR_OUT;
            break;
        case XFER_NO_DATA:
            camFlags |= CAM_DIR_NONE;
            break;
        case XFER_DATA_IN_OUT:
        case XFER_DATA_OUT_IN:
            camFlags |= CAM_DIR_BOTH;
            break;
        }

        if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
        {
            nvmeio->cmd.opc   = nvmeIoCtx->cmd.adminCmd.opcode;
            nvmeio->cmd.fuse  = nvmeIoCtx->cmd.adminCmd.flags;
            nvmeio->cmd.cid   = 0; // this will be set somewhere below us in the driver/CAM layer
            nvmeio->cmd.nsid  = nvmeIoCtx->cmd.adminCmd.nsid;
            nvmeio->cmd.rsvd2 = nvmeIoCtx->cmd.adminCmd.cdw2;
            nvmeio->cmd.rsvd3 = nvmeIoCtx->cmd.adminCmd.cdw3;
            nvmeio->cmd.mptr  = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.metadata));
            nvmeio->cmd.prp1  = nvmeIoCtx->cmd.adminCmd.addr;
            nvmeio->cmd.prp2  = nvmeIoCtx->cmd.adminCmd.metadataLen;
            nvmeio->cmd.cdw10 = nvmeIoCtx->cmd.adminCmd.cdw10;
            nvmeio->cmd.cdw11 = nvmeIoCtx->cmd.adminCmd.cdw11;
            nvmeio->cmd.cdw12 = nvmeIoCtx->cmd.adminCmd.cdw12;
            nvmeio->cmd.cdw13 = nvmeIoCtx->cmd.adminCmd.cdw13;
            nvmeio->cmd.cdw14 = nvmeIoCtx->cmd.adminCmd.cdw14;
            nvmeio->cmd.cdw15 = nvmeIoCtx->cmd.adminCmd.cdw15;
            cam_fill_nvmeadmin(nvmeio, 0 /* retries */, M_NULLPTR, camFlags, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize,
                               camTimeout);
        }
        else
        {
            nvmeio->cmd.opc  = nvmeIoCtx->cmd.nvmCmd.opcode;
            nvmeio->cmd.fuse = nvmeIoCtx->cmd.nvmCmd.flags;
            nvmeio->cmd.cid =
                nvmeIoCtx->cmd.nvmCmd.commandId; // this will be set somewhere below us in the driver/CAM layer
            nvmeio->cmd.nsid  = nvmeIoCtx->cmd.nvmCmd.nsid;
            nvmeio->cmd.rsvd2 = nvmeIoCtx->cmd.nvmCmd.cdw2;
            nvmeio->cmd.rsvd3 = nvmeIoCtx->cmd.nvmCmd.cdw3;
            nvmeio->cmd.mptr  = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.metadata));
            nvmeio->cmd.prp1  = nvmeIoCtx->cmd.nvmCmd.prp1;
            nvmeio->cmd.prp2  = nvmeIoCtx->cmd.nvmCmd.prp2;
            nvmeio->cmd.cdw10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
            nvmeio->cmd.cdw11 = nvmeIoCtx->cmd.nvmCmd.cdw11;
            nvmeio->cmd.cdw12 = nvmeIoCtx->cmd.nvmCmd.cdw12;
            nvmeio->cmd.cdw13 = nvmeIoCtx->cmd.nvmCmd.cdw13;
            nvmeio->cmd.cdw14 = nvmeIoCtx->cmd.nvmCmd.cdw14;
            nvmeio->cmd.cdw15 = nvmeIoCtx->cmd.nvmCmd.cdw15;
            cam_fill_nvmeio(nvmeio, 0 /* retries */, M_NULLPTR, camFlags, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize,
                            camTimeout);
        }
        start_Timer(&commandTimer);
        int ioctlResult = cam_send_ccb(nvmeIoCtx->device->os_info.cam_dev, ccb);
        stop_Timer(&commandTimer);
        if (ioctlResult < 0)
        {
            perror("error sending NVMe I/O");
            cam_error_print(nvmeIoCtx->device->os_info.cam_dev, ccb, CAM_ESF_ALL /*error string flags*/, CAM_EPF_ALL,
                            stdout);
            ret = FAILURE;
        }
        else
        {
            // cam_error_print(nvmeIoCtx->device->os_info.cam_dev, ccb, CAM_ESF_ALL /*error string flags*/,
            // CAM_EPF_ALL, stdout);
            if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
            {
                if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_NVME_STATUS_ERROR)
                {
                    ret = COMMAND_FAILURE;
                }
                else if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_CMD_TIMEOUT)
                {
                    if (VERBOSITY_QUIET < nvmeIoCtx->device->deviceVerbosity)
                    {
                        print_str("WARN: I/O CAM_CMD_TIMEOUT occured\n");
                    }
                }
                else
                {
                    if (VERBOSITY_QUIET < nvmeIoCtx->device->deviceVerbosity)
                    {
                        printf("WARN: I/O error occurred %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
                    }
                }
            }
            else
            {
#        if defined(_DEBUG)
                printf("I/O went through status %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
#        endif
            }
            ret = SUCCESS;
            set_tDevice_Last_Command_Completion_Time_NS(nvmeIoCtx->device, get_Nano_Seconds(commandTimer));
            // Fill the nvme CommandCompletionData
            nvmeIoCtx->commandCompletionData.dw0      = nvmeio->cpl.cdw0;
            nvmeIoCtx->commandCompletionData.dw1      = nvmeio->cpl.rsvd1;
            nvmeIoCtx->commandCompletionData.dw2      = M_WordsTo4ByteValue(nvmeio->cpl.sqid, nvmeio->cpl.sqhd);
            nvmeIoCtx->commandCompletionData.dw3      = M_WordsTo4ByteValue(nvmeio->cpl.status, nvmeio->cpl.cid);
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
        if (ccb != M_NULLPTR)
        {
            cam_freeccb(ccb);
        }
    }
    else
    {
        ret = OS_PASSTHROUGH_FAILURE;
    }
    return ret;
}
#    endif // XPORT_IS_NVME

static eReturnValues send_IOCTL_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
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
        set_Device_Last_Error(nvmeIoCtx->device, errno);
        ret = OS_PASSTHROUGH_FAILURE;
        printf("\nError : %d", nvmeIoCtx->device->os_info.last_error);
        printf("Error %s\n", strerror(C_CAST(int, nvmeIoCtx->device->os_info.last_error)));
        print_str("\n OS_PASSTHROUGH_FAILURE. ");
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
}
#endif // DISABLE_NVME_PASSTHROUGH

M_PARAM_RW(1) eReturnValues send_NVMe_IO(nvmeCmdCtx* M_NONNULL nvmeIoCtx)
{
#if defined(DISABLE_NVME_PASSTHROUGH)
    M_USE_UNUSED(nvmeIoCtx);
    return OS_COMMAND_NOT_AVAILABLE;
#else // DISABLE_NVME_PASSTHROUGH
#    if defined(XPORT_IS_NVME)
    if (nvmeIoCtx->device->os_info.cam_dev != M_NULLPTR)
    {
        return send_CAM_NVMe_IO(nvmeIoCtx);
    }
    else
#    endif // XPORT_IS_NVME
    {
        return send_IOCTL_NVMe_IO(nvmeIoCtx);
    }
#endif     // DISABLE_NVME_PASSTHROUGH
}

M_PARAM_RO(1) eReturnValues os_nvme_Reset(const tDevice* M_NONNULL device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret           = OS_PASSTHROUGH_FAILURE;
    int           handleToReset = device->os_info.fd;
    DECLARE_SEATIMER(commandTimer);
    int ioRes = 0;
    if (device->os_info.cam_dev != M_NULLPTR)
    {
        handleToReset = device->os_info.cam_dev->fd;
    }

    start_Timer(&commandTimer);
    ioRes = ioctl(handleToReset, NVME_RESET_CONTROLLER);
    stop_Timer(&commandTimer);

    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(get_tDevice_Last_Command_Completion_Time_NS(device));
    }

    if (ioRes < 0)
    {
        // failed
        set_Device_Last_Error(M_CONST_CAST(tDevice*, device), errno);
        if (device->deviceVerbosity > VERBOSITY_COMMAND_VERBOSE && device->os_info.last_error != 0)
        {
            print_str("Error :");
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
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

M_PARAM_RO(1) eReturnValues os_nvme_Subsystem_Reset(M_ATTR_UNUSED const tDevice* M_NONNULL device)
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

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Get_Exclusive(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return SUCCESS;
}

OPENSEA_TRANSPORT_API M_PARAM_RW(1) eReturnValues os_Lock_Device(const tDevice* M_NONNULL device)
{
    // There is nothing to lock since you cannot open a CAM device with O_NONBLOCK
    if (device->os_info.lockCount < UINT16_MAX)
    {
        // Always increment this so we know how many times we've been requested to lock
        ++M_CONST_CAST(tDevice*, device)->os_info.lockCount;
    }
    return SUCCESS;
}

OPENSEA_TRANSPORT_API M_PARAM_RW(1) eReturnValues os_Unlock_Device(const tDevice* M_NONNULL device)
{
    // There is nothing to unlock since you cannot open a CAM device with O_NONBLOCK
    if (device->os_info.lockCount > 0)
    {
        --M_CONST_CAST(tDevice*, device)->os_info.lockCount;
    }
    return SUCCESS;
}

// For the file syste cache update and unmount, these two functions may be useful:
// getfsstat ???
// https://www.freebsd.org/cgi/man.cgi?query=getfsstat&sektion=2&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports statfs
// ??? https://www.freebsd.org/cgi/man.cgi?query=statfs&sektion=2&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports fstab
// ??? https://www.freebsd.org/cgi/man.cgi?query=fstab&sektion=5&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports This
// looks very similar to the Linux getmntent: getfsent
// ???https://www.freebsd.org/cgi/man.cgi?query=getfsent&sektion=3&apropos=0&manpath=FreeBSD+13.0-RELEASE+and+Ports

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues
    os_Update_File_System_Cache(M_ATTR_UNUSED const tDevice* M_NONNULL device)
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

OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED const tDevice* M_NONNULL device)
{
    return NOT_SUPPORTED;
}

M_PARAM_RO(1)
OPENSEA_TRANSPORT_API eReturnValues os_Unmount_File_Systems_On_Device(const tDevice* M_NONNULL device)
{
    return unmount_Partitions_From_Device(get_Device_Handle_Name(device));
}
