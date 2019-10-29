//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file of_nvme_helper_func.h
// \brief Defines the function calls to help with open fabrics NVME implementation

#pragma once

#if defined (ENABLE_OFNVME) && !defined (DISABLE_NVME_PASSTHROUGH)

#include "common.h"
#include <stdint.h>
#include "of_nvmeIoctl.h"
#include "of_nvme_helper.h"
#include "common_public.h"
#include "nvme_helper.h"
#include "scsi_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  close_OFNVME_Device()
    //
    //! \brief   Description:  Given a device, free the memory allocated from get_OFNVME_Device and set an invalid handle value.
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int close_OFNVME_Device(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_OFNVME_Device(const char *filename, tDevice *device)
    //
    //! \brief   Description:  Open a handle to the specified OFNVME device. handle should be formatted as \\.\SCSI<controller>:<port or scsi address>
    //
    //  Entry:
    //!   \param[in] filename - string that is a device handle
    //!   \param[out] device = pointer to device structure to fill in with everything to talk over OFNVME to the specified handle
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_OFNVME_Device(const char *filename, tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_OFNVME_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
    //
    //! \brief   Description:  Get the number of available OFNVME devices on the system
    //
    //  Entry:
    //!   \param[out] numberOfDevices - pointer to uint32_t that will hold the number of OFNVME devices on a system
    //!   \param[in] flags = flags to filter scan (NOT USED)
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_OFNVME_Device_Count(uint32_t * numberOfDevices, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  get_OFNVME_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
    //
    //! \brief   Description:  Get the number of available OFNVME devices on the system
    //
    //  Entry:
    //!   \param[out] ptrToDeviceList = pointer to list of devices that has already been allocated based on the result of getting the device count
    //!   \param[in] sizeInBytes - number of bytes in size of the allocated device list
    //!   \param[in] ver = filled in version structure for version compatibility validation
    //!   \param[in] flags = flags to filter scan (NOT USED)
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_OFNVME_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  send_OFNVME_SCSI_IO(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  send a SCSI command through a OFNVME IOCTL (this will be translated by software in the library)
    //
    //  Entry:
    //!   \param[in] nvmeIoCtx - pointer to the scsiIoCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int send_OFNVME_SCSI_IO(ScsiIoCtx *scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  send_OFNVME_IO(nvmeCmdCtx *nvmeIoCtx)
    //
    //! \brief   Description:  send a command through a OFNVME IOCTL
    //
    //  Entry:
    //!   \param[in] nvmeIoCtx - pointer to the nvmeCmdCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int send_OFNVME_IO(nvmeCmdCtx *nvmeIoCtx);

#if defined (__cplusplus)
}
#endif
#endif//ENABLE_OFNVME
