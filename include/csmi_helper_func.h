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
// \file csmi_helper_func.h
// \brief Defines the function calls to help with CSMI implementation. This tries to be generic for any OS, even though Windows is the only known supported OS (pending what driver you use)

#pragma once

#if defined (ENABLE_CSMI)

#include "common.h"
#include <stdint.h>
#include "csmisas.h"
#include "scsi_helper.h"
#include "csmi_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  close_CSMI_Device()
    //
    //! \brief   Description:  Given a device, free the memory allocated from get_CSMI_Device and set an invalid handle value.
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int close_CSMI_Device(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Phy_Info(tDevice *device, PCSMI_SAS_PHY_INFO_BUFFER PhyInfo)
    //
    //! \brief   Description:  Issue the CSMI IOCTL to get the phy information
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!   \param[out] PhyInfo = pointer to phy info buffer structure.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_Phy_Info(tDevice *device, PCSMI_SAS_PHY_INFO_BUFFER PhyInfo);
    
    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Controller_Info(tDevice *device, PCSMI_SAS_CNTLR_CONFIG controllerInfo)
    //
    //! \brief   Description:  Issue the CSMI IOCTL to get the controller information
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!   \param[out] controllerInfo = pointer to controler config buffer structure.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_Controller_Info(tDevice *device, PCSMI_SAS_CNTLR_CONFIG controllerInfo);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Driver_Info(tDevice *device, PCSMI_SAS_DRIVER_INFO driverInfo)
    //
    //! \brief   Description:  Issue the CSMI IOCTL to get the driver information
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!   \param[out] driverInfo = pointer to driver config buffer structure.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_Driver_Info(tDevice *device, PCSMI_SAS_DRIVER_INFO driverInfo);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_SATA_Signature(tDevice *device, PCSMI_SAS_SATA_SIGNATURE signature)
    //
    //! \brief   Description:  Issue the CSMI IOCTL to get the SATA signature. NOTE: This does not seem to return the correct signature, but a dummy value - TJE
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!   \param[out] signature = pointer to sata signature buffer structure.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_SATA_Signature(tDevice *device, PCSMI_SAS_SATA_SIGNATURE signature);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_SCSI_Address(tDevice *device, ptrCSMISCSIAddress scsiAddress)
    //
    //! \brief   Description:  Issue the CSMI IOCTL to get the SCSI Address
    //
    //  Entry:
    //!   \param[in] device = device stuct that holds device information.
    //!   \param[out] scsiAddress = pointer to scsi address buffer structure.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_SCSI_Address(tDevice *device, ptrCSMISCSIAddress scsiAddress);

    //-----------------------------------------------------------------------------
    //
    //  build_CSMI_Passthrough_CDB(uint8_t cdb[16], ataPassthroughCommand * ataPtCmd)
    //
    //! \brief   Description:  build the vendor unique ATA passthrough CDB defined in the original CSMI spec for when STP is not supported...not used as no controllers/drivers seem to use it - TJE
    //
    //  Entry:
    //!   \param[out] cdb = pointer to 16 byte long array to hold the built CDB
    //!   \param[in] ataPtCmd = pointer to structure defining an ATA command
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int build_CSMI_Passthrough_CDB(uint8_t cdb[16], ataPassthroughCommand * ataPtCmd);

    //-----------------------------------------------------------------------------
    //
    //  send_Vendor_Unique_ATA_Passthrough(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  Send the CSMI vendor unique ATA passthrough CDB using SSP passthrough to a device. (NOT used since no supporting device has been found) - TJE
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - pointer to the scsiIoCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int send_Vendor_Unique_ATA_Passthrough(ScsiIoCtx *scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  send_SSP_Passthrough_Command(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  Send a SCSI CDB using CSMI SSP passthrough IOCTL
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - pointer to the scsiIoCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int send_SSP_Passthrough_Command(ScsiIoCtx *scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  build_H2D_fis(FIS_REG_H2D *fis, ataTFRBlock *tfr)
    //
    //! \brief   Description:  Build a H2D FIS for CSMI STP passthrough to issue
    //
    //  Entry:
    //!   \param[out] fis - pointer to a H2D fis structure to fill in
    //!   \param[in] ataTFRBlock - pointer to ATA TFR Block structure describing a command to issue.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API void build_H2D_fis(FIS_REG_H2D *fis, ataTFRBlock *tfr);

    //-----------------------------------------------------------------------------
    //
    //  send_STP_Passthrough_Command(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  Send a ATA command using CSMI STP passthrough IOCTL
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - pointer to the scsiIoCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int send_STP_Passthrough_Command(ScsiIoCtx *scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Device(const char *filename, tDevice *device)
    //
    //! \brief   Description:  Open a handle to the specified CSMI device. handle should be formatted as \\.\SCSI<controller>:<port or scsi address>
    //
    //  Entry:
    //!   \param[in] filename - string that is a device handle
    //!   \param[out] device = pointer to device structure to fill in with everything to talk over CSMI to the specified handle
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_Device(const char *filename, tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
    //
    //! \brief   Description:  Get the number of available CSMI devices on the system
    //
    //  Entry:
    //!   \param[out] numberOfDevices - pointer to uint32_t that will hold the number of CSMI devices on a system
    //!   \param[in] flags = flags to filter scan (NOT USED)
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_CSMI_Device_Count(uint32_t * numberOfDevices, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
    //
    //! \brief   Description:  Get the number of available CSMI devices on the system
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
    OPENSEA_TRANSPORT_API int get_CSMI_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags);

    //-----------------------------------------------------------------------------
    //
    //  send_CSMI_IO(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  send a command through a CSMI IOCTL
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - pointer to the scsiIoCtx structure defining a command to be sent
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int send_CSMI_IO(ScsiIoCtx *scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  scan_And_Print_CSMI_Devs(unsigned int flags, OutputInfo *outputInfo)
    //
    //! \brief   Description:  scan and print out only CSMI devices
    //
    //  Entry:
    //!   \param[in] flags = flags to filter the output
    //!   \param[in] outputInfo - pointer to the output info structure for tools changing from standard text output (Not supported at this time)
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API void scan_And_Print_CSMI_Devs(unsigned int flags, OutputInfo *outputInfo);

    //-----------------------------------------------------------------------------
    //
    //  print_CSMI_Device_Info(tDevice * device)
    //
    //! \brief   Description:  for a given device output information we read and are using about the CSMI device we are talking to (driver, phy, signature, etc information)
    //
    //  Entry:
    //!   \param[in] device = pointer to CSMI device.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API void print_CSMI_Device_Info(tDevice * device);

    //-----------------------------------------------------------------------------
    //
    //  print_FIS(uint8_t fis[20])
    //
    //! \brief   Description:  Print out a FIS for debugging purposes
    //
    //  Entry:
    //!   \param[in] fis = pointer to 20 byte array describing a generic FIS
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API void print_FIS(uint8_t fis[20]);

#if defined (__cplusplus)
}
#endif
#endif