// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file cmds.c   Implementation for generic ATA/SCSI functions
//                     The intention of the file is to be generic & not OS specific

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper_func.h"
#include "cmds.h"
#include "common_public.h"
#include "nvme_helper_func.h"
#include "platform_helper.h"
#include "scsi_helper_func.h"
#include "usb_hacks.h"

eReturnValues send_Sanitize_Block_Erase(tDevice* device, bool exitFailureMode, bool znr)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Sanitize_Block_Erase(device, exitFailureMode, znr);
        break;
    case NVME_DRIVE:
        ret = nvme_Sanitize(device, znr, false, 0, exitFailureMode, SANITIZE_NVM_BLOCK_ERASE, 0);
        break;
    case SCSI_DRIVE:
        ret = scsi_Sanitize_Block_Erase(device, exitFailureMode, true, znr);
        break;
    default:
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Current device type not supported yet\n");
        }
        break;
    }
    return ret;
}

eReturnValues send_Sanitize_Crypto_Erase(tDevice* device, bool exitFailureMode, bool znr)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Sanitize_Crypto_Scramble(device, exitFailureMode, znr);
        break;
    case NVME_DRIVE:
        ret = nvme_Sanitize(device, znr, false, 0, exitFailureMode, SANITIZE_NVM_CRYPTO, 0);
        break;
    case SCSI_DRIVE:
        ret = scsi_Sanitize_Cryptographic_Erase(device, exitFailureMode, true, znr);
        break;
    default:
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Current device type not supported yet\n");
        }
        break;
    }
    return ret;
}

eReturnValues send_Sanitize_Overwrite_Erase(tDevice* device,
                                            bool     exitFailureMode,
                                            bool     invertBetweenPasses,
                                            uint8_t  overwritePasses,
                                            uint8_t* pattern,
                                            uint16_t patternLength,
                                            bool     znr)
{
    eReturnValues ret          = UNKNOWN;
    bool          localPattern = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        uint32_t ataPattern = UINT32_C(0);
        if (pattern && patternLength >= 4)
        {
            ataPattern = M_BytesTo4ByteValue(pattern[3], pattern[2], pattern[1], pattern[0]);
        }
        // Note: ATA drives have a "definitive ending pattern bit"
        //       In order to be consistent with SCSI and NVMe specifications, this should be set when the
        //       Device supports it. Basically it means that the provided pattern WILL DEFINITELY be the
        //       pattern on the drive no matter how many passes or inversions happen.
        //       When this is not supported/set then the device may or may not be consistent with this behavior...it's
        //       up to the firmware to decide. Because of this, this bit will be set when it is discovered as supported
        //       whenever possible -TJE
        ret = ata_Sanitize_Overwrite_Erase(device, exitFailureMode, invertBetweenPasses, overwritePasses & 0x0F,
                                           ataPattern, znr,
                                           device->drive_info.ata_Options.sanitizeOverwriteDefinitiveEndingPattern);
    }
    break;
    case NVME_DRIVE:
    {
        uint32_t nvmPattern = UINT32_C(0);
        if (pattern && patternLength >= 4)
        {
            nvmPattern = M_BytesTo4ByteValue(pattern[3], pattern[2], pattern[1], pattern[0]);
        }
        ret = nvme_Sanitize(device, znr, invertBetweenPasses, overwritePasses, exitFailureMode, SANITIZE_NVM_OVERWRITE,
                            nvmPattern);
    }
    break;
    case SCSI_DRIVE:
        // overwrite passes set to 0 on scsi is reserved. This is being changed to the maximum for SCSI to mean 16
        // passes
        if ((overwritePasses & 0x0F) == 0)
        {
            overwritePasses = 0x1F;
        }
        // we need to allocate a zero pattern if none was provided. 4 bytes is enough for this (and could handle SAT
        // transaltion if necessary).
        if (pattern == M_NULLPTR)
        {
            localPattern = true;
            pattern      = M_REINTERPRET_CAST(uint8_t*, safe_calloc(4, sizeof(uint8_t)));
            if (pattern == M_NULLPTR)
            {
                return MEMORY_FAILURE;
            }
        }
        ret = scsi_Sanitize_Overwrite(device, exitFailureMode, znr, true, invertBetweenPasses,
                                      SANITIZE_OVERWRITE_NO_CHANGES, overwritePasses & 0x1F, pattern, patternLength);
        if (localPattern)
        {
            safe_free(&pattern);
            localPattern = false;
        }
        break;
    default:
        printf("Current device type not supported yet\n");
        break;
    }
    return ret;
}

eReturnValues send_Sanitize_Exit_Failure_Mode(tDevice* device)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Sanitize_Status(device, true);
        break;
    case NVME_DRIVE:
        ret = nvme_Sanitize(device, false, false, 0, false, SANITIZE_NVM_EXIT_FAILURE_MODE, 0);
        break;
    case SCSI_DRIVE:
        ret = scsi_Sanitize_Exit_Failure_Mode(device);
        break;
    default:
        printf("Current device type not supported yet\n");
        break;
    }
    return ret;
}

eReturnValues spin_down_drive(tDevice* device, bool sleepState)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (sleepState) // send sleep command
        {
            ret = ata_Sleep(device);
        }
        else // standby
        {
            ret = ata_Standby_Immediate(device);
        }
        break;
    case NVME_DRIVE:
        if (sleepState)
        {
            ret = NOT_SUPPORTED;
        }
        else
        {
            nvmeFeaturesCmdOpt standby;
            safe_memset(&standby, sizeof(nvmeFeaturesCmdOpt), 0, sizeof(nvmeFeaturesCmdOpt));
            standby.fid             = NVME_FEAT_POWER_MGMT_;
            standby.featSetGetValue = device->drive_info.IdentifyData.nvme.ctrl.npss;
            ret                     = nvme_Set_Features(device, &standby);
        }
        break;
    case SCSI_DRIVE:
        if (device->drive_info.scsiVersion > SCSI_VERSION_SCSI2)
        {
            if (sleepState)
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_SLEEP, false, false, false);
            }
            else
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_STANDBY, false, false, false);
            }
        }
        else
        {
            if (sleepState)
            {
                ret = NOT_SUPPORTED;
            }
            else
            {
                ret = scsi_Start_Stop_Unit(device, false, 0, PC_START_VALID, false, false, false);
            }
        }
        break;
    default:
        if (VERBOSITY_QUIET < device->deviceVerbosity)
        {
            printf("Spin down drive is not supported on this drive type at this time\n");
        }
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

//-----------------------------------------------------------------------------
//
//  fill_Drive_Info_Data()
//
//! \brief   Description:  Generic Function to get drive information data filled
//                         into the driveInfo_TYPE of the device structure.
//                         This function assumes the type & interface has already
//                         determined by the OS layer.
//  Entry:
//!   \param tDevice - pointer to the device structure
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
eReturnValues fill_Drive_Info_Data(tDevice* device)
{
    eReturnValues status = SUCCESS;
#ifdef _DEBUG
    printf("%s: -->\n", __FUNCTION__);
#endif
    DISABLE_NONNULL_COMPARE
    if (device != M_NULLPTR)
    {
        if (device->drive_info.interface_type == UNKNOWN_INTERFACE)
        {
            status = BAD_PARAMETER;
            return status;
        }
        switch (device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
            // We know this is an ATA interface and we SHOULD be able to send either an ATA or ATAPI identify...but that
            // doesn't work right, so if the OS layer told us it is ATAPI, do SCSI device discovery
            if (device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.drive_type == LEGACY_TAPE_DRIVE)
            {
                status = fill_In_Device_Info(device);
            }
            else
            {
                status = fill_In_ATA_Drive_Info(device);
                if (status == FAILURE || status == UNKNOWN)
                {
                    // printf("trying scsi discovery\n");
                    // could not enumerate as ATA, try SCSI in case it's taking CDBs at the low layer to communicate and
                    // not translating more than the A1 op-code to check it if's a SAT command.
                    status = fill_In_Device_Info(device);
                }
            }
            break;
        case IEEE_1394_INTERFACE:
        case USB_INTERFACE:
            // Previously there was separate function to fill in drive info for USB, but has now been combined with the
            // SCSI fill device info. Low-level code capable of figuring out hacks for working with these devices is now
            // able to preconfigure most flags
            status = fill_In_Device_Info(device);
            break;
        case NVME_INTERFACE:
            status = fill_In_NVMe_Device_Info(device);
            break;
        case RAID_INTERFACE:
            // if it's RAID interface, the low-level RAID code may already have set the drive type, so treat it based
            // off of what drive type is set to
            switch (device->drive_info.drive_type)
            {
            case ATA_DRIVE:
                status = fill_In_ATA_Drive_Info(device);
                break;
            case NVME_DRIVE:
                status = fill_In_NVMe_Device_Info(device);
                break;
            default:
                status = fill_In_Device_Info(device);
                break;
            }
            break;
        case SCSI_INTERFACE:
        default:
            // call this instead. It will handle issuing scsi commands and at the end will attempt an ATA Identify if
            // needed
            status = fill_In_Device_Info(device);
            break;
        }
    }
    else
    {
        status = BAD_PARAMETER;
    }
#ifdef _DEBUG
    if (device != M_NULLPTR)
    {
        printf("Drive type: %d\n", device->drive_info.drive_type);
        printf("Interface type: %d\n", device->drive_info.interface_type);
        printf("Media type: %d\n", device->drive_info.media_type);
    }
    printf("%s: <--\n", __FUNCTION__);
#endif
    RESTORE_NONNULL_COMPARE
    return status;
}

