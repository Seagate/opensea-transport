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
// \file iodata_legacy_helper.h   Implementation for IO Data Legacy USB Pass-through CDBs


#include "common_types.h"
#include "code_attributes.h"

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "common_public.h"
#include "scsi_helper.h"

// CDB specific values
enum {
    IO_DATA_LEGACY_OPCODE = 0xE3,
    IO_DATA_LEGACY_CDB_LEN = CDB_LEN_12,
};

// CDB specific offsets
enum {
    IO_DATA_CDB_OFFSET_OPCODE = 0,
    IO_DATA_CDB_OFFSET_1_RESERVED = 1, // possibly flags to help with various commands/protocols/transfer direction?
    IO_DATA_CDB_OFFSET_FEATURES = 2,
    IO_DATA_CDB_OFFSET_SECTOR_COUNT = 3,
    IO_DATA_CDB_OFFSET_LBA_LOW = 4,
    IO_DATA_CDB_OFFSET_LBA_MID = 5,
    IO_DATA_CDB_OFFSET_LBA_HIGH = 6,
    IO_DATA_CDB_OFFSET_DEVICE = 7,
    IO_DATA_CDB_OFFSET_COMMAND = 8,
    IO_DATA_CDB_OFFSET_9_RESERVED = 9,
    IO_DATA_CDB_OFFSET_10_RESERVED = 10,
    IO_DATA_CDB_OFFSET_11_RESERVED = 11,
};

M_PARAM_WO(1)
    M_PARAM_RO(2)
    eReturnValues build_Iodata_Legacy_CDB(uint8_t                          cdb[M_NONNULL_ARRAY IO_DATA_LEGACY_CDB_LEN],
                                       ataPassthroughCommand* M_NONNULL ataCommandOptions);
M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_Iodata_Legacy_Passthrough_Command(const tDevice* M_NONNULL         device,
                                                      ataPassthroughCommand* M_NONNULL ataCommandOptions);
