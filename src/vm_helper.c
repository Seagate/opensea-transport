// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h> // for close
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h> //for mmap pci reads. Potential to move. 
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <libgen.h>//for basename and dirname
#include <string.h>
#include "vm_helper.h"
#include "cmds.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"
#include "nvme_helper_func.h"
#include "sntl_helper.h"

#if defined(DEGUG_SCAN_TIME)
#include "common_platform.h"
#endif

    //If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return true;
}

extern bool validate_Device_Struct(versionBlock);

// Local helper functions for debugging
#if defined (_DEBUG)
static void print_io_hdr(sg_io_hdr_t *pIo)
{
    time_t time_now;
    char timeFormat[TIME_STRING_LENGTH];
    memset(timeFormat, 0, TIME_STRING_LENGTH);//clear this again before reusing it
    time_now = time(NULL);
    printf("\n%s: %s---------------------------------\n", __FUNCTION__, get_Current_Time_String(&time_now, timeFormat, TIME_STRING_LENGTH));
    printf("type int interface_id %d\n", pIo->interface_id);           /* [i] 'S' (required) */
    printf("type int  dxfer_direction %d\n", pIo->dxfer_direction);        /* [i] */
    printf("type unsigned char cmd_len 0x%x\n", pIo->cmd_len);      /* [i] */
    printf("type unsigned char mx_sb_len 0x%x\n", pIo->mx_sb_len);    /* [i] */
    printf("type unsigned short iovec_count 0x%x\n", pIo->iovec_count); /* [i] */
    printf("type unsigned int dxfer_len %d\n", pIo->dxfer_len);     /* [i] */
    printf("type void * dxferp %p\n", C_CAST(unsigned int *, pIo->dxferp));              /* [i], [*io] */
    printf("type unsigned char * cmdp %p\n", C_CAST(unsigned int *, pIo->cmdp));       /* [i], [*i]  */
    printf("type unsigned char * sbp %p\n", C_CAST(unsigned int *, pIo->sbp));        /* [i], [*o]  */
    printf("type unsigned int timeout %d\n", pIo->timeout);       /* [i] unit: millisecs */
    printf("type unsigned int flags 0x%x\n", pIo->flags);         /* [i] */
    printf("type int pack_id %d\n", pIo->pack_id);                /* [i->o] */
    printf("type void * usr_ptr %p\n", C_CAST(unsigned int *, pIo->usr_ptr));             /* [i->o] */
    printf("type unsigned char status 0x%x\n", pIo->status);       /* [o] */
    printf("type unsigned char maskedStatus 0x%x\n", pIo->masked_status); /* [o] */
    printf("type unsigned char msg_status 0x%x\n", pIo->msg_status);   /* [o] */
    printf("type unsigned char sb_len_wr 0x%x\n", pIo->sb_len_wr);    /* [o] */
    printf("type unsigned short host_status 0x%x\n", pIo->host_status); /* [o] */
    printf("type unsigned short driver_status 0x%x\n", pIo->driver_status); /* [o] */
    printf("type int resid %d\n", pIo->resid);                  /* [o] */
    printf("type unsigned int duration %d\n", pIo->duration);      /* [o] */
    printf("type unsigned int info 0x%x\n", pIo->info);          /* [o] */
    printf("-----------------------------------------\n");
}
#endif //_DEBUG

static int sg_filter(const struct dirent *entry)
{
    return !strncmp("sg", entry->d_name, 2);
}

static int drive_filter(const struct dirent *entry)
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
    if (partition != NULL)
    {
        return !driveHandle;
    }
    else
    {
        return driveHandle;
    }

}

//get sd devices, but ignore any partition number information since that isn't something we can actually send commands to
static int sd_filter(const struct dirent *entry)
{
    int sdHandle = strncmp("sd", entry->d_name, 2);
    if (sdHandle != 0)
    {
        return !sdHandle;
    }
    char* partition = strpbrk(entry->d_name, "0123456789");
    if (partition != NULL)
    {
        return sdHandle;
    }
    else
    {
        return !sdHandle;
    }
}


//This function is not currently used or tested...if we need to make more changes for pre-2.6 kernels, we may need this.
//bool does_Kernel_Support_SysFS_Link_Mapping()
//{
//    bool linkMappingSupported = false;
//    //kernel version 2.6 and higher is required to map the handles between sg and sd/sr/st/scd
//    OSVersionNumber linuxVersion;
//    memset(&linuxVersion, 0, sizeof(OSVersionNumber));
//    if(SUCCESS == get_Operating_System_Version_And_Name(&linuxVersion, NULL))
//    {
//        if (linuxVersion.versionType.linuxVersion.kernelVersion >= 2 && linuxVersion.versionType.linuxVersion.majorVersion >= 6)
//        {
//            linkMappingSupported = true;
//        }
//    }
//    return linkMappingSupported;
//}

static bool is_Block_Device_Handle(const char *handle)
{
    bool isBlockDevice = false;
    if (handle && strlen(handle))
    {
        if (strstr(handle, "sd") || strstr(handle, "st") || strstr(handle, "sr") || strstr(handle, "scd"))
        {
            isBlockDevice = true;
        }
    }
    return isBlockDevice;
}

static bool is_SCSI_Generic_Handle(const char *handle)
{
    bool isGenericDevice = false;
    if (handle && strlen(handle))
    {
        if (strstr(handle, "sg") && !strstr(handle, "bsg"))
        {
            isGenericDevice = true;
        }
    }
    return isGenericDevice;
}

static bool is_Block_SCSI_Generic_Handle(const char *handle)
{
    bool isBlockGenericDevice = false;
    if (handle && strlen(handle))
    {
        if (strstr(handle, "bsg"))
        {
            isBlockGenericDevice = true;
        }
    }
    return isBlockGenericDevice;
}


