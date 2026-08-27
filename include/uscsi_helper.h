// SPDX-License-Identifier: MPL-2.0

//! \file uscsi_helper.h
//! \brief types/functions to assist with the uscsi interface in Solaris and Illumos
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_types.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

    // This is the maximum timeout a command can use in uscsi passthrough with Solaris...18.2 hours
#define USCSI_MAX_CMD_TIMEOUT_SECONDS UINT16_MAX

    //-----------------------------------------------------------------------------
    //
    //  send_uscsi_io()
    //
    //! \brief   Description:  Function to send an IO using the Solaris uscsi passthrough
    //
    //  Entry:
    //!   \param[in] scsiIoCtx =  pointer to a scsiIoCtx struct which contains the information necessary to send a
    //!   command.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1) eReturnValues send_uscsi_io(ScsiIoCtx* M_NONNULL scsiIoCtx);

#if defined(__cplusplus)
}
#endif
