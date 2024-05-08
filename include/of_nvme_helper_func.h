// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file of_nvme_helper_func.h
// \brief Defines the function calls to help with open fabrics NVME implementation

#pragma once

#if defined (ENABLE_OFNVME)

#include "common.h"
#include <stdint.h>
#include "of_nvmeIoctl.h"
#include "of_nvme_helper.h"
#include "common_public.h"
#include "nvme_helper.h"
#include "scsi_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    OPENSEA_TRANSPORT_API bool supports_OFNVME_IO(HANDLE deviceHandle);

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
    OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_IO(nvmeCmdCtx *nvmeIoCtx);

    OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_Add_Namespace(tDevice * device);

    OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_Remove_Namespace(tDevice * device);

    OPENSEA_TRANSPORT_API eReturnValues send_OFNVME_Reset(tDevice * device);

#if defined (__cplusplus)
}
#endif
#endif//ENABLE_OFNVME