//while similar to the function below, this is used only by get_Device to set up some fields in the device structure for the above layers
static void set_Device_Fields_From_Handle(const char* handle, tDevice *device)
{
    //check if it's a block handle, bsg, or scsi_generic handle, then setup the path we need to read.
    if (handle && device)
    {
        if (strstr(handle, "nvme") != NULL)
        {
            size_t nvmHandleLen = strlen(handle) + 1;
            char *nvmHandle = C_CAST(char*, calloc(nvmHandleLen, sizeof(char)));
            snprintf(nvmHandle, nvmHandleLen, "%s", handle);
            device->drive_info.interface_type = NVME_INTERFACE;
            device->drive_info.drive_type = NVME_DRIVE;
            snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", nvmHandle);
            snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", basename(nvmHandle));
        }
        else //not NVMe, so we need to do some investigation of the handle. NOTE: this requires 2.6 and later kernel since it reads a link in the /sys/class/ filesystem
        {
            bool incomingBlock = false;//only set for SD!
            bool bsg = false;
            char incomingHandleClassPath[PATH_MAX] = { 0 };
            //char *incomingClassName = NULL;
            common_String_Concat(incomingHandleClassPath, PATH_MAX, "/sys/class/");
            if (is_Block_Device_Handle(handle))
            {
                common_String_Concat(incomingHandleClassPath, PATH_MAX, "block/");
                incomingBlock = true;
                //incomingClassName = strdup("block");
            }
            else if (is_Block_SCSI_Generic_Handle(handle))
            {
                bsg = true;
                common_String_Concat(incomingHandleClassPath, PATH_MAX, "bsg/");
                //incomingClassName = strdup("bsg");
            }
            else if (is_SCSI_Generic_Handle(handle))
            {
                common_String_Concat(incomingHandleClassPath, PATH_MAX, "scsi_generic/");
                //incomingClassName = strdup("scsi_generic");
            }
            else
            {
                //unknown. Time to exit gracefully
                device->drive_info.interface_type = SCSI_INTERFACE;
                device->drive_info.drive_type = UNKNOWN_DRIVE;
                device->drive_info.media_type = MEDIA_UNKNOWN;
                return;
            }
            //first make sure this directory exists
            struct stat inHandleStat;
            memset(&inHandleStat, 0, sizeof(struct stat));
            if (stat(incomingHandleClassPath, &inHandleStat) == 0 && S_ISDIR(inHandleStat.st_mode))
            {
                struct stat link;
                memset(&link, 0, sizeof(struct stat));
                common_String_Concat(incomingHandleClassPath, PATH_MAX, basename(C_CAST(char*, handle)));
                //now read the link with the handle appended on the end
                if (lstat(incomingHandleClassPath, &link) == 0 && S_ISLNK(link.st_mode))
                {
                    char inHandleLink[PATH_MAX] = { 0 };
                    if (readlink(incomingHandleClassPath, inHandleLink, PATH_MAX) > 0)
                    {
                        //Read the link and set up all the fields we want to setup.
                        //Start with setting the device interface
                        //example ata device link: ../../devices/pci0000:00/0000:00:1f.2/ata8/host8/target8:0:0/8:0:0:0/scsi_generic/sg2
                        //example usb device link: ../../devices/pci0000:00/0000:00:1c.1/0000:03:00.0/usb4/4-1/4-1:1.0/host21/target21:0:0/21:0:0:0/scsi_generic/sg4
                        //example sas device link: ../../devices/pci0000:00/0000:00:1c.0/0000:02:00.0/host0/port-0:0/end_device-0:0/target0:0:0/0:0:0:0/scsi_generic/sg3
                        //example firewire device link: ../../devices/pci0000:00/0000:00:1c.5/0000:04:00.0/0000:05:09.0/0000:0b:00.0/0000:0c:02.0/fw1/fw1.0/host13/target13:0:0/13:0:0:0/scsi_generic/sg3
                        //example sata over sas device link: ../../devices/pci0000:00/0000:00:1c.0/0000:02:00.0/host0/port-0:1/end_device-0:1/target0:0:1/0:0:1:0/scsi_generic/sg5
                        if (strstr(inHandleLink, "ata") != 0)
                        {
#if defined (_DEBUG)
                            printf("ATA interface!\n");
#endif
                            device->drive_info.interface_type = IDE_INTERFACE;
                            //get vendor and product IDs of the controller attached to this device.
                            char fullPciPath[PATH_MAX] = { 0 };
                            snprintf(fullPciPath, PATH_MAX, "%s", inHandleLink);

                            fullPciPath[0] = '/';
                            fullPciPath[1] = 's';
                            fullPciPath[2] = 'y';
                            fullPciPath[3] = 's';
                            fullPciPath[4] = '/';
                            memmove(&fullPciPath[5], &fullPciPath[6], strlen(fullPciPath));

                            uint64_t newStrLen = strstr(fullPciPath, "/ata") - fullPciPath + 1;
                            char *pciPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
                            if (pciPath)
                            {
                                snprintf(pciPath, PATH_MAX, "%.*s/vendor", C_CAST(int, newStrLen - 1), fullPciPath);
                                //printf("shortened Path = %s\n", dirname(pciPath));
                                FILE *temp = NULL;
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.vendorID))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        //printf("Got vendor as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                pciPath = dirname(pciPath);//remove vendor from the end
                                common_String_Concat(pciPath, PATH_MAX, "/device");
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.productID))
                                    {
                                        device->drive_info.adapter_info.productIDValid = true;
                                        //printf("Got product as %" PRIX16 "h\n", device->drive_info.adapter_info.productID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                //Store revision data. This seems to be in the bcdDevice file.
                                pciPath = dirname(pciPath);//remove device from the end
                                common_String_Concat(pciPath, PATH_MAX, "/revision");
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    uint8_t pciRev = 0;
                                    if (1 == fscanf(temp, "0x%" SCNx8, &pciRev))
                                    {
                                        device->drive_info.adapter_info.revision = pciRev;
                                        device->drive_info.adapter_info.revisionValid = true;
                                        //printf("Got revision as %" PRIX16 "h\n", device->drive_info.adapter_info.revision);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                safe_Free(pciPath)
                                device->drive_info.adapter_info.infoType = ADAPTER_INFO_PCI;
                            }
                        }
                        else if (strstr(inHandleLink, "usb") != 0)
                        {
#if defined (_DEBUG)
                            printf("USB interface!\n");
#endif
                            device->drive_info.interface_type = USB_INTERFACE;
                            //set the USB VID and PID. NOTE: There may be a better way to do this, but this seems to work for now.
                            char fullPciPath[PATH_MAX] = { 0 };
                            snprintf(fullPciPath, PATH_MAX, "%s", inHandleLink);

                            fullPciPath[0] = '/';
                            fullPciPath[1] = 's';
                            fullPciPath[2] = 'y';
                            fullPciPath[3] = 's';
                            fullPciPath[4] = '/';
                            memmove(&fullPciPath[5], &fullPciPath[6], strlen(fullPciPath));

                            uint64_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath + 1;
                            char *usbPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
                            if (usbPath)
                            {
                                snprintf(usbPath, PATH_MAX, "%.*s", C_CAST(int, newStrLen - 1), fullPciPath);
                                usbPath = dirname(usbPath);
                                //printf("full USB Path = %s\n", usbPath);
                                //now that the path is correct, we need to read the files idVendor and idProduct
                                common_String_Concat(usbPath, PATH_MAX, "/idVendor");
                                //printf("idVendor USB Path = %s\n", usbPath);
                                FILE *temp = NULL;
                                temp = fopen(usbPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "%" SCNx32, &device->drive_info.adapter_info.vendorID))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        //printf("Got vendor ID as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                usbPath = dirname(usbPath);//remove idVendor from the end
                                //printf("full USB Path = %s\n", usbPath);
                                common_String_Concat(usbPath, PATH_MAX, "/idProduct");
                                //printf("idProduct USB Path = %s\n", usbPath);
                                temp = fopen(usbPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "%" SCNx32, &device->drive_info.adapter_info.productID))
                                    {
                                        device->drive_info.adapter_info.productIDValid = true;
                                        //printf("Got product ID as %" PRIX16 "h\n", device->drive_info.adapter_info.productID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                //Store revision data. This seems to be in the bcdDevice file.
                                usbPath = dirname(usbPath);//remove idProduct from the end
                                common_String_Concat(usbPath, PATH_MAX, "/bcdDevice");
                                temp = fopen(usbPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "%" SCNx32, &device->drive_info.adapter_info.revision))
                                    {
                                        device->drive_info.adapter_info.revisionValid = true;
                                        //printf("Got revision as %" PRIX16 "h\n", device->drive_info.adapter_info.revision);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                safe_Free(usbPath)
                                device->drive_info.adapter_info.infoType = ADAPTER_INFO_USB;
                            }
                        }
                        else if (strstr(inHandleLink, "fw") != 0)
                        {
#if defined (_DEBUG)
                            printf("FireWire interface!\n");
#endif
                            device->drive_info.interface_type = IEEE_1394_INTERFACE;
                            //TODO: investigate some way of saving vendor/product like information for firewire.
                            char fullFWPath[PATH_MAX] = { 0 };
                            snprintf(fullFWPath, PATH_MAX, "%s", inHandleLink);

                            fullFWPath[0] = '/';
                            fullFWPath[1] = 's';
                            fullFWPath[2] = 'y';
                            fullFWPath[3] = 's';
                            fullFWPath[4] = '/';
                            memmove(&fullFWPath[5], &fullFWPath[6], strlen(fullFWPath));

                            //now we need to go up a few directories to get the modalias file to parse
                            uint64_t newStrLen = strstr(fullFWPath, "/host") - fullFWPath + 1;
                            char *fwPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
                            if (fwPath)
                            {
                                snprintf(fwPath, PATH_MAX, "%.*s/modalias", C_CAST(int, newStrLen - 1), fullFWPath);
                                //printf("full FW Path = %s\n", dirname(fwPath));
                                //printf("modalias FW Path = %s\n", fwPath);
                                FILE *temp = NULL;
                                temp = fopen(fwPath, "r");
                                if (temp)
                                {
                                    //This file contains everything in one place. Otherwise we would need to parse multiple files at slightly different paths to get everything - TJE
                                    if (4 == fscanf(temp, "ieee1394:ven%8" SCNx32 "mo%8" SCNx32 "sp%8" SCNx32 "ver%8" SCNx32, &device->drive_info.adapter_info.vendorID, &device->drive_info.adapter_info.productID, &device->drive_info.adapter_info.specifierID, &device->drive_info.adapter_info.revision))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        device->drive_info.adapter_info.productIDValid = true;
                                        device->drive_info.adapter_info.specifierIDValid = true;
                                        device->drive_info.adapter_info.revisionValid = true;
                                        //printf("Got vendor ID as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                        //printf("Got product ID as %" PRIX16 "h\n", device->drive_info.adapter_info.productID);
                                        //printf("Got specifier ID as %" PRIX16 "h\n", device->drive_info.adapter_info.specifierID);
                                        //printf("Got revision ID as %" PRIX16 "h\n", device->drive_info.adapter_info.revision);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                device->drive_info.adapter_info.infoType = ADAPTER_INFO_IEEE1394;
                                safe_Free(fwPath)
                            }

                        }
                        //if the link doesn't conatin ata or usb in it, then we are assuming it's scsi since scsi doesn't have a nice simple string to check
                        else
                        {
#if defined (_DEBUG)
                            printf("SCSI interface!\n");
#endif
                            device->drive_info.interface_type = SCSI_INTERFACE;
                            //get vendor and product IDs of the controller attached to this device.

                            char fullPciPath[PATH_MAX] = { 0 };
                            snprintf(fullPciPath, PATH_MAX, "%s", inHandleLink);

                            fullPciPath[0] = '/';
                            fullPciPath[1] = 's';
                            fullPciPath[2] = 'y';
                            fullPciPath[3] = 's';
                            fullPciPath[4] = '/';
                            memmove(&fullPciPath[5], &fullPciPath[6], strlen(fullPciPath));
                            //need to trim the path down now since it can vary by controller:
                            //adaptec: /sys/devices/pci0000:00/0000:00:02.0/0000:02:00.0/host0/target0:1:0/0:1:0:0/scsi_generic/sg2
                            //lsi: /sys/devices/pci0000:00/0000:00:02.0/0000:02:00.0/host0/port-0:16/end_device-0:16/target0:0:16/0:0:16:0/scsi_generic/sg4
                            //The best way seems to break by the word "host" at this time.
                            //printf("Full pci path: %s\n", fullPciPath);
                            //printf("/host location string: %s\n", strstr(fullPciPath, "/host"));
                            //printf("FULL: %" PRIXPTR "\t/HOST: %" PRIXPTR "\n", C_CAST(uintptr_t, fullPciPath), C_CAST(uintptr_t, strstr(fullPciPath, "/host")));
                            uint64_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath + 1;
                            char *pciPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
                            if (pciPath)
                            {
                                snprintf(pciPath, PATH_MAX, "%.*s/vendor", C_CAST(int, newStrLen - 1), fullPciPath);
                                //printf("Shortened PCI Path: %s\n", dirname(pciPath));
                                FILE *temp = NULL;
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.vendorID))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        //printf("Got vendor as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                pciPath = dirname(pciPath);//remove vendor from the end
                                common_String_Concat(pciPath, PATH_MAX, "/device");
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if (1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.productID))
                                    {
                                        device->drive_info.adapter_info.productIDValid = true;
                                        //printf("Got product as %" PRIX16 "h\n", device->drive_info.adapter_info.productID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                //Store revision data. This seems to be in the bcdDevice file.
                                pciPath = dirname(pciPath);//remove device from the end
                                common_String_Concat(pciPath, PATH_MAX, "/revision");
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    uint8_t pciRev = 0;
                                    if (1 == fscanf(temp, "0x%" SCNx8, &pciRev))
                                    {
                                        device->drive_info.adapter_info.revision = pciRev;
                                        device->drive_info.adapter_info.revisionValid = true;
                                        //printf("Got revision as %" PRIX16 "h\n", device->drive_info.adapter_info.revision);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                device->drive_info.adapter_info.infoType = ADAPTER_INFO_PCI;
                                safe_Free(pciPath)
                            }
                        }
                        char *baseLink = basename(inHandleLink);
                        //Now we will set up the device name, etc fields in the os_info structure.
                        if (bsg)
                        {
                            snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "/dev/bsg/%s", baseLink);
                        }
                        else
                        {
                            snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s", baseLink);
                        }
                        snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", baseLink);

                        //printf("getting SCSI address\n");
                        //set the scsi address field
                        char *scsiAddress = basename(dirname(dirname(inHandleLink)));//SCSI address should be 2nd from the end of the link
                        if (scsiAddress)
                        {
                            char *saveptr = NULL;
                            rsize_t addrlen = strlen(scsiAddress);
                            char *token = common_String_Token(scsiAddress, &addrlen, ":", *saveptr);
                            uint8_t counter = 0;
                            while (token)
                            {
                                switch (counter)
                                {
                                case 0://host
                                    sysFsInfo->scsiAddress.host = C_CAST(uint8_t, strtoul(token, NULL, 10));
                                    break;
                                case 1://bus
                                    sysFsInfo->scsiAddress.channel = C_CAST(uint8_t, strtoul(token, NULL, 10));
                                    break;
                                case 2://target
                                    sysFsInfo->scsiAddress.target = C_CAST(uint8_t, strtoul(token, NULL, 10));
                                    break;
                                case 3://lun
                                    sysFsInfo->scsiAddress.lun = C_CAST(uint8_t, strtoul(token, NULL, 10));
                                    break;
                                default:
                                    break;
                                }
                                token = common_String_Token(NULL, &addrlen, ":", &saveptr);
                                ++counter;
                            }
                            if (counter == 4)
                            {
                                device->os_info.scsiAddressValid = true;
                            }
                        }
                        //printf("attempting to map the handle\n");
                        //Lastly, call the mapping function to get the matching block handle and check what we got to set ATAPI, TAPE or leave as-is. Setting these is necessary to prevent talking to ATAPI as HDD due to overlapping A1h opcode
                        char *block = NULL;
                        char *gen = NULL;
                        if (SUCCESS == map_Block_To_Generic_Handle(handle, &gen, &block))
                        {
                            //printf("successfully mapped the handle. gen = %s\tblock=%s\n", gen, block);
                            //Our incoming handle SHOULD always be sg/bsg, but just in case, we need to check before we setup the second handle (mapped handle) information
                            if (incomingBlock)
                            {
                                //block device handle was sent into here (and we made it this far...unlikely)
                                //Secondary handle will be a generic handle
                                if (is_Block_SCSI_Generic_Handle(gen))
                                {
                                    device->os_info.secondHandleValid = true;
                                    snprintf(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/bsg/%s", gen);
                                    snprintf(device->os_info.secondFriendlyName, OS_SECOND_HANDLE_NAME_LENGTH, "%s", gen);
                                }
                                else
                                {
                                    device->os_info.secondHandleValid = true;
                                    snprintf(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/%s", gen);
                                    snprintf(device->os_info.secondFriendlyName, OS_SECOND_HANDLE_NAME_LENGTH, "%s", gen);
                                }
                            }
                            else
                            {
                                //generic handle was sent in
                                //secondary handle will be a block handle
                                device->os_info.secondHandleValid = true;
                                snprintf(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/%s", block);
                                snprintf(device->os_info.secondFriendlyName, OS_SECOND_HANDLE_NAME_LENGTH, "%s", block);
                            }

                            if (strstr(block, "sr") || strstr(block, "scd"))
                            {
                                device->drive_info.drive_type = ATAPI_DRIVE;
                            }
                            else if (strstr(block, "st"))
                            {
                                device->drive_info.drive_type = LEGACY_TAPE_DRIVE;
                            }
                        }
                        //printf("Finish handle mapping\n");
                        safe_Free(block)
                        safe_Free(gen)
                    }
                    else
                    {
                        //couldn't read the link...for who knows what reason...
                    }
                }
                else
                {
                    //Not a link...nothing further to do
                }
            }
        }
    }
    return;
}

