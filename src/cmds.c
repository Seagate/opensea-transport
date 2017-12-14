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
// \file cmds.c   Implementation for generic ATA/SCSI functions
//                     The intention of the file is to be generic & not OS specific

#include "cmds.h"
#include "ata_helper_func.h"
#include "scsi_helper_func.h"
#include "nvme_helper_func.h"
#include "common_public.h"
#include <inttypes.h>
#include "platform_helper.h"

int send_Sanitize_Block_Erase(tDevice *device, bool exitFailureMode)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Sanitize_Block_Erase(device, exitFailureMode);
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Sanitize(device, false, false, 0, exitFailureMode, SANITIZE_NVM_BLOCK_ERASE, 0);
        break;
#else
        //rely on SCSI translation
#endif
    case SCSI_DRIVE:
        ret = scsi_Sanitize_Block_Erase(device, exitFailureMode, true);
        break;
    default:
        if (VERBOSITY_QUIET < g_verbosity)
        {
            printf("Current device type not supported yet\n");
        }
        break;
    }
    return ret;
}

int send_Sanitize_Crypto_Erase(tDevice *device,bool exitFailureMode)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Sanitize_Crypto_Scramble(device, exitFailureMode);
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Sanitize(device, false, false, 0, exitFailureMode, SANITIZE_NVM_CRYPTO, 0);
        break;
#else
        //rely on SCSI translation
#endif
    case SCSI_DRIVE:
        ret = scsi_Sanitize_Cryptographic_Erase(device, exitFailureMode, true);
        break;
    default:
        if (VERBOSITY_QUIET < g_verbosity)
        {
            printf("Current device type not supported yet\n");
        }
        break;
    }
    return ret;
}

int send_Sanitize_Overwrite_Erase(tDevice *device, bool exitFailureMode, bool invertBetweenPasses, uint8_t overwritePasses, uint8_t *pattern, uint32_t patternLength)
{
    int ret = UNKNOWN;
    bool localPattern = false;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        uint32_t ataPattern = 0;
        if (pattern && patternLength >= 4)
        {
            ataPattern = M_BytesTo4ByteValue(pattern[3], pattern[2], pattern[1], pattern[0]);
        }
        ret = ata_Sanitize_Overwrite_Erase(device, exitFailureMode, invertBetweenPasses, overwritePasses & 0x0F, ataPattern);
    }
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
    {
        uint32_t nvmPattern = 0;
        if (pattern && patternLength >= 4)
        {
            nvmPattern = M_BytesTo4ByteValue(pattern[3], pattern[2], pattern[1], pattern[0]);
        }
        ret = nvme_Sanitize(device, false, invertBetweenPasses, overwritePasses, exitFailureMode, SANITIZE_NVM_CRYPTO, nvmPattern);
    }
        break;
#else
        //rely on SCSI translation
        //fall through...
#endif
    case SCSI_DRIVE:
        //overwrite passes set to 0 on scsi is reserved. This is being changed to the maximum for SCSI to mean 16 passes
        if ((overwritePasses & 0x0F) == 0)
        {
            overwritePasses = 0x1F;
        }
        //we need to allocate a zero pattern if none was provided. 4 bytes is enough for this (and could handle SAT transaltion if necessary).
        if (!pattern)
        {
            localPattern = true;
            pattern = (uint8_t*)calloc(4, sizeof(uint8_t));
            if (!pattern)
            {
                return MEMORY_FAILURE;
            }
        }
        ret = scsi_Sanitize_Overwrite(device, exitFailureMode, true, invertBetweenPasses, SANITIZE_OVERWRITE_NO_CHANGES, overwritePasses & 0x1F, pattern, patternLength);
        if (localPattern)
        {
            safe_Free(pattern);
            localPattern = false;
        }
        break;
    default:
        printf("Current device type not supported yet\n");
        break;
    }
    return ret;
}

int send_Sanitize_Exit_Failure_Mode(tDevice *device)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        ret = ata_Sanitize_Status(device, true);
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Sanitize(device, false, false, 0, false, SANITIZE_NVM_EXIT_FAILURE_MODE, 0);
        break;
#else
        //rely on SCSI translation
#endif
    case SCSI_DRIVE:
        ret = scsi_Sanitize_Exit_Failure_Mode(device);
        break;
    default:
        printf("Current device type not supported yet\n");
        break;
    }
    return ret;
}

