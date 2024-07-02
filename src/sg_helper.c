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

//If _GNU_SOURCE is not already defined, define it to get access to versionsort
//NOTE: This will be undefined at the end of this file if _GNU_SOURCE_DEFINED_IN_SG_HELPER is set to prevent unexpected
//      behavior in any other parts of the code.
//NOTE: Adding this definition like this so that it can also easily be removed as necessary in the future in case of errors.
#if !defined (_GNU_SOURCE)
    #define _GNU_SOURCE
    #define _GNU_SOURCE_DEFINED_IN_SG_HELPER
#if defined (_DEBUG)
    #pragma message "Defining _GNU_SOURCE since it was not already defined."
#endif //_DEBUG
#endif //!defined (_GNU_SOURCE)

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
#include "nvme_helper_func.h"
#include "sntl_helper.h"
#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <mntent.h>
#include <sys/mount.h>//for umount and umount2. NOTE: This defines the things we need from linux/fs.h as well, which is why that is commented out - TJE
#include <linux/fs.h> //for BLKRRPART to refresh partition info after completion of an erase
#if defined (__has_include)//GCC5 and higher support this, BUT only if a C standard is specified. The -std=gnuXX does not support this properly for some odd reason.
    #if __has_include (<linux/nvme_ioctl.h>)
        #if defined (_DEBUG)
        #pragma message "Using linux/nvme_ioctl.h"
        #endif
        #include <linux/nvme_ioctl.h>
        #if !defined (SEA_NVME_IOCTL_H)
            #define SEA_NVME_IOCTL_H
        #endif
    #elif __has_include (<linux/nvme.h>)
        #if defined (_DEBUG)
        #pragma message "Using linux/nvme.h"
        #endif
        #include <linux/nvme.h>
        #if !defined (SEA_NVME_IOCTL_H)
            #define SEA_NVME_IOCTL_H
        #endif
    #elif __has_include (<uapi/nvme.h>)
        #if defined (_DEBUG)
        #pragma message "Using uapi/nvme.h"
        #endif
        #include <uapi/nvme.h>
        #if !defined (SEA_UAPI_NVME_H)
            #define SEA_UAPI_NVME_H
        #endif
    #else //__has_include could not locate the header, check if it was specified by the user through a define.
        #if defined (SEA_NVME_IOCTL_H)
            #include <linux/nvme_ioctl.h>
        #elif defined (SEA_NVME_H)
            #include <linux/nvme.h>
        #elif defined (SEA_UAPI_NVME_H)
            #include <uapi/nvme.h>
        #else
            #pragma message "No NVMe header detected with __has_include. Assuming no NVMe support."
        #endif
    #endif
#else
    #if defined (SEA_NVME_IOCTL_H)
        #include <linux/nvme_ioctl.h>
    #elif defined (SEA_NVME_H)
        #include <linux/nvme.h>
    #elif defined (SEA_UAPI_NVME_H)
        #include <uapi/nvme.h>
    #else
        #pragma message "No NVMe header detected. Assuming no NVMe support. Define one of the following to include the correct NVMe header: SEA_NVME_IOCTL_H, SEA_NVME_H, or SEA_UAPI_NVME_H\nThese specify whether the NVMe IOCTL is in /usr/include/linux/nvme_ioctl.h, /usr/include/linux/nvme.h, or /usr/include/uapi/nvme.h"
    #endif
#endif

#if defined (ENABLE_CISS)
#include "raid_scan_helper.h"
#include "ciss_helper_func.h"
#endif //ENABLE_CISS

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

static bool is_NVMe_Handle(char *handle)
{
    bool isNvmeDevice = false;
    if (handle && strlen(handle))
    {
        if (strstr(handle, "nvme"))
        {
            isNvmeDevice = true;
        }
    }
    return isNvmeDevice;
}

#define GETMNTENT_R_LINE_BUF_SIZE (256)
static int get_Partition_Count(const char * blockDeviceName)
{
    int result = 0;
    FILE *mount = setmntent("/etc/mtab", "r");//we only need to know about mounted partitions. Mounted partitions need to be known so that they can be unmounted when necessary. - TJE
    if (mount)
    {
        struct mntent* entry = NULL;
#if defined (_BSD_SOURCE) || defined(_SVID_SOURCE) //getmntent_r lists these feature test macros to look for - TJE
        struct mntent entBuf;
        char lineBuf[GETMNTENT_R_LINE_BUF_SIZE] = { 0 };
        while (NULL != (entry = getmntent_r(mount, &entBuf, lineBuf, GETMNTENT_R_LINE_BUF_SIZE)))
#else //use the not thread safe version since that is all that is available
        while (NULL != (entry = getmntent(mount)))
#endif
        {
            if (strstr(entry->mnt_fsname, blockDeviceName))
            {
                //Found a match, increment result counter.
                ++result;
            }
        }
        endmntent(mount);
    }
    else
    {
        result = -1;//indicate an error opening the mtab file
    }
    return result;
}

#define PART_INFO_NAME_LENGTH (32)
#define PART_INFO_PATH_LENGTH (64)
typedef struct _spartitionInfo
{
    char fsName[PART_INFO_NAME_LENGTH];
    char mntPath[PART_INFO_PATH_LENGTH];
}spartitionInfo, *ptrsPartitionInfo;
//partitionInfoList is a pointer to the beginning of the list
//listCount is the number of these structures, which should be returned by get_Partition_Count
static eReturnValues get_Partition_List(const char * blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount)
{
    eReturnValues result = SUCCESS;
    int matchesFound = 0;
    if (listCount > 0)
    {
        FILE *mount = setmntent("/etc/mtab", "r");//we only need to know about mounted partitions. Mounted partitions need to be known so that they can be unmounted when necessary. - TJE
        if (mount)
        {
            struct mntent* entry = NULL;
#if defined(_BSD_SOURCE) || defined (_SVID_SOURCE) || !defined (NO_GETMNTENT_R) //feature test macros we're defining _BSD_SOURCE or _SVID_SOURCE in my testing, but we want the reentrant version whenever possible. This can be defined if this function is not identified. - TJE
            struct mntent entBuf;
            char lineBuf[GETMNTENT_R_LINE_BUF_SIZE] = { 0 };
            while (NULL != (entry = getmntent_r(mount, &entBuf, lineBuf, GETMNTENT_R_LINE_BUF_SIZE)))
#else //use the not thread safe version since that is all that is available
#pragma message "Not using getmntent_r. Partition detection is not thread safe"
            while (NULL != (entry = getmntent(mount)))
#endif
            {
                if (strstr(entry->mnt_fsname, blockDeviceName))
                {
                    //found a match, copy it to the list
                    if (matchesFound < listCount)
                    {
                        snprintf((partitionInfoList + matchesFound)->fsName, PART_INFO_NAME_LENGTH, "%s", entry->mnt_fsname);
                        snprintf((partitionInfoList + matchesFound)->mntPath, PART_INFO_PATH_LENGTH, "%s", entry->mnt_dir);
                        ++matchesFound;
                    }
                    else
                    {
                        result = MEMORY_FAILURE;//out of memory to copy all results to the list.
                    }
                }
            }
            endmntent(mount);
        }
        else
        {
            result = FAILURE;
        }
    }
    return result;
}

typedef struct _sysFSLowLevelDeviceInfo
{
    eSCSIPeripheralDeviceType scsiDevType;//in Linux this will be reading the "type" file to get this. If it is not available, will retry with "inquiry" data file's first byte
    eDriveType     drive_type;
    eInterfaceType interface_type;
    adapterInfo     adapter_info;
    driverInfo		driver_info;
    struct {
        uint8_t         host;//AKA SCSI adapter #
        uint8_t         channel;//AKA bus
        uint8_t         target;//AKA id number
        uint8_t         lun;//logical unit number
    }scsiAddress;
    char fullDevicePath[OPENSEA_PATH_MAX];
    char primaryHandleStr[OS_HANDLE_NAME_MAX_LENGTH]; //dev/sg or /dev/nvmexny (namespace handle)
    char secondaryHandleStr[OS_SECOND_HANDLE_NAME_LENGTH]; //dev/sd or /dev/nvmex (controller handle)
    char tertiaryHandleStr[OS_SECOND_HANDLE_NAME_LENGTH]; //dev/bsg or /dev/ngXnY (nvme generic handle)
    uint16_t queueDepth;//if 0, then this was unable to be read and populated
}sysFSLowLevelDeviceInfo;

