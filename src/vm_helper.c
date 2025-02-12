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
#include "cmds.h"
#include "nvme_helper_func.h"
#include "scsi_helper_func.h"
#include "sntl_helper.h"
#include "vm_helper.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h> //for basename and dirname
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h> //for mmap pci reads. Potential to move.
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h> // for close
#include <vmkapi.h>

#if defined(DEGUG_SCAN_TIME)
#    include "common_platform.h"
#endif

// If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise
// you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

extern bool validate_Device_Struct(versionBlock);

// Local helper functions for debugging
#if defined(_DEBUG)
static void print_io_hdr(sg_io_hdr_t* pIo)
{
    DECLARE_ZERO_INIT_ARRAY(char, timeFormat, TIME_STRING_LENGTH);
    time_t time_now = time(M_NULLPTR);
    printf("\n%s: %s---------------------------------\n", __FUNCTION__,
           get_Current_Time_String(&time_now, timeFormat, TIME_STRING_LENGTH));
    printf("type int interface_id %d\n", pIo->interface_id);                    /* [i] 'S' (required) */
    printf("type int  dxfer_direction %d\n", pIo->dxfer_direction);             /* [i] */
    printf("type unsigned char cmd_len 0x%x\n", pIo->cmd_len);                  /* [i] */
    printf("type unsigned char mx_sb_len 0x%x\n", pIo->mx_sb_len);              /* [i] */
    printf("type unsigned short iovec_count 0x%x\n", pIo->iovec_count);         /* [i] */
    printf("type unsigned int dxfer_len %d\n", pIo->dxfer_len);                 /* [i] */
    printf("type void * dxferp %p\n", C_CAST(unsigned int*, pIo->dxferp));      /* [i], [*io] */
    printf("type unsigned char * cmdp %p\n", C_CAST(unsigned int*, pIo->cmdp)); /* [i], [*i]  */
    printf("type unsigned char * sbp %p\n", C_CAST(unsigned int*, pIo->sbp));   /* [i], [*o]  */
    printf("type unsigned int timeout %d\n", pIo->timeout);                     /* [i] unit: millisecs */
    printf("type unsigned int flags 0x%x\n", pIo->flags);                       /* [i] */
    printf("type int pack_id %d\n", pIo->pack_id);                              /* [i->o] */
    printf("type void * usr_ptr %p\n", C_CAST(unsigned int*, pIo->usr_ptr));    /* [i->o] */
    printf("type unsigned char status 0x%x\n", pIo->status);                    /* [o] */
    printf("type unsigned char maskedStatus 0x%x\n", pIo->masked_status);       /* [o] */
    printf("type unsigned char msg_status 0x%x\n", pIo->msg_status);            /* [o] */
    printf("type unsigned char sb_len_wr 0x%x\n", pIo->sb_len_wr);              /* [o] */
    printf("type unsigned short host_status 0x%x\n", pIo->host_status);         /* [o] */
    printf("type unsigned short driver_status 0x%x\n", pIo->driver_status);     /* [o] */
    printf("type int resid %d\n", pIo->resid);                                  /* [o] */
    printf("type unsigned int duration %d\n", pIo->duration);                   /* [o] */
    printf("type unsigned int info 0x%x\n", pIo->info);                         /* [o] */
    printf("-----------------------------------------\n");
}
#endif //_DEBUG

static int drive_filter(const struct dirent* entry)
{
    int driveHandle = strncmp("t10", entry->d_name, 3);

    if (driveHandle != 0)
    {
        /**
         * Its not a SATA or NVMe.
         * Lets check if it is SAS (starts with "naa.")
         */

        driveHandle = strncmp("naa.", entry->d_name, 4);

        if (driveHandle != 0)
        {
            return !driveHandle;
        }
    }

    driveHandle = strncmp("t10.NVMe", entry->d_name, 8);

    if (driveHandle == 0)
    {
        return driveHandle;
    }

    char* partition = strpbrk(entry->d_name, ":");
    if (partition != M_NULLPTR)
    {
        return !driveHandle;
    }
    else
    {
        return driveHandle;
    }
}

typedef struct s_sysVMLowLevelDeviceInfo
{
    eSCSIPeripheralDeviceType scsiDevType; // in Linux this will be reading the "type" file to get this. If it is not
                                           // available, will retry with "inquiry" data file's first byte
    eDriveType     drive_type;
    eInterfaceType interface_type;
    adapterInfo    adapter_info;
    driverInfo     driver_info;
    struct
    {
        uint8_t host;    // AKA SCSI adapter #
        uint8_t channel; // AKA bus
        uint8_t target;  // AKA id number
        uint8_t lun;     // logical unit number
    } scsiAddress;
    char     fullDevicePath[OPENSEA_PATH_MAX];
    char     primaryHandleStr[OS_HANDLE_NAME_MAX_LENGTH];      // dev/sg or /dev/nvmexny (namespace handle)
    char     secondaryHandleStr[OS_SECOND_HANDLE_NAME_LENGTH]; // dev/sd or /dev/nvmex (controller handle)
    char     tertiaryHandleStr[OS_SECOND_HANDLE_NAME_LENGTH];  // dev/bsg or /dev/ngXnY (nvme generic handle)
    uint16_t queueDepth;                                       // if 0, then this was unable to be read and populated
} sysVMLowLevelDeviceInfo;

// while similar to the function below, this is used only by get_Device to set up some fields in the device structure
// for the above layers this function gets the following info:
//  pcie/usb product ID, vendor ID, revision ID, sets the interface type, ieee1394 specifier ID, and sets the handle
//  mapping for SD/BSG
// this also calls the function to get the driver version info as well as the name of the driver as a string.
// TODO: Also output the full device path from the read link???
//       get the SCSI peripheral device type to help decide when to scan for RAIDs on a given handle
// handle nvme-generic handles???
// handle looking up nvme controller handle from a namespace handle???
// handle /dev/disk/by-<> lookups. These are links to /dev/sd or /dev/nvme, etc. We can convert these first, then
// convert again to sd/sg/nvme as needed

