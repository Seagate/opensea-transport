//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file sg_helper.h
// \brief Defines the constants structures specific to Linux OS. 
//
#ifndef SG_HELPER_H
#define SG_HELPER_H

#include "scsi_helper.h"
#include "sat_helper.h"
#include "nvme_helper.h"
#include "common_public.h"

#if defined (__cplusplus)
extern "C"
{
#endif

// \file sg_helper.h
// \brief Defines the constants structures and function headers to help parse scsi drives.

    #include <stdlib.h> // for size_t types
    #include <stdio.h>  // for printf
    #include <string.h> // For memset
    #include <unistd.h> // For getpagesize
// \todo Figure out which scsi.h & sg.h should we be including kernel specific or in /usr/..../include
    #include "nvme_helper.h"

#define SG_PHYSICAL_DRIVE   "/dev/sg" //followed by a number
#define SD_PHYSICAL_DRIVE   "/dev/sd" //followed by a letter
#define BSG_PHYSICAL_DRIVE  "/dev/bsg/" //remaining part of the handle is h:c:t:l
#define OPENSEA_PATH_MAX	PATH_MAX

    //This is the maximum timeout a command can use in SG passthrough with linux...1193 hours
    //NOTE: SG also supports an infinite timeout, but that is checked in a separate function
#define SG_MAX_CMD_TIMEOUT_SECONDS 4294967

    //If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
    OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void);

//SG Driver status's since they are not available through standard includes we're using

#ifndef OPENSEA_SG_ERR_DRIVER_MASK
#define OPENSEA_SG_ERR_DRIVER_MASK 0x0F
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_OK
#define OPENSEA_SG_ERR_DRIVER_OK 0x00
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_BUSY
#define OPENSEA_SG_ERR_DRIVER_BUSY 0x01
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_SOFT
#define OPENSEA_SG_ERR_DRIVER_SOFT 0x02
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_MEDIA
#define OPENSEA_SG_ERR_DRIVER_MEDIA 0x03
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_ERROR
#define OPENSEA_SG_ERR_DRIVER_ERROR 0x04
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_INVALID
#define OPENSEA_SG_ERR_DRIVER_INVALID 0x05
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_TIMEOUT
#define OPENSEA_SG_ERR_DRIVER_TIMEOUT 0x06
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_HARD
#define OPENSEA_SG_ERR_DRIVER_HARD 0x07
#endif

#ifndef OPENSEA_SG_ERR_DRIVER_SENSE
#define OPENSEA_SG_ERR_DRIVER_SENSE 0x08
#endif

//Driver error suggestions
#ifndef OPENSEA_SG_ERR_SUGGEST_MASK
#define OPENSEA_SG_ERR_SUGGEST_MASK 0xF0
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_NONE
#define OPENSEA_SG_ERR_SUGGEST_NONE 0x00
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_RETRY
#define OPENSEA_SG_ERR_SUGGEST_RETRY 0x10
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_ABORT
#define OPENSEA_SG_ERR_SUGGEST_ABORT 0x20
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_REMAP
#define OPENSEA_SG_ERR_SUGGEST_REMAP 0x30
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_DIE
#define OPENSEA_SG_ERR_SUGGEST_DIE 0x40
#endif

#ifndef OPENSEA_SG_ERR_SUGGEST_SENSE
#define OPENSEA_SG_ERR_SUGGEST_SENSE 0x80
#endif

//Host errors
#ifndef OPENSEA_SG_ERR_DID_OK
#define OPENSEA_SG_ERR_DID_OK 0x0000
#endif

#ifndef OPENSEA_SG_ERR_DID_NO_CONNECT
#define OPENSEA_SG_ERR_DID_NO_CONNECT 0x0001
#endif

#ifndef OPENSEA_SG_ERR_DID_BUS_BUSY
#define OPENSEA_SG_ERR_DID_BUS_BUSY 0x0002
#endif

#ifndef OPENSEA_SG_ERR_DID_TIME_OUT
#define OPENSEA_SG_ERR_DID_TIME_OUT 0x0003
#endif

#ifndef OPENSEA_SG_ERR_DID_BAD_TARGET
#define OPENSEA_SG_ERR_DID_BAD_TARGET 0x0004
#endif

#ifndef OPENSEA_SG_ERR_DID_ABORT
#define OPENSEA_SG_ERR_DID_ABORT 0x0005
#endif

#ifndef OPENSEA_SG_ERR_DID_PARITY
#define OPENSEA_SG_ERR_DID_PARITY 0x0006
#endif

#ifndef OPENSEA_SG_ERR_DID_ERROR
#define OPENSEA_SG_ERR_DID_ERROR 0x0007
#endif

#ifndef OPENSEA_SG_ERR_DID_RESET
#define OPENSEA_SG_ERR_DID_RESET 0x0008
#endif

#ifndef OPENSEA_SG_ERR_DID_BAD_INTR
#define OPENSEA_SG_ERR_DID_BAD_INTR 0x0009
#endif

#ifndef OPENSEA_SG_ERR_DID_PASSTHROUGH
#define OPENSEA_SG_ERR_DID_PASSTHROUGH 0x000A
#endif

#ifndef OPENSEA_SG_ERR_DID_SOFT_ERROR
#define OPENSEA_SG_ERR_DID_SOFT_ERROR 0x000B
#endif

// \fn send_sg_io(scsiIoCtx * scsiIoCtx)
// \brief Function to send a SG_IO ioctl
// \param scsiIoCtx
    int send_sg_io( ScsiIoCtx *scsiIoCtx );

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


#endif

int map_Block_To_Generic_Handle(const char *handle, char **genericHandle, char **blockHandle);

int device_Reset(int fd);

int bus_Reset(int fd);

int host_Reset(int fd);

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