M_NODISCARD static bool get_Driver_Version_Info_From_String(const char* driververstr, uint32_t *versionlist, uint8_t versionlistlen, uint8_t *versionCount)
{
    //There are a few formats that I have seen for this data:
    //major.minor
    //major.minor.rev
    //major.minor.rev[build]-string
    //There may be more.
    if (driververstr && versionlist && versionlistlen == 4 && versionCount)//require 4 spaces for the current parsing based off of what is commented above
    {
        char* end = NULL;
        char *str = C_CAST(char*, driververstr);
        unsigned long value = strtoul(str, &end, 10);
        *versionCount = 0;
        //major
        if ((value == ULONG_MAX && errno == ERANGE) || (value == 0 && str == end) || end[0] != '.')
        {
            return false;
        }
        versionlist[0] = C_CAST(uint32_t, value);
        *versionCount += 1;
        str = end + 1;//update to past the first dot.
        //minor
        value = strtoul(str, &end, 10);
        if ((value == ULONG_MAX && errno == ERANGE) || (value == 0 && str == end))
        {
            return false;
        }
        versionlist[1] = C_CAST(uint32_t, value);
        *versionCount += 1;
        if (end[0] == '\0')
        {
            return true;
        }
        else if (end[0] == '.' && strlen(end) > 1)
        {
            //rev is available
            str = end + 1;
            value = strtoul(str, &end, 10);
            if ((value == ULONG_MAX && errno == ERANGE) || (value == 0 && str == end))
            {
                return false;
            }
            versionlist[2] = C_CAST(uint32_t, value);
            *versionCount += 1;
            if (end[0] == '\0')
            {
                return true;
            }
            else if (end[0] == '[' && strlen(end) > 1)
            {
                //build is available
                str = end + 1;
                value = strtoul(str, &end, 10);
                if ((value == ULONG_MAX && errno == ERANGE) || (value == 0 && str == end))
                {
                    return false;
                }
                versionlist[3] = C_CAST(uint32_t, value);
                *versionCount += 1;
                if (end[0] == '\0' || end[0] == ']')
                {
                    //considering this complete for now
                    return true;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

static void get_Driver_Version_Info_From_Path(char* driverPath, sysFSLowLevelDeviceInfo *sysFsInfo)
{
    //driverPath now has the full path with the name of the driver.
    //the version number can be found in driverPath/module/version if this file exists.
    //Read this file and save the version information
    char* driverVersionFilePath = C_CAST(char*, calloc(OPENSEA_PATH_MAX, sizeof(char)));
    if (driverVersionFilePath)
    {
        snprintf(driverVersionFilePath, OPENSEA_PATH_MAX, "%s/module/version", driverPath);
        //printf("driver version file path = %s\n", driverVersionFilePath);
        //convert relative path to a full path. Basically replace ../'s with /sys/ since this will always be ../../bus and we need /sys/buf
        char* busPtr = strstr(driverVersionFilePath, "/bus");
        size_t busPtrLen = strlen(busPtr);
        //magic number 4 is for the length of the string "/sys" which is what is being set in the beginning of the path.
        //This is a bit of a mess, but a simple call to realpath was not working, likely due to the current directory not being exactly what we want to start with to allow
        //that function to correctly figure out the path.
        memmove(&driverVersionFilePath[4], busPtr, busPtrLen);
        memset(&driverVersionFilePath[busPtrLen + 4], 0, OPENSEA_PATH_MAX - (busPtrLen + 4));
        driverVersionFilePath[0] = '/';
        driverVersionFilePath[1] = 's';
        driverVersionFilePath[2] = 'y';
        driverVersionFilePath[3] = 's';

        struct stat driverversionstat;
        memset(&driverversionstat, 0, sizeof(struct stat));
        if (0 == stat(driverVersionFilePath, &driverversionstat))
        {
            off_t versionFileSize = driverversionstat.st_size;
            if (versionFileSize > 0)
            {
                FILE* versionFile = fopen(driverVersionFilePath, "r");
                if (versionFile)
                {
                    char* versionFileData = C_CAST(char*, calloc(C_CAST(size_t, versionFileSize) + 1, sizeof(char)));
                    if (versionFileData)
                    {
                        if (C_CAST(size_t, versionFileSize) == fread(versionFileData, sizeof(char), C_CAST(size_t, versionFileSize), versionFile) && !ferror(versionFile))
                        {
                            printf("versionFileData = %s\n", versionFileData);
                            snprintf(sysFsInfo->driver_info.driverVersionString, MAX_DRIVER_VER_STR, "%s", versionFileData);
                            uint32_t versionList[4] = { 0 };
                            uint8_t versionCount = 0;
                            if (get_Driver_Version_Info_From_String(versionFileData, versionList, 4, &versionCount))
                            {
                                switch (versionCount)
                                {
                                case 4:
                                    //try figuring out what is in the extraVerInfo string
                                    sysFsInfo->driver_info.majorVerValid = true;
                                    sysFsInfo->driver_info.minorVerValid = true;
                                    sysFsInfo->driver_info.revisionVerValid = true;
                                    sysFsInfo->driver_info.buildVerValid = true;
                                    sysFsInfo->driver_info.driverMajorVersion = versionList[0];
                                    sysFsInfo->driver_info.driverMinorVersion = versionList[1];
                                    sysFsInfo->driver_info.driverRevision = versionList[2];
                                    sysFsInfo->driver_info.driverBuildNumber = versionList[3];
                                    break;
                                case 3:
                                    sysFsInfo->driver_info.majorVerValid = true;
                                    sysFsInfo->driver_info.minorVerValid = true;
                                    sysFsInfo->driver_info.revisionVerValid = true;
                                    sysFsInfo->driver_info.driverMajorVersion = versionList[0];
                                    sysFsInfo->driver_info.driverMinorVersion = versionList[1];
                                    sysFsInfo->driver_info.driverRevision = versionList[2];
                                    break;
                                case 2:
                                    sysFsInfo->driver_info.majorVerValid = true;
                                    sysFsInfo->driver_info.minorVerValid = true;
                                    sysFsInfo->driver_info.driverMajorVersion = versionList[0];
                                    sysFsInfo->driver_info.driverMinorVersion = versionList[1];
                                    break;
                                default:
                                    //error reading the string! consider the whole scanf a failure!
                                    //Will need to add other format parsing here if there is something else to read instead.-TJE
                                    sysFsInfo->driver_info.driverMajorVersion = 0;
                                    sysFsInfo->driver_info.driverMinorVersion = 0;
                                    sysFsInfo->driver_info.driverRevision = 0;
                                    sysFsInfo->driver_info.driverBuildNumber = 0;
                                    break;
                                }
                            }
                            else
                            {
                                sysFsInfo->driver_info.driverMajorVersion = 0;
                                sysFsInfo->driver_info.driverMinorVersion = 0;
                                sysFsInfo->driver_info.driverRevision = 0;
                                sysFsInfo->driver_info.driverBuildNumber = 0;
                            }
                        }
                        safe_Free(C_CAST(void**, &versionFileData));
                    }
                    fclose(versionFile);
                }
            }
        }
        safe_Free(C_CAST(void**, &driverVersionFilePath));
    }
    snprintf(sysFsInfo->driver_info.driverName, MAX_DRIVER_NAME, "%s", basename(driverPath));
    return;
}

static void get_SYS_FS_ATA_Info(const char *inHandleLink, sysFSLowLevelDeviceInfo *sysFsInfo)
{
#if defined (_DEBUG)
    printf("ATA interface!\n");
#endif
    sysFsInfo->interface_type = IDE_INTERFACE;
    sysFsInfo->drive_type = ATA_DRIVE;//changed to ATAPI later if we detect it
    //get vendor and product IDs of the controller attached to this device.
    char fullPciPath[PATH_MAX] = { 0 };
    snprintf(fullPciPath, PATH_MAX, "%s", inHandleLink);

    fullPciPath[0] = '/';
    fullPciPath[1] = 's';
    fullPciPath[2] = 'y';
    fullPciPath[3] = 's';
    fullPciPath[4] = '/';
    memmove(&fullPciPath[5], &fullPciPath[6], strlen(fullPciPath));
    snprintf(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullPciPath);
    intptr_t newStrLen = strstr(fullPciPath, "/ata") - fullPciPath;
    if (newStrLen > 0)
    {
        char *pciPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
        if (pciPath)
        {
            snprintf(pciPath, PATH_MAX, "%.*s/vendor", C_CAST(int, newStrLen), fullPciPath);
            //printf("shortened Path = %s\n", dirname(pciPath));
            FILE *temp = NULL;
            temp = fopen(pciPath, "r");
            if (temp)
            {
                if (1 == fscanf(temp, "0x%" SCNx32, &sysFsInfo->adapter_info.vendorID))
                {
                    sysFsInfo->adapter_info.vendorIDValid = true;
                    //printf("Got vendor as %" PRIX16 "h\n", sysFsInfo->adapter_info.vendorID);
                }
                fclose(temp);
                temp = NULL;
            }
            pciPath = dirname(pciPath);//remove vendor from the end
            common_String_Concat(pciPath, PATH_MAX, "/device");
            temp = fopen(pciPath, "r");
            if (temp)
            {
                if (1 == fscanf(temp, "0x%" SCNx32, &sysFsInfo->adapter_info.productID))
                {
                    sysFsInfo->adapter_info.productIDValid = true;
                    //printf("Got product as %" PRIX16 "h\n", sysFsInfo->adapter_info.productID);
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
                    sysFsInfo->adapter_info.revision = pciRev;
                    sysFsInfo->adapter_info.revisionValid = true;
                    //printf("Got revision as %" PRIX16 "h\n", sysFsInfo->adapter_info.revision);
                }
                fclose(temp);
                temp = NULL;
            }
            //Get Driver Information.
            pciPath = dirname(pciPath);//remove driver from the end
            common_String_Concat(pciPath, PATH_MAX, "/driver");
            char *driverPath = C_CAST(char *, calloc(OPENSEA_PATH_MAX, sizeof(char)));
            ssize_t len = readlink(pciPath, driverPath, OPENSEA_PATH_MAX);
            if (len != -1)
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            safe_Free(C_CAST(void**, &driverPath));
            safe_Free(C_CAST(void**, &pciPath));
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_PCI;
        }
    }
    return;
}

static void get_SYS_FS_USB_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo *sysFsInfo)
{
#if defined (_DEBUG)
    printf("USB interface!\n");
#endif
    sysFsInfo->interface_type = USB_INTERFACE;
    sysFsInfo->drive_type = SCSI_DRIVE;//changed later if detected as ATA or NVMe
    //set the USB VID and PID. NOTE: There may be a better way to do this, but this seems to work for now.
    char fullPciPath[PATH_MAX] = { 0 };
    snprintf(fullPciPath, PATH_MAX, "%s", inHandleLink);

    fullPciPath[0] = '/';
    fullPciPath[1] = 's';
    fullPciPath[2] = 'y';
    fullPciPath[3] = 's';
    fullPciPath[4] = '/';
    memmove(&fullPciPath[5], &fullPciPath[6], strlen(fullPciPath));
    snprintf(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullPciPath);
    intptr_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath;
    if (newStrLen > 0)
    {
        char *usbPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
        if (usbPath)
        {
            snprintf(usbPath, PATH_MAX, "%.*s", C_CAST(int, newStrLen), fullPciPath);
            usbPath = dirname(usbPath);
            //printf("full USB Path = %s\n", usbPath);
            //now that the path is correct, we need to read the files idVendor and idProduct
            common_String_Concat(usbPath, PATH_MAX, "/idVendor");
            //printf("idVendor USB Path = %s\n", usbPath);
            FILE *temp = NULL;
            temp = fopen(usbPath, "r");
            if (temp)
            {
                if (1 == fscanf(temp, "%" SCNx32, &sysFsInfo->adapter_info.vendorID))
                {
                    sysFsInfo->adapter_info.vendorIDValid = true;
                    //printf("Got vendor ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.vendorID);
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
                if (1 == fscanf(temp, "%" SCNx32, &sysFsInfo->adapter_info.productID))
                {
                    sysFsInfo->adapter_info.productIDValid = true;
                    //printf("Got product ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.productID);
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
                if (1 == fscanf(temp, "%" SCNx32, &sysFsInfo->adapter_info.revision))
                {
                    sysFsInfo->adapter_info.revisionValid = true;
                    //printf("Got revision as %" PRIX16 "h\n", sysFsInfo->adapter_info.revision);
                }
                fclose(temp);
                temp = NULL;
            }
            //Get Driver Information.
            usbPath = dirname(usbPath);//remove idProduct from the end
            common_String_Concat(usbPath, PATH_MAX, "/driver");
            char *driverPath = C_CAST(char *, calloc(OPENSEA_PATH_MAX, sizeof(char)));
            ssize_t len = readlink(usbPath, driverPath, OPENSEA_PATH_MAX);
            if (len != -1)
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            safe_Free(C_CAST(void**, &driverPath));
            safe_Free(C_CAST(void**, &usbPath));
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_USB;
        }
    }
    return;
}

static void get_SYS_FS_1394_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo *sysFsInfo)
{
#if defined (_DEBUG)
    printf("FireWire interface!\n");
#endif
    sysFsInfo->interface_type = IEEE_1394_INTERFACE;
    sysFsInfo->drive_type = SCSI_DRIVE;//changed later if detected as ATA
    char fullFWPath[PATH_MAX] = { 0 };
    snprintf(fullFWPath, PATH_MAX, "%s", inHandleLink);

    fullFWPath[0] = '/';
    fullFWPath[1] = 's';
    fullFWPath[2] = 'y';
    fullFWPath[3] = 's';
    fullFWPath[4] = '/';
    memmove(&fullFWPath[5], &fullFWPath[6], strlen(fullFWPath));
    snprintf(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullFWPath);
    //now we need to go up a few directories to get the modalias file to parse
    intptr_t newStrLen = strstr(fullFWPath, "/host") - fullFWPath;
    if (newStrLen > 0)
    {
        char *fwPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
        if (fwPath)
        {
            snprintf(fwPath, PATH_MAX, "%.*s/modalias", C_CAST(int, newStrLen), fullFWPath);
            //printf("full FW Path = %s\n", dirname(fwPath));
            //printf("modalias FW Path = %s\n", fwPath);
            FILE *temp = NULL;
            temp = fopen(fwPath, "r");
            if (temp)
            {
                //This file contains everything in one place. Otherwise we would need to parse multiple files at slightly different paths to get everything - TJE
                if (4 == fscanf(temp, "ieee1394:ven%8" SCNx32 "mo%8" SCNx32 "sp%8" SCNx32 "ver%8" SCNx32, &sysFsInfo->adapter_info.vendorID, &sysFsInfo->adapter_info.productID, &sysFsInfo->adapter_info.specifierID, &sysFsInfo->adapter_info.revision))
                {
                    sysFsInfo->adapter_info.vendorIDValid = true;
                    sysFsInfo->adapter_info.productIDValid = true;
                    sysFsInfo->adapter_info.specifierIDValid = true;
                    sysFsInfo->adapter_info.revisionValid = true;
                    //printf("Got vendor ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.vendorID);
                    //printf("Got product ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.productID);
                    //printf("Got specifier ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.specifierID);
                    //printf("Got revision ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.revision);
                }
                fclose(temp);
                temp = NULL;
            }
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_IEEE1394;
            //Get Driver Information.
            fwPath = dirname(fwPath);//remove idProduct from the end
            common_String_Concat(fwPath, PATH_MAX, "/driver");
            char *driverPath = C_CAST(char *, calloc(OPENSEA_PATH_MAX, sizeof(char)));
            ssize_t len = readlink(fwPath, driverPath, OPENSEA_PATH_MAX);
            if (len != -1)
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            safe_Free(C_CAST(void**, &driverPath));
            safe_Free(C_CAST(void**, &fwPath));
        }
    }
    return;
}

static void get_SYS_FS_SCSI_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo *sysFsInfo)
{
#if defined (_DEBUG)
    printf("SCSI interface!\n");
#endif
    sysFsInfo->interface_type = SCSI_INTERFACE;
    sysFsInfo->drive_type = SCSI_DRIVE;//changed later if detected as ATA or NVMe or anything else
    //get vendor and product IDs of the controller attached to this device.

    char fullPciPath[PATH_MAX] = { 0 };
    snprintf(fullPciPath, PATH_MAX, "%s", inHandleLink);

    fullPciPath[0] = '/';
    fullPciPath[1] = 's';
    fullPciPath[2] = 'y';
    fullPciPath[3] = 's';
    fullPciPath[4] = '/';
    memmove(&fullPciPath[5], &fullPciPath[6], strlen(fullPciPath));
    snprintf(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullPciPath);
    //need to trim the path down now since it can vary by controller:
    //adaptec: /sys/devices/pci0000:00/0000:00:02.0/0000:02:00.0/host0/target0:1:0/0:1:0:0/scsi_generic/sg2
    //lsi: /sys/devices/pci0000:00/0000:00:02.0/0000:02:00.0/host0/port-0:16/end_device-0:16/target0:0:16/0:0:16:0/scsi_generic/sg4
    //The best way seems to break by the word "host" at this time.
    //printf("Full pci path: %s\n", fullPciPath);
    //printf("/host location string: %s\n", strstr(fullPciPath, "/host"));
    //printf("FULL: %" PRIXPTR "\t/HOST: %" PRIXPTR "\n", C_CAST(uintptr_t, fullPciPath), C_CAST(uintptr_t, strstr(fullPciPath, "/host")));
    intptr_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath;
    if (newStrLen > 0)
    {
        char *pciPath = C_CAST(char*, calloc(PATH_MAX, sizeof(char)));
        if (pciPath)
        {
            snprintf(pciPath, PATH_MAX, "%.*s/vendor", C_CAST(int, newStrLen), fullPciPath);
            //printf("Shortened PCI Path: %s\n", dirname(pciPath));
            FILE *temp = NULL;
            temp = fopen(pciPath, "r");
            if (temp)
            {
                if(1 == fscanf(temp, "0x%" SCNx32, &sysFsInfo->adapter_info.vendorID))
                {
                    sysFsInfo->adapter_info.vendorIDValid = true;
                    //printf("Got vendor as %" PRIX16 "h\n", sysFsInfo->adapter_info.vendorID);
                }
                fclose(temp);
                temp = NULL;
            }
            pciPath = dirname(pciPath);//remove vendor from the end
            common_String_Concat(pciPath, PATH_MAX, "/device");
            temp = fopen(pciPath, "r");
            if (temp)
            {
                if (1 == fscanf(temp, "0x%" SCNx32, &sysFsInfo->adapter_info.productID))
                {
                    sysFsInfo->adapter_info.productIDValid = true;
                    //printf("Got product as %" PRIX16 "h\n", sysFsInfo->adapter_info.productID);
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
                    sysFsInfo->adapter_info.revision = pciRev;
                    sysFsInfo->adapter_info.revisionValid = true;
                    //printf("Got revision as %" PRIX16 "h\n", sysFsInfo->adapter_info.revision);
                }
                fclose(temp);
                temp = NULL;
            }
            //Store Driver Information
            pciPath = dirname(pciPath);
            common_String_Concat(pciPath, PATH_MAX, "/driver");
            char *driverPath = C_CAST(char *, calloc(OPENSEA_PATH_MAX, sizeof(char)));
            if (-1 != readlink(pciPath, driverPath, OPENSEA_PATH_MAX))
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            //printf("\nPath: %s\tname: %s", sysFsInfo->driver_info.driverPath,
            safe_Free(C_CAST(void**, &driverPath));
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_PCI;
            safe_Free(C_CAST(void**, &pciPath));
        }
    }
    return;
}

static void get_SYS_FS_SCSI_Address(const char* inHandleLink, sysFSLowLevelDeviceInfo * sysFsInfo)
{
    //printf("getting SCSI address\n");
    //set the scsi address field
    char *handle = strdup(inHandleLink);
    char *scsiAddress = basename(dirname(dirname(handle)));//SCSI address should be 2nd from the end of the link
    if (scsiAddress)
    {
       char *saveptr = NULL;
       rsize_t addrlen = strlen(scsiAddress);
       char *token = common_String_Token(scsiAddress, &addrlen, ":", &saveptr);
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
    }
    safe_Free(C_CAST(void**, &handle));
    return;
}

//read type or inquiry files, queue depth.
//TODO: read other files in this structure that could be useful??? mn, vendor, revision, sas_address
//NOTE: not all files will exist for all devices/types
//NOTE: SAS, SCSI, SATA, USB will all use this since they all are treated as SCSI devices by the OS.
//      NVMe will need a different set of instructions/things to do.
//NOTE: This counts on "full device path" being set in sysFsInfo already (which it should be)
static void get_Linux_SYS_FS_SCSI_Device_File_Info(sysFSLowLevelDeviceInfo * sysFsInfo)
{
    char fullPathBuffer[PATH_MAX] = { 0 };
    char *fullPath = &fullPathBuffer[0];
    snprintf(fullPath, PATH_MAX, "%s", sysFsInfo->fullDevicePath);
    common_String_Concat(fullPath, PATH_MAX, "/device/type");
    FILE *temp = fopen(fullPath, "r");
    if (temp)
    {
        uint8_t scsiDevType = 0;
        if (1 == fscanf(temp, "%" SCNu8, &scsiDevType))
        {
            sysFsInfo->scsiDevType = scsiDevType;
        }
        fclose(temp);
        temp = NULL;
    }
    else
    {
        //could not open the type file, so try the inquiry file and read the first byte as raw binary since this is how this file is stored
        fullPath = dirname(fullPath);
        common_String_Concat(fullPath, PATH_MAX, "/inquiry");
        temp = fopen(fullPath, "rb");
        if (temp)
        {
            uint8_t peripheralType = 0;
            if (1 == fread(&peripheralType, sizeof(uint8_t), 1, temp))
            {
                sysFsInfo->scsiDevType = M_GETBITRANGE(peripheralType, 4, 0);
            }
            fclose(temp);
            temp = NULL;
        }
    }
    fullPath = dirname(fullPath);
    common_String_Concat(fullPath, PATH_MAX, "/queue_depth");
    temp = fopen(fullPath, "r");
    if (temp)
    {
        if (1 != fscanf(temp, "%" SCNu16, &sysFsInfo->queueDepth))
        {
            sysFsInfo->queueDepth = 0;
        }
        fclose(temp);
        temp = NULL;
    }
}

//while similar to the function below, this is used only by get_Device to set up some fields in the device structure for the above layers
//this function gets the following info:
// pcie/usb product ID, vendor ID, revision ID, sets the interface type, ieee1394 specifier ID, and sets the handle mapping for SD/BSG
//this also calls the function to get the driver version info as well as the name of the driver as a string.
//TODO: Also output the full device path from the read link???
//      get the SCSI peripheral device type to help decide when to scan for RAIDs on a given handle
//handle nvme-generic handles???
//handle looking up nvme controller handle from a namespace handle???
//handle /dev/disk/by-<> lookups. These are links to /dev/sd or /dev/nvme, etc. We can convert these first, then convert again to sd/sg/nvme as needed
static void get_Linux_SYS_FS_Info(const char* handle, sysFSLowLevelDeviceInfo * sysFsInfo)
{
    //check if it's a block handle, bsg, or scsi_generic handle, then setup the path we need to read.
    if (handle && sysFsInfo)
    {
        if (strstr(handle, "nvme") != NULL)
        {
            size_t nvmHandleLen = strlen(handle) + 1;
            char *nvmHandle = C_CAST(char*, calloc(nvmHandleLen, sizeof(char)));
            snprintf(nvmHandle, nvmHandleLen, "%s", handle);
            sysFsInfo->interface_type = NVME_INTERFACE;
            sysFsInfo->drive_type = NVME_DRIVE;
            snprintf(sysFsInfo->primaryHandleStr, OS_HANDLE_NAME_MAX_LENGTH, "%s", nvmHandle);
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
                printf("SCSI interface, unknown drive type\n");
                sysFsInfo->interface_type = SCSI_INTERFACE;
                sysFsInfo->drive_type = UNKNOWN_DRIVE;
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
                            get_SYS_FS_ATA_Info(inHandleLink, sysFsInfo);
                        }
                        else if (strstr(inHandleLink, "usb") != 0)
                        {
                            get_SYS_FS_USB_Info(inHandleLink, sysFsInfo);
                        }
                        else if (strstr(inHandleLink, "fw") != 0)
                        {
                            get_SYS_FS_1394_Info(inHandleLink, sysFsInfo);
                        }
                        //if the link doesn't conatin ata or usb in it, then we are assuming it's scsi since scsi doesn't have a nice simple string to check
                        else
                        {
                            get_SYS_FS_SCSI_Info(inHandleLink, sysFsInfo);
                        }
                        get_Linux_SYS_FS_SCSI_Device_File_Info(sysFsInfo);

                        char *baseLink = basename(inHandleLink);
                        if (bsg)
                        {
                            snprintf(sysFsInfo->primaryHandleStr, OS_HANDLE_NAME_MAX_LENGTH, "/dev/bsg/%s", baseLink);
                        }
                        else
                        {
                            snprintf(sysFsInfo->primaryHandleStr, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s", baseLink);
                        }

                        get_SYS_FS_SCSI_Address(inHandleLink, sysFsInfo);
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
                                    snprintf(sysFsInfo->secondaryHandleStr, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/bsg/%s", gen);
                                }
                                else
                                {
                                    snprintf(sysFsInfo->secondaryHandleStr, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/%s", gen);
                                }
                            }
                            else
                            {
                                //generic handle was sent in
                                //secondary handle will be a block handle
                                snprintf(sysFsInfo->secondaryHandleStr, OS_SECOND_HANDLE_NAME_LENGTH, "/dev/%s", block);
                            }

                            if (strstr(block, "sr") || strstr(block, "scd"))
                            {
                                sysFsInfo->drive_type = ATAPI_DRIVE;
                            }
                            else if (strstr(block, "st"))
                            {
                                sysFsInfo->drive_type = LEGACY_TAPE_DRIVE;
                            }
                            else if (strstr(block, "ses"))
                            {
                                //scsi enclosure services
                            }
                        }
                        //printf("Finish handle mapping\n");
                        safe_Free(C_CAST(void**, &block));
                        safe_Free(C_CAST(void**, &gen));
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

static void set_Device_Fields_From_Handle(const char* handle, tDevice *device)
{
    sysFSLowLevelDeviceInfo sysFsInfo;
    memset(&sysFsInfo, 0, sizeof(sysFSLowLevelDeviceInfo));
    get_Linux_SYS_FS_Info(handle, &sysFsInfo);
    //now copy the saved data to tDevice. -TJE
    if (device)
    {
        device->drive_info.drive_type = sysFsInfo.drive_type;
        device->drive_info.interface_type = sysFsInfo.interface_type;
        memcpy(&device->drive_info.adapter_info, &sysFsInfo.adapter_info, sizeof(adapterInfo));
        memcpy(&device->drive_info.driver_info, &sysFsInfo.driver_info, sizeof(driverInfo));
        if (strlen(sysFsInfo.primaryHandleStr) > 0)
        {
            snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", sysFsInfo.primaryHandleStr);
            snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", basename(sysFsInfo.primaryHandleStr));
        }
        if (strlen(sysFsInfo.secondaryHandleStr) > 0)
        {
            snprintf(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "%s", sysFsInfo.secondaryHandleStr);
            snprintf(device->os_info.secondFriendlyName, OS_SECOND_HANDLE_NAME_LENGTH, "%s", basename(sysFsInfo.secondaryHandleStr));
        }
    }
    return;
}

//map a block handle (sd) to a generic handle (sg or bsg)
//incoming handle can be either sd, sg, or bsg type
//This depends on mapping in the file system provided by 2.6 and later.
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
                        safe_Free(C_CAST(void**, &incomingClassName));
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
                        safe_Free(C_CAST(void**, &incomingClassName));
                        return NOT_SUPPORTED;
                    }
                }
                //now we need to loop through each think in the class folder, read the link, and check if we match.
                struct dirent **classList;
                int remains = 0;
                int numberOfItems = scandir(classPath, &classList, NULL /*not filtering anything. Just go through each item*/, alphasort);
                for (int iter = 0; iter < numberOfItems; ++iter)
                {
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
                                if (NULL != classPtr && strncmp(mapLink, inHandleLink, C_CAST(size_t, (classPtr - mapLink))) == 0)
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
                                    safe_Free(C_CAST(void**, &className));
                                    safe_Free(C_CAST(void**, &incomingClassName));
                                    // start PRH valgrind fixes
                                    // this is causing a mem leak... when we bail the loop, there are a string of classList[] items 
                                    // still allocated. 
                                    for(remains = iter; remains<numberOfItems; remains++)
                                    {
                                        safe_Free(C_CAST(void**, &classList[remains]));
                                    }
                                    safe_Free(C_CAST(void**, &classList));
                                    safe_Free(C_CAST(void**, &temp));
                                    // end PRH valgrind fixes.
                                    return SUCCESS;
                                    break;//found a match, exit the loop
                                }
                            }
                            safe_Free(C_CAST(void**, &className));
                        }
                    }
                    safe_Free(C_CAST(void**, &classList[iter])); // PRH - valgrind
                    safe_Free(C_CAST(void**, &temp));
                }
                safe_Free(C_CAST(void**, &classList));
            }
            else
            {
                //not a link, or some other error....probably an old kernel
                safe_Free(C_CAST(void**, &incomingClassName));
                return NOT_SUPPORTED;
            }
        }
        else
        {
            //Mapping is not supported...probably an old kernel
            safe_Free(C_CAST(void**, &incomingClassName));
            return NOT_SUPPORTED;
        }
        safe_Free(C_CAST(void**, &incomingClassName));
    }
    return UNKNOWN;
}