//map a block handle (sd) to a generic handle (sg or bsg)
//incoming handle can be either sd, sg, or bsg type
//TODO: handle kernels before 2.6 in some other way. This depends on mapping in the file system provided by 2.6 and later.
eReturnValues map_Block_To_Generic_Handle(const char *handle, char **genericHandle, char **blockHandle)
{
    if (handle == NULL)
    {
        return BAD_PARAMETER;
    }
    //if the handle passed in contains "nvme" then we know it's a device on the nvme interface
    if (strstr(handle, "nvme") != NULL)
    {
        return NOT_SUPPORTED;
    }
    else
    {
        bool incomingBlock = false;//only set for SD!
        char incomingHandleClassPath[PATH_MAX] = { 0 };
        char *incomingClassName = NULL;
        common_String_Concat(incomingHandleClassPath, PATH_MAX, "/sys/class/");
        if (is_Block_Device_Handle(handle))
        {
            common_String_Concat(incomingHandleClassPath, PATH_MAX, "block/");
            incomingBlock = true;
            incomingClassName = strdup("block");
        }
        else if (is_Block_SCSI_Generic_Handle(handle))
        {
            common_String_Concat(incomingHandleClassPath, PATH_MAX, "bsg/");
            incomingClassName = strdup("bsg");
        }
        else if (is_SCSI_Generic_Handle(handle))
        {
            common_String_Concat(incomingHandleClassPath, PATH_MAX, "scsi_generic/");
            incomingClassName = strdup("scsi_generic");
        }
        //first make sure this directory exists
        struct stat inHandleStat;
        if (stat(incomingHandleClassPath, &inHandleStat) == 0 && S_ISDIR(inHandleStat.st_mode))
        {
            common_String_Concat(incomingHandleClassPath, PATH_MAX, basename(C_CAST(char*, handle)));
            //now read the link with the handle appended on the end
            char inHandleLink[PATH_MAX] = { 0 };
            if (readlink(incomingHandleClassPath, inHandleLink, PATH_MAX) > 0)
            {
                //printf("full in handleLink = %s\n", inHandleLink);
                //now we need to map it to a generic handle (sg...if sg not available, bsg)
                const char* scsiGenericClass = "/sys/class/scsi_generic/";
                const char* bsgClass = "/sys/class/bsg/";
                const char* blockClass = "/sys/class/block/";
                struct stat mapStat;
                char classPath[PATH_MAX] = { 0 };
                bool bsg = false;
                if (incomingBlock)
                {
                    //check for sg, then bsg
                    if (stat(scsiGenericClass, &mapStat) == 0 && S_ISDIR(mapStat.st_mode))
                    {
                        snprintf(classPath, PATH_MAX, "%s", scsiGenericClass);
                    }
                    else if (stat(bsgClass, &mapStat) == 0 && S_ISDIR(mapStat.st_mode))
                    {
                        snprintf(classPath, PATH_MAX, "%s", bsgClass);
                        bsg = true;
                    }
                    else
                    {
                        //printf ("could not map to generic class");
                        safe_Free(incomingClassName)
                        return NOT_SUPPORTED;
                    }
                }
                else
                {
                    //check for block
                    snprintf(classPath, PATH_MAX, "%s", blockClass);
                    if (!(stat(classPath, &mapStat) == 0 && S_ISDIR(mapStat.st_mode)))
                    {
                        //printf ("could not map to block class");
                        safe_Free(incomingClassName)
                        return NOT_SUPPORTED;
                    }
                }
                //now we need to loop through each think in the class folder, read the link, and check if we match.
                struct dirent **classList;
                int remains = 0;
                int numberOfItems = scandir(classPath, &classList, NULL /*not filtering anything. Just go through each item*/, alphasort);
                for (int iter = 0; iter < numberOfItems; ++iter)
                {
                    //printf("item = %s: %d of %d\n", classList[iter]->d_name,iter,numberOfItems);
                    //now we need to read the link for classPath/d_name into a buffer...then compare it to the one we read earlier.
                    size_t tempLen = strlen(classPath) + strlen(classList[iter]->d_name) + 1;
                    char *temp = C_CAST(char*, calloc(tempLen, sizeof(char)));
                    struct stat tempStat;
                    memset(&tempStat, 0, sizeof(struct stat));
                    snprintf(temp, tempLen, "%s%s", classPath, classList[iter]->d_name);
                    if (lstat(temp, &tempStat) == 0 && S_ISLNK(tempStat.st_mode))/*check if this is a link*/
                    {
                        char mapLink[PATH_MAX] = { 0 };
                        if (readlink(temp, mapLink, PATH_MAX) > 0)
                        {
                            char *className = NULL;
                            size_t classNameLength = 0;
                            //printf("read link as: %s\n", mapLink);
                            //now, we need to check the links and see if they match.
                            //NOTE: If we are in the block class, we will see sda, sda1, sda 2. These are all matches (technically)
                            //      We SHOULD match on the first disk without partition numbers since we did alphasort
                            //We need to match up until the class name (ex: block, bsg, scsi_generic)
                            if (incomingBlock)//block class
                            {
                                classNameLength = strlen("scsi_generic") + 1;
                                className = C_CAST(char*, calloc(classNameLength, sizeof(char)));
                                if (className)
                                {
                                    snprintf(className, classNameLength, "scsi_generic");
                                }
                            }
                            else if (bsg) //bsg class
                            {
                                classNameLength = strlen("bsg") + 1;
                                className = C_CAST(char*, calloc(classNameLength, sizeof(char)));
                                if (className)
                                {
                                    snprintf(className, classNameLength, "bsg");
                                }
                            }
                            else //scsi_generic class
                            {
                                classNameLength = strlen("block") + 1;
                                className = C_CAST(char*, calloc(classNameLength, sizeof(char)));
                                if (className)
                                {
                                    snprintf(className, classNameLength, "block");
                                }
                            }
                            if (className)
                            {
                                char *classPtr = strstr(mapLink, className);
                                //need to match up to the classname
                                if (NULL != classPtr && strncmp(mapLink, inHandleLink, (classPtr - mapLink)) == 0)
                                {
                                    if (incomingBlock)
                                    {
                                        *blockHandle = strndup(basename(C_CAST(char*, handle)), strlen(basename(C_CAST(char*, handle))));
                                        *genericHandle = strdup(basename(classPtr));
                                    }
                                    else
                                    {
                                        *blockHandle = strndup(basename(classPtr), strlen(basename(classPtr)));
                                        *genericHandle = strdup(basename(C_CAST(char *, handle)));
                                    }
                                    safe_Free(className)
                                    safe_Free(incomingClassName)
                                    // start PRH valgrind fixes
                                    // this is causing a mem leak... when we bail the loop, there are a string of classList[] items 
                                    // still allocated. 
                                    for(remains = iter; remains<numberOfItems; remains++)
                                    {
                                        safe_Free(classList[remains])
                                    }
                                    safe_Free(classList)
                                    // end PRH valgrind fixes.
                                    return SUCCESS;
                                    break;//found a match, exit the loop
                                }
                            }
                            safe_Free(className)
                        }
                    }
                    safe_Free(classList[iter]) // PRH - valgrind
                    safe_Free(temp)
                }
                safe_Free(classList)
            }
            else
            {
                //not a link, or some other error....probably an old kernel
                safe_Free(incomingClassName)
                return NOT_SUPPORTED;
            }
        }
        else
        {
            //Mapping is not supported...probably an old kernel
            safe_Free(incomingClassName)
            return NOT_SUPPORTED;
        }
        safe_Free(incomingClassName)
    }
    return UNKNOWN;
}