int spin_down_drive(tDevice *device, bool sleepState)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (sleepState == true)//send sleep command
        {
            ret = ata_Sleep(device);
        }
        else //standby
        {
            ret = ata_Standby_Immediate(device);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        //standby_z to spin the drive down. set the immediate bit as well.
        ret = scsi_Start_Stop_Unit(device, true, 0, PC_STANDBY, false, false, false);
    }
    else
    {
        if (VERBOSITY_QUIET < g_verbosity)
        {
            printf("Spin down drive is not supported on this drive type at this time\n");
        }
        ret = NOT_SUPPORTED;
    }
    return ret;
}

//-----------------------------------------------------------------------------
//
//  fill_Drive_Info_Data()
//
//! \brief   Description:  Generic Function to get drive information data filled  
//						   into the driveInfo_TYPE of the device structure. 
//						   This function assumes the type & interface has already
//						   determined by the OS layer. 
//  Entry:
//!   \param tDevice - pointer to the device structure
//!   
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
int fill_Drive_Info_Data(tDevice *device)
{
	int status = SUCCESS;
    #ifdef _DEBUG
    printf("%s: -->\n",__FUNCTION__);
    #endif
	if (device)
	{		
        if (device->drive_info.interface_type == UNKNOWN_INTERFACE)
        {
            status = BAD_PARAMETER;
            return status;
        }
		switch (device->drive_info.interface_type)
		{
        case IDE_INTERFACE:
            //We know this is an ATA interface and we SHOULD be able to send either an ATA or ATAPI identify...but that doesn't work right, so if the OS layer told us it is ATAPI, do SCSI device discovery
            if (device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.drive_type == TAPE_DRIVE)
            {
                status = fill_In_Device_Info(device);
            }
            else
            {
                status = fill_In_ATA_Drive_Info(device);
                if (status == FAILURE || status == UNKNOWN)
                {
                    //printf("trying scsi discovery\n");
                    //could not enumerate as ATA, try SCSI in case it's taking CDBs at the low layer to communicate and not translating more than the A1 op-code to check it if's a SAT command.
                    status = fill_In_Device_Info(device);
                }
            }
            break;
        case NVME_INTERFACE:
			#if !defined(DISABLE_NVME_PASSTHROUGH)
			status = fill_In_NVMe_Device_Info(device);
			break;
			#endif
		case SCSI_INTERFACE:
		case USB_INTERFACE:
        default:
            //call this instead. It will handle issuing scsi commands and at the end will attempt an ATA Identify if needed
            status = fill_In_Device_Info(device);
			break;
		}		
	}
	else
	{
		status = BAD_PARAMETER;
	}
    #ifdef _DEBUG
    if (device)
    {
        printf("Drive type: %d\n", device->drive_info.drive_type);
        printf("Interface type: %d\n", device->drive_info.interface_type);
        printf("Media type: %d\n", device->drive_info.media_type);
    }
    printf("%s: <--\n",__FUNCTION__);
	#endif
	return status;
}

int firmware_Download_Command(tDevice *device, eDownloadMode dlMode, bool useDMA, uint32_t offset, uint32_t xferLen, uint8_t *ptrData, uint8_t slotNumber)
{
    int ret = UNKNOWN;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        eDownloadMicrocodeFeatures ataDLMode = ATA_DL_MICROCODE_SAVE_IMMEDIATE;//default
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
        ret = ata_Download_Microcode(device, ataDLMode, xferLen / LEGACY_DRIVE_SEC_SIZE, offset / LEGACY_DRIVE_SEC_SIZE, useDMA, ptrData, xferLen);
    }
        break;
    case NVME_DRIVE:
#if !defined(DISABLE_NVME_PASSTHROUGH)
    {
        switch (dlMode)
        {
        case DL_FW_ACTIVATE:
            ret = nvme_Firmware_Commit(device, NVME_CA_REPLACE_ACTIVITE_ON_RST, slotNumber);
            break;
        case DL_FW_DEFERRED:
            ret = nvme_Firmware_Image_Dl(device, offset, xferLen, ptrData);
            break;
        case DL_FW_FULL:
        case DL_FW_TEMP:
        case DL_FW_SEGMENTED:
        case DL_FW_DEFERRED_SELECT_ACTIVATE:
        //none of these are supported
        default:
            ret = NOT_SUPPORTED;
            break;
        }
    }
        break;
