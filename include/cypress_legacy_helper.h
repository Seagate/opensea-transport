// SPDX-License-Identifier: MPL-2.0

//! \file cypress_legacy_helper.h
//! \brief Defines the functions for legacy Cypress USB pass-through
//! \details All code in this file is from an OLD Cypress USB product specification for pass-through commands.
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

#define CYPRESS_SIGNATURE_OPCODE 0x24
#define CYPRESS_SUBCOMMAND       0x24

    // ATA CB Action Select Bits
#define CYPRESS_IDENTIFY_DATA_BIT                                                                                      \
    BIT7 // set when the command is an identify device or identify packet device, or undefined behavior may occur
#define CYPRESS_UDMA_COMMAND_BIT BIT6 // set for UDMA transfers/commands
#define CYPRESS_DEVICE_OVERRIDE_BIT                                                                                    \
    BIT5 // using this forces the controller to use the ATACB field 0x0B bit 4 instead of the assignedLUN (don't
         // recommend setting this - TJE)
#define CYPRESS_DEVICE_ERROR_OVERRIDE_BIT BIT4 // setting this halts data accesses if a device error is detected.
#define CYPRESS_PHASE_ERROR_OVERRIDE_BIT  BIT3 // setting this halts data accesses if a phase error is detected.
#define CYPRESS_POLL_ALTERNATE_STATUS_OVERRIDE_BIT                                                                     \
    BIT2 // set in order to execute the command without polling the AltStat register for a value of 0 (not setting will
         // wait until the busy bit is no longer set)
#define CYPRESS_DEVICE_SELECTION_OVERRIDE_BIT BIT1 // set to select the device after command register write accesses
#define CYPRESS_TASK_FILE_READ_BIT                                                                                     \
    BIT0 // Use this bit to request the RTFRs from the device (8 bytes of data will be returned)

    // ATA CB Register select bits (we'll always set all the bits-TJE)

    //-----------------------------------------------------------------------------
    //
    //  build_Cypress_Legacy_CDB()
    //
    //! \brief   Description:  Function to construct a Cypress Legacy USB Pass-through CDB based on the ATA Command
    //! Options
    //
    //  Entry:
    //!   \param[out] cdb = 16byte array to hold the build command.
    //!   \param[in] ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2) eReturnValues build_Cypress_Legacy_CDB(uint8_t cdb[16], ataPassthroughCommand * ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  get_RTFRs_From_Cypress_Legacy()
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
    eReturnValues get_RTFRs_From_Cypress_Legacy(tDevice * device, ataPassthroughCommand * ataCommandOptions,
                                                eReturnValues commandRet);

    //-----------------------------------------------------------------------------
    //
    //  send_Cypress_Legacy_Passthrough_Command()
    //
    //! \brief   Description:  Function to send a Cypress Legacy Pass-through command. This will automatically call the
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_Cypress_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
