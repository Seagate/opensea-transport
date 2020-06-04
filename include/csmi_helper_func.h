//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2020 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
#include "sata_types.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    OPENSEA_TRANSPORT_API int csmi_Get_Driver_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_Controller_Configuration(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_Controller_Status(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CNTLR_STATUS_BUFFER ctrlStatusBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Controller_Firmware_Download(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER firmwareBuffer, uint32_t firmwareBufferTotalLength, uint32_t downloadFlags, eVerbosityLevels verbosity, uint32_t timeoutSeconds);

    OPENSEA_TRANSPORT_API int csmi_Get_RAID_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_INFO_BUFFER raidInfoBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_RAID_Config(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_CONFIG_BUFFER raidConfigBuffer, uint32_t raidConfigBufferTotalSize, uint32_t raidSetIndex, uint8_t dataType, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_Phy_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_PHY_INFO_BUFFER phyInfoBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Set_Phy_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_SET_PHY_INFO_BUFFER phyInfoBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_Link_Errors(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_LINK_ERRORS_BUFFER linkErrorsBuffer, uint8_t phyIdentifier, bool resetCounts, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_SATA_Signature(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_SATA_SIGNATURE_BUFFER sataSignatureBuffer, uint8_t phyIdentifier, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_SCSI_Address(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddressBuffer, uint8_t sasAddress[8], uint8_t lun[8], eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_Device_Address(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_GET_DEVICE_ADDRESS_BUFFER deviceAddressBuffer, uint8_t hostIndex, uint8_t path, uint8_t target, uint8_t lun, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int csmi_Get_Connector_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CONNECTOR_INFO_BUFFER connectorInfoBuffer, eVerbosityLevels verbosity);

    OPENSEA_TRANSPORT_API int send_CSMI_IO(ScsiIoCtx *scsiIoCtx);

    //Function to check for CSMI IO support on non-RAID devices (from Windows mostly since this can get around Win passthrough restrictions in some cases)
    //This requests driver info and controller config. Both IOCTLs must pass.
    bool handle_Supports_CSMI_IO(CSMI_HANDLE deviceHandle, eVerbosityLevels verbosity);

#if defined (_WIN32)
    //Windows only since Intel RST additions are Windows only enhancements
    bool device_Supports_CSMI_With_RST(tDevice *device);
#endif

    OPENSEA_TRANSPORT_API int jbod_Setup_CSMI_Info(CSMI_HANDLE deviceHandle, tDevice *device, uint8_t controllerNumber, uint8_t hostController, uint8_t pathidBus, uint8_t targetID, uint8_t lun);

#if defined (__cplusplus)
}
#endif
#endif //ENABLE_CSMI