#endif
    case SCSI_DRIVE:
    {
        eWriteBufferMode scsiDLMode = SCSI_WB_DL_MICROCODE_SAVE_ACTIVATE;//default
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
        ret = scsi_Write_Buffer(device, scsiDLMode, 0, slotNumber, offset, xferLen, ptrData);
    }
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int firmware_Download_Activate(tDevice *device, bool useDMA, uint8_t slotNumber)
{
    return firmware_Download_Command(device, DL_FW_ACTIVATE, useDMA, 0, 0, NULL, slotNumber);
}

int security_Send(tDevice *device, bool useDMA, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    switch(device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        if (dataSize != 0)
        {
            uint8_t *tcgBufPtr = ptrData;
            bool useLocalMemory = false;
            //make sure we are sending/recieving in sectors for ATA
            if (dataSize < LEGACY_DRIVE_SEC_SIZE || dataSize % LEGACY_DRIVE_SEC_SIZE != 0)
            {
                //round up to nearest 512byte sector
                size_t newBufferSize = ((dataSize + LEGACY_DRIVE_SEC_SIZE) - 1) / LEGACY_DRIVE_SEC_SIZE;
                tcgBufPtr = (uint8_t*)calloc(newBufferSize, sizeof(uint8_t));
                if (tcgBufPtr == NULL)
                {
                    return MEMORY_FAILURE;
                }
                useLocalMemory = true;
            }
            ret = ata_Trusted_Send(device, useDMA, securityProtocol, securityProtocolSpecific, ptrData, dataSize);
            if (useLocalMemory)
            {
                safe_Free(tcgBufPtr);
            }
        }
        else
        {
            ret = ata_Trusted_Non_Data(device, securityProtocol, false, securityProtocolSpecific);
        }
    }
    break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Security_Send(device, securityProtocol, securityProtocolSpecific, 0, ptrData, dataSize);
        break;
#endif
    case SCSI_DRIVE:
    {
        //The inc512 bit is not allowed on NVMe drives when sent this command....we may want to remove setting it, but for now we'll leave it here.
        bool inc512 = false;
        if (dataSize >= LEGACY_DRIVE_SEC_SIZE && (dataSize % LEGACY_DRIVE_SEC_SIZE) == 0 && device->drive_info.drive_type != NVME_DRIVE && strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) != 0)
        {
            inc512 = true;
            dataSize /= LEGACY_DRIVE_SEC_SIZE;
        }
        ret = scsi_SecurityProtocol_Out(device, securityProtocol, securityProtocolSpecific, inc512, dataSize, ptrData, 15);
    }
    break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