//only to be used by get_Device to set up an os_specific structure
//This could be useful to put into a function for all nix systems to use since it could be useful for them too.
long get_Device_Page_Size(void)
{
#if defined (POSIX_2001)
    //use sysconf: http://man7.org/linux/man-pages/man3/sysconf.3.html
    return sysconf(_SC_PAGESIZE);
#else
    //use get page size: http://man7.org/linux/man-pages/man2/getpagesize.2.html
    return C_CAST(long, getpagesize());
#endif
}

#define LIN_MAX_HANDLE_LENGTH 16
eReturnValues get_Device(const char *filename, tDevice *device)
{
    char *deviceHandle = NULL;
    eReturnValues ret = SUCCESS;
    int k = 0;
    int rc = 0;
    struct nvme_adapter_list nvmeAdptList;
    bool isScsi = false;
    char *nvmeDevName;

    /**
     * In VMWare NVMe device the drivename (for NDDK) 
     * always starts with "vmhba" (e.g. vmhba1) 
     */

    nvmeDevName = strstr(filename, "vmhba");
    isScsi = (nvmeDevName == NULL) ? true : false;

    //printf("Getting device for %s\n", filename);

    /**
     * List down both NVMe and HDD/SSD drives 
     * Get the device after matching the name 
     */
    deviceHandle = strdup(filename);

    if (isScsi)
    {
#if defined (_DEBUG)
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
                safe_Free(deviceHandle)
                return PERMISSION_DENIED;
            }
            else
            {
                safe_Free(deviceHandle)
                return FAILURE;
            }
        }

        device->os_info.minimumAlignment = sizeof(void *);

        //Adding support for different device discovery options. 
        if (device->dFlags == OPEN_HANDLE_ONLY)
        {
            //set scsi interface and scsi drive until we know otherwise
            device->drive_info.drive_type = SCSI_DRIVE;
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.media_type = MEDIA_HDD;
            set_Device_Fields_From_Handle(deviceHandle, device);
            setup_Passthrough_Hacks_By_ID(device);
            safe_Free(deviceHandle)
            return ret;
        }
        //\\TODO: Add support for other flags. 

        if ((device->os_info.fd >= 0) && (ret == SUCCESS))
        {
            struct sg_scsi_id hctlInfo;
            memset(&hctlInfo, 0, sizeof(struct sg_scsi_id));
            int getHctl = ioctl(device->os_info.fd, SG_GET_SCSI_ID, &hctlInfo);
            if (getHctl == 0 && errno == 0)//when this succeeds, both of these will be zeros
            {
                //printf("Got hctlInfo\n");
                device->os_info.scsiAddress.host = C_CAST(uint8_t, hctlInfo.host_no);
                device->os_info.scsiAddress.channel = C_CAST(uint8_t, hctlInfo.channel);
                device->os_info.scsiAddress.target = C_CAST(uint8_t, hctlInfo.scsi_id);
                device->os_info.scsiAddress.lun = C_CAST(uint8_t, hctlInfo.lun);
                device->drive_info.namespaceID = device->os_info.scsiAddress.lun + UINT32_C(1);//Doing this to help with USB to NVMe adapters. Luns start at zero, whereas namespaces start with 1, hence the plus 1.
                //also reported are per lun and per device Q-depth which might be nice to store.
                //printf("H:C:T:L = %" PRIu8 ":%" PRIu8 ":%" PRIu8 ":%" PRIu8 "\n", device->os_info.scsiAddress.host, device->os_info.scsiAddress.channel, device->os_info.scsiAddress.target, device->os_info.scsiAddress.lun);
            }

#if defined (_DEBUG)
            printf("Getting SG driver version\n");
#endif

            /**
             * SG_GET_VERSION_NUM is currently not supported for VMWare 
             * SG_IO. 
             */
#if 0
             // Check we have a valid device by trying an ioctl
             // From http://tldp.org/HOWTO/SCSI-Generic-HOWTO/pexample.html
            if ((ioctl(device->os_info.fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000))
            {
                printf("%s: SG_GET_VERSION_NUM on %s failed version=%d\n", __FUNCTION__, filename, k);
                perror("SG_GET_VERSION_NUM");
                close(device->os_info.fd);
            }

            //http://www.faqs.org/docs/Linux-HOWTO/SCSI-Generic-HOWTO.html#IDDRIVER
            device->os_info.sgDriverVersion.driverVersionValid = true;
            device->os_info.sgDriverVersion.majorVersion = C_CAST(uint8_t, k / 10000);
            device->os_info.sgDriverVersion.minorVersion = C_CAST(uint8_t, (k - (device->os_info.sgDriverVersion.majorVersion * 10000)) / 100);
            device->os_info.sgDriverVersion.revision = C_CAST(uint8_t, k - (device->os_info.sgDriverVersion.majorVersion * 10000) - (device->os_info.sgDriverVersion.minorVersion * 100));
#endif

            //set the OS Type
            device->os_info.osType = OS_ESX;

            memcpy(device->os_info.name, deviceHandle, strlen(deviceHandle) + 1);

            //set scsi interface and scsi drive until we know otherwise
            device->drive_info.drive_type = SCSI_DRIVE;
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.media_type = MEDIA_HDD;
            //now have the device information fields set
#if defined (_DEBUG)
            printf("Setting interface, drive type, secondary handles\n");
#endif

            //set_Device_Fields_From_Handle(deviceHandle, device);
            setup_Passthrough_Hacks_By_ID(device);
            device->drive_info.interface_type = SCSI_INTERFACE;
            device->drive_info.drive_type = UNKNOWN_DRIVE;
            device->drive_info.media_type = MEDIA_UNKNOWN;

#if defined (_DEBUG)
            printf("name = %s\t friendly name = %s\n2ndName = %s\t2ndFName = %s\n",
                device->os_info.name,
                device->os_info.friendlyName,
                device->os_info.secondName,
                device->os_info.secondFriendlyName
            );
            printf("h:c:t:l = %u:%u:%u:%u\n", device->os_info.scsiAddress.host, device->os_info.scsiAddress.channel, device->os_info.scsiAddress.target, device->os_info.scsiAddress.lun);

            printf("SG driver version = %u.%u.%u\n", device->os_info.sgDriverVersion.majorVersion, device->os_info.sgDriverVersion.minorVersion, device->os_info.sgDriverVersion.revision);
#endif

            // Fill in all the device info.
            //this code to set up passthrough commands for USB and IEEE1394 has been removed for now to match Windows functionality. Need better intelligence than this.
            //Some of these old pass-through types issue vendor specific op codes that could be misinterpretted on some devices.
//              if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
//              {
//                  //TODO: Actually get the VID and PID set before calling this...currently it just issues an identify command to test which passthrough to use until it works. - TJE
//                  set_ATA_Passthrough_Type_By_PID_and_VID(device);
//              }

            ret = fill_Drive_Info_Data(device);

#if defined (_DEBUG)
            printf("\nvm helper\n");
            printf("Drive type: %d\n", device->drive_info.drive_type);
            printf("Interface type: %d\n", device->drive_info.interface_type);
            printf("Media type: %d\n", device->drive_info.media_type);
#endif
        }
        safe_Free(deviceHandle)

    }
    else
    {
        rc = Nvme_GetAdapterList(&nvmeAdptList);

        if (rc != 0)
        {
            return FAILURE;
        }

#if defined (_DEBUG)
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

        if (device->os_info.nvmeFd == NULL)
        {
            perror("open");
            device->os_info.nvmeFd = errno;
            printf("open failure\n");
            printf("Error: ");
            print_Errno_To_Screen(errno);
            if (device->os_info.nvmeFd == EACCES)
            {
                safe_Free(deviceHandle)
                return PERMISSION_DENIED;
            }
            else
            {
                safe_Free(deviceHandle)
                return FAILURE;
            }
        }

        device->os_info.minimumAlignment = sizeof(void *);

        //Adding support for different device discovery options. 
        if (device->dFlags == OPEN_HANDLE_ONLY)
        {
            safe_Free(deviceHandle)
            return ret;
        }
        //\\TODO: Add support for other flags. 

        if ((device->os_info.nvmeFd != NULL) && (ret == SUCCESS))
        {
#if defined (_DEBUG)
            printf("Getting SG driver version\n");
#endif

            /**
             * Setting up NVMe drive blindly for now
             */

            device->drive_info.interface_type = NVME_INTERFACE;
            device->drive_info.drive_type = NVME_DRIVE;
            device->drive_info.media_type = MEDIA_NVM;
            memcmp(device->drive_info.T10_vendor_ident, "NVMe", 4);
            device->os_info.osType = OS_ESX;
            memcpy(&(device->os_info.name), filename, strlen(filename) + 1);

#if !defined(DISABLE_NVME_PASSTHROUGH)
            if (device->drive_info.interface_type == NVME_INTERFACE)
            {
#if 0
                ret = ioctl(device->os_info.fd, NVME_IOCTL_ID);
                if (ret < 0)
                {
                    perror("nvme_ioctl_id");
                    return ret;
                }
                device->drive_info.lunOrNSID = C_CAST(uint32_t, ret);
#endif
                ret = fill_In_NVMe_Device_Info(device);
            }
            else
#endif
            {
                // Fill in all the device info.
                //this code to set up passthrough commands for USB and IEEE1394 has been removed for now to match Windows functionality. Need better intelligence than this.
                //Some of these old pass-through types issue vendor specific op codes that could be misinterpretted on some devices.
    //              if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
    //              {
    //                  //TODO: Actually get the VID and PID set before calling this...currently it just issues an identify command to test which passthrough to use until it works. - TJE
    //                  set_ATA_Passthrough_Type_By_PID_and_VID(device);
    //              }

#if 0
                ret = fill_Drive_Info_Data(device);
#endif
            }
#if defined (_DEBUG)
            printf("\nvm helper\n");
            printf("Drive type: %d\n", device->drive_info.drive_type);
            printf("Interface type: %d\n", device->drive_info.interface_type);
            printf("Media type: %d\n", device->drive_info.media_type);
#endif
        }
        safe_Free(deviceHandle)
    }

    return ret;
}
//http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_reset.html
//sgResetType should be one of the values from the link above...so bus or device...controller will work but that shouldn't be done ever.
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
        //poll for reset completion