eReturnValues firmware_Download_Command(tDevice*      device,
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
                                        bool          forceDisableReset)
{
    eReturnValues ret = UNKNOWN;
#ifdef _DEBUG
    printf("-->%s\n", __FUNCTION__);
#endif
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        eDownloadMicrocodeFeatures ataDLMode = ATA_DL_MICROCODE_SAVE_IMMEDIATE; // default
        switch (dlMode)
        {
        case DL_FW_ACTIVATE:
            ataDLMode = ATA_DL_MICROCODE_ACTIVATE;
            break;
        case DL_FW_FULL:
            ataDLMode = ATA_DL_MICROCODE_SAVE_IMMEDIATE;
            break;
        case DL_FW_TEMP:
            ataDLMode = ATA_DL_MICROCODE_TEMPORARY_IMMEDIATE;
            break;
        case DL_FW_SEGMENTED:
            ataDLMode = ATA_DL_MICROCODE_OFFSETS_SAVE_IMMEDIATE;
            break;
        case DL_FW_DEFERRED:
            ataDLMode = ATA_DL_MICROCODE_OFFSETS_SAVE_FUTURE;
            break;
        case DL_FW_DEFERRED_SELECT_ACTIVATE:
        default:
            return BAD_PARAMETER;
        }
        // ret = ata_Download_Microcode(device, ataDLMode, xferLen / LEGACY_DRIVE_SEC_SIZE, offset /
        // LEGACY_DRIVE_SEC_SIZE, useDMA, ptrData, xferLen); Switching to this new function since it will automatically
        // try DMA mode if supported by the drive. If the controller or driver don't like issuing DMA mode, this will
        // detect it and retry the command with PIO mode.
        ret = send_ATA_Download_Microcode_Cmd(device, ataDLMode, C_CAST(uint16_t, xferLen / LEGACY_DRIVE_SEC_SIZE),
                                              C_CAST(uint16_t, offset / LEGACY_DRIVE_SEC_SIZE), ptrData, xferLen,
                                              firstSegment, lastSegment, timeoutSeconds);
    }
    break;
    case NVME_DRIVE:
    {
        switch (dlMode)
        {
        case DL_FW_ACTIVATE:
        {
            uint8_t statusCodeType   = UINT8_C(0);
            uint8_t statusCode       = UINT8_C(0);
            bool    doNotRetry       = false;
            bool    more             = false;
            bool    issueReset       = false;
            bool    subsystem        = false;
            uint8_t nvmeCommitAction = commitAction; // assume something is passed in for now
            if (!nvmeForceCA)
            {
                // user is not forcing a commit action so figure out what to use instead.
                if (device->drive_info.IdentifyData.nvme.ctrl.frmw & BIT4)
                {
                    // this activate action can be used for replacing or activating existing images if the controller
                    // supports it.
                    nvmeCommitAction = NVME_CA_ACTIVITE_IMMEDIATE;
                }
                else
                {
                    if (existingImage)
                    {
                        nvmeCommitAction = NVME_CA_ACTIVITE_ON_RST;
                    }
                    else
                    {
                        nvmeCommitAction = NVME_CA_REPLACE_ACTIVITE_ON_RST;
                    }
                }
            }
            ret = nvme_Firmware_Commit(device, nvmeCommitAction, slotNumber, timeoutSeconds);
            if (ret == SUCCESS &&
                (nvmeCommitAction == NVME_CA_REPLACE_ACTIVITE_ON_RST || nvmeCommitAction == NVME_CA_ACTIVITE_ON_RST) &&
                !forceDisableReset)
            {
                issueReset = true;
            }
            // Issue a reset if we need to!
            get_NVMe_Status_Fields_From_DWord(device->drive_info.lastNVMeResult.lastNVMeStatus, &doNotRetry, &more,
                                              &statusCodeType, &statusCode);
            if (statusCodeType == NVME_SCT_COMMAND_SPECIFIC_STATUS)
            {
                switch (statusCode)
                {
                case NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET:
                    issueReset = true;
                    subsystem  = true;
                    break;
                case NVME_CMD_SP_SC_FW_ACT_REQ_RESET:
                case NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET:
                    issueReset = true;
                    break;
                case NVME_CMD_SP_SC_FW_ACT_REQ_MAX_TIME_VIOALTION:
                    if (!nvmeForceCA) // only retry when not forcing a specific mode
                    {
                        // needs to be reissued for an activate on reset instead due to maximum time violation.
                        if (existingImage)
                        {
                            nvmeCommitAction = NVME_CA_ACTIVITE_ON_RST;
                        }
                        else
                        {
                            nvmeCommitAction = NVME_CA_REPLACE_ACTIVITE_ON_RST;
                        }
                        ret = nvme_Firmware_Commit(device, nvmeCommitAction, slotNumber, timeoutSeconds);
                        get_NVMe_Status_Fields_From_DWord(device->drive_info.lastNVMeResult.lastNVMeStatus, &doNotRetry,
                                                          &more, &statusCodeType, &statusCode);
                        if (statusCodeType == NVME_SCT_COMMAND_SPECIFIC_STATUS)
                        {
                            switch (statusCode)
                            {
                            case NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET:
                                if (!forceDisableReset)
                                {
                                    issueReset = true;
                                    subsystem  = true;
                                }
                                break;
                            case NVME_CMD_SP_SC_FW_ACT_REQ_RESET:
                            case NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET:
                                if (!forceDisableReset)
                                {
                                    issueReset = true;
                                }
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            if (issueReset && !forceDisableReset) // if the reset is being forced to not run, there is a good reason for
                                                  // it! Listen to this flag ALWAYS
            {
                // send an appropriate reset to the device to activate the firmware.
                // NOTE: On Windows, this is a stub since their API call will do this for us.
                if (subsystem)
                {
                    // subsystem reset
                    nvme_Subsystem_Reset(device);
                }
                else
                {
                    // reset
                    nvme_Reset(device);
                }
            }
            else if (nvmeCommitAction != NVME_CA_ACTIVITE_IMMEDIATE)
            {
                // Set this return code since the reset was bypassed.
                ret = POWER_CYCLE_REQUIRED;
            }
        }
        break;
        case DL_FW_DEFERRED:
            ret = nvme_Firmware_Image_Dl(device, offset, xferLen, ptrData, firstSegment, lastSegment, timeoutSeconds);
            break;
        case DL_FW_FULL:
        case DL_FW_TEMP:
        case DL_FW_SEGMENTED:
        case DL_FW_DEFERRED_SELECT_ACTIVATE:
        // none of these are supported
        default:
            ret = NOT_SUPPORTED;
            break;
        }
    }
    break;
    case SCSI_DRIVE:
    {
        eWriteBufferMode scsiDLMode = SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE; // default
        switch (dlMode)
        {
        case DL_FW_ACTIVATE:
            scsiDLMode = SCSI_WB_ACTIVATE_DEFERRED_MICROCODE;
            break;
        case DL_FW_FULL:
            scsiDLMode = SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE;
            break;
        case DL_FW_TEMP:
            scsiDLMode = SCSI_WB_DL_MICROCODE_TEMP_ACTIVATE;
            break;
        case DL_FW_SEGMENTED:
            scsiDLMode = SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_ACTIVATE;
            break;
        case DL_FW_DEFERRED:
            scsiDLMode = SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER;
            break;
        case DL_FW_DEFERRED_SELECT_ACTIVATE:
            scsiDLMode = SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_SELECT_ACTIVATE_DEFER;
            break;
        default:
            return BAD_PARAMETER;
        }
        ret = scsi_Write_Buffer(device, scsiDLMode, 0, slotNumber, offset, xferLen, ptrData, firstSegment, lastSegment,
                                timeoutSeconds);
    }
    break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n", __FUNCTION__, ret);
#endif
    return ret;
}

eReturnValues firmware_Download_Activate(tDevice* device,
                                         uint8_t  slotNumber,
                                         bool     existingImage,
                                         uint32_t timeoutSeconds,
                                         bool     nvmeForceCA,
                                         uint8_t  commitAction,
                                         bool     forceDisableReset)
{
    return firmware_Download_Command(device, DL_FW_ACTIVATE, 0, 0, M_NULLPTR, slotNumber, existingImage, false, false,
                                     timeoutSeconds, nvmeForceCA, commitAction, forceDisableReset);
}

eReturnValues security_Send(tDevice* device,
                            uint8_t  securityProtocol,
                            uint16_t securityProtocolSpecific,
                            uint8_t* ptrData,
                            uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        if (dataSize != 0)
        {
            uint8_t* tcgBufPtr      = ptrData;
            uint32_t tcgDataSize    = dataSize;
            bool     useLocalMemory = false;
            // make sure we are sending/recieving in sectors for ATA
            if (dataSize < LEGACY_DRIVE_SEC_SIZE || dataSize % LEGACY_DRIVE_SEC_SIZE != 0)
            {
                // round up to nearest 512byte sector
                tcgDataSize = uint32_round_up_power2(dataSize, 512);
                tcgBufPtr   = C_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(tcgDataSize), sizeof(uint8_t),
                                                                   device->os_info.minimumAlignment));
                if (tcgBufPtr == M_NULLPTR)
                {
                    return MEMORY_FAILURE;
                }
                // copy packet to new memory pointer before sending it
                safe_memcpy(tcgBufPtr, uint32_to_sizet(tcgDataSize), ptrData, uint32_to_sizet(dataSize));
                useLocalMemory = true;
            }
            // ret = ata_Trusted_Send(device, useDMA, securityProtocol, securityProtocolSpecific, ptrData, dataSize);
            // Switching to this new function since it will automatically try DMA mode if supported by the drive.
            // If the controller or driver don't like issuing DMA mode, this will detect it and retry the command with
            // PIO mode.
            ret = send_ATA_Trusted_Send_Cmd(device, securityProtocol, securityProtocolSpecific, tcgBufPtr, tcgDataSize);
            if (useLocalMemory)
            {
                safe_free_aligned(&tcgBufPtr);
            }
        }
        else
        {
            ret = ata_Trusted_Non_Data(device, securityProtocol, false, securityProtocolSpecific);
        }
    }
    break;
    case NVME_DRIVE:
        ret = nvme_Security_Send(device, securityProtocol, securityProtocolSpecific, 0, ptrData, dataSize);
        break;
    case SCSI_DRIVE:
    {
        // The inc512 bit is not allowed on NVMe drives when sent this command....we may want to remove setting it, but
        // for now we'll leave it here.
        bool inc512 = false;
        if ((dataSize >= LEGACY_DRIVE_SEC_SIZE && (dataSize % LEGACY_DRIVE_SEC_SIZE) == 0) &&
            ((device->drive_info.drive_type != NVME_DRIVE &&
              strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) != 0) ||
             device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512))
        {
            inc512 = true;
            dataSize /= LEGACY_DRIVE_SEC_SIZE;
        }
        ret = scsi_SecurityProtocol_Out(device, securityProtocol, securityProtocolSpecific, inc512, dataSize, ptrData,
                                        15);
    }
    break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues security_Receive(tDevice* device,
                               uint8_t  securityProtocol,
                               uint16_t securityProtocolSpecific,
                               uint8_t* ptrData,
                               uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        if (dataSize != 0)
        {
            uint8_t* tcgBufPtr      = ptrData;
            uint32_t tcgDataSize    = dataSize;
            bool     useLocalMemory = false;
            // make sure we are sending/receiving in sectors for ATA
            if (dataSize < LEGACY_DRIVE_SEC_SIZE || dataSize % LEGACY_DRIVE_SEC_SIZE != 0)
            {
                // round up to nearest 512byte sector
                tcgDataSize = uint32_round_up_power2(dataSize, 512);
                tcgBufPtr   = C_CAST(uint8_t*, safe_calloc_aligned(uint32_to_sizet(tcgDataSize), sizeof(uint8_t),
                                                                   device->os_info.minimumAlignment));
                if (tcgBufPtr == M_NULLPTR)
                {
                    return MEMORY_FAILURE;
                }
                useLocalMemory = true;
            }
            // ret = ata_Trusted_Receive(device, useDMA, securityProtocol, securityProtocolSpecific, tcgBufPtr,
            // tcgDataSize); Switching to this new function since it will automatically try DMA mode if supported by the
            // drive. If the controller or driver don't like issuing DMA mode, this will detect it and retry the command
            // with PIO mode.
            ret = send_ATA_Trusted_Receive_Cmd(device, securityProtocol, securityProtocolSpecific, tcgBufPtr,
                                               tcgDataSize);
            if (useLocalMemory)
            {
                safe_memcpy(ptrData, dataSize, tcgBufPtr, uint32_to_sizet(M_Min(dataSize, tcgDataSize)));
                safe_free_aligned(&tcgBufPtr);
            }
        }
        else
        {
            ret = ata_Trusted_Non_Data(device, securityProtocol, true, securityProtocolSpecific);
        }
    }
    break;
    case NVME_DRIVE:
        ret = nvme_Security_Receive(device, securityProtocol, securityProtocolSpecific, 0, ptrData, dataSize);
        break;
    case SCSI_DRIVE:
    {
        // The inc512 bit is not allowed on NVMe drives when sent this command....we may want to remove setting it, but
        // for now we'll leave it here.
        bool inc512 = false;
        if ((dataSize >= LEGACY_DRIVE_SEC_SIZE && (dataSize % LEGACY_DRIVE_SEC_SIZE) == 0) &&
            ((device->drive_info.drive_type != NVME_DRIVE &&
              strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) != 0) ||
             device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512))
        {
            inc512 = true;
            dataSize /= LEGACY_DRIVE_SEC_SIZE;
        }
        ret = scsi_SecurityProtocol_In(device, securityProtocol, securityProtocolSpecific, inc512, dataSize, ptrData);
    }
    break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues write_Same(tDevice* device, uint64_t startingLba, uint64_t numberOfLogicalBlocks, uint8_t* pattern)
{
    eReturnValues ret            = UNKNOWN;
    bool          noDataTransfer = false;
    if (pattern == M_NULLPTR)
    {
        noDataTransfer = true;
    }
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word206)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT2)
        {
            if (noDataTransfer)
            {
                DECLARE_ZERO_INIT_ARRAY(uint8_t, zeroPattern, 4);
                // ret = ata_SCT_Write_Same(device, useGPL, useDMA, WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD,
                // startingLba, numberOfLogicalBlocks, zeroPattern, SIZE_OF_STACK_ARRAY(zeroPattern));
                ret = send_ATA_SCT_Write_Same(device, WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD, startingLba,
                                              numberOfLogicalBlocks, zeroPattern, SIZE_OF_STACK_ARRAY(zeroPattern));
            }
            else
            {
                // ret = ata_SCT_Write_Same(device, useGPL, useDMA, WRITE_SAME_BACKGROUND_USE_SINGLE_LOGICAL_SECTOR,
                // startingLba, numberOfLogicalBlocks, pattern, 1);
                ret = send_ATA_SCT_Write_Same(device, WRITE_SAME_BACKGROUND_USE_SINGLE_LOGICAL_SECTOR, startingLba,
                                              numberOfLogicalBlocks, pattern, 1);
            }
        }
        else if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word080)) &&
                  (le16_to_host(device->drive_info.IdentifyData.ata.Word080) & BIT1 ||
                   le16_to_host(device->drive_info.IdentifyData.ata.Word080) &
                       BIT2)) && /*check for ATA or ATA-2 support*/
                 (!(is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word053)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word053) &
                        BIT1) /* this is a validity bit for field 69 */
                  && (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word069)) &&
                      (le16_to_host(device->drive_info.IdentifyData.ata.Word069) &
                       BIT11)))) // Legacy Write same uses same op-code as read buffer DMA, so that command cannot be
                                 // supported or the drive won't do the right thing
        {
            bool    localPattern     = false;
            bool    performWriteSame = false;
            uint8_t feature          = LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS;
            if (noDataTransfer)
            {
                pattern      = M_REINTERPRET_CAST(uint8_t*,
                                                  safe_calloc_aligned(device->drive_info.deviceBlockSize, sizeof(uint8_t),
                                                                      device->os_info.minimumAlignment));
                localPattern = true;
            }
            // Check range to see which feature to use
            if (startingLba == 0 && numberOfLogicalBlocks == (device->drive_info.deviceMaxLba + 1))
            {
                feature               = LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS;
                numberOfLogicalBlocks = 0;
                performWriteSame      = true;
            }
            else if (numberOfLogicalBlocks < UINT8_MAX)
            {
                performWriteSame = true;
            }
            if (performWriteSame)
            {
                if (device->drive_info.ata_Options.chsModeOnly)
                {
                    uint16_t cylinder = UINT16_C(0);
                    uint8_t  head     = UINT8_C(0);
                    uint8_t  sector   = UINT8_C(0);
                    if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, startingLba), &cylinder, &head, &sector))
                    {
                        ret =
                            ata_Legacy_Write_Same_CHS(device, feature, C_CAST(uint8_t, numberOfLogicalBlocks), cylinder,
                                                      head, sector, pattern, device->drive_info.deviceBlockSize);
                    }
                    else
                    {
                        ret = NOT_SUPPORTED;
                    }
                }
                else
                {
                    ret = ata_Legacy_Write_Same(device, feature, C_CAST(uint8_t, numberOfLogicalBlocks),
                                                C_CAST(uint32_t, startingLba), pattern,
                                                device->drive_info.deviceBlockSize);
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
            if (localPattern)
            {
                safe_free_aligned(&pattern);
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
        break;
    case SCSI_DRIVE:
        // todo: if there is no data transfer and the drive doesn't support that feature, we need to allocate local
        // zeroed memory to send as the pattern
        if (device->drive_info.scsiVersion > SCSI_VERSION_SPC &&
            (device->drive_info.deviceMaxLba > SCSI_MAX_32_LBA || numberOfLogicalBlocks > UINT16_MAX))
        {
            // write same 16 was made in SBC2 so need to report conformance to version greater than SPC (3) to do this.
            if (numberOfLogicalBlocks > UINT32_MAX)
            {
                ret = NOT_SUPPORTED;
            }
            else
            {
                ret = scsi_Write_Same_16(device, 0, false, false, noDataTransfer, startingLba, 0,
                                         C_CAST(uint32_t, numberOfLogicalBlocks), pattern,
                                         device->drive_info.deviceBlockSize);
            }
        }
        else
        {
            if (numberOfLogicalBlocks > UINT16_MAX)
            {
                ret = NOT_SUPPORTED;
            }
            else
            {
                ret = scsi_Write_Same_10(device, 0, false, false, C_CAST(uint32_t, startingLba), 0,
                                         C_CAST(uint16_t, numberOfLogicalBlocks), pattern,
                                         device->drive_info.deviceBlockSize);
            }
        }
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

bool is_Write_Psuedo_Uncorrectable_Supported(tDevice* device)
{
    bool supported = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (device->drive_info.ata_Options.writeUncorrectableExtSupported)
        {
            supported = true;
        }
        break;
    case NVME_DRIVE:
        supported = false;
        break;
    case SCSI_DRIVE:
    {
        // check for wu_supp in extended inquiry vpd page (SPC4+) since this matches when it was added to SBC3
        DECLARE_ZERO_INIT_ARRAY(uint8_t, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN);
        if (SUCCESS ==
            scsi_Inquiry(device, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
        {
            if (extendedInquiryData[6] & BIT3)
            {
                supported = true;
            }
        }
    }
    break;
    default:
        break;
    }
    return supported;
}

eReturnValues write_Psuedo_Uncorrectable_Error(tDevice* device, uint64_t corruptLBA)
{
    eReturnValues ret                        = UNKNOWN;
    bool          multipleLogicalPerPhysical = false; // used to set the physical block bit when applicable
    uint16_t      logicalPerPhysicalBlocks =
        C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    if (logicalPerPhysicalBlocks > 1)
    {
        // since this device has multiple logical blocks per physical block, we also need to adjust the LBA to be at the
        // start of the physical block do this by dividing by the number of logical sectors per physical sector. This
        // integer division will get us aligned
        uint64_t tempLBA = corruptLBA / logicalPerPhysicalBlocks;
        tempLBA *= logicalPerPhysicalBlocks;
        // do we need to adjust for alignment? We'll add it in later if I ever get a drive that has an alignment other
        // than 0 - TJE
        corruptLBA = tempLBA;
        // set this flag for SCSI
        multipleLogicalPerPhysical = true;
    }
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (device->drive_info.ata_Options.writeUncorrectableExtSupported)
        {
            ret = ata_Write_Uncorrectable(device, WRITE_UNCORRECTABLE_PSEUDO_UNCORRECTABLE_WITH_LOGGING,
                                          logicalPerPhysicalBlocks, corruptLBA);
        }
        else // write psuedo uncorrectable command is not supported by this drive. Return NOT_SUPPORTED
        {
            ret = NOT_SUPPORTED;
        }
        break;
    case NVME_DRIVE:
        ret = NOT_SUPPORTED;
        break;
    case SCSI_DRIVE:
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            ret = scsi_Write_Long_16(device, false, true, multipleLogicalPerPhysical, corruptLBA, 0, M_NULLPTR);
        }
        else
        {
            ret = scsi_Write_Long_10(device, false, true, multipleLogicalPerPhysical, C_CAST(uint32_t, corruptLBA), 0,
                                     M_NULLPTR);
        }
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

bool is_Write_Flagged_Uncorrectable_Supported(tDevice* device)
{
    bool supported = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (device->drive_info.ata_Options.writeUncorrectableExtSupported)
        {
            supported = true;
        }
        break;
    case NVME_DRIVE:
        if (le16_to_host(device->drive_info.IdentifyData.nvme.ctrl.oncs) & BIT1)
        {
            supported = true;
        }
        break;
    case SCSI_DRIVE:
    {
        // check for wu_supp in extended inquiry vpd page (SPC4+) since this matches when it was added to SBC3
        DECLARE_ZERO_INIT_ARRAY(uint8_t, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN);
        if (SUCCESS ==
            scsi_Inquiry(device, extendedInquiryData, VPD_EXTENDED_INQUIRY_LEN, EXTENDED_INQUIRY_DATA, true, false))
        {
            if (extendedInquiryData[6] & BIT2)
            {
                supported = true;
            }
        }
    }
    break;
    default:
        break;
    }
    return supported;
}

eReturnValues write_Flagged_Uncorrectable_Error(tDevice* device, uint64_t corruptLBA)
{
    eReturnValues ret = UNKNOWN;
    // This will only flag individual logical blocks
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (device->drive_info.ata_Options.writeUncorrectableExtSupported)
        {
            ret = ata_Write_Uncorrectable(device, WRITE_UNCORRECTABLE_FLAGGED_WITHOUT_LOGGING, 1, corruptLBA);
        }
        else
        {
            // use SCT read/write long
            ret = NOT_SUPPORTED; // we'll add this in later-TJE
        }
        break;
    case NVME_DRIVE:
        ret = nvme_Write_Uncorrectable(device, corruptLBA, 0); // 0 means 1 LBA since this is a zeros based value
        break;
    case SCSI_DRIVE:
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            ret = scsi_Write_Long_16(device, true, true, false, corruptLBA, 0, M_NULLPTR);
        }
        else
        {
            ret = scsi_Write_Long_10(device, true, true, false, C_CAST(uint32_t, corruptLBA), 0, M_NULLPTR);
        }
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues ata_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret     = SUCCESS; // assume success
    uint32_t      sectors = UINT32_C(0);
    // make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (forceUnitAccess)
    {
        // The synchronous commands in here are not able to set a bit for this, so the closest thing is to issue a
        // read-verify to force cached data to the media ahead of the read
        ret = ata_Read_Verify(device, lba, dataSize / device->drive_info.deviceBlockSize);
    }
    if (SUCCESS == ret) // don't try the read if the read verify fails
    {
        if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
        {
            // use 48bit commands by default
            if (sectors > 65536)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                if (sectors == 65536) // this is represented in the command with sector count set to 0
                {
                    sectors = 0;
                }
                // make sure the LBA is within range
                if (lba > MAX_48_BIT_LBA)
                {
                    ret = BAD_PARAMETER;
                }
                else
                {
                    if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                    {
                        // use PIO commands
                        // check if read multiple is supported (current # logical sectors per DRQ data block)
                        // Also, only bother with read multiple if it's a PATA drive. There isn't really an advantage to
                        // this on SATA other than backwards compatibility.
                        if (!device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands &&
                            device->drive_info.ata_Options.readWriteMultipleSupported &&
                            device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock > 0 &&
                            device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock <=
                                ATA_MAX_BLOCKS_PER_DRQ_DATA_BLOCKS &&
                            device->drive_info.ata_Options.isParallelTransport)
                        {
                            // read multiple supported and drive is currently configured in a mode that will work.
                            if (device->drive_info.ata_Options.chsModeOnly)
                            {
                                uint16_t cylinder = UINT16_C(0);
                                uint8_t  head     = UINT8_C(0);
                                uint8_t  sector   = UINT8_C(0);
                                if (SUCCESS ==
                                    convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                                {
                                    ret = ata_Legacy_Read_Multiple_CHS(device, cylinder, head, sector, ptrData,
                                                                       C_CAST(uint16_t, sectors), dataSize, true);
                                }
                                else // Couldn't convert or the LBA is greater than the current CHS mode
                                {
                                    ret = NOT_SUPPORTED;
                                }
                            }
                            else
                            {
                                ret =
                                    ata_Read_Multiple(device, lba, ptrData, C_CAST(uint16_t, sectors), dataSize, true);
                            }
                        }
                        else
                        {
                            if (device->drive_info.ata_Options.chsModeOnly)
                            {
                                uint16_t cylinder = UINT16_C(0);
                                uint8_t  head     = UINT8_C(0);
                                uint8_t  sector   = UINT8_C(0);
                                if (SUCCESS ==
                                    convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                                {
                                    ret = ata_Legacy_Read_Sectors_CHS(device, cylinder, head, sector, ptrData,
                                                                      C_CAST(uint16_t, sectors), dataSize, true);
                                }
                                else // Couldn't convert or the LBA is greater than the current CHS mode
                                {
                                    ret = NOT_SUPPORTED;
                                }
                            }
                            else
                            {
                                ret = ata_Read_Sectors(device, lba, ptrData, C_CAST(uint16_t, sectors), dataSize, true);
                            }
                        }
                    }
                    else
                    {
                        if (device->drive_info.ata_Options.chsModeOnly)
                        {
                            uint16_t cylinder = UINT16_C(0);
                            uint8_t  head     = UINT8_C(0);
                            uint8_t  sector   = UINT8_C(0);
                            if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                            {
                                ret = ata_Legacy_Read_DMA_CHS(device, cylinder, head, sector, ptrData,
                                                              C_CAST(uint16_t, sectors), dataSize, true);
                            }
                            else // Couldn't convert or the LBA is greater than the current CHS mode
                            {
                                ret = NOT_SUPPORTED;
                            }
                        }
                        else
                        {
                            // use DMA commands
                            ret = ata_Read_DMA(device, lba, ptrData, C_CAST(uint16_t, sectors), dataSize, true);
                        }
                        if (ret != SUCCESS)
                        {
                            // check the sense data. Make sure we didn't get told we have an invalid field in the CDB.
                            // If we do, try turning off DMA mode and retrying with PIO mode commands.
                            uint8_t senseKey = UINT8_C(0);
                            uint8_t asc      = UINT8_C(0);
                            uint8_t ascq     = UINT8_C(0);
                            uint8_t fru      = UINT8_C(0);
                            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN,
                                                       &senseKey, &asc, &ascq, &fru);
                            // Checking for illegal request, invalid field in CDB since this is what we've seen reported
                            // when DMA commands are not supported.
                            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                            {
                                // turn off DMA mode
                                eATASynchronousDMAMode currentDMAMode = device->drive_info.ata_Options.dmaMode;
                                device->drive_info.ata_Options.dmaMode =
                                    ATA_DMA_MODE_NO_DMA; // turning off DMA to try PIO mode
                                // recursively call this function to retry in PIO mode.
                                ret = ata_Read(device, lba, forceUnitAccess, ptrData, dataSize);
                                if (ret != SUCCESS)
                                {
                                    // this means that the error is not related to DMA mode command, so we can turn that
                                    // back on and pass up the return status.
                                    device->drive_info.ata_Options.dmaMode = currentDMAMode;
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // use the 28bit commands...first check that they aren't requesting more data than can be transferred in a
            // 28bit command, exception being 256 since that can be represented by a 0
            if (sectors > 256)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                if (sectors == 256)
                {
                    sectors = 0;
                }
                // make sure the LBA is within range
                if (lba > MAX_28_BIT_LBA)
                {
                    ret = BAD_PARAMETER;
                }
                else
                {
                    if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                    {
                        // use PIO commands
                        // check if read multiple is supported (current # logical sectors per DRQ data block)
                        // Also, only bother with read multiple if it's a PATA drive. There isn't really an advantage to
                        // this on SATA other than backwards compatibility.
                        if (!device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands &&
                            device->drive_info.ata_Options.readWriteMultipleSupported &&
                            device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock > 0 &&
                            device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock <=
                                ATA_MAX_BLOCKS_PER_DRQ_DATA_BLOCKS &&
                            device->drive_info.ata_Options.isParallelTransport)
                        {
                            // read multiple supported and drive is currently configured in a mode that will work.
                            if (device->drive_info.ata_Options.chsModeOnly)
                            {
                                uint16_t cylinder = UINT16_C(0);
                                uint8_t  head     = UINT8_C(0);
                                uint8_t  sector   = UINT8_C(0);
                                if (SUCCESS ==
                                    convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                                {
                                    ret = ata_Legacy_Read_Multiple_CHS(device, cylinder, head, sector, ptrData,
                                                                       C_CAST(uint16_t, sectors), dataSize, false);
                                }
                                else // Couldn't convert or the LBA is greater than the current CHS mode
                                {
                                    ret = NOT_SUPPORTED;
                                }
                            }
                            else
                            {
                                ret =
                                    ata_Read_Multiple(device, lba, ptrData, C_CAST(uint16_t, sectors), dataSize, false);
                            }
                        }
                        else
                        {
                            if (device->drive_info.ata_Options.chsModeOnly)
                            {
                                uint16_t cylinder = UINT16_C(0);
                                uint8_t  head     = UINT8_C(0);
                                uint8_t  sector   = UINT8_C(0);
                                if (SUCCESS ==
                                    convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                                {
                                    ret = ata_Legacy_Read_Sectors_CHS(device, cylinder, head, sector, ptrData,
                                                                      C_CAST(uint16_t, sectors), dataSize, false);
                                }
                                else // Couldn't convert or the LBA is greater than the current CHS mode
                                {
                                    ret = NOT_SUPPORTED;
                                }
                            }
                            else
                            {
                                ret =
                                    ata_Read_Sectors(device, lba, ptrData, C_CAST(uint16_t, sectors), dataSize, false);
                            }
                        }
                    }
                    else
                    {
                        if (device->drive_info.ata_Options.chsModeOnly)
                        {
                            uint16_t cylinder = UINT16_C(0);
                            uint8_t  head     = UINT8_C(0);
                            uint8_t  sector   = UINT8_C(0);
                            if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                            {
                                ret = ata_Legacy_Read_DMA_CHS(device, cylinder, head, sector, ptrData,
                                                              C_CAST(uint16_t, sectors), dataSize, false);
                            }
                            else // Couldn't convert or the LBA is greater than the current CHS mode
                            {
                                ret = NOT_SUPPORTED;
                            }
                        }
                        else
                        {
                            // use DMA commands
                            ret = ata_Read_DMA(device, lba, ptrData, C_CAST(uint16_t, sectors), dataSize, false);
                        }
                        if (ret != SUCCESS)
                        {
                            // check the sense data. Make sure we didn't get told we have an invalid field in the CDB.
                            // If we do, try turning off DMA mode and retrying with PIO mode commands.
                            uint8_t senseKey = UINT8_C(0);
                            uint8_t asc      = UINT8_C(0);
                            uint8_t ascq     = UINT8_C(0);
                            uint8_t fru      = UINT8_C(0);
                            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN,
                                                       &senseKey, &asc, &ascq, &fru);
                            // Checking for illegal request, invalid field in CDB since this is what we've seen reported
                            // when DMA commands are not supported.
                            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                            {
                                // turn off DMA mode
                                eATASynchronousDMAMode currentDMAMode = device->drive_info.ata_Options.dmaMode;
                                device->drive_info.ata_Options.dmaMode =
                                    ATA_DMA_MODE_NO_DMA; // turning off DMA to try PIO mode
                                // recursively call this function to retry in PIO mode.
                                ret = ata_Read(device, lba, forceUnitAccess, ptrData, dataSize);
                                if (ret != SUCCESS)
                                {
                                    // this means that the error is not related to DMA mode command, so we can turn that
                                    // back on and pass up the return status.
                                    device->drive_info.ata_Options.dmaMode = currentDMAMode;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return ret;
}

eReturnValues ata_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret         = SUCCESS; // assume success
    uint32_t      sectors     = UINT32_C(0);
    bool          writeDMAFUA = false;
    // make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;

    if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
    {
        // use 48bit commands by default
        if (sectors > 65536)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            if (sectors == 65536) // this is represented in the command with sector count set to 0
            {
                sectors = 0;
            }
            // make sure the LBA is within range
            if (lba > MAX_48_BIT_LBA)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                {
                    // use PIO commands
                    // check if read multiple is supported (current # logical sectors per DRQ data block)
                    // Also, only bother with write multiple if it's a PATA drive. There isn't really an advantage to
                    // this on SATA other than backwards compatibility.
                    if (!device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands &&
                        device->drive_info.ata_Options.readWriteMultipleSupported &&
                        device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock > 0 &&
                        device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock <=
                            ATA_MAX_BLOCKS_PER_DRQ_DATA_BLOCKS &&
                        device->drive_info.ata_Options.isParallelTransport)
                    {
                        // read multiple supported and drive is currently configured in a mode that will work.
                        if (device->drive_info.ata_Options.chsModeOnly)
                        {
                            uint16_t cylinder = UINT16_C(0);
                            uint8_t  head     = UINT8_C(0);
                            uint8_t  sector   = UINT8_C(0);
                            if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                            {
                                ret = ata_Legacy_Write_Multiple_CHS(device, cylinder, head, sector, ptrData, dataSize,
                                                                    true, false);
                            }
                            else // Couldn't convert or the LBA is greater than the current CHS mode
                            {
                                ret = NOT_SUPPORTED;
                            }
                        }
                        else
                        {
                            ret = ata_Write_Multiple(device, lba, ptrData, dataSize, true, false);
                        }
                    }
                    else
                    {
                        if (device->drive_info.ata_Options.chsModeOnly)
                        {
                            uint16_t cylinder = UINT16_C(0);
                            uint8_t  head     = UINT8_C(0);
                            uint8_t  sector   = UINT8_C(0);
                            if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                            {
                                ret = ata_Legacy_Write_Sectors_CHS(device, cylinder, head, sector, ptrData, dataSize,
                                                                   true);
                            }
                            else // Couldn't convert or the LBA is greater than the current CHS mode
                            {
                                ret = NOT_SUPPORTED;
                            }
                        }
                        else
                        {
                            ret = ata_Write_Sectors(device, lba, ptrData, dataSize, true);
                        }
                    }
                }
                else
                {
                    // use DMA commands
                    if (device->drive_info.ata_Options.chsModeOnly)
                    {
                        uint16_t cylinder = UINT16_C(0);
                        uint8_t  head     = UINT8_C(0);
                        uint8_t  sector   = UINT8_C(0);
                        if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                        {
                            ret = ata_Legacy_Write_DMA_CHS(device, cylinder, head, sector, ptrData, dataSize, true,
                                                           false);
                        }
                        else // Couldn't convert or the LBA is greater than the current CHS mode
                        {
                            ret = NOT_SUPPORTED;
                        }
                    }
                    else
                    {
                        // word84 bit6 or word87 bit6
                        if (forceUnitAccess && ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                                                     device->drive_info.IdentifyData.ata.Word084) &&
                                                 le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT6) ||
                                                (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                                                     device->drive_info.IdentifyData.ata.Word087) &&
                                                 le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT6)))
                        {
                            writeDMAFUA = true;
                        }
                        ret = ata_Write_DMA(device, lba, ptrData, dataSize, true, writeDMAFUA);
                    }
                    if (ret != SUCCESS)
                    {
                        // check the sense data. Make sure we didn't get told we have an invalid field in the CDB.
                        // If we do, try turning off DMA mode and retrying with PIO mode commands.
                        uint8_t senseKey = UINT8_C(0);
                        uint8_t asc      = UINT8_C(0);
                        uint8_t ascq     = UINT8_C(0);
                        uint8_t fru      = UINT8_C(0);
                        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey,
                                                   &asc, &ascq, &fru);
                        // Checking for illegal request, invalid field in CDB since this is what we've seen reported
                        // when DMA commands are not supported.
                        if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                        {
                            // turn off DMA mode
                            eATASynchronousDMAMode currentDMAMode = device->drive_info.ata_Options.dmaMode;
                            device->drive_info.ata_Options.dmaMode =
                                ATA_DMA_MODE_NO_DMA; // turning off DMA to try PIO mode
                            // recursively call this function to retry in PIO mode.
                            ret = ata_Write(device, lba, forceUnitAccess, ptrData, dataSize);
                            if (ret != SUCCESS)
                            {
                                // this means that the error is not related to DMA mode command, so we can turn that
                                // back on and pass up the return status.
                                device->drive_info.ata_Options.dmaMode = currentDMAMode;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        // use the 28bit commands...first check that they aren't requesting more data than can be transferred in a 28bit
        // command, exception being 256 since that can be represented by a 0
        if (sectors > 256)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            if (sectors == 256)
            {
                sectors = 0;
            }
            // make sure the LBA is within range
            if (lba > MAX_28_BIT_LBA)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                {
                    // use PIO commands
                    // check if read multiple is supported (current # logical sectors per DRQ data block)
                    // Also, only bother with write multiple if it's a PATA drive. There isn't really an advantage to
                    // this on SATA other than backwards compatibility.
                    if (!device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands &&
                        device->drive_info.ata_Options.readWriteMultipleSupported &&
                        device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock > 0 &&
                        device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock <=
                            ATA_MAX_BLOCKS_PER_DRQ_DATA_BLOCKS &&
                        device->drive_info.ata_Options.isParallelTransport)
                    {
                        // read multiple supported and drive is currently configured in a mode that will work.
                        if (device->drive_info.ata_Options.chsModeOnly)
                        {
                            uint16_t cylinder = UINT16_C(0);
                            uint8_t  head     = UINT8_C(0);
                            uint8_t  sector   = UINT8_C(0);
                            if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                            {
                                ret = ata_Legacy_Write_Multiple_CHS(device, cylinder, head, sector, ptrData, dataSize,
                                                                    false, false);
                            }
                            else // Couldn't convert or the LBA is greater than the current CHS mode
                            {
                                ret = NOT_SUPPORTED;
                            }
                        }
                        else
                        {
                            ret = ata_Write_Multiple(device, lba, ptrData, dataSize, false, false);
                        }
                    }
                    else
                    {
                        if (device->drive_info.ata_Options.chsModeOnly)
                        {
                            uint16_t cylinder = UINT16_C(0);
                            uint8_t  head     = UINT8_C(0);
                            uint8_t  sector   = UINT8_C(0);
                            if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                            {
                                ret = ata_Legacy_Write_Sectors_CHS(device, cylinder, head, sector, ptrData, dataSize,
                                                                   false);
                            }
                            else // Couldn't convert or the LBA is greater than the current CHS mode
                            {
                                ret = NOT_SUPPORTED;
                            }
                        }
                        else
                        {
                            ret = ata_Write_Sectors(device, lba, ptrData, dataSize, false);
                        }
                    }
                }
                else
                {
                    // use DMA commands
                    if (device->drive_info.ata_Options.chsModeOnly)
                    {
                        uint16_t cylinder = UINT16_C(0);
                        uint8_t  head     = UINT8_C(0);
                        uint8_t  sector   = UINT8_C(0);
                        if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                        {
                            ret = ata_Legacy_Write_DMA_CHS(device, cylinder, head, sector, ptrData, dataSize, false,
                                                           false);
                        }
                        else // Couldn't convert or the LBA is greater than the current CHS mode
                        {
                            ret = NOT_SUPPORTED;
                        }
                    }
                    else
                    {
                        ret = ata_Write_DMA(device, lba, ptrData, dataSize, false, false);
                    }
                    if (ret != SUCCESS)
                    {
                        // check the sense data. Make sure we didn't get told we have an invalid field in the CDB.
                        // If we do, try turning off DMA mode and retrying with PIO mode commands.
                        uint8_t senseKey = UINT8_C(0);
                        uint8_t asc      = UINT8_C(0);
                        uint8_t ascq     = UINT8_C(0);
                        uint8_t fru      = UINT8_C(0);
                        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey,
                                                   &asc, &ascq, &fru);
                        // Checking for illegal request, invalid field in CDB since this is what we've seen reported
                        // when DMA commands are not supported.
                        if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                        {
                            // turn off DMA mode
                            eATASynchronousDMAMode currentDMAMode = device->drive_info.ata_Options.dmaMode;
                            device->drive_info.ata_Options.dmaMode =
                                ATA_DMA_MODE_NO_DMA; // turning off DMA to try PIO mode
                            // recursively call this function to retry in PIO mode.
                            ret = ata_Write(device, lba, forceUnitAccess, ptrData, dataSize);
                            if (ret != SUCCESS)
                            {
                                // this means that the error is not related to DMA mode command, so we can turn that
                                // back on and pass up the return status.
                                device->drive_info.ata_Options.dmaMode = currentDMAMode;
                            }
                        }
                    }
                }
            }
        }
    }
    if (forceUnitAccess && !writeDMAFUA && SUCCESS == ret)
    {
        // The synchronous commands in here are not able to set a bit for this, so the closest thing is to issue a
        // read-verify to force cached data to the media ahead of the read
        ret = ata_Read_Verify(device, lba, dataSize / device->drive_info.deviceBlockSize);
    }
    return ret;
}

static void get_SCSI_DPO_FUA_Support(tDevice* device)
{
    if (!device->drive_info.dpoFUAvalid)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, cachingMP, MODE_PARAMETER_HEADER_10_LEN + MP_CACHING_LEN);
        if (device->drive_info.scsiVersion >= SCSI_VERSION_SCSI2 &&
            SUCCESS == scsi_Mode_Sense_10(device, MP_CACHING, MODE_PARAMETER_HEADER_10_LEN + MP_CACHING_LEN, 0, true,
                                          false, MPC_CURRENT_VALUES, cachingMP))
        {
            // dpo/fua bit is in the device type specific parameter of the header
            // byte 3, bit 4
            device->drive_info.dpoFUA = M_ToBool(cachingMP[3] & BIT4);
        }
        device->drive_info.dpoFUAvalid = true;
    }
}

eReturnValues scsi_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret     = SUCCESS; // assume success
    uint32_t      sectors = UINT32_C(0);
    bool          fua     = false;
    // make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (forceUnitAccess)
    {
        get_SCSI_DPO_FUA_Support(device);
        if (!device->drive_info.dpoFUA)
        {
            // send verify first since FUA is not available
            ret = scsi_Verify(device, lba, dataSize / device->drive_info.deviceBlockSize);
        }
        fua = device->drive_info.dpoFUA;
    }
    if (SUCCESS == ret)
    {
        if (device->drive_info.passThroughHacks.scsiHacks.readWrite.available)
        {
            // This device is in the database or the command support has been determined some other way to allow us to
            // issue a correct command without any other issues.
            if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16)
            {
                ret = scsi_Read_16(device, 0, false, fua, false, lba, 0, sectors, ptrData, dataSize);
            }
            else if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12)
            {
                ret = scsi_Read_12(device, 0, false, fua, false, C_CAST(uint32_t, lba), 0, sectors, ptrData, dataSize);
            }
            else if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10)
            {
                ret = scsi_Read_10(device, 0, false, fua, false, C_CAST(uint32_t, lba), 0, C_CAST(uint16_t, sectors),
                                   ptrData, dataSize);
            }
            else if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6)
            {
                ret = scsi_Read_6(device, C_CAST(uint32_t, lba), C_CAST(uint8_t, sectors), ptrData, dataSize);
            }
            else
            {
                // This shouldn't happen...
                ret = BAD_PARAMETER;
            }
        }
        else // Use the generic rules below to issue what will most likely be the correct commands...-TJE
        {
            if (device->drive_info.scsiVersion >=
                SCSI_VERSION_SPC_3) // SBC2 introduced read 16 command, so checking for SPC3
            {
                // there's no real way to tell when scsi drive supports read 10 vs read 16 (which are all we will care
                // about in here), so just based on transfer length and the maxLBA
                if (device->drive_info.deviceMaxLba <= SCSI_MAX_32_LBA && sectors <= UINT16_MAX &&
                    lba <= SCSI_MAX_32_LBA)
                {
                    // use read 10
                    ret = scsi_Read_10(device, 0, false, fua, false, C_CAST(uint32_t, lba), 0,
                                       C_CAST(uint16_t, sectors), ptrData, dataSize);
                }
                else
                {
                    // use read 16
                    ret = scsi_Read_16(device, 0, false, fua, false, lba, 0, sectors, ptrData, dataSize);
                }
            }
            else
            {
                // try a read10. If it fails for invalid op-code, then try read 6
                ret = scsi_Read_10(device, 0, false, fua, false, C_CAST(uint32_t, lba), 0, C_CAST(uint16_t, sectors),
                                   ptrData, dataSize);
                if (SUCCESS != ret)
                {
                    senseDataFields readSense;
                    safe_memset(&readSense, sizeof(senseDataFields), 0, sizeof(senseDataFields));
                    get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &readSense);
                    if (readSense.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                        readSense.scsiStatusCodes.asc == 0x20 && readSense.scsiStatusCodes.ascq == 0x00)
                    {
                        ret = scsi_Read_6(device, C_CAST(uint32_t, lba), C_CAST(uint8_t, sectors), ptrData, dataSize);
                        if (SUCCESS == ret)
                        {
                            // setup the hacks like this so prevent future retries
                            device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                            device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6       = true;
                        }
                    }
                }
            }
        }
    }
    return ret;
}

eReturnValues scsi_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret     = SUCCESS; // assume success
    uint32_t      sectors = UINT32_C(0);
    bool          fua     = false;
    // make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (forceUnitAccess)
    {
        get_SCSI_DPO_FUA_Support(device);
        fua = device->drive_info.dpoFUA;
    }
    if (device->drive_info.passThroughHacks.scsiHacks.readWrite.available)
    {
        // This device is in the database or the command support has been determined some other way to allow us to issue
        // a correct command without any other issues.
        if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16)
        {
            ret = scsi_Write_16(device, 0, false, fua, lba, 0, sectors, ptrData, dataSize);
        }
        else if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12)
        {
            ret = scsi_Write_12(device, 0, false, fua, C_CAST(uint32_t, lba), 0, sectors, ptrData, dataSize);
        }
        else if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10)
        {
            ret = scsi_Write_10(device, 0, false, fua, C_CAST(uint32_t, lba), 0, C_CAST(uint16_t, sectors), ptrData,
                                dataSize);
        }
        else if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6)
        {
            ret = scsi_Write_6(device, C_CAST(uint32_t, lba), C_CAST(uint8_t, sectors), ptrData, dataSize);
        }
        else
        {
            // This shouldn't happen...
            ret = BAD_PARAMETER;
        }
    }
    else // Use the generic rules below to issue what will most likely be the correct commands...-TJE
    {
        if (device->drive_info.scsiVersion >=
            SCSI_VERSION_SPC_3) // SBC2 introduced write 16 command, so checking for SPC3
        {
            // there's no real way to tell when scsi drive supports write 10 vs write 16 (which are all we will care
            // about in here), so just based on transfer length and the maxLBA
            if (device->drive_info.deviceMaxLba <= UINT32_MAX && sectors <= UINT16_MAX && lba <= UINT32_MAX)
            {
                // use write 10
                ret = scsi_Write_10(device, 0, false, fua, C_CAST(uint32_t, lba), 0, C_CAST(uint16_t, sectors), ptrData,
                                    dataSize);
            }
            else
            {
                // use write 16
                ret = scsi_Write_16(device, 0, false, fua, lba, 0, sectors, ptrData, dataSize);
            }
        }
        else
        {
            // try a write10. If it fails for invalid op-code, then try read 6
            ret = scsi_Write_10(device, 0, false, fua, C_CAST(uint32_t, lba), 0, C_CAST(uint16_t, sectors), ptrData,
                                dataSize);
            if (SUCCESS != ret)
            {
                senseDataFields readSense;
                safe_memset(&readSense, sizeof(senseDataFields), 0, sizeof(senseDataFields));
                get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &readSense);
                if (readSense.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                    readSense.scsiStatusCodes.asc == 0x20 && readSense.scsiStatusCodes.ascq == 0x00)
                {
                    ret = scsi_Write_6(device, C_CAST(uint32_t, lba), C_CAST(uint8_t, sectors), ptrData, dataSize);
                    if (SUCCESS == ret)
                    {
                        // setup the hacks like this so prevent future retries
                        device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6       = true;
                    }
                }
            }
        }
    }
    if (forceUnitAccess && !device->drive_info.dpoFUA)
    {
        // send verify after write since FUA is not available
        ret = scsi_Verify(device, lba, dataSize / device->drive_info.deviceBlockSize);
    }
    return ret;
}

