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
// \file nvme_helper_func.h
// \brief Defines the function headers to help with NVMe implementation

#pragma once

#if !defined(DISABLE_NVME_PASSTHROUGH)
#include "nvme_helper.h"
#if defined (__cplusplus)
extern "C"
{
#endif

    void print_NVMe_Cmd_Verbose(const nvmeCmdCtx * cmdCtx);
    void print_NVMe_Cmd_Result_Verbose(const nvmeCmdCtx * cmdCtx);
    void get_NVMe_Status_Fields_From_DWord(uint32_t nvmeStatusDWord, bool *doNotRetry, bool *more, uint8_t *statusCodeType, uint8_t *statusCode);
    int check_NVMe_Status(uint32_t nvmeStatusDWord);//converts NVMe status to a return status used by open-sea libs

    //These reset functions will be defined in the os_Helper file since this is OS specific. Not all OS's will support this function either.
    int nvme_Reset(tDevice *device);
    int nvme_Subsystem_Reset(tDevice *device);

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
OPENSEA_TRANSPORT_API int nvme_Cmd(tDevice *device, nvmeCmdCtx * cmdCtx);

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
OPENSEA_TRANSPORT_API int nvme_Get_Features(tDevice *device, nvmeFeaturesCmdOpt * featCmdOpts);

//-----------------------------------------------------------------------------
//
//  nvme_Get_Log_Page
//
//! \brief   Description:  Function to send Get Log Page NVMe command to a device
//
//  Entry:
//!   \param[in] device = pointer to tDevice structure
//!   \param[in,out] getLogPageCmdOpts = commands options for get log page. 

//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
OPENSEA_TRANSPORT_API int nvme_Get_Log_Page(tDevice *device, nvmeGetLogPageCmdOpts * getLogPageCmdOpts);

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
OPENSEA_TRANSPORT_API int nvme_Get_SMART_Log_Page(tDevice *device, uint32_t nsid, uint8_t * pData, uint32_t dataLen);

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
OPENSEA_TRANSPORT_API int nvme_Get_ERROR_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen);



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
OPENSEA_TRANSPORT_API int nvme_Get_FWSLOTS_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen);

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
OPENSEA_TRANSPORT_API int nvme_Get_CmdSptEfft_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen);

OPENSEA_TRANSPORT_API char *nvme_cmd_to_string(int admin, uint8_t opcode);

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
OPENSEA_TRANSPORT_API int nvme_Get_DevSelfTest_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen);

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
OPENSEA_TRANSPORT_API int nvme_Identify(tDevice *device, uint8_t *ptrData, uint32_t nvmeNamespace, uint32_t cns);

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
//! 
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
OPENSEA_TRANSPORT_API int nvme_Firmware_Image_Dl(tDevice *device,\
                            uint32_t bufferOffset,\
                            uint32_t numberOfBytes,\
                            uint8_t *ptrData);

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
//! 
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
OPENSEA_TRANSPORT_API int nvme_Firmware_Commit(tDevice *device, nvmeFWCommitAction commitAction, uint8_t firmwareSlot);



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
OPENSEA_TRANSPORT_API int nvme_Set_Features(tDevice *device, nvmeFeaturesCmdOpt * featCmdOpts);


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
OPENSEA_TRANSPORT_API int nvme_Format(tDevice *device, nvmeFormatCmdOpts * formatCmdOpts);

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
OPENSEA_TRANSPORT_API int nvme_Read_Ctrl_Reg(tDevice *device, nvmeBarCtrlRegisters * ctrlRegs);


#define FORMAT_NVME_NO_SECURE_ERASE (0)
#define FORMAT_NVME_ERASE_USER_DATA (1)
#define FORMAT_NVME_CRYPTO_ERASE    (2)
#define FORMAT_NVME_PI_FIRST_BYTES  (4)
#define FORMAT_NVME_PI_TYPE_I       (8)
#define FORMAT_NVME_PI_TYPE_II      (16)
#define FORMAT_NVME_PI_TYPE_III     (32)
#define FORMAT_NVME_XFER_METADATA   (64)

