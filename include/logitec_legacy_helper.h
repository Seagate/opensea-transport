// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2026-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file logitec_legacy_helper.h   Implementation for Logitec Legacy USB Pass-through CDBs


#include "common_types.h"
#include "code_attributes.h"

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "common_public.h"
#include "scsi_helper.h"

// CDB specific values
enum {
    LOGITEC_LEGACY_OPCODE = 0xE0,
    LOGITEC_LEGACY_SIGNATURE = 0x4C,
    LOGITEC_LEGACY_CDB_LEN = CDB_LEN_10,
};

// CDB specific offsets
enum {
    LOGITEC_CDB_OFFSET_OPCODE = 0,
    LOGITEC_CDB_OFFSET_1_RESERVED = 1, // possibly flags to help with various commands/protocols/transfer direction?
    LOGITEC_CDB_OFFSET_FEATURES = 2,
    LOGITEC_CDB_OFFSET_SECTOR_COUNT = 3,
    LOGITEC_CDB_OFFSET_LBA_LOW = 4,
    LOGITEC_CDB_OFFSET_LBA_MID = 5,
    LOGITEC_CDB_OFFSET_LBA_HIGH = 6,
    LOGITEC_CDB_OFFSET_DEVICE = 7,
    LOGITEC_CDB_OFFSET_COMMAND = 8,
    LOGITEC_CDB_OFFSET_SIGNATURE = 9
};

M_PARAM_WO(1)
    M_PARAM_RO(2)
    eReturnValues build_Logitec_Legacy_CDB(uint8_t                          cdb[M_NONNULL_ARRAY LOGITEC_LEGACY_CDB_LEN],
                                       ataPassthroughCommand* M_NONNULL ataCommandOptions);
M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_Logitec_Legacy_Passthrough_Command(const tDevice* M_NONNULL         device,
                                                      ataPassthroughCommand* M_NONNULL ataCommandOptions);
