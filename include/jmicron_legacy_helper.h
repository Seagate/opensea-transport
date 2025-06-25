// SPDX-License-Identifier: MPL-2.0

//! \file jmicron_legacy_helper.h
//! \brief Defines the functions for legacy JMicron USB to ATA pass-through
//! \details This code should only be used on products that are known to use this pass-through interface.
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

#if defined(__cplusplus)
extern 'C'
{
#endif

#define JM_ATA_PT_OPCODE          UINT8_C(0xDF)
#define JM_READ_ADAPTER_REGISTERS UINT8_C(0xFD) // byte 11 of CDB

    typedef enum eJMicronDirEnum
    {
        JM_DIR_DATAOUT = 0x00,
        JM_DIR_DATAIN  = 0x10,
        JM_DIR_NODATA  = 0x10
    } eJMicronDir;

// prolific reuses this with this signature appended to the end of the CDB
// This is the same as the USB VID code.
#define JM_PROLIFIC_SIGNATURE UINT16_C(0x067B)

    typedef enum eJMicronAdapterRegistersEnum
    {
        JM_REG_CONNECTED_PORTS = 0x720F, // single byte
        JM_REG_DEV0_RESULTS    = 0x8000,
        JM_REG_DEV1_RESULTS    = 0x9000,
    } eJMicronAdapterRegisters;

    typedef enum eJMicronConnectedPortsEnum
    {
        JM_PORT_DEV0 = 0x04,
        JM_PORT_DEV1 = 0x40,
        JM_PORT_BOTH = 0x44 // Note: in this case it is not possible to figured out which device is which and is treated
                            // as an error. - TJE
    } eJMicronConnectedPorts;

    typedef enum eJMicronCDBLenEnum
    {
        JM_CDB_LEN          = 12,
        JM_PROLIFIC_CDB_LEN = 14
    } eJMicronCDBLen;

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW(3)
    eReturnValues read_Adapter_Register(tDevice * device, eJMicronAdapterRegisters jmregister, uint8_t * ptrData,
                                        uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues set_JM_Dev(tDevice * device);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_WO(1)
    M_PARAM_RO(2)
    eReturnValues build_JMicron_Legacy_PT_CDB(uint8_t cdb[JM_PROLIFIC_CDB_LEN],
                                              ataPassthroughCommand * ataCommandOptions);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues get_RTFRs_From_JMicron_Legacy(tDevice * device, ataPassthroughCommand * ataCommandOptions,
                                                eReturnValues commandRet);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    eReturnValues send_JMicron_Legacy_Passthrough_Command(tDevice * device, ataPassthroughCommand * ataCommandOptions);

#if defined(__cplusplus)
}
#endif