eReturnValues io_Read(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    // make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }

    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
        // perform ATA reads
        return ata_Read(device, lba, forceUnitAccess, ptrData, dataSize);
    case SCSI_INTERFACE:
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        // perform SCSI reads
        return scsi_Read(device, lba, forceUnitAccess, ptrData, dataSize);
    case NVME_INTERFACE:
        return nvme_Read(device, lba, C_CAST(uint16_t, (dataSize / device->drive_info.deviceBlockSize) - 1), false,
                         forceUnitAccess, 0, ptrData, dataSize);
    case RAID_INTERFACE:
        // perform SCSI reads for now. We may need to add unique functions for NVMe and RAID reads later
        return scsi_Read(device, lba, forceUnitAccess, ptrData, dataSize);
    default:
        return NOT_SUPPORTED;
    }
}

eReturnValues io_Write(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    // make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }

    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
        // perform ATA writes
        return ata_Write(device, lba, forceUnitAccess, ptrData, dataSize);
    case SCSI_INTERFACE:
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        // perform SCSI writes
        return scsi_Write(device, lba, forceUnitAccess, ptrData, dataSize);
    case NVME_INTERFACE:
        return nvme_Write(device, lba, C_CAST(uint16_t, (dataSize / device->drive_info.deviceBlockSize) - 1), false,
                          forceUnitAccess, 0, 0, ptrData, dataSize);
    case RAID_INTERFACE:
        // perform SCSI writes for now. We may need to add unique functions for NVMe and RAID writes later
        return scsi_Write(device, lba, forceUnitAccess, ptrData, dataSize);
    default:
        return NOT_SUPPORTED;
    }
}

