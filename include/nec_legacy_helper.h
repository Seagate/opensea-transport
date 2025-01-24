// SPDX-License-Identifier: MPL-2.0

//! \file nec_legacy_helper.h
//! \brief Defines the functions for legacy NEC USB pass-through
//! \details All code in this file is from an OLD NEC USB product specification for pass-through commands.
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

#define NEC_WRITE_OPCODE      0xF0 // send the command (TFRs)
#define NEC_READ_OPCODE       0xF1 // get the results (RTFRs)

#define NEC_MULTIPLE_BIT      BIT5

#define NEC_WRAPPER_SIGNATURE 0x41

    // this code will always use 0 since I don't really understand these options - TJE (old code always set 0 too)
    typedef enum eNECSectorCountOptionEnum
    {
        SECTOR_COUNT_FROM_19H_1AH_SECTOR_COUNT_REGISTERS =
            0, // 00b = Sector Count Register Sector Count (15:8), Byte 10, Sector Count Low = Sector Count Register
               // Sector Count (7:0), Byte 11
        SECTOR_COUNT_LOW_FROM_1EH = 1, // 01b - sector count high = 0, sector count low = Control (0), 15h
        SECTOR_COUNT_LOW_FROM_OFFSET_1AH_SECTOR_COUNT_HIGH_FROM_OFFSET_18H =
            2, // 10b - sector count high = sector number register LBA (7:0), Byte 9, sector count Low = Sector Count
               // Register Sector Count (7:0), Byte 11
        SECTOR_COUNT_RESERVED = 3 // 11b
    } eNECSectorCountOption;

    //-----------------------------------------------------------------------------
    //
    //  build_TI_Legacy_CDB()
    //
    //! \brief   Description:  Function to construct a TI Legacy USB Pass-through CDB based on the ATA Command Options
    //
    //  Entry:
    //!   \param[out] cdb = 16byte array to fill in with the built CDB
    //!   \param[in] ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_WO(1)
    M_PARAM_RO(2) eReturnValues build_NEC_Legacy_CDB(uint8_t cdb[16], ataPassthroughCommand * ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  get_RTFRs_From_NEC_Legacy()
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
    eReturnValues get_RTFRs_From_NEC_Legacy(tDevice * device, ataPassthroughCommand * ataCommandOptions,
                                            eReturnValues commandRet);

    //-----------------------------------------------------------------------------
    //
    //  send_NEC_Legacy_Passthrough_Command()
    //
    //! \brief   Description:  Function to send a NEC Legacy Pass-through command. This will automatically call the
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
    eReturnValues send_NEC_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