int security_Receive(tDevice *device, bool useDMA, uint8_t securityProtocol, uint16_t securityProtocolSpecific, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
    {
        if (dataSize != 0)
        {
            uint8_t *tcgBufPtr = ptrData;
            uint32_t tcgDataSize = dataSize;
            bool useLocalMemory = false;
            //make sure we are sending/receiving in sectors for ATA
            if (dataSize < LEGACY_DRIVE_SEC_SIZE || dataSize % LEGACY_DRIVE_SEC_SIZE != 0)
            {
                //round up to nearest 512byte sector
                tcgDataSize = ((dataSize + LEGACY_DRIVE_SEC_SIZE) - 1) / LEGACY_DRIVE_SEC_SIZE;
                tcgBufPtr = (uint8_t*)calloc(tcgDataSize, sizeof(uint8_t));
                if (!tcgBufPtr)
                {
                    return MEMORY_FAILURE;
                }
                useLocalMemory = true;
            }
            ret = ata_Trusted_Receive(device, useDMA, securityProtocol, securityProtocolSpecific, tcgBufPtr, tcgDataSize);
            if (useLocalMemory)
            {
                memcpy(ptrData, tcgBufPtr, M_Min(dataSize, tcgDataSize));
                safe_Free(tcgBufPtr);
            }
        }
        else
        {
            ret = ata_Trusted_Non_Data(device, securityProtocol, true, securityProtocolSpecific);
        }
    }
    break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Security_Receive(device, securityProtocol, securityProtocolSpecific, 0, ptrData, dataSize);
        break;
#endif
    case SCSI_DRIVE:
    {
		//The inc512 bit is not allowed on NVMe drives when sent this command....we may want to remove setting it, but for now we'll leave it here.
        bool inc512 = false;
        if (dataSize >= LEGACY_DRIVE_SEC_SIZE && dataSize % LEGACY_DRIVE_SEC_SIZE == 0 && device->drive_info.drive_type != NVME_DRIVE && strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) != 0)
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

int write_Same(tDevice *device, bool useGPL, bool useDMA, uint64_t startingLba, uint64_t numberOfLogicalBlocks, uint8_t *pattern)
{
    int ret = UNKNOWN;
    bool noDataTransfer = false;
    if (pattern == NULL)
    {
        noDataTransfer = true;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (noDataTransfer)
        {
            uint8_t zeroPattern[4] = { 0 };
            ret = ata_SCT_Write_Same(device, useGPL, useDMA, WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD, startingLba, numberOfLogicalBlocks, zeroPattern, sizeof(zeroPattern) / sizeof(*zeroPattern));
        }
        else
        {
            ret = ata_SCT_Write_Same(device, useGPL, useDMA, WRITE_SAME_BACKGROUND_USE_SINGLE_LOGICAL_SECTOR, startingLba, numberOfLogicalBlocks, pattern, 1);
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Write_Same_16(device, 0, false, false, noDataTransfer, startingLba, 0, (uint32_t)numberOfLogicalBlocks, pattern, device->drive_info.deviceBlockSize);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int write_Psuedo_Uncorrectable_Error(tDevice *device, uint64_t corruptLBA)
{
    int ret = UNKNOWN;
    bool multipleLogicalPerPhysical = false;//used to set the physical block bit when applicable
    uint16_t logicalPerPhysicalBlocks = (uint16_t)(device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    if (logicalPerPhysicalBlocks > 1)
    {
        //since this device has multiple logical blocks per physical block, we also need to adjust the LBA to be at the start of the physical block
        //do this by dividing by the number of logical sectors per physical sector. This integer division will get us aligned
        uint64_t tempLBA = corruptLBA / logicalPerPhysicalBlocks;
        tempLBA *= logicalPerPhysicalBlocks;
        //do we need to adjust for alignment? We'll add it in later if I ever get a drive that has an alignment other than 0 - TJE
        corruptLBA = tempLBA;
        //set this flag for SCSI
        multipleLogicalPerPhysical = true;
    }
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (device->drive_info.ata_Options.writeUncorrectableExtSupported)
        {
            ret = ata_Write_Uncorrectable(device, 0x55, logicalPerPhysicalBlocks, corruptLBA);
        }
        else if (device->drive_info.IdentifyData.ata.Word206 & BIT1)
        {
            //use SCT read & write long commands
            uint16_t numberOfECCCRCBytes = 0;
            uint16_t numberOfBlocksRequested = 0;
            uint32_t dataSize = device->drive_info.deviceBlockSize + LEGACY_DRIVE_SEC_SIZE;
            uint8_t *data = (uint8_t*)calloc(dataSize, sizeof(uint8_t));
            if (!data)
            {
                return MEMORY_FAILURE;
            }
            ret = ata_SCT_Read_Write_Long(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, SCT_RWL_READ_LONG, corruptLBA, data, dataSize, &numberOfECCCRCBytes, &numberOfBlocksRequested);
            if (ret == SUCCESS)
            {
                seed_64(time(NULL));
                //modify the user data to cause a uncorrectable error
                for (uint32_t iter = 0; iter < device->drive_info.deviceBlockSize - 1; ++iter)
                {
                    data[iter] = (uint8_t)random_Range_64(0, UINT8_MAX);
                }
                if (numberOfBlocksRequested)
                {
                    //The drive responded through SAT enough to tell us exactly how many blocks are expected...so we can set the data transfer length as is expected...since this wasn't clear on non 512B logical sector drives.
                    dataSize = LEGACY_DRIVE_SEC_SIZE * numberOfBlocksRequested;
                }
                //now write back the data with a write long command
                ret = ata_SCT_Read_Write_Long(device, device->drive_info.ata_Options.generalPurposeLoggingSupported, device->drive_info.ata_Options.readLogWriteLogDMASupported, SCT_RWL_WRITE_LONG, corruptLBA, data, dataSize, NULL, NULL);
            }
            safe_Free(data);
        }
        else if (device->drive_info.IdentifyData.ata.Word022 > 0 && device->drive_info.IdentifyData.ata.Word022 < UINT16_MAX && corruptLBA < MAX_28_BIT_LBA)/*a value of zero may be valid on really old drives which otherwise accept this command, but this should be ok for now*/
        {
            bool setFeaturesToChangeECCBytes = false;
            if (device->drive_info.IdentifyData.ata.Word022 != 4)
            {
                //need to issue a set features command to specify the number of ECC bytes before doing a read or write long (according to old Seagate ATA reference manual from the web)
                if (SUCCESS == ata_Set_Features(device, SF_LEGACY_SET_VENDOR_SPECIFIC_ECC_BYTES_FOR_READ_WRITE_LONG, M_Byte0(device->drive_info.IdentifyData.ata.Word022), 0, 0, 0))
                {
                    setFeaturesToChangeECCBytes = true;
                }
            }
            uint32_t dataSize = device->drive_info.deviceBlockSize + device->drive_info.IdentifyData.ata.Word022;
            uint8_t *data = (uint8_t*)calloc(dataSize, sizeof(uint8_t));
            if (!data)
            {
                return MEMORY_FAILURE;
            }
            //This drive supports the legacy 28bit read/write long commands from ATA...
            //These commands are really old and transfer weir dbyte based values.
            //While these transfer lengths shouldbe supported by SAT, there are some SATLs that won't handle this odd case. It may or may not go through...-TJE
            ret = ata_Legacy_Read_Long(device, true, (uint32_t)corruptLBA, data, dataSize);
            if (ret == SUCCESS)
            {
                seed_64(time(NULL));
                //modify the user data to cause a uncorrectable error
                for (uint32_t iter = 0; iter < device->drive_info.deviceBlockSize - 1; ++iter)
                {
                    data[iter] = (uint8_t)random_Range_64(0, UINT8_MAX);
                }
                ret = ata_Legacy_Write_Long(device, true, (uint32_t)corruptLBA, data, dataSize);
            }   
            if (setFeaturesToChangeECCBytes)
            {
                //reverting back to drive defaults again so that we don't mess anyone else up.
                if (SUCCESS == ata_Set_Features(device, SF_LEGACY_SET_4_BYTES_ECC_FOR_READ_WRITE_LONG, 0, 0, 0, 0))
                {
                    setFeaturesToChangeECCBytes = false;
                }
            }
            safe_Free(data);
        }
        else //no other standardized way to write a error to this location.
        {
            ret = NOT_SUPPORTED;
        }
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            ret = scsi_Write_Long_16(device, false, true, multipleLogicalPerPhysical, corruptLBA, 0, NULL);
        }
        else
        {
            ret = scsi_Write_Long_10(device, false, true, multipleLogicalPerPhysical, (uint32_t)corruptLBA, 0, NULL);
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int write_Flagged_Uncorrectable_Error(tDevice *device, uint64_t corruptLBA)
{
    int ret = UNKNOWN;
    //This will only flag individual logical blocks
    switch (device->drive_info.drive_type)
    {
    case ATA_DRIVE:
        if (device->drive_info.ata_Options.writeUncorrectableExtSupported)
        {
            ret = ata_Write_Uncorrectable(device, 0xAA, 1, corruptLBA);
        }
        else
        {
            //use SCT read/write long
            ret = NOT_SUPPORTED;//we'll add this in later-TJE
        }
        break;
    case NVME_DRIVE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        ret = nvme_Write_Uncorrectable(device, corruptLBA, 1);
        break;
#endif
    case SCSI_DRIVE:
        if (device->drive_info.deviceMaxLba > UINT32_MAX)
        {
            ret = scsi_Write_Long_16(device, true, true, false, corruptLBA, 0, NULL);
        }
        else
        {
            ret = scsi_Write_Long_10(device, true, true, false, (uint32_t)corruptLBA, 0, NULL);
        }
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
    return ret;
}

int ata_Read(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = SUCCESS;//assume success
    uint32_t sectors = 0;
    //make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (async)
    {
        //asynchronous not supported yet
        return NOT_SUPPORTED;
    }
    else //synchronous reads
    {   
        if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
        {
            //use 48bit commands by default
            if (sectors > 65536)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                if (sectors == 65536)//this is represented in the command with sector count set to 0
                {
                    sectors = 0;
                }
                //make sure the LBA is within range
                if (lba > MAX_48_BIT_LBA)
                {
                    ret = BAD_PARAMETER;
                }
                else
                {
                    if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                    {
                        //use PIO commands
                        ret = ata_Read_Sectors(device, lba, ptrData, sectors, dataSize, true);
                    }
                    else
                    {
                        //use DMA commands
                        ret = ata_Read_DMA(device, lba, ptrData, sectors, dataSize, true);
                    }                    
                }
            }
        }
        else
        {
            //use the 28bit commands...first check that they aren't requesting more data than can be transferred in a 28bit command, exception being 256 since that can be represented by a 0
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
                //make sure the LBA is within range
                if (lba > MAX_28_BIT_LBA)
                {
                    ret = BAD_PARAMETER;
                }
                else
                {
                    if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                    {
                        //use PIO commands
                        ret = ata_Read_Sectors(device, lba, ptrData, sectors, dataSize, false);
                    }
                    else
                    {
                        //use DMA commands
                        ret = ata_Read_DMA(device, lba, ptrData, sectors, dataSize, false);
                    }
                }
            }
        }
    }
    return ret;
}

int ata_Write(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = SUCCESS;//assume success
    uint32_t sectors = 0;
    //make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (async)
    {
        //asynchronous not supported yet
        return NOT_SUPPORTED;
    }
    else //synchronous writes
    {
        if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
        {
            //use 48bit commands by default
            if (sectors > 65536)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                if (sectors == 65536)//this is represented in the command with sector count set to 0
                {
                    sectors = 0;
                }
                //make sure the LBA is within range
                if (lba > MAX_48_BIT_LBA)
                {
                    ret = BAD_PARAMETER;
                }
                else
                {
                    if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                    {
                        //use PIO commands
                        ret = ata_Write_Sectors(device, lba, ptrData, dataSize, true);
                    }
                    else
                    {
                        //use DMA commands
                        ret = ata_Write_DMA(device, lba, ptrData, dataSize, true, false);
                    }
                }
            }
        }
        else
        {
            //use the 28bit commands...first check that they aren't requesting more data than can be transferred in a 28bit command, exception being 256 since that can be represented by a 0
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
                //make sure the LBA is within range
                if (lba > MAX_28_BIT_LBA)
                {
                    ret = BAD_PARAMETER;
                }
                else
                {
                    if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA)
                    {
                        //use PIO commands
                        ret = ata_Write_Sectors(device, lba, ptrData, dataSize, false);
                    }
                    else
                    {
                        //use DMA commands
                        ret = ata_Write_DMA(device, lba, ptrData, dataSize, false, false);
                    }
                }
            }
        }
    }
    return ret;
}

int scsi_Read(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = SUCCESS;//assume success
    uint32_t sectors = 0;
    //make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (async)
    {
        //asynchronous not supported yet
        return NOT_SUPPORTED;
    }
    else //synchronous reads
    {
        //there's no real way to tell when scsi drive supports read 10 vs read 16 (which are all we will care about in here), so just based on transfer length and the maxLBA
        if (device->drive_info.deviceMaxLba <= UINT32_MAX && sectors <= UINT16_MAX && lba <= UINT32_MAX)
        {
            //use read 10
            ret = scsi_Read_10(device, 0, false, false, false, (uint32_t)lba, 0, sectors, ptrData, dataSize);
        }
        else
        {
            //use read 16
            ret = scsi_Read_16(device, 0, false, false, false, lba, 0, sectors, ptrData, dataSize);
        }
    }
    return ret;
}

int scsi_Write(tDevice *device, uint64_t lba, bool async, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = SUCCESS;//assume success
    uint32_t sectors = 0;
    //make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }
    sectors = dataSize / device->drive_info.deviceBlockSize;
    if (async)
    {
        //asynchronous not supported yet
        return NOT_SUPPORTED;
    }
    else //synchronous reads
    {
        //there's no real way to tell when scsi drive supports write 10 vs write 16 (which are all we will care about in here), so just based on transfer length and the maxLBA
        if (device->drive_info.deviceMaxLba <= UINT32_MAX && sectors <= UINT16_MAX && lba <= UINT32_MAX)
        {
            //use write 10
            ret = scsi_Write_10(device, 0, false, false, (uint32_t)lba, 0, sectors, ptrData, dataSize);
        }
        else
        {
            //use write 16
            ret = scsi_Write_16(device, 0, false, false, lba, 0, sectors, ptrData, dataSize);
        }
    }
    return ret;
}

int io_Read(tDevice *device, uint64_t lba, bool async, uint8_t* ptrData, uint32_t dataSize)
{
    //asynchronous not supported yet
    if (async)
    {
        return NOT_SUPPORTED;
    }
    //make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }

    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
        //perform ATA reads
        return ata_Read(device, lba, async, ptrData, dataSize);
        break;
    case SCSI_INTERFACE:
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        //perform SCSI reads
        return scsi_Read(device, lba, async, ptrData, dataSize);
        break;
    case NVME_INTERFACE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        //TODO: validate that the protection information input value of 0 works!
        return nvme_Read(device, lba, dataSize / device->drive_info.deviceBlockSize, false, false, 0, ptrData, dataSize);
#endif
    case RAID_INTERFACE:
        //perform SCSI reads for now. We may need to add unique functions for NVMe and RAID reads later
        return scsi_Read(device, lba, async, ptrData, dataSize);
        break;
    default:
        return NOT_SUPPORTED;
        break;
    }
    return UNKNOWN;
}

int io_Write(tDevice *device, uint64_t lba, bool async, uint8_t* ptrData, uint32_t dataSize)
{
    //asynchronous not supported yet
    if (async)
    {
        return NOT_SUPPORTED;
    }
    //make sure that the data size is at least logical sector in size
    if (dataSize < device->drive_info.deviceBlockSize)
    {
        return BAD_PARAMETER;
    }

    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
        //perform ATA writes
        return ata_Write(device, lba, async, ptrData, dataSize);
        break;
    case SCSI_INTERFACE:
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        //perform SCSI writes
        return scsi_Write(device, lba, async, ptrData, dataSize);
        break;
    case NVME_INTERFACE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
        //TODO: validate that the protection information input value of 0 works!
        return nvme_Write(device, lba, dataSize / device->drive_info.deviceBlockSize, false, false, 0, 0, ptrData, dataSize);
#endif
    case RAID_INTERFACE:
        //perform SCSI writes for now. We may need to add unique functions for NVMe and RAID writes later
        return scsi_Write(device, lba, async, ptrData, dataSize);
    default:
        return NOT_SUPPORTED;
        break;
    }
    return UNKNOWN;
}

