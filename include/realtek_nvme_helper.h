// SPDX-License-Identifier: MPL-2.0

//! \file realtek_nvme_helper.h
//! \brief Defines the functions for Realtek NVMe-USB pass-through
//! \details All code in this file is from a Realtek USB to NVMe product specification for pass-through nvme commands.
//! This code should only be used on products that are known to use this pass-through interface.
//! NOTE: Not all realtek chips support this passthrough - TJE
//!\copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2024-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "nvme_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define REALTEK_NVME_PT_OPCODE_OUT           UINT8_C(0xE5)
#define REALTEK_NVME_PT_OPCODE_IN            UINT8_C(0xE4)

#define REALTEK_NVME_MAX_TRANSFER_SIZE_BYTES UINT32_C(262144) // 256KiB

#define REALTEK_NVME_CDB_SIZE                UINT8_C(16)

#define REALTEK_NVME_ADMIN_CMD               UINT32_C(0)
#define REALTEK_NVME_IO_CMD                  UINT32_C(1)

#define REALTEK_NVME_CMD_PAYLOAD_LEN         UINT16_C(64) // command phase payload length
#define REALTEK_NVME_COMPLETION_PAYLOAD_LEN  UINT16_C(16) // completion phase

    typedef enum eRealtekNVMCMDPhaseEnum
    {
        REALTEK_PHASE_COMMAND    = 0xF7,
        REALTEK_PHASE_DATA       = 0xF8,
        REALTEK_PHASE_COMPLETION = 0xF9
    } eRealtekNVMCMDPhase;

    typedef enum eRealtekDataXferEnum
    {
        REALTEK_NO_DATA  = 0x0000,
        REALTEK_DATA_IN  = 0x0001,
        REALTEK_DATA_OUT = 0x0002
    } eRealtekDataXfer;

    M_NONNULL_PARAM_LIST(1, 2, 6)
    M_PARAM_WO(1)
    M_PARAM_WO(2)
    M_NONNULL_IF_NONZERO_PARAM(3, 4)
    M_PARAM_RW_SIZE(3, 4)
    M_PARAM_RO(6)
    eReturnValues build_Realtek_NVMe_CDB_And_Payload(uint8_t*                cdb,
                                                     eDataTransferDirection* cdbDataDirection,
                                                     uint8_t*                dataPtr,
                                                     uint32_t                dataSize,
                                                     eRealtekNVMCMDPhase     phase,
                                                     nvmeCmdCtx*             nvmCmd);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) eReturnValues send_Realtek_NVMe_Cmd(nvmeCmdCtx* nvmCmd);

#if defined(__cplusplus)
}
#endif
