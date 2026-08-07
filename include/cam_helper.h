// SPDX-License-Identifier: MPL-2.0

//! \file cam_helper.h
//! \brief Low level interface for FreeBSD's CAM API. Also FreeBSD NVMe support
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

#include <inttypes.h>
#include <sys/cdefs.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/sbuf.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(__DragonFly__)
#    include <cam/ata/ata_all.h>
#    include <cam/cam.h>
#    include <cam/cam_ccb.h>
#    include <cam/cam_debug.h>
#    include <cam/scsi/scsi_all.h>
#    include <cam/scsi/scsi_da.h>
#    include <cam/scsi/scsi_message.h>
#    include <cam/scsi/scsi_pass.h>
#    include <cam/scsi/smp_all.h>
#    if defined(XPORT_IS_NVME)
// If this is defined, then this version of CAM supports NVMe
#        include <cam/nvme/nvme_all.h>
#    endif // XPORT_IS_NVME
#else
// slightly different paths for dragonflybsd
#    include <bus/cam/cam.h>
#    include <bus/cam/cam_ccb.h>
#    include <bus/cam/cam_debug.h>
#    include <bus/cam/scsi/scsi_all.h>
#    include <bus/cam/scsi/scsi_da.h>
#    include <bus/cam/scsi/scsi_message.h>
#    include <bus/cam/scsi/scsi_pass.h>
#endif //!__DragonFly__
#include <camlib.h>

    // This is the maximum timeout a command can use in CAM passthrough with FreeBSD...1193 hours
    // NOTE: CAM also supports an infinite timeout, but that is checked in a separate function
#define CAM_MAX_CMD_TIMEOUT_SECONDS 4294967

    //-----------------------------------------------------------------------------
    //
    //  send_Scsi_Cam_IO()
    //
    //! \brief   Description:  Function to send a SCSI CAM ioctl
    //
    //  Entry:
    //!   \param scsiIoCtx = pointer to the ScsiIoCtx struct describing the command to send to a device
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues send_Scsi_Cam_IO(ScsiIoCtx* M_NONNULL scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  send_Ata_Cam_IO()
    //
    //! \brief   Description:  Function to send a ATA CAM ioctl
    //
    //  Entry:
    //!   \param scsiIoCtx = pointer to the ScsiIoCtx struct describing the command to send to a device
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues send_Ata_Cam_IO(ScsiIoCtx* M_NONNULL scsiIoCtx);

#if defined(__cplusplus)
}
#endif