OPENSEA_TRANSPORT_API int nvme_Abort_Command(tDevice *device, uint16_t commandIdentifier, uint16_t submissionQueueIdentifier);

OPENSEA_TRANSPORT_API int nvme_Device_Self_Test(tDevice *device, uint32_t nsid, uint8_t selfTestCode);

OPENSEA_TRANSPORT_API int nvme_Security_Receive(tDevice *device, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t nvmeSecuritySpecificField, uint8_t *ptrData, uint32_t dataLength);

OPENSEA_TRANSPORT_API int nvme_Security_Send(tDevice *device, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t nvmeSecuritySpecificField, uint8_t *ptrData, uint32_t dataLength);

OPENSEA_TRANSPORT_API int nvme_Write_Uncorrectable(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks);

#define SANITIZE_NVM_EXIT_FAILURE_MODE 1
#define SANITIZE_NVM_BLOCK_ERASE 2
#define SANITIZE_NVM_OVERWRITE 3
#define SANITIZE_NVM_CRYPTO 4

OPENSEA_TRANSPORT_API int nvme_Sanitize(tDevice *device, bool noDeallocateAfterSanitize, bool invertBetweenOverwritePasses, uint8_t overWritePassCount, bool allowUnrestrictedSanitizeExit, uint8_t sanitizeAction, uint32_t overwritePattern);

OPENSEA_TRANSPORT_API int nvme_Dataset_Management(tDevice *device, uint8_t numberOfRanges, bool deallocate, bool integralDatasetForWrite, bool integralDatasetForRead, uint8_t *ptrData, uint32_t dataLength);

OPENSEA_TRANSPORT_API int nvme_Flush(tDevice *device);

OPENSEA_TRANSPORT_API int nvme_Write(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks, bool limitedRetry, bool fua, uint8_t protectionInformationField, uint8_t directiveType, uint8_t *ptrData, uint32_t dataLength);

OPENSEA_TRANSPORT_API int nvme_Read(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks, bool limitedRetry, bool fua, uint8_t protectionInformationField, uint8_t *ptrData, uint32_t dataLength);

OPENSEA_TRANSPORT_API int nvme_Compare(tDevice *device, uint64_t startingLBA, uint16_t numberOfLogicalBlocks, bool limitedRetry, bool fua, uint8_t protectionInformationField, uint8_t *ptrData, uint32_t dataLength);

OPENSEA_TRANSPORT_API int nvme_Reservation_Report(tDevice *device, bool extendedDataStructure, uint8_t *ptrData, uint32_t dataSize);

OPENSEA_TRANSPORT_API int nvme_Reservation_Register(tDevice *device, uint8_t changePersistThroughPowerLossState, bool ignoreExistingKey, uint8_t reservationRegisterAction, uint8_t *ptrData, uint32_t dataSize);

OPENSEA_TRANSPORT_API int nvme_Reservation_Acquire(tDevice *device, uint8_t reservationType, bool ignoreExistingKey, uint8_t reservtionAcquireAction, uint8_t *ptrData, uint32_t dataSize);

OPENSEA_TRANSPORT_API int nvme_Reservation_Release(tDevice *device, uint8_t reservationType, bool ignoreExistingKey, uint8_t reservtionReleaseAction, uint8_t *ptrData, uint32_t dataSize);

OPENSEA_TRANSPORT_API int pci_Correctble_Err(tDevice *device,uint8_t  opcode, uint32_t  nsid, uint32_t  cdw10, uint32_t cdw11, uint32_t data_len, void *data);

// \fn fill_In_NVMe_Device_Info(tDevice * device)
// \brief Sends a set Identify etc commands & fills in the device information
// \param device device struture
// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int fill_In_NVMe_Device_Info(tDevice *device);

//Seagate unique?
OPENSEA_TRANSPORT_API int nvme_Read_Ext_Smt_Log(tDevice *device, EXTENDED_SMART_INFO_T *ExtdSMARTInfo);

#if defined (__cplusplus)
}
#endif

#endif