int read_LBA(tDevice *device, uint64_t lba, bool async, uint8_t* ptrData, uint32_t dataSize)
{
    if (device->os_info.osReadWriteRecommended)
    {
        //Old comment says this function does not always work reliably in Windows...This is NOT functional in other OS's.
        return os_Read(device, lba, async, ptrData, dataSize);
    }
    else
    {
        return io_Read(device, lba, async, ptrData, dataSize);
    }
}


int write_LBA(tDevice *device, uint64_t lba, bool async, uint8_t* ptrData, uint32_t dataSize)
{
    if (device->os_info.osReadWriteRecommended)
    {
        //Old comment says this function does not always work reliably in Windows...This is NOT functional in other OS's.
        return os_Write(device, lba, async, ptrData, dataSize);
    }
    else
    {
        return io_Write(device, lba, async, ptrData, dataSize);
    }
}

int ata_Read_Verify(tDevice *device, uint64_t lba, uint32_t range)
{
    int ret = SUCCESS;//assume success
    if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
    {
        //use 48bit commands by default
        if (range > 65536)
        {
            ret = BAD_PARAMETER;
        }
        else
        {
            if (range == 65536)//this is represented in the command with sector count set to 0
            {
                range = 0;
            }
            //make sure the LBA is within range
            if (lba > MAX_48_BIT_LBA)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                //send read verify ext
                ret = ata_Read_Verify_Sectors(device, true, (uint16_t)range, lba);
            }
        }
    }
    else
    {
        //use the 28bit commands...first check that they aren't requesting more data than can be transferred in a 28bit command, exception being 256 since that can be represented by a 0
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
            //make sure the LBA is within range
            if (lba > MAX_28_BIT_LBA)
            {
                ret = BAD_PARAMETER;
            }
            else
            {
                //send read verify (28bit)
                ret = ata_Read_Verify_Sectors(device, false, (uint16_t)range, lba);
            }
        }
    }
    return ret;
}

