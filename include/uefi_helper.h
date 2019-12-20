//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

//TODO: UEFI includes

#if defined (__cplusplus)
extern "C"
{
#endif

    // \fn send_IO(scsiIoCtx * scsiIoCtx)
    // \brief Function to send a ioctl after converting it from the ScsiIoCtx to OS tSPTIoContext
    // \param ScsiIoCtx
    // \return SUCCESS - pass, !SUCCESS fail or something went wrong
    int send_IO(ScsiIoCtx *scsiIoCtx);

#if !defined (DISABLE_NVME_PASSTHROUGH)
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
    int pci_Read_Bar_Reg(tDevice * device, uint8_t * pData, uint32_t dataSize);

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
    int send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx);


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

    int os_nvme_Reset(tDevice *device);
    int os_nvme_Subsystem_Reset(tDevice *device);

#endif


#if defined (__cplusplus)
}
#endif
