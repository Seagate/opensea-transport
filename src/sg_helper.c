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

// If _GNU_SOURCE is not already defined, define it to get access to versionsort
// NOTE: This will be undefined at the end of this file if GNU_SOURCE_DEFINED_IN_SG_HELPER is set to prevent unexpected
//       behavior in any other parts of the code.
// NOTE: Adding this definition like this so that it can also easily be removed as necessary in the future in case of
// errors.
#if !defined(_GNU_SOURCE)
#    define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#    define GNU_SOURCE_DEFINED_IN_SG_HELPER
#    if defined(_DEBUG)
#        pragma message "Defining _GNU_SOURCE since it was not already defined."
#    endif //_DEBUG
#endif     //! defined (_GNU_SOURCE)

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
#include "time_utils.h"
#include "type_conversion.h"

#include "ata_helper_func.h"
#include "cmds.h"
#include "nvme_helper_func.h"
#include "scsi_helper_func.h"
#include "sg_helper.h"
#include "sntl_helper.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h> //for basename and dirname
#include <mntent.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>  //for mmap pci reads. Potential to move.
#include <sys/mount.h> //for umount and umount2. NOTE: This defines the things we need from linux/fs.h as well, which is why that is commented out - TJE
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h> // for close

// This must be included AFTER sys/mount.h
#include <linux/fs.h> //for BLKRRPART to refresh partition info after completion of an erase

#if defined(__has_include) // GCC5 and higher support this, BUT only if a C standard is specified. The -std=gnuXX does
                           // not support this properly for some odd reason.
#    if __has_include(<linux/nvme_ioctl.h>)
#        if defined(_DEBUG)
#            pragma message "Using linux/nvme_ioctl.h"
#        endif
#        include <linux/nvme_ioctl.h>
#        if !defined(SEA_NVME_IOCTL_H)
#            define SEA_NVME_IOCTL_H
#        endif
#    elif __has_include(<linux/nvme.h>)
#        if defined(_DEBUG)
#            pragma message "Using linux/nvme.h"
#        endif
#        include <linux/nvme.h>
#        if !defined(SEA_NVME_IOCTL_H)
#            define SEA_NVME_IOCTL_H
#        endif
#    elif __has_include(<uapi/nvme.h>)
#        if defined(_DEBUG)
#            pragma message "Using uapi/nvme.h"
#        endif
#        include <uapi/nvme.h>
#        if !defined(SEA_UAPI_NVME_H)
#            define SEA_UAPI_NVME_H
#        endif
#    else //__has_include could not locate the header, check if it was specified by the user through a define.
#        if defined(SEA_NVME_IOCTL_H)
#            include <linux/nvme_ioctl.h>
#        elif defined(SEA_NVME_H)
#            include <linux/nvme.h>
#        elif defined(SEA_UAPI_NVME_H)
#            include <uapi/nvme.h>
#        else
#            pragma message "No NVMe header detected with __has_include. Assuming no NVMe support."
#        endif
#    endif
#else
#    if defined(SEA_NVME_IOCTL_H)
#        include <linux/nvme_ioctl.h>
#    elif defined(SEA_NVME_H)
#        include <linux/nvme.h>
#    elif defined(SEA_UAPI_NVME_H)
#        include <uapi/nvme.h>
#    else
#        pragma message                                                                                                \
            "No NVMe header detected. Assuming no NVMe support. Define one of the following to include the correct NVMe header: SEA_NVME_IOCTL_H, SEA_NVME_H, or SEA_UAPI_NVME_H\nThese specify whether the NVMe IOCTL is in /usr/include/linux/nvme_ioctl.h, /usr/include/linux/nvme.h, or /usr/include/uapi/nvme.h"
#    endif
#endif

#if defined(ENABLE_CISS)
#    include "ciss_helper_func.h"
#    include "raid_scan_helper.h"
#endif // ENABLE_CISS

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

static int sg_filter(const struct dirent* entry)
{
    return !strncmp("sg", entry->d_name, 2);
}

// get sd devices, but ignore any partition number information since that isn't something we can actually send commands
// to
static int sd_filter(const struct dirent* entry)
{
    int sdHandle = strncmp("sd", entry->d_name, 2);
    if (sdHandle != 0)
    {
        return !sdHandle;
    }
    char* partition = strpbrk(entry->d_name, "0123456789");
    if (partition != M_NULLPTR)
    {
        return sdHandle;
    }
    else
    {
        return !sdHandle;
    }
}

// This function is not currently used or tested...if we need to make more changes for pre-2.6 kernels, we may need
// this. bool does_Kernel_Support_SysFS_Link_Mapping()
//{
//     bool linkMappingSupported = false;
//     //kernel version 2.6 and higher is required to map the handles between sg and sd/sr/st/scd
//     OSVersionNumber linuxVersion;
//     safe_memset(&linuxVersion, sizeof(OSVersionNumber), 0, sizeof(OSVersionNumber));
//     if(SUCCESS == get_Operating_System_Version_And_Name(&linuxVersion, M_NULLPTR))
//     {
//         if (linuxVersion.versionType.linuxVersion.kernelVersion >= 2 &&
//         linuxVersion.versionType.linuxVersion.majorVersion >= 6)
//         {
//             linkMappingSupported = true;
//         }
//     }
//     return linkMappingSupported;
// }

static bool is_Block_Device_Handle(const char* handle)
{
    bool isBlockDevice = false;
    if (handle && safe_strlen(handle))
    {
        if (strstr(handle, "sd") || strstr(handle, "st") || strstr(handle, "sr") || strstr(handle, "scd"))
        {
            isBlockDevice = true;
        }
    }
    return isBlockDevice;
}

static bool is_SCSI_Generic_Handle(const char* handle)
{
    bool isGenericDevice = false;
    if (handle && safe_strlen(handle))
    {
        if (strstr(handle, "sg") && !strstr(handle, "bsg"))
        {
            isGenericDevice = true;
        }
    }
    return isGenericDevice;
}

static bool is_Block_SCSI_Generic_Handle(const char* handle)
{
    bool isBlockGenericDevice = false;
    if (handle && safe_strlen(handle))
    {
        if (strstr(handle, "bsg"))
        {
            isBlockGenericDevice = true;
        }
    }
    return isBlockGenericDevice;
}

static bool is_NVMe_Handle(char* handle)
{
    bool isNvmeDevice = false;
    if (handle && safe_strlen(handle))
    {
        if (strstr(handle, "nvme"))
        {
            isNvmeDevice = true;
        }
    }
    return isNvmeDevice;
}

