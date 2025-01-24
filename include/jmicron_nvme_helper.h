// SPDX-License-Identifier: MPL-2.0

//! \file jmicron_nvme_helper.h
//! \brief Defines the functions for Jmicron NVMe-USB pass-through
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2019-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

// All code in this file is from a JMicron USB to NVMe product specification for pass-through nvme commands.
// This code should only be used on products that are known to use this pass-through interface.

#pragma once

#include "nvme_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#define JMICRON_NVME_PT_OPCODE                                                                                         \
    UINT8_C(0xA1) // NOTE: this is NOT a vendor unique opcode which will make data traces confusing and can confuse
                  // other devices that know this to mean SAT passthrough

#define JMICRON_NVME_NAMESTRING                                                                                        \
    "NVME" // signature used in various placed to validate the payload or returned contents from the device.

#define JMICRON_NVME_MAX_TRANSFER_SIZE_BYTES UINT32_C(65536)

#define JMICRON_NVME_CDB_SIZE                UINT8_C(12)

#define JMICRON_NVME_CMD_PAYLOAD_SIZE        UINT16_C(512)

#define JMICRON_NVME_ADMIN_BIT               BIT7

    typedef enum eJMNvmeProtocolEnum
    {
        JM_PROTOCOL_SET_PAYLOAD = 0,
        JM_PROTOCOL_NON_DATA    = 1,
        JM_PROTOCOL_DMA_IN      = 2,
        JM_PROTOCOL_DMA_OUT     = 3,
        // RESERVED
        JM_PROTOCOL_RETURN_RESPONSE_INFO = 15
    } eJMNvmeProtocol;

    typedef enum eJMNvmeVendorControlEnum
    {
        JM_VENDOR_CTRL_SERVICE_PROTOCOL_FIELD  = 0,
        JM_VENDOR_CTRL_PCIE_POWER_OFF          = 1,
        JM_VENDOR_CTRL_PCIE_POWER_ON           = 2,
        JM_VENDOR_CTRL_INITIAL_PCIE_PRSTN_HIGH = 3, // asserts this to high value
        JM_VENDOR_CTRL_INITIAL_PCIE_PRSTN_583  = 4, // lets the 583 controller handle this
        JM_VENDOR_CTRL_MCU_RESET               = 5,
        JM_VENDOR_CTRL_NVME_NORMAL_SHUTDOWN    = 6
        // All other values up to 255 are reserved
    } eJMNvmeVendorControl;

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_WO(1)
    M_PARAM_RO(2)
    M_PARAM_WO_SIZE(3, 4)
    M_PARAM_RO(7)
    eReturnValues build_JM_NVMe_CDB_And_Payload(uint8_t*                cdb,
                                                eDataTransferDirection* cdbDataDirection,
                                                uint8_t*                dataPtr,
                                                uint32_t                dataSize,
                                                eJMNvmeProtocol         jmProtocol,
                                                eJMNvmeVendorControl    jmCtrl,
                                                nvmeCmdCtx*             nvmCmd);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    eReturnValues send_JM_NVMe_Cmd(nvmeCmdCtx* nvmCmd);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues jm_nvme_Reset(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    eReturnValues jm_nvme_Subsystem_Reset(tDevice* device);

#if defined(__cplusplus)
}
#endif