eReturnValues read_LBA(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    if (device->os_info.osReadWriteRecommended)
    {
        // Old comment says this function does not always work reliably in Windows...This is NOT functional in other
        // OS's.
        return os_Read(device, lba, forceUnitAccess, ptrData, dataSize);
    }
    else
    {
        return io_Read(device, lba, forceUnitAccess, ptrData, dataSize);
    }
}

eReturnValues write_LBA(tDevice* device, uint64_t lba, bool forceUnitAccess, uint8_t* ptrData, uint32_t dataSize)
{
    if (device->os_info.osReadWriteRecommended)
    {
        // Old comment says this function does not always work reliably in Windows...This is NOT functional in other
        // OS's.
        return os_Write(device, lba, forceUnitAccess, ptrData, dataSize);
    }
    else
    {
        return io_Write(device, lba, forceUnitAccess, ptrData, dataSize);
    }
}

eReturnValues ata_Read_Verify(tDevice* device, uint64_t lba, uint32_t range)
{
    eReturnValues ret = SUCCESS; // assume success
    if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
    {
        // use 48bit commands by default
        if (range > 65536)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            if (range == 65536) // this is represented in the command with sector count set to 0
            {
                range = 0;
            }
            // make sure the LBA is within range
            if (lba > MAX_48_BIT_LBA)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                // send read verify ext
                if (device->drive_info.ata_Options.chsModeOnly)
                {
                    uint16_t cylinder = UINT16_C(0);
                    uint8_t  head     = UINT8_C(0);
                    uint8_t  sector   = UINT8_C(0);
                    if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                    {
                        ret = ata_Legacy_Read_Verify_Sectors_CHS(device, true, C_CAST(uint16_t, range), cylinder, head,
                                                                 sector);
                    }
                    else // Couldn't convert or the LBA is greater than the current CHS mode
                    {
                        ret = NOT_SUPPORTED;
                    }
                }
                else
                {
                    ret = ata_Read_Verify_Sectors(device, true, C_CAST(uint16_t, range), lba);
                }
            }
        }
    }
    else
    {
        // use the 28bit commands...first check that they aren't requesting more data than can be transferred in a 28bit
        // command, exception being 256 since that can be represented by a 0
        if (range > 256)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            if (range == 256)
            {
                range = 0;
            }
            // make sure the LBA is within range
            if (lba > MAX_28_BIT_LBA)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                // send read verify (28bit)
                if (device->drive_info.ata_Options.chsModeOnly)
                {
                    uint16_t cylinder = UINT16_C(0);
                    uint8_t  head     = UINT8_C(0);
                    uint8_t  sector   = UINT8_C(0);
                    if (SUCCESS == convert_LBA_To_CHS(device, C_CAST(uint32_t, lba), &cylinder, &head, &sector))
                    {
                        ret = ata_Legacy_Read_Verify_Sectors_CHS(device, false, C_CAST(uint16_t, range), cylinder, head,
                                                                 sector);
                    }
                    else // Couldn't convert or the LBA is greater than the current CHS mode
                    {
                        ret = NOT_SUPPORTED;
                    }
                }
                else
                {
                    ret = ata_Read_Verify_Sectors(device, false, C_CAST(uint16_t, range), lba);
                }
            }
        }
    }
    return ret;
}

