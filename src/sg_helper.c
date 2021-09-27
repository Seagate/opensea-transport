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

//If _GNU_SOURCE is not already defined, define it to get access to versionsort
//NOTE: This will be undefined at the end of this file if _GNU_SOURCE_DEFINED_IN_SG_HELPER is set to prevent unexpected
//      behavior in any other parts of the code.
//NOTE: Adding this definition like this so that it can also easily be removed as necessary in the future in case of errors.
#if !defined (_GNU_SOURCE)
    #define _GNU_SOURCE
    #define _GNU_SOURCE_DEFINED_IN_SG_HELPER
    #pragma message "Defining _GNU_SOURCE since it was not already defined."
#endif

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
#include "sg_helper.h"
#include "cmds.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"
#if !defined(DISABLE_NVME_PASSTHROUGH)
#include "nvme_helper_func.h"
#include "sntl_helper.h"
#endif

#if defined(DEGUG_SCAN_TIME)
#include "common_platform.h"
#endif

    //If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported()
{
    return true;
}

extern bool validate_Device_Struct(versionBlock);

// Local helper functions for debugging
void print_io_hdr( sg_io_hdr_t *pIo )
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
    printf("type void * dxferp %p\n", (unsigned int *)pIo->dxferp);              /* [i], [*io] */
    printf("type unsigned char * cmdp %p\n", (unsigned int *)pIo->cmdp);       /* [i], [*i]  */
    printf("type unsigned char * sbp %p\n", (unsigned int *)pIo->sbp);        /* [i], [*o]  */
    printf("type unsigned int timeout %d\n", pIo->timeout);       /* [i] unit: millisecs */
    printf("type unsigned int flags 0x%x\n", pIo->flags);         /* [i] */
    printf("type int pack_id %d\n", pIo->pack_id);                /* [i->o] */
    printf("type void * usr_ptr %p\n", (unsigned int *)pIo->usr_ptr);             /* [i->o] */
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

static int sg_filter( const struct dirent *entry )
{
    return !strncmp("sg", entry->d_name, 2);
}