#define GETMNTENT_R_LINE_BUF_SIZE (256)
static int get_Partition_Count(const char* blockDeviceName)
{
    int   result = 0;
    FILE* mount = setmntent("/etc/mtab", "r"); // we only need to know about mounted partitions. Mounted partitions need
                                               // to be known so that they can be unmounted when necessary. - TJE
    if (mount)
    {
        struct mntent* entry = M_NULLPTR;
#if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) // getmntent_r lists these feature test macros to look for - TJE
        struct mntent entBuf;
        DECLARE_ZERO_INIT_ARRAY(char, lineBuf, GETMNTENT_R_LINE_BUF_SIZE);
        while (M_NULLPTR != (entry = getmntent_r(mount, &entBuf, lineBuf, GETMNTENT_R_LINE_BUF_SIZE)))
#else // use the not thread safe version since that is all that is available
        while (M_NULLPTR != (entry = getmntent(mount)))
#endif
        {
            if (strstr(entry->mnt_fsname, blockDeviceName))
            {
                // Found a match, increment result counter.
                ++result;
            }
        }
        endmntent(mount);
    }
    else
    {
        result = -1; // indicate an error opening the mtab file
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
        FILE* mount =
            setmntent("/etc/mtab", "r"); // we only need to know about mounted partitions. Mounted partitions need to be
                                         // known so that they can be unmounted when necessary. - TJE
        if (mount)
        {
            struct mntent* entry = M_NULLPTR;
#if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) ||                                                                   \
    !defined(NO_GETMNTENT_R) // feature test macros we're defining _BSD_SOURCE or _SVID_SOURCE in my testing, but we
                             // want the reentrant version whenever possible. This can be defined if this function is
                             // not identified. - TJE
            struct mntent entBuf;
            DECLARE_ZERO_INIT_ARRAY(char, lineBuf, GETMNTENT_R_LINE_BUF_SIZE);
            while (M_NULLPTR != (entry = getmntent_r(mount, &entBuf, lineBuf, GETMNTENT_R_LINE_BUF_SIZE)))
#else // use the not thread safe version since that is all that is available
#    pragma message "Not using getmntent_r. Partition detection is not thread safe"
            while (M_NULLPTR != (entry = getmntent(mount)))
#endif
            {
                if (strstr(entry->mnt_fsname, blockDeviceName))
                {
                    // found a match, copy it to the list
                    if (matchesFound < listCount)
                    {
                        snprintf_err_handle((partitionInfoList + matchesFound)->fsName, PART_INFO_NAME_LENGTH, "%s",
                                            entry->mnt_fsname);
                        snprintf_err_handle((partitionInfoList + matchesFound)->mntPath, PART_INFO_PATH_LENGTH, "%s",
                                            entry->mnt_dir);
                        ++matchesFound;
                    }
                    else
                    {
                        result = MEMORY_FAILURE; // out of memory to copy all results to the list.
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

typedef struct s_sysFSLowLevelDeviceInfo
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
} sysFSLowLevelDeviceInfo;

#define DRIVER_VERSION_LIST_LENGTH 4

M_NODISCARD static bool get_Driver_Version_Info_From_String(const char* driververstr,
                                                            uint32_t*   versionlist,
                                                            uint8_t     versionlistlen,
                                                            uint8_t*    versionCount)
{
    // There are a few formats that I have seen for this data:
    // major.minor
    // major.minor.rev
    // major.minor.rev[build]-string
    // There may be more.
    if (driververstr && versionlist && versionlistlen == DRIVER_VERSION_LIST_LENGTH &&
        versionCount) // require 4 spaces for the current parsing based off of what is commented above
    {
        char*         end   = M_NULLPTR;
        const char*   str   = driververstr;
        unsigned long value = 0UL;
        *versionCount       = 0;
        // major
        if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL) || end[0] != '.')
        {
            return false;
        }
        versionlist[0] = C_CAST(uint32_t, value);
        *versionCount += 1;
        str = end + 1; // update to past the first dot.
        // minor
        if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
        {
            return false;
        }
        versionlist[1] = C_CAST(uint32_t, value);
        *versionCount += 1;
        if (end[0] == '\0')
        {
            return true;
        }
        else if (end[0] == '.' && safe_strlen(end) > SIZE_T_C(1))
        {
            // rev is available
            str = end + 1;
            if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
            {
                return false;
            }
            versionlist[2] = C_CAST(uint32_t, value);
            *versionCount += 1;
            if (end[0] == '\0')
            {
                return true;
            }
            else if (end[0] == '[' && safe_strlen(end) > SIZE_T_C(1))
            {
                // build is available
                str = end + 1;
                if (0 != safe_strtoul(&value, str, &end, BASE_10_DECIMAL))
                {
                    return false;
                }
                versionlist[3] = C_CAST(uint32_t, value);
                *versionCount += 1;
                if (end[0] == '\0' || end[0] == ']')
                {
                    // considering this complete for now
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

static M_INLINE void close_sysfs_file(FILE** file)
{
    M_STATIC_CAST(void, fclose(*file));
    *file = M_NULLPTR;
}

static void get_Driver_Version_Info_From_Path(const char* driverPath, sysFSLowLevelDeviceInfo* sysFsInfo)
{
    // driverPath now has the full path with the name of the driver.
    // the version number can be found in driverPath/module/version if this file exists.
    // Read this file and save the version information
    char* driverVersionFilePath = M_REINTERPRET_CAST(char*, safe_calloc(OPENSEA_PATH_MAX, sizeof(char)));
    if (driverVersionFilePath)
    {
        snprintf_err_handle(driverVersionFilePath, OPENSEA_PATH_MAX, "%s/module/version", driverPath);
        // printf("driver version file path = %s\n", driverVersionFilePath);
        // convert relative path to a full path. Basically replace ../'s with /sys/ since this will always be ../../bus
        // and we need /sys/buf
        char*  busPtr    = strstr(driverVersionFilePath, "/bus");
        size_t busPtrLen = safe_strlen(busPtr);
        // magic number 4 is for the length of the string "/sys" which is what is being set in the beginning of the
        // path. This is a bit of a mess, but a simple call to realpath was not working, likely due to the current
        // directory not being exactly what we want to start with to allow that function to correctly figure out the
        // path.
        safe_memmove(&driverVersionFilePath[4], OPENSEA_PATH_MAX - 4, busPtr, busPtrLen);
        safe_memset(&driverVersionFilePath[busPtrLen + 4], OPENSEA_PATH_MAX - (busPtrLen + 4), 0,
                    OPENSEA_PATH_MAX - (busPtrLen + 4));
        driverVersionFilePath[0] = '/';
        driverVersionFilePath[1] = 's';
        driverVersionFilePath[2] = 'y';
        driverVersionFilePath[3] = 's';

        struct stat driverversionstat;
        safe_memset(&driverversionstat, sizeof(struct stat), 0, sizeof(struct stat));
        if (0 == stat(driverVersionFilePath, &driverversionstat))
        {
            off_t versionFileSize = driverversionstat.st_size;
            if (versionFileSize > 0)
            {
                FILE*   versionFile = M_NULLPTR;
                errno_t fileopenerr = safe_fopen(&versionFile, driverVersionFilePath, "r");
                if (fileopenerr == 0 && versionFile)
                {
                    char* versionFileData =
                        M_REINTERPRET_CAST(char*, safe_calloc(C_CAST(size_t, versionFileSize) + 1, sizeof(char)));
                    if (versionFileData)
                    {
                        if (C_CAST(size_t, versionFileSize) ==
                                fread(versionFileData, sizeof(char), C_CAST(size_t, versionFileSize), versionFile) &&
                            !ferror(versionFile))
                        {
                            printf("versionFileData = %s\n", versionFileData);
                            snprintf_err_handle(sysFsInfo->driver_info.driverVersionString, MAX_DRIVER_VER_STR, "%s",
                                                versionFileData);
                            DECLARE_ZERO_INIT_ARRAY(uint32_t, versionList, DRIVER_VERSION_LIST_LENGTH);
                            uint8_t versionCount = UINT8_C(0);
                            if (get_Driver_Version_Info_From_String(versionFileData, versionList, 4, &versionCount))
                            {
                                switch (versionCount)
                                {
                                case 4:
                                    // try figuring out what is in the extraVerInfo string
                                    sysFsInfo->driver_info.majorVerValid      = true;
                                    sysFsInfo->driver_info.minorVerValid      = true;
                                    sysFsInfo->driver_info.revisionVerValid   = true;
                                    sysFsInfo->driver_info.buildVerValid      = true;
                                    sysFsInfo->driver_info.driverMajorVersion = versionList[0];
                                    sysFsInfo->driver_info.driverMinorVersion = versionList[1];
                                    sysFsInfo->driver_info.driverRevision     = versionList[2];
                                    sysFsInfo->driver_info.driverBuildNumber  = versionList[3];
                                    break;
                                case 3:
                                    sysFsInfo->driver_info.majorVerValid      = true;
                                    sysFsInfo->driver_info.minorVerValid      = true;
                                    sysFsInfo->driver_info.revisionVerValid   = true;
                                    sysFsInfo->driver_info.driverMajorVersion = versionList[0];
                                    sysFsInfo->driver_info.driverMinorVersion = versionList[1];
                                    sysFsInfo->driver_info.driverRevision     = versionList[2];
                                    break;
                                case 2:
                                    sysFsInfo->driver_info.majorVerValid      = true;
                                    sysFsInfo->driver_info.minorVerValid      = true;
                                    sysFsInfo->driver_info.driverMajorVersion = versionList[0];
                                    sysFsInfo->driver_info.driverMinorVersion = versionList[1];
                                    break;
                                default:
                                    // error reading the string! consider the whole scanf a failure!
                                    // Will need to add other format parsing here if there is something else to read
                                    // instead.-TJE
                                    sysFsInfo->driver_info.driverMajorVersion = UINT32_C(0);
                                    sysFsInfo->driver_info.driverMinorVersion = UINT32_C(0);
                                    sysFsInfo->driver_info.driverRevision     = UINT32_C(0);
                                    sysFsInfo->driver_info.driverBuildNumber  = UINT32_C(0);
                                    break;
                                }
                            }
                            else
                            {
                                sysFsInfo->driver_info.driverMajorVersion = UINT32_C(0);
                                sysFsInfo->driver_info.driverMinorVersion = UINT32_C(0);
                                sysFsInfo->driver_info.driverRevision     = UINT32_C(0);
                                sysFsInfo->driver_info.driverBuildNumber  = UINT32_C(0);
                            }
                        }
                        safe_free(&versionFileData);
                    }
                    close_sysfs_file(&versionFile);
                }
            }
        }
        char* drvPathDup = M_NULLPTR;
        ;
        if (safe_strdup(&drvPathDup, driverPath) == 0 && drvPathDup != M_NULLPTR)
        {
            snprintf_err_handle(sysFsInfo->driver_info.driverName, MAX_DRIVER_NAME, "%s", basename(drvPathDup));
            safe_free(&drvPathDup);
        }
        safe_free(&driverVersionFilePath);
    }
}

static bool read_sysfs_file_uint8(FILE* sysfsfile, uint8_t* value)
{
    bool success = false;
    if (sysfsfile != M_NULLPTR && value != M_NULLPTR)
    {
        char*  line    = M_NULLPTR;
        size_t linelen = SIZE_T_C(0);
        if (getline(&line, &linelen, sysfsfile) != SSIZE_T_C(-1))
        {
            success = get_And_Validate_Integer_Input_Uint8(line, M_NULLPTR, ALLOW_UNIT_NONE, value);
            safe_free(&line);
        }
    }
    return success;
}

static bool read_sysfs_file_uint16(FILE* sysfsfile, uint16_t* value)
{
    bool success = false;
    if (sysfsfile != M_NULLPTR && value != M_NULLPTR)
    {
        char*  line    = M_NULLPTR;
        size_t linelen = SIZE_T_C(0);
        if (getline(&line, &linelen, sysfsfile) != SSIZE_T_C(-1))
        {
            success = get_And_Validate_Integer_Input_Uint16(line, M_NULLPTR, ALLOW_UNIT_NONE, value);
            safe_free(&line);
        }
    }
    return success;
}

static bool read_sysfs_file_uint32(FILE* sysfsfile, uint32_t* value)
{
    bool success = false;
    if (sysfsfile != M_NULLPTR && value != M_NULLPTR)
    {
        char*  line    = M_NULLPTR;
        size_t linelen = SIZE_T_C(0);
        if (getline(&line, &linelen, sysfsfile) != SSIZE_T_C(-1))
        {
            success = get_And_Validate_Integer_Input_Uint32(line, M_NULLPTR, ALLOW_UNIT_NONE, value);
            safe_free(&line);
        }
    }
    return success;
}

static void get_SYS_FS_ATA_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo* sysFsInfo)
{
#if defined(_DEBUG)
    printf("ATA interface!\n");
#endif
    sysFsInfo->interface_type = IDE_INTERFACE;
    sysFsInfo->drive_type     = ATA_DRIVE;
    // get vendor and product IDs of the controller attached to this device.
    DECLARE_ZERO_INIT_ARRAY(char, fullPciPath, PATH_MAX);
    snprintf_err_handle(fullPciPath, PATH_MAX, "%s", inHandleLink);

    fullPciPath[0] = '/';
    fullPciPath[1] = 's';
    fullPciPath[2] = 'y';
    fullPciPath[3] = 's';
    fullPciPath[4] = '/';
    safe_memmove(&fullPciPath[5], PATH_MAX - 5, &fullPciPath[6], safe_strlen(fullPciPath));
    snprintf_err_handle(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullPciPath);
    intptr_t newStrLen = strstr(fullPciPath, "/ata") - fullPciPath;
    if (newStrLen > 0)
    {
        char* pciPath = M_REINTERPRET_CAST(char*, safe_calloc(PATH_MAX, sizeof(char)));
        if (pciPath)
        {
            snprintf_err_handle(pciPath, PATH_MAX, "%.*s/vendor", C_CAST(int, newStrLen), fullPciPath);
            // printf("shortened Path = %s\n", dirname(pciPath));
            FILE*   temp        = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&temp, pciPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.vendorIDValid = read_sysfs_file_uint32(temp, &sysFsInfo->adapter_info.vendorID);
                close_sysfs_file(&temp);
            }
            pciPath = dirname(pciPath); // remove vendor from the end
            safe_strcat(pciPath, PATH_MAX, "/device");
            fileopenerr = safe_fopen(&temp, pciPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.productIDValid =
                    read_sysfs_file_uint32(temp, &sysFsInfo->adapter_info.productID);
                close_sysfs_file(&temp);
            }
            // Store revision data. This seems to be in the bcdDevice file.
            pciPath = dirname(pciPath); // remove device from the end
            safe_strcat(pciPath, PATH_MAX, "/revision");
            fileopenerr = safe_fopen(&temp, pciPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.revisionValid = read_sysfs_file_uint32(temp, &sysFsInfo->adapter_info.revision);
                close_sysfs_file(&temp);
            }
            // Get Driver Information.
            pciPath = dirname(pciPath); // remove driver from the end
            safe_strcat(pciPath, PATH_MAX, "/driver");
            char*   driverPath = M_REINTERPRET_CAST(char*, safe_calloc(OPENSEA_PATH_MAX, sizeof(char)));
            ssize_t len        = readlink(pciPath, driverPath, OPENSEA_PATH_MAX);
            if (len != SSIZE_T_C(-1))
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            safe_free(&driverPath);
            safe_free(&pciPath);
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_PCI;
        }
    }
}

static M_INLINE bool get_usb_file_id_hex(FILE* usbFile, uint32_t* hexvalue)
{
    bool success = false;

    if (usbFile != M_NULLPTR && hexvalue != M_NULLPTR)
    {
        char*  line    = M_NULLPTR;
        size_t linelen = SIZE_T_C(0);
        if (getline(&line, &linelen, usbFile) != -1)
        {
            unsigned long temp = 0UL;
            if (0 != safe_strtoul(&temp, line, M_NULLPTR, BASE_16_HEX))
            {
                success = false;
            }
            else
            {
                if (temp <= UINT32_MAX) // make sure this is in range before considering this successful
                {
                    *hexvalue = M_STATIC_CAST(uint32_t, temp);
                    success   = true;
                }
            }
            safe_free(&line);
        }
    }
    return success;
}

static void get_SYS_FS_USB_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo* sysFsInfo)
{
#if defined(_DEBUG)
    printf("USB interface!\n");
#endif
    sysFsInfo->interface_type = USB_INTERFACE;
    sysFsInfo->drive_type     = SCSI_DRIVE; // changed later depending on what passthrough the USB adapter supports
    // set the USB VID and PID. NOTE: There may be a better way to do this, but this seems to work for now.
    DECLARE_ZERO_INIT_ARRAY(char, fullPciPath, PATH_MAX);
    snprintf_err_handle(fullPciPath, PATH_MAX, "%s", inHandleLink);

    fullPciPath[0] = '/';
    fullPciPath[1] = 's';
    fullPciPath[2] = 'y';
    fullPciPath[3] = 's';
    fullPciPath[4] = '/';
    safe_memmove(&fullPciPath[5], PATH_MAX - 5, &fullPciPath[6], safe_strlen(fullPciPath));
    snprintf_err_handle(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullPciPath);
    intptr_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath;
    if (newStrLen > 0)
    {
        char* usbPath = M_REINTERPRET_CAST(char*, safe_calloc(PATH_MAX, sizeof(char)));
        if (usbPath)
        {
            snprintf_err_handle(usbPath, PATH_MAX, "%.*s", C_CAST(int, newStrLen), fullPciPath);
            usbPath = dirname(usbPath);
            // printf("full USB Path = %s\n", usbPath);
            // now that the path is correct, we need to read the files idVendor and idProduct
            safe_strcat(usbPath, PATH_MAX, "/idVendor");
            // printf("idVendor USB Path = %s\n", usbPath);
            FILE*   temp        = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&temp, usbPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.vendorIDValid = get_usb_file_id_hex(temp, &sysFsInfo->adapter_info.vendorID);
                close_sysfs_file(&temp);
            }
            usbPath = dirname(usbPath); // remove idVendor from the end
            // printf("full USB Path = %s\n", usbPath);
            safe_strcat(usbPath, PATH_MAX, "/idProduct");
            // printf("idProduct USB Path = %s\n", usbPath);
            fileopenerr = safe_fopen(&temp, usbPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.productIDValid = get_usb_file_id_hex(temp, &sysFsInfo->adapter_info.productID);
                close_sysfs_file(&temp);
            }
            // Store revision data. This seems to be in the bcdDevice file.
            usbPath = dirname(usbPath); // remove idProduct from the end
            safe_strcat(usbPath, PATH_MAX, "/bcdDevice");
            fileopenerr = safe_fopen(&temp, usbPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.revisionValid = get_usb_file_id_hex(temp, &sysFsInfo->adapter_info.revision);
                close_sysfs_file(&temp);
            }
            // Get Driver Information.
            usbPath = dirname(usbPath); // remove idProduct from the end
            safe_strcat(usbPath, PATH_MAX, "/driver");
            char*   driverPath = M_REINTERPRET_CAST(char*, safe_calloc(OPENSEA_PATH_MAX, sizeof(char)));
            ssize_t len        = readlink(usbPath, driverPath, OPENSEA_PATH_MAX);
            if (len != SSIZE_T_C(-1))
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            safe_free(&driverPath);
            safe_free(&usbPath);
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_USB;
        }
    }
}

static bool get_ieee1394_ids(FILE*     idFile,
                             uint32_t* vendorID,
                             uint32_t* productID,
                             uint32_t* specifierID,
                             uint32_t* revision)
{
    bool success = false;
    if (idFile != M_NULLPTR && vendorID != M_NULLPTR && productID != M_NULLPTR && specifierID != M_NULLPTR &&
        revision != M_NULLPTR)
    {
        char*  line = M_NULLPTR;
        size_t len  = SIZE_T_C(0);
        if (getline(&line, &len, idFile) != -1)
        {
            // line format: ieee1394:venXXXXXXXXmoXXXXXXXXspXXXXXXXXverXXXXXXXX
            // all values are hex
            // verify file starts with interface
            if (strstr(line, "ieee1394") != M_NULLPTR)
            {
                // offset past the "ven" in the string
                char*         endptr    = M_NULLPTR;
                char*         stroffset = strchr(line, ':') + 3; // 3 = length of "ven" in file's format shown above
                unsigned long temp      = 0UL;
                bool          parsing   = true;
                // parse each part with strtoul
                success = true; // assume this works until we hit a failure below
                do
                {
                    if (stroffset == M_NULLPTR)
                    {
                        success = false;
                        parsing = false;
                        break;
                    }
                    if (0 != safe_strtoul(&temp, stroffset, &endptr, BASE_16_HEX))
                    {
                        success = false;
                        parsing = false;
                    }
                    else if (strstr(endptr, "mo") == endptr)
                    {
                        if (temp <= UINT32_MAX)
                        {
                            *vendorID = M_STATIC_CAST(uint32_t, temp);
                            stroffset = endptr + 2;
                        }
                        else
                        {
                            parsing = false;
                            success = false;
                        }
                    }
                    else if (strstr(endptr, "sp") == endptr)
                    {
                        if (temp <= UINT32_MAX)
                        {
                            *productID = M_STATIC_CAST(uint32_t, temp);
                            stroffset  = endptr + 2;
                        }
                        else
                        {
                            parsing = false;
                            success = false;
                        }
                    }
                    else if (strstr(endptr, "ver") == endptr)
                    {
                        if (temp <= UINT32_MAX)
                        {
                            *specifierID = M_STATIC_CAST(uint32_t, temp);
                            stroffset    = endptr + 3;
                        }
                        else
                        {
                            parsing = false;
                            success = false;
                        }
                    }
                    else if (endptr[0] == '\0') // end of the line
                    {
                        if (temp <= UINT32_MAX)
                        {
                            *revision = M_STATIC_CAST(uint32_t, temp);
                            parsing   = false; // we are now done parsing so set this flag
                        }
                        else
                        {
                            parsing = false;
                        }
                    }
                    else // this is an error case, so assume done and failure
                    {
                        parsing = false;
                        success = false;
                    }
                } while (parsing);
            }
            safe_free(&line);
        }
    }
    return success;
}

static void get_SYS_FS_1394_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo* sysFsInfo)
{
#if defined(_DEBUG)
    printf("FireWire interface!\n");
#endif
    sysFsInfo->interface_type = IEEE_1394_INTERFACE;
    sysFsInfo->drive_type     = SCSI_DRIVE; // changed later if detected as ATA
    DECLARE_ZERO_INIT_ARRAY(char, fullFWPath, PATH_MAX);
    snprintf_err_handle(fullFWPath, PATH_MAX, "%s", inHandleLink);

    fullFWPath[0] = '/';
    fullFWPath[1] = 's';
    fullFWPath[2] = 'y';
    fullFWPath[3] = 's';
    fullFWPath[4] = '/';
    safe_memmove(&fullFWPath[5], PATH_MAX - 5, &fullFWPath[6], safe_strlen(fullFWPath));
    snprintf_err_handle(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullFWPath);
    // now we need to go up a few directories to get the modalias file to parse
    intptr_t newStrLen = strstr(fullFWPath, "/host") - fullFWPath;
    if (newStrLen > 0)
    {
        char* fwPath = M_REINTERPRET_CAST(char*, safe_calloc(PATH_MAX, sizeof(char)));
        if (fwPath)
        {
            snprintf_err_handle(fwPath, PATH_MAX, "%.*s/modalias", C_CAST(int, newStrLen), fullFWPath);
            // printf("full FW Path = %s\n", dirname(fwPath));
            // printf("modalias FW Path = %s\n", fwPath);
            FILE*   temp        = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&temp, fwPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                // This file contains everything in one place. Otherwise we would need to parse multiple files at
                // slightly different paths to get everything - TJE
                if (get_ieee1394_ids(temp, &sysFsInfo->adapter_info.vendorID, &sysFsInfo->adapter_info.productID,
                                     &sysFsInfo->adapter_info.specifierID, &sysFsInfo->adapter_info.revision))
                {
                    sysFsInfo->adapter_info.vendorIDValid    = true;
                    sysFsInfo->adapter_info.productIDValid   = true;
                    sysFsInfo->adapter_info.specifierIDValid = true;
                    sysFsInfo->adapter_info.revisionValid    = true;
                    // printf("Got vendor ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.vendorID);
                    // printf("Got product ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.productID);
                    // printf("Got specifier ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.specifierID);
                    // printf("Got revision ID as %" PRIX16 "h\n", sysFsInfo->adapter_info.revision);
                }
                close_sysfs_file(&temp);
            }
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_IEEE1394;
            // Get Driver Information.
            fwPath = dirname(fwPath); // remove idProduct from the end
            safe_strcat(fwPath, PATH_MAX, "/driver");
            char*   driverPath = M_REINTERPRET_CAST(char*, safe_calloc(OPENSEA_PATH_MAX, sizeof(char)));
            ssize_t len        = readlink(fwPath, driverPath, OPENSEA_PATH_MAX);
            if (len != SSIZE_T_C(-1))
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            safe_free(&driverPath);
            safe_free(&fwPath);
        }
    }
}

static void get_SYS_FS_SCSI_Info(const char* inHandleLink, sysFSLowLevelDeviceInfo* sysFsInfo)
{
#if defined(_DEBUG)
    printf("SCSI interface!\n");
#endif
    sysFsInfo->interface_type = SCSI_INTERFACE;
    sysFsInfo->drive_type     = SCSI_DRIVE;
    // get vendor and product IDs of the controller attached to this device.

    DECLARE_ZERO_INIT_ARRAY(char, fullPciPath, PATH_MAX);
    snprintf_err_handle(fullPciPath, PATH_MAX, "%s", inHandleLink);

    fullPciPath[0] = '/';
    fullPciPath[1] = 's';
    fullPciPath[2] = 'y';
    fullPciPath[3] = 's';
    fullPciPath[4] = '/';
    safe_memmove(&fullPciPath[5], PATH_MAX - 5, &fullPciPath[6], safe_strlen(fullPciPath));
    snprintf_err_handle(sysFsInfo->fullDevicePath, OPENSEA_PATH_MAX, "%s", fullPciPath);
    // need to trim the path down now since it can vary by controller:
    // adaptec: /sys/devices/pci0000:00/0000:00:02.0/0000:02:00.0/host0/target0:1:0/0:1:0:0/scsi_generic/sg2
    // lsi:
    // /sys/devices/pci0000:00/0000:00:02.0/0000:02:00.0/host0/port-0:16/end_device-0:16/target0:0:16/0:0:16:0/scsi_generic/sg4
    // The best way seems to break by the word "host" at this time.
    // printf("Full pci path: %s\n", fullPciPath);
    // printf("/host location string: %s\n", strstr(fullPciPath, "/host"));
    // printf("FULL: %" PRIXPTR "\t/HOST: %" PRIXPTR "\n", C_CAST(uintptr_t, fullPciPath), C_CAST(uintptr_t,
    // strstr(fullPciPath, "/host")));
    intptr_t newStrLen = strstr(fullPciPath, "/host") - fullPciPath;
    if (newStrLen > 0)
    {
        char* pciPath = M_REINTERPRET_CAST(char*, safe_calloc(PATH_MAX, sizeof(char)));
        if (pciPath)
        {
            snprintf_err_handle(pciPath, PATH_MAX, "%.*s/vendor", C_CAST(int, newStrLen), fullPciPath);
            // printf("Shortened PCI Path: %s\n", dirname(pciPath));
            FILE*   temp        = M_NULLPTR;
            errno_t fileopenerr = safe_fopen(&temp, pciPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.vendorIDValid = read_sysfs_file_uint32(temp, &sysFsInfo->adapter_info.vendorID);
                close_sysfs_file(&temp);
            }
            pciPath = dirname(pciPath); // remove vendor from the end
            safe_strcat(pciPath, PATH_MAX, "/device");
            fileopenerr = safe_fopen(&temp, pciPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.productIDValid =
                    read_sysfs_file_uint32(temp, &sysFsInfo->adapter_info.productID);
                close_sysfs_file(&temp);
            }
            // Store revision data. This seems to be in the bcdDevice file.
            pciPath = dirname(pciPath); // remove device from the end
            safe_strcat(pciPath, PATH_MAX, "/revision");
            fileopenerr = safe_fopen(&temp, pciPath, "r");
            if (fileopenerr == 0 && temp != M_NULLPTR)
            {
                sysFsInfo->adapter_info.revisionValid = read_sysfs_file_uint32(temp, &sysFsInfo->adapter_info.revision);
                close_sysfs_file(&temp);
            }
            // Store Driver Information
            pciPath = dirname(pciPath);
            safe_strcat(pciPath, PATH_MAX, "/driver");
            char* driverPath = M_REINTERPRET_CAST(char*, safe_calloc(OPENSEA_PATH_MAX, sizeof(char)));
            if (SSIZE_T_C(-1) != readlink(pciPath, driverPath, OPENSEA_PATH_MAX))
            {
                get_Driver_Version_Info_From_Path(driverPath, sysFsInfo);
            }
            // printf("\nPath: %s\tname: %s", sysFsInfo->driver_info.driverPath,
            safe_free(&driverPath);
            sysFsInfo->adapter_info.infoType = ADAPTER_INFO_PCI;
            safe_free(&pciPath);
        }
    }
}

static void get_SYS_FS_SCSI_Address(const char* inHandleLink, sysFSLowLevelDeviceInfo* sysFsInfo)
{
    // printf("getting SCSI address\n");
    // set the scsi address field
    char* handle = M_NULLPTR;
    if (0 != safe_strdup(&handle, inHandleLink))
    {
        return;
    }
    char* scsiAddress = basename(dirname(dirname(handle))); // SCSI address should be 2nd from the end of the link
    if (scsiAddress)
    {
        char*   saveptr = M_NULLPTR;
        rsize_t addrlen = safe_strlen(scsiAddress);
        char*   token   = safe_String_Token(scsiAddress, &addrlen, ":", &saveptr);
        uint8_t counter = UINT8_C(0);
        while (token)
        {
            unsigned long temp = 0UL;
            if (0 != safe_strtoul(&temp, token, M_NULLPTR, BASE_10_DECIMAL) || temp > UINT8_MAX)
            {
#if defined(_DEBUG)
                printf("Error parsing HCTL\n");
#endif //_DEBUG
                break;
            }
            switch (counter)
            {
            case 0: // host
                sysFsInfo->scsiAddress.host = M_STATIC_CAST(uint8_t, temp);
                break;
            case 1: // bus
                sysFsInfo->scsiAddress.channel = M_STATIC_CAST(uint8_t, temp);
                break;
            case 2: // target
                sysFsInfo->scsiAddress.target = M_STATIC_CAST(uint8_t, temp);
                break;
            case 3: // lun
                sysFsInfo->scsiAddress.lun = M_STATIC_CAST(uint8_t, temp);
                break;
            default:
                break;
            }
            token = safe_String_Token(M_NULLPTR, &addrlen, ":", &saveptr);
            ++counter;
        }
    }
    safe_free(&handle);
}

// read type or inquiry files, queue depth.
// TODO: read other files in this structure that could be useful??? mn, vendor, revision, sas_address
// NOTE: not all files will exist for all devices/types
// NOTE: SAS, SCSI, SATA, USB will all use this since they all are treated as SCSI devices by the OS.
//       NVMe will need a different set of instructions/things to do.
// NOTE: This counts on "full device path" being set in sysFsInfo already (which it should be)
static void get_Linux_SYS_FS_SCSI_Device_File_Info(sysFSLowLevelDeviceInfo* sysFsInfo)
{
    DECLARE_ZERO_INIT_ARRAY(char, fullPathBuffer, PATH_MAX);
    char* fullPath = &fullPathBuffer[0];
    snprintf_err_handle(fullPath, PATH_MAX, "%s", sysFsInfo->fullDevicePath);
    safe_strcat(fullPath, PATH_MAX, "/device/type");
    FILE*   temp        = M_NULLPTR;
    errno_t fileopenerr = safe_fopen(&temp, fullPath, "r");
    if (fileopenerr == 0 && temp != M_NULLPTR)
    {
        uint8_t scsiDeviceType = UINT8_C(0);
        sysFsInfo->scsiDevType = PERIPHERAL_UNKNOWN_OR_NO_DEVICE_TYPE;
        if (!read_sysfs_file_uint8(temp, &scsiDeviceType))
        {
            sysFsInfo->scsiDevType = M_STATIC_CAST(eSCSIPeripheralDeviceType, scsiDeviceType);
        }
        close_sysfs_file(&temp);
    }
    else
    {
        // could not open the type file, so try the inquiry file and read the first byte as raw binary since this is how
        // this file is stored
        fullPath = dirname(fullPath);
        safe_strcat(fullPath, PATH_MAX, "/inquiry");
        fileopenerr = safe_fopen(&temp, fullPath, "rb");
        if (fileopenerr == 0 && temp != M_NULLPTR)
        {
            uint8_t peripheralType = UINT8_C(0);
            if (SIZE_T_C(1) == fread(&peripheralType, sizeof(uint8_t), SIZE_T_C(1), temp))
            {
                sysFsInfo->scsiDevType = get_bit_range_uint8(peripheralType, 4, 0);
            }
            close_sysfs_file(&temp);
        }
    }
    fullPath = dirname(fullPath);
    safe_strcat(fullPath, PATH_MAX, "/queue_depth");
    fileopenerr = safe_fopen(&temp, fullPath, "r");
    if (fileopenerr == 0 && temp != M_NULLPTR)
    {
        if (!read_sysfs_file_uint16(temp, &sysFsInfo->queueDepth))
        {
            sysFsInfo->queueDepth = 0;
        }
        close_sysfs_file(&temp);
    }
}

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
static void get_Linux_SYS_FS_Info(const char* handle, sysFSLowLevelDeviceInfo* sysFsInfo)
{
    // check if it's a block handle, bsg, or scsi_generic handle, then setup the path we need to read.
    if (handle && sysFsInfo)
    {
        if (strstr(handle, "nvme") != M_NULLPTR)
        {
            size_t nvmHandleLen = safe_strlen(handle) + 1;
            char*  nvmHandle    = M_REINTERPRET_CAST(char*, safe_calloc(nvmHandleLen, sizeof(char)));
            snprintf_err_handle(nvmHandle, nvmHandleLen, "%s", handle);
            sysFsInfo->interface_type = NVME_INTERFACE;
            sysFsInfo->drive_type     = NVME_DRIVE;
            snprintf_err_handle(sysFsInfo->primaryHandleStr, OS_HANDLE_NAME_MAX_LENGTH, "%s", nvmHandle);
        }
        else // not NVMe, so we need to do some investigation of the handle. NOTE: this requires 2.6 and later kernel
             // since it reads a link in the /sys/class/ filesystem
        {
            bool incomingBlock = false; // only set for SD!
            bool bsg           = false;
            DECLARE_ZERO_INIT_ARRAY(char, incomingHandleClassPath, PATH_MAX);
            safe_strcat(incomingHandleClassPath, PATH_MAX, "/sys/class/");
            if (is_Block_Device_Handle(handle))
            {
                safe_strcat(incomingHandleClassPath, PATH_MAX, "block/");
                incomingBlock = true;
            }
            else if (is_Block_SCSI_Generic_Handle(handle))
            {
                bsg = true;
                safe_strcat(incomingHandleClassPath, PATH_MAX, "bsg/");
            }
            else if (is_SCSI_Generic_Handle(handle))
            {
                safe_strcat(incomingHandleClassPath, PATH_MAX, "scsi_generic/");
            }
            else
            {
                // unknown. Time to exit gracefully
                sysFsInfo->interface_type = SCSI_INTERFACE;
                sysFsInfo->drive_type     = UNKNOWN_DRIVE;
                return;
            }
            // first make sure this directory exists
            struct stat inHandleStat;
            safe_memset(&inHandleStat, sizeof(struct stat), 0, sizeof(struct stat));
            if (stat(incomingHandleClassPath, &inHandleStat) == 0 && S_ISDIR(inHandleStat.st_mode))
            {
                char* duphandle = M_NULLPTR;
                if (0 != safe_strdup(&duphandle, handle) || duphandle == M_NULLPTR)
                {
                    return;
                }
                const char* basehandle = basename(duphandle);
                struct stat link;
                safe_memset(&link, sizeof(struct stat), 0, sizeof(struct stat));
                safe_strcat(incomingHandleClassPath, PATH_MAX, basehandle);
                // now read the link with the handle appended on the end
                if (lstat(incomingHandleClassPath, &link) == 0 && S_ISLNK(link.st_mode))
                {
                    DECLARE_ZERO_INIT_ARRAY(char, inHandleLink, PATH_MAX);
                    if (readlink(incomingHandleClassPath, inHandleLink, PATH_MAX) > 0)
                    {
                        // Read the link and set up all the fields we want to setup.
                        // Start with setting the device interface
                        // example ata device link:
                        // ../../devices/pci0000:00/0000:00:1f.2/ata8/host8/target8:0:0/8:0:0:0/scsi_generic/sg2 example
                        // usb device link:
                        // ../../devices/pci0000:00/0000:00:1c.1/0000:03:00.0/usb4/4-1/4-1:1.0/host21/target21:0:0/21:0:0:0/scsi_generic/sg4
                        // example sas device link:
                        // ../../devices/pci0000:00/0000:00:1c.0/0000:02:00.0/host0/port-0:0/end_device-0:0/target0:0:0/0:0:0:0/scsi_generic/sg3
                        // example firewire device link:
                        // ../../devices/pci0000:00/0000:00:1c.5/0000:04:00.0/0000:05:09.0/0000:0b:00.0/0000:0c:02.0/fw1/fw1.0/host13/target13:0:0/13:0:0:0/scsi_generic/sg3
                        // example sata over sas device link:
                        // ../../devices/pci0000:00/0000:00:1c.0/0000:02:00.0/host0/port-0:1/end_device-0:1/target0:0:1/0:0:1:0/scsi_generic/sg5
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
                        // if the link doesn't conatin ata or usb in it, then we are assuming it's scsi since scsi
                        // doesn't have a nice simple string to check
                        else
                        {
                            get_SYS_FS_SCSI_Info(inHandleLink, sysFsInfo);
                        }
                        get_Linux_SYS_FS_SCSI_Device_File_Info(sysFsInfo);

                        char* baseLink = basename(inHandleLink);
                        if (bsg)
                        {
                            snprintf_err_handle(sysFsInfo->primaryHandleStr, OS_HANDLE_NAME_MAX_LENGTH, "/dev/bsg/%s",
                                                baseLink);
                        }
                        else
                        {
                            snprintf_err_handle(sysFsInfo->primaryHandleStr, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s",
                                                baseLink);
                        }

                        get_SYS_FS_SCSI_Address(inHandleLink, sysFsInfo);
                        // printf("attempting to map the handle\n");
                        // Lastly, call the mapping function to get the matching block handle and check what we got to
                        // set ATAPI, TAPE or leave as-is. Setting these is necessary to prevent talking to ATAPI as HDD
                        // due to overlapping A1h opcode
                        char* block = M_NULLPTR;
                        char* gen   = M_NULLPTR;
                        if (SUCCESS == map_Block_To_Generic_Handle(handle, &gen, &block))
                        {
                            // printf("successfully mapped the handle. gen = %s\tblock=%s\n", gen, block);
                            // Our incoming handle SHOULD always be sg/bsg, but just in case, we need to check before we
                            // setup the second handle (mapped handle) information
                            if (incomingBlock)
                            {
                                // block device handle was sent into here (and we made it this far...unlikely)
                                // Secondary handle will be a generic handle
                                if (is_Block_SCSI_Generic_Handle(gen))
                                {
                                    snprintf_err_handle(sysFsInfo->secondaryHandleStr, OS_SECOND_HANDLE_NAME_LENGTH,
                                                        "/dev/bsg/%s", gen);
                                }
                                else
                                {
                                    snprintf_err_handle(sysFsInfo->secondaryHandleStr, OS_SECOND_HANDLE_NAME_LENGTH,
                                                        "/dev/%s", gen);
                                }
                            }
                            else
                            {
                                // generic handle was sent in
                                // secondary handle will be a block handle
                                snprintf_err_handle(sysFsInfo->secondaryHandleStr, OS_SECOND_HANDLE_NAME_LENGTH,
                                                    "/dev/%s", block);
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
                                // scsi enclosure services
                            }
                        }
                        // printf("Finish handle mapping\n");
                        safe_free(&block);
                        safe_free(&gen);
                    }
                    else
                    {
                        // couldn't read the link...for who knows what reason...
                    }
                }
                else
                {
                    // Not a link...nothing further to do
                }
                safe_free(&duphandle);
            }
        }
    }
}

static void set_Device_Fields_From_Handle(const char* handle, tDevice* device)
{
    sysFSLowLevelDeviceInfo sysFsInfo;
    safe_memset(&sysFsInfo, sizeof(sysFSLowLevelDeviceInfo), 0, sizeof(sysFSLowLevelDeviceInfo));
    // set scsi interface and scsi drive until we know otherwise
    sysFsInfo.drive_type     = SCSI_DRIVE;
    sysFsInfo.interface_type = SCSI_INTERFACE;
    get_Linux_SYS_FS_Info(handle, &sysFsInfo);
    // now copy the saved data to tDevice. -TJE
    if (device)
    {
        device->drive_info.drive_type     = sysFsInfo.drive_type;
        device->drive_info.interface_type = sysFsInfo.interface_type;
        safe_memcpy(&device->drive_info.adapter_info, sizeof(adapterInfo), &sysFsInfo.adapter_info,
                    sizeof(adapterInfo));
        safe_memcpy(&device->drive_info.driver_info, sizeof(driverInfo), &sysFsInfo.driver_info, sizeof(driverInfo));
        if (safe_strlen(sysFsInfo.primaryHandleStr) > 0)
        {
            snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", sysFsInfo.primaryHandleStr);
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s",
                                basename(sysFsInfo.primaryHandleStr));
        }
        if (safe_strlen(sysFsInfo.secondaryHandleStr) > 0)
        {
            snprintf_err_handle(device->os_info.secondName, OS_SECOND_HANDLE_NAME_LENGTH, "%s",
                                sysFsInfo.secondaryHandleStr);
            snprintf_err_handle(device->os_info.secondFriendlyName, OS_SECOND_HANDLE_NAME_LENGTH, "%s",
                                basename(sysFsInfo.secondaryHandleStr));
        }
    }
}

// map a block handle (sd) to a generic handle (sg or bsg)
// incoming handle can be either sd, sg, or bsg type
// This depends on mapping in the file system provided by 2.6 and later.
eReturnValues map_Block_To_Generic_Handle(const char* handle, char** genericHandle, char** blockHandle)
{
    DISABLE_NONNULL_COMPARE
    if (handle == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    // if the handle passed in contains "nvme" then we know it's a device on the nvme interface
    if (strstr(handle, "nvme") != M_NULLPTR)
    {
        return NOT_SUPPORTED;
    }
    else
    {
        bool incomingBlock = false; // only set for SD!
        DECLARE_ZERO_INIT_ARRAY(char, incomingHandleClassPath, PATH_MAX);
        safe_strcat(incomingHandleClassPath, PATH_MAX, "/sys/class/");
        if (is_Block_Device_Handle(handle))
        {
            safe_strcat(incomingHandleClassPath, PATH_MAX, "block/");
            incomingBlock = true;
        }
        else if (is_Block_SCSI_Generic_Handle(handle))
        {
            safe_strcat(incomingHandleClassPath, PATH_MAX, "bsg/");
        }
        else if (is_SCSI_Generic_Handle(handle))
        {
            safe_strcat(incomingHandleClassPath, PATH_MAX, "scsi_generic/");
        }
        // first make sure this directory exists
        struct stat inHandleStat;
        if (stat(incomingHandleClassPath, &inHandleStat) == 0 && S_ISDIR(inHandleStat.st_mode))
        {
            char* dupHandle = M_NULLPTR;
            if (0 != safe_strdup(&dupHandle, handle) || dupHandle == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
            const char* basehandle = basename(dupHandle);
            safe_strcat(incomingHandleClassPath, PATH_MAX, basehandle);
            // now read the link with the handle appended on the end
            DECLARE_ZERO_INIT_ARRAY(char, inHandleLink, PATH_MAX);
            if (readlink(incomingHandleClassPath, inHandleLink, PATH_MAX) > 0)
            {
                // printf("full in handleLink = %s\n", inHandleLink);
                // now we need to map it to a generic handle (sg...if sg not available, bsg)
                const char* scsiGenericClass = "/sys/class/scsi_generic/";
                const char* bsgClass         = "/sys/class/bsg/";
                const char* blockClass       = "/sys/class/block/";
                struct stat mapStat;
                DECLARE_ZERO_INIT_ARRAY(char, classPath, PATH_MAX);
                bool bsg = false;
                if (incomingBlock)
                {
                    // check for sg, then bsg
                    if (stat(scsiGenericClass, &mapStat) == 0 && S_ISDIR(mapStat.st_mode))
                    {
                        snprintf_err_handle(classPath, PATH_MAX, "%s", scsiGenericClass);
                    }
                    else if (stat(bsgClass, &mapStat) == 0 && S_ISDIR(mapStat.st_mode))
                    {
                        snprintf_err_handle(classPath, PATH_MAX, "%s", bsgClass);
                        bsg = true;
                    }
                    else
                    {
                        // printf ("could not map to generic class");
                        safe_free(&dupHandle);
                        return NOT_SUPPORTED;
                    }
                }
                else
                {
                    // check for block
                    snprintf_err_handle(classPath, PATH_MAX, "%s", blockClass);
                    if (!(stat(classPath, &mapStat) == 0 && S_ISDIR(mapStat.st_mode)))
                    {
                        // printf ("could not map to block class");
                        safe_free(&dupHandle);
                        return NOT_SUPPORTED;
                    }
                }
                // now we need to loop through each think in the class folder, read the link, and check if we match.
                struct dirent** classList;
                int             numberOfItems = scandir(classPath, &classList,
                                                        M_NULLPTR /*not filtering anything. Just go through each item*/, alphasort);
                eReturnValues   ret           = SUCCESS;
                for (int iter = 0; iter < numberOfItems && ret == SUCCESS; ++iter)
                {
                    // now we need to read the link for classPath/d_name into a buffer...then compare it to the one we
                    // read earlier.
                    size_t      tempLen = safe_strlen(classPath) + safe_strlen(classList[iter]->d_name) + 1;
                    char*       temp    = M_REINTERPRET_CAST(char*, safe_calloc(tempLen, sizeof(char)));
                    struct stat tempStat;
                    safe_memset(&tempStat, sizeof(struct stat), 0, sizeof(struct stat));
                    snprintf_err_handle(temp, tempLen, "%s%s", classPath, classList[iter]->d_name);
                    if (lstat(temp, &tempStat) == 0 && S_ISLNK(tempStat.st_mode)) /*check if this is a link*/
                    {
                        DECLARE_ZERO_INIT_ARRAY(char, mapLink, PATH_MAX);
                        if (readlink(temp, mapLink, PATH_MAX) > 0)
                        {
                            char*  className       = M_NULLPTR;
                            size_t classNameLength = SIZE_T_C(0);
                            // printf("read link as: %s\n", mapLink);
                            // now, we need to check the links and see if they match.
                            // NOTE: If we are in the block class, we will see sda, sda1, sda 2. These are all matches
                            // (technically)
                            //       We SHOULD match on the first disk without partition numbers since we did alphasort
                            // We need to match up until the class name (ex: block, bsg, scsi_generic)
                            if (incomingBlock) // block class
                            {
                                classNameLength = safe_strlen("scsi_generic") + 1;
                                className       = M_REINTERPRET_CAST(char*, safe_calloc(classNameLength, sizeof(char)));
                                if (className)
                                {
                                    snprintf_err_handle(className, classNameLength, "scsi_generic");
                                }
                            }
                            else if (bsg) // bsg class
                            {
                                classNameLength = safe_strlen("bsg") + 1;
                                className       = M_REINTERPRET_CAST(char*, safe_calloc(classNameLength, sizeof(char)));
                                if (className)
                                {
                                    snprintf_err_handle(className, classNameLength, "bsg");
                                }
                            }
                            else // scsi_generic class
                            {
                                classNameLength = safe_strlen("block") + 1;
                                className       = M_REINTERPRET_CAST(char*, safe_calloc(classNameLength, sizeof(char)));
                                if (className)
                                {
                                    snprintf_err_handle(className, classNameLength, "block");
                                }
                            }
                            if (className)
                            {
                                char* classPtr = strstr(mapLink, className);
                                // need to match up to the classname
                                if (M_NULLPTR != classPtr && strncmp(mapLink, inHandleLink,
                                                                     (M_STATIC_CAST(uintptr_t, classPtr) -
                                                                      M_STATIC_CAST(uintptr_t, mapLink))) == 0)
                                {
                                    if (incomingBlock)
                                    {
                                        if (0 != safe_strndup(blockHandle, basehandle, safe_strlen(basehandle)) ||
                                            0 != safe_strdup(genericHandle, basename(classPtr)))
                                        {
                                            ret = MEMORY_FAILURE;
                                        }
                                    }
                                    else
                                    {
                                        if (0 != safe_strndup(blockHandle, basename(classPtr),
                                                              safe_strlen(basename(classPtr))) ||
                                            0 != safe_strdup(genericHandle, basehandle))
                                        {
                                            ret = MEMORY_FAILURE;
                                        }
                                    }
                                    safe_free(&className);
                                    safe_free(&temp);
                                    safe_free(&dupHandle);
                                    break; // found a match, exit the loop
                                }
                            }
                            safe_free(&className);
                        }
                    }
                    safe_free(&temp);
                }
                for (int classiter = 0; classiter < numberOfItems; ++classiter)
                {
                    safe_free_dirent(&classList[classiter]);
                }
                safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &classList));
                if (ret != SUCCESS)
                {
                    return ret;
                }
            }
            else
            {
                // not a link, or some other error....probably an old kernel
                safe_free(&dupHandle);
                return NOT_SUPPORTED;
            }
            safe_free(&dupHandle);
        }
        else
        {
            // Mapping is not supported...probably an old kernel
            return NOT_SUPPORTED;
        }
    }
    return UNKNOWN;
}

static eReturnValues set_Device_Partition_Info(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    char*         blockHandle    = device->os_info.name;
    if (device->os_info.secondHandleValid && !is_Block_Device_Handle(blockHandle))
    {
        blockHandle = device->os_info.secondName;
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
                        printf("found system disk\n");
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

#define LIN_MAX_HANDLE_LENGTH 16
static eReturnValues get_Lin_Device(const char* filename, tDevice* device)
{
    char*         deviceHandle = M_NULLPTR;
    eReturnValues ret          = SUCCESS;
    int           k            = 0;
#if defined(_DEBUG)
    printf("%s: Getting device for %s\n", __FUNCTION__, filename);
#endif

    if (is_Block_Device_Handle(filename))
    {
        // printf("\tBlock handle found, mapping...\n");
        char*         genHandle   = M_NULLPTR;
        char*         blockHandle = M_NULLPTR;
        eReturnValues mapResult   = map_Block_To_Generic_Handle(filename, &genHandle, &blockHandle);
#if defined(_DEBUG)
        printf("sg = %s\tsd = %s\n", genHandle, blockHandle);
#endif
        if (mapResult == SUCCESS && genHandle != M_NULLPTR)
        {
            deviceHandle = M_REINTERPRET_CAST(char*, safe_calloc(LIN_MAX_HANDLE_LENGTH, sizeof(char)));
            // printf("Changing filename to SG device....\n");
            if (is_SCSI_Generic_Handle(genHandle))
            {
                snprintf_err_handle(deviceHandle, LIN_MAX_HANDLE_LENGTH, "/dev/%s", genHandle);
            }
            else
            {
                snprintf_err_handle(deviceHandle, LIN_MAX_HANDLE_LENGTH, "/dev/bsg/%s", genHandle);
            }
#if defined(_DEBUG)
            printf("\tfilename = %s\n", deviceHandle);
#endif
        }
        else // If we can't map, let still try anyway.
        {
            if (0 != safe_strdup(&deviceHandle, filename))
            {
                safe_free(&genHandle);
                safe_free(&blockHandle);
                return MEMORY_FAILURE;
            }
        }
        safe_free(&genHandle);
        safe_free(&blockHandle);
    }
    else
    {
        if (0 != safe_strdup(&deviceHandle, filename))
        {
            return MEMORY_FAILURE;
        }
    }
#if defined(_DEBUG)
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
        set_Device_Partition_Info(device);
        safe_free(&deviceHandle);
        return ret;
    }
    // Add support for other flags.
    if ((device->os_info.fd >= 0) && (ret == SUCCESS))
    {
        if (is_NVMe_Handle(deviceHandle))
        {
#if !defined(DISABLE_NVME_PASSTHROUGH)
            // Do NVMe specific setup and enumeration
            device->drive_info.drive_type     = NVME_DRIVE;
            device->drive_info.interface_type = NVME_INTERFACE;
            int ioctlResult                   = ioctl(device->os_info.fd, NVME_IOCTL_ID);
            if (ioctlResult < 0)
            {
                perror("nvme_ioctl_id");
                safe_free(&deviceHandle);
                return FAILURE;
            }
            device->drive_info.namespaceID = C_CAST(uint32_t, ioctlResult);
            device->os_info.osType         = OS_LINUX;
            device->drive_info.media_type  = MEDIA_NVM;

            char* baseLink = basename(deviceHandle);
            // Now we will set up the device name, etc fields in the os_info structure.
            snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s", baseLink);
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s", baseLink);

            set_Device_Partition_Info(device);

            ret = fill_Drive_Info_Data(device);
#    if defined(_DEBUG)
            printf("\nsg helper-nvmedev\n");
            printf("Drive type: %d\n", device->drive_info.drive_type);
            printf("Interface type: %d\n", device->drive_info.interface_type);
            printf("Media type: %d\n", device->drive_info.media_type);
#    endif // DEBUG
#else      // DISABLE_NVME_PASSTHROUGH
#    if defined(_DEBUG)
            printf("\nsg helper-nvmedev --  NVME Passthrough disabled, device not supported\n");
#    endif // DEBUG
            return NOT_SUPPORTED; // return not supported since NVMe-passthrough is disabled
#endif     // DISABLE_NVME_PASSTHROUGH
        }
        else // not an NVMe handle
        {
#if defined(_DEBUG)
            printf("Getting SG SCSI address\n");
#endif
            struct sg_scsi_id hctlInfo;
            safe_memset(&hctlInfo, sizeof(struct sg_scsi_id), 0, sizeof(struct sg_scsi_id));
            errno       = 0; // clear before calling this ioctl
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
                // http://www.faqs.org/docs/Linux-HOWTO/SCSI-Generic-HOWTO.html#IDDRIVER
                device->os_info.sgDriverVersion.driverVersionValid = true;
                device->os_info.sgDriverVersion.majorVersion       = C_CAST(uint8_t, k / 10000);
                device->os_info.sgDriverVersion.minorVersion =
                    C_CAST(uint8_t, (k - (device->os_info.sgDriverVersion.majorVersion * 10000)) / 100);
                device->os_info.sgDriverVersion.revision =
                    C_CAST(uint8_t, k - (device->os_info.sgDriverVersion.majorVersion * 10000) -
                                        (device->os_info.sgDriverVersion.minorVersion * 100));

                // set the OS Type
                device->os_info.osType = OS_LINUX;

                // set scsi interface and scsi drive until we know otherwise
                device->drive_info.drive_type     = SCSI_DRIVE;
                device->drive_info.interface_type = SCSI_INTERFACE;
                device->drive_info.media_type     = MEDIA_HDD;
                // now have the device information fields set
#if defined(_DEBUG)
                printf("Setting interface, drive type, secondary handles\n");
#endif
                set_Device_Fields_From_Handle(deviceHandle, device);
                setup_Passthrough_Hacks_By_ID(device);
                set_Device_Partition_Info(device);

#if defined(_DEBUG)
                printf("name = %s\t friendly name = %s\n2ndName = %s\t2ndFName = %s\n", device->os_info.name,
                       device->os_info.friendlyName, device->os_info.secondName, device->os_info.secondFriendlyName);
                printf("h:c:t:l = %u:%u:%u:%u\n", device->os_info.scsiAddress.host, device->os_info.scsiAddress.channel,
                       device->os_info.scsiAddress.target, device->os_info.scsiAddress.lun);

                printf("SG driver version = %u.%u.%u\n", device->os_info.sgDriverVersion.majorVersion,
                       device->os_info.sgDriverVersion.minorVersion, device->os_info.sgDriverVersion.revision);
#endif

                // Fill in all the device info.
                // this code to set up passthrough commands for USB and IEEE1394 has been removed for now to match
                // Windows functionality. Need better intelligence than this. Some of these old pass-through types issue
                // vendor specific op codes that could be misinterpretted on some devices.
                //              if (device->drive_info.interface_type == USB_INTERFACE ||
                //              device->drive_info.interface_type == IEEE_1394_INTERFACE)
                //              {
                //                  set_ATA_Passthrough_Type_By_PID_and_VID(device);
                //              }

                ret = fill_Drive_Info_Data(device);

#if defined(_DEBUG)
                printf("\nsg helper\n");
                printf("Drive type: %d\n", device->drive_info.drive_type);
                printf("Interface type: %d\n", device->drive_info.interface_type);
                printf("Media type: %d\n", device->drive_info.media_type);
#endif
            }
        }
    }
    safe_free(&deviceHandle);
    return ret;
}

eReturnValues get_Device(const char* filename, tDevice* device)
{
#if defined(ENABLE_CISS)
    if (is_Supported_ciss_Dev(filename))
    {
        return get_CISS_RAID_Device(filename, device);
    }
#endif // ENABLE_CISS
    return get_Lin_Device(filename, device);
}

// http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_reset.html
// sgResetType should be one of the values from the link above...so bus or device...controller will work but that
// shouldn't be done ever.
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
        // poll for reset completion
#if defined(_DEBUG)
        printf("Reset in progress, polling for completion!\n");
#endif
        resetType = SG_SCSI_RESET_NOTHING;
        while (errno == EBUSY)
        {
            ioctlResult = ioctl(fd, SG_SCSI_RESET, &resetType);
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
        return sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
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
#if defined(SG_DXFER_UNKNOWN)
        io_hdr.dxfer_direction =
            SG_DXFER_UNKNOWN; // using unknown because SG_DXFER_TO_FROM_DEV is described as something different to use
                              // with indirect IO as it copied into kernel buffers before transfer.
#else
        io_hdr.dxfer_direction = -5; // this is what this is defined as in sg.h
#endif // SG_DXFER_UNKNOWN
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
    if (ioctlResult < 0)
    {
        scsiIoCtx->device->os_info.last_error = errno;
        ret                                   = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(errno);
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
#if defined(TASK_ABORTED)
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
                // Special case for MegaRAID and controllers based on MegaRAID.
                // These controllers block the command and set "Internal Adapter Error" with no other information
                // available.
                // TODO: Need to test and see if SAT passthrough trusted send/receive are also blocked to add them to
                // this case. -TJE
                if (io_hdr.host_status == OPENSEA_SG_ERR_DID_ERROR &&
                    (scsiIoCtx->cdb[OPERATION_CODE] == SECURITY_PROTOCOL_IN ||
                     scsiIoCtx->cdb[OPERATION_CODE] == SECURITY_PROTOCOL_OUT))
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

static int nvme_filter(const struct dirent* entry)
{
    int nvmeHandle = strncmp("nvme", entry->d_name, 4);
    if (nvmeHandle != 0)
    {
        return !nvmeHandle;
    }
    if (safe_strlen(entry->d_name) > 5)
    {
        char* partition = strpbrk(entry->d_name, "p");
        if (partition != M_NULLPTR)
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
eReturnValues get_Device_Count(uint32_t* numberOfDevices, uint64_t flags)
{
    uint32_t        num_devs                                      = UINT32_C(0);
    uint32_t        num_nvme_devs                                 = UINT32_C(0);
    int             scandirresult                                 = 0;
    struct dirent** namelist                                      = M_NULLPTR;
    struct dirent** nvmenamelist                                  = M_NULLPTR;
    int (*sortFunc)(const struct dirent**, const struct dirent**) = &alphasort;
#if defined(_GNU_SOURCE)
    sortFunc = &versionsort; // use versionsort instead when available with _GNU_SOURCE
#endif

    scandirresult = scandir("/dev", &namelist, sg_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_devs = C_CAST(uint32_t, scandirresult);
    }
    if (num_devs == 0)
    {
        safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));
        // check for SD devices
        scandirresult = scandir("/dev", &namelist, sd_filter, sortFunc);
        if (scandirresult >= 0)
        {
            num_devs = C_CAST(uint32_t, scandirresult);
        }
    }
#if defined(ENABLE_CISS)
    // build a list of devices to scan for physical drives behind a RAID
    ptrRaidHandleToScan raidHandleList      = M_NULLPTR;
    ptrRaidHandleToScan beginRaidHandleList = raidHandleList;
    raidTypeHint        raidHint;
    safe_memset(&raidHint, sizeof(raidTypeHint), 0, sizeof(raidTypeHint));
    // need to check if existing sg/sd handles are attached to hpsa or smartpqi drives in addition to /dev/cciss devices
    struct dirent** ccisslist;
    uint32_t        num_ccissdevs = UINT32_C(0);
    scandirresult                 = scandir("/dev", &ccisslist, ciss_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_ccissdevs = C_CAST(uint32_t, scandirresult);
    }
    if (num_ccissdevs > 0)
    {
        raidHint.cissRAID = true; // true as all the following will be CISS devices
        for (uint32_t cissIter = UINT32_C(0); cissIter < num_ccissdevs; ++cissIter)
        {
            raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList,
                                                            ccisslist[cissIter]->d_name, raidHint);
            if (!beginRaidHandleList)
            {
                beginRaidHandleList = raidHandleList;
            }
            // now free this as we are done with it.
            safe_free_dirent(&ccisslist[cissIter]);
        }
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &ccisslist));
    for (uint32_t iter = UINT32_C(0); iter < num_devs; ++iter)
    {
        // before freeing, check if any of these handles may be a RAID handle
        sysFSLowLevelDeviceInfo sysFsInfo;
        safe_memset(&sysFsInfo, sizeof(sysFSLowLevelDeviceInfo), 0, sizeof(sysFSLowLevelDeviceInfo));
        get_Linux_SYS_FS_Info(namelist[iter]->d_name, &sysFsInfo);

        safe_memset(&raidHint, sizeof(raidTypeHint), 0,
                    sizeof(raidTypeHint)); // clear out before checking driver name since this will be expanded to check
                                           // other drivers in the future
#    if defined(ENABLE_CISS)
        if (sysFsInfo.scsiDevType == PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE)
        {
            if (strcmp(sysFsInfo.driver_info.driverName, "hpsa") == 0 ||
                strcmp(sysFsInfo.driver_info.driverName, "smartpqi") == 0)
            {
                raidHint.cissRAID = true;
                // this handle is a /dev/sg handle with the hpsa or smartpqi driver, so we can scan for cciss devices
                raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList,
                                                                namelist[iter]->d_name, raidHint);
                if (!beginRaidHandleList)
                {
                    beginRaidHandleList = raidHandleList;
                }
            }
        }
#    endif // ENABLE_CISS
    }
#endif // ENABLE_CISS

    // free the list of names to not leak memory
    for (uint32_t iter = UINT32_C(0); iter < num_devs; ++iter)
    {
        safe_free_dirent(&namelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));
    // add nvme devices to the list
    scandirresult = scandir("/dev", &nvmenamelist, nvme_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_nvme_devs = C_CAST(uint32_t, scandirresult);
    }
    // free the nvmenamelist to not leak memory
    for (uint32_t iter = UINT32_C(0); iter < num_nvme_devs; ++iter)
    {
        safe_free_dirent(&nvmenamelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &nvmenamelist));

    *numberOfDevices = num_devs + num_nvme_devs;

#if defined(ENABLE_CISS)
    uint32_t      cissDeviceCount = UINT32_C(0);
    eReturnValues cissRet         = get_CISS_RAID_Device_Count(&cissDeviceCount, flags, &beginRaidHandleList);
    if (cissRet == SUCCESS)
    {
        *numberOfDevices += cissDeviceCount;
    }
#endif // ENABLE_CISS

    // Clean up RAID handle list
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
eReturnValues get_Device_List(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, name, 80); // Because get device needs char
    int      fd = -1;
    tDevice* d  = M_NULLPTR;
#if defined(DEGUG_SCAN_TIME)
    DECLARE_SEATIMER(getDeviceTimer);
    DECLARE_SEATIMER(getDeviceListTimer);
#endif
    ptrRaidHandleToScan raidHandleList      = M_NULLPTR;
    ptrRaidHandleToScan beginRaidHandleList = raidHandleList;
    raidTypeHint        raidHint;
    safe_memset(&raidHint, sizeof(raidTypeHint), 0, sizeof(raidTypeHint));

    int      scandirresult = 0;
    uint32_t num_sg_devs   = UINT32_C(0);
    uint32_t num_sd_devs   = UINT32_C(0);
    uint32_t num_nvme_devs = UINT32_C(0);

    struct dirent** namelist;
    struct dirent** nvmenamelist;

    int (*sortFunc)(const struct dirent**, const struct dirent**) = &alphasort;
#if defined(_GNU_SOURCE)
    sortFunc = &versionsort; // use versionsort instead when available with _GNU_SOURCE
#endif

    scandirresult = scandir("/dev", &namelist, sg_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_sg_devs = C_CAST(uint32_t, scandirresult);
    }
    if (num_sg_devs == 0)
    {
        safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));
        // check for SD devices
        scandirresult = scandir("/dev", &namelist, sd_filter, sortFunc);
        if (scandirresult >= 0)
        {
            num_sd_devs = C_CAST(uint32_t, scandirresult);
        }
    }
    // add nvme devices to the list
    scandirresult = scandir("/dev", &nvmenamelist, nvme_filter, sortFunc);
    if (scandirresult >= 0)
    {
        num_nvme_devs = C_CAST(uint32_t, scandirresult);
    }
    uint32_t totalDevs = num_sg_devs + num_sd_devs + num_nvme_devs;

    char**   devs = M_REINTERPRET_CAST(char**, safe_calloc(totalDevs + 1, sizeof(char*)));
    uint32_t i    = UINT32_C(0);
    uint32_t j    = UINT32_C(0);
    // add sg/sd devices to the list
    for (; i < (num_sg_devs + num_sd_devs); i++)
    {
        size_t handleSize = (safe_strlen("/dev/") + safe_strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i]           = M_REINTERPRET_CAST(char*, safe_malloc(handleSize));
        snprintf_err_handle(devs[i], handleSize, "/dev/%s", namelist[i]->d_name);
        safe_free_dirent(&namelist[i]);
    }
    // add nvme devices to the list
    for (j = 0; i < totalDevs && j < num_nvme_devs; i++, j++)
    {
        size_t handleSize = (safe_strlen("/dev/") + safe_strlen(nvmenamelist[j]->d_name) + 1) * sizeof(char);
        devs[i]           = M_REINTERPRET_CAST(char*, safe_malloc(handleSize));
        snprintf_err_handle(devs[i], handleSize, "/dev/%s", nvmenamelist[j]->d_name);
        safe_free_dirent(&nvmenamelist[j]);
    }
    devs[i] = M_NULLPTR; // Added this so the for loop down doesn't cause a segmentation fault.
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &nvmenamelist));

    struct dirent** ccisslist;
    int             num_ccissdevs = scandir("/dev", &ccisslist, ciss_filter, sortFunc);
    if (num_ccissdevs > 0)
    {
        raidHint.cissRAID = true; // true as all the following will be CISS devices
        for (int cissIter = 0; cissIter < num_ccissdevs; ++cissIter)
        {
            raidHandleList = add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList,
                                                            ccisslist[cissIter]->d_name, raidHint);
            if (!beginRaidHandleList)
            {
                beginRaidHandleList = raidHandleList;
            }
            // now free this as we are done with it.
            safe_free_dirent(&ccisslist[cissIter]);
        }
        safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &ccisslist));
    }

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
        for (driveNumber = 0;
             (driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < totalDevs) && (found < numberOfDevices); ++driveNumber)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, sizeof(name), 0, sizeof(name)); // clear name before reusing it
            snprintf_err_handle(name, sizeof(name), "%s", devs[driveNumber]);
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
                else
                {
                    safe_memset(&raidHint, sizeof(raidTypeHint), 0, sizeof(raidTypeHint));
#if defined(ENABLE_CISS)
                    // check that we are only scanning a SCSI controller for RAID to avoid duplicates
                    // NOTE: If num_sg_devs == 0, then the sg driver is missing and SCSI controllers do not get /dev/sd
                    // handles, so we will skip this check in this special case.
                    //       This special case exists because sometimes a kernel is built and deployed without the SG
                    //       driver enabled, but we still want to detect RAID devices, so we don't want to skip
                    //       enumerating a RAID when all we see are the logical RAID volumes -TJE
                    if (get_bit_range_uint8(d->drive_info.scsiVpdData.inquiryData[0], 4, 0) ==
                            PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE ||
                        num_sg_devs == 0)
                    {
                        if (strcmp(d->drive_info.driver_info.driverName, "hpsa") == 0 ||
                            strcmp(d->drive_info.driver_info.driverName, "smartpqi") == 0)
                        {
                            raidHint.cissRAID = true;
                            // this handle is a /dev/sg handle with the hpsa or smartpqi driver, so we can scan for
                            // cciss devices
                            raidHandleList =
                                add_RAID_Handle_If_Not_In_List(beginRaidHandleList, raidHandleList, name, raidHint);
                            if (!beginRaidHandleList)
                            {
                                beginRaidHandleList = raidHandleList;
                            }
                        }
                    }
#endif // ENABLE_CISS
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

#if defined(ENABLE_CISS)
        uint32_t cissDeviceCount = numberOfDevices - found;
        if (cissDeviceCount > 0)
        {
            eReturnValues cissRet = get_CISS_RAID_Device_List(
                &ptrToDeviceList[found], cissDeviceCount * sizeof(tDevice), ver, flags, &beginRaidHandleList);
            if (returnValue == SUCCESS && cissRet != SUCCESS)
            {
                // this will override the normal ret if it is already set to success with the CISS return value
                returnValue = cissRet;
            }
        }
#endif // ENABLE_CISS

        // Clean up RAID handle list
        delete_RAID_List(beginRaidHandleList);

#if defined(DEGUG_SCAN_TIME)
        stop_Timer(&getDeviceListTimer);
        printf("Time to get all device = %fms\n", get_Milli_Seconds(getDeviceListTimer));
#endif

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
    int retValue = 0;
    DISABLE_NONNULL_COMPARE
    if (dev != M_NULLPTR)
    {
        if (dev->os_info.cissDeviceData)
        {
            close_CISS_RAID_Device(dev);
        }
        else
        {
            retValue                = close(dev->os_info.fd);
            dev->os_info.last_error = errno;

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
    RESTORE_NONNULL_COMPARE
}

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues ret = SUCCESS; // NVME_SC_SUCCESS;//This defined value used to exist in some version of nvme.h but is
                                 // missing in nvme_ioctl.h...it was a value of zero, so this should be ok.
    DECLARE_SEATIMER(commandTimer);
    struct nvme_admin_cmd adminCmd;
    struct nvme_user_io nvmCmd; // it's possible that this is not defined in some funky early nvme kernel, but we don't
                                // see that today. This seems to be defined everywhere. -TJE
#    if defined(NVME_IOCTL_IO_CMD)
    struct nvme_passthru_cmd* passThroughCmd =
        (struct nvme_passthru_cmd*)&adminCmd; // setting a pointer since these are defined to be the same. No point in
                                              // allocating yet another structure. - TJE
#    endif                                    // NVME_IOCTL_IO_CMD

    int ioctlResult = 0;

    DISABLE_NONNULL_COMPARE
    if (nvmeIoCtx != M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

    switch (nvmeIoCtx->commandType)
    {
    case NVM_ADMIN_CMD:
        safe_memset(&adminCmd, sizeof(struct nvme_admin_cmd), 0, sizeof(struct nvme_admin_cmd));
        adminCmd.opcode       = nvmeIoCtx->cmd.adminCmd.opcode;
        adminCmd.flags        = nvmeIoCtx->cmd.adminCmd.flags;
        adminCmd.rsvd1        = nvmeIoCtx->cmd.adminCmd.rsvd1;
        adminCmd.nsid         = nvmeIoCtx->cmd.adminCmd.nsid;
        adminCmd.cdw2         = nvmeIoCtx->cmd.adminCmd.cdw2;
        adminCmd.cdw3         = nvmeIoCtx->cmd.adminCmd.cdw3;
        adminCmd.metadata     = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.metadata));
        adminCmd.addr         = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.adminCmd.addr));
        adminCmd.metadata_len = nvmeIoCtx->cmd.adminCmd.metadataLen;
        adminCmd.data_len     = nvmeIoCtx->dataSize;
        adminCmd.cdw10        = nvmeIoCtx->cmd.adminCmd.cdw10;
        adminCmd.cdw11        = nvmeIoCtx->cmd.adminCmd.cdw11;
        adminCmd.cdw12        = nvmeIoCtx->cmd.adminCmd.cdw12;
        adminCmd.cdw13        = nvmeIoCtx->cmd.adminCmd.cdw13;
        adminCmd.cdw14        = nvmeIoCtx->cmd.adminCmd.cdw14;
        adminCmd.cdw15        = nvmeIoCtx->cmd.adminCmd.cdw15;
        adminCmd.timeout_ms   = nvmeIoCtx->timeout ? nvmeIoCtx->timeout * 1000 : 15000;
        start_Timer(&commandTimer);
        DISABLE_WARNING_SIGN_CONVERSION
        ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_ADMIN_CMD, &adminCmd);
        RESTORE_WARNING_SIGN_CONVERSION
        stop_Timer(&commandTimer);
        if (ioctlResult < 0)
        {
            nvmeIoCtx->device->os_info.last_error = errno;
            ret                                   = OS_PASSTHROUGH_FAILURE;
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
            nvmeIoCtx->commandCompletionData.dw3Valid        = true;
            nvmeIoCtx->commandCompletionData.dw0Valid        = true;
            nvmeIoCtx->commandCompletionData.statusAndCID    = C_CAST(uint32_t, ioctlResult)
                                                            << 17; // shift into place since we don't get the phase tag
                                                                   // or command ID bits and these are the status field
        }
        break;
    case NVM_CMD:
        // check opcode to perform the correct IOCTL
        switch (nvmeIoCtx->cmd.nvmCmd.opcode)
        {
        case NVME_CMD_READ:
        case NVME_CMD_WRITE:
            // use user IO cmd structure and SUBMIT_IO IOCTL
            safe_memset(&nvmCmd, sizeof(nvmCmd), 0, sizeof(nvmCmd));
            nvmCmd.opcode   = nvmeIoCtx->cmd.nvmCmd.opcode;
            nvmCmd.flags    = nvmeIoCtx->cmd.nvmCmd.flags;
            nvmCmd.control  = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw12);
            nvmCmd.nblocks  = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw12);
            nvmCmd.rsvd     = RESERVED;
            nvmCmd.metadata = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.nvmCmd.metadata));
            nvmCmd.addr     = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->ptrData));
            nvmCmd.slba     = M_DWordsTo8ByteValue(nvmeIoCtx->cmd.nvmCmd.cdw11, nvmeIoCtx->cmd.nvmCmd.cdw10);
            nvmCmd.dsmgmt   = nvmeIoCtx->cmd.nvmCmd.cdw13;
            nvmCmd.reftag   = nvmeIoCtx->cmd.nvmCmd.cdw14;
            nvmCmd.apptag   = M_Word0(nvmeIoCtx->cmd.nvmCmd.cdw15);
            nvmCmd.appmask  = M_Word1(nvmeIoCtx->cmd.nvmCmd.cdw15);
            start_Timer(&commandTimer);
            DISABLE_WARNING_SIGN_CONVERSION
            ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_SUBMIT_IO, &nvmCmd);
            RESTORE_WARNING_SIGN_CONVERSION
            stop_Timer(&commandTimer);
            if (ioctlResult < 0)
            {
                nvmeIoCtx->device->os_info.last_error = errno;
                ret                                   = OS_PASSTHROUGH_FAILURE;
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
                // TODO: How do we set the command specific result on read/write?
                nvmeIoCtx->commandCompletionData.statusAndCID =
                    C_CAST(uint32_t, ioctlResult) << 17; // shift into place since we don't get the phase tag or command
                                                         // ID bits and these are the status field
            }
            break;
        default:
#    if defined(NVME_IOCTL_IO_CMD)
            // use the generic passthrough command structure and IO_CMD
            safe_memset(passThroughCmd, sizeof(struct nvme_passthru_cmd), 0, sizeof(struct nvme_passthru_cmd));
            passThroughCmd->opcode   = nvmeIoCtx->cmd.nvmCmd.opcode;
            passThroughCmd->flags    = nvmeIoCtx->cmd.nvmCmd.flags;
            passThroughCmd->rsvd1    = RESERVED;
            passThroughCmd->nsid     = nvmeIoCtx->cmd.nvmCmd.nsid;
            passThroughCmd->cdw2     = nvmeIoCtx->cmd.nvmCmd.cdw2;
            passThroughCmd->cdw3     = nvmeIoCtx->cmd.nvmCmd.cdw3;
            passThroughCmd->metadata = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->cmd.nvmCmd.metadata));
            passThroughCmd->addr     = C_CAST(uint64_t, C_CAST(uintptr_t, nvmeIoCtx->ptrData));
            passThroughCmd->metadata_len =
                M_DoubleWord0(nvmeIoCtx->cmd.nvmCmd.prp2);  // guessing here since I don't really know - TJE
            passThroughCmd->data_len = nvmeIoCtx->dataSize; // Or do I use the other PRP2 data? Not sure - TJE
                                                            // //M_DWord1(nvmeIoCtx->cmd.nvmCmd.prp2);//guessing here
                                                            // since I don't really know - TJE
            passThroughCmd->cdw10      = nvmeIoCtx->cmd.nvmCmd.cdw10;
            passThroughCmd->cdw11      = nvmeIoCtx->cmd.nvmCmd.cdw11;
            passThroughCmd->cdw12      = nvmeIoCtx->cmd.nvmCmd.cdw12;
            passThroughCmd->cdw13      = nvmeIoCtx->cmd.nvmCmd.cdw13;
            passThroughCmd->cdw14      = nvmeIoCtx->cmd.nvmCmd.cdw14;
            passThroughCmd->cdw15      = nvmeIoCtx->cmd.nvmCmd.cdw15;
            passThroughCmd->timeout_ms = nvmeIoCtx->timeout
                                             ? nvmeIoCtx->timeout * 1000
                                             : 15000; // timeout is in seconds, so converting to milliseconds
            start_Timer(&commandTimer);
            DISABLE_WARNING_SIGN_CONVERSION
            ioctlResult = ioctl(nvmeIoCtx->device->os_info.fd, NVME_IOCTL_IO_CMD, passThroughCmd);
            RESTORE_WARNING_SIGN_CONVERSION
            stop_Timer(&commandTimer);
            if (ioctlResult < 0)
            {
                nvmeIoCtx->device->os_info.last_error = errno;
                ret                                   = OS_PASSTHROUGH_FAILURE;
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
                nvmeIoCtx->commandCompletionData.dw3Valid        = true;
                nvmeIoCtx->commandCompletionData.dw0Valid        = true;
                nvmeIoCtx->commandCompletionData.statusAndCID =
                    C_CAST(uint32_t, ioctlResult) << 17; // shift into place since we don't get the phase tag or command
                                                         // ID bits and these are the status field
            }