#if defined(_DEBUG)
        printf("Reset in progress, polling for completion!\n");
#endif
        resetType = SG_SCSI_RESET_NOTHING;
        while (errno == EBUSY)
        {
            ret = ioctl(fd, SG_SCSI_RESET, &resetType);
        }
        ret = SUCCESS;
        //printf("Reset Success!\n");
    }
    return ret;
}

eReturnValues os_Device_Reset(tDevice *device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_DEVICE);
}

eReturnValues os_Bus_Reset(tDevice *device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_BUS);
}

eReturnValues os_Controller_Reset(tDevice *device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_HOST);
}

eReturnValues send_IO(ScsiIoCtx *scsiIoCtx)
{
    eReturnValues ret = FAILURE;
#ifdef _DEBUG
    printf("-->%s \n", __FUNCTION__);
#endif
    switch (scsiIoCtx->device->drive_info.interface_type)
    {
    case NVME_INTERFACE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        return sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
#endif
        //USB, ATA, and SCSI interface all use sg, so just issue an SG IO.
    case SCSI_INTERFACE:
    case IDE_INTERFACE:
    case USB_INTERFACE:
    case IEEE_1394_INTERFACE:
        ret = send_sg_io(scsiIoCtx);
        break;
    case RAID_INTERFACE:
        if (scsiIoCtx->device->issue_io != NULL)
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
            printf("Target Device does not have a valid interface %d\n", \
                scsiIoCtx->device->drive_info.interface_type);
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

eReturnValues send_sg_io(ScsiIoCtx *scsiIoCtx)
{
    sg_io_hdr_t io_hdr;
    uint8_t *localSenseBuffer = NULL;
    eReturnValues ret = SUCCESS;
    seatimer_t  commandTimer;
#ifdef _DEBUG
    printf("-->%s \n", __FUNCTION__);
#endif


    memset(&commandTimer, 0, sizeof(seatimer_t));
    //int idx = 0;
    // Start with zapping the io_hdr
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));

    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("Sending command with send_IO\n");
    }

    // Set up the io_hdr
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = scsiIoCtx->cdbLength;
    // Use user's sense or local?
    if ((scsiIoCtx->senseDataSize) && (scsiIoCtx->psense != NULL))
    {
        io_hdr.mx_sb_len = scsiIoCtx->senseDataSize;
        io_hdr.sbp = scsiIoCtx->psense;
    }
    else
    {
        localSenseBuffer = C_CAST(uint8_t *, calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), scsiIoCtx->device->os_info.minimumAlignment));
        if (!localSenseBuffer)
        {
            return MEMORY_FAILURE;
        }
        io_hdr.mx_sb_len = SPC3_SENSE_LEN;
        io_hdr.sbp = localSenseBuffer;
    }

    switch (scsiIoCtx->direction)
    {
    case XFER_NO_DATA:
    case SG_DXFER_NONE:
        io_hdr.dxfer_direction = SG_DXFER_NONE;
        break;
    case XFER_DATA_IN:
    case SG_DXFER_FROM_DEV:
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        break;
    case XFER_DATA_OUT:
    case SG_DXFER_TO_DEV:
        io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
        break;
    case SG_DXFER_TO_FROM_DEV:
        io_hdr.dxfer_direction = SG_DXFER_TO_FROM_DEV;
        break;
        //case SG_DXFER_UNKNOWN:
        //io_hdr.dxfer_direction = SG_DXFER_UNKNOWN;
        //break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("%s Didn't understand direction\n", __FUNCTION__);
        }
        safe_Free_aligned(localSenseBuffer)
        return BAD_PARAMETER;
    }

    io_hdr.dxfer_len = scsiIoCtx->dataLength;
    io_hdr.dxferp = scsiIoCtx->pdata;
    io_hdr.cmdp = scsiIoCtx->cdb;
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        io_hdr.timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        //this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that we DON'T do a conversion and leave the time as the max...
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < SG_MAX_CMD_TIMEOUT_SECONDS)
        {
            io_hdr.timeout *= 1000;//convert to milliseconds
        }
        else
        {
            io_hdr.timeout = UINT32_MAX;//no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            io_hdr.timeout = scsiIoCtx->timeout;
            //this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->timeout < SG_MAX_CMD_TIMEOUT_SECONDS)
            {
                io_hdr.timeout *= 1000;//convert to milliseconds
            }
            else
            {
                io_hdr.timeout = UINT32_MAX;//no timeout or maximum timeout
            }
        }
        else
        {
            io_hdr.timeout = 15 * 1000;//default to 15 second timeout
        }
    }

    // \revisit: should this be FF or something invalid than 0?
    scsiIoCtx->returnStatus.format = 0xFF;
    scsiIoCtx->returnStatus.senseKey = 0;
    scsiIoCtx->returnStatus.asc = 0;
    scsiIoCtx->returnStatus.ascq = 0;
    //print_io_hdr(&io_hdr);
    //printf("scsiIoCtx->device->os_info.fd = %d\n", scsiIoCtx->device->os_info.fd);
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

    //print_io_hdr(&io_hdr);

    if (io_hdr.sb_len_wr)
    {
        scsiIoCtx->returnStatus.format = io_hdr.sbp[0];
        get_Sense_Key_ASC_ASCQ_FRU(io_hdr.sbp, io_hdr.mx_sb_len, &scsiIoCtx->returnStatus.senseKey, &scsiIoCtx->returnStatus.asc, &scsiIoCtx->returnStatus.ascq, &scsiIoCtx->returnStatus.fru);
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
        //something has gone wrong. Sense data may or may not have been returned.
        //Check the masked status, host status and driver status to see what happened.
        if (io_hdr.masked_status != 0) //SAM_STAT_GOOD???
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
                //No sense data back. We need to set an error since the layers above are going to look for sense data and we don't have any.
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
            if (io_hdr.sb_len_wr == 0)//Doing this because some drivers may set an error even if the command otherwise went through and sense data was available.
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
                //now error suggestions
                switch (io_hdr.driver_status & OPENSEA_SG_ERR_SUGGEST_MASK)
                {
                case OPENSEA_SG_ERR_SUGGEST_NONE:
                    break;//no suggestions, nothing necessary to print
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
                //No sense data back. We need to set an error since the layers above are going to look for sense data and we don't have any.
                ret = OS_PASSTHROUGH_FAILURE;
            }
        }

    }

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    safe_Free_aligned(localSenseBuffer)
    return ret;
}

