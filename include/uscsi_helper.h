// SPDX-License-Identifier: MPL-2.0

//! \file uscsi_helper.h
//! \brief types/functions to assist with the uscsi interface in Solaris and Illumos
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_types.h"
#include "nvme_helper.h"
#include "sat_helper.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

    // This is the maximum timeout a command can use in uscsi passthrough with Solaris...18.2 hours
#define USCSI_MAX_CMD_TIMEOUT_SECONDS UINT16_MAX

    // If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued,
    // otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
    OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void);

    //-----------------------------------------------------------------------------
    //
    //  send_uscsi_io()
    //
    //! \brief   Description:  Function to send an IO using the Solaris uscsi passthrough
    //
    //  Entry:
    //!   \param[in] scsiIoCtx =  pointer to a scsiIoCtx struct which contains the information necessary to send a
    //!   command.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues send_uscsi_io(ScsiIoCtx* scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  send_IO()
    //
    //! \brief   Description:  Function to send an IO
    //
    //  Entry:
    //!   \param[in] scsiIoCtx =  pointer to a scsiIoCtx struct which contains the information necessary to send a
    //!   command.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues send_IO(ScsiIoCtx* scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  os_Device_Reset(tDevice *device)
    //
    //! \brief   Description:  Attempts a device reset through OS functions available. NOTE: This won't work on every
    //! device
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Device_Reset(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  os_Bus_Reset(tDevice *device)
    //
    //! \brief   Description:  Attempts a bus reset through OS functions available. NOTE: This won't work on every
    //! device
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Bus_Reset(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  os_Controller_Reset(tDevice *device)
    //
    //! \brief   Description:  Attempts a controller reset through OS functions available. NOTE: This won't work on
    //! every device
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Controller_Reset(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  pci_Read_Bar_Reg()
    //
    //! \brief   Description:  Function to Read PCI Bar register
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!
    //!   \param[out] pData =  pointer to data that need to be filled.
    //!                        this needs to be at least the size of a page
    //!                        e.g. getPageSize() in Linux
    //!   \param[out] dataSize =  size of the data
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(2, 3) eReturnValues pci_Read_Bar_Reg(tDevice* device, uint8_t* pData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  send_NVMe_IO()
    //
    //! \brief   Description:  Function to send a NVMe command to a device
    //
    //  Entry:
    //!   \param[in] scsiIoCtx = pointer to IO Context!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx);

    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_nvme_Reset(tDevice* device);

    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_nvme_Subsystem_Reset(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  os_Lock_Device(tDevice *device)
    //
    //! \brief   Description:  removes the O_NONBLOCK flag from the handle to get exclusive access to the device.
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Lock_Device(tDevice* device);

    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Get_Exclusive(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  os_Unlock_Device(tDevice *device)
    //
    //! \brief   Description:  adds the O_NONBLOCK flag to the handle to restore shared access to the device.
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Unlock_Device(tDevice* device);

    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        os_Update_File_System_Cache(tDevice* device);

    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        os_Unmount_File_Systems_On_Device(tDevice* device);

    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues os_Erase_Boot_Sectors(tDevice* device);

#if defined(__cplusplus)
}
#endif