eReturnValues scsi_Verify(tDevice* device, uint64_t lba, uint32_t range)
{
    eReturnValues ret = SUCCESS; // assume success
    // there's no real way to tell when scsi drive supports verify 10 vs verify 16 (which are all we will care about in
    // here), so just based on transfer length and the maxLBA
    if (device->drive_info.deviceMaxLba <= SCSI_MAX_32_LBA && range <= UINT16_MAX && lba <= SCSI_MAX_32_LBA)
    {
        // use verify 10
        ret = scsi_Verify_10(device, 0, false, 00, C_CAST(uint32_t, lba), 0, C_CAST(uint16_t, range), M_NULLPTR, 0);
    }
    else
    {
        // use verify 16 (SPC3-SBC2 brought this command in)
        ret = scsi_Verify_16(device, 0, false, 00, lba, 0, range, M_NULLPTR, 0);
    }
    return ret;
}

eReturnValues nvme_Verify_LBA(tDevice* device, uint64_t lba, uint32_t range)
{
    eReturnValues ret = SUCCESS;
    if (le16_to_host(device->drive_info.IdentifyData.nvme.ctrl.oncs) & BIT7 && range < (UINT16_MAX + 1))
    {
        // nvme verify command is supported
        ret = nvme_Verify(device, lba, false, true, 0, C_CAST(uint16_t, range) - 1);
    }
    else
    {
        // NVME doesn't have a verify command like ATA or SCSI, so we're going to substitute by doing a read with FUA
        // set....should be the same minus doing a data transfer.
        uint32_t dataLength = device->drive_info.deviceBlockSize * range;
        uint8_t* data       = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (data != M_NULLPTR)
        {
            ret = nvme_Read(device, lba, C_CAST(uint16_t, range - 1), false, true, 0, data, dataLength);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
        safe_free_aligned(&data);
    }
    return ret;
}

eReturnValues verify_LBA(tDevice* device, uint64_t lba, uint32_t range)
{
    if (device->os_info.osReadWriteRecommended)
    {
        return os_Verify(device, lba, range);
    }
    else
    {
        switch (device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
            // perform ATA verifies
            return ata_Read_Verify(device, lba, range);
        case SCSI_INTERFACE:
        case USB_INTERFACE:
        case MMC_INTERFACE:
        case SD_INTERFACE:
        case IEEE_1394_INTERFACE:
            // perform SCSI verifies
            return scsi_Verify(device, lba, range);
        case NVME_INTERFACE:
            return nvme_Verify_LBA(device, lba, range);
        case RAID_INTERFACE:
            // perform SCSI verifies for now. We may need to add unique functions for NVMe and RAID writes later
            return scsi_Verify(device, lba, range);
        default:
            return NOT_SUPPORTED;
        }
    }
}

eReturnValues ata_Flush_Cache_Command(tDevice* device)
{
    bool ext = false;
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT13)
    {
        ext = true;
    }
    return ata_Flush_Cache(device, ext);
}