static void get_VMV_SYS_FS_Info(const char* handle, sysVMLowLevelDeviceInfo* sysVmInfo)
{
    // check if it's a block handle, bsg, or scsi_generic handle, then setup the path we need to read.
    if (handle && sysVmInfo)
    {
        if (strstr(handle, "t10.ATA") != NULL)
        {
            // set scsi interface and scsi drive until we know otherwise
            sysVmInfo->drive_type     = ATA_DRIVE;
            sysVmInfo->interface_type = IDE_INTERFACE;
        }
        if (strstr(handle, "naa.") != NULL)
        {
            sysVmInfo->drive_type     = SCSI_DRIVE;
            sysVmInfo->interface_type = SCSI_INTERFACE;
        }
    }
}

static void set_Device_Fields_From_Handle(const char* handle, tDevice* device)
{
    sysVMLowLevelDeviceInfo sysVmInfo;
    /**
     * Setting up defaults
     */
    // sysVmInfo.drive_type = SCSI_DRIVE;
    // device->drive_info.drive_type = ATA_DRIVE;
    // sysVmInfo.interface_type = SCSI_INTERFACE;
    // device->drive_info.interface_type = IDE_INTERFACE;
    // sysVmInfo.media_type = MEDIA_HDD;

    safe_memset(&sysVmInfo, sizeof(sysVMLowLevelDeviceInfo), 0, sizeof(sysVMLowLevelDeviceInfo));
    get_VMV_SYS_FS_Info(handle, &sysVmInfo);
    // now copy the saved data to tDevice. -DB
    if (device)
    {
        device->drive_info.drive_type     = sysVmInfo.drive_type;
        device->drive_info.interface_type = sysVmInfo.interface_type;
        safe_memcpy(&device->drive_info.adapter_info, sizeof(adapterInfo), &sysVmInfo.adapter_info,
                    sizeof(adapterInfo));
        safe_memcpy(&device->drive_info.driver_info, sizeof(driverInfo), &sysVmInfo.driver_info, sizeof(driverInfo));
        if (strlen(sysVmInfo.primaryHandleStr) > 0)
        {
            snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", sysVmInfo.primaryHandleStr);
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s",
                                basename(sysVmInfo.primaryHandleStr));
        }
        if (strlen(sysVmInfo.secondaryHandleStr) > 0)
        {
            snprintf_err_handle(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "%s",
                                sysVmInfo.secondaryHandleStr);
            snprintf_err_handle(device->os_info.secondFriendlyName, OS_SECOND_HANDLE_NAME_LENGTH, "%s",
                                basename(sysVmInfo.secondaryHandleStr));
        }
    }
}

