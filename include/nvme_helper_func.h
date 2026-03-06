// SPDX-License-Identifier: MPL-2.0

//! \file nvme_helper_func.h
//! \brief Defines the functions to help with NVMe implementation and issuing NVMe commands
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "nvme_helper.h"
#if defined(__cplusplus)
extern "C"
{
#endif

    M_PARAM_RO(1) void print_NVMe_Cmd_Verbose(const nvmeCmdCtx* M_NONNULL cmdCtx);

    M_PARAM_RO(1) void print_NVMe_Cmd_Result_Verbose(const nvmeCmdCtx* M_NONNULL cmdCtx);

    M_PARAM_WO(2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    void get_NVMe_Status_Fields_From_DWord(uint32_t           nvmeStatusDWord,
                                           bool* M_NONNULL    doNotRetry,
                                           bool* M_NONNULL    more,
                                           uint8_t* M_NONNULL statusCodeType,
                                           uint8_t* M_NONNULL statusCode);

    eReturnValues check_NVMe_Status(
        uint32_t nvmeStatusDWord); // converts NVMe status to a return status used by open-sea libs

    // These reset functions will be defined in the os_Helper file since this is OS specific. Not all OS's will support
    // this function either.
    M_PARAM_RO(1) eReturnValues nvme_Reset(const tDevice* M_NONNULL device);

    M_PARAM_RO(1) eReturnValues nvme_Subsystem_Reset(const tDevice* M_NONNULL device);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Cmd()
    //
    //! \brief   Description:  Function to send any NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] cmdCtx = pointer to the NVMe command context
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Cmd(const tDevice* M_NONNULL device, nvmeCmdCtx* M_NONNULL cmdCtx);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_Features
    //
    //! \brief   Description:  Function to send Get Features NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in,out] featCmdOpts = commands options for get feature.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_Features(const tDevice* M_NONNULL      device,
                                                          nvmeFeaturesCmdOpt* M_NONNULL featCmdOpts);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_Log_Page
    //
    //! \brief   Description:  Function to send Get Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in,out] getLogPageCmdOpts = commands options for get log page
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_Log_Page(const tDevice* M_NONNULL         device,
                                                          nvmeGetLogPageCmdOpts* M_NONNULL getLogPageCmdOpts);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_SMART_Log_Page
    //
    //! \brief   Description:  Function to send Get SMART/Health Information Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] nsid = Namespace ID for the namespace of 0xFFFFFFFF for entire controller.
    //!   \param[out] pData = Data buffer (suppose to be 512 bytes)
    //!   \param[in] dataLen = Data buffer Length
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_SMART_Log_Page(const tDevice* M_NONNULL device,
                                                                uint32_t                 nsid,
                                                                uint8_t* M_NONNULL       pData,
                                                                uint32_t                 dataLen);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_ERROR_Log_Page
    //
    //! \brief   Description:  Function to send Get Error Information Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] pData = Data buffer (at least 64 bytes for a single entry)
    //!   \param[in] dataLen = Data buffer Length (multiple of 64 bytes for each entry)
    //!                        [NVMe Identify data shows how many entries are present]
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_ERROR_Log_Page(const tDevice* M_NONNULL device,
                                                                uint8_t* M_NONNULL       pData,
                                                                uint32_t                 dataLen);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_FWSLOTS_Log_Page
    //
    //! \brief   Description:  Function to send Get Firmware Slots Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] pData = Data buffer
    //!   \param[in] dataLen = Data buffer Length
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_FWSLOTS_Log_Page(const tDevice* M_NONNULL device,
                                                                  uint8_t* M_NONNULL       pData,
                                                                  uint32_t                 dataLen);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_CmdSptEfft_Log_Page
    //
    //! \brief   Description:  Function to send Get Commands Supported and Effects Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] pData = Data buffer
    //!   \param[in] dataLen = Data buffer Length
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_CmdSptEfft_Log_Page(const tDevice* M_NONNULL device,
                                                                     uint8_t* M_NONNULL       pData,
                                                                     uint32_t                 dataLen);

    M_RETURNS_NONNULL
    OPENSEA_TRANSPORT_API const char* M_NONNULL nvme_cmd_to_string(int admin, uint8_t opcode);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Get_DevSelfTest_Log_Page
    //
    //! \brief   Description:  Function to send Get Device Self-test Log Page NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] pData = Data buffer
    //!   \param[in] dataLen = Data buffer Length
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Get_DevSelfTest_Log_Page(const tDevice* M_NONNULL device,
                                                                      uint8_t* M_NONNULL       pData,
                                                                      uint32_t                 dataLen);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Identify()
    //
    //! \brief   Description:  Function to send a NVMe identify command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] ptrData = pointer to the data buffer to be filled in with identify data
    //!   \param[in] nvmeNamespace =
    //!   \param[in] cns = 0~NS Data Structre 1~Controller Data Structure 2~list of up to 1024 NSIDs,
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Identify(const tDevice* M_NONNULL device,
                                                      uint8_t* M_NONNULL       ptrData,
                                                      uint32_t                 nvmeNamespace,
                                                      uint32_t                 cns);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Firmware_Image_Dl()
    //
    //! \brief   Description:  Function to send a NVMe Firmware Image Download command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] bufferOffset = buffer offset for the image download
    //!   \param[in] numberOfBytes = Xfer length
    //!   \param[in] ptrData = pointer to the data buffer to be filled in with identify data
    //!   \param[in] firstSegment = Flag to help some low-level OSs know when the first segment of a firmware download
    //!   is happening...specifically Windows \param[in] lastSegment = Flag to help some low-level OSs know when the
    //!   last segment of a firmware download is happening...specifrically Windows \param[in] timeoutSeconds = set a
    //!   timeout in seconds for the command. This can be useful if some FWDL commands take longer (code activation for
    //!   example)
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 3)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Firmware_Image_Dl(const tDevice* M_NONNULL device,
                                                               uint32_t                 bufferOffset,
                                                               uint32_t                 numberOfBytes,
                                                               uint8_t* M_NONNULL       ptrData,
                                                               bool                     firstSegment,
                                                               bool                     lastSegment,
                                                               uint32_t                 timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Firmware_Commit()
    //
    //! \brief   Description:  Function to send a NVMe Firmware Commit (Activate) command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] commitAction = Image Download commit action to take
    //!   \param[in] firmwareSlot = Firmware Slot to take action on.
    //!   \param[in] timeoutSeconds = timeout for the activate command. Selectable since some commands take more time
    //!   than others
    //!
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Firmware_Commit(const tDevice* M_NONNULL device,
                                                             nvmeFWCommitAction       commitAction,
                                                             uint8_t                  firmwareSlot,
                                                             uint32_t                 timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Set_Features
    //
    //! \brief   Description:  Function to send Set Features NVMe command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in,out] featCmdOpts = commands options for get feature.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Set_Features(const tDevice* M_NONNULL      device,
                                                          nvmeFeaturesCmdOpt* M_NONNULL featCmdOpts);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Format
    //
    //! \brief   Description:  Function to send NVMe Format command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] formatCmdOpts = commands options to format .
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RO(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Format(const tDevice* M_NONNULL     device,
                                                    nvmeFormatCmdOpts* M_NONNULL formatCmdOpts);

    //-----------------------------------------------------------------------------
    //
    //  nvme_Read_Ctrl_Reg
    //
    //! \brief   Description:  Function to read NVMe controller registers from the device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] ctrlRegs = controller registers structure to fill.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Read_Ctrl_Reg(const tDevice* M_NONNULL        device,
                                                           nvmeBarCtrlRegisters* M_NONNULL ctrlRegs);

#define FORMAT_NVME_NO_SECURE_ERASE (0)
#define FORMAT_NVME_ERASE_USER_DATA (1)
#define FORMAT_NVME_CRYPTO_ERASE    (2)
#define FORMAT_NVME_PI_FIRST_BYTES  (4)
#define FORMAT_NVME_PI_TYPE_I       (8)
#define FORMAT_NVME_PI_TYPE_II      (16)
#define FORMAT_NVME_PI_TYPE_III     (32)
#define FORMAT_NVME_XFER_METADATA   (64)

    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Abort_Command(const tDevice* M_NONNULL device,
                                                           uint16_t                 commandIdentifier,
                                                           uint16_t                 submissionQueueIdentifier);

    // Do not use the asynchronous event request at this time. More work is required at low levels to properly support
    // this. This definition is here for completeness at this time.
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_RW(3)
    M_PARAM_RW(4)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Asynchronous_Event_Request(const tDevice* M_NONNULL device,
                                                                        uint8_t* M_NONNULL       logPageIdentifier,
                                                                        uint8_t* M_NONNULL asynchronousEventInformation,
                                                                        uint8_t* M_NONNULL asynchronousEventType);

    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Device_Self_Test(const tDevice* M_NONNULL device,
                                                              uint32_t                 nsid,
                                                              uint8_t                  selfTestCode);

    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 6)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Security_Receive(const tDevice* M_NONNULL device,
                                                              uint8_t                  securityProtocol,
                                                              uint16_t                 securityProtocolSpecific,
                                                              uint8_t                  nvmeSecuritySpecificField,
                                                              uint8_t* M_NULLABLE      ptrData,
                                                              uint32_t                 dataLength);

    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 6)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Security_Send(const tDevice* M_NONNULL device,
                                                           uint8_t                  securityProtocol,
                                                           uint16_t                 securityProtocolSpecific,
                                                           uint8_t                  nvmeSecuritySpecificField,
                                                           uint8_t* M_NULLABLE      ptrData,
                                                           uint32_t                 dataLength);

    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Verify(const tDevice* M_NONNULL device,
                                                    uint64_t                 startingLBA,
                                                    bool                     limitedRetry,
                                                    bool                     fua,
                                                    uint8_t                  protectionInformationField,
                                                    uint16_t                 numberOfLogicalBlocks);

    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Write_Uncorrectable(const tDevice* M_NONNULL device,
                                                                 uint64_t                 startingLBA,
                                                                 uint16_t                 numberOfLogicalBlocks);