eReturnValues scsi_Synchronize_Cache_Command(tDevice* device)
{
    // synch/flush cache introduced in SCSI2. Not going to check for it though since some USB drives do support this
    // command and report SCSI or no version. - TJE there's no real way to tell when SCSI drive supports synchronize
    // cache 10 vs synchronize cache 16 (which are all we will care about in here), so just based on the maxLBA
    eReturnValues ret = SUCCESS;
    if (device->drive_info.deviceMaxLba <= SCSI_MAX_32_LBA)
    {
        ret = scsi_Synchronize_Cache_10(device, false, 0, 0, 0);
    }
    else
    {
        ret = scsi_Synchronize_Cache_16(device, false, 0, 0, 0);
        if (ret == NOT_SUPPORTED)
        {
            // Some devices/adapters only support the 10B command
            // Need to retry with 10B if this happens
            ret = scsi_Synchronize_Cache_10(device, false, 0, 0, 0);
        }
    }
    return ret;
}

eReturnValues flush_Cache(tDevice* device)
{
    if (device->os_info.osReadWriteRecommended)
    {
        return os_Flush(device);
    }
    else
    {
        switch (device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
            // perform ATA writes
            return ata_Flush_Cache_Command(device);
        case SCSI_INTERFACE:
        case USB_INTERFACE:
        case MMC_INTERFACE:
        case SD_INTERFACE:
        case IEEE_1394_INTERFACE:
            // perform SCSI writes
            return scsi_Synchronize_Cache_Command(device);
        case NVME_INTERFACE:
            return nvme_Flush(device);
        case RAID_INTERFACE:
            // perform SCSI writes for now. We may need to add unique functions for NVMe and RAID writes later
            return scsi_Synchronize_Cache_Command(device);
        default:
            return NOT_SUPPORTED;
        }
    }
}