//get sd devices, but ignore any partition number information since that isn't something we can actually send commands to
static int sd_filter( const struct dirent *entry )
{
    int sdHandle = strncmp("sd",entry->d_name,2);
    if(sdHandle != 0)
    {
      return !sdHandle;
    }
    char* partition = strpbrk(entry->d_name,"0123456789");
    if(partition != NULL)
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

bool is_Block_Device_Handle(char *handle)
{
    bool isBlockDevice = false;
    if (handle && strlen(handle))
    {
        if(strstr(handle,"sd") || strstr(handle, "st") || strstr(handle, "sr") || strstr(handle, "scd"))
        {
            isBlockDevice = true;
        }
    }
    return isBlockDevice;
}

bool is_SCSI_Generic_Handle(char *handle)
{
    bool isGenericDevice = false;
    if (handle && strlen(handle))
    {
        if(strstr(handle,"sg") && !strstr(handle, "bsg"))
        {
            isGenericDevice = true;
        }
    }
    return isGenericDevice;
}

bool is_Block_SCSI_Generic_Handle(char *handle)
{
    bool isBlockGenericDevice = false;
    if (handle && strlen(handle))
    {
        if(strstr(handle,"bsg"))
        {
            isBlockGenericDevice = true;
        }
    }
    return isBlockGenericDevice;
}

bool is_NVMe_Handle(char *handle)
{
    bool isNvmeDevice = false;
    if (handle && strlen(handle))
    {
        if(strstr(handle,"nvme"))
        {
            isNvmeDevice = true;
        }
    }
    return isNvmeDevice;
}


//while similar to the function below, this is used only by get_Device to set up some fields in the device structure for the above layers
static void set_Device_Fields_From_Handle(const char* handle, tDevice *device)
{
    //check if it's a block handle, bsg, or scsi_generic handle, then setup the path we need to read.
    if (handle && device)
    {
        if (strstr(handle,"nvme") != NULL)
        {
            size_t nvmHandleLen = strlen(handle) + 1;
            char *nvmHandle = (char*)calloc(nvmHandleLen, sizeof(char));
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
            snprintf(incomingHandleClassPath, PATH_MAX, "%s/sys/class/", incomingHandleClassPath);
            if (is_Block_Device_Handle((char*)handle))
            {
                snprintf(incomingHandleClassPath, PATH_MAX, "%sblock/", incomingHandleClassPath);
                incomingBlock = true;
                //incomingClassName = strdup("block");
            }
            else if (is_Block_SCSI_Generic_Handle((char*)handle))
            {
                bsg = true;
                snprintf(incomingHandleClassPath, PATH_MAX, "%sbsg/", incomingHandleClassPath);
                //incomingClassName = strdup("bsg");
            }
            else if (is_SCSI_Generic_Handle((char*)handle))
            {
                snprintf(incomingHandleClassPath, PATH_MAX, "%sscsi_generic/", incomingHandleClassPath);
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
                snprintf(incomingHandleClassPath, PATH_MAX, "%s%s", incomingHandleClassPath, basename((char*)handle));
                //now read the link with the handle appended on the end
                if (lstat(incomingHandleClassPath,&link) == 0 && S_ISLNK(link.st_mode))
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
                        if (strstr(inHandleLink,"ata") != 0)
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
                            char *pciPath = (char*)calloc(PATH_MAX, sizeof(char));
                            if (pciPath)
                            {
                                snprintf(pciPath, PATH_MAX, "%.*s/vendor", newStrLen - 1, fullPciPath);
                                //printf("shortened Path = %s\n", dirname(pciPath));
                                FILE *temp = NULL;
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if(1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.vendorID))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        //printf("Got vendor as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                pciPath = dirname(pciPath);//remove vendor from the end
                                snprintf(pciPath, PATH_MAX, "%s/device", pciPath);
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if(1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.productID))
                                    {
                                        device->drive_info.adapter_info.productIDValid = true;
                                        //printf("Got product as %" PRIX16 "h\n", device->drive_info.adapter_info.productID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                //Store revision data. This seems to be in the bcdDevice file.
                                pciPath = dirname(pciPath);//remove device from the end
                                snprintf(pciPath, PATH_MAX, "%s/revision", pciPath);
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
                                safe_Free(pciPath);
                                device->drive_info.adapter_info.infoType = ADAPTER_INFO_PCI;
                            }
                        }
                        else if (strstr(inHandleLink,"usb") != 0)
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
                            char *usbPath = (char*)calloc(PATH_MAX, sizeof(char));
                            if (usbPath)
                            {
                                snprintf(usbPath, PATH_MAX, "%.*s", newStrLen - 1, fullPciPath);
                                usbPath = dirname(usbPath);
                                //printf("full USB Path = %s\n", usbPath);
                                //now that the path is correct, we need to read the files idVendor and idProduct
                                snprintf(usbPath, PATH_MAX, "%s/idVendor", usbPath);
                                //printf("idVendor USB Path = %s\n", usbPath);
                                FILE *temp = NULL;
                                temp = fopen(usbPath, "r");
                                if (temp)
                                {
                                    if(1 == fscanf(temp, "%" SCNx32, &device->drive_info.adapter_info.vendorID))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        //printf("Got vendor ID as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                usbPath = dirname(usbPath);//remove idVendor from the end
                                //printf("full USB Path = %s\n", usbPath);
                                snprintf(usbPath, PATH_MAX, "%s/idProduct", usbPath);
                                //printf("idProduct USB Path = %s\n", usbPath);
                                temp = fopen(usbPath, "r");
                                if (temp)
                                {
                                    if(1 == fscanf(temp, "%" SCNx32, &device->drive_info.adapter_info.productID))
                                    {
                                        device->drive_info.adapter_info.productIDValid = true;
                                        //printf("Got product ID as %" PRIX16 "h\n", device->drive_info.adapter_info.productID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                //Store revision data. This seems to be in the bcdDevice file.
                                usbPath = dirname(usbPath);//remove idProduct from the end
                                snprintf(usbPath, PATH_MAX, "%s/bcdDevice", usbPath);
                                temp = fopen(usbPath, "r");
                                if (temp)
                                {
                                    if(1 == fscanf(temp, "%" SCNx32, &device->drive_info.adapter_info.revision))
                                    {
                                        device->drive_info.adapter_info.revisionValid = true;
                                        //printf("Got revision as %" PRIX16 "h\n", device->drive_info.adapter_info.revision);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                safe_Free(usbPath);
                                device->drive_info.adapter_info.infoType = ADAPTER_INFO_USB;
                            }
                        }
                        else if (strstr(inHandleLink,"fw") != 0)
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
                            char *fwPath = (char*)calloc(PATH_MAX, sizeof(char));
                            if (fwPath)
                            {
                                snprintf(fwPath, PATH_MAX, "%.*s/modalias", newStrLen - 1, fullFWPath);
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
                                safe_Free(fwPath);
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
                            //printf("FULL: %" PRIXPTR "\t/HOST: %" PRIXPTR "\n", (uintptr_t)fullPciPath, (uintptr_t)strstr(fullPciPath, "/host"));
                            uint64_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath + 1;
                            char *pciPath = (char*)calloc(PATH_MAX, sizeof(char));
                            if (pciPath)
                            {
                                snprintf(pciPath, PATH_MAX, "%.*s/vendor", newStrLen - 1, fullPciPath);
                                //printf("Shortened PCI Path: %s\n", dirname(pciPath));
                                FILE *temp = NULL;
                                temp = fopen(pciPath, "r");
                                if (temp)
                                {
                                    if(1 == fscanf(temp, "0x%" SCNx32, &device->drive_info.adapter_info.vendorID))
                                    {
                                        device->drive_info.adapter_info.vendorIDValid = true;
                                        //printf("Got vendor as %" PRIX16 "h\n", device->drive_info.adapter_info.vendorID);
                                    }
                                    fclose(temp);
                                    temp = NULL;
                                }
                                pciPath = dirname(pciPath);//remove vendor from the end
                                snprintf(pciPath, PATH_MAX, "%s/device", pciPath);
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
                                snprintf(pciPath, PATH_MAX, "%s/revision", pciPath);
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
                                safe_Free(pciPath);
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
                        //char *scsiAddress = basename(dirname(dirname(inHandleLink)));//SCSI address should be 2nd from the end of the link
                        //if (scsiAddress)
                        //{
                        //    char *token = strtok(scsiAddress, ":");
                        //    uint8_t counter = 0;
                        //    while (token)
                        //    {
                        //        switch (counter)
                        //        {
                        //        case 0://host
                        //            device->os_info.scsiAddress.host = (uint8_t)atoi(token);
                        //            break;
                        //        case 1://bus
                        //            device->os_info.scsiAddress.channel = (uint8_t)atoi(token);
                        //            break;
                        //        case 2://target
                        //            device->os_info.scsiAddress.target = (uint8_t)atoi(token);
                        //            break;
                        //        case 3://lun
                        //            device->os_info.scsiAddress.lun = (uint8_t)atoi(token);
                        //            break;
                        //        default:
                        //            break;
                        //        }
                        //        token = strtok(NULL, ":");
                        //        ++counter;
                        //    }
                        //    if (counter >= 4)
                        //    {
                        //        device->os_info.scsiAddressValid = true;
                        //    }
                        //}
                        //printf("attempting to map the handle\n");
                        //Lastly, call the mapping function to get the matching block handle and check what we got to set ATAPI, TAPE or leave as-is. Setting these is necessary to prevent talking to ATAPI as HDD due to overlapping A1h opcode
                        char *block = NULL;
                        char *gen = NULL;
                        if (SUCCESS == map_Block_To_Generic_Handle((char*)handle, &gen, &block))
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
                        safe_Free(block);
                        safe_Free(gen);
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
int map_Block_To_Generic_Handle(char *handle, char **genericHandle, char **blockHandle)
{
    if (handle == NULL)
    {
        return BAD_PARAMETER;
    }
    //if the handle passed in contains "nvme" then we know it's a device on the nvme interface
    if (strstr(handle,"nvme") != NULL)
    {
        return NOT_SUPPORTED;
    }
    else
    {
        bool incomingBlock = false;//only set for SD!
        char incomingHandleClassPath[PATH_MAX] = { 0 };
        char *incomingClassName = NULL;
        snprintf(incomingHandleClassPath, PATH_MAX, "%s/sys/class/", incomingHandleClassPath);
        if (is_Block_Device_Handle(handle))
        {
            snprintf(incomingHandleClassPath, PATH_MAX, "%sblock/", incomingHandleClassPath);
            incomingBlock = true;
            incomingClassName = strdup("block");
        }
        else if (is_Block_SCSI_Generic_Handle(handle))
        {
            snprintf(incomingHandleClassPath, PATH_MAX, "%sbsg/", incomingHandleClassPath);
            incomingClassName = strdup("bsg");
        }
        else if (is_SCSI_Generic_Handle(handle))
        {
            snprintf(incomingHandleClassPath, PATH_MAX, "%sscsi_generic/", incomingHandleClassPath);
            incomingClassName = strdup("scsi_generic");
        }
        //first make sure this directory exists
        struct stat inHandleStat;
        if (stat(incomingHandleClassPath, &inHandleStat) == 0 && S_ISDIR(inHandleStat.st_mode))
        {
            snprintf(incomingHandleClassPath, PATH_MAX, "%s%s", incomingHandleClassPath, basename(handle));
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
                        safe_Free(incomingClassName);
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
                        safe_Free(incomingClassName);
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
                    char temp[PATH_MAX] = { 0 };
                    struct stat tempStat;
                    memset(&tempStat, 0, sizeof(struct stat));
                    snprintf(temp, PATH_MAX, "%s%s", classPath, classList[iter]->d_name);
                    if (lstat(temp,&tempStat) == 0 && S_ISLNK(tempStat.st_mode))/*check if this is a link*/
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
                                className = (char*)calloc(classNameLength, sizeof(char));
                                if (className)
                                {
                                    snprintf(className, classNameLength, "scsi_generic");
                                }
                            }
                            else if (bsg) //bsg class
                            {
                                classNameLength = strlen("bsg") + 1;
                                className = (char*)calloc(classNameLength, sizeof(char));
                                if (className)
                                {
                                    snprintf(className, classNameLength, "bsg");
                                }
                            }
                            else //scsi_generic class
                            {
                                classNameLength = strlen("block") + 1;
                                className = (char*)calloc(classNameLength, sizeof(char));
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
                                        *blockHandle = strndup(basename(handle), strlen(basename(handle)));
                                        *genericHandle = strdup(basename(classPtr));
                                    }
                                    else
                                    {
                                        *blockHandle = strndup(basename(classPtr), strlen(basename(classPtr)));
                                        *genericHandle = strdup(basename(handle));
                                    }
                                    safe_Free(className);
                                    safe_Free(incomingClassName);
                                    // start PRH valgrind fixes
                                    // this is causing a mem leak... when we bail the loop, there are a string of classList[] items 
                                    // still allocated. 
                                    for(remains = iter; remains<numberOfItems; remains++)
                                    {
                                        safe_Free(classList[remains]);
                                    }
                                    safe_Free(classList);
                                    // end PRH valgrind fixes.
                                    return SUCCESS;
                                    break;//found a match, exit the loop
                                }
                            }
                            safe_Free(className);
                        }
                    }
                    safe_Free(classList[iter]); // PRH - valgrind
                }
                safe_Free(classList);
            }
            else
            {
                //not a link, or some other error....probably an old kernel
                safe_Free(incomingClassName);
                return NOT_SUPPORTED;
            }
        }
        else
        {
            //Mapping is not supported...probably an old kernel
            safe_Free(incomingClassName);
            return NOT_SUPPORTED;
        }
        safe_Free(incomingClassName);
    }
    return UNKNOWN;
}

//only to be used by get_Device to set up an os_specific structure
//This could be useful to put into a function for all nix systems to use since it could be useful for them too.
long get_Device_Page_Size(void)
{
#if defined (_POSIX_VERSION) && _POSIX_VERSION >= 200112L
    //use sysconf: http://man7.org/linux/man-pages/man3/sysconf.3.html
    return sysconf(_SC_PAGESIZE);
#else
    //use get page size: http://man7.org/linux/man-pages/man2/getpagesize.2.html
    return (long)getpagesize();
#endif
}

#define LIN_MAX_HANDLE_LENGTH 16
int get_Device(const char *filename, tDevice *device)
{
    char *deviceHandle = NULL;
    int ret = SUCCESS, k = 0;
    #if defined (_DEBUG)
    printf("%s: Getting device for %s\n", __FUNCTION__, filename);
    #endif

    if(is_Block_Device_Handle((char*)filename))
    {
        //printf("\tBlock handle found, mapping...\n");
        char *genHandle = NULL;
        char *blockHandle = NULL;
        int mapResult = map_Block_To_Generic_Handle((char*)filename, &genHandle, &blockHandle);
        #if defined (_DEBUG)
        printf("sg = %s\tsd = %s\n", genHandle, blockHandle);
        #endif
        if(mapResult == SUCCESS && genHandle!=NULL)
        {
            deviceHandle = (char*)calloc(LIN_MAX_HANDLE_LENGTH, sizeof(char));
            //printf("Changing filename to SG device....\n");
            if (is_SCSI_Generic_Handle(genHandle))
            {
                snprintf(deviceHandle, LIN_MAX_HANDLE_LENGTH, "/dev/%s", genHandle);
            }
            else
            {   
                snprintf(deviceHandle, LIN_MAX_HANDLE_LENGTH, "/dev/bsg/%s", genHandle);
            }
            #if defined (_DEBUG)
            printf("\tfilename = %s\n", deviceHandle);
            #endif
        }
        else //If we can't map, let still try anyway. 
        {
            deviceHandle = strdup(filename);
        }
        safe_Free(genHandle);
        safe_Free(blockHandle);
    }
    else
    {
        deviceHandle = strdup(filename);
    }
    #if defined (_DEBUG)
    printf("%s: Attempting to open %s\n", __FUNCTION__, deviceHandle);
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
            safe_Free(deviceHandle);
            return PERMISSION_DENIED;
        }
        else
        {
            safe_Free(deviceHandle);
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
        safe_Free(deviceHandle);
        return ret;
    }
    //\\TODO: Add support for other flags. 

    if ((device->os_info.fd >= 0) && (ret == SUCCESS))
    {
        if (is_NVMe_Handle(deviceHandle))
        {
            #if !defined(DISABLE_NVME_PASSTHROUGH)
            //Do NVMe specific setup and enumeration
            device->drive_info.drive_type = NVME_DRIVE;
            device->drive_info.interface_type = NVME_INTERFACE;
            ret = ioctl(device->os_info.fd, NVME_IOCTL_ID);
            if (ret < 0)
            {
                 perror("nvme_ioctl_id");
                 return ret;
            }
            device->drive_info.namespaceID = (uint32_t)ret;
            device->os_info.osType = OS_LINUX;
            device->drive_info.media_type = MEDIA_NVM;

            char *baseLink = basename(deviceHandle);
            //Now we will set up the device name, etc fields in the os_info structure.
            snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s", baseLink);
            snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", baseLink);

            ret = fill_Drive_Info_Data(device);
            #if defined (_DEBUG)
            printf("\nsg helper-nvmedev\n");
            printf("Drive type: %d\n",device->drive_info.drive_type);
            printf("Interface type: %d\n",device->drive_info.interface_type);
            printf("Media type: %d\n",device->drive_info.media_type);
            #endif //DEBUG
            #else
            #if defined (_DEBUG)
            printf("\nsg helper-nvmedev --  NVME Passthrough disabled, device not supported\n");
            #endif //DEBUG
            return NOT_SUPPORTED;//return not supported since NVMe-passthrough is disabled
            #endif //DISABLE_NVME_PASSTHROUGH
        }
        else //not an NVMe handle
        {
            #if defined (_DEBUG)
            printf("Getting SG SCSI address\n");
            #endif
            struct sg_scsi_id hctlInfo;
            memset(&hctlInfo, 0, sizeof(struct sg_scsi_id));
            int getHctl = ioctl(device->os_info.fd, SG_GET_SCSI_ID, &hctlInfo);
            if (getHctl == 0 && errno == 0)//when this succeeds, both of these will be zeros
            {
                //printf("Got hctlInfo\n");
                device->os_info.scsiAddress.host = (uint8_t)hctlInfo.host_no;
                device->os_info.scsiAddress.channel = (uint8_t)hctlInfo.channel;
                device->os_info.scsiAddress.target = (uint8_t)hctlInfo.scsi_id;
                device->os_info.scsiAddress.lun = (uint8_t)hctlInfo.lun;
                device->drive_info.namespaceID = device->os_info.scsiAddress.lun + 1;//Doing this to help with USB to NVMe adapters. Luns start at zero, whereas namespaces start with 1, hence the plus 1.
                //also reported are per lun and per device Q-depth which might be nice to store.
                //printf("H:C:T:L = %" PRIu8 ":%" PRIu8 ":%" PRIu8 ":%" PRIu8 "\n", device->os_info.scsiAddress.host, device->os_info.scsiAddress.channel, device->os_info.scsiAddress.target, device->os_info.scsiAddress.lun);
            }

            #if defined (_DEBUG)
            printf("Getting SG driver version\n");
            #endif
            // Check we have a valid device by trying an ioctl
            // From http://tldp.org/HOWTO/SCSI-Generic-HOWTO/pexample.html
            if ((ioctl(device->os_info.fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000))
            {
                printf("%s: SG_GET_VERSION_NUM on %s failed version=%d\n", __FUNCTION__, filename,k);
                perror("SG_GET_VERSION_NUM");
                close(device->os_info.fd);
            }
            else
            {
                //http://www.faqs.org/docs/Linux-HOWTO/SCSI-Generic-HOWTO.html#IDDRIVER
                device->os_info.sgDriverVersion.driverVersionValid = true;
                device->os_info.sgDriverVersion.majorVersion = (uint8_t)(k / 10000);
                device->os_info.sgDriverVersion.minorVersion = (uint8_t)((k - (device->os_info.sgDriverVersion.majorVersion * 10000)) / 100);
                device->os_info.sgDriverVersion.revision = (uint8_t)(k - (device->os_info.sgDriverVersion.majorVersion * 10000) - (device->os_info.sgDriverVersion.minorVersion * 100));
                
                //set the OS Type
                device->os_info.osType = OS_LINUX;

                //set scsi interface and scsi drive until we know otherwise
                device->drive_info.drive_type = SCSI_DRIVE;
                device->drive_info.interface_type = SCSI_INTERFACE;
                device->drive_info.media_type = MEDIA_HDD;
                //now have the device information fields set
                #if defined (_DEBUG)
                printf("Setting interface, drive type, secondary handles\n");
                #endif
                set_Device_Fields_From_Handle(deviceHandle, device);
                setup_Passthrough_Hacks_By_ID(device);

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
                printf("\nsg helper\n");
                printf("Drive type: %d\n",device->drive_info.drive_type);
                printf("Interface type: %d\n",device->drive_info.interface_type);
                printf("Media type: %d\n",device->drive_info.media_type);
                #endif
            }
        }
    }
    safe_Free(deviceHandle);
    return ret;
}
//http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_reset.html
//sgResetType should be one of the values from the link above...so bus or device...controller will work but that shouldn't be done ever.
int sg_reset(int fd, int resetType)
{
    int ret = UNKNOWN;
    
    ret = ioctl(fd, SG_SCSI_RESET, &resetType);

    if (ret < 0)
    {
        #if defined(_DEBUG)
        printf("Reset failure! errorcode: %d, errno: %d\n",ret, errno);
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

int os_Device_Reset(tDevice *device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_DEVICE);
}

int os_Bus_Reset(tDevice *device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_BUS);
}

int os_Controller_Reset(tDevice *device)
{
    return sg_reset(device->os_info.fd, SG_SCSI_RESET_HOST);
}

int send_IO( ScsiIoCtx *scsiIoCtx )
{
    int ret = FAILURE;    
#ifdef _DEBUG
    printf("-->%s \n",__FUNCTION__);
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
            printf("Target Device does not have a valid interface %d\n",\
                        scsiIoCtx->device->drive_info.interface_type);
        }
        break;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int send_sg_io( ScsiIoCtx *scsiIoCtx )
{
    sg_io_hdr_t io_hdr;
    uint8_t     *localSenseBuffer = NULL;
    int         ret          = SUCCESS;
    seatimer_t  commandTimer;
#ifdef _DEBUG
    printf("-->%s \n",__FUNCTION__);
#endif


    memset(&commandTimer,0,sizeof(seatimer_t));
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
        localSenseBuffer = (uint8_t *)calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), scsiIoCtx->device->os_info.minimumAlignment);
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
        safe_Free_aligned(localSenseBuffer);
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
    ret = ioctl(scsiIoCtx->device->os_info.fd, SG_IO, &io_hdr);
    stop_Timer(&commandTimer);
    scsiIoCtx->device->os_info.last_error = errno;
    if (ret < 0)
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
        scsiIoCtx->returnStatus.format  = io_hdr.sbp[0];
        get_Sense_Key_ASC_ASCQ_FRU(io_hdr.sbp, io_hdr.mx_sb_len, &scsiIoCtx->returnStatus.senseKey, &scsiIoCtx->returnStatus.asc, &scsiIoCtx->returnStatus.ascq, &scsiIoCtx->returnStatus.fru);
    }

    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
    {
        switch(io_hdr.info & SG_INFO_DIRECT_IO_MASK)
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
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    safe_Free_aligned(localSenseBuffer);
    return ret;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
static int nvme_filter( const struct dirent *entry)
{
    int nvmeHandle = strncmp("nvme",entry->d_name,4);
    if (nvmeHandle != 0)
    {
        return !nvmeHandle;
    }
    if (strlen(entry->d_name) > 5)
    {
        char* partition = strpbrk(entry->d_name,"p");
        if(partition != NULL)
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
#endif

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
int get_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
{
    int  num_devs = 0, num_nvme_devs = 0;

    struct dirent **namelist;
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    struct dirent **nvmenamelist;
    #endif
    int (*sortFunc)(const struct dirent **, const struct dirent **) = &alphasort;
    #if defined (_GNU_SOURCE)
        sortFunc = &versionsort;//use versionsort instead when available with _GNU_SOURCE
    #endif

    num_devs = scandir("/dev", &namelist, sg_filter, sortFunc); 
    if(num_devs == 0)
    {
        //check for SD devices
        num_devs = scandir("/dev", &namelist, sd_filter, sortFunc); 
    }
    //free the list of names to not leak memory
    for(int iter = 0; iter < num_devs; ++iter)
    {
    	safe_Free(namelist[iter]);
    }
    safe_Free(namelist);
    //add nvme devices to the list
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    num_nvme_devs = scandir("/dev", &nvmenamelist, nvme_filter,sortFunc);
    //free the nvmenamelist to not leak memory
    for(int iter = 0; iter < num_nvme_devs; ++iter)
    {
    	safe_Free(nvmenamelist[iter]);
    }
    safe_Free(nvmenamelist);
    #endif

    *numberOfDevices = num_devs + num_nvme_devs;

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
int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    int returnValue = SUCCESS;
    int numberOfDevices = 0;
    int driveNumber = 0, found = 0, failedGetDeviceCount = 0, permissionDeniedCount = 0;
    char name[80] = { 0 }; //Because get device needs char
    int fd;
    tDevice * d = NULL;
#if defined (DEGUG_SCAN_TIME)
    seatimer_t getDeviceTimer;
    seatimer_t getDeviceListTimer;
    memset(&getDeviceTimer, 0, sizeof(seatimer_t));
    memset(&getDeviceListTimer, 0, sizeof(seatimer_t));
#endif
    
    int  num_sg_devs = 0, num_sd_devs = 0, num_nvme_devs = 0;

    struct dirent **namelist;
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    struct dirent **nvmenamelist;
    #endif

    int (*sortFunc)(const struct dirent **, const struct dirent **) = &alphasort;
    #if defined (_GNU_SOURCE)
        sortFunc = &versionsort;//use versionsort instead when available with _GNU_SOURCE
    #endif

    num_sg_devs = scandir("/dev", &namelist, sg_filter, sortFunc); 
    if(num_sg_devs == 0)
    {
        //check for SD devices
        num_sd_devs = scandir("/dev", &namelist, sd_filter, sortFunc); 
    }
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    //add nvme devices to the list
    num_nvme_devs = scandir("/dev", &nvmenamelist, nvme_filter,sortFunc);
    #endif
    
    char **devs = (char **)calloc(num_sg_devs + num_sd_devs + num_nvme_devs + 1, sizeof(char *));
    int i = 0;
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    int j = 0;
    #endif
    //add sg/sd devices to the list
    for (; i < (num_sg_devs + num_sd_devs); i++)
    {
        size_t handleSize = (strlen("/dev/") + strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i] = (char *)malloc(handleSize);
        snprintf(devs[i], handleSize, "/dev/%s", namelist[i]->d_name);
        safe_Free(namelist[i]);
    }
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    //add nvme devices to the list
    for (j = 0; i < (num_sg_devs + num_sd_devs + num_nvme_devs) && j < num_nvme_devs;i++, j++)
    {
        size_t handleSize = (strlen("/dev/") + strlen(nvmenamelist[j]->d_name) + 1) * sizeof(char);
        devs[i] = (char *)malloc(handleSize);
        snprintf(devs[i], handleSize, "/dev/%s", nvmenamelist[j]->d_name);
        safe_Free(nvmenamelist[j]);
    }
    #endif
    devs[i] = NULL; //Added this so the for loop down doesn't cause a segmentation fault.
    safe_Free(namelist);
    #if !defined(DISABLE_NVME_PASSTHROUGH)
    safe_Free(nvmenamelist);
    #endif

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
        for (driveNumber = 0; ((driveNumber >= 0 && (unsigned int)driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < (num_sg_devs + num_sd_devs + num_nvme_devs)) && (found < numberOfDevices)); ++driveNumber)
        {
            if(!devs[driveNumber] || strlen(devs[driveNumber]) == 0)
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
                int ret = get_Device(name, d);
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
            safe_Free(devs[driveNumber]);
        }
#if defined (DEGUG_SCAN_TIME)
        stop_Timer(&getDeviceListTimer);
        printf("Time to get all device = %fms\n", get_Milli_Seconds(getDeviceListTimer));
#endif
	    if (found == failedGetDeviceCount)
	    {
	        returnValue = FAILURE;
	    }
        else if(permissionDeniedCount == (num_sg_devs + num_sd_devs + num_nvme_devs))
        {
            returnValue = PERMISSION_DENIED;
        }
	    else if (failedGetDeviceCount && returnValue != PERMISSION_DENIED)
	    {
	        returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
	    }
    }
    safe_Free(devs);
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
int close_Device(tDevice *dev)
{
    int retValue = 0;
    if (dev)
    {
        retValue = close(dev->os_info.fd);
        dev->os_info.last_error = errno;
        if ( retValue == 0)
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

#if !defined(DISABLE_NVME_PASSTHROUGH)
int send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx )
{
    int ret = SUCCESS;//NVME_SC_SUCCESS;//This defined value used to exist in some version of nvme.h but is missing in nvme_ioctl.h...it was a value of zero, so this should be ok.
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    struct nvme_admin_cmd adminCmd;
    struct nvme_user_io nvmCmd;// it's possible that this is not defined in some funky early nvme kernel, but we don't see that today. This seems to be defined everywhere. -TJE
    struct nvme_passthru_cmd *passThroughCmd = (struct nvme_passthru_cmd*)&adminCmd;//setting a pointer since these are defined to be the same. No point in allocating yet another structure. - TJE

    int32_t ioctlResult = 0;

    if (!nvmeIoCtx)
    {
        return BAD_PARAMETER; 
    }

    switch (nvmeIoCtx->commandType) 
    {
    case NVM_ADMIN_CMD:
        memset(&adminCmd, 0,sizeof(struct nvme_admin_cmd));
        adminCmd.opcode = nvmeIoCtx->cmd.adminCmd.opcode;
        adminCmd.flags = nvmeIoCtx->cmd.adminCmd.flags;
        adminCmd.rsvd1 = nvmeIoCtx->cmd.adminCmd.rsvd1;
        adminCmd.nsid = nvmeIoCtx->cmd.adminCmd.nsid;
        adminCmd.cdw2 = nvmeIoCtx->cmd.adminCmd.cdw2;
        adminCmd.cdw3 = nvmeIoCtx->cmd.adminCmd.cdw3;
        adminCmd.metadata = (uint64_t)(uintptr_t)nvmeIoCtx->cmd.adminCmd.metadata;
        adminCmd.addr = (uint64_t)(uintptr_t)nvmeIoCtx->cmd.adminCmd.addr;
        adminCmd.metadata_len = nvmeIoCtx->cmd.adminCmd.metadataLen;
        adminCmd.data_len = nvmeIoCtx->dataSize;
        adminCmd.cdw10 = nvmeIoCtx->cmd.adminCmd.cdw10;
        adminCmd.cdw11 = nvmeIoCtx->cmd.adminCmd.cdw11;
        adminCmd.cdw12 = nvmeIoCtx->cmd.adminCmd.cdw12;
        adminCmd.cdw13 = nvmeIoCtx->cmd.adminCmd.cdw13;
        adminCmd.cdw14 = nvmeIoCtx->cmd.adminCmd.cdw14;
        adminCmd.cdw15 = nvmeIoCtx->cmd.adminCmd.cdw15;
        adminCmd.timeout_ms = nvmeIoCtx->timeout ? nvmeIoCtx->timeout * 1000 : 15000;
        start_Timer(&commandTimer);
        ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_ADMIN_CMD, &adminCmd);
        stop_Timer(&commandTimer);
        nvmeIoCtx->device->os_info.last_error = errno;
        if (ioctlResult < 0)
        {
            ret = OS_PASSTHROUGH_FAILURE;
            if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
            {
                if (nvmeIoCtx->device->os_info.last_error != 0)
                {
                    printf("Error: ");
                    print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
                }
            }
        }
        else
        {
            nvmeIoCtx->commandCompletionData.commandSpecific = adminCmd.result;
            nvmeIoCtx->commandCompletionData.dw3Valid = true;
            nvmeIoCtx->commandCompletionData.dw0Valid = true;
            nvmeIoCtx->commandCompletionData.statusAndCID = ioctlResult << 17;//shift into place since we don't get the phase tag or command ID bits and these are the status field
        }
        break;
    case NVM_CMD:
        //check opcode to perform the correct IOCTL
        switch (nvmeIoCtx->cmd.nvmCmd.opcode)
        {
        case NVME_CMD_READ:
        case NVME_CMD_WRITE:
            //use user IO cmd structure and SUBMIT_IO IOCTL
            memset(&nvmCmd, 0, sizeof(nvmCmd));
            nvmCmd.opcode = nvmeIoCtx->cmd.nvmCmd.opcode;
            nvmCmd.flags = nvmeIoCtx->cmd.nvmCmd.flags;
            nvmCmd.control = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw12);
            nvmCmd.nblocks = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12);
            nvmCmd.rsvd = RESERVED;
            nvmCmd.metadata = (uint64_t)(uintptr_t)nvmeIoCtx->cmd.nvmCmd.metadata;
            nvmCmd.addr = (uint64_t)(uintptr_t)nvmeIoCtx->ptrData;
            nvmCmd.slba = M_DWordsTo8ByteValue(nvmeIoCtx->cmd.nvmCmd.cdw11, nvmeIoCtx->cmd.nvmCmd.cdw10);
            nvmCmd.dsmgmt = nvmeIoCtx->cmd.nvmCmd.cdw13;
            nvmCmd.reftag = nvmeIoCtx->cmd.nvmCmd.cdw14;
            nvmCmd.apptag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
            nvmCmd.appmask = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
            start_Timer(&commandTimer);
            ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_SUBMIT_IO, &nvmCmd);
            stop_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = errno;
            if (ioctlResult < 0)
            {
                ret = OS_PASSTHROUGH_FAILURE;
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    if (nvmeIoCtx->device->os_info.last_error != 0)
                    {
                        printf("Error: ");
                        print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
                    }
                }
            }
            else
            {
                nvmeIoCtx->commandCompletionData.dw3Valid = true;
                //TODO: How do we set the command specific result on read/write?
                nvmeIoCtx->commandCompletionData.statusAndCID = ioctlResult << 17;//shift into place since we don't get the phase tag or command ID bits and these are the status field
            }
            break;
        default:
            //use the generic passthrough command structure and IO_CMD
            memset(passThroughCmd, 0,sizeof(struct nvme_passthru_cmd));
            passThroughCmd->opcode = nvmeIoCtx->cmd.nvmCmd.opcode;
            passThroughCmd->flags = nvmeIoCtx->cmd.nvmCmd.flags;
            passThroughCmd->rsvd1 = RESERVED; //TODO: Should we put this in here since it's part of this DWORD? nvmeIoCtx->cmd.nvmCmd.commandId;
            passThroughCmd->nsid = nvmeIoCtx->cmd.nvmCmd.nsid;
            passThroughCmd->cdw2 = nvmeIoCtx->cmd.nvmCmd.cdw2;
            passThroughCmd->cdw3 = nvmeIoCtx->cmd.nvmCmd.cdw3;
            passThroughCmd->metadata = (uint64_t)(uintptr_t)nvmeIoCtx->cmd.nvmCmd.metadata;
            passThroughCmd->addr = (uint64_t)(uintptr_t)nvmeIoCtx->ptrData;
            passThroughCmd->metadata_len = M_DoubleWord0(nvmeIoCtx->cmd.nvmCmd.prp2);//guessing here since I don't really know - TJE
            passThroughCmd->data_len = nvmeIoCtx->dataSize;//Or do I use the other PRP2 data? Not sure - TJE //M_DWord1(nvmeIoCtx->cmd.nvmCmd.prp2);//guessing here since I don't really know - TJE
            passThroughCmd->cdw10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
            passThroughCmd->cdw11 = nvmeIoCtx->cmd.nvmCmd.cdw11;
            passThroughCmd->cdw12 = nvmeIoCtx->cmd.nvmCmd.cdw12;
            passThroughCmd->cdw13 = nvmeIoCtx->cmd.nvmCmd.cdw13;
            passThroughCmd->cdw14 = nvmeIoCtx->cmd.nvmCmd.cdw14;
            passThroughCmd->cdw15 = nvmeIoCtx->cmd.nvmCmd.cdw15;
            passThroughCmd->timeout_ms = nvmeIoCtx->timeout ? nvmeIoCtx->timeout * 1000 : 15000;//timeout is in seconds, so converting to milliseconds
            start_Timer(&commandTimer);
            ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_IO_CMD, passThroughCmd);
            stop_Timer(&commandTimer);
            nvmeIoCtx->device->os_info.last_error = errno;
            if (ioctlResult < 0)
            {
                ret = OS_PASSTHROUGH_FAILURE;
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    if (nvmeIoCtx->device->os_info.last_error != 0)
                    {
                        printf("Error: ");
                        print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
                    }
                }
            }
            else
            {
                nvmeIoCtx->commandCompletionData.commandSpecific = passThroughCmd->result;
                nvmeIoCtx->commandCompletionData.dw3Valid = true;
                nvmeIoCtx->commandCompletionData.dw0Valid = true;
                nvmeIoCtx->commandCompletionData.statusAndCID = ioctlResult << 17;//shift into place since we don't get the phase tag or command ID bits and these are the status field
            }
            break;
        }
        break;
    default:
        return BAD_PARAMETER;
        break;
    }
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

