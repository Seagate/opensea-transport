// SPDX-License-Identifier: MPL-2.0

//! \file hpux_helper.h
//! \brief low level drive interface support for HP-UX
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_public.h"
#include "common_types.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define HPUX_MAX_CMD_TIMEOUT_SECONDS UINT_MAX

#if defined(__cplusplus)
}
#endif