static eReturnValues set_Device_Partition_Info(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    int partitionCount = 0;
    char* blockHandle = device->os_info.name;
    if (device->os_info.secondHandleValid && !is_Block_Device_Handle(blockHandle))
    {
        blockHandle = device->os_info.secondName;
    }
    partitionCount = get_Partition_Count(blockHandle);
#if defined (_DEBUG)
    printf("Partition count for %s = %d\n", blockHandle, partitionCount);
#endif
    if (partitionCount > 0)
    {
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;
        device->os_info.fileSystemInfo.hasActiveFileSystem = false;
        device->os_info.fileSystemInfo.isSystemDisk = false;
        ptrsPartitionInfo parts = C_CAST(ptrsPartitionInfo, calloc(C_CAST(size_t, partitionCount), sizeof(spartitionInfo)));
        if (parts)
        {
            if (SUCCESS == get_Partition_List(blockHandle, parts, partitionCount))
            {
                int iter = 0;
                for (; iter < partitionCount; ++iter)
                {
                    //since we found a partition, set the "has file system" bool to true
                    device->os_info.fileSystemInfo.hasActiveFileSystem = true;
#if defined (_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    //check if one of the partitions is /boot and mark the system disk when this is found
                    //TODO: Should / be treated as a system disk too?
                    if (strncmp((parts + iter)->mntPath, "/boot", 5) == 0)
                    {
                        device->os_info.fileSystemInfo.isSystemDisk = true;
#if defined (_DEBUG)
                        printf("found system disk\n");
#endif
                    }
                }
            }
            safe_Free(C_CAST(void**, &parts));
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
        device->os_info.fileSystemInfo.isSystemDisk = false;
    }
    return ret;
}

#define LIN_MAX_HANDLE_LENGTH 16
static eReturnValues get_Lin_Device(const char *filename, tDevice *device)
{
    char *deviceHandle = NULL;
    eReturnValues ret = SUCCESS;
    int k = 0;
#if defined (_DEBUG)
    printf("%s: Getting device for %s\n", __FUNCTION__, filename);
#endif

    if (is_Block_Device_Handle(filename))
    {
        //printf("\tBlock handle found, mapping...\n");
        char *genHandle = NULL;
        char *blockHandle = NULL;
        eReturnValues mapResult = map_Block_To_Generic_Handle(filename, &genHandle, &blockHandle);
#if defined (_DEBUG)
        printf("sg = %s\tsd = %s\n", genHandle, blockHandle);
#endif
        if (mapResult == SUCCESS && genHandle != NULL)
        {
            deviceHandle = C_CAST(char*, calloc(LIN_MAX_HANDLE_LENGTH, sizeof(char)));
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
        safe_Free(C_CAST(void**, &genHandle));
        safe_Free(C_CAST(void**, &blockHandle));
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
            safe_Free(C_CAST(void**, &deviceHandle));
            return PERMISSION_DENIED;
        }
        else
        {
            safe_Free(C_CAST(void**, &deviceHandle));
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
        set_Device_Partition_Info(device);
        safe_Free(C_CAST(void**, &deviceHandle));
        return ret;
    }
    //Add support for other flags. 
    if ((device->os_info.fd >= 0) && (ret == SUCCESS))
    {
        if (is_NVMe_Handle(deviceHandle))
        {
#if !defined(DISABLE_NVME_PASSTHROUGH)
            //Do NVMe specific setup and enumeration
            device->drive_info.drive_type = NVME_DRIVE;
            device->drive_info.interface_type = NVME_INTERFACE;
            int ioctlResult = ioctl(device->os_info.fd, NVME_IOCTL_ID);
            if (ioctlResult < 0)
            {
                 perror("nvme_ioctl_id");
                 return FAILURE;
            }
            device->drive_info.namespaceID = C_CAST(uint32_t, ioctlResult);
            device->os_info.osType = OS_LINUX;
            device->drive_info.media_type = MEDIA_NVM;

            char *baseLink = basename(deviceHandle);
            //Now we will set up the device name, etc fields in the os_info structure.
            snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s", baseLink);
            snprintf(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", baseLink);

            set_Device_Partition_Info(device);

            ret = fill_Drive_Info_Data(device);
#if defined (_DEBUG)
            printf("\nsg helper-nvmedev\n");
            printf("Drive type: %d\n", device->drive_info.drive_type);
            printf("Interface type: %d\n", device->drive_info.interface_type);
            printf("Media type: %d\n", device->drive_info.media_type);
#endif //DEBUG
#else //DISABLE_NVME_PASSTHROUGH
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
            errno = 0;//clear before calling this ioctl
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
            // Check we have a valid device by trying an ioctl
            // From http://tldp.org/HOWTO/SCSI-Generic-HOWTO/pexample.html
            if ((ioctl(device->os_info.fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000))
            {
                printf("%s: SG_GET_VERSION_NUM on %s failed version=%d\n", __FUNCTION__, filename, k);
                perror("SG_GET_VERSION_NUM");
                close(device->os_info.fd);
            }
            else
            {
                //http://www.faqs.org/docs/Linux-HOWTO/SCSI-Generic-HOWTO.html#IDDRIVER
                device->os_info.sgDriverVersion.driverVersionValid = true;
                device->os_info.sgDriverVersion.majorVersion = C_CAST(uint8_t, k / 10000);
                device->os_info.sgDriverVersion.minorVersion = C_CAST(uint8_t, (k - (device->os_info.sgDriverVersion.majorVersion * 10000)) / 100);
                device->os_info.sgDriverVersion.revision = C_CAST(uint8_t, k - (device->os_info.sgDriverVersion.majorVersion * 10000) - (device->os_info.sgDriverVersion.minorVersion * 100));

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
                set_Device_Partition_Info(device);

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
//                  set_ATA_Passthrough_Type_By_PID_and_VID(device);
//              }

                ret = fill_Drive_Info_Data(device);

#if defined (_DEBUG)
                printf("\nsg helper\n");
                printf("Drive type: %d\n", device->drive_info.drive_type);
                printf("Interface type: %d\n", device->drive_info.interface_type);
                printf("Media type: %d\n", device->drive_info.media_type);
#endif
            }
        }
    }
    safe_Free(C_CAST(void**, &deviceHandle));
    return ret;
}

eReturnValues get_Device(const char *filename, tDevice *device)
{
#if defined (ENABLE_CISS)
    if (is_Supported_ciss_Dev(filename))
    {
        return get_CISS_RAID_Device(filename, device);
    }
#endif //ENABLE_CISS
    return get_Lin_Device(filename, device);
}

//http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_reset.html
//sgResetType should be one of the values from the link above...so bus or device...controller will work but that shouldn't be done ever.
static eReturnValues sg_reset(int fd, int resetType)
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
            ioctlResult = ioctl(fd, SG_SCSI_RESET, &resetType);
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

eReturnValues send_IO( ScsiIoCtx *scsiIoCtx )
{
    eReturnValues ret = FAILURE;    
#ifdef _DEBUG
    printf("-->%s \n", __FUNCTION__);
#endif
    switch (scsiIoCtx->device->drive_info.interface_type)
    {
    case NVME_INTERFACE:
        return sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
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

eReturnValues send_sg_io( ScsiIoCtx *scsiIoCtx )
{
    sg_io_hdr_t io_hdr;
    uint8_t     *localSenseBuffer = NULL;
    eReturnValues         ret          = SUCCESS;
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
        if (scsiIoCtx->senseDataSize > UINT8_MAX)
        {
            io_hdr.mx_sb_len = UINT8_MAX;
        }
        else
        {
            io_hdr.mx_sb_len = C_CAST(uint8_t, scsiIoCtx->senseDataSize);
        }
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
    #if defined (SG_DXFER_UNKNOWN)
        io_hdr.dxfer_direction = SG_DXFER_UNKNOWN;//using unknown because SG_DXFER_TO_FROM_DEV is described as something different to use with indirect IO as it copied into kernel buffers before transfer.
    #else
        io_hdr.dxfer_direction = -5;//this is what this is defined as in sg.h
    #endif //SG_DXFER_UNKNOWN
        break;
    default:
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("%s Didn't understand direction\n", __FUNCTION__);
        }
        safe_Free_aligned(C_CAST(void**, &localSenseBuffer));
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
    if (ioctlResult < 0)
    {
        scsiIoCtx->device->os_info.last_error = C_CAST(unsigned int, errno);
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(errno);
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
#if defined (TASK_ABORTED)
                case TASK_ABORTED:
#else
                case 0x20:
#endif
                    printf(" - Task Aborted\n");
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
                //Special case for MegaRAID and controllers based on MegaRAID.
                //These controllers block the command and set "Internal Adapter Error" with no other information available.
                //TODO: Need to test and see if SAT passthrough trusted send/receive are also blocked to add them to this case. -TJE
                if (io_hdr.host_status == OPENSEA_SG_ERR_DID_ERROR && (scsiIoCtx->cdb[OPERATION_CODE] == SECURITY_PROTOCOL_IN || scsiIoCtx->cdb[OPERATION_CODE] == SECURITY_PROTOCOL_OUT))
                {
                    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                    {
                        printf("\tSpecial Case: Security Protocol Command Blocked\n");
                    }
                    ret = OS_COMMAND_BLOCKED;
                }
                else
                {
                    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                    {
                        printf("\t(Host Status) Sense data not available, assuming OS_PASSTHROUGH_FAILURE\n");
                    }
                    ret = OS_PASSTHROUGH_FAILURE;
                }
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
    safe_Free_aligned(C_CAST(void**, &localSenseBuffer));
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
    uint32_t num_devs = 0, num_nvme_devs = 0;
    int scandirresult = 0;
    struct dirent **namelist;
    struct dirent **nvmenamelist;
    int(*sortFunc)(const struct dirent **, const struct dirent **) = &alphasort;
#if defined (_GNU_SOURCE)
    sortFunc = &versionsort;//use versionsort instead when available with _GNU_SOURCE
#endif

    scandirresult = scandir("/dev", &namelist, sg_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_devs = C_CAST(uint32_t, scandirresult);
    }
    if (num_devs == 0)
    {
        //check for SD devices
        scandirresult = scandir("/dev", &namelist, sd_filter, sortFunc); 
        if (scandirresult >= 0)
        {
            num_devs = C_CAST(uint32_t, scandirresult);
        }
    }
#if defined (ENABLE_CISS)
    //build a list of devices to scan for physical drives behind a RAID
    ptrRaidHandleToScan raidHandleList = NULL;
    ptrRaidHandleToScan beginRaidHandleList = raidHandleList;
    raidTypeHint raidHint;
    memset(&raidHint, 0, sizeof(raidTypeHint));
    //need to check if existing sg/sd handles are attached to hpsa or smartpqi drives in addition to /dev/cciss devices
    struct dirent **ccisslist;
    uint32_t num_ccissdevs = 0;
    scandirresult = scandir("/dev", &ccisslist, ciss_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_ccissdevs = C_CAST(uint32_t, scandirresult);
    }
    if (num_ccissdevs > 0)
    {
        raidHint.cissRAID = true;//true as all the following will be CISS devices
        for (uint32_t cissIter = 0; cissIter < num_ccissdevs; ++cissIter)
        {
            raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, ccisslist[cissIter]->d_name, raidHint);
            if (!beginRaidHandleList)
            {
                beginRaidHandleList = raidHandleList;
            }
            //now free this as we are done with it.
            safe_Free(C_CAST(void**, &ccisslist[cissIter]));
        }
        safe_Free(C_CAST(void**, &ccisslist));
    }
    for (uint32_t iter = 0; iter < num_devs; ++iter)
    {
        //before freeing, check if any of these handles may be a RAID handle
        sysFSLowLevelDeviceInfo sysFsInfo;
        memset(&sysFsInfo, 0, sizeof(sysFSLowLevelDeviceInfo));
        get_Linux_SYS_FS_Info(namelist[iter]->d_name, &sysFsInfo);

        memset(&raidHint, 0, sizeof(raidTypeHint));//clear out before checking driver name since this will be expanded to check other drivers in the future
#if defined (ENABLE_CISS)
        if (sysFsInfo.scsiDevType == PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE)
        {
            if (strcmp(sysFsInfo.driver_info.driverName, "hpsa") == 0)
            {
                raidHint.cissRAID = true;
                //this handle is a /dev/sg handle with the hpsa driver, so we can scan for cciss devices
                raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, namelist[iter]->d_name, raidHint);
                if (!beginRaidHandleList)
                {
                    beginRaidHandleList = raidHandleList;
                }
            }
            else if (strcmp(sysFsInfo.driver_info.driverName, "smartpqi") == 0)
            {
                raidHint.cissRAID = true;
                //this handle is a /dev/sg handle with the smartpqi driver, so we can scan for cciss devices
                raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, namelist[iter]->d_name, raidHint);
                if (!beginRaidHandleList)
                {
                    beginRaidHandleList = raidHandleList;
                }
            }
        }
#endif //ENABLE_CISS
    }
#endif //ENABLE_CISS

    //free the list of names to not leak memory
    for (uint32_t iter = 0; iter < num_devs; ++iter)
    {
        safe_Free(C_CAST(void**, &namelist[iter]));
    }
    safe_Free(C_CAST(void**, &namelist));
    //add nvme devices to the list
    scandirresult = scandir("/dev", &nvmenamelist, nvme_filter,sortFunc);
    if (scandirresult >= 0)
    {
        num_nvme_devs = C_CAST(uint32_t, scandirresult);
    }
    //free the nvmenamelist to not leak memory
    for (uint32_t iter = 0; iter < num_nvme_devs; ++iter)
    {
        safe_Free(C_CAST(void**, &nvmenamelist[iter]));
    }
    safe_Free(C_CAST(void**, &nvmenamelist));

    *numberOfDevices = num_devs + num_nvme_devs;

#if defined (ENABLE_CISS)
    uint32_t cissDeviceCount = 0;
    eReturnValues cissRet = get_CISS_RAID_Device_Count(&cissDeviceCount, flags, &beginRaidHandleList);
    if (cissRet == SUCCESS)
    {
        *numberOfDevices += cissDeviceCount;
    }
#endif //ENABLE_CISS

    //Clean up RAID handle list
    delete_RAID_List(beginRaidHandleList);

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
eReturnValues get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    eReturnValues returnValue = SUCCESS;
    uint32_t numberOfDevices = 0;
    uint32_t driveNumber = 0, found = 0, failedGetDeviceCount = 0, permissionDeniedCount = 0;
    char name[80] = { 0 }; //Because get device needs char
    int fd;
    tDevice * d = NULL;
#if defined (DEGUG_SCAN_TIME)
    seatimer_t getDeviceTimer;
    seatimer_t getDeviceListTimer;
    memset(&getDeviceTimer, 0, sizeof(seatimer_t));
    memset(&getDeviceListTimer, 0, sizeof(seatimer_t));
#endif
    ptrRaidHandleToScan raidHandleList = NULL;
    ptrRaidHandleToScan beginRaidHandleList = raidHandleList;
    raidTypeHint raidHint;
    memset(&raidHint, 0, sizeof(raidTypeHint));
    
    int scandirresult = 0;
    uint32_t num_sg_devs = 0, num_sd_devs = 0, num_nvme_devs = 0;

    struct dirent **namelist;
    struct dirent **nvmenamelist;

    int(*sortFunc)(const struct dirent **, const struct dirent **) = &alphasort;
#if defined (_GNU_SOURCE)
    sortFunc = &versionsort;//use versionsort instead when available with _GNU_SOURCE
#endif

    scandirresult = scandir("/dev", &namelist, sg_filter, sortFunc); 
    if (scandirresult >= 0)
    {
        num_sg_devs = C_CAST(uint32_t, scandirresult);
    }
    if (num_sg_devs == 0)
    {
        //check for SD devices
        scandirresult = scandir("/dev", &namelist, sd_filter, sortFunc); 
        if (scandirresult >= 0)
        {
            num_sd_devs = C_CAST(uint32_t, scandirresult);
        }
    }
    //add nvme devices to the list
    scandirresult = scandir("/dev", &nvmenamelist, nvme_filter,sortFunc);
    if (scandirresult >= 0)
    {
        num_nvme_devs = C_CAST(uint32_t, scandirresult);
    }
    uint32_t totalDevs = num_sg_devs + num_sd_devs + num_nvme_devs;
    
    char **devs = C_CAST(char **, calloc(totalDevs + 1, sizeof(char *)));
    uint32_t i = 0;
    uint32_t j = 0;
    //add sg/sd devices to the list
    for (; i < (num_sg_devs + num_sd_devs); i++)
    {
        size_t handleSize = (strlen("/dev/") + strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i] = C_CAST(char *, malloc(handleSize));
        snprintf(devs[i], handleSize, "/dev/%s", namelist[i]->d_name);
        safe_Free(C_CAST(void**, &namelist[i]));
    }
    //add nvme devices to the list
    for (j = 0; i < totalDevs && j < num_nvme_devs; i++, j++)
    {
        size_t handleSize = (strlen("/dev/") + strlen(nvmenamelist[j]->d_name) + 1) * sizeof(char);
        devs[i] = C_CAST(char *, malloc(handleSize));
        snprintf(devs[i], handleSize, "/dev/%s", nvmenamelist[j]->d_name);
        safe_Free(C_CAST(void**, &nvmenamelist[j]));
    }
    devs[i] = NULL; //Added this so the for loop down doesn't cause a segmentation fault.
    safe_Free(C_CAST(void**, &namelist));
    safe_Free(C_CAST(void**, &nvmenamelist));

    struct dirent **ccisslist;
    int num_ccissdevs = scandir("/dev", &ccisslist, ciss_filter, sortFunc);
    if (num_ccissdevs > 0)
    {
        raidHint.cissRAID = true;//true as all the following will be CISS devices
        for (int cissIter = 0; cissIter < num_ccissdevs; ++cissIter)
        {
            raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, ccisslist[cissIter]->d_name, raidHint);
            if (!beginRaidHandleList)
            {
                beginRaidHandleList = raidHandleList;
            }
            //now free this as we are done with it.
            safe_Free(C_CAST(void**, &ccisslist[cissIter]));
        }
        safe_Free(C_CAST(void**, &ccisslist));
    }

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
        for (driveNumber = 0; (driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < totalDevs) && (found < numberOfDevices); ++driveNumber)
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
                d->dFlags =  flags;
                eReturnValues ret = get_Device(name, d);
#if defined (DEGUG_SCAN_TIME)
                stop_Timer(&getDeviceTimer);
                printf("Time to get %s = %fms\n", name, get_Milli_Seconds(getDeviceTimer));
#endif
                if (ret != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                else
                {
                    memset(&raidHint, 0, sizeof(raidTypeHint));
#if defined (ENABLE_CISS)
                    //check that we are only scanning a SCSI controller for RAID to avoid duplicates
                    //NOTE: If num_sg_devs == 0, then the sg driver is missing and SCSI controllers do not get /dev/sd handles, so we will skip this check in this special case.
                    //      This special case exists because sometimes a kernel is built and deployed without the SG driver enabled, but we still want to detect RAID devices, so
                    //      we don't want to skip enumerating a RAID when all we see are the logical RAID volumes -TJE
                    if (M_GETBITRANGE(d->drive_info.scsiVpdData.inquiryData[0], 4, 0) == PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE || num_sg_devs == 0)
                    {
                        if (strcmp(d->drive_info.driver_info.driverName, "hpsa") == 0)
                        {
                            raidHint.cissRAID = true;
                            //this handle is a /dev/sg handle with the hpsa driver, so we can scan for cciss devices
                            raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, name, raidHint);
                            if (!beginRaidHandleList)
                            {
                                beginRaidHandleList = raidHandleList;
                            }
                        }
                        else if (strcmp(d->drive_info.driver_info.driverName, "smartpqi") == 0)
                        {
                            raidHint.cissRAID = true;
                            //this handle is a /dev/sg handle with the smartpqi driver, so we can scan for cciss devices
                            raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, name, raidHint);
                            if (!beginRaidHandleList)
                            {
                                beginRaidHandleList = raidHandleList;
                            }
                        }
                    }
#endif //ENABLE_CISS
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
            safe_Free(C_CAST(void**, &devs[driveNumber]));
        }

#if defined (ENABLE_CISS)
        uint32_t cissDeviceCount = numberOfDevices - found;
        if (cissDeviceCount > 0)
        {
            eReturnValues cissRet = get_CISS_RAID_Device_List(&ptrToDeviceList[found], cissDeviceCount * sizeof(tDevice), ver, flags, &beginRaidHandleList);
            if (returnValue == SUCCESS && cissRet != SUCCESS)
            {
                //this will override the normal ret if it is already set to success with the CISS return value
                returnValue = cissRet;
            }
        }
#endif //ENABLE_CISS

        //Clean up RAID handle list
        delete_RAID_List(beginRaidHandleList);

#if defined (DEGUG_SCAN_TIME)
        stop_Timer(&getDeviceListTimer);
        printf("Time to get all device = %fms\n", get_Milli_Seconds(getDeviceListTimer));
#endif

	    if (found == failedGetDeviceCount)
	    {
	        returnValue = FAILURE;
	    }
        else if(permissionDeniedCount == totalDevs)
        {
            returnValue = PERMISSION_DENIED;
        }
        else if (failedGetDeviceCount && returnValue != PERMISSION_DENIED)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    safe_Free(C_CAST(void**, &devs));
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
    if (dev)
    {
        if (dev->os_info.cissDeviceData)
        {
            close_CISS_RAID_Device(dev);
        }
        else
        {
            retValue = close(dev->os_info.fd);
            dev->os_info.last_error = C_CAST(unsigned int, errno);

            if (dev->os_info.secondHandleValid && dev->os_info.secondHandleOpened)
            {
                if (close(dev->os_info.fd2) == 0)
                {
                    dev->os_info.fd2 = -1;
                }
            }
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
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = SUCCESS;//NVME_SC_SUCCESS;//This defined value used to exist in some version of nvme.h but is missing in nvme_ioctl.h...it was a value of zero, so this should be ok.
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    struct nvme_admin_cmd adminCmd;
    struct nvme_user_io nvmCmd;// it's possible that this is not defined in some funky early nvme kernel, but we don't see that today. This seems to be defined everywhere. -TJE
    struct nvme_passthru_cmd *passThroughCmd = (struct nvme_passthru_cmd*)&adminCmd;//setting a pointer since these are defined to be the same. No point in allocating yet another structure. - TJE

    int ioctlResult = 0;

    if (!nvmeIoCtx)
    {
        return BAD_PARAMETER;
    }

    switch (nvmeIoCtx->commandType)
    {
    case NVM_ADMIN_CMD:
        memset(&adminCmd, 0, sizeof(struct nvme_admin_cmd));
        adminCmd.opcode = nvmeIoCtx->cmd.adminCmd.opcode;
        adminCmd.flags = nvmeIoCtx->cmd.adminCmd.flags;
        adminCmd.rsvd1 = nvmeIoCtx->cmd.adminCmd.rsvd1;
        adminCmd.nsid = nvmeIoCtx->cmd.adminCmd.nsid;
        adminCmd.cdw2 = nvmeIoCtx->cmd.adminCmd.cdw2;
        adminCmd.cdw3 = nvmeIoCtx->cmd.adminCmd.cdw3;
        adminCmd.metadata = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.metadata));
        adminCmd.addr = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.addr));
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
#if defined __clang__
// clang specific because behavior can differ even with the GCC diagnostic being "compatible"
// https ://clang.llvm.org/docs/UsersManual.html#controlling-diagnostics-via-pragmas
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined __GNUC__
//temporarily disable the warning for sign conversion because ioctl definition 
// in some distributions/cross compilers is defined as ioctl(int, unsigned long, ...) and 
// in others is defined as ioctl(int, int, ...)
//While debugging there does not seem to be a real conversion issue here.
//These ioctls still work in either situation, so disabling the warning seems best since there is not
//another way I have found to determine when to cast or not cast the sign conversion.-TJE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif //__clang__, __GNUC__
        ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_ADMIN_CMD, &adminCmd);
