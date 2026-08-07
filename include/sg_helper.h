// SPDX-License-Identifier: MPL-2.0

//! \file sg_helper.h
//! \brief Defines the constants structures specific to Linux's SG interface.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_public.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    // \file sg_helper.h
    // \brief Defines the constants structures and function headers to help parse scsi drives.

#include <stdio.h>  // for printf
#include <stdlib.h> // for size_t types
#include <string.h> // For memset
#include <unistd.h>

#define SG_PHYSICAL_DRIVE  "/dev/sg"   // followed by a number
#define SD_PHYSICAL_DRIVE  "/dev/sd"   // followed by a letter
#define BSG_PHYSICAL_DRIVE "/dev/bsg/" // remaining part of the handle is h:c:t:l
#define OPENSEA_PATH_MAX   PATH_MAX

    // This is the maximum timeout a command can use in SG passthrough with linux...1193 hours
    // NOTE: SG also supports an infinite timeout, but that is checked in a separate function
#define SG_MAX_CMD_TIMEOUT_SECONDS 4294967

    // SG Driver status's since they are not available through standard includes we're using

#ifndef OPENSEA_SG_ERR_DRIVER_MASK
#    define OPENSEA_SG_ERR_DRIVER_MASK 0x0F
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_OK
#    define OPENSEA_SG_ERR_DRIVER_OK 0x00
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_BUSY
#    define OPENSEA_SG_ERR_DRIVER_BUSY 0x01
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_SOFT
#    define OPENSEA_SG_ERR_DRIVER_SOFT 0x02
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_MEDIA
#    define OPENSEA_SG_ERR_DRIVER_MEDIA 0x03
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_ERROR
#    define OPENSEA_SG_ERR_DRIVER_ERROR 0x04
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_INVALID
#    define OPENSEA_SG_ERR_DRIVER_INVALID 0x05
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_TIMEOUT
#    define OPENSEA_SG_ERR_DRIVER_TIMEOUT 0x06
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_HARD
#    define OPENSEA_SG_ERR_DRIVER_HARD 0x07
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_SENSE
#    define OPENSEA_SG_ERR_DRIVER_SENSE 0x08
#endif

// Driver error suggestions
#ifndef OPENSEA_SG_ERR_SUGGEST_MASK
#    define OPENSEA_SG_ERR_SUGGEST_MASK 0xF0
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_NONE
#    define OPENSEA_SG_ERR_SUGGEST_NONE 0x00
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_RETRY
#    define OPENSEA_SG_ERR_SUGGEST_RETRY 0x10
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_ABORT
#    define OPENSEA_SG_ERR_SUGGEST_ABORT 0x20
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_REMAP
#    define OPENSEA_SG_ERR_SUGGEST_REMAP 0x30
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_DIE
#    define OPENSEA_SG_ERR_SUGGEST_DIE 0x40
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_SENSE
#    define OPENSEA_SG_ERR_SUGGEST_SENSE 0x80
#endif

// Host errors
#ifndef OPENSEA_SG_ERR_DID_OK
#    define OPENSEA_SG_ERR_DID_OK 0x0000
#endif

#ifndef OPENSEA_SG_ERR_DID_NO_CONNECT
#    define OPENSEA_SG_ERR_DID_NO_CONNECT 0x0001
#endif

#ifndef OPENSEA_SG_ERR_DID_BUS_BUSY
#    define OPENSEA_SG_ERR_DID_BUS_BUSY 0x0002
#endif

#ifndef OPENSEA_SG_ERR_DID_TIME_OUT
#    define OPENSEA_SG_ERR_DID_TIME_OUT 0x0003
#endif

#ifndef OPENSEA_SG_ERR_DID_BAD_TARGET
#    define OPENSEA_SG_ERR_DID_BAD_TARGET 0x0004
#endif

#ifndef OPENSEA_SG_ERR_DID_ABORT
#    define OPENSEA_SG_ERR_DID_ABORT 0x0005
#endif

#ifndef OPENSEA_SG_ERR_DID_PARITY
#    define OPENSEA_SG_ERR_DID_PARITY 0x0006
#endif

#ifndef OPENSEA_SG_ERR_DID_ERROR
#    define OPENSEA_SG_ERR_DID_ERROR 0x0007
#endif

#ifndef OPENSEA_SG_ERR_DID_RESET
#    define OPENSEA_SG_ERR_DID_RESET 0x0008
#endif

#ifndef OPENSEA_SG_ERR_DID_BAD_INTR
#    define OPENSEA_SG_ERR_DID_BAD_INTR 0x0009
#endif

#ifndef OPENSEA_SG_ERR_DID_PASSTHROUGH
#    define OPENSEA_SG_ERR_DID_PASSTHROUGH 0x000A
#endif

#ifndef OPENSEA_SG_ERR_DID_SOFT_ERROR
#    define OPENSEA_SG_ERR_DID_SOFT_ERROR 0x000B
#endif

    // \fn send_sg_io(scsiIoCtx * scsiIoCtx)
    // \brief Function to send a SG_IO ioctl
    // \param scsiIoCtx
    M_PARAM_RW(1) eReturnValues send_sg_io(ScsiIoCtx* M_NONNULL scsiIoCtx);

    // to be used with a deep scan???
    // eReturnValues nvme_Namespace_Rescan(int fd);//rescans a controller for namespaces. This must be a file descriptor
    // without a namespace. EX: /dev/nvme0 and NOT /dev/nvme0n1

    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    eReturnValues map_Block_To_Generic_Handle(const char* M_NONNULL       handle,
                                              char* M_NONNULL* M_NULLABLE genericHandle,
                                              char* M_NONNULL* M_NULLABLE blockHandle,
                                              char* M_NONNULL* M_NULLABLE blockGenericHandle);

#if defined(__cplusplus)
}
#endif