eReturnValues close_Zone(tDevice* device, bool closeAll, uint64_t zoneID, uint16_t zoneCount)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Close_Zone_Ext(device, closeAll, zoneID, zoneCount);
        break;
    case SCSI_DRIVE:
        ret = scsi_Close_Zone(device, closeAll, zoneID, zoneCount);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues finish_Zone(tDevice* device, bool finishAll, uint64_t zoneID, uint16_t zoneCount)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Finish_Zone_Ext(device, finishAll, zoneID, zoneCount);
        break;
    case SCSI_DRIVE:
        ret = scsi_Finish_Zone(device, finishAll, zoneID, zoneCount);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues open_Zone(tDevice* device, bool openAll, uint64_t zoneID, uint16_t zoneCount)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Open_Zone_Ext(device, openAll, zoneID, zoneCount);
        break;
    case SCSI_DRIVE:
        ret = scsi_Open_Zone(device, openAll, zoneID, zoneCount);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues reset_Write_Pointer(tDevice* device, bool resetAll, uint64_t zoneID, uint16_t zoneCount)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Reset_Write_Pointers_Ext(device, resetAll, zoneID, zoneCount);
        break;
    case SCSI_DRIVE:
        ret = scsi_Reset_Write_Pointers(device, resetAll, zoneID, zoneCount);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

eReturnValues report_Zones(tDevice*              device,
                           eZoneReportingOptions reportingOptions,
                           bool                  partial,
                           uint64_t              zoneLocator,
                           uint8_t*              ptrData,
                           uint32_t              dataSize)
{
    eReturnValues ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (dataSize % LEGACY_DRIVE_SEC_SIZE != 0)
        {
            return BAD_PARAMETER;
        }
        ret = ata_Report_Zones_Ext(device, reportingOptions, partial,
                                   C_CAST(uint16_t, dataSize / LEGACY_DRIVE_SEC_SIZE), zoneLocator, ptrData, dataSize);
        break;
    case SCSI_DRIVE:
        ret = scsi_Report_Zones(device, reportingOptions, partial, dataSize, zoneLocator, ptrData);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}
