// SPDX-License-Identifier: MPL-2.0

//! \file nix_mounts.h
//! \brief This file contains Unix/Unix-like system specific implementations for mounting and unmounting and reading
//! mounted file system information. It has some OS specific code, but is meant to be generic enough to be used
//! across multiple Unix-like OS's such as Linux, FreeBSD, NetBSD, OpenBSD, etc. Not all may be covered!!!
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2025-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "code_attributes.h"
#include "common_types.h"

#include "common_public.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    // Normalized mount entry the iterator yields (borrowed pointers; NOT owned)
    typedef struct MountEntry
    {
        const char* fsname; // device/resource (Linux: mnt_fsname; Solaris: mnt_special; BSD: f_mntfromname)
        const char* dir;    // mount point       (Linux: mnt_dir;    Solaris: mnt_mountp; BSD: f_mntonname)
        const char* type;   // filesystem type   (Linux: mnt_type;   Solaris: mnt_fstype; BSD: f_fstypename)
    } MountEntry;

    // Opaque per-platform iterator state
    typedef struct MountIter MountIter;

    // Pointer-based OS-agnostic partition info (caller owns strings)
    typedef struct s_spartitionInfo
    {
        char* fsName;  // device/resource (dup'd)
        char* mntPath; // mount point     (dup'd)
        char* mntType; // filesystem type (dup'd)
    } spartitionInfo, *ptrsPartitionInfo;

    // -------------------- Iterator functions --------------------
    // Opens the platform mount source and prepares iteration.
    int mount_iter_open(MountIter* it);

    // Retrieves the next mount entry (returns 0 while entries remain; -1 on end/error).
    int mount_iter_next(MountIter* it, MountEntry* out);

    // Closes the iterator and frees any internal resources.
    void mount_iter_close(MountIter* it);

    // -------------------- High-level helpers --------------------
    // Counts the number of mounts whose fsname contains blockDeviceName.
    int get_Partition_Count(const char* blockDeviceName);

    // Fills a caller-allocated array of spartitionInfo (size = listCount)
    // with mounts whose fsname contains blockDeviceName; strings are dup'd via safe_strdup.
    eReturnValues get_Partition_List(const char* blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount);

    // -------------------- Free helpers for pointer-based structures --------------------
    // Frees nested strings in a single partition info (fsName, mntPath, mntType).
    void free_spartitionInfo(spartitionInfo* pi);

    // Frees nested strings for each element in the list and frees the list itself.
    // Pass a pointer-to-array so the function can NULL it.
    void free_spartitionInfo_list(spartitionInfo** list, int count);

    M_PARAM_RW(1)
    M_NULL_TERM_STRING(2)
    M_PARAM_RO(2)
    eReturnValues set_Device_Partition_Info(fileSystemInfo* fsInfo, const char* blockDevice);

    /**
     * Unmount all partitions mounted from the given block device/resource.
     * Always attempts a forced unmount first (where supported), then retries
     * normal unmount if force fails (with short backoff).
     *
     * @param blockDevice  Device path or resource name (e.g., "/dev/sda", "/dev/dsk/c0t0d0s0").
     * @return SUCCESS if all matched mounts were unmounted,
     *         FAILURE if any unmount failed,
     *         MEMORY_FAILURE if allocation failed.
     */
    M_NULL_TERM_STRING(1)
    M_PARAM_RO(1)
    eReturnValues unmount_Partitions_From_Device(const char* blockDevice);

#if defined(__cplusplus)
}
#endif