#    else
            ret = OS_COMMAND_NOT_AVAILABLE;
#    endif // NVME_IOCTL_IO_CMD
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
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

static eReturnValues linux_NVMe_Reset(tDevice* device, bool subsystemReset)
{
#if !defined(DISABLE_NVME_PASSTHROUGH) && defined(NVME_IOCTL_SUBSYS_RESET) && defined(NVME_IOCTL_RESET)
    // Can only do a reset on a controller handle. Need to get the controller handle if this is a namespace handle!!!
    eReturnValues ret           = OS_PASSTHROUGH_FAILURE;
    int           handleToReset = device->os_info.fd;
    DECLARE_SEATIMER(commandTimer);
    int  ioRes                  = 0;
    bool openedControllerHandle = false; // used so we can close the handle at the end.
    // Need to make sure the handle we use to issue the reset is a controller handle and not a namespace handle.
    char* endptr = M_NULLPTR;
    char* handle = strstr(&device->os_info.name[0], "/dev/nvme");
    if (handle)
    {
        handle += safe_strlen("/dev/nvme");
    }
    else
    {
        return FAILURE;
    }
    unsigned long controller  = 0UL;
    unsigned long namespaceID = 0UL;
    if (0 != safe_strtoul(&controller, handle, &endptr, BASE_10_DECIMAL))
    {
        return FAILURE;
    }
    if (endptr && safe_strlen(endptr) > SIZE_T_C(1) && endptr[0] == 'n')
    {
        handle += 1;
        if (0 != safe_strtoul(&namespaceID, handle, &endptr, BASE_10_DECIMAL))
        {
            return FAILURE;
        }
    }
    else
    {
        return FAILURE;
    }
    // found a namespace. Need to open a controller handle instead and use it.
    DECLARE_ZERO_INIT_ARRAY(char, controllerHandle, 40);
    snprintf_err_handle(controllerHandle, 40, "/dev/nvme%lu", controller);
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
    openedControllerHandle     = true;
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
    device->drive_info.lastCommandTimeNanoSeconds             = get_Nano_Seconds(commandTimer);
    device->drive_info.lastNVMeResult.lastNVMeStatus          = 0;
    device->drive_info.lastNVMeResult.lastNVMeCommandSpecific = 0;
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    if (ioRes < 0)
    {
        // failed!
        device->os_info.last_error = errno;
        if (device->deviceVerbosity > VERBOSITY_COMMAND_VERBOSE && device->os_info.last_error != 0)
        {
            printf("Error: ");
            print_Errno_To_Screen(errno);
        }
    }
    else
    {
        // success!
        ret = SUCCESS;
    }
    if (openedControllerHandle)
    {
        // close the controller handle we opened in this function
        close(handleToReset);
    }
    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Reset(tDevice* device)
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
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Subsystem_Reset(tDevice* device)
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
#else  // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

// to be used with a deep scan???
// fd must be a controller handle
#if defined(_DEBUG)
// making this a debug flagged call since it is currently an unused function. We should look into how to appropriately
// support this.-TJE
static eReturnValues nvme_Namespace_Rescan(int fd)
{
#    if defined(NVME_IOCTL_RESCAN) // This IOCTL is not available on older kernels, which is why this is checked like
                                   // this - TJE
    eReturnValues ret   = OS_PASSTHROUGH_FAILURE;
    int           ioRes = ioctl(fd, NVME_IOCTL_RESCAN);
    if (ioRes < 0)
    {
        // failed!
        perror("NVMe Rescan");
    }
    else
    {
        // success!
        ret = SUCCESS;
    }
    return ret;
#    else
    M_USE_UNUSED(fd);
    return OS_COMMAND_NOT_AVAILABLE;
#    endif
}
#endif //_DEBUG

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
#else // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif
}

