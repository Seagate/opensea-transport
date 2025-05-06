// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file bsd_ata_passthrough.h issues a ata passthrough request for openbsd and netbsd

#include "common_types.h"
#include "scsi_helper.h"

#pragma once

eReturnValues send_BSD_ATA_Reset(int fd);

M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) eReturnValues send_BSD_ATA_IO(ScsiIoCtx* scsiIoCtx);
