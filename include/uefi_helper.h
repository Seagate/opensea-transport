// SPDX-License-Identifier: MPL-2.0

//! \file uefi_helper.h
//! \brief Definitions and function calls (and includes) specific to UEFI for ATA, SCSI, and NVMe passthrough
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2017-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "nvme_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    // This is the maximum timeout a command can use in UEFI...which is nearly infinite to begin with
    // NOTE: UEFI also supports an infinite timeout, but that is checked in a separate function
#define UEFI_MAX_CMD_TIMEOUT_SECONDS                                                                                   \
    UINT32_MAX // Technically, max seconds is 18446744074, but I don't want to switch to a 64bit for the timeout.
               // Anything with this value will round up to infinite in UEFI...where a timeout this long may as well be
               // infinite


#if defined(__cplusplus)
}
#endif
