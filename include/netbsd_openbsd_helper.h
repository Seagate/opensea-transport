// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file netbsd_openbsd_helper.h handle functionality to scan for devices and issue commands in
// both netbsd and openbsd

#pragma once

#if defined(__cplusplus)
extern "C"
{
#endif

#include "code_attributes.h"
#include "common_types.h"

#include "common_public.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#define BSD_MAX_CMD_TIMEOUT_SECONDS INT_MAX

    // If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued,
    // otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
    OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void);

    M_PARAM_RO(1) eReturnValues send_IO(ScsiIoCtx* M_NONNULL scsiIoCtx);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Device_Reset(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Bus_Reset(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Controller_Reset(const tDevice* M_NONNULL device);

    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(2, 3)
    eReturnValues pci_Read_Bar_Reg(const tDevice* M_NONNULL device, uint8_t* M_NONNULL pData, uint32_t dataSize);

    M_PARAM_RW(1) eReturnValues send_NVMe_IO(nvmeCmdCtx* M_NONNULL nvmeIoCtx);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_nvme_Reset(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_nvme_Subsystem_Reset(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Lock_Device(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Get_Exclusive(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Unlock_Device(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Update_File_System_Cache(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues
        os_Unmount_File_Systems_On_Device(const tDevice* M_NONNULL device);

    OPENSEA_TRANSPORT_API M_PARAM_RO(1) eReturnValues os_Erase_Boot_Sectors(const tDevice* M_NONNULL device);

#if defined(__cplusplus)
}
#endif