int linux_NVMe_Reset(tDevice *device, bool subsystemReset)
{
    //Can only do a reset on a controller handle. Need to get the controller handle if this is a namespace handle!!!
    int ret = OS_PASSTHROUGH_FAILURE;
    int handleToReset = device->os_info.fd;
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    uint16_t controllerNumber = 0;
    uint32_t namespaceID = 0;
    int ioRes = 0;
    bool openedControllerHandle = false;//used so we can close the handle at the end.
    //Need to make sure the handle we use to issue the reset is a controller handle and not a namespace handle.
    int sscanfRes = sscanf(device->os_info.name, "/dev/nvme%" SCNu16 "n%" SCNu32 , &controllerNumber, &namespaceID);
    if (sscanfRes == 2)
    {
        //found a namespace. Need to open a controller handle instead and use it.
        char controllerHandle[40] = { 0 };
        snprintf(controllerHandle, 40, "/dev/nvme%" PRIu16, controllerNumber);
        if ((handleToReset = open(controllerHandle, O_RDWR | O_NONBLOCK)) < 0)
        {
            device->os_info.last_error = errno;
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
            {
                printf("Error opening controller handle for nvme reset: ");
                print_Errno_To_Screen(errno);
            }
            if (errno == EACCES) 
            {
                return PERMISSION_DENIED;
            }
            else
            {
                return OS_PASSTHROUGH_FAILURE;
            }
        }
        openedControllerHandle = true;
    }
    device->os_info.last_error = 0;
    if (subsystemReset)
    {
        start_Timer(&commandTimer);
        ioRes = ioctl(handleToReset, NVME_IOCTL_SUBSYS_RESET);
        stop_Timer(&commandTimer);
    }
    else
    {   
        start_Timer(&commandTimer);
        ioRes = ioctl(handleToReset, NVME_IOCTL_RESET);
        stop_Timer(&commandTimer);
    }
    device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    device->drive_info.lastNVMeResult.lastNVMeStatus = 0;
    device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = 0;
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {   
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    if (ioRes < 0)
    {   
        //failed!
        device->os_info.last_error = errno;
        if (device->deviceVerbosity > VERBOSITY_COMMAND_VERBOSE && device->os_info.last_error != 0)
        {
            printf("Error: ");
            print_Errno_To_Screen(device->os_info.last_error);
        }
    }
    else
    {
        //success!
        ret = SUCCESS;
    }
    if (openedControllerHandle)
    {
        //close the controller handle we opened in this function
        close(handleToReset);
    }
    return ret;
}

int os_nvme_Reset(tDevice *device)
{
    int ret = SUCCESS;
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Sending NVMe Reset\n");
    }
    ret = linux_NVMe_Reset(device, false);
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        print_Return_Enum("NVMe Reset", ret);
    }
    return ret;
}

