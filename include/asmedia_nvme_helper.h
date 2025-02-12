// SPDX-License-Identifier: MPL-2.0

//! \file asmedia_nvme_helper.h
//! \brief Support for ASMedia USB adapter NVMe passthrough
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2019-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

// All code in this file is from a ASMedia USB to NVMe product specification for pass-through nvme commands.
// This code should only be used on products that are known to use this pass-through interface.
// NOTE: There are 2 possible pass-through types for ASMedia. 1. Limited/basic capability to get identify and log page
// data (most common) and 2. a full pass-through capability (not supported on every device)
//       This file contains both. It is not clear how to determine if the full packet passthrough is supported other
//       than trying it.

#pragma once
#include "common_types.h"
#include "nvme_helper.h"
#include "scsi_helper.h"
#include <inttypes.h>

#if defined(__cplusplus)
extern "C"
{
#endif

    // Documentation shows that the inquiry data sets an additional product identifier in bytes 36 & 37.
    // These defines are below. These values will only show when reading EXACTLY 38 bytes of std inquiry data.
#define INQ_ADPID_B36 UINT8_C(0x60)
#define INQ_ADPID_B37 UINT8_C(0x23)

    ////////////////////////////////////
    // ASMEDIA Basic NVMe passthrough //
    ////////////////////////////////////

#define ASMEDIA_NVME_PASSTHROUGH_OP       UINT8_C(0xE6)
#define ASMEDIA_NVME_PT_NVME_OP_OFFSET    1
#define ASMEDIA_NVME_PASSTHROUGH_CDB_SIZE UINT8_C(16)

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    eReturnValues send_ASMedia_Basic_NVMe_Passthrough_Cmd(nvmeCmdCtx* nvmCmd);

    /////////////////////////////////////
    // ASMEDIA Full packet passthrough //
    /////////////////////////////////////

    // ASMedia also has some other packet commands described below here.
#define ASMEDIA_NVME_PACKET_WRITE_OP           UINT8_C(0xEA)
#define ASMEDIA_NVME_PACKET_READ_OP            UINT8_C(0xEB)
#define ASMEDIA_NVME_PACKET_SIGNATURE          UINT8_C(0x5A)
#define ASMEDIA_NVME_PACKET_OPERATION_OFFSET   2
#define ASMEDIA_NVME_PACKET_PARAMETER_1_OFFSET 3
#define ASMEDIA_NVME_PACKET_PARAMETER_2_OFFSET 4
#define ASMEDIA_NVME_PACKET_CDB_SIZE           UINT8_C(16)

    typedef enum eASM_NVMPacket_OperationEnum
    {
        ASMEDIA_NVMP_OP_POWER_DOWN_NVME =
            0x00, // ND (w) - param 1 used to control shutdown process. Only useful if power control circuit is present
        ASMEDIA_NVMP_OP_RESET_BRIDGE    = 0x01, // ND (w) - no params
        ASMEDIA_NVMP_OP_GET_BRIDGE_INFO = 0x02, // R - no params, returns 64B of data. USB SN, VID, PID, PCIe detection,
                                                // PCIe speed, PCIe lane width, power control of PCIe
        ASMEDIA_NVMP_OP_CONTROL_LED = 0x03,     // ND (w) - param 1 controls operations mode of LED
        ASMEDIA_NVMP_OP_RELINK_USB  = 0x04,     // ND (w) - param 1 controls NVMe shutdown process before relink
        // Below operations are for NVM packet passthrough. Max transfer is 128KB in provided documentation, but may
        // have previously been different based on revision history.
        ASMEDIA_NVMP_OP_SEND_ADMIN_IO_NVM_CMD = 0x80, // W
        ASMEDIA_NVMP_OP_DATA_PHASE            = 0x81, // R | W
        ASMEDIA_NVMP_OP_GET_NVM_COMPLETION    = 0x82, // R
    } eASM_NVMPacket_Operation;

    // These are parameter 1 values for power down operation
#define ASM_NVMP_PWR_WITHOUT_SHUTDOWN UINT8_C(0)
#define ASM_NVMP_PWR_WITH_SHUTDOWN    UINT8_C(1)

    // These are parameter 1 values for LED
#define ASM_NVMP_LED_OFF   UINT8_C(0)
#define ASM_NVMP_LED_ON    UINT8_C(1)
#define ASM_NVMP_LED_BLINK UINT8_C(2)

    // These are parameter 1 values for Relink
#define ASM_NVMP_RELINK_NO_SHUTDOWN     UINT8_C(0)
#define ASM_NVMP_RELINK_NORMAL_SHUTDOWN UINT8_C(1)

    // Process for NVM packet passthrough command:
    // 1. Send 64B of command data
    // 2. perform data phase (even for non-data, send with length set to 0)
    // 3. read 16 B of command completion data
    // Other notes: metadata is reserved, data pointer and CID will be modified by the bridge. Must be 512B aligned
    // transfers according to documentation. If command completion data bytes 14 & 15 are FFh, then entry is invalid and
    // no NVMe command was sent or executed. Data phase/direction parameter should match for all 3 steps of packet
    // passthrough

#define ASM_NVMP_DWORDS_DATA_PACKET_SIZE UINT8_C(64) // Size of packet for NVMe DWORDS
#define ASM_NVMP_RESPONSE_DATA_SIZE      UINT8_C(16) // size of NVM completion results

    // Parameter 1 values for packet passthrough of NVM command
#define ASM_NVMP_SEND_CMD_ADMIN UINT8_C(0)
#define ASM_NVMP_SEND_CMD_IO    UINT8_C(1)

    // Parameter 2 values for packet passthrough of NVM command
#define ASM_NVMP_NON_DATA UINT8_C(0)
#define ASM_NVMP_DATA_IN  UINT8_C(1)
#define ASM_NVMP_DATA_OUT UINT8_C(2)

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    eReturnValues send_ASM_NVMe_Cmd(nvmeCmdCtx* nvmCmd);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues asm_nvme_Reset(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues asm_nvme_Subsystem_Reset(tDevice* device);

#if defined(__cplusplus)
}
#endif
