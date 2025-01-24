// SPDX-License-Identifier: MPL-2.0

//! \file of_nvme_helper_func.h
//! \brief Defines the function calls to help with open fabrics NVME ioctl interface
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(ENABLE_OFNVME)

#    include "common_public.h"
#    include "common_types.h"
#    include "nvme_helper.h"
#    include "of_nvmeIoctl.h"
#    include "of_nvme_helper.h"
#    include "scsi_helper.h"
#    include <stdint.h>

#    if defined(__cplusplus)
extern "C"
{
#    endif

    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API bool supports_OFNVME_IO(HANDLE deviceHandle);

    //-----------------------------------------------------------------------------
    //
    //  send_OFNVME_IO(nvmeCmdCtx *nvmeIoCtx)
    //
    //! \brief   Description:  send a command through a OFNVME IOCTL
    //
    //  Entry:
    //!   \param[in] nvmeIoCtx - pointer to the nvmeCmdCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_IO(nvmeCmdCtx* nvmeIoCtx);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_Add_Namespace(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_Remove_Namespace(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_Reset(tDevice* device);

#    if defined(__cplusplus)
}
#    endif
#endif // ENABLE_OFNVME
