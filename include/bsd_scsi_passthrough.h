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
// \file bsd_scsi_passthrough.h issues a scsi passthrough request for openbsd and netbsd

#include "common_types.h"
#include "scsi_helper.h"

#pragma once

M_PARAM_WO(2)
M_PARAM_WO(3)
M_PARAM_WO(4)
M_PARAM_WO(5)
eReturnValues get_BSD_SCSI_Address(int fd, int* type, int* bus, int* target, int* lun);

eReturnValues send_BSD_SCSI_Reset(int fd);

eReturnValues send_BSD_SCSI_Bus_Reset(int fd);

M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) eReturnValues send_BSD_SCSI_IO(ScsiIoCtx* scsiIoCtx);
