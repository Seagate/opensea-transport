// SPDX-License-Identifier: MPL-2.0

//! \file ata_helper_func.h
//! \brief Functions to issue ATA commands
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "ata_helper.h"
#include "common_public.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    //
    //  ata_Passthrough_Command()
    //
    //! \brief   Description:  Function to send a ATA Spec cmd as a pass-through
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ataCommandOptions = ATA command options
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_RW(2) eReturnValues
        ata_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Command()
    //
    //! \brief   Description:  Function to send a ATA Sanitize command. Use one of the below helper functions to send a
    //! specific command correctly, unless you know what you are doing.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] sanitizeFeature = enum value specifying the sanitize feature to run
    //!   \param[in] lba = the value to set in the LBA registers
    //!   \param[in] sectorCount = the value to set in the sector count registers
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Sanitize_Command(tDevice* device, eATASanitizeFeature sanitizeFeature, uint64_t lba, uint16_t sectorCount);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Status()
    //
    //! \brief   Description:  This function calls ata_Sanitize_Command with the correct inputs to perform a sanitize
    //! status command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] clearFailureMode = when set to true, set the clear Failure Mode bit
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Sanitize_Status(tDevice* device, bool clearFailureMode);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Crypto_Scramble()
    //
    //! \brief   Description:  This function calls ata_Sanitize_Command with the correct inputs to perform a sanitize
    //! crypto scramble command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] failureModeBit = when set to true, set the Failure Mode bit
    //!   \param[in] znr = zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Sanitize_Crypto_Scramble(tDevice* device, bool failureModeBit, bool znr);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Block_Erase()
    //
    //! \brief   Description:  This function calls ata_Sanitize_Command with the correct inputs to perform a sanitize
    //! block erase command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] failureModeBit = when set to true, set the Failure Mode bit
    //!   \param[in] znr = zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Sanitize_Block_Erase(tDevice* device, bool failureModeBit, bool znr);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Overwrite_Erase()
    //
    //! \brief   Description:  This function calls ata_Sanitize_Command with the correct inputs to perform a sanitize
    //! overwrite erase command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] failureModeBit = when set to true, set the Failure Mode bit
    //!   \param[in] invertBetweenPasses = set to true to set the bit specifying to invert the pattern between passes
    //!   \param[in] numberOfPasses = this will contain the number of passes to perform. A value of 0 means 16 passes.
    //!   \param[in] overwritePattern = this specifies the pattern to use during overwrite
    //!   \param[in] znr = zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize. \param[in] definitiveEndingPattern = if the drive supports this bit, it will make
    //!   sure that the specified pattern is the pattern upon completion between each pass and the invert between passes
    //!   bit.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Sanitize_Overwrite_Erase(tDevice* device,
                                     bool     failureModeBit,
                                     bool     invertBetweenPasses,
                                     uint8_t  numberOfPasses,
                                     uint32_t overwritePattern,
                                     bool     znr,
                                     bool     definitiveEndingPattern);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Freeze_Lock()
    //
    //! \brief   Description:  This function calls ata_Sanitize_Command with the correct inputs to perform a sanitize
    //! freeze lock command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues ata_Sanitize_Freeze_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sanitize_Anti_Freeze_Lock()
    //
    //! \brief   Description:  This function calls ata_Sanitize_Command with the correct inputs to perform a sanitize
    //! anti freeze lock command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Sanitize_Anti_Freeze_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Log_Ext()
    //
    //! \brief   Description:  Function to send a ATA read log ext command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] logAddress = the log address to be read
    //!   \param[in] pageNumber = the page of the log you wish to retrieve (typically, when reading a multisector log
    //!   all at once, this is set to 0) \param[out] ptrData = pointer to the data buffer that will be filled in upon
    //!   successful command completion \param[in] dataSize = value describing the size of the buffer that will be
    //!   filled in \param[in] useDMA = use the DMA command instead of the PIO command \param[in] featureRegister = set
    //!   the feature register for the command. This should be 0 unless the log you are reading requires this to be set
    //!   to something specific
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 4) M_PARAM_RO(1) M_PARAM_WO_SIZE(4, 5) eReturnValues
        ata_Read_Log_Ext(tDevice* device,
                         uint8_t  logAddress,
                         uint16_t pageNumber,
                         uint8_t* ptrData,
                         uint32_t dataSize,
                         bool     useDMA,
                         uint16_t featureRegister);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_Log_Ext()
    //
    //! \brief   Description:  Function to send a ATA write log ext command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] logAddress = the log address to be written
    //!   \param[in] pageNumber = the page of the log you wish to write to
    //!   \param[out] ptrData = pointer to the data buffer that will be sent to the device
    //!   \param[in] dataSize = value describing the size of the buffer that will be sent
    //!   \param[in] useDMA = use the DMA command instead of the PIO command
    //!   \param[in] forceRTFRs = this was added to force returning rtfrs on a command, specifically for SCT feature
    //!   control commands
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 4) M_PARAM_RO(1) M_PARAM_RO_SIZE(4, 5) eReturnValues
        ata_Write_Log_Ext(tDevice* device,
                          uint8_t  logAddress,
                          uint16_t pageNumber,
                          uint8_t* ptrData,
                          uint32_t dataSize,
                          bool     useDMA,
                          bool     forceRTFRs);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Read_Log()
    //
    //! \brief   Description:  Function to send a ATA SMART read log command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] logAddress = the log address to be read
    //!   \param[out] ptrData = pointer to the data buffer that will be filled in upon successful command completion
    //!   \param[in] dataSize = value describing the size of the buffer that will be filled in
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Read_Log(tDevice* device,
                                                           uint8_t  logAddress,
                                                           uint8_t* ptrData,
                                                           uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Write_Log()
    //
    //! \brief   Description:  Function to send a ATA SMART write log command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] logAddress = the log address to be written
    //!   \param[out] ptrData = pointer to the data buffer that will be sent to the device
    //!   \param[in] dataSize = value describing the size of the buffer that will be sent to the device
    //!   \param[in] forceRTFRs = this was added to force returning rtfrs on a command, specifically for SCT feature
    //!   control commands
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_SMART_Write_Log(tDevice* device, uint8_t logAddress, uint8_t* ptrData, uint32_t dataSize, bool forceRTFRs);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Command()
    //
    //! \brief   Description:  Function to send a ATA SMART command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] feature = the SMART feature to execute
    //!   \param[in] lbaLo = what the contents of the LBA lo register should be when sending the command
    //!   \param[out] ptrData = pointer to the data buffer that will be filled in upon successful command completion
    //!   \param[in] dataSize = value describing the size of the buffer that will be filled in
    //!   \param[in] timeout = timeout value in seconds for the command
    //!   \param[in] forceRTFRs = this was added to force returning rtfrs on a command, specifically for SCT feature
    //!   control commands \param[in] countReg = use this to set the secotr count register for NON DATA commands. This
    //!   is automatically set for data transfers
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 5)
    M_PARAM_RW_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_SMART_Command(tDevice* device,
                                                          uint8_t  feature,
                                                          uint8_t  lbaLo,
                                                          uint8_t* ptrData,
                                                          uint32_t dataSize,
                                                          uint32_t timeout,
                                                          bool     forceRTFRs,
                                                          uint8_t  countReg);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Read_Data()
    //
    //! \brief   Description:  Function to send a SMART read data command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] ptrData = pointer to the data buffer to be filled in with SMART data
    //!   \param[in] dataSize = size of the data buffer to be filled in (should always be 512)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_RW_SIZE(2, 3) eReturnValues
        ata_SMART_Read_Data(tDevice* device, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Offline()
    //
    //! \brief   Description:  Function to send a SMART offline command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] subcommand = the specific subcommand, or test to send
    //!   \param[in] timeout = timeout value in seconds for the command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_SMART_Offline(tDevice* device, uint8_t subcommand, uint32_t timeout);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Return_Status()
    //
    //! \brief   Description:  Function to send a SMART return status command to a device. the return tfrs will need be
    //! checked after this command is complete.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues ata_SMART_Return_Status(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Enable_Operations()
    //
    //! \brief   Description:  Function to send a SMART enable operations command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_SMART_Enable_Operations(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Disable_Operations()
    //
    //! \brief   Description:  Function to send a SMART disable operations command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_SMART_Disable_Operations(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Read_Thresholds()
    //
    //! \brief   Description:  Function to send a SMART read thresholds command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[out] ptrData = pointer to the data buffer to read the thresholds into
    //!   \param[in] dataSize = size of the data buffer to save the thresholds into. Must be at least 512B in size
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_RW_SIZE(2, 3) eReturnValues
        ata_SMART_Read_Thresholds(tDevice* device, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Save_Attributes()
    //
    //! \brief   Description:  Function to send a SMART save attributes command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_SMART_Save_Attributes(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Attribute_Autosave()
    //
    //! \brief   Description:  Function to send a SMART enable/disable attribute autosave command
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] enable = set to true to enable attribute autosave and false to disable.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_SMART_Attribute_Autosave(tDevice* device, bool enable);

    //-----------------------------------------------------------------------------
    //
    //  ata_SMART_Auto_Offline()
    //
    //! \brief   Description:  Function to send a SMART enable/disable auto-off-line. (Not officially adopted by ATA
    //! spec)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] enable = set to true to enable auto-off-line and false to disable.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_SMART_Auto_Offline(tDevice* device, bool enable);

    // This assumes standard ATA identify like reported from ata_Identify or page 1 of the ID data log. 512B long and as
    // reported by the standards.
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2, 3, 4) M_PARAM_RO(1) M_PARAM_WO(2) M_PARAM_WO(3)
        M_PARAM_WO(4) void fill_ATA_Strings_From_Identify_Data(uint8_t* ptrIdentifyData,
                                                               char     ataMN[ATA_IDENTIFY_MN_LENGTH + 1],
                                                               char     ataSN[ATA_IDENTIFY_SN_LENGTH + 1],
                                                               char     ataFW[ATA_IDENTIFY_FW_LENGTH + 1]);

    //-----------------------------------------------------------------------------
    //
    //  ata_Identify()
    //
    //! \brief   Description:  Function to send a ATA identify command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[out] ptrData = pointer to the data buffer to be filled in with identify data
    //!   \param[in] dataSize = the size of the data buffer to be filled in (should always be 512)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RW(1) M_PARAM_WO_SIZE(2, 3) eReturnValues
        ata_Identify(tDevice* device, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Security_Disable_Password()
    //
    //! \brief   Description:  Function to send a ATA Security Disable Password command to a device
    //
    //  Entry:
    //!   \param[in] device = pointer to tDevice structure
    //!   \param[in] ptrData = pointer to the data buffer to send to the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) eReturnValues
        ata_Security_Disable_Password(tDevice* device, uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  ata_Security_Erase_Prepare()
    //
    //! \brief   Description:  Function to send a ATA Security Erase Prepare Command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues
        ata_Security_Erase_Prepare(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Security_Erase_Unit()
    //
    //! \brief   Description:  Function to send a ATA Security Erase Unit command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ptrData = pointer to the data buffer to send to the device
    //!   \param[in] timeout = value to use for the timeout on the command. This is a value in seconds. It is
    //!   recommended that the time provided by ATA identify data is used.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_RO_SIZE(2, 3) eReturnValues
        ata_Security_Erase_Unit(tDevice* device, uint8_t* ptrData, uint32_t timeout);

    //-----------------------------------------------------------------------------
    //
    //  ata_Security_Set_Password()
    //
    //! \brief   Description:  Function to send a ATA Security Set Password command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ptrData = pointer to the data buffer to send to the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_RO(2) eReturnValues
        ata_Security_Set_Password(tDevice* device, uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  ata_Security_Unlock()
    //
    //! \brief   Description:  Function to send a ATA Security Unlock command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] ptrData = pointer to the data buffer to send to the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_RO(2) eReturnValues
        ata_Security_Unlock(tDevice* device, uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  ata_Security_Freeze_Lock()
    //
    //! \brief   Description:  Function to send a ATA Security Freeze Lock Command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues ata_Security_Freeze_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Download_Microcode()
    //
    //! \brief   Description:  Function to send a ATA Download Microcode command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] subCommand = the subcommand for the download function (feature register)
    //!   \param[in] blockCount
    //!   \param[in] bufferOffset
    //!   \param[in] useDMA = use download Microcode DMA command (device must support this command or this will return
    //!   an error) \param[in] pData = pointer to the data buffer to send to the device \param[in] dataLen = length of
    //!   data to transfer \param[in] firstSegment = Flag to help some low-level OSs know when the first segment of a
    //!   firmware download is happening...specifically Windows \param[in] lastSegment = Flag to help some low-level OSs
    //!   know when the last segment of a firmware download is happening...specifrically Windows \param[in]
    //!   timeoutSeconds = set a timeout in seconds for the command. This can be useful if some FWDL commands take
    //!   longer (code activation for example)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) M_NONNULL_IF_NONZERO_PARAM(6, 7)
        M_PARAM_RO_SIZE(6, 7) eReturnValues ata_Download_Microcode(tDevice*                   device,
                                                                   eDownloadMicrocodeFeatures subCommand,
                                                                   uint16_t                   blockCount,
                                                                   uint16_t                   bufferOffset,
                                                                   bool                       useDMA,
                                                                   uint8_t*                   pData,
                                                                   uint32_t                   dataLen,
                                                                   bool                       firstSegment,
                                                                   bool                       lastSegment,
                                                                   uint32_t                   timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  ata_Is_Extended_Power_Conditions_Feature_Supported()
    //
    //! \brief   Description:  Return true if the EPC bit is set.
    //
    //  Entry:
    //!   \param[in] pIdentify - ATA identify data
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) uint16_t ata_Is_Extended_Power_Conditions_Feature_Supported(uint16_t* pIdentify);

    //-----------------------------------------------------------------------------
    //
    //  ata_Is_One_Extended_Power_Conditions_Feature_Supported()
    //
    //! \brief   Description:  Return true if at least one of the EPC bit is set.
    //
    //  Entry:
    //!   \param[in] pIdentify - ATA identify data
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) uint16_t ata_Is_One_Extended_Power_Conditions_Feature_Supported(uint16_t* pIdentify);

    //-----------------------------------------------------------------------------
    //
    //  ata_Accessible_Max_Address_Feature()
    //
    //! \brief   Description:  send a accessible max address configuration command
    //
    //  Entry:
    //!   \param device device handle
    //!   \param feature the feature registers for the command
    //!   \param lba the lba registers for the command
    //!   \param rtfrs set to non-null to get rtfrs out of this function
    //!   \param[in] sectorCount = the value to set in the sector count registers
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(4)
    OPENSEA_TRANSPORT_API eReturnValues ata_Accessible_Max_Address_Feature(tDevice*       device,
                                                                           uint16_t       feature,
                                                                           uint64_t       lba,
                                                                           ataReturnTFRs* rtfrs,
                                                                           uint16_t       sectorCount);

    //-----------------------------------------------------------------------------
    //
    //  ata_Get_Native_Max_Address_Ext()
    //
    //! \brief   Description:  get the native max address using the accessible max address ext command
    //
    //  Entry:
    //!   \param device device handle
    //!   \param nativeMaxLBA pointer to the var you want to hold the native max LBA upon command completion
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_TRANSPORT_API eReturnValues ata_Get_Native_Max_Address_Ext(tDevice* device, uint64_t* nativeMaxLBA);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Accessible_Max_Address_Ext()
    //
    //! \brief   Description:  set the accessible max LBA using the accessible max address ext command
    //
    //  Entry:
    //!   \param device device handle
    //!   \param newMaxLBA the new maxLBA you wish to have set
    //!   \param changeId whether to set ENABLE CHANGE IDENTIFY STRINGS bit for changing model number
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Accessible_Max_Address_Ext(tDevice* device,
                                                                           uint64_t newMaxLBA,
                                                                           bool     changeId);

    //-----------------------------------------------------------------------------
    //
    //  ata_Freeze_Accessible_Max_Address_Ext()
    //
    //! \brief   Description:  freeze the accessible max address using the accessible max address ext command
    //
    //  Entry:
    //!   \param device device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Freeze_Accessible_Max_Address_Ext(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Native_Max_Address()
    //
    //! \brief   Description:  (Obsolete command) This command reads the native max LBA of the drive
    //
    //  Entry:
    //!   \param device device handle
    //!   \param nativeMaxLBA pointer to the value you wish to have hold the native max address upon command completion
    //!   \param ext set to true to do the extended command
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_Native_Max_Address(tDevice*  device,
                                                                    uint64_t* nativeMaxLBA,
                                                                    bool      ext); // obsolete on new drives

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max()
    //
    //! \brief   Description:  (Obsolete command) This command sets the native max LBA of the drive or freezes or locks
    //! etc
    //
    //  Entry:
    //!   \param device device handle
    //!   \param setMaxFeature which set max feature you want to do...(set, freeze, lock, etc)
    //!   \param newMaxLBA the new MaxLBA you wish to have set on the device
    //!   \param volatileValue if set to false, this is a volatile setting and it will not stick upon completion of a
    //!   power cycle or reset \param ptrData pointer to the data to transfer (on data transfer commands) \param
    //!   dataLength length of data to transfer (on data transfer commands)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 6)
    M_PARAM_RW_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max(tDevice*    device,
                                                    eHPAFeature setMaxFeature,
                                                    uint32_t    newMaxLBA,
                                                    bool        volatileValue,
                                                    uint8_t*    ptrData,
                                                    uint32_t    dataLength); // obsolete on new drives

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_Address()
    //
    //! \brief   Description:  (Obsolete command) This command sets the native max LBA of the drive (28bit)
    //
    //  Entry:
    //!   \param device device handle
    //!   \param newMaxLBA the new MaxLBA you wish to have set on the device
    //!   \param voltileValue if set to false, this is a volatile setting and it will not stick upon completion of a
    //!   power cycle or reset
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Address(tDevice* device, uint32_t newMaxLBA, bool volatileValue);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_Password()
    //
    //! \brief   Description:  (Obsolete command) This command sets the password for locking the native max LBA
    //
    //  Entry:
    //!   \param device device handle
    //!   \param ptrData pointer to the data to transfer
    //!   \param dataLength length of data to transfer
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Password(tDevice* device, uint8_t* ptrData, uint32_t dataLength);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_Lock()
    //
    //! \brief   Description:  (Obsolete command) This command locks the native max LBA of the drive
    //
    //  Entry:
    //!   \param device device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_Unlock()
    //
    //! \brief   Description:  (Obsolete command) This command unlocks the native max LBA using the provided password
    //
    //  Entry:
    //!   \param device device handle
    //!   \param ptrData pointer to the data to transfer
    //!   \param dataLength length of data to transfer
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Unlock(tDevice* device, uint8_t* ptrData, uint32_t dataLength);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_Freeze_Lock()
    //
    //! \brief   Description:  (Obsolete command) This command freeze locks the native max LBA of the drive
    //
    //  Entry:
    //!   \param device device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Freeze_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Max_Address_Ext()
    //
    //! \brief   Description:  (Obsolete command) This command sets the native max LBA of the drive (must be preceded by
    //! read native max ext)
    //
    //  Entry:
    //!   \param device device handle
    //!   \param newMaxLBA the new MaxLBA you wish to have set on the device
    //!   \param volatileValue if set to false, this is a volatile setting and it will not stick upon completion of a
    //!   power cycle or reset
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Max_Address_Ext(tDevice* device,
                                                                uint64_t newMaxLBA,
                                                                bool     volatileValue); // obsolete on new drives

    // ATA SCT definitions that were here were removed in favor of the versions starting with send_ in ata_helper since
    // those include retries as needed to work around adapter issues
    //  and reduce confusion on the code that was implemented for performing SCT operations more easily.

    //-----------------------------------------------------------------------------
    //
    //  ata_Check_Power_Mode()
    //
    //! \brief   Description:  This command sends a ATA Check Power Mode command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param powerMode - pointer to a variable that will contain the value of the power mode. Check the spec to see
    //!   how to determine what the power mode is.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Check_Power_Mode(tDevice* device, uint8_t* powerMode);

    //-----------------------------------------------------------------------------
    //
    //  ata_Configure_Stream()
    //
    //! \brief   Description:  This command sends a ATA Configure Stream command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param streamID - set this to the stream ID. only bits 2:0 are valid
    //!   \param addRemoveStreamBit - set to true to set this bit (meaning add stream). Set to false to leave this bit
    //!   at 0 (meaning remove stream) \param readWriteStreamBit - This bit is obsolete since ACS so set this to false
    //!   for new devices. Only set this for old devices. (see ATA/ATAPI 7 for details on this bit) \param defaultCCTL -
    //!   time in which the device will return command completion for read stream command or a write stream command with
    //!   COMMAND CCTL field set to 0. See spec for formula \param allocationUnit - number of logical blocks the device
    //!   should use for read look-ahead and write cache operations for the stream being configured
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Configure_Stream(tDevice* device,
                                                             uint8_t  streamID,
                                                             bool     addRemoveStreamBit,
                                                             bool     readWriteStreamBit,
                                                             uint8_t  defaultCCTL,
                                                             uint16_t allocationUnit);

    //-----------------------------------------------------------------------------
    //
    //  ata_Data_Set_Management()
    //
    //! \brief   Description:  This command sends a ATA Data Set Management (XL) command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param trimBit - set the TRIM bit. (Since this is currently the only available operation with this command,
    //!   this should be set to true) \param ptrData - pointer to the data buffer that will be sent to the device \param
    //!   dataSize - the size of the data buffer to send. \param xl - set to true to issue data set management XL
    //!   command instead of standard command. Support for this is shown in ID Data log - supported features subpage.
    //!   NOTE: This uses a different data range format, so only set this when the buffer is setup correctly!
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API M_NONNULL_PARAM_LIST(1, 3) M_PARAM_RO(1) M_PARAM_RO_SIZE(3, 4) eReturnValues
        ata_Data_Set_Management(tDevice* device, bool trimBit, uint8_t* ptrData, uint32_t dataSize, bool xl);

    //-----------------------------------------------------------------------------
    //
    //  ata_execute_Device_Diagnostic()
    //
    //! \brief   Description:  This command sends a ATA Execute Device Diagnostic command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param diagnosticCode - returned diagnostic code value
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    OPENSEA_TRANSPORT_API eReturnValues ata_Execute_Device_Diagnostic(tDevice* device, uint8_t* diagnosticCode);

    //-----------------------------------------------------------------------------
    //
    //  ata_Flush_Cache()
    //
    //! \brief   Description:  This command sends a ATA Flush Cache command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param extendedCommand - set to true to issue flush cache extended (48 bit) instead on flush cache (28 bit)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Flush_Cache(tDevice* device, bool extendedCommand);

    //-----------------------------------------------------------------------------
    //
    //  ata_Idle()
    //
    //! \brief   Description:  This command sends a ATA Idle command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param standbyTimerPeriod - set to the value you want to set the standby timer for. (See ATA spec for
    //!   available timer periods)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Idle(tDevice* device, uint8_t standbyTimerPeriod);

    //-----------------------------------------------------------------------------
    //
    //  ata_Idle_Immediate()
    //
    //! \brief   Description:  This command sends a ATA Idle immediate command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param unloadFeature - set to true if the drive supports the unload feature to move the heads to a safe
    //!   position
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Idle_Immediate(tDevice* device, bool unloadFeature);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Buffer()
    //
    //! \brief   Description:  This command sends a ATA Read Buffer command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command. This buffer must be at
    //!   least 512bytes in size! \param useDMA - set to true to issue the read buffer DMA command
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_TRANSPORT_API eReturnValues ata_Read_Buffer(tDevice* device, uint8_t* ptrData, bool useDMA);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_DMA()
    //
    //! \brief   Description:  This command sends a ATA Read DMA command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param sectorCount - number of sectors to read
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read \param extendedCmd - set to true to issue the read DMA EXT command (48bit) instead of the
    //!   normal command (28bit)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_DMA(tDevice* device,
                                                     uint64_t LBA,
                                                     uint8_t* ptrData,
                                                     uint16_t sectorCount,
                                                     uint32_t dataSize,
                                                     bool     extendedCmd);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Multiple()
    //
    //! \brief   Description:  This command sends a ATA Read Multiple command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param sectorCount - number of sectors to read
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read \param extendedCmd - set to true to issue the read Multiple EXT command (48bit) instead of the
    //!   normal command (28bit)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_Multiple(tDevice* device,
                                                          uint64_t LBA,
                                                          uint8_t* ptrData,
                                                          uint16_t sectorCount,
                                                          uint32_t dataSize,
                                                          bool     extendedCmd);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Sectors()
    //
    //! \brief   Description:  This command sends a ATA Read Sectors command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param sectorCount - number of sectors to read
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read \param extendedCmd - set to true to issue the read Sectors EXT command (48bit) instead of the
    //!   normal command (28bit)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_Sectors(tDevice* device,
                                                         uint64_t LBA,
                                                         uint8_t* ptrData,
                                                         uint16_t sectorCount,
                                                         uint32_t dataSize,
                                                         bool     extendedCmd);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Sectors_No_Retry(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint16_t sectorCount, uint32_t
    //  dataSize)
    //
    //! \brief   Description:  This command sends a ATA Read Sectors(No Retry) command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param sectorCount - number of sectors to read
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Read_Sectors_No_Retry(tDevice* device, uint64_t LBA, uint8_t* ptrData, uint16_t sectorCount, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Stream_Ext()
    //
    //! \brief   Description:  This command sends a ATA Read Stream Extended command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param useDMA - use the DMA read stream ext command instead of the PIO command
    //!   \param streamID - the stream ID. only bits 2:0 are valid. Anything else will be stripped off.
    //!   \param notSequential - set to true to set the Not Sequential bit
    //!   \param readContinuous - set to true to set the Read Continuous bit
    //!   \param commandCCTL - the command completion time limit
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 8)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_Stream_Ext(tDevice* device,
                                                            bool     useDMA,
                                                            uint8_t  streamID,
                                                            bool     notSequential,
                                                            bool     readContinuous,
                                                            uint8_t  commandCCTL,
                                                            uint64_t LBA,
                                                            uint8_t* ptrData,
                                                            uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read_Verify_Sectors()
    //
    //! \brief   Description:  This command sends a ATA Read Verify Sectors command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param extendedCmd - Send read verify sectors ext command instead of the 28bit command
    //!   \param numberOfSectors - The number of sectors you want to have verified. 0, sets a max as defined in the ATA
    //!   spec \param LBA - the starting LBA to read and verify
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_Verify_Sectors(tDevice* device,
                                                                bool     extendedCmd,
                                                                uint16_t numberOfSectors,
                                                                uint64_t LBA);

    //-----------------------------------------------------------------------------
    //
    //  ata_Request_Sense_Data()
    //
    //! \brief   Description:  This command sends a ATA Request Sense Data command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param senseKey - pointer to a variable that will hold the sense key on successful command completion. Must be
    //!   non-M_NULLPTR \param additionalSenseCode - pointer to a variable that will hold the additional sense code on
    //!   successful command completion. Must be non-M_NULLPTR \param additionalSenseCodeQualifier - pointer to a
    //!   variable that will hold the additional sense code qualifier on successful command completion. Must be
    //!   non-M_NULLPTR
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 3, 4)
    M_PARAM_RO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    OPENSEA_TRANSPORT_API eReturnValues ata_Request_Sense_Data(tDevice* device,
                                                               uint8_t* senseKey,
                                                               uint8_t* additionalSenseCode,
                                                               uint8_t* additionalSenseCodeQualifier);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Date_And_Time()
    //
    //! \brief   Description:  This command sends a ATA Set Data and Time Ext command to the tDevice
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param timeStamp - The timestamp you wish to set (See ATA Spec for details on how to specify a timestamp)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Set_Date_And_Time(tDevice* device, uint64_t timeStamp);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Multiple_Mode()
    //
    //! \brief   Description:  This command sends a ATA Set Multiple Mode command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param drqDataBlockCount - The number of logical sectors the DRQ data block count for the read/write multiple
    //!   commands. (Should be equal or less than the value in Identify word 47 bits 7:0). See ATA Spec for more details
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Set_Multiple_Mode(tDevice* device, uint8_t drqDataBlockCount);

    //-----------------------------------------------------------------------------
    //
    //  ata_Sleep()
    //
    //! \brief   Description:  This command sends a ATA Sleep command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Sleep(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Standby()
    //
    //! \brief   Description:  This command sends a ATA Standby command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param standbyTimerPeriod - the standby timer. See ATA spec for details
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Standby(tDevice* device, uint8_t standbyTimerPeriod);

    //-----------------------------------------------------------------------------
    //
    //  ata_Standby()
    //
    //! \brief   Description:  This command sends a ATA Standby Immediate command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Standby_Immediate(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Trusted_Non_Data()
    //
    //! \brief   Description:  This command sends a ATA Trusted Non Data command to the tDevice
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param securityProtocol - set to the security protocol to use
    //!   \param trustedSendReceiveBit - if this is true, we set the trusted send receive bit to 1, otherwise we leave
    //!   it at 0. See ATA Spec for details \param securityProtocolSpecific - See ATA Spec for details
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Trusted_Non_Data(tDevice* device,
                                                             uint8_t  securityProtocol,
                                                             bool     trustedSendReceiveBit,
                                                             uint16_t securityProtocolSpecific);

    //-----------------------------------------------------------------------------
    //
    //  ata_Trusted_Receive()
    //
    //! \brief   Description:  This command sends a ATA Trusted Receive command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param useDMA - set to true to use Trusted Receive DMA
    //!   \param securityProtocol - set to the security protocol to use
    //!   \param securityProtocolSpecific - See ATA Spec for details
    //!   \param ptrData - pointer to the data buffer to fill upon command completion. Must be Non-M_NULLPTR
    //!   \param dataSize - size of the data buffer. This will also be used to determine the transfer length.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Trusted_Receive(tDevice* device,
                                                            bool     useDMA,
                                                            uint8_t  securityProtocol,
                                                            uint16_t securityProtocolSpecific,
                                                            uint8_t* ptrData,
                                                            uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Trusted_Send()
    //
    //! \brief   Description:  This command sends a ATA Trusted Send command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param useDMA - set to true to use Trusted Send DMA
    //!   \param securityProtocol - set to the security protocol to use
    //!   \param securityProtocolSpecific - See ATA Spec for details
    //!   \param ptrData - pointer to the data buffer to send to the device. Must be Non-M_NULLPTR
    //!   \param dataSize - size of the data buffer. This will also be used to determine the transfer length.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Trusted_Send(tDevice* device,
                                                         bool     useDMA,
                                                         uint8_t  securityProtocol,
                                                         uint16_t securityProtocolSpecific,
                                                         uint8_t* ptrData,
                                                         uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_Buffer()
    //
    //! \brief   Description:  This command sends a ATA Write Buffer command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param ptrData - pointer to the data buffer to send to the device. Must be Non-M_NULLPTR
    //!   \param useDMA - set to true to use Trusted Send DMA
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2) OPENSEA_TRANSPORT_API eReturnValues ata_Write_Buffer(tDevice* device, uint8_t* ptrData, bool useDMA);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_DMA()
    //
    //! \brief   Description:  This command sends a ATA Write DMA command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to write
    //!   \param ptrData - pointer to the data buffer to send to the device. Must be Non-M_NULLPTR
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to write \param extendedCmd - set to true to issue the write DMA EXT command (48bit) instead of the
    //!   normal command (28bit) \param fua - send the write ext fua command. Only valid if extendedCmd is also set
    //!   (since it is a extended command). This command forces writing to disk regardless of write caching enabled or
    //!   disabled.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Write_DMA(tDevice* device, uint64_t LBA, uint8_t* ptrData, uint32_t dataSize, bool extendedCmd, bool fua);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_Multiple()
    //
    //! \brief   Description:  This command sends a ATA Write Multiple command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read \param extendedCmd - set to true to issue the write Multiple EXT command (48bit) instead of
    //!   the normal command (28bit) \param fua - send the write multiple ext fua command. Only valid if extendedCmd is
    //!   also set (since it is a extended command). This command forces writing to disk regardless of write caching
    //!   enabled or disabled.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Write_Multiple(tDevice* device, uint64_t LBA, uint8_t* ptrData, uint32_t dataSize, bool extendedCmd, bool fua);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_Sectors()
    //
    //! \brief   Description:  This command sends a ATA Write Sectors command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to write \param extendedCmd - set to true to issue the write Sectors EXT command (48bit) instead of
    //!   the normal command (28bit)
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Write_Sectors(tDevice* device, uint64_t LBA, uint8_t* ptrData, uint32_t dataSize, bool extendedCmd);

    //
    //  ata_Write_Sectors_No_Retry(tDevice *device, uint64_t LBA, uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  This command sends a ATA Write Sectors(No Retry) command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to write
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_Write_Sectors_No_Retry(tDevice* device,
                                                                   uint64_t LBA,
                                                                   uint8_t* ptrData,
                                                                   uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_Stream_Ext()
    //
    //! \brief   Description:  This command sends a ATA Write Stream Extended command to the device
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param useDMA - use the DMA read stream ext command instead of the PIO command
    //!   \param streamID - the stream ID. only bits 2:0 are valid. Anything else will be stripped off.
    //!   \param flush - set to true to set the flush bit
    //!   \param writeContinuous - set to true to set the Write Continuous bit
    //!   \param commandCCTL - the command completion time limit
    //!   \param LBA - the starting LBA to read
    //!   \param ptrData - pointer to the data buffer to fill in with the result of this command.
    //!   \param dataSize - the Size of your buffer. This will be used to determine the sector count for how many
    //!   sectors to read
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 8)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_Write_Stream_Ext(tDevice* device,
                                                             bool     useDMA,
                                                             uint8_t  streamID,
                                                             bool     flush,
                                                             bool     writeContinuous,
                                                             uint8_t  commandCCTL,
                                                             uint64_t LBA,
                                                             uint8_t* ptrData,
                                                             uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write_Uncorrectable()
    //
    //! \brief   Description:  WARNING!!!!!!!!!!!!!! This command is dangerous and can corrupt data! Only use this for
    //! debugging and testing purposes!
    //!                        This command sends a ATA Write Uncorrectable Extended command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param unrecoverableOptions - See ATA spec. There are 2 defined features, and the other 2 are left up to
    //!   vendors. All other values are reserved \param numberOfSectors - The number of sectors including the starting
    //!   LBA to write an uncorrectable error to \param LBA - the starting LBA to read
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Write_Uncorrectable(tDevice* device,
                                                                uint8_t  unrecoverableOptions,
                                                                uint16_t numberOfSectors,
                                                                uint64_t LBA);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Cache_Feature()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Feature Set command to the device. You must know what
    //! you are doing when calling this function. If not, call one of the below helper functions that define each of the
    //! operations in this feature set.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param feature - See ATA spec. The feature register of the command.
    //!   \param count - See ATA spec. The sector count register of the command.
    //!   \param LBA - the LBA registers to set.
    //!   \param ptrData - pointer to the data buffer to use. This can be M_NULLPTR on Non-data commands
    //!   \param dataSize - the size of the data buffer being used. Set to zero for Non-Data commands.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 6)
    M_PARAM_RW_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Feature(tDevice*         device,
                                                             eNVCacheFeatures feature,
                                                             uint16_t         count,
                                                             uint64_t         LBA,
                                                             uint8_t*         ptrData,
                                                             uint32_t         dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Cache_Add_LBAs_To_Cache()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Add LBAs to Cache command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param populateImmediately - Set the populateImmediately bit
    //!   \param ptrData - pointer to the data buffer to use.
    //!   \param dataSize - the size of the data buffer being used.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Add_LBAs_To_Cache(tDevice* device,
                                                                       bool     populateImmediately,
                                                                       uint8_t* ptrData,
                                                                       uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Flush_NV_Cache()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Flush Cache command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param minNumberOfLogicalBlocks - The minimum number of logical blocks to be flushed
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_NV_Flush_NV_Cache(tDevice* device, uint32_t minNumberOfLogicalBlocks);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Cache_Disable()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Disable NV Cache command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Disable(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Cache_Enable()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Enable NV Cache command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_NV_Cache_Enable(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Query_Misses()
    //
    //! \brief   Description:  This command sends a ATA NV Query Misses command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param ptrData - Pointer to the data buffer to fill with the result. This must be at least 512B is size
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2) OPENSEA_TRANSPORT_API eReturnValues ata_NV_Query_Misses(tDevice* device, uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Query_Pinned_Set()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Query Pinned Set command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param dataBlockNumber - set this to the first data block to start reading this information from. Useful if
    //!   doing this one sector at a time. (Everything is in 512B blocks here) \param ptrData - pointer to the data
    //!   buffer to use. \param dataSize - the size of the data buffer being used.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_NV_Query_Pinned_Set(tDevice* device,
                                                                uint64_t dataBlockNumber,
                                                                uint8_t* ptrData,
                                                                uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_NV_Query_Pinned_Set()
    //
    //! \brief   Description:  This command sends a ATA NV Cache Query Pinned Set command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param unpinAll - set to true to unpin all LBAs
    //!   \param ptrData - pointer to the data buffer to use. This can be M_NULLPTR if unpinAll is true
    //!   \param dataSize - the size of the data buffer being used. Set to zero if unpinAll is true
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 4)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_NV_Remove_LBAs_From_Cache(tDevice* device,
                                                                      bool     unpinAll,
                                                                      uint8_t* ptrData,
                                                                      uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Features()
    //
    //! \brief   Description:  This command sends a ATA Set Features command to the device.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param subcommand - set to the subcommand/feature you want to configure. Can be a hex value, or from
    //!   eATASetFeaturesSubcommands \param subcommandCountField - subcommand specific \param subcommandLBALo -
    //!   subcommand specific \param subcommandLBAMid - subcommand specific \param subcommandLBAHi - subcommand specific
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Features(tDevice* device,
                                                         uint8_t  subcommand,
                                                         uint8_t  subcommandCountField,
                                                         uint8_t  subcommandLBALo,
                                                         uint8_t  subcommandLBAMid,
                                                         uint16_t subcommandLBAHi);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Restore_Power_Condition_Settings(tDevice* device,
                                                                                 uint8_t  powerConditionID,
                                                                                 bool     defaultBit,
                                                                                 bool     save);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Go_To_Power_Condition(tDevice* device,
                                                                      uint8_t  powerConditionID,
                                                                      bool     delayedEntry,
                                                                      bool     holdPowerCondition);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Set_Power_Condition_Timer(tDevice* device,
                                                                          uint8_t  powerConditionID,
                                                                          uint16_t timerValue,
                                                                          bool     timerUnits,
                                                                          bool     enable,
                                                                          bool     save);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Set_Power_Condition_State(tDevice* device,
                                                                          uint8_t  powerConditionID,
                                                                          bool     enable,
                                                                          bool     save);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Enable_EPC_Feature_Set(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Disable_EPC_Feature_Set(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_EPC_Set_EPC_Power_Source(tDevice* device, uint8_t powerSource);

    //-----------------------------------------------------------------------------
    //
    //  ata_Soft_Reset()
    //
    //! \brief   Description:  This command attempts to perform a soft reset to an ATA device. Most of the time
    //! NOT_SUPPORTED will be returned due to OS or HBA limitations
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param timeout - time to wait for software reset. Should be 0, 2, 4, 6, or 14
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues ata_Soft_Reset(tDevice* device, uint8_t timeout);

    //-----------------------------------------------------------------------------
    //
    //  ata_Hard_Reset()
    //
    //! \brief   Description:  This command attempts to perform a hard reset to an ATA device. Most of the time
    //! NOT_SUPPORTED will be returned due to OS or HBA limitations
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param timeout - time to wait for software reset. Should be 0, 2, 4, 6, or 14
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) eReturnValues ata_Hard_Reset(tDevice* device, uint8_t timeout);

    //-----------------------------------------------------------------------------
    //
    //  ata_Identify_Packet_Device()
    //
    //! \brief   Description:  This command sends an Identify Packet Device command, a.k.a. ATAPI Identify.
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param ptrData - pointer to the data buffer to use.
    //!   \param dataSize - the size of the data buffer being used.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues ata_Identify_Packet_Device(tDevice* device,
                                                                   uint8_t* ptrData,
                                                                   uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Device_Configuration_Overlay_Feature()
    //
    //! \brief   Description:  This function sends a DCO command. It will only send DCO features defined by ACS2 at this
    //! time. This should not be called directly, use one of the below DCO commands to send a specific feature
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param dcoFeature - the dco feature you wish to send. This should be from the enum in ata_helper.h
    //!   \param ptrData - pointer to the data buffer to use.
    //!   \param dataSize - the size of the data buffer being used.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 4)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_Device_Configuration_Overlay_Feature(tDevice*     device,
                                                                                 eDCOFeatures dcoFeature,
                                                                                 uint8_t*     ptrData,
                                                                                 uint32_t     dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_DCO_Restore()
    //
    //! \brief   Description:  This function sends a DCO restore command
    //
    //  Entry:
    //!   \param device - device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Restore(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_DCO_Freeze_Lock()
    //
    //! \brief   Description:  This function sends a DCO freeze lock command
    //
    //  Entry:
    //!   \param device - device handle
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Freeze_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_DCO_Identify()
    //
    //! \brief   Description:  This function sends a DCO identify command
    //
    //  Entry:
    //!   \param device - device handle
    //!   \param useDMA - set to true to use the DMA version of the command (drive must support this, check identify
    //!   data) \param ptrData - pointer to the data buffer to use. \param dataSize - the size of the data buffer being
    //!   used.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Identify(tDevice* device,
                                                         bool     useDMA,
                                                         uint8_t* ptrData,
                                                         uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_DCO_Set()
    //
    //! \brief   Description:  This function sends a DCO set command
    //
    //  Entry:
    //!   \param device - device handle
    //! //!   \param useDMA - set to true to use the DMA version of the command (drive must support this, check identify
    //! data)
    //!   \param ptrData - pointer to the data buffer to use.
    //!   \param dataSize - the size of the data buffer being used.
    //
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_DCO_Set(tDevice* device, bool useDMA, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  fill_In_ATA_Drive_Info()
    //
    //! \brief   Description:  Function to send a ATA identify command and fill in
    //                         some ATA specific data to the device structure
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues fill_In_ATA_Drive_Info(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  print_Verbose_ATA_Command_Information()
    //
    //! \brief   Description:  This prints out information to the screen about the task file registers being sent to a
    //! device. This is called by a lower layer portion of the opensea-transport code.
    //
    //  Entry:
    //!   \param[in] ataCommandOptions = structure with the TFR information filled in to be printed out. (and protocol
    //!   and direction)
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API void print_Verbose_ATA_Command_Information(ataPassthroughCommand* ataCommandOptions);

    //-----------------------------------------------------------------------------
    //
    //  fill_In_ATA_Drive_Info()
    //
    //! \brief   Description:  This prints out information to the screen about the return task file registers coming
    //! back from a device. This is called by a lower layer portion of the opensea-transport code.
    //
    //  Entry:
    //!   \param[in] ataCommandOptions = structure with the TFR information filled in to be printed out. (and protocol
    //!   and direction) \param[in] device = pointer to device struct so that some things can be verified about
    //!   capabilities and features before printing the meaning of status and error bits.
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2)
    OPENSEA_TRANSPORT_API
    void print_Verbose_ATA_Command_Result_Information(ataPassthroughCommand* ataCommandOptions, tDevice* device);

    //////////////////////////////////////////
    ///         Zoned Device Commands      ///
    //////////////////////////////////////////

    //-----------------------------------------------------------------------------
    //
    //  ata_Zone_Management_In(tDevice *device, eZMAction action, uint8_t actionSpecificFeatureExt, uint16_t
    //  returnPageCount, uint64_t actionSpecificLBA, uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  Sends a zone management in command to a device. - recommend using helper functions below
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] action = set this to the zone management action to perform. (enum is in common_public.h)
    //!   \param[in] actionSpecificFeatureExt = set the action specific feature ext register bits.
    //!   \param[in] returnPageCount = used on data transfer commands. This is a count of 512B sectors to be transfered.
    //!   Should be set to zero for non-data commands \param[in] actionSpecificLBA = set the action specific LBA
    //!   registers. \param[in] actionSpecificAUX = set the action specific AUX registers. NOTE: May not be possible to
    //!   issue this command if these are set! Not all OS's or controllers support 32B passthrough CDBs! \param[out]
    //!   ptrData = pointer to the data buffer to use. Can be M_NULLPTR for non-data actions \param[in] dataSize = size
    //!   of the data buffer used for a data transfer. Should be zero for non-data actions
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_WO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_ZAC_Management_In(tDevice*  device,
                                                              eZMAction action,
                                                              uint8_t   actionSpecificFeatureExt,
                                                              uint8_t   actionSpecificFeatureBits,
                                                              uint16_t  returnPageCount,
                                                              uint64_t  actionSpecificLBA,
                                                              uint16_t  actionSpecificAUX,
                                                              uint8_t*  ptrData,
                                                              uint32_t  dataSize); // 4Ah

    //-----------------------------------------------------------------------------
    //
    //  ata_Zone_Management_Out(tDevice *device, eZMAction action, uint8_t actionSpecificFeatureExt, uint16_t
    //  pagesToSend_ActionSpecific, uint64_t actionSpecificLBA, uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  Sends a zone management out command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] action = set this to the zone management action to perform. (enum is in common_public.h)
    //!   \param[in] actionSpecificFeatureExt = set the action specific feature ext register bits.
    //!   \param[in] pagesToSend_ActionSpecific = used on data transfer commands. This is a count of 512B sectors to be
    //!   transfered. Should be set to zero for non-data commands \param[in] actionSpecificLBA = set the action specific
    //!   LBA registers. \param[in] actionSpecificAUX = set the action specific AUX registers. NOTE: May not be possible
    //!   to issue this command if these are set! Not all OS's or controllers support 32B passthrough CDBs! \param[in]
    //!   ptrData = pointer to the data buffer to use. Can be M_NULLPTR for non-data actions \param[in] dataSize = size
    //!   of the data buffer used for a data transfer. Should be zero for non-data actions
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 8)
    M_PARAM_RO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues ata_ZAC_Management_Out(tDevice*  device,
                                                               eZMAction action,
                                                               uint8_t   actionSpecificFeatureExt,
                                                               uint16_t  pagesToSend_ActionSpecific,
                                                               uint64_t  actionSpecificLBA,
                                                               uint16_t  actionSpecificAUX,
                                                               uint8_t*  ptrData,
                                                               uint32_t  dataSize); // 9Fh

    //-----------------------------------------------------------------------------
    //
    //  ata_Close_Zone_Ext(tDevice *device, bool closeAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a close zone ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] closeAll = set the closeAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to close
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZAC, use zero. On
    //!   ZAC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Close_Zone_Ext(tDevice* device,
                                                           bool     closeAll,
                                                           uint64_t zoneID,
                                                           uint16_t zoneCount); // non-data

    //-----------------------------------------------------------------------------
    //
    //  ata_Finish_Zone_Ext(tDevice *device, bool finishAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a finish zone ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] finishAll = set the finishAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to finish
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZAC, use zero. On
    //!   ZAC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Finish_Zone_Ext(tDevice* device,
                                                            bool     finishAll,
                                                            uint64_t zoneID,
                                                            uint16_t zoneCount); // non-data

    //-----------------------------------------------------------------------------
    //
    //  ata_Open_Zone_Ext(tDevice *device, bool openAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a open zone ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] openAll = set the openAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to open
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZAC, use zero. On
    //!   ZAC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Open_Zone_Ext(tDevice* device,
                                                          bool     openAll,
                                                          uint64_t zoneID,
                                                          uint16_t zoneCount); // non-data

    //-----------------------------------------------------------------------------
    //
    //  ata_Report_Zones_Ext(tDevice *device, eZoneReportingOptions reportingOptions, uint16_t returnPageCount, uint64_t
    //  zoneLocator, uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  Sends a report zones ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] reportingOptions = set to the value for the types of zones to be reported. enum is in
    //!   common_public.h \param[in] partial = set the partial bit \param[in] returnPageCount = This is a count of 512B
    //!   sectors to be transfered. \param[in] zoneLocator = zone locater field. Set the an LBA value for the lowest
    //!   reported zone (0 for all zones) \param[out] ptrData = pointer to the data buffer to use. Must be non-M_NULLPTR
    //!   \param[in] dataSize = size of the data buffer used for a data transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 6)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(6, 7)
    OPENSEA_TRANSPORT_API eReturnValues ata_Report_Zones_Ext(tDevice*              device,
                                                             eZoneReportingOptions reportingOptions,
                                                             bool                  partial,
                                                             uint16_t              returnPageCount,
                                                             uint64_t              zoneLocator,
                                                             uint8_t*              ptrData,
                                                             uint32_t              dataSize); // dma in

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Report_Realms_Ext(tDevice*                device,
                                                              eRealmsReportingOptions reportingOptions,
                                                              uint16_t                returnPageCount,
                                                              uint64_t                realmLocator,
                                                              uint8_t*                ptrData,
                                                              uint32_t                dataSize);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Report_Zone_Domains_Ext(tDevice*                    device,
                                                                    eZoneDomainReportingOptions reportingOptions,
                                                                    uint16_t                    returnPageCount,
                                                                    uint64_t                    zoneDomainLocator,
                                                                    uint8_t*                    ptrData,
                                                                    uint32_t                    dataSize);

    // recommend using numZonesSF for compatibility! Not likely possible to use AUX registers! numZonesSF means the
    // number of zones was set by set features and is reported in the ID data log
    M_NONNULL_PARAM_LIST(1, 8)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_Zone_Activate_Ext(tDevice* device,
                                                              bool     all,
                                                              uint16_t returnPageCount,
                                                              uint64_t zoneID,
                                                              bool     numZonesSF,
                                                              uint16_t numberOfZones,
                                                              uint8_t  otherZoneDomainID,
                                                              uint8_t* ptrData,
                                                              uint32_t dataSize);

    // recommend using numZonesSF for compatibility! Not likely possible to use AUX registers! numZonesSF means the
    // number of zones was set by set features and is reported in the ID data log
    M_NONNULL_PARAM_LIST(1, 8)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_Zone_Query_Ext(tDevice* device,
                                                           bool     all,
                                                           uint16_t returnPageCount,
                                                           uint64_t zoneID,
                                                           bool     numZonesSF,
                                                           uint16_t numberOfZones,
                                                           uint8_t  otherZoneDomainID,
                                                           uint8_t* ptrData,
                                                           uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Sequentialize_Zone_Ext(tDevice* device,
                                                                   bool     all,
                                                                   uint64_t zoneID,
                                                                   uint16_t zoneCount);

    //-----------------------------------------------------------------------------
    //
    //  ata_Reset_Write_Pointers_Ext(tDevice *device, bool resetAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a reset write pointers ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] resetAll = set the resetAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to open
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Reset_Write_Pointers_Ext(tDevice* device,
                                                                     bool     resetAll,
                                                                     uint64_t zoneID,
                                                                     uint16_t zoneCount); // non-data

    //-----------------------------------------------------------------------------
    //
    //  ata_Media_Eject(tDevice *device)
    //
    //! \brief   Description:  Sends a ATA Media Eject command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Media_Eject(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Get_Media_Status(tDevice *device)
    //
    //! \brief   Description:  Sends a ATA get media status command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Get_Media_Status(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Media_Lock(tDevice *device)
    //
    //! \brief   Description:  Sends a ATA media lock command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Media_Lock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Media_Unlock(tDevice *device)
    //
    //! \brief   Description:  Sends a ATA media unlock command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Media_Unlock(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  ata_Zeros_Ext(tDevice *device, uint16_t numberOfLogicalSectors, uint64_t lba, bool trim)
    //
    //! \brief   Description:  Sends a ATA Zeros Ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] numberOfLogicalSectors = range/number of sectors to write zeros to
    //!   \param[in] lba = starting lba to write zeros to
    //!   \param[in] trim = trim bit. This allows the device to trim these sectors instead of writing zeros
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Zeros_Ext(tDevice* device,
                                                      uint16_t numberOfLogicalSectors,
                                                      uint64_t lba,
                                                      bool     trim);

    //-----------------------------------------------------------------------------
    //
    //  ata_Set_Sector_Configuration_Ext(tDevice *device, uint16_t commandCheck, uint8_t
    //  sectorConfigurationDescriptorIndex)
    //
    //! \brief   Description:  Sends a ATA Set Sector Configuration Ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] commandCheck = see ACS4
    //!   \param[in] sectorConfigurationDescriptorIndex = (bits 2:0 are valid) see ACS4
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Set_Sector_Configuration_Ext(tDevice* device,
                                                                         uint16_t commandCheck,
                                                                         uint8_t  sectorConfigurationDescriptorIndex);

    //-----------------------------------------------------------------------------
    //
    //  ata_Get_Physical_Element_Status(tDevice *device, uint8_t filter, uint8_t reportType, uint64_t startingElement,
    //  uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  Sends the ATA Get Physical Element Status command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] filter = filter type for command output. NOTE: Bits 0:1 are valid
    //!   \param[in] reportType = report type filter. NOTE: BITS 0:3 are valid
    //!   \param[in] startingElement = element to start requesting from
    //!   \param[in] ptrData = pointer to data buffer to fill with command result
    //!   \param[in] dataSize = amount of data allocated for retrieved data (512B blocks)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Get_Physical_Element_Status(tDevice* device,
                                                                        uint8_t  filter,
                                                                        uint8_t  reportType,
                                                                        uint64_t startingElement,
                                                                        uint8_t* ptrData,
                                                                        uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Remove_Element_And_Truncate(tDevice *device, uint32_t elementIdentifier, uint64_t requestedMaxLBA)
    //
    //! \brief   Description:  Sends the ATA Remove and Truncate command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] elementIdentifier = identifier of the element to truncate
    //!   \param[in] requestedCapacity = requested new native/accessible max capacity. Can be left as zero for drive to
    //!   make this call
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Remove_Element_And_Truncate(tDevice* device,
                                                                        uint32_t elementIdentifier,
                                                                        uint64_t requestedMaxLBA);

    //-----------------------------------------------------------------------------
    //
    //  ata_Remove_Element_And_Modify_Zones(tDevice *device, uint32_t elementIdentifier)
    //
    //! \brief   Description:  Sends the ATA Remove and modify zones command for ZAC devices
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] elementIdentifier = identifier of the element to truncate
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Remove_Element_And_Modify_Zones(tDevice* device,
                                                                            uint32_t elementIdentifier);

    //-----------------------------------------------------------------------------
    //
    //  ata_Restore_Elements_And_Rebuild(tDevice *device)
    //
    //! \brief   Description:  Sends the ATA Restore Elements and Rebuild command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Restore_Elements_And_Rebuild(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Mutate_Ext(tDevice* device,
                                                       bool     requestMaximumAccessibleCapacity,
                                                       uint32_t requestedConfigurationID);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Checksum_Into_Data_Buffer(uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  Use this function to calculate and set a checksum into a data buffer. Useful for some
    //! SMART commands and DCO commands.
    //
    //  Entry:
    //!   \param[in] ptrData = pointer to data buffer to use
    //!
    //  Exit:
    //!   \return uint8_t checksum value
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API uint8_t calculate_ATA_Checksum(const uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  is_Checksum_Valid(uint8_t *ptrData, uint32_t dataSize, uint32_t *firstInvalidSector)
    //
    //! \brief   Description:  Use this function to check if the checksum provided in byte 511 of each sector is valid.
    //! Useful for SMART, Identify, DCO, and some Logs.
    //!                        When a multiple 512 byte sector buffer is given, each sector will be checked and if any
    //!                        is invalid, false is returned and firstInvalidSector tells the sector number with an
    //!                        error
    //
    //  Entry:
    //!   \param[in] ptrData = pointer to data buffer to use
    //!   \param[in] dataSize = set this to a multiple of 512 (LEGACY_DRIVE_SEC_SIZE) bytes. Each 511 * nth byte will
    //!   have a check sum calculated and verified to be zero \param[out] firstInvalidSector = must be non-M_NULLPTR.
    //!   will contain the sector number of the first invalid checksum (if any)
    //!
    //  Exit:
    //!   \return true = checksum(s) valid, false = invalid checksum found. Check firstInvalidSector for which sector
    //!   has an error
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    OPENSEA_TRANSPORT_API
    bool is_Checksum_Valid(const uint8_t* ptrData, uint32_t dataSize, uint32_t* firstInvalidSector);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Checksum_Into_Data_Buffer(uint8_t *ptrData, uint32_t dataSize)
    //
    //! \brief   Description:  Use this function to calculate and set a checksum into a data buffer. Useful for some
    //! SMART commands and DCO commands.
    //
    //  Entry:
    //!   \param[out] ptrData = pointer to data buffer to use
    //!   \param[in] dataSize = set this to a multiple of 512 (LEGACY_DRIVE_SEC_SIZE) bytes. Each 511 bytes will have a
    //!   check sum calculated and placed into the 511 * nth byte
    //!
    //  Exit:
    //!   \return SUCCESS = everything worked, !SUCCESS = error check return code.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO_SIZE(1, 2)
    OPENSEA_TRANSPORT_API eReturnValues set_ATA_Checksum_Into_Data_Buffer(uint8_t* ptrData, uint32_t dataSize);

    // A couple helper functions to help with Legacy drives
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_LBA_Mode_Supported(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_CHS_Mode_Supported(tDevice* device);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO(5)
    OPENSEA_TRANSPORT_API eReturnValues
    convert_CHS_To_LBA(tDevice* device, uint16_t cylinder, uint8_t head, uint16_t sector, uint32_t* lba);

    M_NONNULL_PARAM_LIST(1, 3, 4, 5)
    M_PARAM_RO(1)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    OPENSEA_TRANSPORT_API eReturnValues
    convert_LBA_To_CHS(tDevice* device, uint32_t lba, uint16_t* cylinder, uint8_t* head, uint8_t* sector);

    /////////////////////////////////////////////////////////////////////////////////
    /// Obsolete ATA Commands. These commands are from specs prior to ATA-ATAPI 7 ///
    /////////////////////////////////////////////////////////////////////////////////

    // Last seen in ATA-3. All inputs are vendor specific and outputs are vendor specific. Protocol is vendor specific.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 8)
    M_PARAM_RO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Format_Track(tDevice*     device,
                                                                uint8_t      feature,
                                                                uint8_t      sectorCount,
                                                                uint8_t      sectorNumber,
                                                                uint8_t      cylinderLow,
                                                                uint8_t      cylinderHigh,
                                                                uint8_t*     ptrData,
                                                                uint32_t     dataSize,
                                                                eAtaProtocol protocol,
                                                                bool         lbaMode);

    // Last seen in ATA-3. Prior to ATA3, the lower nibble of the command could be 0 - F.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Recalibrate(tDevice* device, uint8_t lowCmdNibble, bool chsMode);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 7)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Read_DMA_CHS(tDevice* device,
                                                                uint16_t cylinder,
                                                                uint8_t  head,
                                                                uint8_t  sector,
                                                                uint8_t* ptrData,
                                                                uint16_t sectorCount,
                                                                uint32_t dataSize,
                                                                bool     extendedCmd);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 7)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Read_Multiple_CHS(tDevice* device,
                                                                     uint16_t cylinder,
                                                                     uint8_t  head,
                                                                     uint8_t  sector,
                                                                     uint8_t* ptrData,
                                                                     uint16_t sectorCount,
                                                                     uint32_t dataSize,
                                                                     bool     extendedCmd);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Set_Max_Address_CHS(tDevice* device,
                                                                       uint16_t newMaxCylinder,
                                                                       uint8_t  newMaxHead,
                                                                       uint8_t  newMaxSector,
                                                                       bool     volatileValue);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Set_Max_Address_Ext_CHS(tDevice* device,
                                                                           uint16_t newMaxCylinder,
                                                                           uint8_t  newMaxHead,
                                                                           uint8_t  newMaxSector,
                                                                           bool     volatileValue);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 7)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Read_Sectors_CHS(tDevice* device,
                                                                    uint16_t cylinder,
                                                                    uint8_t  head,
                                                                    uint8_t  sector,
                                                                    uint8_t* ptrData,
                                                                    uint16_t sectorCount,
                                                                    uint32_t dataSize,
                                                                    bool     extendedCmd);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Read_Verify_Sectors_CHS(tDevice* device,
                                                                           bool     extendedCmd,
                                                                           uint16_t numberOfSectors,
                                                                           uint16_t cylinder,
                                                                           uint8_t  head,
                                                                           uint8_t  sector);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Read_Verify_Sectors_No_Retry_CHS(tDevice* device,
                                                                                    uint16_t numberOfSectors,
                                                                                    uint16_t cylinder,
                                                                                    uint8_t  head,
                                                                                    uint8_t  sector);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Read_Verify_Sectors_No_Retry(tDevice* device,
                                                                         uint16_t numberOfSectors,
                                                                         uint32_t LBA);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_DMA_CHS(tDevice* device,
                                                                 uint16_t cylinder,
                                                                 uint8_t  head,
                                                                 uint8_t  sector,
                                                                 uint8_t* ptrData,
                                                                 uint32_t dataSize,
                                                                 bool     extendedCmd,
                                                                 bool     fua);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Multiple_CHS(tDevice* device,
                                                                      uint16_t cylinder,
                                                                      uint8_t  head,
                                                                      uint8_t  sector,
                                                                      uint8_t* ptrData,
                                                                      uint32_t dataSize,
                                                                      bool     extendedCmd,
                                                                      bool     fua);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Sectors_CHS(tDevice* device,
                                                                     uint16_t cylinder,
                                                                     uint8_t  head,
                                                                     uint8_t  sector,
                                                                     uint8_t* ptrData,
                                                                     uint32_t dataSize,
                                                                     bool     extendedCmd);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Legacy_Seek_CHS(tDevice* device, uint16_t cylinder, uint8_t head, uint8_t sector, uint8_t lowCmdNibble);

    // last seen in ATA-ATAPI 6.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Seek(tDevice* device, uint32_t lba, uint8_t lowCmdNibble);

    // last seen in ATA-3
    M_NONNULL_PARAM_LIST(1, 6)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(6, 7)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Read_Long_CHS(tDevice* device,
                                                                 bool     retries,
                                                                 uint16_t cylinder,
                                                                 uint8_t  head,
                                                                 uint8_t  sector,
                                                                 uint8_t* ptrData,
                                                                 uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Legacy_Read_Long(tDevice* device, bool retries, uint32_t lba, uint8_t* ptrData, uint32_t dataSize);

    // last seen in ATA-3
    M_NONNULL_PARAM_LIST(1, 6)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(6, 7)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Long_CHS(tDevice* device,
                                                                  bool     retries,
                                                                  uint16_t cylinder,
                                                                  uint8_t  head,
                                                                  uint8_t  sector,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Legacy_Write_Long(tDevice* device, bool retries, uint32_t lba, uint8_t* ptrData, uint32_t dataSize);

    // last seen in ATA-2
    // Sub command 22h = LBA (or Cyl lo, hi, head#), and sec number specify where to start. count specifies how many
    // sectors to write. Taking in lba mode by default since CHS is dead. (528MB and higher are recommended to implement
    // LBA) Sub command DDh = initialize all usable sectors. Number of sectors field is ignored
    M_NONNULL_PARAM_LIST(1, 7)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Same_CHS(tDevice* device,
                                                                  uint8_t  subcommand,
                                                                  uint8_t  numberOfSectorsToWrite,
                                                                  uint16_t cylinder,
                                                                  uint8_t  head,
                                                                  uint8_t  sector,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Same(tDevice* device,
                                                              uint8_t  subcommand,
                                                              uint8_t  numberOfSectorsToWrite,
                                                              uint32_t lba,
                                                              uint8_t* ptrData,
                                                              uint32_t dataSize);

    // last seen in ATA-3
    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Verify_CHS(tDevice* device,
                                                                    uint16_t cylinder,
                                                                    uint8_t  head,
                                                                    uint8_t  sector,
                                                                    uint8_t* ptrData,
                                                                    uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Write_Verify(tDevice* device,
                                                                uint32_t lba,
                                                                uint8_t* ptrData,
                                                                uint32_t dataSize);

    // last seen in ATA-3
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Identify_Device_DMA(tDevice* device,
                                                                       uint8_t* ptrData,
                                                                       uint32_t dataSize);

    OPENSEA_TRANSPORT_API eReturnValues ata_Legacy_Check_Power_Mode(tDevice* device, uint8_t* powerMode);

    // These functions below are commands that can be sent in PIO or DMA Mode.
    // They will automatically try DMA if it is supported, then retry with PIO mode if the Translator or Driver doesn't
    // support issuing DMA mode commands.
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Read_Log_Ext_Cmd(tDevice* device,
                                                                  uint8_t  logAddress,
                                                                  uint16_t pageNumber,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize,
                                                                  uint16_t featureRegister);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Write_Log_Ext_Cmd(tDevice* device,
                                                                   uint8_t  logAddress,
                                                                   uint16_t pageNumber,
                                                                   uint8_t* ptrData,
                                                                   uint32_t dataSize,
                                                                   bool     forceRTFRs);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 6)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Download_Microcode_Cmd(tDevice*                   device,
                                                                        eDownloadMicrocodeFeatures subCommand,
                                                                        uint16_t                   blockCount,
                                                                        uint16_t                   bufferOffset,
                                                                        uint8_t*                   pData,
                                                                        uint32_t                   dataLen,
                                                                        bool                       firstSegment,
                                                                        bool                       lastSegment,
                                                                        uint32_t                   timeoutSeconds);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Trusted_Send_Cmd(tDevice* device,
                                                                  uint8_t  securityProtocol,
                                                                  uint16_t securityProtocolSpecific,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Trusted_Receive_Cmd(tDevice* device,
                                                                     uint8_t  securityProtocol,
                                                                     uint16_t securityProtocolSpecific,
                                                                     uint8_t* ptrData,
                                                                     uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_TRANSPORT_API eReturnValues send_ATA_Read_Buffer_Cmd(tDevice* device, uint8_t* ptrData);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO(2) OPENSEA_TRANSPORT_API eReturnValues send_ATA_Write_Buffer_Cmd(tDevice* device, uint8_t* ptrData);

    M_NONNULL_PARAM_LIST(1, 7)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Read_Stream_Cmd(tDevice* device,
                                                                 uint8_t  streamID,
                                                                 bool     notSequential,
                                                                 bool     readContinuous,
                                                                 uint8_t  commandCCTL,
                                                                 uint64_t LBA,
                                                                 uint8_t* ptrData,
                                                                 uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 7)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_Write_Stream_Cmd(tDevice* device,
                                                                  uint8_t  streamID,
                                                                  bool     flush,
                                                                  bool     writeContinuous,
                                                                  uint8_t  commandCCTL,
                                                                  uint64_t LBA,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize);

    // Similar to above, but for SCT stuff. This will automatically retry from DMA to PIO mode. Also removes GPL flag.
    // Now depends on if device supports GPL or not internally (can be flipped in
    // device->drive_info.ata_Options.generalPurposeLoggingSupported if you want to force a SMART command)
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT(tDevice*               device,
                                                     eDataTransferDirection direction,
                                                     uint8_t                logAddress,
                                                     uint8_t*               dataBuf,
                                                     uint32_t               dataSize,
                                                     bool                   forceRTFRs);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Status(tDevice* device, uint8_t* dataBuf, uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Command(tDevice* device,
                                                             uint8_t* dataBuf,
                                                             uint32_t dataSize,
                                                             bool     forceRTFRs);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Data_Transfer(tDevice*               device,
                                                                   eDataTransferDirection direction,
                                                                   uint8_t*               dataBuf,
                                                                   uint32_t               dataSize);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RW_SIZE(4, 5)
    M_PARAM_WO(6)
    M_PARAM_WO(7)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Read_Write_Long(tDevice*    device,
                                                                     eSCTRWLMode mode,
                                                                     uint64_t    lba,
                                                                     uint8_t*    dataBuf,
                                                                     uint32_t    dataSize,
                                                                     uint16_t*   numberOfECCCRCBytes,
                                                                     uint16_t*   numberOfBlocksRequested);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Write_Same(tDevice*               device,
                                                                eSCTWriteSameFunctions functionCode,
                                                                uint64_t               startLBA,
                                                                uint64_t               fillCount,
                                                                uint8_t*               pattern,
                                                                uint64_t               patternLength);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(4)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Error_Recovery_Control(tDevice*  device,
                                                                            uint16_t  functionCode,
                                                                            uint16_t  selectionCode,
                                                                            uint16_t* currentValue,
                                                                            uint16_t  recoveryTimeLimit);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Feature_Control(tDevice*  device,
                                                                     uint16_t  functionCode,
                                                                     uint16_t  featureCode,
                                                                     uint16_t* state,
                                                                     uint16_t* optionFlags);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues send_ATA_SCT_Data_Table(tDevice* device,
                                                                uint16_t functionCode,
                                                                uint16_t tableID,
                                                                uint8_t* dataBuf,
                                                                uint32_t dataSize);

    // NCQ command definitions
    // NOTE: You can try these all you want, but it is basically impossible to issue these in passthrough.
    //       Some USB adapters will allow SOME of them.
    //       libata in Linux will allow most of them.
    //       No other HBAs or operating systems are known to support/allow these to be issued.
    //       Stick to the synchronous commands whenever possible due to how limited support for these commands is.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Non_Data(tDevice* device,
                                                         uint8_t  subCommand /*bits 4:0*/,
                                                         uint16_t subCommandSpecificFeature /*bits 11:0*/,
                                                         uint8_t  subCommandSpecificCount,
                                                         uint8_t  ncqTag /*bits 5:0*/,
                                                         uint64_t lba,
                                                         uint32_t auxilary);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Abort_NCQ_Queue(tDevice* device,
                                                                uint8_t  abortType /*bits0:3*/,
                                                                uint8_t  prio /*bits 1:0*/,
                                                                uint8_t  ncqTag,
                                                                uint8_t  tTag);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Deadline_Handling(tDevice* device,
                                                                  bool     rdnc,
                                                                  bool     wdnc,
                                                                  uint8_t  ncqTag);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Set_Features(tDevice*                   device,
                                                             eATASetFeaturesSubcommands subcommand,
                                                             uint8_t                    subcommandCountField,
                                                             uint8_t                    subcommandLBALo,
                                                             uint8_t                    subcommandLBAMid,
                                                             uint16_t                   subcommandLBAHi,
                                                             uint8_t                    ncqTag);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_NCQ_Zeros_Ext(tDevice* device, uint16_t numberOfLogicalSectors, uint64_t lba, bool trim, uint8_t ncqTag);

    M_NONNULL_PARAM_LIST(1, 8)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Receive_FPDMA_Queued(tDevice* device,
                                                                     uint8_t  subCommand /*bits 5:0*/,
                                                                     uint16_t sectorCount /*ft*/,
                                                                     uint8_t  prio /*bits 1:0*/,
                                                                     uint8_t  ncqTag,
                                                                     uint64_t lba,
                                                                     uint32_t auxilary,
                                                                     uint8_t* ptrData,
                                                                     uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Read_Log_DMA_Ext(tDevice* device,
                                                                 uint8_t  logAddress,
                                                                 uint16_t pageNumber,
                                                                 uint8_t* ptrData,
                                                                 uint32_t dataSize,
                                                                 uint16_t featureRegister,
                                                                 uint8_t  prio /*bits 1:0*/,
                                                                 uint8_t  ncqTag);

    M_NONNULL_PARAM_LIST(1, 8)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Send_FPDMA_Queued(tDevice* device,
                                                                  uint8_t  subCommand /*bits 5:0*/,
                                                                  uint16_t sectorCount /*ft*/,
                                                                  uint8_t  prio /*bits 1:0*/,
                                                                  uint8_t  ncqTag,
                                                                  uint64_t lba,
                                                                  uint32_t auxilary,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Data_Set_Management(tDevice* device,
                                                                    bool     trimBit,
                                                                    uint8_t* ptrData,
                                                                    uint32_t dataSize,
                                                                    uint8_t  prio /*bits 1:0*/,
                                                                    uint8_t  ncqTag);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Write_Log_DMA_Ext(tDevice* device,
                                                                  uint8_t  logAddress,
                                                                  uint16_t pageNumber,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize,
                                                                  uint8_t  prio /*bits 1:0*/,
                                                                  uint8_t  ncqTag);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Read_FPDMA_Queued(tDevice* device,
                                                                  bool     fua,
                                                                  uint64_t lba,
                                                                  uint8_t* ptrData,
                                                                  uint32_t dataSize,
                                                                  uint8_t  prio,
                                                                  uint8_t  ncqTag,
                                                                  uint8_t  icc);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues ata_NCQ_Write_FPDMA_Queued(tDevice* device,
                                                                   bool     fua,
                                                                   uint64_t lba,
                                                                   uint8_t* ptrData,
                                                                   uint32_t dataSize,
                                                                   uint8_t  prio,
                                                                   uint8_t  ncqTag,
                                                                   uint8_t  icc);

    // Old TCQ commands
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Read_DMA_Queued(tDevice* device, bool ext, uint64_t lba, uint8_t* ptrData, uint32_t dataSize, uint8_t tag);

    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Write_DMA_Queued(tDevice* device, bool ext, uint64_t lba, uint8_t* ptrData, uint32_t dataSize, uint8_t tag);

#if defined(__cplusplus)
}
#endif