#define LIN_MAX_HANDLE_LENGTH 16
eReturnValues get_Device(const char* filename, tDevice* device)
{
    char*                    deviceHandle = M_NULLPTR;
    eReturnValues            ret          = SUCCESS;
    int                      rc           = 0;
    struct nvme_adapter_list nvmeAdptList;
    bool                     isScsi      = false;
    char*                    nvmeDevName = M_NULLPTR;

    /**
     * In VMWare NVMe device the drivename (for NDDK)
     * always starts with "vmhba" (e.g. vmhba1)
     */

    nvmeDevName = strstr(filename, "vmhba");
    isScsi      = (nvmeDevName == M_NULLPTR) ? true : false;

    // printf("Getting device for %s\n", filename);

    /**
     * List down both NVMe and HDD/SSD drives
     * Get the device after matching the name
     */
    if (0 != safe_strdup(&deviceHandle, filename))
    {
        return MEMORY_FAILURE;
    }

    if (isScsi)
    {
#if defined(_DEBUG)
        printf("This is a SCSI drive\n");
        printf("Attempting to open %s\n", deviceHandle);
#endif
        // Note: We are opening a READ/Write flag
        if ((device->os_info.fd = open(deviceHandle, O_RDWR | O_NONBLOCK)) < 0)
        {
            perror("open");
            device->os_info.fd = errno;
            printf("open failure\n");
            printf("Error: ");
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

        // Adding support for different device discovery options.
        if (device->dFlags == OPEN_HANDLE_ONLY)
        {
            // set scsi interface and scsi drive until we know otherwise
            device->drive_info.drive_type     = SCSI_DRIVE;
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.media_type     = MEDIA_HDD;
            set_Device_Fields_From_Handle(deviceHandle, device);
            setup_Passthrough_Hacks_By_ID(device);
            safe_free(&deviceHandle);
            return ret;
        }

        if ((device->os_info.fd >= 0) && (ret == SUCCESS))
        {
            struct sg_scsi_id hctlInfo;
            safe_memset(&hctlInfo, sizeof(struct sg_scsi_id), 0, sizeof(struct sg_scsi_id));
            int getHctl = ioctl(device->os_info.fd, SG_GET_SCSI_ID, &hctlInfo);
            if (getHctl == 0 && errno == 0) // when this succeeds, both of these will be zeros
            {
                // printf("Got hctlInfo\n");
                device->os_info.scsiAddress.host    = C_CAST(uint8_t, hctlInfo.host_no);
                device->os_info.scsiAddress.channel = C_CAST(uint8_t, hctlInfo.channel);
                device->os_info.scsiAddress.target  = C_CAST(uint8_t, hctlInfo.scsi_id);
                device->os_info.scsiAddress.lun     = C_CAST(uint8_t, hctlInfo.lun);
                device->drive_info.namespaceID =
                    device->os_info.scsiAddress.lun +
                    UINT32_C(1); // Doing this to help with USB to NVMe adapters. Luns start at zero, whereas namespaces
                                 // start with 1, hence the plus 1.
                // also reported are per lun and per device Q-depth which might be nice to store.
                // printf("H:C:T:L = %" PRIu8 ":%" PRIu8 ":%" PRIu8 ":%" PRIu8 "\n", device->os_info.scsiAddress.host,
                // device->os_info.scsiAddress.channel, device->os_info.scsiAddress.target,
                // device->os_info.scsiAddress.lun);
            }

#if defined(_DEBUG)
            printf("Getting SG driver version\n");
#endif

            /**
             * SG_GET_VERSION_NUM is currently not supported for VMWare
             * SG_IO. Assume sg v3 IO support only
             */

            // set the OS Type
            device->os_info.osType = OS_ESX;

            safe_memcpy(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, deviceHandle, safe_strlen(deviceHandle) + 1);

            // set scsi interface and scsi drive until we know otherwise
            device->drive_info.drive_type = SCSI_DRIVE;
            // device->drive_info.drive_type = ATA_DRIVE;
            device->drive_info.interface_type = SCSI_INTERFACE;
            // device->drive_info.interface_type = IDE_INTERFACE;
            device->drive_info.media_type = MEDIA_HDD;
            // now have the device information fields set
#if defined(_DEBUG)
            printf("Setting interface, drive type, secondary handles\n");
#endif

            set_Device_Fields_From_Handle(deviceHandle, device);
            setup_Passthrough_Hacks_By_ID(device);
            // device->drive_info.interface_type = SCSI_INTERFACE;
            // device->drive_info.drive_type = UNKNOWN_DRIVE;
            // device->drive_info.media_type = MEDIA_UNKNOWN;

#if defined(_DEBUG)
            printf("name = %s\t friendly name = %s\n2ndName = %s\t2ndFName = %s\n", device->os_info.name,
                   device->os_info.friendlyName, device->os_info.secondName, device->os_info.secondFriendlyName);
            printf("h:c:t:l = %u:%u:%u:%u\n", device->os_info.scsiAddress.host, device->os_info.scsiAddress.channel,
                   device->os_info.scsiAddress.target, device->os_info.scsiAddress.lun);

            printf("SG driver version = %u.%u.%u\n", device->os_info.sgDriverVersion.majorVersion,
                   device->os_info.sgDriverVersion.minorVersion, device->os_info.sgDriverVersion.revision);
#endif

            // Fill in all the device info.
            // this code to set up passthrough commands for USB and IEEE1394 has been removed for now to match Windows
            // functionality. Need better intelligence than this. Some of these old pass-through types issue vendor
            // specific op codes that could be misinterpretted on some devices.
            //              if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type
            //              == IEEE_1394_INTERFACE)
            //              {
            //                  set_ATA_Passthrough_Type_By_PID_and_VID(device);
            //              }

            ret = fill_Drive_Info_Data(device);

#if defined(_DEBUG)
            printf("\nvm helper\n");
            printf("Drive type: %d\n", device->drive_info.drive_type);
            printf("Interface type: %d\n", device->drive_info.interface_type);
            printf("Media type: %d\n", device->drive_info.media_type);
#endif
        }
        safe_free(&deviceHandle);
    }
    else
    {
        rc = Nvme_GetAdapterList(&nvmeAdptList);

        if (rc != 0)
        {
            return FAILURE;
        }

#if defined(_DEBUG)
        printf("This is a NVMe drive\n");
        printf("Attempting to open %s\n", deviceHandle);
#endif
        // Note: We are opening a READ/Write flag
        /**
         * Opening up the dev handle for NVMe
         */

        device->os_info.nvmeFd = Nvme_Open(&nvmeAdptList, filename);

        /**
         * We should do a HDD/SSD open here
         */

        if (device->os_info.nvmeFd == M_NULLPTR)
        {
            perror("open");
            printf("open failure\n");
            printf("Error: ");
            print_Errno_To_Screen(errno);
            if (errno == EACCES)
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

        // Set NSID from the incoming handle. It's not clear if this is correct, but based on the vmware structures, the
        // "cookie" in the adapter info says it points to the controller, so currently assuming this is a reasonable way
        // to read the nsid.
        if (!get_And_Validate_Integer_Input_Uint32(nvmeDevName + safe_strlen("vmhba"), M_NULLPTR, ALLOW_UNIT_NONE,
                                                   &device->drive_info.namespaceID))
        {
            printf("Error: Unable to read NSID\n");
        }

        device->os_info.minimumAlignment = sizeof(void*);

        // Adding support for different device discovery options.
        if (device->dFlags == OPEN_HANDLE_ONLY)
        {
            safe_free(&deviceHandle);
            return ret;
        }
        //\\TODO: Add support for other flags.

        if ((device->os_info.nvmeFd != M_NULLPTR) && (ret == SUCCESS))
        {
#if defined(_DEBUG)
            printf("Getting SG driver version\n");
#endif

            /**
             * Setting up NVMe drive blindly for now
             */

            device->drive_info.interface_type = NVME_INTERFACE;
            device->drive_info.drive_type     = NVME_DRIVE;
            device->drive_info.media_type     = MEDIA_NVM;
            safe_memcpy(device->drive_info.T10_vendor_ident, T10_VENDOR_ID_LEN + 1, "NVMe", 4);
            device->os_info.osType = OS_ESX;
            safe_memcpy(&(device->os_info.name), OS_HANDLE_NAME_MAX_LENGTH, filename, safe_strlen(filename) + 1);

#if !defined(DISABLE_NVME_PASSTHROUGH)
            if (device->drive_info.interface_type == NVME_INTERFACE)
            {
                ret = fill_In_NVMe_Device_Info(device);
            }
#endif
#if defined(_DEBUG)
            printf("\nvm helper\n");
            printf("Drive type: %d\n", device->drive_info.drive_type);
            printf("Interface type: %d\n", device->drive_info.interface_type);
            printf("Media type: %d\n", device->drive_info.media_type);
#endif
        }
        safe_free(&deviceHandle);
    }

    return ret;
}
// http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_reset.html
// sgResetType should be one of the values from the link above...so bus or device...controller will work but that
// shouldn't be done ever.
eReturnValues sg_reset(int fd, int resetType)
{
    eReturnValues ret = UNKNOWN;

    int ioctlResult = ioctl(fd, SG_SCSI_RESET, &resetType);

    if (ioctlResult < 0)
    {
#if defined(_DEBUG)
        printf("Reset failure! errorcode: %d, errno: %d\n", ret, errno);
        print_Errno_To_Screen(errno);
#endif
        if (errno == EAFNOSUPPORT)
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            ret = OS_COMMAND_BLOCKED;
        }
    }
    else
    {
        // poll for reset completion
#if defined(_DEBUG)
        printf("Reset in progress, polling for completion!\n");
#endif
        resetType = SG_SCSI_RESET_NOTHING;
        while (errno == EBUSY)
        {
            ret = ioctl(fd, SG_SCSI_RESET, &resetType);
        }
        ret = SUCCESS;
        // printf("Reset Success!\n");
    }
    return ret;
}

eReturnValues os_Device_Reset(tDevice* device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_DEVICE);
}

eReturnValues os_Bus_Reset(tDevice* device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_BUS);
}

