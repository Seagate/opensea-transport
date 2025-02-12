// SPDX-License-Identifier: MPL-2.0

//! \file psp_legacy_helper.h
//! \brief Defines the functions for legacy PSP USB pass-through
//! \details All code in this file is from an OLD PSP (Personal Storage Products-Maxtor) USB product specification for
//! pass-through commands. This code should only be used on products that are known to use this pass-through interface.
//! Some of this code may also be from something found in legacy source. This will be commented if it is.
//!\copyright
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

#define PSP_OPCODE                 0xF0
#define PSP_PASSTHROUGH_ENABLE_KEY 0x4D584F4154415054 // ASCII: MXOATAPT
#define PSP_EXT_COMMAND_CDB_LEN    18

    typedef enum ePSPATAPTFunctionsEnum
    {
        PSP_FUNC_RETURN_TASK_FILE_REGISTERS = 0,
        PSP_FUNC_NON_DATA_COMMAND           = 1,
        PSP_FUNC_DATA_IN_COMMAND            = 3,
        PSP_FUNC_DATA_OUT_COMMAND           = 7,
        PSP_FUNC_EXT_DATA_IN_COMMAND        = 8,
        PSP_FUNC_EXT_DATA_OUT_COMMAND       = 9,
        PSP_FUNC_ENABLE_ATA_PASSTHROUGH     = 12,
        PSP_FUNC_DISABLE_ATA_PASSTHROUGH    = 15,
    } ePSPATAPTFunctions;

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues enable_Disable_ATA_Passthrough(tDevice * device, bool enable);

    //-----------------------------------------------------------------------------
    //
    //  build_PSP_Legacy_CDB()
    //
    //! \brief   Description:  Function to construct a PSP Legacy USB Pass-through CDB based on the ATA Command Options
    //
    //  Entry:
    //!   \param[out] cdb = pointer to an array that is at least 18 bytes in size to fill in with the command to send to
    //!   the device. \param[out] cdbLen = pointer to a valid to hold the size of the CDB to issue. This will be either
    //!   12 or 18 bytes. \param[in] ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 3)
    M_PARAM_WO(1)
    M_PARAM_WO(2)
    M_PARAM_RO(3)
    eReturnValues build_PSP_Legacy_CDB(uint8_t * cdb, uint8_t * cdbLen, ataPassthroughCommand * ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  get_RTFRs_From_PSP_Legacy()
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
    eReturnValues get_RTFRs_From_PSP_Legacy(tDevice * device, ataPassthroughCommand * ataCommandOptions,
                                            eReturnValues commandRet);

    //-----------------------------------------------------------------------------
    //
    //  send_PSP_Legacy_Passthrough_Command()
    //
    //! \brief   Description:  Function to send a PSP Legacy Pass-through command. This will automatically call the
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
    eReturnValues send_PSP_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