#if defined __clang__
#pragma clang diagnostic pop
#elif defined __GNUC__
//reenable the unused function warning
#pragma GCC diagnostic pop
#endif //__clang__, __GNUC__
        stop_Timer(&commandTimer);
        if (ioctlResult < 0)
        {
            nvmeIoCtx->device->os_info.last_error = C_CAST(unsigned int, errno);
            ret = OS_PASSTHROUGH_FAILURE;
            if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
            {
                if (nvmeIoCtx->device->os_info.last_error != 0)
                {
                    printf("Error: ");
                    print_Errno_To_Screen(errno);
                }
            }
        }
        else
        {
            nvmeIoCtx->commandCompletionData.commandSpecific = adminCmd.result;
            nvmeIoCtx->commandCompletionData.dw3Valid = true;
            nvmeIoCtx->commandCompletionData.dw0Valid = true;
            nvmeIoCtx->commandCompletionData.statusAndCID = C_CAST(uint32_t, ioctlResult) << 17;//shift into place since we don't get the phase tag or command ID bits and these are the status field
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
            nvmCmd.metadata = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.nvmCmd.metadata));
            nvmCmd.addr = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->ptrData));
            nvmCmd.slba = M_DWordsTo8ByteValue(nvmeIoCtx->cmd.nvmCmd.cdw11, nvmeIoCtx->cmd.nvmCmd.cdw10);
            nvmCmd.dsmgmt = nvmeIoCtx->cmd.nvmCmd.cdw13;
            nvmCmd.reftag = nvmeIoCtx->cmd.nvmCmd.cdw14;
            nvmCmd.apptag = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
            nvmCmd.appmask = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
            start_Timer(&commandTimer);