// This is used to open device->os_info.fd2 which is where we will store
// a /dev/sd handle which is a block device handle for SCSI devices.
// This will do nothing on NVMe as it is not needed. - TJE
static eReturnValues open_fd2(tDevice* device)
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
    // BLKFLSBUF
    return NOT_SUPPORTED;
}

eReturnValues os_Lock_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    // Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    // disable O_NONBLOCK
    flags &= ~O_NONBLOCK;
    // Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

eReturnValues os_Unlock_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    // Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    // enable O_NONBLOCK
    flags |= O_NONBLOCK;
    // Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

eReturnValues os_Update_File_System_Cache(tDevice* device)
{
    eReturnValues ret        = SUCCESS;
    int*          fdToRescan = &device->os_info.fd;
#if defined(_DEBUG)
    printf("Updating file system cache\n");
#endif
    if (device->os_info.secondHandleValid && SUCCESS == open_fd2(device))
    {
#if defined(_DEBUG)
        printf("using fd2: %s\n", device->os_info.secondName);
#endif
        fdToRescan = &device->os_info.fd2;
    }

    // Now, call BLKRRPART
#if defined(_DEBUG)
    printf("Rescanning partition table\n");
#endif
    if (ioctl(*fdToRescan, BLKRRPART) < 0)
    {
#if defined(_DEBUG)
        printf("\tCould not update partition table\n");
#endif
        device->os_info.last_error = errno;
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
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
    // TODO: if BLKZEROOUT available, use this to write zeroes to begining and end of the drive???
    return NOT_SUPPORTED;
}

eReturnValues os_Unmount_File_Systems_On_Device(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    char*         blockHandle    = device->os_info.name;
    if (device->os_info.secondHandleValid && !is_Block_Device_Handle(blockHandle))
    {
        blockHandle = device->os_info.secondName;
    }
    partitionCount = get_Partition_Count(blockHandle);
#if defined(_DEBUG)
    printf("Partition count for %s = %d\n", blockHandle, partitionCount);
#endif
    if (partitionCount > 0)
    {
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
#if defined(_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    // Now that we have a name, unmount the file system
                    // Linux 2.1.116 added the umount2()
                    if (0 > umount2((parts + iter)->mntPath, MNT_FORCE))
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
            safe_free_spartition_info(&parts);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}

// This should be at the end of this file to undefine _GNU_SOURCE if this file manually enabled it
#if !defined(GNU_SOURCE_DEFINED_IN_SG_HELPER)
#    undef _GNU_SOURCE // NOLINT
#    undef GNU_SOURCE_DEFINED_IN_SG_HELPER
#endif
