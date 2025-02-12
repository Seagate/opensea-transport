// SPDX-License-Identifier: MPL-2.0

//! \file vm_nvme_lib.h
//! \brief Defines the constants structures specific to VMWare Cross compiler for ESXi
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2018-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <vm_nvme.h>
#include <vm_nvme_mgmt.h>

/**
 * Command timeout in microseconds
 */
#define ADMIN_TIMEOUT (2 * 1000 * 1000) /* 2 seconds */

/**
 * for firmware download
 */
// TODO: confirm max transfer size
#define NVME_MAX_XFER_SIZE   (8 * 1024)
#define MAX_FW_SLOT          7
#define VM_FW_REV_LEN        8
#define MAX_ADAPTER_NAME_LEN 64
#define MAX_FW_PATH_LEN      512

/**
 * firmware activate action code
 */
#define NVME_FIRMWARE_ACTIVATE_ACTION_NOACT    0
#define NVME_FIRMWARE_ACTIVATE_ACTION_DLACT    1
#define NVME_FIRMWARE_ACTIVATE_ACTION_ACTIVATE 2
#define NVME_FIRMWARE_ACTIVATE_ACTION_RESERVED 3

/**
 * firmware activate successful but need reboot
 */
#define NVME_NEED_COLD_REBOOT 0x1

/**
 * Adapter instance list
 */
struct nvme_adapter_list
{
    vmk_uint32             count;
    struct nvmeAdapterInfo adapters[NVME_MAX_ADAPTERS];
};

/**
 * Device handle
 */
struct nvme_handle
{
    /** vmhba name */
    char name[VMK_MISC_NAME_MAX];
    /** management handle */
    vmk_MgmtUserHandle handle;
};

/**
 * Global data to hold all active NVMe adapters
 */
extern struct nvme_adapter_list adapterList;

/**
 * NVMe management interfaces
 */
struct nvme_handle* Nvme_Open(struct nvme_adapter_list* adapters, const char* name);
void                Nvme_Close(struct nvme_handle* handle);
int                 Nvme_GetAdapterList(struct nvme_adapter_list* list);
int                 Nvme_AdminPassthru(struct nvme_handle* handle, struct usr_io* uio);
int                 Nvme_AdminPassthru_error(struct nvme_handle* handle, int cmd, struct usr_io* uio);
int                 Nvme_Ioctl(struct nvme_handle* handle, int cmd, struct usr_io* uio);
int                 Nvme_SetLogLevel(int loglevel, int debuglevel);

// if uio.status begins with 0X?BAD????, then use this function to look up the error meaning.
const char* get_VMK_API_Error(VMK_ReturnStatus status);