#if defined __clang__
// clang specific because behavior can differ even with the GCC diagnostic being "compatible"
// https ://clang.llvm.org/docs/UsersManual.html#controlling-diagnostics-via-pragmas
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined __GNUC__
//temporarily disable the warning for sign conversion because ioctl definition 
// in some distributions/cross compilers is defined as ioctl(int, unsigned long, ...) and 
// in others is defined as ioctl(int, int, ...)
//While debugging there does not seem to be a real conversion issue here.
//These ioctls still work in either situation, so disabling the warning seems best since there is not
//another way I have found to determine when to cast or not cast the sign conversion.-TJE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif //__clang__, __GNUC__
            ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_SUBMIT_IO, &nvmCmd);
#if defined __clang__
#pragma clang diagnostic pop
#elif defined __GNUC__
//reenable the unused function warning
#pragma GCC diagnostic pop
#endif //__clang__, __GNUC__
            stop_Timer(&commandTimer);
            if (ioctlResult < 0)
            {
                nvmeIoCtx->device->os_info.last_error = C_CAST(unsigned int, errno);
                ret = OS_PASSTHROUGH_FAILURE;
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    if (nvmeIoCtx->device->os_info.last_error != 0)
                    {
                        printf("Error: ");
                        print_Errno_To_Screen(errno);
                    }
                }
            }
            else
            {
                nvmeIoCtx->commandCompletionData.dw3Valid = true;
                //TODO: How do we set the command specific result on read/write?
                nvmeIoCtx->commandCompletionData.statusAndCID = C_CAST(uint32_t, ioctlResult) << 17;//shift into place since we don't get the phase tag or command ID bits and these are the status field
            }
            break;
        default:
            //use the generic passthrough command structure and IO_CMD
            memset(passThroughCmd, 0, sizeof(struct nvme_passthru_cmd));
            passThroughCmd->opcode = nvmeIoCtx->cmd.nvmCmd.opcode;
            passThroughCmd->flags = nvmeIoCtx->cmd.nvmCmd.flags;
            passThroughCmd->rsvd1 = RESERVED;
            passThroughCmd->nsid = nvmeIoCtx->cmd.nvmCmd.nsid;
            passThroughCmd->cdw2 = nvmeIoCtx->cmd.nvmCmd.cdw2;
            passThroughCmd->cdw3 = nvmeIoCtx->cmd.nvmCmd.cdw3;
            passThroughCmd->metadata = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.nvmCmd.metadata));
            passThroughCmd->addr = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->ptrData));
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
#if defined __clang__
// clang specific because behavior can differ even with the GCC diagnostic being "compatible"
// https ://clang.llvm.org/docs/UsersManual.html#controlling-diagnostics-via-pragmas
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined __GNUC__
//temporarily disable the warning for sign conversion because ioctl definition 
// in some distributions/cross compilers is defined as ioctl(int, unsigned long, ...) and 
// in others is defined as ioctl(int, int, ...)
//While debugging there does not seem to be a real conversion issue here.
//These ioctls still work in either situation, so disabling the warning seems best since there is not
//another way I have found to determine when to cast or not cast the sign conversion.-TJE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif //__clang__, __GNUC__
            ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_IO_CMD, passThroughCmd);
