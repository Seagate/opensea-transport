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
// \file bsd_mount_info.c Handles the getmntinfo request for freebsd, netbsd, openbsd, etc

#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "memory_safety.h"
#include "type_conversion.h"

#include "bsd_mount_info.h"

#include <sys/mount.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/ucred.h>

int get_BSD_Partition_Count(const char* blockDeviceName)
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

// partitionInfoList is a pointer to the beginning of the list
// listCount is the number of these structures, which should be returned by get_BSD_Partition_Count
eReturnValues get_BSD_Partition_List(const char* blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount)
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
                        snprintf_err_handle((partitionInfoList + matchesFound)->mntType, PART_INFO_TYPE_LENGTH, "%s",
                                            (mountedFS + entIter)->f_fstypename);
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

eReturnValues set_BSD_Device_Partition_Info(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    partitionCount               = get_BSD_Partition_Count(device->os_info.name);
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
            if (SUCCESS == get_BSD_Partition_List(device->os_info.name, parts, partitionCount))
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

// eReturnValues reload_BSD_From_Matching_Dev(tDevice* device)
// {
//     eReturnValues ret            = SUCCESS;
//     int           partitionCount = 0;
//     partitionCount               = get_BSD_Partition_Count(device->os_info.name);
// #if defined(_DEBUG)
//     printf("Partition count for %s = %d\n", device->os_info.name, partitionCount);
// #endif
//     if (partitionCount > 0)
//     {
//         ptrsPartitionInfo parts =
//             M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
//         if (parts != M_NULLPTR)
//         {
//             if (SUCCESS == get_BSD_Partition_List(device->os_info.name, parts, partitionCount))
//             {
//                 int iter = 0;
//                 for (; iter < partitionCount; ++iter)
//                 {
//                     // since we found a partition, set the "has file system" bool to true
// #if defined(_DEBUG)
//                     printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
// #endif
//                     // Now that we have a name, unmount the file system
//                     // unmount is more line Linux unmount2
//                     //
//                     https://www.freebsd.org/cgi/man.cgi?query=unmount&sektion=2&apropos=0&manpath=FreeBSD+13.0-RELEASE
//                     if (0 > mount((parts + iter)->mntType, (parts + iter)->mntPath, MNT_RELOAD, M_NULLPTR))
//                     {
//                         ret                        = FAILURE;
//                         device->os_info.last_error = errno;
//                         if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
//                         {
//                             printf("Unable to reload mount info for %s: \n", (parts + iter)->mntPath);
//                             print_Errno_To_Screen(errno);
//                             printf("\n");
//                         }
//                     }
//                 }
//             }
//             safe_free_spartioninfo(&parts);
//         }
//         else
//         {
//             ret = MEMORY_FAILURE;
//         }
//     }
//     return ret;
// }

eReturnValues bsd_Unmount_From_Matching_Dev(tDevice* device)
{
    eReturnValues ret            = SUCCESS;
    int           partitionCount = 0;
    partitionCount               = get_BSD_Partition_Count(device->os_info.name);
#if defined(_DEBUG)
    printf("Partition count for %s = %d\n", device->os_info.name, partitionCount);
#endif
    if (partitionCount > 0)
    {
        ptrsPartitionInfo parts =
            M_REINTERPRET_CAST(ptrsPartitionInfo, safe_calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts != M_NULLPTR)
        {
            if (SUCCESS == get_BSD_Partition_List(device->os_info.name, parts, partitionCount))
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
