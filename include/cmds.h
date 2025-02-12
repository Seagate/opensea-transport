// SPDX-License-Identifier: MPL-2.0

//! \file cmds.h
//! \brief Defines the structures and function headers for common commands that work across
//! different drive types and interfaces.
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

#include <time.h>

    //-----------------------------------------------------------------------------
    //
    //  send_Sanitize_Block_Erase()
    //
    //! \brief   Description:  Funtion to send a Sanitize Block Erase command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] exitFailureMode = set the failure mode bit to 1. See ACS3 or SBC4 for details on what this does
    //!   \param[in] znr - zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues send_Sanitize_Block_Erase(tDevice* device, bool exitFailureMode, bool znr);

    //-----------------------------------------------------------------------------
    //
    //  send_Sanitize_Crypto_Erase()
    //
    //! \brief   Description:  Function to send a Sanitize Crypto Scramble command to a device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] exitFailureMode = set the failure mode bit to 1. See ACS3 or SBC4 for details on what this does
    //!   \param[in] znr - zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues send_Sanitize_Crypto_Erase(tDevice* device, bool exitFailureMode, bool znr);

    //-----------------------------------------------------------------------------
    //
    //  send_Sanitize_Overwrite_Erase()
    //
    //! \brief   Description:  Function to send a Sanitize Overwrite command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] exitFailureMode = set the failure mode bit to 1. See ACS3 or SBC4 for details on what this does
    //!   \param[in] invertBetweenPasses = set the bit to invert the pattern between passes
    //!   \param[in] overwritePasses = this is the number of passes to run. a value of 0 means 16 passes (which is the
    //!   max for sanitize). (For SCSI, we adjust this to set 16 since a value of 0 is reserved) \param[in] pattern =
    //!   pointer to a buffer containing a pattern. (Set to M_NULLPTR to use zeros) \param[in] patternLength = the
    //!   length of the patter. Max length on SCSI is 1 logical sector. On ATA, the length MUST BE 4! Only required if
    //!   pattern is non-M_NULLPTR. \param[in] znr - zone no reset bit. This is used on host managed and host aware
    //!   drives to not reset the zone pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues send_Sanitize_Overwrite_Erase(tDevice* device,
                                                                      bool     exitFailureMode,
                                                                      bool     invertBetweenPasses,
                                                                      uint8_t  overwritePasses,
                                                                      uint8_t* pattern,
                                                                      uint16_t patternLength,
                                                                      bool     znr);

    //-----------------------------------------------------------------------------
    //
    //  send_Sanitize_Exit_Failure_Mode()
    //
    //! \brief   Description:  Function to send a Sanitize Exit Failure Mode command to the device. This clears a
    //! sanitize error from the device
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return SUCCESS = pass, FAILURE = fail
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues send_Sanitize_Exit_Failure_Mode(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  spin_down_drive( tDevice * device, bool sleepState )
    //
    //! \brief   Spin down a drive (The OS may still be able to spin it up which cannot be controlled).
    //
    //  Entry:
    //!   \param device - file descriptor
    //!   \param sleepState - set to true to send ata_Sleep instead of standby immediate (Only valid on ATA as SCSI does
    //!   not offer this capability).
    //!
    //  Exit:
    //!   \return SUCCESS = good, !SUCCESS something went wrong see error codes
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues spin_down_drive(tDevice* device, bool sleepState);

    //-----------------------------------------------------------------------------
    //
    //  fill_Drive_Info_Data()
    //
    //! \brief   Description:  Generic Function to get drive information data filled
    //                         into the driveInfo_TYPE of the device structure.
    //                         This function assumes the type & interface has already
    //                         determined by the OS layer.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues fill_Drive_Info_Data(tDevice* device);

    typedef enum eDownloadModeEnum
    {
        DL_FW_ACTIVATE,
        DL_FW_FULL,
        DL_FW_TEMP, // obsolete
        DL_FW_SEGMENTED,
        DL_FW_DEFERRED,
        DL_FW_DEFERRED_SELECT_ACTIVATE, // SAS Only! If this is used on ATA, the activation event can only be a power
                                        // cycle. This is treated exactly the same as above on ATA.
        DL_FW_UNKNOWN
    } eDownloadMode;

    //-----------------------------------------------------------------------------
    //
    //  firmware_Download_Command()
    //
    //! \brief   Description:  This function will send a firmware download command to a device for you as it will
    //! automatically determine the correct command based on drive type
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param dlMode - enum value specifying the download mode to use.
    //!   \param offset - offset (used for segmented download, otherwise set to 0)
    //!   \param xferLen - transfer length
    //!   \param ptrData - pointer to the data buffer that will do the transfer
    //!   \param slotNumber - set to the slot number (NVMe) or buffer ID (SCSI) that you want to set. If unsure, set
    //!   this to zero. Ignored on ATA drives. Only used for activate on NVMe drives. \param existingImage - set to true
    //!   if using this command to activate an existing image in the specified slot, false if it is a new image to
    //!   activate. \param firstSegment - first segment of a firmware download. Helps low-level OS ioctls (Mostly
    //!   Windows based) \param lastSegment - last segment of a firmware download. Helps low-level OS ioctls (Mostly
    //!   Windows based) \param timeoutSeconds - timeout for the command. Selectable since some updates may be slower
    //!   than others. \param nvmeForceCA - force a specific commit action for NVMe firmware commit commands \param
    //!   commitAction - the forced action. nvmeForceCA must be set to true, otherwise this is ignored. \param
    //!   forceDisableReset - if this is set to true, it overrides any automatic reset after an NVMe firmware commit.
    //!   When false, it lets this function decide when to issue a reset.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 4)
    M_PARAM_RO_SIZE(5, 4)
    OPENSEA_TRANSPORT_API eReturnValues firmware_Download_Command(tDevice*      device,
                                                                  eDownloadMode dlMode,
                                                                  uint32_t      offset,
                                                                  uint32_t      xferLen,
                                                                  uint8_t*      ptrData,
                                                                  uint8_t       slotNumber,
                                                                  bool          existingImage,
                                                                  bool          firstSegment,
                                                                  bool          lastSegment,
                                                                  uint32_t      timeoutSeconds,
                                                                  bool          nvmeForceCA,
                                                                  uint8_t       commitAction,
                                                                  bool          forceDisableReset);

    //-----------------------------------------------------------------------------
    //
    //  firmware_Download_Activate()
    //
    //! \brief   Description:  This will send the command to activate a firmware that was downloaded in a deferred
    //! update
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param slotNumber - set to the slot number (NVMe) or buffer ID (SCSI) that you want to set. If unsure, set
    //!   this to zero. Ignored on ATA drives. \param existingImage - set to true if using this command to activate an
    //!   existing image in the specified slot, false if it is a new image to activate. \param timeoutSeconds - timeout
    //!   for the command. Selectable since some updates may be slower than others. \param nvmeForceCA - force a
    //!   specific commit action for NVMe firmware commit commands \param commitAction - the forced action. nvmeForceCA
    //!   must be set to true, otherwise this is ignored. \param forceDisableReset - if this is set to true, it
    //!   overrides any automatic reset after an NVMe firmware commit. When false, it lets this function decide when to
    //!   issue a reset.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues firmware_Download_Activate(tDevice* device,
                                                                   uint8_t  slotNumber,
                                                                   bool     existingImage,
                                                                   uint32_t timeoutSeconds,
                                                                   bool     nvmeForceCA,
                                                                   uint8_t  commitAction,
                                                                   bool     forceDisableReset);

    typedef enum eSecurityProtocolsEnum
    {
        SECURITY_PROTOCOL_RETURN_SUPPORTED                                                = 0x00,
        SECURITY_PROTOCOL_INFORMATION                                                     = 0x00,
        SECURITY_PROTOCOL_TCG_1                                                           = 0x01,
        SECURITY_PROTOCOL_TCG_2                                                           = 0x02,
        SECURITY_PROTOCOL_TCG_3                                                           = 0x03,
        SECURITY_PROTOCOL_TCG_4                                                           = 0x04,
        SECURITY_PROTOCOL_TCG_5                                                           = 0x05,
        SECURITY_PROTOCOL_TCG_6                                                           = 0x06,
        SECURITY_PROTOCOL_CbCS                                                            = 0x07,
        SECURITY_PROTOCOL_TAPE_DATA_ENCRYPTION                                            = 0x20,
        SECURITY_PROTOCOL_DATA_ENCRYPTION_CONFIGURATION                                   = 0x21,
        SECURITY_PROTOCOL_SA_CREATION_CAPABILITIES                                        = 0x40,
        SECURITY_PROTOCOL_IKE_V2_SCSI                                                     = 0x41,
        SECURITY_PROTOCOL_SD_ASSOCIATION                                                  = 0xE7,
        SECURITY_PROTOCOL_DMTF_SECURITY_PROTOCOL_AND_DATA_MODEL                           = 0xE8,
        SECURITY_PROTOCOL_NVM_EXPRESS_RESERVED                                            = 0xE9,
        SECURITY_PROTOCOL_NVM_EXPRESS                                                     = 0xEA,
        SECURITY_PROTOCOL_SCSA                                                            = 0xEB,
        SECURITY_PROTOCOL_JEDEC_UFS                                                       = 0xEC,
        SECURITY_PROTOCOL_SDcard_TRUSTEDFLASH_SECURITY                                    = 0xED,
        SECURITY_PROTOCOL_IEEE_1667                                                       = 0xEE,
        SECURITY_PROTOCOL_AUTHENTICATION_IN_HOST_ATTACHMENTS_OF_TRANSIENT_STORAGE_DEVICES = 0xEE,
        SECURITY_PROTOCOL_ATA_DEVICE_SERVER_PASSWORD                                      = 0xEF, // ATA over SCSI only
        SECURITY_PROTOCOL_RESERVED
    } eSecurityProtocols;

    //-----------------------------------------------------------------------------
    //
    //  security_Send()
    //
    //! \brief   Description:  This function will send a security command to the device transfering data to the device
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param securityProtocol - security protocol being used
    //!   \param securityProtocolSpecific - any specific information to the security protocol being used
    //!   \param ptrData - pointer to the data buffer that will do the transfer
    //!   \param dataSize - size of the data to transfer
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 5)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues security_Send(tDevice* device,
                                                      uint8_t  securityProtocol,
                                                      uint16_t securityProtocolSpecific,
                                                      uint8_t* ptrData,
                                                      uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  security_Receive()
    //
    //! \brief   Description:  This function will send a security command to the device transferring data to the host
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param securityProtocol - security protocol being used
    //!   \param securityProtocolSpecific - any specific information to the security protocol being used
    //!   \param ptrData - pointer to the data buffer that will do the transfer
    //!   \param dataSize - size of the data to transfer
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 5)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues security_Receive(tDevice* device,
                                                         uint8_t  securityProtocol,
                                                         uint16_t securityProtocolSpecific,
                                                         uint8_t* ptrData,
                                                         uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  write_Same()
    //
    //! \brief   Description:  This function will send a write same command to a drive. For scsi drives, writesame16 is
    //! used, on ata drives, SCT write same is used.
    //!          If pattern is M_NULLPTR, a zero pattern is used (SCSI sets the nodataout bit, which may not be
    //!          supported on the drive, so sending a buffer is recommended). If pattern is non-null, the buffer it
    //!          points to MUST be 1 logical sector in size.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param startingLba - lba to start the write same at
    //!   \param numberOfLogicalBlocks - The number of logical blocks to write to from the startingLba (range). SCSI Has
    //!   a max range of a UINT32, ATA has max of UINT64 \param pattern - pointer to a buffer that is 1 logical sector
    //!   in size and contains a pattern to write. If this is M_NULLPTR, a zero pattern will be used in place.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_RO(4)
    OPENSEA_TRANSPORT_API eReturnValues write_Same(tDevice* device,
                                                   uint64_t startingLba,
                                                   uint64_t numberOfLogicalBlocks,
                                                   uint8_t* pattern);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Write_Psuedo_Uncorrectable_Supported(tDevice* device);
    //-----------------------------------------------------------------------------
    //
    //  write_Psuedo_Uncorrectable_Error()
    //
    //! \brief   Description:  This function will create a pseudo uncorrectable error on a drive. The error will be
    //! written to the full physical sector. If the LBA provided is not aligned, it will be aligned in this function.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param corruptLBA - lba you wish to corrupt with a pseudo uncorrectable error. (If not aligned to the
    //!   beginning of the physical block, it will be aligned for you here)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues write_Psuedo_Uncorrectable_Error(tDevice* device, uint64_t corruptLBA);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Write_Flagged_Uncorrectable_Supported(tDevice* device);
    //-----------------------------------------------------------------------------
    //
    //  write_Flagged_Uncorrectable_Error()
    //
    //! \brief   Description:  This function will create a flagged uncorrectable error on a drive. This will only flag
    //! the specified logical sector
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param corruptLBA - lba you wish to corrupt with a flagged uncorrectable error.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues write_Flagged_Uncorrectable_Error(tDevice* device, uint64_t corruptLBA);

    //-----------------------------------------------------------------------------
    //
    //  read_LBA()
    //
    //! \brief   Description:  This function first tries performing a read using the OS's defined read function
    //! (os_Read), but when that isn't supported it tries an io_Read instead.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start reading at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send read verify
    //!   AND read in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to fill
    //!   in with read data \param dataSize - size of the buffer, in bytes, for what is to be read. This size is divided
    //!   by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    read_LBA(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  write_LBA()
    //
    //! \brief   Description:  This function first tries performing a write using the OS's defined write function
    //! (os_Write), but when that isn't supported it tries an io_Write instead.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start writing at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send write AND
    //!   read verify in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to
    //!   use for writing data \param dataSize - size of the buffer, in bytes, for what is to be written. This size is
    //!   divided by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    write_LBA(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  verify_LBA()
    //
    //! \brief   Description:  This function sends a verify command to a device for the lba and range specified.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start writing at
    //!   \param range - number of LBAs to verify
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues verify_LBA(tDevice* device, uint64_t lba, uint32_t range);

    //-----------------------------------------------------------------------------
    //
    //  flush_Cache()
    //
    //! \brief   Description:  This function sends a flush cache / synchronize cache command to a device to flush all
    //! LBAs in the cache to the media
    //  Entry:
    //!   \param device - pointer to the device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues flush_Cache(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  os_Read()
    //
    //! \brief   Description:  This function sends a read through the OS, it does not use a particular scsi or ata read
    //! command, it lets the OS decide the best possible way to read data from the device.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start reading at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send read verify
    //!   AND read in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to fill
    //!   in with read data \param dataSize - size of the buffer, in bytes, for what is to be read. This size is divided
    //!   by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    os_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  os_Write()
    //
    //! \brief   Description:  This function sends a write through the OS, it does not use a particular scsi or ata
    //! write command, it lets the OS decide the best possible way to write data to the device.
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start writing at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send write AND
    //!   read verify in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to
    //!   use for writing data \param dataSize - size of the buffer, in bytes, for what is to be written. This size is
    //!   divided by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    os_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues os_Verify(tDevice* device, uint64_t lba, uint32_t range);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues os_Flush(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  io_Read()
    //
    //! \brief   Description:  This function sends the best possible read for the device's interface (ATA vs SCSI). This
    //! calls scsi_Read or ata_Read based on the interface type for the drive
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start reading at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send read verify
    //!   AND read in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to fill
    //!   in with read data \param dataSize - size of the buffer, in bytes, for what is to be read. This size is divided
    //!   by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    io_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  io_Write()
    //
    //! \brief   Description:  This function sends the best possible write for the device's interface (ATA vs SCSI).
    //! This calls scsi_Write or ata_Write based on the interface type for the drive
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start writing at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send write AND
    //!   read verify in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to
    //!   use for writing data \param dataSize - size of the buffer, in bytes, for what is to be written. This size is
    //!   divided by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    io_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Write()
    //
    //! \brief   Description:  This function attempts to send the best possible SCSI write command based on the LBA to
    //! write and transfer length (write 10 vs write 16)
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start writing at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send write AND
    //!   read verify in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to
    //!   use for writing data \param dataSize - size of the buffer, in bytes, for what is to be written. This size is
    //!   divided by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    scsi_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Read()
    //
    //! \brief   Description:  This function attempts to send the best possible SCSI write command based on the LBA to
    //! write and transfer length (read 10 vs read 16)
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start reading at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send read verify
    //!   AND read in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to use
    //!   for reading data \param dataSize - size of the buffer, in bytes, for what is to be read. This size is divided
    //!   by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    scsi_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Write()
    //
    //! \brief   Description:  This function attempts to send the best possible ATA write command based on the LBA to
    //! write and transfer length and DMA vs PIO modes supported
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start writing at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send write AND
    //!   read verify in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to
    //!   use for writing data \param dataSize - size of the buffer, in bytes, for what is to be written. This size is
    //!   divided by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  ata_Read()
    //
    //! \brief   Description:  This function attempts to send the best possible ATA write command based on the LBA to
    //! write and transfer length and DMA vs PIO modes supported
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param lba - the LBA you wish to start reading at
    //!   \param forceUnitAccess - set to true to bypass the cache and go directly to media (NOTE: May send read verify
    //!   AND read in some cases if FUA bit not supported in a command) \param ptrData - pointer to the data buf to use
    //!   for reading data \param dataSize - size of the buffer, in bytes, for what is to be read. This size is divided
    //!   by the device's logical sector size to get how many sectors to transfer.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues
    ata_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues scsi_Verify(tDevice* device, uint64_t lba, uint32_t range);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues nvme_Verify_LBA(tDevice* device, uint64_t lba, uint32_t range);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Read_Verify(tDevice* device, uint64_t lba, uint32_t range);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues ata_Flush_Cache_Command(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues scsi_Synchronize_Cache_Command(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues close_Zone(tDevice* device, bool closeAll, uint64_t zoneID, uint16_t zoneCount);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues finish_Zone(tDevice* device,
                                                    bool     finishAll,
                                                    uint64_t zoneID,
                                                    uint16_t zoneCount);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues open_Zone(tDevice* device, bool openAll, uint64_t zoneID, uint16_t zoneCount);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues reset_Write_Pointer(tDevice* device,
                                                            bool     resetAll,
                                                            uint64_t zoneID,
                                                            uint16_t zoneCount);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues report_Zones(tDevice*              device,
                                                     eZoneReportingOptions reportingOptions,
                                                     bool                  partial,
                                                     uint64_t              zoneLocator,
                                                     uint8_t*              ptrData,
                                                     uint32_t              dataSize);

#if defined(__cplusplus)
}
#endif
