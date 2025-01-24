// SPDX-License-Identifier: MPL-2.0

//! \file sntl_helper.h
//! \brief Defines the function headers to help with SCSI to NVMe translation
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_public.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  translate_SCSI_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  This function attempts to perform SCSI to NVMe translation according to the SNTL white
    //! paper from nvmexpress.org
    //!          This function is meant to be called by a lower layer that doesn't natively support SCSI IOs to NVMe
    //!          devices
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] scsiIoCtx = scsiIoCtx containing a SCSI command to issue to a drive.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1) M_PARAM_RW(2) eReturnValues sntl_Translate_SCSI_Command(tDevice* device, ScsiIoCtx* scsiIoCtx);

#if defined(__cplusplus)
}
#endif