int scsi_Verify(tDevice *device, uint64_t lba, uint32_t range)
{
    int ret = SUCCESS;//assume success
    //there's no real way to tell when scsi drive supports verify 10 vs verify 16 (which are all we will care about in here), so just based on transfer length and the maxLBA
    if (device->drive_info.deviceMaxLba <= UINT32_MAX && range <= UINT16_MAX && lba <= UINT32_MAX)
    {
        //use verify 10
        ret = scsi_Verify_10(device, 0, false, 00, (uint32_t)lba, 0, (uint16_t)range, NULL, 0);
    }
    else
    {
        //use verify 16
        ret = scsi_Verify_16(device, 0, false, 00, lba, 0, range, NULL, 0);
    }
    return ret;
}

#if !defined (DISABLE_NVME_PASSTHROUGH)
int nvme_Verify_LBA(tDevice *device, uint64_t lba, uint32_t range)
{
	//NVME doesn't have a verify command like ATA or SCSI, so we're going to substitute by doing a read with FUA set....should be the same minus doing a data transfer.
	int ret = SUCCESS;
	uint32_t dataLength = device->drive_info.deviceBlockSize * range;
	uint8_t *data = (uint8_t*)calloc(dataLength, sizeof(uint8_t));
	if (data)
	{
		ret = nvme_Read(device, lba, range, false, true, 0, data, dataLength);
	}
	else
	{
		ret = MEMORY_FAILURE;
	}
	safe_Free(data);
	return ret;
}
#endif

