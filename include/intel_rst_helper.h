// SPDX-License-Identifier: MPL-2.0

//! \file intel_rst_helper.h
//! \brief Defines the constants structures to help with CSMI implementation. This tries to be generic for any OS, even
// though Windows is the only known supported OS (pending what driver you use)
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(ENABLE_INTEL_RST)
#    include "intel_rst_defs.h"
#    include "scsi_helper.h"

#    include "nvme_helper.h"

#    if defined(__cplusplus)
extern "C"
{
#    endif

    //-----------------------------------------------------------------------------
    //
    //  send_Intel_NVM_Command(nvmeCmdCtx *nvmeIoCtx)
    //
    //! \brief   Description:  Sends an NVMe command through the Intel RST passthrough IOCTL.
    //!          NOTE: This driver filters commands, so many may not work. This function does not perform any filtering
    //!          and attempts all commands it receives. It is recommended this is used for any NVMe command received. It
    //!          will call the appropriate firmware download IOCTL if firmware download commands are received.
    //
    //  Entry:
    //!   \param[in] nvmeIoCtx - NVMe context structure that holds all the information necessary to issue an NVMe
    //!   command to a device.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues send_Intel_NVM_Command(nvmeCmdCtx* nvmeIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  send_Intel_NVM_Firmware_Download(nvmeCmdCtx *nvmeIoCtx)
    //
    //! \brief   Description:  Sends an NVMe Firmware download command through the Intel RST passthrough IOCTL.
    //
    //  Entry:
    //!   \param[in] nvmeIoCtx - NVMe context structure that holds all the information necessary to issue an NVMe
    //!   command to a device.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues send_Intel_NVM_Firmware_Download(nvmeCmdCtx* nvmeIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  send_Intel_NVM_SCSI_Command(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  Handles reception of a SCSI command by sending it through software translation.
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - SCSI context structure that holds all the information necessary to translate a SCSI
    //!   command to a device.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues send_Intel_NVM_SCSI_Command(ScsiIoCtx* scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  supports_Intel_Firmware_Download(tDevice *device)
    //
    //! \brief   Description:  Checks if the provided device supports Intel's Firmware update IOCTLs. Due to how this
    //! works, CSMI may be necessary to make this work properly
    //
    //  Entry:
    //!   \param[in] device - pointer to device structure.
    //!
    //!
    //  Exit:
    //!   \return true = supports Intel RST firmware update IOCTLs, false = not supported.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API bool supports_Intel_Firmware_Download(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  send_Intel_Firmware_Download(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description: Sends an Intel RST Firmware update IOCTL based on provided SCSI parameters
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - SCSI context structure that holds all the information necessary to send a firmware
    //!   update (ATA or SCSI command)
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues send_Intel_Firmware_Download(ScsiIoCtx* scsiIoCtx);

#    if defined(__cplusplus)
}
#    endif

#endif // ENABLE_INTEL_RST
