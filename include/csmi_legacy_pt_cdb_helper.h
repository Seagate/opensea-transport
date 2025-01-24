// SPDX-License-Identifier: MPL-2.0

//! \file csmi_legacy_pt_cdb_helper.h
//! \brief Defines the constants, structures, & functions to help with legacy CSMI ATA passthrough CDB implementation
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "ata_helper.h"
#include "common_public.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define CSMI_PASSTHROUGH_CDB_LENGTH UINT8_C(16)

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_RO(2)
    eReturnValues build_CSMI_Passthrough_CDB(uint8_t cdb[CSMI_PASSTHROUGH_CDB_LENGTH], ataPassthroughCommand* ataPtCmd);

    // NOTE: This is a stub. There is not currently a known way to get RTFRs when sending this passthrough CDB
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues get_RTFRs_From_CSMI_Legacy(tDevice* device, ataPassthroughCommand* ataCommandOptions, int commandRet);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_CSMI_Legacy_ATA_Passthrough(tDevice* device, ataPassthroughCommand* ataCommandOptions);

#if defined(__cplusplus)
}
#endif
