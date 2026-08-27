// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2025-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file netbsd_openbsd_helper.h handle functionality to scan for devices and issue commands in
// both netbsd and openbsd

#pragma once

#if defined(__cplusplus)
extern "C"
{
#endif

#include "code_attributes.h"
#include "common_types.h"

#include "common_public.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#define BSD_MAX_CMD_TIMEOUT_SECONDS INT_MAX

#if defined(__cplusplus)
}
#endif
