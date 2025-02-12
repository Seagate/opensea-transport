// SPDX-License-Identifier: MPL-2.0

//! \file sat_helper_func.h
//! \brief Defines the function headers to help with SAT implementation
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_public.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  get_Return_TFRs_From_Passthrough_Results_Log(tDevice *device, ataReturnTFRs *ataRTFRs, uint16_t parameterCode)
    //
    //! \brief   Description:  This will pull the passthrough results log and parse the rtfrs out of it.
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] ataRTFRs = pointer to the struct to hold the rtfrs
    //!   \param[in] parameterCode = value specifying which parameter to read for the results.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_WO(2)
    eReturnValues get_Return_TFRs_From_Passthrough_Results_Log(tDevice*       device,
                                                               ataReturnTFRs* ataRTFRs,
                                                               uint16_t       parameterCode);

    //-----------------------------------------------------------------------------
    //
    //  get_RTFRs_From_Descriptor_Format_Sense_Data(uint8_t *ptrSenseData, uint32_t senseDataSize, ataReturnTFRs *rtfr)
    //
    //! \brief   Description:  This will retrieve the rtfrs from Descriptor Format Sense Data
    //
    //  Entry:
    //!   \param[in] ptrSenseData = pointer to the sense data to parse for RTFRs
    //!   \param[in] senseDataSize = number of bytes long the sense data is
    //!   \param[in] rtfr = pointer to the struct to hold the rtfrs
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    eReturnValues get_RTFRs_From_Descriptor_Format_Sense_Data(const uint8_t* ptrSenseData,
                                                              uint32_t       senseDataSize,
                                                              ataReturnTFRs* rtfr);

    //-----------------------------------------------------------------------------
    //
    //  get_RTFRs_From_Fixed_Format_Sense_Data(tDevice *device, uint8_t *ptrSenseData, uint32_t senseDataSize,
    //  ataReturnTFRs *rtfr)
    //
    //! \brief   Description:  This will retrieve the rtfrs from Fixed Format Sense Data
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to. (This may be used
    //!   in the event of trying to read the passthrough results log or re-requesting sense data) \param[in]
    //!   ptrSenseData = pointer to the sense data to parse for RTFRs \param[in] senseDataSize = number of bytes long
    //!   the sense data is \param[in] ataCmd = pointer to the full command info. This will help with proper
    //!   interpretation of sense data when asc and ascq are not necessarily "ata passthrough information available"
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(2, 3)
    M_PARAM_RW(4)
    eReturnValues get_RTFRs_From_Fixed_Format_Sense_Data(tDevice*               device,
                                                         const uint8_t*         ptrSenseData,
                                                         uint32_t               senseDataSize,
                                                         ataPassthroughCommand* ataCmd);

    //-----------------------------------------------------------------------------
    //
    //  get_Return_TFRs_From_Sense_Data(tDevice *device, ataPassthroughCommand *ataCommandOptions, int senseRet)
    //
    //! \brief   Description:  This will parse the returned sense data and in some cases issue a follow up command to
    //! get the rtfrs from a device
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] ataCommandOptions = ATA command options
    //!   \param[in] ioRet = the return status from sendIO (may be used if there is an OS_PASSTHROUGH_FAILURE)
    //!   \param[in] senseRet = return value from sending the ATA Pass-through command to the device and the meaning of
    //!   the sense data in SCSI terms
    //!
    //  Exit:
    //!   \return true = got RTFRs from the sense data, false = rtfrs not available
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    bool get_Return_TFRs_From_Sense_Data(tDevice*               device,
                                         ataPassthroughCommand* ataCommandOptions,
                                         eReturnValues          ioRet,
                                         eReturnValues          senseRet);

    //-----------------------------------------------------------------------------
    //
    //  set_Protocol_Field(uint8_t *satCDB, eAtaProtocol commadProtocol, eDataTransferDirection dataDirection)
    //
    //! \brief   Description:  Sets the protocol field into a satCDB. (used when building a SAT CDB)
    //
    //  Entry:
    //!   \param[out] satCDB = pointer to the cdb to set the protocol field in
    //!   \param[in] commadProtocol = ATA Protocol of the command
    //!   \param[in] dataDirection = Direction of the command (in/out/none)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    eReturnValues set_Protocol_Field(uint8_t*               satCDB,
                                     eAtaProtocol           commadProtocol,
                                     eDataTransferDirection dataDirection,
                                     uint8_t                protocolOffset);

    //-----------------------------------------------------------------------------
    //
    //  set_Transfer_Bits(uint8_t *satCDB, eATAPassthroughLength tLength, eATAPassthroughTransferBlocks ttype, bool
    //  byteBlockBit, eDataTransferDirection dataDirection)
    //
    //! \brief   Description:  Sets the transfer bits (T_Type, T_Dir, T_Length, Byte_Block) into a satCDB. (used when
    //! building a SAT CDB)
    //
    //  Entry:
    //!   \param[in] satCDB = pointer to the device structure for the device to issue the command to.
    //!   \param[in] tLength = set tlength type for the command
    //!   \param[in] ttype = set the ttype for the command
    //!   \param[in] dataDirection = Direction of the command (in/out/none)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    eReturnValues set_Transfer_Bits(uint8_t*                      satCDB,
                                    eATAPassthroughLength         tLength,
                                    eATAPassthroughTransferBlocks ttype,
                                    eDataTransferDirection        dataDirection,
                                    uint8_t                       transferBitsOffset);

    //-----------------------------------------------------------------------------
    //
    //  set_Multiple_Count(uint8_t *satCDB, uint8_t multipleCount)
    //
    //! \brief   Description:  Sets the multiple count field in a SAT CDB. This should only be done for multiple
    //! commands (read/write multiple)
    //
    //  Entry:
    //!   \param[in] satCDB = pointer to the device structure for the device to issue the command to.
    //!   \param[in] multipleCount = multiple count which should match what the drive is configured for (from identify
    //!   data)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) eReturnValues set_Multiple_Count(uint8_t* satCDB, uint8_t multipleCount, uint8_t protocolOffset);

    //-----------------------------------------------------------------------------
    //
    //  set_Offline_Bits(uint8_t *satCDB, uint32_t timeout)
    //
    //! \brief   Description:  Set the offline bits. This is used for offline commands for a timer.
    //
    //  Entry:
    //!   \param[in] satCDB = pointer to the device structure for the device to issue the command to.
    //!   \param[in] timeout = set for a timeout value as per SAT spec.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) eReturnValues set_Offline_Bits(uint8_t* satCDB, uint32_t timeout, uint8_t transferBitsOffset);

    //-----------------------------------------------------------------------------
    //
    //  set_Check_Condition_Bit(uint8_t *satCDB)
    //
    //! \brief   Description:  Set the check condition bit in a SAT CDB
    //
    //  Entry:
    //!   \param[in] satCDB = pointer to the device structure for the device to issue the command to.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) eReturnValues set_Check_Condition_Bit(uint8_t* satCDB, uint8_t transferBitsOffset);

    //-----------------------------------------------------------------------------
    //
    //  set_Registers(uint8_t *satCDB, ataPassthroughCommand *ataCommandOptions)
    //
    //! \brief   Description:  Set the task file registers into the SAT CDB
    //
    //  Entry:
    //!   \param[in] satCDB = pointer to the device structure for the device to issue the command to.
    //!   \param[in] ataCommandOptions = pointer to the struct with the ata task file registers in it
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1) M_PARAM_RO(2) eReturnValues set_Registers(uint8_t* satCDB, ataPassthroughCommand* ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  request_Return_TFRs_From_Device(tDevice *device, ataReturnTFRs *rtfr)
    //
    //! \brief   Description:  Send the SAT CDB to request the RTFRs. This command is not "pure" to the SAT spec as
    //! T_DIR is set to help USB bridges work with this command
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] rtfr = pointer to the struct where the rtfrs will be filled in
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1) M_PARAM_WO(2) eReturnValues request_Return_TFRs_From_Device(tDevice* device, ataReturnTFRs* rtfr);

    //-----------------------------------------------------------------------------
    //
    //  build_SAT_CDB(tDevice *device, uint8_t **satCDB, eCDBLen *cdbLen, ataPassthroughCommand *ataCommandOptions)
    //
    //! \brief   Description:  Function to construct a SAT Pass-through CDB based on the ATA Command Options
    //
    //  Entry:
    //!   \param[in] device = pointer to device struct for device command will be sent to.
    //!   \param[out] satCDB = 16byte array to hold the built command.
    //!   \param[out] cdbLen = length of CDB built
    //!   \param[in] ataCommandOptions = pointer to ATA command options that defines the command to build.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 3, 4)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_PARAM_RW(3)
    M_PARAM_RO(4)
    eReturnValues build_SAT_CDB(tDevice*               device,
                                uint8_t**              satCDB,
                                eCDBLen*               cdbLen,
                                ataPassthroughCommand* ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  send_SAT_Passthrough_Command(tDevice *device, ataPassthroughCommand  *ataCommandOptions)
    //
    //! \brief   Description:  Function to send a SAT Pass-through command. This will automatically call the function to
    //! build the command, then send it to the drive.
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2) eReturnValues send_SAT_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  translate_SCSI_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
    //
    //! \brief   Description:  This function attempts to perform SCSI to ATA translation according to the SAT4 spec. It
    //! is not 100% complete at this time.
    //!          SCSI Read, Write, Verify, Test Unit Ready, Inquiry, and a couple others are supported at this time.
    //!          This function is meant to be called by a lower layer that doesn't natively support SAT (Win ATA
    //!          passthrough or FreeBSD CAM for ATA)
    //
    //  Entry:
    //!   \param[in] device = pointer to the device structure for the device to issue the command to.
    //!   \param[in] scsiIoCtx = scsiIoCtx containing a SCSI command to issue to a drive.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1) M_PARAM_RW(2) eReturnValues translate_SCSI_Command(tDevice* device, ScsiIoCtx* scsiIoCtx);

#if defined(__cplusplus)
}
#endif
