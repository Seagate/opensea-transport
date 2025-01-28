// SPDX-License-Identifier: MPL-2.0

//! \file ti_legacy_helper.h
//! \brief Defines the functions for legacy TI USB pass-through
//! \details All code in this file is from an OLD TI USB product specification for pass-through commands.
//! This code should only be used on products that are known to use this pass-through interface.
//! Some of this code may also be from something found in legacy source. This will be commented if it is.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "ata_helper.h"
#include "common_public.h"
#include "common_types.h"

#if defined(__cplusplus)
extern 'C'
{
#endif

#define TI_LEGACY_OPCODE_OLD                                                                                           \
    0xF0 // There is no VID/PID combination that references this type. By default this code is not using this value
         // since we don't have a drive that responds to this.
#define TI_LEGACY_OPCODE 0x3C // Also referenced as TIREV35

    //-----------------------------------------------------------------------------
    //
    //  build_TI_Legacy_CDB()
    //
    //! \brief   Description:  Function to construct a TI Legacy USB Pass-through CDB based on the ATA Command Options
    //
    //  Entry:
    //!   \param[out] cdb = 16byte array to fill in with the built CDB
    //!   \param[in] ataCommandOptions = ATA command options
    //!   \param[in] olderOpCode = set to TRUE to use the older 0xF0 opcode. By default, this should be false.
    //!   \param[in] forceMode = set to true to force a PIO or UDMA mode (1-4). By default, we tell the USB bridge to
    //!   use the highest available mode the bridge and drive support. \param[in] modeValue = set to 1 - 4 for the mode
    //!   value. This is only used when forceMode is set to TRUE.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_RO(2)
    eReturnValues build_TI_Legacy_CDB(uint8_t cdb[16], ataPassthroughCommand * ataCommandOptions, bool olderOpCode,
                                      bool forceMode, uint8_t modeValue);

    //-----------------------------------------------------------------------------
    //
    //  send_TI_Legacy_Passthrough_Command()
    //
    //! \brief   Description:  Function to send a TI Legacy Pass-through command. This will automatically call the
    //! function to build the command, then send it to the drive.
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_TI_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
