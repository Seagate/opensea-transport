// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file netbsd_openbsd_helper.c handle functionality to scan for devices and issue commands in
// both netbsd and openbsd

#include "netbsd_openbsd_helper.h"
#include "bsd_ata_passthrough.h"
#include "bsd_mount_info.h"
#include "bsd_scsi_passthrough.h"

extern bool validate_Device_Struct(versionBlock);

bool os_Is_Infinite_Timeout_Supported(void)
{
    return false;
}

static int rsd_filter(const struct dirent* entry)
{
    int daHandle = strncmp("rsd", entry->d_name, 2);
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

static int rwd_filter(const struct dirent* entry)
{
    int adaHandle = strncmp("rwd", entry->d_name, 3);
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

eReturnValues get_Device(const char* filename, tDevice* device)
{
    int           fd           = 0;
    eReturnValues ret          = SUCCESS;
    char*         deviceHandle = M_NULLPTR;
    errno_t       duphandle    = safe_strdup(&deviceHandle, filename);
    if (duphandle != 0 || deviceHandle == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }

    fd = open(duphandle, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        perror("Opening device handle");
        return FAILURE;
    }
    device->os_info.fd = fd;

    // setup any other necessary enumeration of the device
    device->drive_info.drive_type     = SCSI_DRIVE;
    device->drive_info.interface_type = SCSI_INTERFACE;
    if (strstr(filename, "wd"))
    {
        device->drive_info.drive_type                                 = ATA_DRIVE;
        device->drive_info.interface_type                             = IDE_INTERFACE;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly   = true;
        device->drive_info.passThroughHacks.someHacksSetByOSDiscovery = true;
        // TODO: See if there is some other method available to get some kind of ATA address
        device->os_info.addresstype = 0;
        device->os_info.bus         = 0;
        device->os_info.target      = 0;
        device->os_info.lun         = 0;
    }
    else
    {
        // assuming scsi for now
        get_BSD_SCSI_Address(device->os_info.fd, &device->os_info.addresstype, &device->os_info.bus,
                             &device->os_info.target, &device->os_info.lun);
    }
    set_BSD_Device_Partition_Info(device);
    ret = fill_Drive_Info_Data(device);
    return ret;
}

eReturnValues close_Device(tDevice* dev)
{
    if (dev->os_info.fd)
    {
        close(dev->os_info.fd);
        dev->os_info.fd              = -1;
        dev->os_info.passthroughType = BSD_PASSTHROUGH_NOT_SET;
    }
    return SUCCESS;
}

eReturnValues get_Device_Count(uint32_t* numberOfDevices, M_ATTR_UNUSED uint64_t flags)
{
    int num_wd_devs = 0;
    int num_sd_devs = 0;
    // int num_nvme_devs = 0;

    struct dirent** wdnamelist  = M_NULLPTR;
    struct dirent** sdanamelist = M_NULLPTR;
    // struct dirent** nvmenamelist = M_NULLPTR;

    num_wd_devs = scandir("/dev", &wdnamelist, rwd_filter, alphasort);
    num_sd_devs = scandir("/dev", &sdanamelist, rsd_filter, alphasort);
#if !defined(DISABLE_NVME_PASSTHROUGH)
    // num_nvme_devs = scandir("/dev", &nvmenamelist, nvme_filter, alphasort);
#endif

    // free the list of names to not leak memory
    for (int iter = 0; iter < num_wd_devs; ++iter)
    {
        safe_free_dirent(&wdnamelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &wdnamelist));
    // free the list of names to not leak memory
    for (int iter = 0; iter < num_sd_devs; ++iter)
    {
        safe_free_dirent(&sdanamelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &sdanamelist));

    // free the list of names to not leak memory
    // for (int iter = 0; iter < num_nvme_devs; ++iter)
    // {
    //     safe_free_dirent(&nvmenamelist[iter]);
    // }
    // safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &nvmenamelist));

    if (num_wd_devs > 0)
    {
        *numberOfDevices += C_CAST(uint32_t, num_wd_devs);
    }
    if (num_sd_devs > 0)
    {
        *numberOfDevices += C_CAST(uint32_t, num_sd_devs);
    }
    // if (num_nvme_devs > 0)
    // {
    //     *numberOfDevices += C_CAST(uint32_t, num_nvme_devs);
    // }

    return SUCCESS;
}

#define BSD_DEV_NAME_LEN 80
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
    DECLARE_ZERO_INIT_ARRAY(char, name, BSD_DEV_NAME_LEN);
    int      fd          = 0;
    tDevice* d           = M_NULLPTR;
    int      scandirres  = 0;
    uint32_t num_sd_devs = UINT32_C(0);
    uint32_t num_wd_devs = UINT32_C(0);
    // uint32_t num_nvme_devs = UINT32_C(0);

    struct dirent** sdnamelist = M_NULLPTR;
    struct dirent** wdnamelist = M_NULLPTR;
    // struct dirent** nvmenamelist = M_NULLPTR;

    scandirres = scandir("/dev", &sdnamelist, rsd_filter, alphasort);
    if (scandirres > 0)
    {
        num_sd_devs = C_CAST(uint32_t, scandirres);
    }
    scandirres = scandir("/dev", &wdnamelist, rwd_filter, alphasort);
    if (scandirres > 0)
    {
        num_wd_devs = C_CAST(uint32_t, scandirres);
    }
#if !defined(DISABLE_NVME_PASSTHROUGH)
    // scandirres = scandir("/dev", &nvmenamelist, nvme_filter, alphasort);
    // if (scandirres > 0)
    // {
    //     num_nvme_devs = C_CAST(uint32_t, scandirres);
    // }
#endif
    uint32_t totalDevs = num_sd_devs + num_wd_devs; // + num_nvme_devs;

    char**   devs = M_REINTERPRET_CAST(char**, safe_calloc(totalDevs + 1, sizeof(char*)));
    uint32_t i    = UINT32_C(0);
    uint32_t j    = UINT32_C(0);
    uint32_t k    = UINT32_C(0);
    for (i = 0; i < num_sd_devs; ++i)
    {
        size_t devNameStringLength = (safe_strlen("/dev/") + safe_strlen(sdnamelist[i]->d_name) + 1) * sizeof(char);
        devs[i]                    = M_REINTERPRET_CAST(char*, safe_malloc(devNameStringLength));
        snprintf_err_handle(devs[i], devNameStringLength, "/dev/%s", sdnamelist[i]->d_name);
        safe_free_dirent(&sdnamelist[i]);
    }
    for (j = 0; i < (num_sd_devs + num_wd_devs) && j < num_wd_devs; ++i, j++)
    {
        size_t devNameStringLength = (safe_strlen("/dev/") + safe_strlen(wdnamelist[j]->d_name) + 1) * sizeof(char);
        devs[i]                    = M_REINTERPRET_CAST(char*, safe_malloc(devNameStringLength));
        snprintf_err_handle(devs[i], devNameStringLength, "/dev/%s", wdnamelist[j]->d_name);
        safe_free_dirent(&wdnamelist[j]);
    }

    // for (k = 0; i < (totalDevs) && k < num_nvme_devs; ++i, ++j, ++k)
    // {
    //     size_t devNameStringLength = (safe_strlen("/dev/") + safe_strlen(nvmenamelist[k]->d_name) + 1) *
    //     sizeof(char); devs[i]                    = M_REINTERPRET_CAST(char*, safe_malloc(devNameStringLength));
    //     snprintf_err_handle(devs[i], devNameStringLength, "/dev/%s", nvmenamelist[k]->d_name);
    //     safe_free_dirent(&nvmenamelist[k]);
    // }

    devs[i] = M_NULLPTR; // Added this so the for loop down doesn't cause a segmentation fault.
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &sdnamelist));
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &wdnamelist));
    // safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &nvmenamelist));

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
             ((driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < totalDevs) && found < numberOfDevices); ++driveNumber)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, BSD_DEV_NAME_LEN, 0, BSD_DEV_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, BSD_DEV_NAME_LEN, "%s", devs[driveNumber]);
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
        else if (permissionDeniedCount == totalDevs)
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

eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    switch (scsiIoCtx->device->os_info.passthroughType)
    {
    case BSD_PASSTHROUGH_SCSI:
        return send_BSD_SCSI_IO(scsiIoCtx);
    case BSD_PASSTHROUGH_ATA:
        return send_BSD_ATA_IO(scsiIoCtx);
    default:
        return BAD_PARAMETER;
    }
}

eReturnValues os_Device_Reset(tDevice* device)
{
    switch (device->os_info.passthroughType)
    {
    case BSD_PASSTHROUGH_SCSI:
        return send_BSD_SCSI_Reset(device.os_info.fd);
    case BSD_PASSTHROUGH_ATA:
        return send_BSD_ATA_Reset(device.os_info.fd);
    default:
        return BAD_PARAMETER;
    }
}

eReturnValues os_Bus_Reset(tDevice* device)
{
    switch (device->os_info.passthroughType)
    {
    case BSD_PASSTHROUGH_SCSI:
        return send_BSD_SCSI_Bus_Reset(device.os_info.fd);
    case BSD_PASSTHROUGH_ATA:
        return OS_COMMAND_NOT_AVAILABLE;
    default:
        return BAD_PARAMETER;
    }
}

eReturnValues os_Controller_Reset(tDevice* device)
{
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues pci_Read_Bar_Reg(tDevice* device, uint8_t* pData, uint32_t dataSize)
{
    M_USE_UNUSED(device);
    M_USE_UNUSED(pData);
    M_USE_UNUSED(dataSize);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
    M_USE_UNUSED(nvmeIoCtx);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Reset(tDevice* device)
{
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Subsystem_Reset(tDevice* device)
{
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Lock_Device(tDevice* device)
{
    // flock?
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Unlock_Device(tDevice* device)
{
    // flock?
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Update_File_System_Cache(tDevice* device)
{
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_Unmount_File_Systems_On_Device(tDevice* device)
{
    return bsd_Unmount_From_Matching_Dev(device);
}

eReturnValues os_Erase_Boot_Sectors(tDevice* device)
{
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
}