eReturnValues os_Controller_Reset(tDevice* device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_HOST);
}

eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = FAILURE;
#ifdef _DEBUG
    printf("-->%s \n", __FUNCTION__);
#endif
    switch (scsiIoCtx->device->drive_info.interface_type)
    {
    case NVME_INTERFACE:
#if !defined(DISABLE_NVME_PASSTHROUGH)
        return sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
#endif
        // USB, ATA, and SCSI interface all use sg, so just issue an SG IO.
    case SCSI_INTERFACE:
    case IDE_INTERFACE:
    case USB_INTERFACE:
    case IEEE_1394_INTERFACE:
        ret = send_sg_io(scsiIoCtx);
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
                printf("No Raid PassThrough IO Routine present for this device\n");
            }
        }
        break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Target Device does not have a valid interface %d\n", scsiIoCtx->device->drive_info.interface_type);
        }
        break;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
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

eReturnValues send_sg_io(ScsiIoCtx* scsiIoCtx)
{
    sg_io_hdr_t   io_hdr;
    uint8_t*      localSenseBuffer = M_NULLPTR;
    eReturnValues ret              = SUCCESS;
    DECLARE_SEATIMER(commandTimer);
#ifdef _DEBUG
    printf("-->%s \n", __FUNCTION__);
#endif

    // int idx = 0;
    //  Start with zapping the io_hdr
    safe_memset(&io_hdr, sizeof(sg_io_hdr_t), 0, sizeof(sg_io_hdr_t));

    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("Sending command with send_IO\n");
    }

    // Set up the io_hdr
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len      = scsiIoCtx->cdbLength;
    // Use user's sense or local?
    if ((scsiIoCtx->senseDataSize) && (scsiIoCtx->psense != M_NULLPTR))
    {
        io_hdr.mx_sb_len = scsiIoCtx->senseDataSize;
        io_hdr.sbp       = scsiIoCtx->psense;
    }
    else
    {
        localSenseBuffer =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t),
                                                             scsiIoCtx->device->os_info.minimumAlignment));
        if (!localSenseBuffer)
        {
            return MEMORY_FAILURE;
        }
        io_hdr.mx_sb_len = SPC3_SENSE_LEN;
        io_hdr.sbp       = localSenseBuffer;
    }

    switch (scsiIoCtx->direction)
    {
    case XFER_NO_DATA:
        io_hdr.dxfer_direction = SG_DXFER_NONE;
        break;
    case XFER_DATA_IN:
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        break;
    case XFER_DATA_OUT:
        io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        io_hdr.dxfer_direction = SG_DXFER_TO_FROM_DEV;
        break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("%s Didn't understand direction\n", __FUNCTION__);
        }
        safe_free_aligned(&localSenseBuffer);
        return BAD_PARAMETER;
    }

    io_hdr.dxfer_len = scsiIoCtx->dataLength;
    io_hdr.dxferp    = scsiIoCtx->pdata;
    io_hdr.cmdp      = scsiIoCtx->cdb;
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        io_hdr.timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that
        // we DON'T do a conversion and leave the time as the max...
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < SG_MAX_CMD_TIMEOUT_SECONDS)
        {
            io_hdr.timeout *= 1000; // convert to milliseconds
        }
        else
        {
            io_hdr.timeout = UINT32_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            io_hdr.timeout = scsiIoCtx->timeout;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->timeout < SG_MAX_CMD_TIMEOUT_SECONDS)
            {
                io_hdr.timeout *= 1000; // convert to milliseconds
            }
            else
            {
                io_hdr.timeout = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            io_hdr.timeout = 15 * 1000; // default to 15 second timeout
        }
    }

    // \revisit: should this be FF or something invalid than 0?
    scsiIoCtx->returnStatus.format   = 0xFF;
    scsiIoCtx->returnStatus.senseKey = 0;
    scsiIoCtx->returnStatus.asc      = 0;
    scsiIoCtx->returnStatus.ascq     = 0;
    // print_io_hdr(&io_hdr);
    // printf("scsiIoCtx->device->os_info.fd = %d\n", scsiIoCtx->device->os_info.fd);
    start_Timer(&commandTimer);
    int ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, SG_IO, &io_hdr);
    stop_Timer(&commandTimer);
    scsiIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
    }

    // print_io_hdr(&io_hdr);

    if (io_hdr.sb_len_wr)
    {
        scsiIoCtx->returnStatus.format = io_hdr.sbp[0];
        get_Sense_Key_ASC_ASCQ_FRU(io_hdr.sbp, io_hdr.mx_sb_len, &scsiIoCtx->returnStatus.senseKey,
                                   &scsiIoCtx->returnStatus.asc, &scsiIoCtx->returnStatus.ascq,
                                   &scsiIoCtx->returnStatus.fru);
    }

    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
    {
        switch (io_hdr.info & SG_INFO_DIRECT_IO_MASK)
        {
        case SG_INFO_INDIRECT_IO:
            printf("SG IO Issued as Indirect IO\n");
            break;
        case SG_INFO_DIRECT_IO:
            printf("SG IO Issued as Direct IO\n");
            break;
        case SG_INFO_MIXED_IO:
            printf("SG IO Issued as Mixed IO\n");
            break;
        default:
            printf("SG IO Issued as Unknown IO type\n");
            break;
        }
    }

    if ((io_hdr.info & SG_INFO_OK_MASK) != SG_INFO_OK)
    {
        // something has gone wrong. Sense data may or may not have been returned.
        // Check the masked status, host status and driver status to see what happened.
        if (io_hdr.masked_status != 0) // SAM_STAT_GOOD???
        {
            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                printf("SG Masked Status = %02" PRIX8 "h", io_hdr.masked_status);
                switch (io_hdr.masked_status)
                {
                case GOOD:
                    printf(" - Good\n");
                    break;
                case CHECK_CONDITION:
                    printf(" - Check Condition\n");
                    break;
                case CONDITION_GOOD:
                    printf(" - Condition Good\n");
                    break;
                case BUSY:
                    printf(" - Busy\n");
                    break;
                case INTERMEDIATE_GOOD:
                    printf(" - Intermediate Good\n");
                    break;
                case INTERMEDIATE_C_GOOD:
                    printf(" - Intermediate C Good\n");
                    break;
                case RESERVATION_CONFLICT:
                    printf(" - Reservation Conflict\n");
                    break;
                case COMMAND_TERMINATED:
                    printf(" - Command Terminated\n");
                    break;
                case QUEUE_FULL:
                    printf(" - Queue Full\n");
                    break;
                default:
                    printf(" - Unknown Masked Status\n");
                    break;
                }
            }
            if (io_hdr.sb_len_wr == 0)
            {
                if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                {
                    printf("\t(Masked Status) Sense data not available, assuming OS_PASSTHROUGH_FAILURE\n");
                }
                // No sense data back. We need to set an error since the layers above are going to look for sense data
                // and we don't have any.
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }
        if (io_hdr.host_status != 0)
        {
            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                printf("SG Host Status = %02" PRIX16 "h", io_hdr.host_status);
                switch (io_hdr.host_status)
                {
                case OPENSEA_SG_ERR_DID_OK:
                    printf(" - No Error\n");
                    break;
                case OPENSEA_SG_ERR_DID_NO_CONNECT:
                    printf(" - Could Not Connect\n");
                    break;
                case OPENSEA_SG_ERR_DID_BUS_BUSY:
                    printf(" - Bus Busy\n");
                    break;
                case OPENSEA_SG_ERR_DID_TIME_OUT:
                    printf(" - Timed Out\n");
                    break;
                case OPENSEA_SG_ERR_DID_BAD_TARGET:
                    printf(" - Bad Target Device\n");
                    break;
                case OPENSEA_SG_ERR_DID_ABORT:
                    printf(" - Abort\n");
                    break;
                case OPENSEA_SG_ERR_DID_PARITY:
                    printf(" - Parity Error\n");
                    break;
                case OPENSEA_SG_ERR_DID_ERROR:
                    printf(" - Internal Adapter Error\n");
                    break;
                case OPENSEA_SG_ERR_DID_RESET:
                    printf(" - SCSI Bus/Device Has Been Reset\n");
                    break;
                case OPENSEA_SG_ERR_DID_BAD_INTR:
                    printf(" - Bad Interrupt\n");
                    break;
                case OPENSEA_SG_ERR_DID_PASSTHROUGH:
                    printf(" - Forced Passthrough Past Mid-Layer\n");
                    break;
                case OPENSEA_SG_ERR_DID_SOFT_ERROR:
                    printf(" - Soft Error, Retry?\n");
                    break;
                default:
                    printf(" - Unknown Host Status\n");
                    break;
                }
            }
            if (io_hdr.sb_len_wr == 0) // Doing this because some drivers may set an error even if the command otherwise
                                       // went through and sense data was available.
            {
                if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                {
                    printf("\t(Host Status) Sense data not available, assuming OS_PASSTHROUGH_FAILURE\n");
                }
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }
        if (io_hdr.driver_status != 0)
        {
            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                printf("SG Driver Status = %02" PRIX16 "h", io_hdr.driver_status);
                switch (io_hdr.driver_status & OPENSEA_SG_ERR_DRIVER_MASK)
                {
                case OPENSEA_SG_ERR_DRIVER_OK:
                    printf(" - Driver OK");
                    break;
                case OPENSEA_SG_ERR_DRIVER_BUSY:
                    printf(" - Driver Busy");
                    break;
                case OPENSEA_SG_ERR_DRIVER_SOFT:
                    printf(" - Driver Soft Error");
                    break;
                case OPENSEA_SG_ERR_DRIVER_MEDIA:
                    printf(" - Driver Media Error");
                    break;
                case OPENSEA_SG_ERR_DRIVER_ERROR:
                    printf(" - Driver Error");
                    break;
                case OPENSEA_SG_ERR_DRIVER_INVALID:
                    printf(" - Driver Invalid");
                    break;
                case OPENSEA_SG_ERR_DRIVER_TIMEOUT:
                    printf(" - Driver Timeout");
                    break;
                case OPENSEA_SG_ERR_DRIVER_HARD:
                    printf(" - Driver Hard Error");
                    break;
                case OPENSEA_SG_ERR_DRIVER_SENSE:
                    printf(" - Driver Sense Data Available");
                    break;
                default:
                    printf(" - Unknown Driver Error");
                    break;
                }
                // now error suggestions
                switch (io_hdr.driver_status & OPENSEA_SG_ERR_SUGGEST_MASK)
                {
                case OPENSEA_SG_ERR_SUGGEST_NONE:
                    break; // no suggestions, nothing necessary to print
                case OPENSEA_SG_ERR_SUGGEST_RETRY:
                    printf(" - Suggest Retry");
                    break;
                case OPENSEA_SG_ERR_SUGGEST_ABORT:
                    printf(" - Suggest Abort");
                    break;
                case OPENSEA_SG_ERR_SUGGEST_REMAP:
                    printf(" - Suggest Remap");
                    break;
                case OPENSEA_SG_ERR_SUGGEST_DIE:
                    printf(" - Suggest Die");
                    break;
                case OPENSEA_SG_ERR_SUGGEST_SENSE:
                    printf(" - Suggest Sense");
                    break;
                default:
                    printf(" - Unknown suggestion");
                    break;
                }
                printf("\n");
            }
            if (io_hdr.sb_len_wr == 0)
            {
                if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                {
                    printf("\t(Driver Status) Sense data not available, assuming OS_PASSTHROUGH_FAILURE\n");
                }
                // No sense data back. We need to set an error since the layers above are going to look for sense data
                // and we don't have any.
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }
    }

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    safe_free_aligned(&localSenseBuffer);
    return ret;
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
eReturnValues get_Device_Count(uint32_t* numberOfDevices, uint64_t flags)
{
    int                      num_devs      = 0;
    int                      num_nvme_devs = 0;
    int                      rc            = 0;
    struct nvme_adapter_list nvmeAdptList;

    struct dirent** namelist;

    num_devs = scandir("/dev/disks", &namelist, drive_filter, alphasort);

    // free the list of names to not leak memory
    for (int iter = 0; iter < num_devs; ++iter)
    {
        safe_free_dirent(&namelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist);

#ifdef _DEBUG
    printf("get_Device_Count : num_devs %d\n", num_devs);
#endif

    // add nvme devices to the list
    rc = Nvme_GetAdapterList(&nvmeAdptList);

    if (rc == 0)
    {
        num_nvme_devs = nvmeAdptList.count;
    }

#ifdef _DEBUG
    printf("get_Device_Count : num_nvme_devs %d\n", num_nvme_devs);
#endif

    *numberOfDevices = num_devs + num_nvme_devs;

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
#define VM_NAME_LEN 128
eReturnValues get_Device_List(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, name, VM_NAME_LEN);
    char*                    nvmeDevName = M_NULLPTR;
    int                      fd          = -1;
    bool                     isScsi      = false;
    tDevice*                 d           = M_NULLPTR;
    struct nvme_adapter_list nvmeAdptList;
    int                      rc = 0;
#if defined(DEGUG_SCAN_TIME)
    DECLARE_SEATIMER(getDeviceTimer);
    DECLARE_SEATIMER(getDeviceListTimer);
#endif
    safe_memset(&nvmeAdptList, sizeof(struct nvme_adapter_list), 0, sizeof(struct nvme_adapter_list));
    struct dirent** namelist = M_NULLPTR;

    int num_sg_devs = 0;

    int num_nvme_devs = 0;

    num_sg_devs = scandir("/dev/disks", &namelist, drive_filter, alphasort);

    rc = Nvme_GetAdapterList(&nvmeAdptList);

    if (rc == 0)
    {
        num_nvme_devs = nvmeAdptList.count;
    }

    char** devs = M_REINTERPRET_CAST(char**, safe_calloc(num_sg_devs + num_nvme_devs + 1, sizeof(char*)));
    int    i    = 0;
    int    j    = 0;
    // add sg/sd devices to the list
    for (; i < (num_sg_devs); i++)
    {
        size_t deviceHandleLen = (safe_strlen("/dev/disks/") + safe_strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i]                = M_REINTERPRET_CAST(char*, safe_malloc(deviceHandleLen));
        snprintf_err_handle(devs[i], deviceHandleLen, "/dev/disks/%s", namelist[i]->d_name);
        safe_free_dirent(&namelist[i]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist);

    // add nvme devices to the list
    for (j = 0; i < (num_sg_devs + num_nvme_devs) && i < MAX_DEVICES_PER_CONTROLLER; i++, j++)
    {
        size_t nvmeAdptNameLen = safe_strlen(nvmeAdptList.adapters[j].name) + 1;
        devs[i]                = M_REINTERPRET_CAST(char*, safe_malloc(nvmeAdptNameLen));
        safe_memset(devs[i], nvmeAdptNameLen, 0, nvmeAdptNameLen);
        snprintf_err_handle(devs[i], nvmeAdptNameLen, "%s", nvmeAdptList.adapters[j].name);
#ifdef _DEBUG
        printf("Discovered NVMe Device index - %d Name - %s \n", j, nvmeAdptList.adapters[j].name);
#endif
    }
    devs[i] = M_NULLPTR; // Added this so the for loop down doesn't cause a segmentation fault.

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
#if defined(DEGUG_SCAN_TIME)
        start_Timer(&getDeviceListTimer);
#endif
        for (driveNumber = UINT32_C(0); ((driveNumber >= UINT32_C(0) && driveNumber < MAX_DEVICES_TO_SCAN &&
                                          driveNumber < (num_sg_devs + num_nvme_devs)) &&
                                         (found < numberOfDevices));
             driveNumber++)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, VM_NAME_LEN, 0, VM_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, VM_NAME_LEN, "%s", devs[driveNumber]);

            nvmeDevName = strstr(name, "vmhba");
            isScsi      = (nvmeDevName == M_NULLPTR) ? true : false;

            if (isScsi)
            {
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
#if defined(DEGUG_SCAN_TIME)
                    DECLARE_SEATIMER(getDeviceTimer);
                    start_Timer(&getDeviceTimer);
#endif
                    d->dFlags         = flags;
                    eReturnValues ret = get_Device(name, d);
#if defined(DEGUG_SCAN_TIME)
                    stop_Timer(&getDeviceTimer);
                    printf("Time to get %s = %fms\n", name, get_Milli_Seconds(getDeviceTimer));
#endif
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
            }
            else
            {
                eReturnValues ret = get_Device(name, d);
                if (ret != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                found++;
                d++;
            }
            // free the dev[deviceNumber] since we are done with it now.
            safe_free(&devs[driveNumber]);
        }
#if defined(DEGUG_SCAN_TIME)
        stop_Timer(&getDeviceListTimer);
        printf("Time to get all device = %fms\n", get_Milli_Seconds(getDeviceListTimer));
#endif
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (permissionDeniedCount == (num_sg_devs + num_nvme_devs))
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

//-----------------------------------------------------------------------------
//
//  close_Device()
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
eReturnValues close_Device(tDevice* dev)
{
    int   retValue = 0;
    bool  isNVMe   = false;
    char* nvmeDevName;

    /**
     * In VMWare NVMe device the drivename (for NDDK)
     * always starts with "vmhba" (e.g. vmhba1)
     */

    nvmeDevName = strstr(dev->os_info.name, "vmhba");
    isNVMe      = (nvmeDevName != M_NULLPTR) ? true : false;

    if (dev)
    {
        if (isNVMe)
        {
            Nvme_Close(dev->os_info.nvmeFd);
            dev->os_info.last_error = errno;
            retValue                = 0;
            dev->os_info.nvmeFd     = M_NULLPTR;
        }
        else
        {
            retValue                = close(dev->os_info.fd);
            dev->os_info.last_error = errno;
        }

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
}

void print_usr_io_struct(struct usr_io uio)
{
    printf("\n====VMWare User NVMe IO====\n");
    printf("\tnvme cmd:\n");
    printf("\t\tnvme Header:\n");
    printf("\t\t\topcode: %" PRIu32 "\n", uio.cmd.header.opCode);
    printf("\t\t\tfusedOp: %" PRIu32 "\n", uio.cmd.header.fusedOp);
    printf("\t\t\tcmdID: %" PRIu32 "\n", uio.cmd.header.cmdID);
    printf("\t\t\tnamespaceID: %" PRIu32 "\n", uio.cmd.header.namespaceID);
    printf("\t\t\treserved: %" PRIu64 "\n", uio.cmd.header.reserved);
    printf("\t\t\tmetadataPtr: %" PRIu64 "\n", uio.cmd.header.metadataPtr);
    printf("\t\t\tprp1: %" PRIX64 "\n", uio.cmd.header.prp[0].addr);
    printf("\t\t\tprp2: %" PRIX64 "\n", uio.cmd.header.prp[1].addr);
    printf("\t\tas dwords:\n");
    for (uint8_t iter = UINT8_C(0); iter < 16; ++iter)
    {
        printf("\t\t\tCDW%" PRIu8 ":\t%08" PRIX32 "h\n", iter, uio.cmd.dw[iter]);
    }
    printf("\tnvme comp:\n");
    printf("\t\tCommand Specific: %" PRIu32 "\n", uio.comp.param.cmdSpecific);
    printf("\t\treserved: %" PRIu32 "\n", uio.comp.reserved);
    printf("\t\tsqHdPtr: %" PRIu32 "\n", uio.comp.sqHdPtr);
    printf("\t\tsqID: %" PRIu32 "\n", uio.comp.sqID);
    printf("\t\tcmdID: %" PRIu32 "\n", uio.comp.cmdID);
    printf("\t\tphaseTag: %" PRIu32 "\n", uio.comp.phaseTag);
    printf("\t\tSC: %" PRIu32 "\n", uio.comp.SC);
    printf("\t\tSCT: %" PRIu32 "\n", uio.comp.SCT);
    printf("\t\tmore: %" PRIu32 "\n", uio.comp.more);
    printf("\t\tnoRetry: %" PRIu32 "\n", uio.comp.noRetry);

    printf("\tNamespaceID: %" PRIu8 "\n", uio.namespaceID);
    printf("\tDirection: %" PRIu8 "\n", uio.direction);
    printf("\tReserved: %" PRIu16 "\n", uio.reserved);
    printf(
        "\tStatus: %" PRIX16 "\n",
        uio.status); // If this starts with 0x0BADxxxx then it is indicating an error. Use vmware API to translate it.
    printf("\tLength: %" PRIu32 "\n", uio.length);
    printf("\tMeta Length: %" PRIu32 "\n", uio.meta_length);
    printf("\tTimeout (us): %" PRIu64 "\n", uio.timeoutUs);
    printf("\tAddress: %" PRIu64 "\n", uio.addr);
    printf("\tMeta Address: %" PRIu64 "\n", uio.meta_addr);
}

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = SUCCESS; // NVME_SC_SUCCESS;//This defined value used to exist in some version of nvme.h but is
                                 // missing in nvme_ioctl.h...it was a value of zero, so this should be ok.
    int           ioctlret = 0;
    struct usr_io uio;
    DECLARE_SEATIMER(cmdtimer);

#    ifdef _DEBUG
    printf("-->%s\n", __FILE__);
    printf("-->%s\n", __FUNCTION__);
#    endif

    safe_memset(&uio, sizeof(struct usr_io), 0, sizeof(struct usr_io));

    if (nvmeIoCtx == M_NULLPTR)
    {
#    ifdef _DEBUG
        printf("-->%s\n", __FUNCTION__);
#    endif
        return BAD_PARAMETER;
    }

    switch (nvmeIoCtx->commandType)
    {
    case NVM_ADMIN_CMD:
        safe_memcpy(&(uio.cmd), sizeof(struct nvme_cmd), &(nvmeIoCtx->cmd.adminCmd), sizeof(nvmeCommands));

        if ((nvmeIoCtx->commandDirection == XFER_NO_DATA) || (nvmeIoCtx->commandDirection == XFER_DATA_IN))
        {
            uio.direction = XFER_FROM_DEV;
        }
        else
        {
            uio.direction = XFER_TO_DEV;
        }

        uio.length = nvmeIoCtx->dataSize;
        uio.addr   = C_CAST(__typeof__(uio.addr), nvmeIoCtx->ptrData);
        if (nvmeIoCtx->cmd.adminCmd.nsid == 0 || nvmeIoCtx->cmd.adminCmd.nsid == NVME_ALL_NAMESPACES)
        {
            uio.namespaceID =
                C_CAST(vmk_uint8, -1); // this is what the header files say to do for non-specific namespace -TJE
        }
        else
        {
            uio.namespaceID = nvmeIoCtx->cmd.adminCmd.nsid;
        }
        uio.timeoutUs = nvmeIoCtx->timeout ? nvmeIoCtx->timeout * 1000 : 15000;

        errno = 0;
        start_Timer(&cmdtimer);
        // ioctlret = Nvme_AdminPassthru(nvmeIoCtx->device->os_info.nvmeFd, &uio);
        ioctlret = Nvme_Ioctl(nvmeIoCtx->device->os_info.nvmeFd, NVME_IOCTL_ADMIN_CMD, &uio);
        stop_Timer(&cmdtimer);
        nvmeIoCtx->device->os_info.last_error = errno;
        // Get error?
        if (ioctlret < 0 ||
            (uio.status & 0x0FFF0000) == 0x0BAD0000) // If this starts with 0x0BADxxxx then it is indicating an error.
                                                     // Use vmware API to translate it.
        {
            if ((uio.status & 0x0FFF0000) == 0x0BAD0000)
            {
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    printf("VMWare error: %s\n", get_VMK_API_Error(C_CAST(VMK_ReturnStatus, uio.status)));
                }
            }
            else
            {
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    if (nvmeIoCtx->device->os_info.last_error != 0)
                    {
                        printf("Error: ");
                        print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
                    }
                }
            }
            ret = OS_PASSTHROUGH_FAILURE;
        }
        nvmeIoCtx->commandCompletionData.commandSpecific = uio.comp.param.cmdSpecific;
        nvmeIoCtx->commandCompletionData.dw0Valid        = true;
        nvmeIoCtx->commandCompletionData.dw1Reserved     = uio.comp.reserved;
        nvmeIoCtx->commandCompletionData.dw1Valid        = true;
        nvmeIoCtx->commandCompletionData.sqIDandHeadPtr  = M_WordsTo4ByteValue(uio.comp.sqID, uio.comp.sqHdPtr);
        nvmeIoCtx->commandCompletionData.dw2Valid        = true;
        nvmeIoCtx->commandCompletionData.statusAndCID    = uio.comp.cmdID | (uio.comp.phaseTag << 16) |
                                                        (uio.comp.SC << 17) | (uio.comp.SCT << 25) |
                                                        (uio.comp.more << 30) | (uio.comp.noRetry << 31);
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        break;

    case NVM_CMD:
        safe_memcpy(&(uio.cmd), (struct nvme_cmd), &(nvmeIoCtx->cmd.nvmCmd), sizeof(nvmeCommands));

        if ((nvmeIoCtx->commandDirection == XFER_NO_DATA) || (nvmeIoCtx->commandDirection == XFER_DATA_IN))
        {
            uio.direction = XFER_FROM_DEV;
        }
        else
        {
            uio.direction = XFER_TO_DEV;
        }

        uio.length = nvmeIoCtx->dataSize;
        uio.addr   = C_CAST(__typeof__(uio.addr), nvmeIoCtx->ptrData);
        if (nvmeIoCtx->cmd.nvmCmd.nsid == 0 || nvmeIoCtx->cmd.nvmCmd.nsid == NVME_ALL_NAMESPACES)
        {
            uio.namespaceID =
                C_CAST(vmk_uint8, -1); // this is what the header files say to do for non-specific namespace -TJE
        }
        else
        {
            uio.namespaceID = nvmeIoCtx->cmd.nvmCmd.nsid;
        }

        uio.timeoutUs = nvmeIoCtx->timeout ? nvmeIoCtx->timeout * 1000 : 15000;
        errno         = 0;
        start_Timer(&cmdtimer);
        ioctlret = Nvme_Ioctl(nvmeIoCtx->device->os_info.nvmeFd, NVME_IOCTL_IO_CMD, &uio);
        stop_Timer(&cmdtimer);
        nvmeIoCtx->device->os_info.last_error = errno;
        // Get error?
        if (ioctlret < 0 ||
            (uio.status & 0x0FFF0000) == 0x0BAD0000) // If this starts with 0x0BADxxxx then it is indicating an error.
                                                     // Use vmware API to translate it.
        {
            if ((uio.status & 0x0FFF0000) == 0x0BAD0000)
            {
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    printf("VMWare error: %s\n", get_VMK_API_Error(C_CAST(VMK_ReturnStatus, uio.status)));
                }
            }
            else
            {
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    if (nvmeIoCtx->device->os_info.last_error != 0)
                    {
                        printf("Error: ");
                        print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
                    }
                }
            }
            ret = OS_PASSTHROUGH_FAILURE;
        }
        nvmeIoCtx->commandCompletionData.commandSpecific = uio.comp.param.cmdSpecific;
        nvmeIoCtx->commandCompletionData.dw0Valid        = true;
        nvmeIoCtx->commandCompletionData.dw1Reserved     = uio.comp.reserved;
        nvmeIoCtx->commandCompletionData.dw1Valid        = true;
        nvmeIoCtx->commandCompletionData.sqIDandHeadPtr  = M_WordsTo4ByteValue(uio.comp.sqID, uio.comp.sqHdPtr);
        nvmeIoCtx->commandCompletionData.dw2Valid        = true;
        nvmeIoCtx->commandCompletionData.statusAndCID    = uio.comp.cmdID | (uio.comp.phaseTag << 16) |
                                                        (uio.comp.SC << 17) | (uio.comp.SCT << 25) |
                                                        (uio.comp.more << 30) | (uio.comp.noRetry << 31);
        nvmeIoCtx->commandCompletionData.dw3Valid = true;

        break;
    default:
        return BAD_PARAMETER;
        break;
    }

