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
//! Copyright (c) 2025-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "code_attributes.h"
#include "common_types.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //! \fn void free_Posix_Resolved_Filename(char** resolvedFilename)
    //! \brief Frees memory allocated for a resolved filename by posix_Resolve_Filename_Link.
    //! \param[in,out] resolvedFilename Pointer to the resolved filename to free. After freeing, it will be set to NULL.
    void free_Posix_Resolved_Filename(char** resolvedFilename);

    //! \fn eReturnValues posix_Resolve_Filename_Link(const char* filename, char** resolvedFilename)
    //! \brief Resolves a filename that may be a symlink to its actual target filename.
    //! \param[in] filename The filename to resolve. (Device handle to be opnened).
    //! \param[out] resolvedFilename Pointer to hold the resolved filename. Memory will be allocated inside the function
    //! and must be freed by the caller using safe_free.
    //! \return eReturnValues enum indicating success or failure reason.
    M_NODISCARD eReturnValues posix_Resolve_Filename_Link(const char* filename, char** resolvedFilename);

    //! \enum ePosixHandleFlags
    //! \brief Flags to specify desired behavior when opening a POSIX device handle.
    typedef enum ePosixHandleFlags
    {
        POSIX_HANDLE_FLAGS_DEFAULT = 0, //!< No special flags to request
        POSIX_HANDLE_FLAGS_REQUEST_EXCLUSIVE =
            1, //!< Request exclusive access (O_EXCL), but will open non-exclusive if exclusive fails
        POSIX_HANDLE_FLAGS_REQUIRE_EXCLUSIVE =
            2 //!< Require exclusive access (O_EXCL), fail if exclusive access cannot be obtained
    } ePosixHandleFlags;

    //! \fn eReturnValues posix_Get_Device_Handle(const char* deviceHandle, int *fd, ePosixHandleFlags
    //! *requestedHandleFlags, int otherOSFlags) \brief Opens a device handle on a POSIX system. Uses O_RDWR and
    //! O_NONBLOCK flags by default. \param[in] deviceHandle The device handle to open. Recommend using
    //! posix_Resolve_Filename_Link to resolve any symlinks first. \param[out] fd Pointer to integer to hold the file
    //! descriptor. \param[in,out] requestedHandleFlags Pointer to ePosixHandleFlags enum to specify desired handle
    //! flags. Can be NULL. If non-NULL, on return it will hold actual opened result. \param[in] otherOSFlags Additional
    //! OS-specific flags to use when opening the handle. OR'd with O_RDWR and O_NONBLOCK. \return eReturnValues enum
    //! indicating success or failure reason.
    M_NODISCARD eReturnValues posix_Get_Device_Handle(const char*        deviceHandle,
                                                      int*               fd,
                                                      ePosixHandleFlags* requestedHandleFlags,
                                                      int                otherOSFlags);

#if defined(__cplusplus)
}
#endif
