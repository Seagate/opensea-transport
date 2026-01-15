// SPDX-License-Identifier: MPL-2.0

//! \file posix_common_lowlevel.h
//! \brief Declares common POSIX functions used across Unix/Unix-like OS's to reduce
//! code duplication for common low-level operations.
//! \details This file is meant to hold common POSIX functions used across Unix/Unix-like OS's.
//! Examples: resolving filename files for device handle, a generic scandir implementation to find handles, etc
//! all belong here to reduce code duplication across multiple OS implementations.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

// NOTE: In general, this file should be free of #ifdef as much as possible since this is meant to be generic/common
// POSIX code. OS unique methods should go in their respective OS implementation files when possible. If functionality
// is common or similar but vary by system, they should be in their own file (like reading partition info is similar on
// many OS's, but the API calls differ slightly, so that belongs in its own file).

#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "memory_safety.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "posix_common_lowlevel.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void free_Posix_Resolved_Filename(char** resolvedFilename)
{
    safe_free(resolvedFilename);
}

eReturnValues posix_Resolve_Filename_Link(const char* filename, char** resolvedFilename)
{
    struct stat handleStat;
    safe_memset(&handleStat, sizeof(struct stat), 0, sizeof(struct stat));
    if (lstat(filename, &handleStat) != 0)
    {
        errno_t err = errno;
        print_str("lstat failure\n");
        printf("Error: ");
        print_Errno_To_Screen(err);
        if (err == EACCES)
        {
            return PERMISSION_DENIED;
        }
        else if (err == ENOENT || err == ENODEV)
        {
            return DEVICE_INVALID;
        }
        else
        {
            return FAILURE;
        }
    }

    if (S_ISLNK(handleStat.st_mode))
    {
        // The passed handle is a symlink. We need to resolve it to the real path.
        if (handleStat.st_size < 0)
        {
            return FAILURE;
        }
        ssize_t linkSize  = handleStat.st_size + SSIZE_T_C(1);
        *resolvedFilename = M_REINTERPRET_CAST(char*, safe_calloc(M_STATIC_CAST(size_t, linkSize), sizeof(char)));
        if (*resolvedFilename == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        ssize_t readSize = readlink(filename, *resolvedFilename, M_STATIC_CAST(size_t, linkSize));
        if (readSize < 0 || readSize >= linkSize)
        {
            safe_free(resolvedFilename);
            return FAILURE;
        }
        (*resolvedFilename)[readSize] = '\0'; // Null terminate the string
    }
    else
    {
        if (0 != safe_strdup(resolvedFilename, filename))
        {
            return MEMORY_FAILURE;
        }
    }
    return SUCCESS;
}

enum
{
    POSIX_OPEN_ATTEMPTS_MAX = 2
};

eReturnValues posix_Get_Device_Handle(const char*        deviceHandle,
                                      int*               fd,
                                      ePosixHandleFlags* requestedHandleFlags,
                                      int                otherOSFlags)
{
    eReturnValues     ret             = SUCCESS;
    ePosixHandleFlags handleFlags     = POSIX_HANDLE_FLAGS_DEFAULT;
    int               openHandleFlags = O_RDWR | otherOSFlags;
    if (fd == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    if (*requestedHandleFlags)
    {
        handleFlags = *requestedHandleFlags;
    }

#if defined(O_NONBLOCK)
    openHandleFlags |= O_NONBLOCK;
#endif

#if defined(_DEBUG)
    printf("%s: Attempting to open %s\n", __FUNCTION__, deviceHandle);
#endif
    // Note: We are opening a READ/Write flag
    int attempts = 0;
    if (handleFlags == POSIX_HANDLE_FLAGS_REQUEST_EXCLUSIVE || handleFlags == POSIX_HANDLE_FLAGS_REQUIRE_EXCLUSIVE)
    {
        openHandleFlags |= O_EXCL;
    }
    do
    {
        ++attempts;
        *fd = open(deviceHandle, openHandleFlags);
        if (*fd < 0)
        {
            errno_t error = errno;
            if (handleFlags == POSIX_HANDLE_FLAGS_REQUEST_EXCLUSIVE)
            {
                openHandleFlags &= ~O_EXCL;
                continue;
            }
            print_str("Posix Get Device Handle Error: ");
            print_Errno_To_Screen(error);
            if (error == EACCES)
            {
                return PERMISSION_DENIED;
            }
            else if (error == EBUSY)
            {
                return DEVICE_BUSY;
            }
            else if (error == ENOENT || error == ENODEV)
            {
                return DEVICE_INVALID;
            }
            else
            {
                return FAILURE;
            }
        }
        else
        {
            break;
        }
    } while (attempts < POSIX_OPEN_ATTEMPTS_MAX);

    if (*requestedHandleFlags)
    {
        if (!(openHandleFlags & O_EXCL))
        {
            *requestedHandleFlags = POSIX_HANDLE_FLAGS_DEFAULT;
        }
    }
    return ret;
}
