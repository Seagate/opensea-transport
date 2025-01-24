// SPDX-License-Identifier: MPL-2.0

//! \file csmi_helper_func.h
//! \brief Defines the function calls to help with CSMI implementation. This tries to be generic for any OS, even though
//! Windows is the only known supported OS (pending what driver you use)
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(ENABLE_CSMI)

#    include "common_types.h"
#    include <stdint.h>
#    if defined(_WIN32) && !defined(_NTDDSCSIH_)
#        include <ntddscsi.h>
#    endif
#    include "csmi_helper.h"
#    include "external/csmi/csmisas.h"
#    include "raid_scan_helper.h"
#    include "sata_types.h"
#    include "scsi_helper.h"

#    if defined(__cplusplus)
extern "C"
{
#    endif

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Driver_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_DRIVER_INFO_BUFFER
    //  driverInfoBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get Driver Info IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] driverInfoBuffer - CSMI spec driver info buffer to use. \param[in] verbosity - the
    //!   level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_Driver_Info(CSMI_HANDLE                  deviceHandle,
                                                             uint32_t                     controllerNumber,
                                                             PCSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer,
                                                             eVerbosityLevels             verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Controller_Configuration(CSMI_HANDLE deviceHandle, uint32_t controllerNumber,
    //  PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get Controller Configuration IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] ctrlConfigBuffer - CSMI spec controller configuration buffer buffer to use. \param[in]
    //!   verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues
    csmi_Get_Controller_Configuration(CSMI_HANDLE                   deviceHandle,
                                      uint32_t                      controllerNumber,
                                      PCSMI_SAS_CNTLR_CONFIG_BUFFER ctrlConfigBuffer,
                                      eVerbosityLevels              verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Controller_Status(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CNTLR_STATUS_BUFFER
    //  ctrlStatusBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Controller Status IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] ctrlStatusBuffer - CSMI spec controller status buffer buffer to use. \param[in]
    //!   verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_Controller_Status(CSMI_HANDLE                   deviceHandle,
                                                                   uint32_t                      controllerNumber,
                                                                   PCSMI_SAS_CNTLR_STATUS_BUFFER ctrlStatusBuffer,
                                                                   eVerbosityLevels              verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Controller_Firmware_Download(CSMI_HANDLE deviceHandle, uint32_t controllerNumber,
    //  PCSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER firmwareBuffer, uint32_t firmwareBufferTotalLength, uint32_t downloadFlags,
    //  eVerbosityLevels verbosity, uint32_t timeoutSeconds)
    //
    //! \brief   Description:  Sends the CSMI Controller Firmware Download IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] firmwareBuffer - CSMI spec controller status buffer buffer to use. Filled in with
    //!   controller firmware to flash \param[in] firmwareBufferTotalLength - length of the allocated firmwareBuffer so
    //!   that the IOs send the correct data length \param[in] downloadFlags - flags to control the download \param[in]
    //!   verbosity - the level of verbose output to use when performing this IO \param[in] timeoutSeconds - the number
    //!   of seconds to use for the timeout on downloading controller firmware.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    csmi_Controller_Firmware_Download(CSMI_HANDLE                        deviceHandle,
                                      uint32_t                           controllerNumber,
                                      PCSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER firmwareBuffer,
                                      uint32_t                           firmwareBufferTotalLength,
                                      uint32_t                           downloadFlags,
                                      eVerbosityLevels                   verbosity,
                                      uint32_t                           timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_RAID_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_INFO_BUFFER
    //  raidInfoBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get RAID Info IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] raidInfoBuffer - CSMI spec buffer to use. This should be empty. \param[in] verbosity -
    //!   the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_RAID_Info(CSMI_HANDLE                deviceHandle,
                                                           uint32_t                   controllerNumber,
                                                           PCSMI_SAS_RAID_INFO_BUFFER raidInfoBuffer,
                                                           eVerbosityLevels           verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_RAID_Config(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_CONFIG_BUFFER
    //  raidConfigBuffer, uint32_t raidConfigBufferTotalSize, uint32_t raidSetIndex, uint8_t dataType, eVerbosityLevels
    //  verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get RAID Config IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] raidConfigBuffer - CSMI spec buffer to use. This should be empty. \param[in]
    //!   raidConfigBufferTotalSize - size of full raid config buffer allocation. This needs to be passed in since this
    //!   should be allocated based on maximum number of drives in a given RAID set, as reported by get RAID info.
    //!   \param[in] raidSetIndex - which RAID set to get configuration of. This is for controllers that support
    //!   multiple RAIDs on a single controller. \param[in] dataType - what data to report about a RAID configuration.
    //!   This should be one of the bDataType's from the csmisas.h file. \param[in] verbosity - the level of verbose
    //!   output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_RAID_Config(CSMI_HANDLE                  deviceHandle,
                                                             uint32_t                     controllerNumber,
                                                             PCSMI_SAS_RAID_CONFIG_BUFFER raidConfigBuffer,
                                                             uint32_t                     raidConfigBufferTotalSize,
                                                             uint32_t                     raidSetIndex,
                                                             uint8_t                      dataType,
                                                             eVerbosityLevels             verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_RAID_Features(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_RAID_FEATURES_BUFFER
    //  raidFeaturesBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get RAID Features IOCTL. This is not supported on many CSMI RAIDs as it
    //! was not part of the original CSMI proposal.
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] raidFeaturesBuffer - CSMI spec buffer to use. This should be empty. \param[in]
    //!   verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_RAID_Features(CSMI_HANDLE                    deviceHandle,
                                                               uint32_t                       controllerNumber,
                                                               PCSMI_SAS_RAID_FEATURES_BUFFER raidFeaturesBuffer,
                                                               eVerbosityLevels               verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Phy_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_PHY_INFO_BUFFER phyInfoBuffer,
    //  eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get Phy Info IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] phyInfoBuffer - CSMI spec buffer to use. This should be empty. \param[in] verbosity -
    //!   the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_Phy_Info(CSMI_HANDLE               deviceHandle,
                                                          uint32_t                  controllerNumber,
                                                          PCSMI_SAS_PHY_INFO_BUFFER phyInfoBuffer,
                                                          eVerbosityLevels          verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Set_Phy_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_SET_PHY_INFO_BUFFER
    //  phyInfoBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Set Phy Info IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] phyInfoBuffer - CSMI spec buffer to use. This should be filled with the phy settings
    //!   being changed. \param[in] verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Set_Phy_Info(CSMI_HANDLE                   deviceHandle,
                                                          uint32_t                      controllerNumber,
                                                          PCSMI_SAS_SET_PHY_INFO_BUFFER phyInfoBuffer,
                                                          eVerbosityLevels              verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Link_Errors(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_LINK_ERRORS_BUFFER
    //  linkErrorsBuffer, uint8_t phyIdentifier, bool resetCounts, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get Link Errors IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] linkErrorsBuffer - CSMI spec buffer to use. This should be empty. \param[in]
    //!   phyIdentifier - phy to get link errors for \param[in] resetCounts - set to true will cause all counts to reset
    //!   after reading the current counts \param[in] verbosity - the level of verbose output to use when performing
    //!   this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_Link_Errors(CSMI_HANDLE                  deviceHandle,
                                                             uint32_t                     controllerNumber,
                                                             PCSMI_SAS_LINK_ERRORS_BUFFER linkErrorsBuffer,
                                                             uint8_t                      phyIdentifier,
                                                             bool                         resetCounts,
                                                             eVerbosityLevels             verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_SATA_Signature(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_SATA_SIGNATURE_BUFFER
    //  sataSignatureBuffer, uint8_t phyIdentifier, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get SATA Signature IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] sataSignatureBuffer - CSMI spec buffer to use. This should be empty \param[in]
    //!   phyIdentifier - phy ID to read signature for. This can help identify how a device was detected on boot/last
    //!   reset to detect the device type. \param[in] verbosity - the level of verbose output to use when performing
    //!   this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_SATA_Signature(CSMI_HANDLE                     deviceHandle,
                                                                uint32_t                        controllerNumber,
                                                                PCSMI_SAS_SATA_SIGNATURE_BUFFER sataSignatureBuffer,
                                                                uint8_t                         phyIdentifier,
                                                                eVerbosityLevels                verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_SCSI_Address(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_GET_SCSI_ADDRESS_BUFFER
    //  scsiAddressBuffer, uint8_t sasAddress[8], uint8_t lun[8], eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get SCSI Address IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] scsiAddressBuffer - CSMI spec buffer to use. This should be empty (filled on
    //!   successful completion) \param[in] sasAddress - SAS Address to use to convert to SCSI address. this may come
    //!   from RAID Config data. \param[in] lun - SAS Lun, 64bits that should be converted to SCSI Address. this may
    //!   come from RAID config data. \param[in] verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3, 4, 5)
    M_PARAM_RW(3)
    M_PARAM_RO(4)
    M_PARAM_RO(5)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_SCSI_Address(CSMI_HANDLE                       deviceHandle,
                                                              uint32_t                          controllerNumber,
                                                              PCSMI_SAS_GET_SCSI_ADDRESS_BUFFER scsiAddressBuffer,
                                                              uint8_t                           sasAddress[8],
                                                              uint8_t                           lun[8],
                                                              eVerbosityLevels                  verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Device_Address(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_GET_DEVICE_ADDRESS_BUFFER
    //  deviceAddressBuffer, uint8_t hostIndex, uint8_t path, uint8_t target, uint8_t lun, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get Device Address IOCTL
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] deviceAddressBuffer - CSMI spec buffer to use. This should be empty (filled on
    //!   successful completion) \param[in] hostIndex - host/controller number. Most likely available from some other
    //!   host specific reporting method \param[in] path - path ID, or bus. Most likely available from some other host
    //!   specific reporting method \param[in] target - target ID. Most likely available from some other host specific
    //!   reporting method \param[in] lun = logical unit number. Most likely available from some other host specific
    //!   reporting method \param[in] verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_Device_Address(CSMI_HANDLE                         deviceHandle,
                                                                uint32_t                            controllerNumber,
                                                                PCSMI_SAS_GET_DEVICE_ADDRESS_BUFFER deviceAddressBuffer,
                                                                uint8_t                             hostIndex,
                                                                uint8_t                             path,
                                                                uint8_t                             target,
                                                                uint8_t                             lun,
                                                                eVerbosityLevels                    verbosity);

    //-----------------------------------------------------------------------------
    //
    //  csmi_Get_Connector_Info(CSMI_HANDLE deviceHandle, uint32_t controllerNumber, PCSMI_SAS_CONNECTOR_INFO_BUFFER
    //  connectorInfoBuffer, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Sends the CSMI Get Conector Info IOCTL (Phy count reported in get Phy Info)
    //
    //  Entry:
    //!   \param[in] deviceHandle - operating system device handle value. Opened as \\.\SCSIX: on Windows, /dev/<hba> on
    //!   other OSs \param[in] controllerNumber - Linux only, controller number since Linux needs this in the
    //!   IOCTL_HEADER \param[in] connectorInfoBuffer - CSMI spec buffer to use. This should be empty (filled on
    //!   successful completion) \param[in] verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues csmi_Get_Connector_Info(CSMI_HANDLE                     deviceHandle,
                                                                uint32_t                        controllerNumber,
                                                                PCSMI_SAS_CONNECTOR_INFO_BUFFER connectorInfoBuffer,
                                                                eVerbosityLevels                verbosity);

    //-----------------------------------------------------------------------------
    //
    //  send_CSMI_IO(ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  Sends a SAS/SATA command based on CSMI flags from device discovery
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - holds all information pertinent to sending a SCSI or ATA command to a given device.
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues send_CSMI_IO(ScsiIoCtx* scsiIoCtx);

    //-----------------------------------------------------------------------------
    //
    //  handle_Supports_CSMI_IO(CSMI_HANDLE deviceHandle, eVerbosityLevels verbosity)
    //
    //! \brief   Description:  Quick check to see if CSMI is supported. Determined by success of both reading controller
    //! config and driver information. These are mandatory IOCTLs in CSMI spec.
    //
    //  Entry:
    //!   \param[in] scsiIoCtx - holds all information pertinent to sending a SCSI or ATA command to a given device.
    //!   \param[in] verbosity - the level of verbose output to use when performing this IO
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    bool handle_Supports_CSMI_IO(CSMI_HANDLE deviceHandle, eVerbosityLevels verbosity);

#    if defined(_WIN32)
    //-----------------------------------------------------------------------------
    //
    //  device_Supports_CSMI_With_RST(tDevice *device)
    //
    //! \brief   Description:  Checks if CSMI and Intel's RST IOCTLs are supported. This is for Windows only. This uses
    //! Intel's FWDL API to check if Intel IOCTLs are supported. It does not do a complete check of each Intel IOCTL.
    //
    //  Entry:
    //!   \param[in] device - device structure. This is intended to be used only Within Windows where this provides the
    //!   information necessary to perform this test.
    //!
    //!
    //  Exit:
    //!   \return true = CSMI and RST calls supported, false = RST calls not supported. CSMI may or may not be supported
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API bool device_Supports_CSMI_With_RST(tDevice* device);
#    endif

    //-----------------------------------------------------------------------------
    //
    //  jbod_Setup_CSMI_Info(CSMI_HANDLE deviceHandle, tDevice *device, uint8_t controllerNumber, uint8_t
    //  hostController, uint8_t pathidBus, uint8_t targetID, uint8_t lun)
    //
    //! \brief   Description:  Meant for use primarily in Windows where low-level may filter or block certain commands.
    //! CSMI can be used in place to get around those limitations.
    //!                        While this may work for SAS, this will likely only be used for SATA devices. SAS may not
    //!                        work if the LUN is non-zero, but this could depend on the HBA driver's CSMI support.
    //
    //  Entry:
    //!   \param[in] deviceHandle - OS handle value that was opened to be used for issuing commands.
    //!   \param[in] device - device structure. This is intended to be used only Within Windows where this provides the
    //!   information necessary to perform this test. \param[in] controllerNumber - not necessary for Windows, but put
    //!   into this call in case CSMI support like this is needed in another OS. \param[in] hostController - host or
    //!   contoller number from system's scsi Address \param[in] pathidBus - pathID or Bus number from system's scsi
    //!   Address \param[in] targetID - target ID from system's scsi Address \param[in] lun - logical unit number from
    //!   system's scsi Address.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    CSMI_HANDLE_PARAM(1)
    M_NONNULL_PARAM_LIST(2)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues jbod_Setup_CSMI_Info(CSMI_HANDLE deviceHandle,
                                                             tDevice*    device,
                                                             uint8_t     controllerNumber,
                                                             uint8_t     hostController,
                                                             uint8_t     pathidBus,
                                                             uint8_t     targetID,
                                                             uint8_t     lun);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_RAID_Device_Count(uint32_t * numberOfDevices, uint64_t flags, char **checkHandleList, uint32_t
    //  checkHandleListLength)
    //
    //! \brief   Description:  Get the number of CSMI RAID devices. This is intended to only discover drives configured
    //! in a RAID. Those not configured in RAID should use the OS passthrough with CSMI as a backup with
    //! jbod_Setup_CSMI_Info call
    //!                        This call requires a list of handles to check for RAID support. This list should be
    //!                        created based on what is a suspected, or known RAID controller to help reduce duplicate
    //!                        devices.
    //
    //  Entry:
    //!   \param[out] numberOfDevices - number of devices discovered based on provided list of handles to check.
    //!   \param[in] device - device structure. This is intended to be used only Within Windows where this provides the
    //!   information necessary to perform this test. \param[in] flags - \param[in/out] beginningOfList - pointer to the
    //!   beginning of the RAID handle list. double pointer so that it can be updated as necessary. I.E. remove elements
    //!   as devices are found so that other RAID libs don't rescan the same handle
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_PARAM_RW(3)
    OPENSEA_TRANSPORT_API eReturnValues get_CSMI_RAID_Device_Count(uint32_t*            numberOfDevices,
                                                                   uint64_t             flags,
                                                                   ptrRaidHandleToScan* beginningOfList);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_RAID_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t
    //  flags, char **checkHandleList, uint32_t checkHandleListLength)
    //
    //! \brief   Description:  Get the list of CSMI RAID devices. This is intended to only discover drives configured in
    //! a RAID. Those not configured in RAID should use the OS passthrough with CSMI as a backup with
    //! jbod_Setup_CSMI_Info call
    //!                        This call requires a list of handles to check for RAID support. This list should be
    //!                        created based on what is a suspected, or known RAID controller to help reduce duplicate
    //!                        devices.
    //
    //  Entry:
    //!   \param[out] ptrToDeviceList - pointer to a device list to start using to fill in discovered CSMI RAID devices.
    //!   \param[in] sizeInBytes - size of device list in bytes. Should be multiple of tDevice. Used to determine how
    //!   many CSMI devices to find. \param[in] ver - version block info to use when opening devices. Used to check
    //!   version compatibility. \param[in] flags - \param[in/out] beginningOfList - pointer to the beginning of the
    //!   RAID handle list. double pointer so that it can be updated as necessary. I.E. remove elements as devices are
    //!   found so that other RAID libs don't rescan the same handle
    //!
    //  Exit:
    //!   \return SUCCESS = pass, OS_COMMAND_NOT_AVAILABLE = not support in this OS or driver of the device,
    //!   OS_COMMAND_BLOCKED = Command not allowed, all others = other failures.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_PARAM_RW(5)
    OPENSEA_TRANSPORT_API eReturnValues get_CSMI_RAID_Device_List(tDevice* const       ptrToDeviceList,
                                                                  uint32_t             sizeInBytes,
                                                                  versionBlock         ver,
                                                                  uint64_t             flags,
                                                                  ptrRaidHandleToScan* beginningOfList);

    //-----------------------------------------------------------------------------
    //
    //  bool is_CSMI_Handle(const char * filename)
    //
    //! \brief   Description:  Checks if the provided string is a valid format for opening a CSMI device (very basic
    //! check right now)
    //
    //  Entry:
    //!   \param[in] sizeInBytes - size of device list in bytes. Should be multiple of tDevice. Used to determine how
    //!   many CSMI devices to find.
    //!
    //  Exit:
    //!   \return true = valid CSMI device handle format. false = not a valid CSMI device handle format.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_NULL_TERM_STRING(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_CSMI_Handle(const char* filename);

    //-----------------------------------------------------------------------------
    //
    //  close_CSMI_RAID_Device(tDevice *device)
    //
    //! \brief   Description:  Closes a handle to CSMI RAID device. Should only be used for those opened with
    //! get_CSMI_RAID_Device(). This also free's the memory allocated for CSMI devices.
    //
    //  Entry:
    //!   \param[in] device - pointer to tDevice structure for a CSMI RAID Device.
    //!
    //  Exit:
    //!   \return SUCCESS = sucessfully closed the handle, else something else went wrong or not a CSMI device to close
    //!   a handle for.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues close_CSMI_RAID_Device(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_RAID_Device(const char *filename, tDevice *device)
    //
    //! \brief   Description:  Opens the provided file for a CSMI device. This should be in one of the following
    //! formats:
    //!                            1. SAS/SATA - csmi:<controllerNumber/SCSI #>:<portID>:<phyID>:<lun>
    //!                            2. Intel RST NVMe - csmi:<controllerNumber/SCSI #>:N:<pathID>:<targetID>:<lun>
    //
    //  Entry:
    //!   \param[in] filename - formatted handle to specify opening a device for CSMI commands, typically in a RAID
    //!   environment. Can be a JBOD device, but that is not recommended. \param[in] device - pointer to tDevice
    //!   structure for a CSMI RAID Device.
    //!
    //  Exit:
    //!   \return SUCCESS = sucessfully opened CSMI device, else something went wrong while trying to open the device.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_NULL_TERM_STRING(1)
    M_PARAM_RO(1)
    M_PARAM_RW(2) OPENSEA_TRANSPORT_API eReturnValues get_CSMI_RAID_Device(const char* filename, tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  print_CSMI_Device_Info(tDevice *device)
    //
    //! \brief   Description:  Prints out some CSMI Device info which may be helpful for debugging.
    //
    //  Entry:
    //!   \param[in] device - pointer to tDevice structure for a CSMI RAID Device.
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API void print_CSMI_Device_Info(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  get_CSMI_Security_Access(char *driverName)
    //
    //! \brief   Description:  Using the driver name, Windows registry is checked for the level of access available. if
    //! not able to determine, FULL will be returned.
    //!                        In other OS's, this just checks for root permissions as that is all that is noted in CSMI
    //!                        documentation.
    //
    //  Entry:
    //!   \param[in] driverName - Name of the port driver. This will match reporting by the CSMI get driver information
    //!   call
    //!
    //  Exit:
    //!   \return eCSMISecurityAccess value that describes access level
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_NULL_TERM_STRING(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eCSMISecurityAccess get_CSMI_Security_Access(const char* driverName);

#    if defined(__cplusplus)
}
#    endif
#endif // ENABLE_CSMI