int os_nvme_Subsystem_Reset(tDevice *device)
{
    int ret = SUCCESS;
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Sending NVMe Subsystem Reset\n");
    }
    ret = linux_NVMe_Reset(device, false);
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        print_Return_Enum("NVMe Subsystem Reset", ret);
    }
    return ret;
}

//to be used with a deep scan???
//fd must be a controller handle
//TODO: Should we rework the linux_NVMe_Reset call to handle this too?
//int nvme_Namespace_Rescan(int fd)
//{
//    int ret = OS_PASSTHROUGH_FAILURE;
//    int ioRes = ioctl(fd, NVME_IOCTL_RESCAN);
//    if (ioRes < 0)
//    {
//        //failed!
//        perror("NVMe Rescan");
//    }
//    else
//    {
//        //success!
//        ret = SUCCESS;
//    }
//    return ret;
//}

//Case to remove this from sg_helper.h/c and have a platform/lin/pci-herlper.h vs platform/win/pci-helper.c 

int pci_Read_Bar_Reg( tDevice * device, uint8_t * pData, uint32_t dataSize )
{
    int ret = UNKNOWN;
    int fd=0;
    void * barRegs = NULL;
    char sysfsPath[PATH_MAX];
    snprintf(sysfsPath, PATH_MAX, "/sys/block/%s/device/resource0",device->os_info.name);
    fd = open(sysfsPath, O_RDONLY);
    if (fd >= 0) 
    {
        //
        barRegs = mmap(0,dataSize,PROT_READ, MAP_SHARED, fd, 0);
        if (barRegs != MAP_FAILED) 
        {
            ret = SUCCESS;
            memcpy(pData,barRegs,dataSize);
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
}
#endif
int os_Read(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool async, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Write(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool async, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Verify(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

int os_Flush(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}

int os_Lock_Device(tDevice *device)
{
    int ret = SUCCESS;
    //Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    //disable O_NONBLOCK
    flags &= ~O_NONBLOCK;
    //Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

int os_Unlock_Device(tDevice *device)
{
    int ret = SUCCESS;
    //Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    //enable O_NONBLOCK
    flags |= O_NONBLOCK;
    //Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}


//This should be at the end of this file to undefine _GNU_SOURCE if this file manually enabled it
#if !defined (_GNU_SOURCE_DEFINED_IN_SG_HELPER)
    #undef _GNU_SOURCE
    #undef _GNU_SOURCE_DEFINED_IN_SG_HELPER
#endif