#    ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#    endif

    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(cmdtimer);

    if (nvmeIoCtx->device->delay_io)
    {
        delay_Milliseconds(nvmeIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", nvmeIoCtx->device->delay_io);
        }
    }

    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Reset(M_ATTR_UNUSED tDevice* device)
{
    // This is a stub. If this is possible, this should perform an nvme reset;
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Subsystem_Reset(M_ATTR_UNUSED tDevice* device)
{
    // This is a stub. If this is possible, this should perform an nvme subsystem reset;
    return OS_COMMAND_NOT_AVAILABLE;
}

// Case to remove this from sg_helper.h/c and have a platform/lin/pci-herlper.h vs platform/win/pci-helper.c

eReturnValues pci_Read_Bar_Reg(tDevice* device, uint8_t* pData, uint32_t dataSize)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret     = UNKNOWN;
    int           fd      = 0;
    void*         barRegs = M_NULLPTR;
    DECLARE_ZERO_INIT_ARRAY(char, sysfsPath, PATH_MAX);
    snprintf_err_handle(sysfsPath, PATH_MAX, "/sys/block/%s/device/resource0", device->os_info.name);
    fd = open(sysfsPath, O_RDONLY);
    if (fd >= 0)
    {
        //
        barRegs = mmap(0, dataSize, PROT_READ, MAP_SHARED, fd, 0);
        if (barRegs != MAP_FAILED)
        {
            ret = SUCCESS;
            safe_memcpy(pData, dataSize, barRegs, dataSize);
        }
        else
        {
            ret = FAILURE;
        }
        close(fd);
    }
    else
    {
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("couldn't open device %s\n", device->os_info.name);
        }
        ret = BAD_PARAMETER;
    }
    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    M_USE_UNUSED(device);
    M_USE_UNUSED(pData);
    M_USE_UNUSED(dataSize);
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
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

eReturnValues os_Lock_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        // Not sure what to do
    }
    else
    {
        // Get flags
        int flags = fcntl(device->os_info.fd, F_GETFL);
        // disable O_NONBLOCK
        flags &= ~O_NONBLOCK;
        // Set Flags
        fcntl(device->os_info.fd, F_SETFL, flags);
    }
    return ret;
}

eReturnValues os_Unlock_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        // Not sure what to do
    }
    else
    {
        // Get flags
        int flags = fcntl(device->os_info.fd, F_GETFL);
        // enable O_NONBLOCK
        flags |= O_NONBLOCK;
        // Set Flags
        fcntl(device->os_info.fd, F_SETFL, flags);
    }
    return ret;
}

eReturnValues os_Update_File_System_Cache(M_ATTR_UNUSED tDevice* device)
{
    // note: linux code for blkrrprt might work
    return NOT_SUPPORTED;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Unmount_File_Systems_On_Device(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}