int verify_LBA(tDevice *device, uint64_t lba, uint32_t range)
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
            //perform ATA verifies
            return ata_Read_Verify(device, lba, range);
            break;
        case SCSI_INTERFACE:
        case USB_INTERFACE:
        case MMC_INTERFACE:
        case SD_INTERFACE:
        case IEEE_1394_INTERFACE:
            //perform SCSI verifies
            return scsi_Verify(device, lba, range);
            break;
        case NVME_INTERFACE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
			return nvme_Verify_LBA(device, lba, range);
#endif
        case RAID_INTERFACE:
            //perform SCSI verifies for now. We may need to add unique functions for NVMe and RAID writes later
            return scsi_Verify(device, lba, range);
            break;
        default:
            return NOT_SUPPORTED;
            break;
        }
    }
    return UNKNOWN;
}

int ata_Flush_Cache_Command(tDevice *device)
{
    bool ext = false;
    if (device->drive_info.IdentifyData.ata.Word083 & BIT13)
    {
        ext = true;
    }
    return ata_Flush_Cache(device, ext);
}

int scsi_Synchronize_Cache_Command(tDevice *device)
{
    //there's no real way to tell when SCSI drive supports synchronize cache 10 vs synchronize cache 16 (which are all we will care about in here), so just based on the maxLBA
    if (device->drive_info.deviceMaxLba <= UINT32_MAX)
    {
        return scsi_Synchronize_Cache_10(device, false, 0, 0, 0);
    }
    else
    {
        return scsi_Synchronize_Cache_16(device, false, 0, 0, 0);
    }
}

