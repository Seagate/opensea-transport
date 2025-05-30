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
#include "io_utils.h"
#include "memory_safety.h"

#include "common_public.h"

#include <sys/mount.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/ucred.h>

#if defined(__NetBSD__)
#    include <sys/statvfs.h>
#endif // __NetBSD__

#pragma once

#if defined(MNAMELEN)
#    define PART_INFO_NAME_LENGTH (MNAMELEN)
#    define PART_INFO_PATH_LENGTH (MNAMELEN)
#else
// 90 seems to be a common size for this
#    define PART_INFO_NAME_LENGTH (90)
#    define PART_INFO_PATH_LENGTH (90)
#endif // MNAMELEN
#if defined(MFSNAMELEN)
#    define PART_INFO_TYPE_LENGTH MFSNAMELEN
#else
#    define PART_INFO_TYPE_LENGTH (16)
#endif // MFSNAMELEN

typedef struct s_spartitionInfo
{
    char fsName[PART_INFO_NAME_LENGTH];
    char mntPath[PART_INFO_PATH_LENGTH];
    char mntType[PART_INFO_TYPE_LENGTH];
} spartitionInfo, *ptrsPartitionInfo;

static M_INLINE void safe_free_spartioninfo(spartitionInfo** partinfo)
{
    safe_free_core(M_REINTERPRET_CAST(void**, partinfo));
}

#if defined(__NetBSD__)
static M_INLINE void safe_free_statfs(struct statvfs** fs)
#else
static M_INLINE void safe_free_statfs(struct statfs** fs)
#endif // __NetBSD__
{
    safe_free_core(M_REINTERPRET_CAST(void**, fs));
}

M_NULL_TERM_STRING(1)
M_PARAM_RO(1)
M_NONNULL_PARAM_LIST(1)
int get_BSD_Partition_Count(const char* blockDeviceName);

M_NULL_TERM_STRING(1)
M_PARAM_RO(1)
M_NONNULL_PARAM_LIST(1, 2)
M_PARAM_WO(2)
eReturnValues get_BSD_Partition_List(const char* blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount);

M_NONNULL_PARAM_LIST(1)
M_PARAM_RW(1)
eReturnValues set_BSD_Device_Partition_Info(tDevice* device);

M_NONNULL_PARAM_LIST(1)
M_PARAM_RW(1)
eReturnValues bsd_Unmount_From_Matching_Dev(tDevice* device);