static int nvme_filter(const struct dirent *entry)
{
    int nvmeHandle = strncmp("nvme", entry->d_name, 4);
    if (nvmeHandle != 0)
    {
        return !nvmeHandle;
    }
    if (strlen(entry->d_name) > 5)
    {
        char* partition = strpbrk(entry->d_name, "p");
        if (partition != NULL)
        {
            return nvmeHandle;
        }
        else
        {
            return !nvmeHandle;
        }
    }
    else
    {
        return 0;
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
eReturnValues get_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
{
    int  num_devs = 0, num_nvme_devs = 0;
    int rc = 0;
    struct nvme_adapter_list nvmeAdptList;

    struct dirent **namelist;

    num_devs = scandir("/dev/disks", &namelist, drive_filter, alphasort);

    //free the list of names to not leak memory
    for (int iter = 0; iter < num_devs; ++iter)
    {
        safe_Free(namelist[iter])
    }
    safe_Free(namelist)

#ifdef _DEBUG
    printf("get_Device_Count : num_devs %d\n", num_devs);
#endif

    //add nvme devices to the list
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
eReturnValues get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    eReturnValues returnValue = SUCCESS;
    int numberOfDevices = 0;
    int driveNumber = 0, found = 0, failedGetDeviceCount = 0, permissionDeniedCount = 0;
    char name[128] = { 0 }; //Because get device needs char
    int fd;
    tDevice * d = NULL;
    struct nvme_adapter_list nvmeAdptList;
    int rc = 0;
#if defined (DEGUG_SCAN_TIME)
    seatimer_t getDeviceTimer;
    seatimer_t getDeviceListTimer;
    memset(&getDeviceTimer, 0, sizeof(seatimer_t));
    memset(&getDeviceListTimer, 0, sizeof(seatimer_t));
#endif
    struct dirent **namelist;

    int  num_sg_devs = 0, num_nvme_devs = 0;

    num_sg_devs = scandir("/dev/disks", &namelist, drive_filter, alphasort);

    rc = Nvme_GetAdapterList(&nvmeAdptList);

    if (rc == 0)
    {
        num_nvme_devs = nvmeAdptList.count;
    }


    char **devs = C_CAST(char **, calloc(num_sg_devs + num_nvme_devs + 1, sizeof(char *)));
    int i = 0, j = 0;
    //add sg/sd devices to the list
    for (; i < (num_sg_devs); i++)
    {
        size_t deviceHandleLen = (strlen("/dev/disks/") + strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i] = C_CAST(char *, malloc(deviceHandleLen));
        snprintf(devs[i], deviceHandleLen, "/dev/disks/%s", namelist[i]->d_name);
        safe_Free(namelist[i])
    }
    safe_Free(namelist)

    //add nvme devices to the list
    for (j = 0; i < (num_sg_devs + num_nvme_devs) && i < MAX_DEVICES_PER_CONTROLLER;i++, j++)
    {
        size_t nvmeAdptNameLen = strlen(nvmeAdptList.adapters[j].name) + 1;
        devs[i] = C_CAST(char *, malloc(nvmeAdptNameLen));
        memset(devs[i], 0, nvmeAdptNameLen);
        snprintf(devs[i], nvmeAdptNameLen, "%s", nvmeAdptList.adapters[j].name);
    }
    devs[i] = NULL; //Added this so the for loop down doesn't cause a segmentation fault.


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
        numberOfDevices = sizeInBytes / sizeof(tDevice);
        d = ptrToDeviceList;
#if defined (DEGUG_SCAN_TIME)
        start_Timer(&getDeviceListTimer);
#endif
        for (driveNumber = 0; ((driveNumber >= 0 && C_CAST(unsigned int, driveNumber) < MAX_DEVICES_TO_SCAN && driveNumber < (num_sg_devs + num_nvme_devs)) && (found < numberOfDevices)); ++driveNumber)
        {
            if (!devs[driveNumber] || strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            memset(name, 0, sizeof(name));//clear name before reusing it
            snprintf(name, sizeof(name), "%s", devs[driveNumber]);
            fd = -1;
            //lets try to open the device.      
            fd = open(name, O_RDWR | O_NONBLOCK);
            if (fd >= 0)
            {
                close(fd);
                eVerbosityLevels temp = d->deviceVerbosity;
                memset(d, 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size = ver.size;
                d->sanity.version = ver.version;
#if defined (DEGUG_SCAN_TIME)
                seatimer_t getDeviceTimer;
                memset(&getDeviceTimer, 0, sizeof(seatimer_t));
                start_Timer(&getDeviceTimer);
#endif
                d->dFlags = flags;
                eReturnValues ret = get_Device(name, d);
#if defined (DEGUG_SCAN_TIME)
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
            else if (errno == EACCES) //quick fix for opening drives without sudo
            {
                ++permissionDeniedCount;
                failedGetDeviceCount++;
            }
            else
            {
                failedGetDeviceCount++;
            }
            //free the dev[deviceNumber] since we are done with it now.
            safe_Free(devs[driveNumber])
        }
#if defined (DEGUG_SCAN_TIME)
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
    safe_Free(devs)
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
eReturnValues close_Device(tDevice *dev)
{
    int retValue = 0;
    bool isNVMe = false;
    char *nvmeDevName;

    /**
     * In VMWare NVMe device the drivename (for NDDK) 
     * always starts with "vmhba" (e.g. vmhba1) 
     */

    nvmeDevName = strstr(dev->os_info.name, "vmhba");
    isNVMe = (nvmeDevName != NULL) ? true : false;

    if (dev)
    {
        if (isNVMe)
        {
            Nvme_Close(dev->os_info.nvmeFd);
            dev->os_info.last_error = errno;
            retValue = 0;
        }
        else
        {
            retValue = close(dev->os_info.fd);
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

eReturnValues send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx )
{
#if !defined (DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = 0;//NVME_SC_SUCCESS;//This defined value used to exist in some version of nvme.h but is missing in nvme_ioctl.h...it was a value of zero, so this should be ok.
    struct usr_io uio;

#ifdef _DEBUG
    printf("-->%s\n", __FILE__);
    printf("-->%s\n", __FUNCTION__);
#endif

    memset(&uio, 0, sizeof(uio));

    if (nvmeIoCtx == NULL)
    {
#ifdef _DEBUG
        printf("-->%s\n", __FUNCTION__);
#endif
        return BAD_PARAMETER;
    }

    switch (nvmeIoCtx->commandType)
    {
    case NVM_ADMIN_CMD:
        memcpy(&(uio.cmd), &(nvmeIoCtx->cmd.adminCmd), sizeof(nvmeCommands));

        /*
        uio.cmd.header.opCode = nvmeIoCtx->cmd.adminCmd.opcode;
        uio.cmd.header.fusedOp = nvmeIoCtx->cmd.adminCmd.flags;
        uio.cmd.header.namespaceID = nvmeIoCtx->cmd.adminCmd.nsid;
        uio.cmd.header.metadataPtr = nvmeIoCtx->cmd.adminCmd.metadata;
        uio.cmd.header.prp[0].addr = nvmeIoCtx->cmd.adminCmd.addr;
        uio.cmd.header.prp[1].lower = nvmeIoCtx->cmd.adminCmd.metadataLen;
        uio.cmd.header.prp[1].upper = nvmeIoCtx->cmd.adminCmd.dataLen;
        uio.cmd.cmd.vendorSpecific.buffNumDW = nvmeIoCtx->cmd.adminCmd.cdw10;
        uio.cmd.cmd.vendorSpecific.metaNumDW = nvmeIoCtx->cmd.adminCmd.cdw11;
        uio.cmd.cmd.vendorSpecific.vndrCDW12 = nvmeIoCtx->cmd.adminCmd.cdw12;
        uio.cmd.cmd.vendorSpecific.vndrCDW13 = nvmeIoCtx->cmd.adminCmd.cdw13;
        uio.cmd.cmd.vendorSpecific.vndrCDW14 = nvmeIoCtx->cmd.adminCmd.cdw14;
        uio.cmd.cmd.vendorSpecific.vndrCDW15 = nvmeIoCtx->cmd.adminCmd.cdw15;
        */

        if ((nvmeIoCtx->commandDirection == XFER_NO_DATA) ||
            (nvmeIoCtx->commandDirection == XFER_DATA_IN))
        {
            uio.direction = XFER_FROM_DEV;
        }
        else
        {
            uio.direction = XFER_TO_DEV;
        }

        uio.length = nvmeIoCtx->dataSize;
        uio.addr = C_CAST(vmk_uint32, nvmeIoCtx->cmd.adminCmd.addr);
        uio.namespaceID = nvmeIoCtx->cmd.adminCmd.nsid;
        uio.timeoutUs = nvmeIoCtx->timeout ? nvmeIoCtx->timeout * 1000 : 15000;

#ifdef _DEBUG
/*
        printf("Before Nvme_AdminPassthru %s: uio.addr=%p, uio.length=%d, cdw10=0x%X, nsid=0x%x\n",\
               __FUNCTION__, uio.addr,\
               uio.length, uio.cmd.cmd.vendorSpecific.buffNumDW,\
               uio.cmd.header.namespaceID\
                );
*/
        printf("Before Nvme_AdminPassthru %s: uio.length=%d, cdw10=0x%X, nsid=0x%x\n",\
               __FUNCTION__,\
               uio.length, uio.cmd.cmd.vendorSpecific.buffNumDW,\
               uio.cmd.header.namespaceID\
                );

        /*
        printf("Printing uio.cmd\n");
        for(int i = 0; i < sizeof(uio.cmd); i++)
        {
            if(i%8 == 0)
            {
                printf("%d : ", i);
            }
            printf(" %x", C_CAST(unsigned char, *((unsigned char *)&(uio.cmd) + i)));
            if(i%8 == 7)
            {
                printf("\n");
            }
        }
        printf("\n");
        */

#endif

        ret = Nvme_AdminPassthru(nvmeIoCtx->device->os_info.nvmeFd, &uio);

#ifdef _DEBUG
/*
        printf("After Nvme_AdminPassthru %s: uio.addr=%p, uio.length=%d, cdw10=0x%X, nsid=0x%x\n",\
               __FUNCTION__, uio.addr,\
               uio.length, uio.cmd.cmd.vendorSpecific.buffNumDW,\
               uio.cmd.header.namespaceID\
                );

        printf("After Nvme_AdminPassthru %s: uio.length=%d, cdw10=0x%X, nsid=0x%x\n",\
               __FUNCTION__,\
               uio.length, uio.cmd.cmd.vendorSpecific.buffNumDW,\
               uio.cmd.header.namespaceID\
                );

        printf("Printing buffer\n");
        for(int i = 0; i < uio.length; i++) 
        {
            if(i%8 == 0) 
            {
                printf("%d : ", i);
            }
            printf(" %x", C_CAST(unsigned char, *(C_CAST(unsigned char *, uio.addr) + i)));
            if(i%8 == 7) 
            {
                printf("\n");
            }
        }
        printf("\n");
*/
#endif


        nvmeIoCtx->device->os_info.last_error = ret;
        //Get error? 
        if (ret < 0)
        {
            if (VERBOSITY_QUIET < nvmeIoCtx->device->deviceVerbosity)
            {
                perror("send_IO");
            }
        }
        nvmeIoCtx->commandCompletionData.commandSpecific = uio.comp.param.cmdSpecific;
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
        nvmeIoCtx->commandCompletionData.dw1Reserved = uio.comp.reserved;
        nvmeIoCtx->commandCompletionData.dw1Valid = true;
        nvmeIoCtx->commandCompletionData.sqIDandHeadPtr = M_WordsTo4ByteValue(uio.comp.sqID, uio.comp.sqHdPtr);
        nvmeIoCtx->commandCompletionData.dw2Valid = true;
        nvmeIoCtx->commandCompletionData.statusAndCID = uio.comp.cmdID | (uio.comp.phaseTag << 16) | (uio.comp.SC << 17) | (uio.comp.SCT << 25) | (uio.comp.more << 30) | (uio.comp.noRetry << 31);
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        break;

#if 0
    case NVM_CMD:
        memset(&nvmCmd, 0, sizeof(nvmCmd));
        ret = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_SUBMIT_IO, &nvmCmd);
        break;
#endif

    default:
        return BAD_PARAMETER;
        break;
    }
    if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
    {
        if (nvmeIoCtx->device->os_info.last_error != 0)
        {
            printf("Error: ");
            print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif

    if (nvmeIoCtx->device->delay_io)
    {
        delay_Milliseconds(nvmeIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", nvmeIoCtx->device->delay_io);
        }
    }

    return ret;
#else //DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif //DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Reset(tDevice *device)
{
    //This is a stub. If this is possible, this should perform an nvme reset;
    return OS_COMMAND_NOT_AVAILABLE;
}

eReturnValues os_nvme_Subsystem_Reset(tDevice *device)
{
    //This is a stub. If this is possible, this should perform an nvme subsystem reset;
    return OS_COMMAND_NOT_AVAILABLE;
}

//Case to remove this from sg_helper.h/c and have a platform/lin/pci-herlper.h vs platform/win/pci-helper.c 

eReturnValues pci_Read_Bar_Reg( tDevice * device, uint8_t * pData, uint32_t dataSize )
{
#if !defined (DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = UNKNOWN;
    int fd=0;
    void * barRegs = NULL;
    char sysfsPath[PATH_MAX];
    snprintf(sysfsPath, PATH_MAX, "/sys/block/%s/device/resource0", device->os_info.name);
    fd = open(sysfsPath, O_RDONLY);
    if (fd >= 0)
    {
        //
        barRegs = mmap(0, dataSize, PROT_READ, MAP_SHARED, fd, 0);
        if (barRegs != MAP_FAILED)
        {
            ret = SUCCESS;
            memcpy(pData, barRegs, dataSize);
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
#else //DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif //DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_Read(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool forceUnitAccess, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Write(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool forceUnitAccess, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Verify(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Flush(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Lock_Device(tDevice *device)
{
    eReturnValues ret = SUCCESS;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //Not sure what to do
    }
    else
    {
        //Get flags
        int flags = fcntl(device->os_info.fd, F_GETFL);
        //disable O_NONBLOCK
        flags &= ~O_NONBLOCK;
        //Set Flags
        fcntl(device->os_info.fd, F_SETFL, flags);
    }
    return ret;
}

eReturnValues os_Unlock_Device(tDevice *device)
{
    eReturnValues ret = SUCCESS;
    if (device->drive_info.drive_type == NVME_DRIVE)
    {
        //Not sure what to do
    }
    else
    {
        //Get flags
        int flags = fcntl(device->os_info.fd, F_GETFL);
        //enable O_NONBLOCK
        flags |= O_NONBLOCK;
        //Set Flags
        fcntl(device->os_info.fd, F_SETFL, flags);
    }
    return ret;
}

eReturnValues os_Update_File_System_Cache(M_ATTR_UNUSED tDevice* device)
{
    //note: linux code for blkrrprt might work
    //TODO: Complete this stub when this is figured out - TJE
    return NOT_SUPPORTED;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Unmount_File_Systems_On_Device(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}