#if defined __clang__
#pragma clang diagnostic pop
#elif defined __GNUC__
//reenable the unused function warning
#pragma GCC diagnostic pop
#endif //__clang__, __GNUC__
            stop_Timer(&commandTimer);
            if (ioctlResult < 0)
            {
                nvmeIoCtx->device->os_info.last_error = C_CAST(unsigned int, errno);
                ret = OS_PASSTHROUGH_FAILURE;
                if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
                {
                    if (nvmeIoCtx->device->os_info.last_error != 0)
                    {
                        printf("Error: ");
                        print_Errno_To_Screen(errno);
                    }
                }
            }
            else
            {
                nvmeIoCtx->commandCompletionData.commandSpecific = passThroughCmd->result;
                nvmeIoCtx->commandCompletionData.dw3Valid = true;
                nvmeIoCtx->commandCompletionData.dw0Valid = true;
                nvmeIoCtx->commandCompletionData.statusAndCID = C_CAST(uint32_t, ioctlResult) << 17;//shift into place since we don't get the phase tag or command ID bits and these are the status field
            }
            break;
        }
        break;
    default:
        return BAD_PARAMETER;
        break;
    }
    nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

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

static eReturnValues linux_NVMe_Reset(tDevice *device, bool subsystemReset)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    //Can only do a reset on a controller handle. Need to get the controller handle if this is a namespace handle!!!
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    int handleToReset = device->os_info.fd;
    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(commandTimer));
    int ioRes = 0;
    bool openedControllerHandle = false;//used so we can close the handle at the end.
    //Need to make sure the handle we use to issue the reset is a controller handle and not a namespace handle.
    char *endptr = NULL;
    char *handle = strstr(&device->os_info.name[0], "/dev/nvme");
    if (handle)
    {
        handle += strlen("/dev/nvme");
    }
    else
    {
        return FAILURE;
    }
    unsigned long controller = 0, namespaceID = 0;
    controller = strtoul(handle, &endptr, 10);
    if ((controller == ULONG_MAX && errno == ERANGE) || (handle == endptr && controller == 0))
    {
        return FAILURE;
    }
    if (endptr && strlen(endptr) > 1 && endptr[0] == 'n')
    {
        handle += 1;
        namespaceID = strtoul(handle, &endptr, 10);
        if ((namespaceID == ULONG_MAX && errno == ERANGE) || (handle == endptr && namespaceID == 0))
        {
            return FAILURE;
        }
    }
    else
    {
        return FAILURE;
    }
    //found a namespace. Need to open a controller handle instead and use it.
    char controllerHandle[40] = { 0 };
    snprintf(controllerHandle, 40, "/dev/nvme%lu", controller);
    if ((handleToReset = open(controllerHandle, O_RDWR | O_NONBLOCK)) < 0)
    {
        device->os_info.last_error = C_CAST(unsigned int, errno);
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
        device->os_info.last_error = C_CAST(unsigned int, errno);
        if (device->deviceVerbosity > VERBOSITY_COMMAND_VERBOSE && device->os_info.last_error != 0)
        {
            printf("Error: ");
            print_Errno_To_Screen(errno);
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
#else //DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif //DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Reset(tDevice *device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = SUCCESS;
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
#else //DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif //DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Subsystem_Reset(tDevice *device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = SUCCESS;
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
#else //DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif //DISABLE_NVME_PASSTHROUGH
}

//to be used with a deep scan???
//fd must be a controller handle
#if defined (_DEBUG)
//making this a debug flagged call since it is currently an unused function. We should look into how to appropriately support this.-TJE
static eReturnValues nvme_Namespace_Rescan(int fd)
{
#if defined (NVME_IOCTL_RESCAN) //This IOCTL is not available on older kernels, which is why this is checked like this - TJE
   eReturnValues ret = OS_PASSTHROUGH_FAILURE;
   int ioRes = ioctl(fd, NVME_IOCTL_RESCAN);
   if (ioRes < 0)
   {
       //failed!
       perror("NVMe Rescan");
   }
   else
   {
       //success!
       ret = SUCCESS;
   }
   return ret;
#else
    M_USE_UNUSED(fd);
    return OS_COMMAND_NOT_AVAILABLE;
#endif
}
#endif //_DEBUG

//Case to remove this from sg_helper.h/c and have a platform/lin/pci-herlper.h vs platform/win/pci-helper.c 

eReturnValues pci_Read_Bar_Reg( tDevice * device, uint8_t * pData, uint32_t dataSize )
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
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
#endif
}

