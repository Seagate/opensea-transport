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
// \file sg_helper.h
// \brief Defines the constants structures specific to Linux OS. 
//
#ifndef SG_HELPER_H
#define SG_HELPER_H

#include "scsi_helper.h"
#include "sat_helper.h"
#if !defined(DISABLE_NVME_PASSTHROUGH)
#include "nvme_helper.h"
#endif
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
    #include <scsi/sg.h>
    #include <scsi/scsi.h>
#if !defined(DISABLE_NVME_PASSTHROUGH)
    #if __GNUC__ > 5 //GCC5 and higher have a #if __has_include check that we can use to make this easier :)
        #if __has_include (<linux/nvme_ioctl.h>)
            #include <linux/nvme_ioctl.h>
            #define SEA_NVME_IOCTL_H
        #else
            #if __has_include (<linux/nvme.h>)
                #include <linux/nvme.h>
                #define SEA_NVME_H
            #else
                #if __has_include (<uapi/nvme.h>)
                    #include <uapi/nvme.h>
                    #define SEA_UAPI_NVME_H
                #else
                    //no NVMe includes available so disable support for it.
                    #define DISABLE_NVME_PASSTHROUGH
                #endif
            #endif
        #endif
    #else
        #include <linux/version.h> //To be used for checking which include to use (Only partially narrowed down)
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)//in version 4.4.??+, header is in linux/nvme_ioctl.h
            #include <linux/nvme_ioctl.h>
            #define SEA_NVME_IOCTL_H
        #else
            #if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0) && LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0) //in 3.x - 4.4.??, header is in linux/nvme.h
                #include <linux/nvme.h>
                #define SEA_NVME_H
            #else
                #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32) && LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0) //in earlier versions, it is in either uapi/nvme.h or uapi/linux/nvme.h IF it even exists
                    //this is what I see to include on CentOS6.8 - TJE
                    #include <uapi/nvme.h>
                    #define SEA_UAPI_NVME_H
                #else
                    //no NVMe includes available so disable support for it.
                    #define DISABLE_NVME_PASSTHROUGH
                #endif
            #endif
        #endif
    #endif
    #include "nvme_helper.h"
#endif

#define SG_PHYSICAL_DRIVE	"/dev/sg"
#define SD_PHYSICAL_DRIVE   "/dev/sd"

// \fn get_Device(char * filename)
// \brief Given a device name (e.g. /dev/sg0) returns the device descriptor
// \details Function opens the device & then sends a SG_GET_VERSION_NUM
//          if everything goes well, it returns a sg file descriptor & fills out other info.
// \todo Add a flags param to allow user to open with O_RDWR, O_RDONLY etc.
// \param filename name of the device to open
// \returns device device structure
//int get_Device(char * filename, device);

// \fn decipher_maskedStatus
// \brief Function to figure out what the maskedStatus means
// \param maskedStatus from the sg structure
    void decipher_maskedStatus( unsigned char maskedStatus );

// \fn send_sg_io(scsiIoCtx * scsiIoCtx)
// \brief Function to send a SG_IO ioctl
// \param scsiIoCtx
    int send_sg_io( ScsiIoCtx *scsiIoCtx );

// \fn send_IO(scsiIoCtx * scsiIoCtx)
// \brief Function to send IO to the device.
// \param scsiIoCtx
    int send_IO( ScsiIoCtx *scsiIoCtx );

#if !defined(DISABLE_NVME_PASSTHROUGH)
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

#endif

int map_sg_to_sd(char* filename, char *sgName, char *sdName);


int device_Reset(int fd);

int bus_Reset(int fd);

int host_Reset(int fd);

    #if defined (__cplusplus)
}
    #endif

#endif
