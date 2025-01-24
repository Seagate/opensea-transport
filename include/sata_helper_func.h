// SPDX-License-Identifier: MPL-2.0

//! \file sata_helper_func.h
//! \brief functions to help with SATA specific things. Printing out FIS, creating FIS, etc.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "ata_helper.h"
#include "common_types.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO_SIZE(1, 2) void print_FIS(void* fis, uint32_t fisLengthBytes);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_RO(2)
    eReturnValues build_H2D_FIS_From_ATA_PT_Command(ptrSataH2DFis h2dFis, ataTFRBlock* ataPTCmd, uint8_t pmPort);

#if defined(__cplusplus)
}
#endif
