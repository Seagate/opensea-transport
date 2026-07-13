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

#if defined(__cplusplus)
extern "C"
{
#endif

#include <stdint.h>
#include <sys/types.h>

#define BSG_PROTOCOL_SCSI               0

#define BSG_SUB_PROTOCOL_SCSI_CMD       0
#define BSG_SUB_PROTOCOL_SCSI_TMF       1
#define BSG_SUB_PROTOCOL_SCSI_TRANSPORT 2

/* v4 SG_IO flags */
#define SG_FLAG_DIRECT_IO   1
#define SG_FLAG_MMAP_IO     4
#define SG_FLAG_NO_DXFER    0x10000
#define SG_FLAG_Q_AT_TAIL   0x10
#define SG_FLAG_Q_AT_HEAD   0x20

#define SGV4_FLAG_DIRECT_IO SG_FLAG_DIRECT_IO
#define SGV4_FLAG_MMAP_IO   SG_FLAG_MMAP_IO
#define SGV4_FLAG_NO_DXFER  SG_FLAG_NO_DXFER
#define SGV4_FLAG_Q_AT_TAIL SG_FLAG_Q_AT_TAIL
#define SGV4_FLAG_Q_AT_HEAD SG_FLAG_Q_AT_HEAD
#define SGV4_FLAG_IMMED     0x400

/* SG_IO info field output */
#define SG_INFO_OK_MASK        0x1
#define SG_INFO_OK             0x0
#define SG_INFO_CHECK          0x1
#define SG_INFO_DIRECT_IO_MASK 0x6
#define SG_INFO_INDIRECT_IO    0x0
#define SG_INFO_DIRECT_IO      0x2
#define SG_INFO_MIXED_IO       0x4

    /* v4 interface structure
     * Always use this version for cross-compilation compatibility.
     * This ensures consistent struct layout regardless of system headers.
     */
    struct sg_io_v4
    {
        __s32 guard;       /* [i] 'Q' to differentiate from v3 */
        __u32 protocol;    /* [i] 0 -> SCSI , .... */
        __u32 subprotocol; /* [i] 0 -> SCSI command, 1 -> SCSI task management function, .... */

        __u32 request_len;      /* [i] in bytes */
        __u64 request;          /* [i], [*i] {SCSI: cdb} */
        __u64 request_tag;      /* [i] {SCSI: task tag (only if flagged)} */
        __u32 request_attr;     /* [i] {SCSI: task attribute} */
        __u32 request_priority; /* [i] {SCSI: task priority} */
        __u32 request_extra;    /* [i] {spare, for padding} */
        __u32 max_response_len; /* [i] in bytes */
        __u64 response;         /* [i], [*o] {SCSI: (auto)sense data} */

        /* "dout_": data out (to device); "din_": data in (from device) */
        __u32 dout_iovec_count; /* [i] 0 -> "flat" dout transfer else
                       dout_xfer points to array of iovec */
        __u32 dout_xfer_len;    /* [i] bytes to be transferred to device */
        __u32 din_iovec_count;  /* [i] 0 -> "flat" din transfer */
        __u32 din_xfer_len;     /* [i] bytes to be transferred from device */
        __u64 dout_xferp;       /* [i], [*i] */
        __u64 din_xferp;        /* [i], [*o] */

        __u32 timeout;  /* [i] units: millisecond */
        __u32 flags;    /* [i] bit mask */
        __u64 usr_ptr;  /* [i->o] unused internally */
        __u32 spare_in; /* [i] */

        __u32 driver_status;    /* [o] 0 -> ok */
        __u32 transport_status; /* [o] 0 -> ok */
        __u32 device_status;    /* [o] {SCSI: command completion status} */
        __u32 retry_delay;      /* [o] {SCSI: status auxiliary information} */
        __u32 info;             /* [o] additional information */
        __u32 duration;         /* [o] time to complete, in milliseconds */
        __u32 response_len;     /* [o] bytes of response actually written */
        __s32 din_resid;        /* [o] din_xfer_len - actual_din_xfer_len */
        __s32 dout_resid;       /* [o] dout_xfer_len - actual_dout_xfer_len */
        __u64 generated_tag;    /* [o] {SCSI: transport generated task tag} */
        __u32 spare_out;        /* [o] */

        __u32 padding;
    };

#if defined(__cplusplus)
}
#endif