#define SANITIZE_NVM_EXIT_FAILURE_MODE 1
#define SANITIZE_NVM_BLOCK_ERASE       2
#define SANITIZE_NVM_OVERWRITE         3
#define SANITIZE_NVM_CRYPTO            4

    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Sanitize(const tDevice* M_NONNULL device,
                                                      bool                     noDeallocateAfterSanitize,
                                                      bool                     invertBetweenOverwritePasses,
                                                      uint8_t                  overWritePassCount,
                                                      bool                     allowUnrestrictedSanitizeExit,
                                                      uint8_t                  sanitizeAction,
                                                      uint32_t                 overwritePattern);

    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(6, 7)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Dataset_Management(const tDevice* M_NONNULL device,
                                                                uint8_t                  numberOfRanges,
                                                                bool                     deallocate,
                                                                bool                     integralDatasetForWrite,
                                                                bool                     integralDatasetForRead,
                                                                uint8_t* M_NONNULL       ptrData,
                                                                uint32_t                 dataLength);

    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues nvme_Flush(const tDevice* M_NONNULL device);

    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Write(const tDevice* M_NONNULL device,
                                                   uint64_t                 startingLBA,
                                                   uint16_t                 numberOfLogicalBlocks,
                                                   bool                     limitedRetry,
                                                   bool                     fua,
                                                   uint8_t                  protectionInformationField,
                                                   uint8_t                  directiveType,
                                                   uint8_t* M_NONNULL       ptrData,
                                                   uint32_t                 dataLength);

    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Read(const tDevice* M_NONNULL device,
                                                  uint64_t                 startingLBA,
                                                  uint16_t                 numberOfLogicalBlocks,
                                                  bool                     limitedRetry,
                                                  bool                     fua,
                                                  uint8_t                  protectionInformationField,
                                                  uint8_t* M_NONNULL       ptrData,
                                                  uint32_t                 dataLength);

    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Compare(const tDevice* M_NONNULL device,
                                                     uint64_t                 startingLBA,
                                                     uint16_t                 numberOfLogicalBlocks,
                                                     bool                     limitedRetry,
                                                     bool                     fua,
                                                     uint8_t                  protectionInformationField,
                                                     uint8_t* M_NONNULL       ptrData,
                                                     uint32_t                 dataLength);

    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Reservation_Report(const tDevice* M_NONNULL device,
                                                                bool                     extendedDataStructure,
                                                                uint8_t* M_NONNULL       ptrData,
                                                                uint32_t                 dataSize);

    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Reservation_Register(const tDevice* M_NONNULL device,
                                                                  uint8_t            changePersistThroughPowerLossState,
                                                                  bool               ignoreExistingKey,
                                                                  uint8_t            reservationRegisterAction,
                                                                  uint8_t* M_NONNULL ptrData,
                                                                  uint32_t           dataSize);

    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Reservation_Acquire(const tDevice* M_NONNULL device,
                                                                 uint8_t                  reservationType,
                                                                 bool                     ignoreExistingKey,
                                                                 uint8_t                  reservtionAcquireAction,
                                                                 uint8_t* M_NONNULL       ptrData,
                                                                 uint32_t                 dataSize);

    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Reservation_Release(const tDevice* M_NONNULL device,
                                                                 uint8_t                  reservationType,
                                                                 bool                     ignoreExistingKey,
                                                                 uint8_t                  reservtionReleaseAction,
                                                                 uint8_t* M_NONNULL       ptrData,
                                                                 uint32_t                 dataSize);

    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Write_Zeroes(const tDevice* M_NONNULL device,
                                                          uint64_t                 startingLBA,
                                                          uint16_t                 numberOfLogicalBlocks,
                                                          bool                     limitedRetry,
                                                          bool                     forceUnitAccess,
                                                          bool                     deallocate);

    M_PARAM_RO(1)
    M_PARAM_RO(7)
    OPENSEA_TRANSPORT_API eReturnValues pci_Correctble_Err(const tDevice* M_NONNULL device,
                                                           uint8_t                  opcode,
                                                           uint32_t                 nsid,
                                                           uint32_t                 cdw10,
                                                           uint32_t                 cdw11,
                                                           uint32_t                 data_len,
                                                           void* M_NONNULL          data);

    // \fn fill_In_NVMe_Device_Info(const tDevice* M_NONNULL device)
    // \brief Sends a set Identify etc commands & fills in the device information
    // \param device device struture
    // \return SUCCESS - pass, !SUCCESS fail or something went wrong
    M_PARAM_RW(1) eReturnValues fill_In_NVMe_Device_Info(tDevice* M_NONNULL device);

    // Seagate unique?
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eReturnValues nvme_Read_Ext_Smt_Log(const tDevice* M_NONNULL         device,
                                                              EXTENDED_SMART_INFO_T* M_NONNULL ExtdSMARTInfo);

#if defined(__cplusplus)
}
#endif
