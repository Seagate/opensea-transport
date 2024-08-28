// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2017-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file common_uefi.h
// \brief Definitions and function calls (and includes) specific to UEFI

#pragma once

#include "scsi_helper.h"
#include "nvme_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    // \fn send_IO(scsiIoCtx * scsiIoCtx)
    // \brief Function to send a ioctl after converting it from the ScsiIoCtx to OS tSPTIoContext
    // \param ScsiIoCtx
    // \return SUCCESS - pass, !SUCCESS fail or something went wrong
    eReturnValues send_IO(ScsiIoCtx *scsiIoCtx);

    //This is the maximum timeout a command can use in UEFI...which is nearly infinite to begin with
    //NOTE: UEFI also supports an infinite timeout, but that is checked in a separate function
#define UEFI_MAX_CMD_TIMEOUT_SECONDS UINT32_MAX //Technically, max seconds is 18446744074, but I don't want to switch to a 64bit for the timeout. Anything with this value will round up to infinite in UEFI...where a timeout this long may as well be infinite

    //If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
    OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void);

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
    eReturnValues pci_Read_Bar_Reg(tDevice * device, uint8_t * pData, uint32_t dataSize);

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
    eReturnValues send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx);


    //-----------------------------------------------------------------------------
    //
    //  getpagesize()
    //
    //! \brief   Description:  Windows equivalent of Linux's getpagesize
    //
    //  Entry:
    //!
    //  Exit:
    //!   \return long page size. 
    //
    //-----------------------------------------------------------------------------
	long getpagesize(void);

    OPENSEA_TRANSPORT_API eReturnValues os_nvme_Reset(tDevice *device);
    OPENSEA_TRANSPORT_API eReturnValues os_nvme_Subsystem_Reset(tDevice *device);

    OPENSEA_TRANSPORT_API eReturnValues os_Lock_Device(tDevice *device);

    OPENSEA_TRANSPORT_API eReturnValues os_Unlock_Device(tDevice *device);

    OPENSEA_TRANSPORT_API eReturnValues os_Update_File_System_Cache(tDevice* device);

    OPENSEA_TRANSPORT_API eReturnValues os_Unmount_File_Systems_On_Device(tDevice *device);

    OPENSEA_TRANSPORT_API eReturnValues os_Erase_Boot_Sectors(tDevice* device);

#if defined (__cplusplus)
}
#endif
