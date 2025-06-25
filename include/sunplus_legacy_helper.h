// SPDX-License-Identifier: MPL-2.0

//! \file sunplus_legacy_helper.h
//! \brief Defines the functions for legacy sunplus USB pass-through
//! \details All code in this file is from an OLD sunplus USB product specification for pass-through commands.
//! This code should only be used on products that are known to use this pass-through interface.
//! Some of this code may also be from something found in legacy source. This will be commented if it is.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "ata_helper.h"
#include "common_public.h"
#include "common_types.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern 'C'
{
#endif

#define SUNPLUS_PT_COMMAND_OPCODE 0xF8
#define SUNPLUS_PT_CDB_LEN CDB_LEN_12

typedef enum eSunplusSubCommandEnum
{
    SUNPLUS_SUBCOMMAND_GET_STATUS = 0x21,
    SUNPLUS_SUBCOMMAND_SEND_ATA_COMMAND = 0x22,
    SUNPLUS_SUBCOMMAND_SET_48BIT_REGISTERS = 0x23
}eSunplusSubCommand;

typedef enum eSunplusDataDirEnum
{
    SUNPLUS_XFER_NONE = 0x00,
    SUNPLUS_XFER_IN = 0x10,
    SUNPLUS_XFER_OUT = 0x11
}eSunplusDataDir;

    M_NONNULL_PARAM_LIST(1, 2, 3, 4)
    M_PARAM_WO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    M_PARAM_RO(4)
    eReturnValues build_Sunplus_Legacy_Passthrough_CDBs(uint8_t lowCDB[SUNPLUS_PT_CDB_LEN], uint8_t hiCDB[SUNPLUS_PT_CDB_LEN], bool* highCDBValid,
                                                         ataPassthroughCommand* ataCommandOptions);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues get_RTFRs_From_Sunplus_Legacy(tDevice * device, ataPassthroughCommand * ataCommandOptions,
                                                 eReturnValues commandRet);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_Sunplus_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
