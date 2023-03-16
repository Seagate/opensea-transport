//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2022 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 

#pragma once

#include "scsi_helper.h"
#include "sat_helper.h"
#include "nvme_helper.h"
#include "common_public.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    #include <stdlib.h> // for size_t types
    #include <stdio.h>  // for printf
    #include <string.h> // For memset
    #include <unistd.h> // For getpagesize

    #define AIX_PHYSICAL_DRIVE  "/dev/hdisk" 

    #define AIX_MAX_CMD_TIMEOUT_SECONDS UINT32_MAX

    //If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
    OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void);

    // \fn send_IO(scsiIoCtx * scsiIoCtx)
    // \brief Function to send IO to the device.
    // \param scsiIoCtx
    int send_IO( ScsiIoCtx *scsiIoCtx );

    //-----------------------------------------------------------------------------
    //
    //  os_Device_Reset(tDevice *device)
    //
    //! \brief   Description:  Attempts a device reset through OS functions available. NOTE: This won't work on every device
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!   
    //! 
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device, OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int os_Device_Reset(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  os_Bus_Reset(tDevice *device)
    //
    //! \brief   Description:  Attempts a bus reset through OS functions available. NOTE: This won't work on every device
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!   
    //! 
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device, OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int os_Bus_Reset(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  os_Controller_Reset(tDevice *device)
    //
    //! \brief   Description:  Attempts a controller reset through OS functions available. NOTE: This won't work on every device
    //
    //  Entry:
    //!   \param[in]  device = pointer to device context!   
    //! 
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device, OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int os_Controller_Reset(tDevice *device);


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
    int pci_Read_Bar_Reg( tDevice * device, uint8_t * pData, uint32_t dataSize );

    int send_NVMe_IO(nvmeCmdCtx *nvmeIoCtx);

    //to be used with a deep scan???
    //int nvme_Namespace_Rescan(int fd);//rescans a controller for namespaces. This must be a file descriptor without a namespace. EX: /dev/nvme0 and NOT /dev/nvme0n1

    OPENSEA_TRANSPORT_API int os_nvme_Reset(tDevice *device);

    OPENSEA_TRANSPORT_API int os_nvme_Subsystem_Reset(tDevice *device);

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
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device, OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int os_Lock_Device(tDevice *device);

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
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device, OS_COMMAND_BLOCKED = failed to perform the reset
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int os_Unlock_Device(tDevice *device);

    OPENSEA_TRANSPORT_API int os_Update_File_System_Cache(tDevice* device);

    OPENSEA_TRANSPORT_API int os_Unmount_File_Systems_On_Device(tDevice *device);

    OPENSEA_TRANSPORT_API int os_Erase_Boot_Sectors(tDevice* device);

#if defined (__cplusplus)
}
#endif