//This is used to open device->os_info.fd2 which is where we will store
//a /dev/sd handle which is a block device handle for SCSI devices.
//This will do nothing on NVMe as it is not needed. - TJE
static eReturnValues open_fd2(tDevice *device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.secondHandleValid && !device->os_info.secondHandleOpened)
    {
        if ((device->os_info.fd2 = open(device->os_info.secondName, O_RDWR | O_NONBLOCK)) < 0)
        {
            perror("open");
            device->os_info.fd2 = errno;
            printf("open failure\n");
            printf("Error: ");
            print_Errno_To_Screen(errno);
            if (device->os_info.fd2 == EACCES)
            {
                return PERMISSION_DENIED;
            }
            else
            {
                return FAILURE;
            }
        }
    }
    return ret;
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
    //BLKFLSBUF
    return NOT_SUPPORTED;
}

eReturnValues os_Lock_Device(tDevice *device)
{
    eReturnValues ret = SUCCESS;
    //Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    //disable O_NONBLOCK
    flags &= ~O_NONBLOCK;
    //Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

eReturnValues os_Unlock_Device(tDevice *device)
{
    eReturnValues ret = SUCCESS;
    //Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    //enable O_NONBLOCK
    flags |= O_NONBLOCK;
    //Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

eReturnValues os_Update_File_System_Cache(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    int *fdToRescan = &device->os_info.fd;
#if defined (_DEBUG)
    printf("Updating file system cache\n");
#endif
    if (device->os_info.secondHandleValid && SUCCESS == open_fd2(device))
    {
#if defined (_DEBUG)
        printf("using fd2: %s\n", device->os_info.secondName);
#endif
        fdToRescan = &device->os_info.fd2;
    }

    //Now, call BLKRRPART
#if defined (_DEBUG)
    printf("Rescanning partition table\n");
#endif
    if (ioctl(*fdToRescan, BLKRRPART) < 0)
    {
#if defined (_DEBUG)
        printf("\tCould not update partition table\n");
        #endif
        device->os_info.last_error = C_CAST(unsigned int, errno);
        if(device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
        {
            printf("Error update partition table: \n");
            print_Errno_To_Screen(errno);
            printf("\n");
        }
        ret = FAILURE;
    }
    return ret;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    //TODO: if BLKZEROOUT available, use this to write zeroes to begining and end of the drive???
    return NOT_SUPPORTED;
}

eReturnValues os_Unmount_File_Systems_On_Device(tDevice *device)
{
    eReturnValues ret = SUCCESS;
    int partitionCount = 0;
    char *blockHandle = device->os_info.name;
    if (device->os_info.secondHandleValid && !is_Block_Device_Handle(blockHandle))
    {
        blockHandle = device->os_info.secondName;
    }
    partitionCount = get_Partition_Count(blockHandle);
#if defined (_DEBUG)
    printf("Partition count for %s = %d\n", blockHandle, partitionCount);
    #endif
    if (partitionCount > 0)
    {
        ptrsPartitionInfo parts = C_CAST(ptrsPartitionInfo, calloc(C_CAST(size_t, partitionCount), sizeof(spartitionInfo)));
        if (parts)
        {
            if (SUCCESS == get_Partition_List(blockHandle, parts, partitionCount))
            {
                int iter = 0;
                for (; iter < partitionCount; ++iter)
                {
                    //since we found a partition, set the "has file system" bool to true
#if defined (_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    //Now that we have a name, unmount the file system
                    //Linux 2.1.116 added the umount2()
                    if (0 > umount2((parts + iter)->mntPath, MNT_FORCE))
                    {
                        ret = FAILURE;
                        device->os_info.last_error = C_CAST(unsigned int, errno);
                        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
                        {
                            printf("Unable to unmount %s: \n", (parts + iter)->mntPath);
                            print_Errno_To_Screen(errno);
                            printf("\n");
                        }
                    }
                }
            }
            safe_Free(C_CAST(void**, &parts));
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}

//This should be at the end of this file to undefine _GNU_SOURCE if this file manually enabled it
#if !defined (_GNU_SOURCE_DEFINED_IN_SG_HELPER)
#undef _GNU_SOURCE
#undef _GNU_SOURCE_DEFINED_IN_SG_HELPER
#endif
