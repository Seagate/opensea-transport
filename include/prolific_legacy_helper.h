// SPDX-License-Identifier: MPL-2.0

//! \file prolific_legacy_helper.h
//! \brief Defines the functions for legacy Prolific USB pass-through
//! \details All code in this file is from an OLD Prolific USB product specification for pass-through commands.
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

#define PROLIFIC_GET_REGISTERS_OPCODE       0xD7
#define PROLIFIC_EXECUTE_ATA_COMMAND_OPCODE 0xD8
#define CHECK_WORD                          0x067B

    //-----------------------------------------------------------------------------
    //
    //  build_Prolific_Legacy_Passthrough_CDBs()
    //
    //! \brief   Description:  Function to construct a Prolific Legacy USB Pass-through CDBs based on the ATA Command
    //! Options
    //
    //  Entry:
    //!   \param[out] lowCDB = 16byte array to fill in with the built CDB for the 28bit command registers.
    //!   \param[out] hiCDB = 16byte array to fill in with the build CDB for the Ext command registers.
    //!   \param[out] highCDBValid = output parameter to let you know when the data in the hiCDB is valid and should be
    //!   send to the device. Only issue the HighCDB to the device when this bool returns true. \param[in]
    //!   ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 3, 4)
    M_PARAM_WO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    M_PARAM_RO(4)
    eReturnValues build_Prolific_Legacy_Passthrough_CDBs(uint8_t lowCDB[16], uint8_t hiCDB[16], bool* highCDBValid,
                                                         ataPassthroughCommand* ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  get_RTFRs_From_Prolific_Legacy()
    //
    //! \brief   Description:  This will build and send the command to get the RTFR results of the last pass-through
    //! command. The RTFRs in the ataCommandOptions will be filled in when this is successful.
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] ataCommandOptions = ATA command options
    //!   \param[in] commandRet = return value from sending the ATA Pass-through command to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues get_RTFRs_From_Prolific_Legacy(tDevice * device, ataPassthroughCommand * ataCommandOptions,
                                                 eReturnValues commandRet);

    //-----------------------------------------------------------------------------
    //
    //  send_Prolific_Legacy_Passthrough_Command()
    //
    //! \brief   Description:  Function to send a Prolific Legacy Pass-through command. This will automatically call the
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
    eReturnValues send_Prolific_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