int flush_Cache(tDevice *device)
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
            //perform ATA writes
            return ata_Flush_Cache_Command(device);
            break;
        case SCSI_INTERFACE:
        case USB_INTERFACE:
        case MMC_INTERFACE:
        case SD_INTERFACE:
        case IEEE_1394_INTERFACE:
            //perform SCSI writes
            return scsi_Synchronize_Cache_Command(device);
            break;
        case NVME_INTERFACE:
#if !defined (DISABLE_NVME_PASSTHROUGH)
            return nvme_Flush(device);
#endif
        case RAID_INTERFACE:
            //perform SCSI writes for now. We may need to add unique functions for NVMe and RAID writes later
            return scsi_Synchronize_Cache_Command(device);
            break;
        default:
            return NOT_SUPPORTED;
            break;
        }
    }
    return UNKNOWN;
}

int close_Zone(tDevice *device, bool closeAll, uint64_t zoneID)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Close_Zone_Ext(device, closeAll, zoneID);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Close_Zone(device, closeAll, zoneID);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int finish_Zone(tDevice *device, bool finishAll, uint64_t zoneID)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Finish_Zone_Ext(device, finishAll, zoneID);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Finish_Zone(device, finishAll, zoneID);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int open_Zone(tDevice *device, bool openAll, uint64_t zoneID)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Open_Zone_Ext(device, openAll, zoneID);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Open_Zone(device, openAll, zoneID);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int reset_Write_Pointer(tDevice *device, bool resetAll, uint64_t zoneID)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        ret = ata_Reset_Write_Pointers_Ext(device, resetAll, zoneID);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Reset_Write_Pointers(device, resetAll, zoneID);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

int report_Zones(tDevice *device, eZoneReportingOptions reportingOptions, bool partial, uint64_t zoneLocator, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        if (dataSize % LEGACY_DRIVE_SEC_SIZE != 0)
        {
            return BAD_PARAMETER;
        }
        ret = ata_Report_Zones_Ext(device, reportingOptions, partial, dataSize / LEGACY_DRIVE_SEC_SIZE, zoneLocator, ptrData, dataSize);
    }
    else if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        ret = scsi_Report_Zones(device, reportingOptions, partial, dataSize, zoneLocator, ptrData);
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}