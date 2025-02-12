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

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "common_public.h"
#include "scsi_helper_func.h"
#include <ctype.h> //for isprint

typedef enum eATADeviceTypeEnum
{
    ATA_DEVICE_TYPE_NONE,
    ATA_DEVICE_TYPE_ATA,
    ATA_DEVICE_TYPE_ATAPI,
    ATA_DEVICE_TYPE_SATA_RESERVED_1,
    ATA_DEVICE_TYPE_SATA_RESERVED_2,
    ATA_DEVICE_TYPE_CE_ATA,
    ATA_DEVICE_TYPE_HOST_MANGED_ZONED
} eATADeviceType;

static eATADeviceType get_ATA_Device_Type_From_Signature(ataReturnTFRs* rtfrs)
{
    eATADeviceType type = ATA_DEVICE_TYPE_NONE;
    if (rtfrs != M_NULLPTR)
    {
        if (rtfrs->secCnt == 0x01 && rtfrs->lbaLow == 0x01)
        {
            if (rtfrs->lbaMid == 0x00 && rtfrs->lbaHi == 0x00)
            {
                type = ATA_DEVICE_TYPE_ATA;
            }
            else if (rtfrs->lbaMid == 0xCD && rtfrs->lbaHi == 0xAB)
            {
                type = ATA_DEVICE_TYPE_HOST_MANGED_ZONED;
            }
            else if (rtfrs->lbaMid == 0x14 && rtfrs->lbaHi == 0xEB)
            {
                type = ATA_DEVICE_TYPE_ATAPI;
            }
            else if (rtfrs->lbaMid == 0xCE && rtfrs->lbaHi == 0xAA)
            {
                type = ATA_DEVICE_TYPE_CE_ATA;
            }
            else if (rtfrs->lbaMid == 0x3C && rtfrs->lbaHi == 0xC3)
            {
                type = ATA_DEVICE_TYPE_SATA_RESERVED_1;
            }
            else if (rtfrs->lbaMid == 0x69 && rtfrs->lbaHi == 0x96)
            {
                type = ATA_DEVICE_TYPE_SATA_RESERVED_2;
            }
        }
        else if (rtfrs->lbaMid == 0xCE && rtfrs->lbaHi == 0xAA)
        {
            // CE ATA does not require sector count and lba low set to 1
            type = ATA_DEVICE_TYPE_CE_ATA;
        }
    }
    return type;
}

// This is a basic validity indicator for a given ATA identify word. Checks that it is non-zero and not FFFFh
bool is_ATA_Identify_Word_Valid(uint16_t word)
{
    bool valid = false;
    if (word != UINT16_C(0) && word != UINT16_MAX)
    {
        valid = true;
    }
    return valid;
}

bool is_ATA_Identify_Word_Valid_With_Bits_14_And_15(uint16_t word)
{
    bool valid = false;
    if (is_ATA_Identify_Word_Valid(word) && (word & BIT15) == 0 && (word & BIT14) == BIT14)
    {
        valid = true;
    }
    return valid;
}

bool is_ATA_Identify_Word_Valid_SATA(uint16_t word)
{
    bool valid = false;
    if (is_ATA_Identify_Word_Valid(word) && (word & BIT0) == 0)
    {
        valid = true;
    }
    return valid;
}

static bool is_Buffer_Non_Zero(const uint8_t* ptrData, uint32_t dataLen)
{
    bool isNonZero = false;
    if (ptrData != M_NULLPTR)
    {
        for (uint32_t iter = UINT32_C(0); iter < dataLen; ++iter)
        {
            if (ptrData[iter] != 0)
            {
                isNonZero = true;
                break;
            }
        }
    }
    return isNonZero;
}

// This will send a read log ext command, and if it's DMA and sense data tells us that we had an invalid field in CDB,
// then we retry with PIO mode
eReturnValues send_ATA_Read_Log_Ext_Cmd(tDevice* device,
                                        uint8_t  logAddress,
                                        uint16_t pageNumber,
                                        uint8_t* ptrData,
                                        uint32_t dataSize,
                                        uint16_t featureRegister)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        bool dmaRetry           = false;
        bool sataLogRequiresPIO = false;
        if (!device->drive_info.ata_Options.sataReadLogDMASameAsPIO &&
            (logAddress == ATA_LOG_NCQ_COMMAND_ERROR_LOG || logAddress == ATA_LOG_SATA_PHY_EVENT_COUNTERS_LOG))
        {
            // special case for SATA NCQ error log and Phy event counters log!
            //  Old drives may require PIO mode to read these logs!
            // This uses SATA id word 76 to identify the case and adjust which command to issue in this case.
            sataLogRequiresPIO = true;
        }
        if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA &&
            device->drive_info.ata_Options.readLogWriteLogDMASupported && !sataLogRequiresPIO)
        {
            // try a read log ext DMA command
            ret = ata_Read_Log_Ext(device, logAddress, pageNumber, ptrData, dataSize, true, featureRegister);
            if (ret == SUCCESS)
            {
                return ret;
            }
            else
            {
                uint8_t senseKey = UINT8_C(0);
                uint8_t asc      = UINT8_C(0);
                uint8_t ascq     = UINT8_C(0);
                uint8_t fru      = UINT8_C(0);
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc,
                                           &ascq, &fru);
                // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
                // commands are not supported.
                if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                {
                    // turn off DMA mode
                    dmaRetry                                                   = true;
                    device->drive_info.ata_Options.readLogWriteLogDMASupported = false;
                }
                else
                {
                    // we likely had a failure not related to DMA mode command, so return the error
                    return ret;
                }
            }
        }
        // Send PIO Command
        ret = ata_Read_Log_Ext(device, logAddress, pageNumber, ptrData, dataSize, false, featureRegister);
        if (dmaRetry && ret != SUCCESS)
        {
            // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
            device->drive_info.ata_Options.readLogWriteLogDMASupported = true;
        }
    }
    return ret;
}

eReturnValues send_ATA_Write_Log_Ext_Cmd(tDevice* device,
                                         uint8_t  logAddress,
                                         uint16_t pageNumber,
                                         uint8_t* ptrData,
                                         uint32_t dataSize,
                                         bool     forceRTFRs)
{
    eReturnValues ret = NOT_SUPPORTED;
    if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        bool dmaRetry = false;
        if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA &&
            device->drive_info.ata_Options.readLogWriteLogDMASupported)
        {
            // try a write log ext DMA command
            ret = ata_Write_Log_Ext(device, logAddress, pageNumber, ptrData, dataSize, true, forceRTFRs);
            if (ret == SUCCESS)
            {
                return ret;
            }
            else
            {
                uint8_t senseKey = UINT8_C(0);
                uint8_t asc      = UINT8_C(0);
                uint8_t ascq     = UINT8_C(0);
                uint8_t fru      = UINT8_C(0);
                get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc,
                                           &ascq, &fru);
                // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
                // commands are not supported.
                if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
                {
                    // turn off DMA mode
                    dmaRetry                                                   = true;
                    device->drive_info.ata_Options.readLogWriteLogDMASupported = false;
                }
                else
                {
                    // we likely had a failure not related to DMA mode command, so return the error
                    return ret;
                }
            }
        }
        // Send PIO command
        ret = ata_Write_Log_Ext(device, logAddress, pageNumber, ptrData, dataSize, false, forceRTFRs);
        if (dmaRetry && ret != SUCCESS)
        {
            // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
            device->drive_info.ata_Options.readLogWriteLogDMASupported = true;
        }
    }
    return ret;
}

eReturnValues send_ATA_SCT(tDevice*               device,
                           eDataTransferDirection direction,
                           uint8_t                logAddress,
                           uint8_t*               dataBuf,
                           uint32_t               dataSize,
                           bool                   forceRTFRs)
{
    eReturnValues ret = UNKNOWN;
    if (logAddress != ATA_SCT_COMMAND_STATUS && logAddress != ATA_SCT_DATA_TRANSFER)
    {
        return BAD_PARAMETER;
    }
    bool useGPL = device->drive_info.ata_Options.generalPurposeLoggingSupported;
    // This is a hack for some USB drives. While a caller somewhere above this should handle this, this needs to be here
    // to ensure we don't hang these devices.
    if (device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly)
    {
        useGPL = false;
    }
    if (useGPL)
    {
        if (direction == XFER_DATA_IN)
        {
            ret = send_ATA_Read_Log_Ext_Cmd(device, logAddress, 0, dataBuf, dataSize, 0);
        }
        else if (direction == XFER_DATA_OUT)
        {
            ret = send_ATA_Write_Log_Ext_Cmd(device, logAddress, 0, dataBuf, dataSize, forceRTFRs);
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    else
    {
        if (direction == XFER_DATA_IN) // data in
        {
            ret = ata_SMART_Read_Log(device, logAddress, dataBuf, dataSize);
        }
        else if (direction == XFER_DATA_OUT)
        {
            ret = ata_SMART_Write_Log(device, logAddress, dataBuf, dataSize, forceRTFRs);
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    return ret;
}

eReturnValues send_ATA_SCT_Status(tDevice* device, uint8_t* dataBuf, uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    if (dataSize < LEGACY_DRIVE_SEC_SIZE)
    {
        return FAILURE;
    }
    ret = send_ATA_SCT(device, XFER_DATA_IN, ATA_SCT_COMMAND_STATUS, dataBuf, LEGACY_DRIVE_SEC_SIZE, false);

    return ret;
}

eReturnValues send_ATA_SCT_Command(tDevice* device, uint8_t* dataBuf, uint32_t dataSize, bool forceRTFRs)
{
    eReturnValues ret = UNKNOWN;
    if (dataSize < LEGACY_DRIVE_SEC_SIZE)
    {
        return FAILURE;
    }
    ret = send_ATA_SCT(device, XFER_DATA_OUT, ATA_SCT_COMMAND_STATUS, dataBuf, LEGACY_DRIVE_SEC_SIZE, forceRTFRs);

    return ret;
}

eReturnValues send_ATA_SCT_Data_Transfer(tDevice*               device,
                                         eDataTransferDirection direction,
                                         uint8_t*               dataBuf,
                                         uint32_t               dataSize)
{
    eReturnValues ret = UNKNOWN;

    ret = send_ATA_SCT(device, direction, ATA_SCT_DATA_TRANSFER, dataBuf, dataSize, false);

    return ret;
}

eReturnValues send_ATA_SCT_Read_Write_Long(tDevice*    device,
                                           eSCTRWLMode mode,
                                           uint64_t    lba,
                                           uint8_t*    dataBuf,
                                           uint32_t    dataSize,
                                           uint16_t*   numberOfECCCRCBytes,
                                           uint16_t*   numberOfBlocksRequested)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, readWriteLongCommandSector, LEGACY_DRIVE_SEC_SIZE);

    // action code
    readWriteLongCommandSector[0] = M_Byte0(SCT_READ_WRITE_LONG);
    readWriteLongCommandSector[1] = M_Byte1(SCT_READ_WRITE_LONG);
    // function code set in if below
    // LBA
    readWriteLongCommandSector[4]  = M_Byte0(lba);
    readWriteLongCommandSector[5]  = M_Byte1(lba);
    readWriteLongCommandSector[6]  = M_Byte2(lba);
    readWriteLongCommandSector[7]  = M_Byte3(lba);
    readWriteLongCommandSector[8]  = M_Byte4(lba);
    readWriteLongCommandSector[9]  = M_Byte5(lba);
    readWriteLongCommandSector[10] = RESERVED;
    readWriteLongCommandSector[11] = RESERVED;

    if (mode == SCT_RWL_READ_LONG)
    {
        readWriteLongCommandSector[2] = M_Byte0(SCT_RWL_READ_LONG);
        readWriteLongCommandSector[3] = M_Byte1(SCT_RWL_READ_LONG);

        // send a SCT command
        if (SUCCESS == send_ATA_SCT_Command(device, readWriteLongCommandSector, LEGACY_DRIVE_SEC_SIZE, true))
        {
            if (numberOfECCCRCBytes != M_NULLPTR)
            {
                *numberOfECCCRCBytes = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaLow,
                                                           device->drive_info.lastCommandRTFRs.secCnt);
            }
            if (numberOfBlocksRequested != M_NULLPTR)
            {
                *numberOfBlocksRequested = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaHi,
                                                               device->drive_info.lastCommandRTFRs.lbaMid);
            }
            // Read the SCT data log
            ret = send_ATA_SCT_Data_Transfer(device, XFER_DATA_IN, dataBuf, dataSize);
        }
    }
    else if (mode == SCT_RWL_WRITE_LONG)
    {
        readWriteLongCommandSector[2] = M_Byte0(SCT_RWL_WRITE_LONG);
        readWriteLongCommandSector[3] = M_Byte1(SCT_RWL_WRITE_LONG);

        // send a SCT command
        if (SUCCESS == send_ATA_SCT_Command(device, readWriteLongCommandSector, LEGACY_DRIVE_SEC_SIZE, true))
        {
            if (numberOfECCCRCBytes != M_NULLPTR)
            {
                *numberOfECCCRCBytes = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaLow,
                                                           device->drive_info.lastCommandRTFRs.secCnt);
            }
            if (numberOfBlocksRequested != M_NULLPTR)
            {
                *numberOfBlocksRequested = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaHi,
                                                               device->drive_info.lastCommandRTFRs.lbaMid);
            }
            // Write the SCT data log
            ret = send_ATA_SCT_Data_Transfer(device, XFER_DATA_OUT, dataBuf, dataSize);
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
    }
    return ret;
}

eReturnValues send_ATA_SCT_Write_Same(tDevice*               device,
                                      eSCTWriteSameFunctions functionCode,
                                      uint64_t               startLBA,
                                      uint64_t               fillCount,
                                      uint8_t*               pattern,
                                      uint64_t               patternLength)
{
    eReturnValues ret             = UNKNOWN;
    uint8_t*      writeSameBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!writeSameBuffer)
    {
        perror("Calloc failure!\n");
        return MEMORY_FAILURE;
    }
    // action code
    writeSameBuffer[0] = M_Byte0(SCT_WRITE_SAME);
    writeSameBuffer[1] = M_Byte1(SCT_WRITE_SAME);
    // function code
    writeSameBuffer[2] = M_Byte0(C_CAST(uint16_t, functionCode));
    writeSameBuffer[3] = M_Byte1(C_CAST(uint16_t, functionCode));
    // start
    writeSameBuffer[4]  = M_Byte0(startLBA);
    writeSameBuffer[5]  = M_Byte1(startLBA);
    writeSameBuffer[6]  = M_Byte2(startLBA);
    writeSameBuffer[7]  = M_Byte3(startLBA);
    writeSameBuffer[8]  = M_Byte4(startLBA);
    writeSameBuffer[9]  = M_Byte5(startLBA);
    writeSameBuffer[10] = RESERVED;
    writeSameBuffer[11] = RESERVED;
    // Fill Count
    writeSameBuffer[12] = M_Byte0(fillCount);
    writeSameBuffer[13] = M_Byte1(fillCount);
    writeSameBuffer[14] = M_Byte2(fillCount);
    writeSameBuffer[15] = M_Byte3(fillCount);
    writeSameBuffer[16] = M_Byte4(fillCount);
    writeSameBuffer[17] = M_Byte5(fillCount);
    writeSameBuffer[18] = M_Byte6(fillCount);
    writeSameBuffer[19] = M_Byte7(fillCount);
    // Pattern field (when it applies)
    if (functionCode == WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD ||
        functionCode == WRITE_SAME_FOREGROUND_USE_PATTERN_FIELD)
    {
        uint32_t thePattern  = UINT32_C(0);
        uint64_t patternIter = UINT64_C(0);
        // copy at most a 32bit pattern into thePattern
        for (patternIter = 0; patternIter < patternLength && patternIter < 4; patternIter++)
        {
            thePattern |= pattern[patternIter];
            if ((patternIter + 1) == 4)
            {
                break;
            }
            thePattern = thePattern << 8;
        }
        writeSameBuffer[20] = M_Byte0(thePattern);
        writeSameBuffer[21] = M_Byte1(thePattern);
        writeSameBuffer[22] = M_Byte2(thePattern);
        writeSameBuffer[23] = M_Byte3(thePattern);
    }
    // pattern length (when it applies)
    if (functionCode == WRITE_SAME_BACKGROUND_USE_MULTIPLE_LOGICAL_SECTORS ||
        functionCode == WRITE_SAME_FOREGROUND_USE_MULTIPLE_LOGICAL_SECTORS)
    {
        writeSameBuffer[24] = M_Byte0(patternLength);
        writeSameBuffer[25] = M_Byte1(patternLength);
        writeSameBuffer[26] = M_Byte2(patternLength);
        writeSameBuffer[27] = M_Byte3(patternLength);
        writeSameBuffer[28] = M_Byte4(patternLength);
        writeSameBuffer[29] = M_Byte5(patternLength);
        writeSameBuffer[30] = M_Byte6(patternLength);
        writeSameBuffer[31] = M_Byte7(patternLength);
    }

    ret = send_ATA_SCT_Command(device, writeSameBuffer, LEGACY_DRIVE_SEC_SIZE, false);

    if (functionCode == WRITE_SAME_BACKGROUND_USE_MULTIPLE_LOGICAL_SECTORS ||
        functionCode == WRITE_SAME_FOREGROUND_USE_MULTIPLE_LOGICAL_SECTORS ||
        functionCode == WRITE_SAME_BACKGROUND_USE_SINGLE_LOGICAL_SECTOR ||
        functionCode == WRITE_SAME_FOREGROUND_USE_SINGLE_LOGICAL_SECTOR)
    {
        // send the pattern to the data transfer log
        ret = send_ATA_SCT_Data_Transfer(device, XFER_DATA_OUT, pattern,
                                         C_CAST(uint32_t, patternLength * device->drive_info.deviceBlockSize));
    }

    safe_free_aligned(&writeSameBuffer);
    return ret;
}

eReturnValues send_ATA_SCT_Error_Recovery_Control(tDevice*  device,
                                                  uint16_t  functionCode,
                                                  uint16_t  selectionCode,
                                                  uint16_t* currentValue,
                                                  uint16_t  recoveryTimeLimit)
{
    eReturnValues ret                 = UNKNOWN;
    uint8_t*      errorRecoveryBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!errorRecoveryBuffer)
    {
        perror("Calloc failure!\n");
        return MEMORY_FAILURE;
    }
    // if we are retrieving the current values, then we better have a good pointer...no point in sending the command if
    // we don't
    if ((functionCode == 0x0002 || functionCode == 0x0004) && !currentValue)
    {
        safe_free_aligned(&errorRecoveryBuffer);
        return BAD_PARAMETER;
    }

    // action code
    errorRecoveryBuffer[0] = M_Byte0(SCT_ERROR_RECOVERY_CONTROL);
    errorRecoveryBuffer[1] = M_Byte1(SCT_ERROR_RECOVERY_CONTROL);
    // function code
    errorRecoveryBuffer[2] = M_Byte0(functionCode);
    errorRecoveryBuffer[3] = M_Byte1(functionCode);
    // selection code
    errorRecoveryBuffer[4] = M_Byte0(selectionCode);
    errorRecoveryBuffer[5] = M_Byte1(selectionCode);
    // recovery time limit
    errorRecoveryBuffer[6] = M_Byte0(recoveryTimeLimit);
    errorRecoveryBuffer[7] = M_Byte1(recoveryTimeLimit);

    ret = send_ATA_SCT_Command(device, errorRecoveryBuffer, LEGACY_DRIVE_SEC_SIZE, true);

    if ((functionCode == 0x0002 || functionCode == 0x0004) && currentValue != M_NULLPTR)
    {
        *currentValue =
            M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaLow, device->drive_info.lastCommandRTFRs.secCnt);
    }
    safe_free_aligned(&errorRecoveryBuffer);
    return ret;
}

eReturnValues send_ATA_SCT_Feature_Control(tDevice*  device,
                                           uint16_t  functionCode,
                                           uint16_t  featureCode,
                                           uint16_t* state,
                                           uint16_t* optionFlags)
{
    eReturnValues ret                  = UNKNOWN;
    uint8_t*      featureControlBuffer = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!featureControlBuffer)
    {
        perror("Calloc Failure!\n");
        return MEMORY_FAILURE;
    }
    // make sure we have valid pointers for state and optionFlags
    if (!state || !optionFlags)
    {
        safe_free_aligned(&featureControlBuffer);
        return BAD_PARAMETER;
    }
    // clear the state and option flags out, unless we are setting something
    if (functionCode != 0x0001)
    {
        *state       = 0;
        *optionFlags = 0;
    }
    // fill in the buffer with the correct information
    // action code
    featureControlBuffer[0] = M_Byte0(SCT_FEATURE_CONTROL);
    featureControlBuffer[1] = M_Byte1(SCT_FEATURE_CONTROL);
    // function code
    featureControlBuffer[2] = M_Byte0(functionCode);
    featureControlBuffer[3] = M_Byte1(functionCode);
    // feature code
    featureControlBuffer[4] = M_Byte0(featureCode);
    featureControlBuffer[5] = M_Byte1(featureCode);
    // state
    featureControlBuffer[6] = M_Byte0(*state);
    featureControlBuffer[7] = M_Byte1(*state);
    // option flags
    featureControlBuffer[8] = M_Byte0(*optionFlags);
    featureControlBuffer[9] = M_Byte1(*optionFlags);

    ret = send_ATA_SCT_Command(device, featureControlBuffer, LEGACY_DRIVE_SEC_SIZE, true);

    // add in copying rtfrs into status or option flags here
    if (ret == SUCCESS)
    {
        if (functionCode == 0x0002)
        {
            *state = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaLow,
                                         device->drive_info.lastCommandRTFRs.secCnt);
        }
        else if (functionCode == 0x0003)
        {
            *optionFlags = M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaLow,
                                               device->drive_info.lastCommandRTFRs.secCnt);
        }
    }
    safe_free_aligned(&featureControlBuffer);
    return ret;
}

eReturnValues send_ATA_SCT_Data_Table(tDevice* device,
                                      uint16_t functionCode,
                                      uint16_t tableID,
                                      uint8_t* dataBuf,
                                      uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;

    if (!dataBuf)
    {
        return BAD_PARAMETER;
    }
    // Action code
    dataBuf[0] = M_Byte0(SCT_DATA_TABLES);
    dataBuf[1] = M_Byte1(SCT_DATA_TABLES);
    // Function code
    dataBuf[2] = M_Byte0(functionCode);
    dataBuf[3] = M_Byte1(functionCode);
    // Table ID
    dataBuf[4] = M_Byte0(tableID);
    dataBuf[5] = M_Byte1(tableID);

    ret = send_ATA_SCT_Command(device, dataBuf, LEGACY_DRIVE_SEC_SIZE, false);

    if (ret == SUCCESS)
    {
        if (functionCode == 0x0001)
        {
            // now read the log that tells us the table we requested
            safe_memset(dataBuf, dataSize, 0, dataSize); // clear the buffer before we read in data since we are done
                                                         // with what we had to send to the drive
            ret = send_ATA_SCT_Data_Transfer(device, XFER_DATA_IN, dataBuf, dataSize);
        }
        // else we need to add functionality since something new was added to the spec
    }
    return ret;
}

eReturnValues send_ATA_Download_Microcode_Cmd(tDevice*                   device,
                                              eDownloadMicrocodeFeatures subCommand,
                                              uint16_t                   blockCount,
                                              uint16_t                   bufferOffset,
                                              uint8_t*                   pData,
                                              uint32_t                   dataLen,
                                              bool                       firstSegment,
                                              bool                       lastSegment,
                                              uint32_t                   timeoutSeconds)
{
    eReturnValues ret      = NOT_SUPPORTED;
    bool          dmaRetry = false;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA &&
        device->drive_info.ata_Options.downloadMicrocodeDMASupported)
    {
        ret = ata_Download_Microcode(device, subCommand, blockCount, bufferOffset, true, pData, dataLen, firstSegment,
                                     lastSegment, timeoutSeconds);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry                                                     = true;
                device->drive_info.ata_Options.downloadMicrocodeDMASupported = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Download_Microcode(device, subCommand, blockCount, bufferOffset, false, pData, dataLen, firstSegment,
                                 lastSegment, timeoutSeconds);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        device->drive_info.ata_Options.downloadMicrocodeDMASupported = true;
    }
    return ret;
}

eReturnValues send_ATA_Trusted_Send_Cmd(tDevice* device,
                                        uint8_t  securityProtocol,
                                        uint16_t securityProtocolSpecific,
                                        uint8_t* ptrData,
                                        uint32_t dataSize)
{
    eReturnValues ret           = NOT_SUPPORTED;
    bool          dmaRetry      = false;
    static bool   dmaTrustedCmd = true;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA && dmaTrustedCmd &&
        device->drive_info.ata_Options.dmaSupported)
    {
        ret = ata_Trusted_Send(device, true, securityProtocol, securityProtocolSpecific, ptrData, dataSize);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry      = true;
                dmaTrustedCmd = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Trusted_Send(device, false, securityProtocol, securityProtocolSpecific, ptrData, dataSize);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        dmaTrustedCmd = true;
    }
    return ret;
}

eReturnValues send_ATA_Trusted_Receive_Cmd(tDevice* device,
                                           uint8_t  securityProtocol,
                                           uint16_t securityProtocolSpecific,
                                           uint8_t* ptrData,
                                           uint32_t dataSize)
{
    eReturnValues ret           = NOT_SUPPORTED;
    bool          dmaRetry      = false;
    static bool   dmaTrustedCmd = true;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA && dmaTrustedCmd &&
        device->drive_info.ata_Options.dmaSupported)
    {
        ret = ata_Trusted_Receive(device, true, securityProtocol, securityProtocolSpecific, ptrData, dataSize);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry      = true;
                dmaTrustedCmd = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Trusted_Receive(device, false, securityProtocol, securityProtocolSpecific, ptrData, dataSize);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        dmaTrustedCmd = true;
    }
    return ret;
}

eReturnValues send_ATA_Read_Buffer_Cmd(tDevice* device, uint8_t* ptrData)
{
    eReturnValues ret      = NOT_SUPPORTED;
    bool          dmaRetry = false;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA &&
        device->drive_info.ata_Options.readBufferDMASupported)
    {
        ret = ata_Read_Buffer(device, ptrData, true);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry                                              = true;
                device->drive_info.ata_Options.readBufferDMASupported = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Read_Buffer(device, ptrData, false);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        device->drive_info.ata_Options.readBufferDMASupported = true;
    }
    return ret;
}

eReturnValues send_ATA_Write_Buffer_Cmd(tDevice* device, uint8_t* ptrData)
{
    eReturnValues ret      = NOT_SUPPORTED;
    bool          dmaRetry = false;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA &&
        device->drive_info.ata_Options.writeBufferDMASupported)
    {
        ret = ata_Write_Buffer(device, ptrData, true);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry                                               = true;
                device->drive_info.ata_Options.writeBufferDMASupported = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Write_Buffer(device, ptrData, false);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        device->drive_info.ata_Options.writeBufferDMASupported = true;
    }
    return ret;
}

eReturnValues send_ATA_Read_Stream_Cmd(tDevice* device,
                                       uint8_t  streamID,
                                       bool     notSequential,
                                       bool     readContinuous,
                                       uint8_t  commandCCTL,
                                       uint64_t LBA,
                                       uint8_t* ptrData,
                                       uint32_t dataSize)
{
    eReturnValues ret       = NOT_SUPPORTED;
    bool          dmaRetry  = false;
    static bool   streamDMA = true;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA && streamDMA &&
        device->drive_info.ata_Options.dmaSupported)
    {
        ret = ata_Read_Stream_Ext(device, true, streamID, notSequential, readContinuous, commandCCTL, LBA, ptrData,
                                  dataSize);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry  = true;
                streamDMA = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Read_Stream_Ext(device, false, streamID, notSequential, readContinuous, commandCCTL, LBA, ptrData,
                              dataSize);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        streamDMA = true;
    }
    return ret;
}

eReturnValues send_ATA_Write_Stream_Cmd(tDevice* device,
                                        uint8_t  streamID,
                                        bool     flush,
                                        bool     writeContinuous,
                                        uint8_t  commandCCTL,
                                        uint64_t LBA,
                                        uint8_t* ptrData,
                                        uint32_t dataSize)
{
    eReturnValues ret       = NOT_SUPPORTED;
    bool          dmaRetry  = false;
    static bool   streamDMA = true;
    if (device->drive_info.ata_Options.dmaMode != ATA_DMA_MODE_NO_DMA && streamDMA &&
        device->drive_info.ata_Options.dmaSupported)
    {
        ret = ata_Write_Stream_Ext(device, true, streamID, flush, writeContinuous, commandCCTL, LBA, ptrData, dataSize);
        if (ret == SUCCESS)
        {
            return ret;
        }
        else
        {
            uint8_t senseKey = UINT8_C(0);
            uint8_t asc      = UINT8_C(0);
            uint8_t ascq     = UINT8_C(0);
            uint8_t fru      = UINT8_C(0);
            get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseKey, &asc, &ascq,
                                       &fru);
            // Checking for illegal request, invalid field in CDB since this is what we've seen reported when DMA
            // commands are not supported.
            if (senseKey == SENSE_KEY_ILLEGAL_REQUEST && asc == 0x24 && ascq == 0x00)
            {
                // turn off DMA mode
                dmaRetry  = true;
                streamDMA = false;
            }
            else
            {
                // we likely had a failure not related to DMA mode command, so return the error
                return ret;
            }
        }
    }
    ret = ata_Write_Stream_Ext(device, false, streamID, flush, writeContinuous, commandCCTL, LBA, ptrData, dataSize);
    if (dmaRetry && ret != SUCCESS)
    {
        // this means something else is wrong, and it's not the DMA mode, so we can turn it back on
        streamDMA = true;
    }
    return ret;
}

void fill_ATA_Strings_From_Identify_Data(uint8_t* ptrIdentifyData,
                                         char     ataMN[ATA_IDENTIFY_MN_LENGTH + 1],
                                         char     ataSN[ATA_IDENTIFY_SN_LENGTH + 1],
                                         char     ataFW[ATA_IDENTIFY_FW_LENGTH + 1])
{
    DISABLE_NONNULL_COMPARE
    if (ptrIdentifyData != M_NULLPTR)
    {
        ptAtaIdentifyData idData  = C_CAST(ptAtaIdentifyData, ptrIdentifyData);
        bool              validSN = true;
        bool              validMN = true;
        bool              validFW = true;
        // check for valid strings (ATA-2 mentioned if these are set to zero, then they are not defined in the standards
        if (idData->Word010 == UINT16_C(0))
        {
            validSN = false;
        }
        if (idData->Word023 == UINT16_C(0))
        {
            validFW = false;
        }
        if (idData->Word027 == UINT16_C(0))
        {
            validFW = false;
        }
        // fill each buffer with data from ATA ID data
        if (validSN && ataSN != M_NULLPTR)
        {
#if defined(SERIAL_NUM_LEN) && defined(ATA_IDENTIFY_SN_LENGTH) && ATA_IDENTIFY_SN_LENGTH == SERIAL_NUM_LEN
            uint16_t snLimit = SERIAL_NUM_LEN;
#else
            uint16_t snLimit = M_Min(SERIAL_NUM_LEN, ATA_IDENTIFY_SN_LENGTH);
#endif
            safe_memset(ataSN, ATA_IDENTIFY_SN_LENGTH + 1, 0, snLimit + UINT16_C(1));
            safe_memcpy(ataSN, ATA_IDENTIFY_SN_LENGTH + 1, idData->SerNum, snLimit);
            for (uint16_t iter = UINT16_C(0); iter < snLimit; ++iter)
            {
                if (!safe_isascii(ataSN[iter]) || !safe_isprint(ataSN[iter]))
                {
                    ataSN[iter] = ' '; // replace with a space
                }
            }
            remove_Leading_And_Trailing_Whitespace_Len(ataSN, snLimit);
        }
        if (validFW && ataFW != M_NULLPTR)
        {
#if defined(FW_REV_LEN) && defined(ATA_IDENTIFY_FW_LENGTH) && ATA_IDENTIFY_FW_LENGTH == FW_REV_LEN
            uint16_t fwLimit = FW_REV_LEN;
#else
            uint16_t fwLimit = M_Min(FW_REV_LEN, ATA_IDENTIFY_FW_LENGTH);
#endif
            safe_memset(ataFW, ATA_IDENTIFY_FW_LENGTH + 1, 0, fwLimit + UINT16_C(1));
            safe_memcpy(ataFW, ATA_IDENTIFY_FW_LENGTH + 1, idData->FirmVer, fwLimit);
            for (uint16_t iter = UINT16_C(0); iter < fwLimit; ++iter)
            {
                if (!safe_isascii(ataFW[iter]) || !safe_isprint(ataFW[iter]))
                {
                    ataFW[iter] = ' '; // replace with a space
                }
            }
            remove_Leading_And_Trailing_Whitespace_Len(ataFW, fwLimit);
        }
        if (validMN && ataMN != M_NULLPTR)
        {
#if defined(MODEL_NUM_LEN) && defined(ATA_IDENTIFY_MN_LENGTH) && ATA_IDENTIFY_MN_LENGTH == MODEL_NUM_LEN
            uint16_t mnLimit = MODEL_NUM_LEN;
#else
            uint16_t mnLimit = M_Min(MODEL_NUM_LEN, ATA_IDENTIFY_MN_LENGTH);
#endif
            safe_memset(ataMN, ATA_IDENTIFY_MN_LENGTH + 1, 0, mnLimit + UINT16_C(1));
            safe_memcpy(ataMN, ATA_IDENTIFY_MN_LENGTH + 1, idData->ModelNum, mnLimit);
            for (uint16_t iter = UINT16_C(0); iter < mnLimit; ++iter)
            {
                if (!safe_isascii(ataMN[iter]) || !safe_isprint(ataMN[iter]))
                {
                    ataMN[iter] = ' '; // replace with a space
                }
            }
            remove_Leading_And_Trailing_Whitespace_Len(ataMN, mnLimit);
        }
    }
    RESTORE_NONNULL_COMPARE
}

static eReturnValues get_Identify_Data(tDevice* device, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret = FAILURE;

    if (device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.drive_type == LEGACY_TAPE_DRIVE)
    {
        if (SUCCESS == ata_Identify_Packet_Device(device, ptrData, dataSize) && is_Buffer_Non_Zero(ptrData, dataSize))
        {
            ret = SUCCESS;
        }
    }
    else
    {
        if (SUCCESS == ata_Identify(device, ptrData, dataSize) && is_Buffer_Non_Zero(ptrData, dataSize))
        {
            ret = SUCCESS;
        }
        else if (ATA_DEVICE_TYPE_ATAPI == get_ATA_Device_Type_From_Signature(&device->drive_info.lastCommandRTFRs))
        {
            if (SUCCESS == ata_Identify_Packet_Device(device, ptrData, dataSize) &&
                is_Buffer_Non_Zero(ptrData, dataSize))
            {
                ret                           = SUCCESS;
                device->drive_info.drive_type = ATAPI_DRIVE;
            }
        }
    }
    return ret;
}

// This function attempts numerous workarounds to get working identify data (to work around SAT issues)
static eReturnValues initial_Identify_Device(tDevice* device)
{
    eReturnValues ret           = NOT_SUPPORTED;
    bool          noMoreRetries = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, iddata, LEGACY_DRIVE_SEC_SIZE);
    if ((device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.drive_type == LEGACY_TAPE_DRIVE ||
         device->drive_info.media_type == MEDIA_OPTICAL || device->drive_info.media_type == MEDIA_TAPE) &&
        !(device->drive_info.passThroughHacks.hacksSetByReportedID ||
          device->drive_info.passThroughHacks.someHacksSetByOSDiscovery))
    {
        // make sure we disable sending the A1h SAT CDB or we could accidentally send a "blank" command due to how most
        // of these devices receive commands under different OSs or through translators.
        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;
    }
    do
    {
        noMoreRetries = true; // start with this to force exit in case of missing error handling
        ret           = get_Identify_Data(device, iddata, LEGACY_DRIVE_SEC_SIZE);
        if (ret != SUCCESS)
        {
            // First check the sense data to see if we got invalid operation code or invalid field in CDB before we
            // continue. If invalid field in CDB, then the opcode is supported, so try changing to TPSIU. Also try
            // turning check condition off if it is enabled. If invalid operation code try 16B command (if not already)
            //    If still invalid operation code AND is a USB interface it could be returning an incorrect sense code
            //    so try tpsiu to see if that works.
            senseDataFields senseFields;
            safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
            get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST)
            {
                if (senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)
                {
                    // Invalid Field in CDB
                    // Most likely check condition, so check that first.
                    // Then try TPSIU
                    if (!device->drive_info.passThroughHacks.hacksSetByReportedID)
                    {
                        if (device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable)
                        {
                            device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = false;
                            noMoreRetries                                                                = false;
                        }
                        else if (!device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported)
                        {
                            device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;
                            noMoreRetries                                                   = false;
                        }
                        else if (!device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough)
                        {
                            device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                            noMoreRetries                                                                  = false;
                        }
                    }
                }
                else if (senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)
                {
                    // Invalid operation code.
                    // This means CDB byte 0 is wrong, however a lot of USB adapters seem to do this when they just
                    // don't like a field in the CDB. In this case first try the 16B command if we have not already.
                    // Otherwise check if the check condition bit is set to remove. If not, just exit
                    if (!device->drive_info.passThroughHacks.hacksSetByReportedID)
                    {
                        if (!device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported)
                        {
                            device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;
                            noMoreRetries                                                   = false;
                        }
                        else if (device->drive_info.interface_type == USB_INTERFACE &&
                                 !device->drive_info.passThroughHacks.ataPTHacks.disableCheckCondition &&
                                 device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable)
                        {
                            device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = false;
                            noMoreRetries                                                                = false;
                        }
                    }
                }
            }
            if (device->drive_info.interface_type != IDE_INTERFACE)
            {
                // This can help prevent overwhelming some adapters when multiple commands fail causing unnecessary
                // delays Only when not IDE Interface since that is only set at the low-level when using a native ATA
                // passthrough. When in that situation, this would just cause a loop since we use the opensea software
                // translation
                scsi_Test_Unit_Ready(device, M_NULLPTR);
            }
        }
    } while (ret != SUCCESS && noMoreRetries == false);
    return ret;
}

eReturnValues fill_In_ATA_Drive_Info(tDevice* device)
{
    eReturnValues ret = UNKNOWN;
    // Both pointers pointing to the same data.
    uint8_t*  identifyData = M_REINTERPRET_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000);
    uint16_t* ident_word   = &device->drive_info.IdentifyData.ata.Word000;
#ifdef _DEBUG
    printf("%s -->\n", __FUNCTION__);
#endif

    ret = initial_Identify_Device(device);
    if (ret == SUCCESS)
    {
        bool     lbaModeSupported = false;
        uint16_t cylinder         = UINT16_C(0);
        uint8_t  head             = UINT8_C(0);
        uint8_t  spt              = UINT8_C(0);

        uint64_t* fillWWN                = M_NULLPTR;
        uint32_t* fillLogicalSectorSize  = M_NULLPTR;
        uint32_t* fillPhysicalSectorSize = M_NULLPTR;
        uint16_t* fillSectorAlignment    = M_NULLPTR;
        uint64_t* fillMaxLba             = M_NULLPTR;

        if (device->drive_info.interface_type == IDE_INTERFACE &&
            device->drive_info.scsiVersion == SCSI_VERSION_NO_STANDARD)
        {
            device->drive_info.scsiVersion =
                SCSI_VERSION_SPC_5; // SPC5. This is what software translator will set at the moment. Can make this
                                    // configurable later, but this should be OK
        }
        ret = SUCCESS;
        if (device->drive_info.lastCommandRTFRs.device &
            DEVICE_SELECT_BIT) // Checking for the device select bit being set to know it's device 1 (Not that we really
                               // need it). This may not always be reported correctly depending on the lower layers of
                               // the OS and hardware. - TJE
        {
            device->drive_info.ata_Options.isDevice1 = true;
        }

        if (le16_to_host(ident_word[0]) & BIT15)
        {
            device->drive_info.drive_type = ATAPI_DRIVE;
            device->drive_info.media_type = MEDIA_OPTICAL;
        }
        else
        {
            device->drive_info.drive_type = ATA_DRIVE;
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[1])) &&
            is_ATA_Identify_Word_Valid(le16_to_host(ident_word[3])) &&
            is_ATA_Identify_Word_Valid(le16_to_host(ident_word[6])))
        {
            cylinder = le16_to_host(ident_word[1]);      // word 1
            head = M_Byte0(le16_to_host(ident_word[3])); // Word3 - adapted from ESDI so discard high byte. High byte
                                                         // was number of removable drive heads
            spt = M_Byte0(le16_to_host(ident_word[6]));  // Word6
            // According to ATA, word 53, bit 0 set to 1 means the words 54,-58 are valid.
            // if set to zero they MAY be valid....so just check validity on everything
        }

        // set some pointers to where we want to fill in information...we're doing this so that on USB, we can store
        // some info about the child drive, without disrupting the standard drive_info that has already been filled in
        // by the fill_SCSI_Info function
        fillWWN                = &device->drive_info.worldWideName;
        fillLogicalSectorSize  = &device->drive_info.deviceBlockSize;
        fillPhysicalSectorSize = &device->drive_info.devicePhyBlockSize;
        fillSectorAlignment    = &device->drive_info.sectorAlignment;
        fillMaxLba             = &device->drive_info.deviceMaxLba;

        // IDE interface means we're connected to a native SATA/PATA interface so we leave the default pointer alone and
        // don't touch the drive info that was filled in by the SCSI commands since that is how the OS talks to it for
        // read/write and we don't want to disrupt that Everything else is some sort of SAT interface (UDS, SAS,
        // IEEE1394, etc) so we want to fill in bridge info here
        if ((device->drive_info.interface_type != IDE_INTERFACE) &&
            (device->drive_info.interface_type != RAID_INTERFACE))
        {
            device->drive_info.bridge_info.isValid = true;
            fillWWN                                = &device->drive_info.bridge_info.childWWN;
            fillLogicalSectorSize                  = &device->drive_info.bridge_info.childDeviceBlockSize;
            fillPhysicalSectorSize                 = &device->drive_info.bridge_info.childDevicePhyBlockSize;
            fillSectorAlignment                    = &device->drive_info.bridge_info.childSectorAlignment;
            fillMaxLba                             = &device->drive_info.bridge_info.childDeviceMaxLba;
        }
        // this will catch all IDE interface devices to set a vendor identification IF one is not already set
        else if (safe_strlen(device->drive_info.T10_vendor_ident) == 0)
        {
            // vendor ID is not set, so set it to ATA like SAT spec
            device->drive_info.T10_vendor_ident[0] = 'A';
            device->drive_info.T10_vendor_ident[1] = 'T';
            device->drive_info.T10_vendor_ident[2] = 'A';
            device->drive_info.T10_vendor_ident[3] = 0;
            device->drive_info.T10_vendor_ident[4] = 0;
            device->drive_info.T10_vendor_ident[5] = 0;
            device->drive_info.T10_vendor_ident[6] = 0;
            device->drive_info.T10_vendor_ident[7] = 0;
        }
        device->drive_info.numberOfLUs = 1;

        if ((device->drive_info.interface_type != IDE_INTERFACE) &&
            (device->drive_info.interface_type != RAID_INTERFACE))
        {
            fill_ATA_Strings_From_Identify_Data(identifyData, device->drive_info.bridge_info.childDriveMN,
                                                device->drive_info.bridge_info.childDriveSN,
                                                device->drive_info.bridge_info.childDriveFW);
        }
        else
        {
            fill_ATA_Strings_From_Identify_Data(identifyData, device->drive_info.product_identification,
                                                device->drive_info.serialNumber, device->drive_info.product_revision);
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[47])) && M_Byte0(le16_to_host(ident_word[47])) > 0)
        {
            device->drive_info.ata_Options.readWriteMultipleSupported = true;
        }
        // assume PATA until we get other data to tell us otherwise.
        device->drive_info.ata_Options.isParallelTransport = true;
        device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits =
            false; // for PATA devices, continue setting these bits for backwards compatibility.
        *fillLogicalSectorSize  = LEGACY_DRIVE_SEC_SIZE; // start with this and change later
        *fillPhysicalSectorSize = LEGACY_DRIVE_SEC_SIZE; // start with this and change later
        // clear DMA support until it is found later
        device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_NO_DMA;
        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[49])))
        {
            if (le16_to_host(ident_word[49]) & BIT9)
            {
                lbaModeSupported = true;
            }
            if (le16_to_host(ident_word[49]) & BIT8)
            {
                device->drive_info.ata_Options.dmaSupported = true;
                // do not set DMA mode here. Let other field checks set this.
            }
        }

        bool words64to70Valid = false;
        bool word88Valid      = false;
        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[53])))
        {
            if (le16_to_host(ident_word[53]) & BIT2)
            {
                word88Valid = true;
            }
            if (le16_to_host(ident_word[53]) & BIT1)
            {
                words64to70Valid = true;
            }
            if ((le16_to_host(ident_word[53]) & BIT0) || (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[54])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[55])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[56])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[57])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[58]))))
            {
                // only override if these are non-zero. If all are zero, then we cannot determine the current
                // configuration and should rely on the defaults read earlier. This is being checked again since a
                // device may set bit0 of word 53 meaning this is a valid field. however if the values are zero, we do
                // not want to use them.
                if (le16_to_host(ident_word[54]) > 0 && M_Byte0(le16_to_host(ident_word[55])) > 0 &&
                    M_Byte0(le16_to_host(ident_word[56])) > 0)
                {
                    cylinder = le16_to_host(ident_word[54]);          // word 54
                    head     = M_Byte0(le16_to_host(ident_word[55])); // Word55
                    spt      = M_Byte0(le16_to_host(ident_word[56])); // Word56
                }
            }
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[59])))
        {
            // set the number of logical sectors per DRQ data block (current setting)
            device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock = M_Byte0(le16_to_host(ident_word[59]));
        }
        else
        {
            // if we do not know the current read/write multiple setting, do not use these commands on these really old
            // drives!
            device->drive_info.ata_Options.readWriteMultipleSupported = false;
        }

        // simulate a max LBA into device information
        *fillMaxLba = C_CAST(uint64_t, cylinder) * C_CAST(uint64_t, head) * C_CAST(uint64_t, spt);

        if (lbaModeSupported || (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[60])) ||
                                 is_ATA_Identify_Word_Valid(le16_to_host(ident_word[61]))))
        {
            lbaModeSupported = true; // workaround for some USB devices that do support lbamode as can be seen by
                                     // reading this LBA value
            *fillMaxLba = M_WordsTo4ByteValue(le16_to_host(ident_word[60]), le16_to_host(ident_word[61]));
        }
        else
        {
            device->drive_info.ata_Options.chsModeOnly = true;
        }

        // Word62 has SWDMA, but this is long obsolete. Just use PIO mode instead-TJE
        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[62])))
        {
            // SWDMA
            // uint8_t swdmaSupported = get_8bit_range_uint16(le16_to_host(ident_word[62]), 2, 0);
            uint8_t swdmaSelected = get_8bit_range_uint16(le16_to_host(ident_word[62]), 10, 8);
            if (swdmaSelected)
            {
                device->drive_info.ata_Options.dmaMode      = ATA_DMA_MODE_NO_DMA;
                device->drive_info.ata_Options.dmaSupported = false;
            }
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[63])))
        {
            // MWDMA
            // uint8_t mwdmaSupported = get_8bit_range_uint16(le16_to_host(ident_word[63]), 2, 0);
            uint8_t mwdmaSelected = get_8bit_range_uint16(le16_to_host(ident_word[63]), 10, 8);
            if (mwdmaSelected)
            {
                device->drive_info.ata_Options.dmaMode =
                    ATA_DMA_MODE_MWDMA; // assume this until we find MWDMA or UDMA modes
            }
        }

        bool extendedLBAFieldValid = false;
        if (words64to70Valid && is_ATA_Identify_Word_Valid(le16_to_host(ident_word[69])))
        {
            // DCO DMA
            if (le16_to_host(ident_word[69]) & BIT12)
            {
                device->drive_info.ata_Options.dcoDMASupported = true;
            }
            // set read/write buffer DMA
            if (le16_to_host(ident_word[69]) & BIT11)
            {
                device->drive_info.ata_Options.readBufferDMASupported = true;
            }
            if (le16_to_host(ident_word[69]) & BIT10)
            {
                device->drive_info.ata_Options.writeBufferDMASupported = true;
            }
            // HPA security ext DMA
            if (le16_to_host(ident_word[69]) & BIT9)
            {
                device->drive_info.ata_Options.hpaSecurityExtDMASupported = true;
            }
            if (le16_to_host(ident_word[69]) & BIT8)
            {
                device->drive_info.ata_Options.downloadMicrocodeDMASupported = true;
            }
            if (le16_to_host(ident_word[69]) & BIT3)
            {
                extendedLBAFieldValid = true;
            }
            if (device->drive_info.zonedType != ZONED_TYPE_HOST_MANAGED)
            {
                // zoned capabilities (ACS4)
                device->drive_info.zonedType = C_CAST(uint8_t, le16_to_host(ident_word[69]) & (BIT0 | BIT1));
            }
        }

        // SATA Capabilities (Words 76 & 77)
        if (is_ATA_Identify_Word_Valid_SATA(le16_to_host(ident_word[76])))
        {
            device->drive_info.ata_Options.isParallelTransport              = false;
            device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits = true;
            // check for native command queuing support
            if (le16_to_host(ident_word[76]) & BIT8)
            {
                device->drive_info.ata_Options.nativeCommandQueuingSupported = true;
            }
            if (le16_to_host(device->drive_info.IdentifyData.ata.Word076) & BIT15)
            {
                device->drive_info.ata_Options.sataReadLogDMASameAsPIO = true;
            }
        }
        bool words119to120Valid = false;
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[83])) &&
            is_ATA_Identify_Word_Valid(le16_to_host(ident_word[86])))
        {
            if (le16_to_host(ident_word[86]) & BIT15)
            {
                words119to120Valid = true;
            }
            // check that 48bit is supported
            if (le16_to_host(ident_word[83]) & BIT10)
            {
                device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported = true;
            }
            // check for tagged command queuing support
            if (le16_to_host(ident_word[83]) & BIT1 || le16_to_host(ident_word[86]) & BIT1)
            {
                device->drive_info.ata_Options.taggedCommandQueuingSupported = true;
            }
        }

        bool word84Valid = false;
        bool word87Valid = false;
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[84])))
        {
            word84Valid = true;
        }
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[87])))
        {
            word87Valid = true;
        }

        // GPL support
        if ((word84Valid && le16_to_host(ident_word[84]) & BIT5) ||
            (word87Valid && le16_to_host(ident_word[87]) & BIT5))
        {
            device->drive_info.ata_Options.generalPurposeLoggingSupported = true;
        }

        if ((word84Valid && le16_to_host(ident_word[84]) & BIT8) ||
            (word87Valid && le16_to_host(ident_word[87]) & BIT8))
        {
            // get the WWN
            *fillWWN = M_WordsTo8ByteValue(le16_to_host(ident_word[108]), le16_to_host(ident_word[109]),
                                           le16_to_host(ident_word[110]), le16_to_host(ident_word[111]));
        }
        else
        {
            *fillWWN = 0;
        }

        // check for UDMA support
        if (word88Valid && is_ATA_Identify_Word_Valid(le16_to_host(ident_word[88])) &&
            le16_to_host(ident_word[88]) & 0x007F)
        {
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_UDMA;
        }

        // another check to make sure we've identified device 1 correctly
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[93])))
        {
            if (get_8bit_range_uint16(le16_to_host(ident_word[93]), 12, 8) > 0 && le16_to_host(ident_word[93]) & BIT8)
            {
                device->drive_info.ata_Options.isDevice1 = true;
            }
        }

        if (lbaModeSupported && *fillMaxLba >= MAX_28BIT)
        {
            // max LBA from other words since 28bit max field is maxed out
            // check words 100-103 are valid values
            if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[100])) ||
                is_ATA_Identify_Word_Valid(le16_to_host(ident_word[101])) ||
                is_ATA_Identify_Word_Valid(le16_to_host(ident_word[102])) ||
                is_ATA_Identify_Word_Valid(le16_to_host(ident_word[103])))
            {
                *fillMaxLba = M_WordsTo8ByteValue(le16_to_host(ident_word[103]), le16_to_host(ident_word[102]),
                                                  le16_to_host(ident_word[101]), le16_to_host(ident_word[100]));
            }
        }

        // get the sector sizes from the identify data
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                le16_to_host(ident_word[106]))) // making sure this word has valid data
        {
            // word 117 is only valid when word 106 bit 12 is set
            if ((le16_to_host(ident_word[106]) & BIT12) == BIT12)
            {
                *fillLogicalSectorSize =
                    M_WordsTo4ByteValue(le16_to_host(ident_word[117]), le16_to_host(ident_word[118]));
                *fillLogicalSectorSize *= 2; // convert to words to bytes
            }
            else // means that logical sector size is 512bytes
            {
                *fillLogicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
            }
            if ((le16_to_host(ident_word[106]) & BIT13) == 0)
            {
                *fillPhysicalSectorSize = *fillLogicalSectorSize;
            }
            else // multiple logical sectors per physical sector
            {
                uint8_t sectorSizeExponent = UINT8_C(0);
                // get the number of logical blocks per physical blocks
                sectorSizeExponent      = le16_to_host(ident_word[106]) & 0x000F;
                *fillPhysicalSectorSize = C_CAST(uint32_t, *fillLogicalSectorSize * power_Of_Two(sectorSizeExponent));
            }
        }

        if (words119to120Valid && is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[119])) &&
            is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[120])))
        {
            if (le16_to_host(ident_word[120]) & BIT6) // word120 holds if this is enabled
            {
                device->drive_info.ata_Options.senseDataReportingEnabled = true;
            }
            // Determine if read/write log ext DMA commands are supported
            if (le16_to_host(ident_word[119]) & BIT3 || le16_to_host(ident_word[120]) & BIT3)
            {
                device->drive_info.ata_Options.readLogWriteLogDMASupported = true;
            }
            if (le16_to_host(ident_word[119]) & BIT2 || le16_to_host(ident_word[120]) & BIT2)
            {
                device->drive_info.ata_Options.writeUncorrectableExtSupported = true;
            }
        }

        // get the sector alignment
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[209])))
        {
            // bits 13:0 are valid for alignment. bit 15 will be 0 and bit 14 will be 1. remove bit 14 with an xor
            *fillSectorAlignment = le16_to_host(ident_word[209]) ^ BIT14;
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[217])) && le16_to_host(ident_word[217]) == 0x0001)
        {
            device->drive_info.media_type = MEDIA_SSD;
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[222])))
        {
            // check if the device is parallel or serial
            uint8_t transportType =
                C_CAST(uint8_t, (le16_to_host(ident_word[222]) & (BIT15 | BIT14 | BIT13 | BIT12)) >> 12);
            switch (transportType)
            {
            case 0x00: // parallel
                break;
            case 0x01: // serial
            case 0x0E: // PCIe
                device->drive_info.ata_Options.isParallelTransport              = false;
                device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits = true;
                break;
            default:
                break;
            }
        }

        // maxLBA
        // acs4 - word 69 bit3 means extended number of user addressable sectors word supported (words 230 - 233) (Use
        // this to get the max LBA since words 100 - 103 may only contain a value of FFFF_FFFF)
        if (extendedLBAFieldValid)
        {
            *fillMaxLba = M_WordsTo8ByteValue(le16_to_host(ident_word[233]), le16_to_host(ident_word[232]),
                                              le16_to_host(ident_word[231]), le16_to_host(ident_word[230]));
        }
        if (*fillMaxLba > 0 && !device->drive_info.ata_Options.chsModeOnly)
        {
            *fillMaxLba -= 1;
        }

        // Special case for SSD detection. One of these SSDs didn't set the media_type to SSD
        // but it is an SSD. So this match will catch it when this happens. It should be uncommon to find though -TJE
        if (device->drive_info.media_type != MEDIA_SSD &&
            safe_strlen(device->drive_info.bridge_info.childDriveMN) > 0 &&
            (strstr(device->drive_info.bridge_info.childDriveMN, "Seagate SSD") != M_NULLPTR) &&
            safe_strlen(device->drive_info.bridge_info.childDriveFW) > 0 &&
            (strstr(device->drive_info.bridge_info.childDriveFW, "UHFS") != M_NULLPTR))
        {
            device->drive_info.media_type = MEDIA_SSD;
        }

        if (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA &&
            device->drive_info.ata_Options.dmaSupported && device->drive_info.ata_Options.isParallelTransport)
        {
            if (device->drive_info.ata_Options.isParallelTransport)
            {
                // most likely some old drive with SWDMA only. Disable DMA mode commands just to be sure everything
                // works as expected.-TJE
                device->drive_info.ata_Options.dmaSupported                    = false;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
            }
            else
            {
                // weird case as all SATA will support DMA...so set the lowest DMA mode...should be compatible as any
                // incompatible translators will retry and turn this off
                device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
            }
        }

        if (device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported)
        {
            // turn off the DMA supported bits so upper layers issue PIO mode commands instead.
            device->drive_info.ata_Options.dmaMode                       = ATA_DMA_MODE_NO_DMA;
            device->drive_info.ata_Options.dmaSupported                  = false;
            device->drive_info.ata_Options.readLogWriteLogDMASupported   = false;
            device->drive_info.ata_Options.readBufferDMASupported        = false;
            device->drive_info.ata_Options.writeBufferDMASupported       = false;
            device->drive_info.ata_Options.downloadMicrocodeDMASupported = false;
            device->drive_info.ata_Options.sataReadLogDMASameAsPIO       = false;
            device->drive_info.ata_Options.dcoDMASupported               = false;
            device->drive_info.ata_Options.hpaSecurityExtDMASupported    = false;
        }

        // This is to detect realtek USB to NVMe device since it will respond to SAT ATA identify commands with valid
        // strings and NOTHING else. If it has a SATA drive, these will all report DMA mode of some kind and a maxLBA
        // and will never be ATAPI So this should be a reasonably good check to catch this thing for now.
        if (device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe &&
            (!device->drive_info.ata_Options.dmaSupported &&
             device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA && *fillMaxLba == 0 &&
             device->drive_info.drive_type != ATAPI_DRIVE))
        {
            // This means it's an emulated NVMe device where only the MN/SN/FW were reported.
            device->drive_info.drive_type = SCSI_DRIVE;
        }
        else if ((!device->drive_info.ata_Options.dmaSupported &&
                  device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_NO_DMA && *fillMaxLba == 0 &&
                  device->drive_info.drive_type != ATAPI_DRIVE))
        {
            // This very likely is emulated since a valid ATA device will have fillMaxLba set to SOMETHING even in
            // really old CHS drives since the LBA is simulated in software.
            if (device->deviceVerbosity <= VERBOSITY_DEFAULT)
            {
                printf("WARNING: possible RTL 9210 detected and missed with all other checks.\n");
                printf("         this may cause adverse behavior and require --forceSCSI\n");
            }
        }

        if (device->drive_info.interface_type == SCSI_INTERFACE)
        {
            // for the SCSI interface, copy this information back to the main drive info since SCSI translated info may
            // truncate these fields and we don't want that
            safe_memcpy(device->drive_info.product_identification, MODEL_NUM_LEN + 1,
                        device->drive_info.bridge_info.childDriveMN, MODEL_NUM_LEN);
            safe_memcpy(device->drive_info.serialNumber, SERIAL_NUM_LEN + 1,
                        device->drive_info.bridge_info.childDriveSN, SERIAL_NUM_LEN);
            safe_memcpy(device->drive_info.product_revision, FW_REV_LEN + 1,
                        device->drive_info.bridge_info.childDriveFW, FW_REV_LEN);
            device->drive_info.worldWideName = device->drive_info.bridge_info.childWWN;
        }
    }
    else
    {
        ret = FAILURE;
    }
    // This may not be required...need to do some testing to see if reading the logs below wakes a drive up - TJE
    if (M_Word0(device->dFlags) == DO_NOT_WAKE_DRIVE || M_Word0(device->dFlags) == FAST_SCAN)
    {
#ifdef _DEBUG
        printf("Quiting device discovery early for %s per DO_NOT_WAKE_DRIVE\n", device->drive_info.serialNumber);
        printf("Drive type: %d\n", device->drive_info.drive_type);
        printf("Interface type: %d\n", device->drive_info.interface_type);
        printf("Media type: %d\n", device->drive_info.media_type);
        printf("SN: %s\n", device->drive_info.serialNumber);
        printf("%s <--\n", __FUNCTION__);
#endif
        return ret;
    }

    if (!device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported)
    {
        if (device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA)
        {
            // forcing using DMA mode instead of UDMA since the translator doesn't like UDMA mode set
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
        }
    }

    // Check if we were given any force flags regarding how we talk to ATA drives.
    if ((device->dFlags & FORCE_ATA_PIO_ONLY) != 0)
    {
        // turn off the DMA supported bits so upper layers issue PIO mode commands instead.
        device->drive_info.ata_Options.dmaMode                       = ATA_DMA_MODE_NO_DMA;
        device->drive_info.ata_Options.dmaSupported                  = false;
        device->drive_info.ata_Options.readLogWriteLogDMASupported   = false;
        device->drive_info.ata_Options.readBufferDMASupported        = false;
        device->drive_info.ata_Options.writeBufferDMASupported       = false;
        device->drive_info.ata_Options.downloadMicrocodeDMASupported = false;
        device->drive_info.ata_Options.sataReadLogDMASameAsPIO       = false;
        device->drive_info.ata_Options.dcoDMASupported               = false;
        device->drive_info.ata_Options.hpaSecurityExtDMASupported    = false;
    }
    // check if we're being asked to set the protocol to DMA for DMA commands (default behavior depends on drive support
    // from identify)
    if ((device->dFlags & FORCE_ATA_DMA_SAT_MODE) != 0)
    {
        device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
    }
    // check if we're being asked to set the protocol to UDMA for DMA commands (default behavior depends on drive
    // support from identify)
    if ((device->dFlags & FORCE_ATA_UDMA_SAT_MODE) != 0)
    {
        device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_UDMA;
    }

    // device->drive_info.softSATFlags.senseDataDescriptorFormat = true;//by default software SAT will set this to
    // descriptor format so that ATA pass-through works as expected with RTFRs. only bother reading logs if GPL is
    // supported...not going to bother with SMART even though some of the things we are looking for are in SMART - TJE
    if (ret == SUCCESS && device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, logBuffer, ATA_LOG_PAGE_LEN_BYTES);
        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DIRECTORY, 0, logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0))
        {
            bool readIDDataLog           = false;
            bool readDeviceStatisticsLog = false;
            // check for support of ID Data Log, Current Device Internal Status, Saved Device Internal Status, Device
            // Statistics Log
            if (get_ATA_Log_Size_From_Directory(logBuffer, ATA_LOG_PAGE_LEN_BYTES, ATA_LOG_DEVICE_STATISTICS) > 0)
            {
                readDeviceStatisticsLog = true;
            }
            if (get_ATA_Log_Size_From_Directory(logBuffer, ATA_LOG_PAGE_LEN_BYTES,
                                                ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG) > 0)
            {
                device->drive_info.softSATFlags.currentInternalStatusLogSupported = true;
            }
            if (get_ATA_Log_Size_From_Directory(logBuffer, ATA_LOG_PAGE_LEN_BYTES,
                                                ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG) > 0)
            {
                device->drive_info.softSATFlags.savedInternalStatusLogSupported = true;
            }
            if (get_ATA_Log_Size_From_Directory(logBuffer, ATA_LOG_PAGE_LEN_BYTES, ATA_LOG_IDENTIFY_DEVICE_DATA) > 0)
            {
                readIDDataLog = true;
            }
            // could check any log from address 80h through 9Fh...but one should be enough (used for SAT application
            // client log page translation) Using 90h since that is the first page the application client translation
            // uses.

            if (get_ATA_Log_Size_From_Directory(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0x90) > 0)
            {
                device->drive_info.softSATFlags.hostLogsSupported = true;
            }
            // now read the couple pages of logs we care about to set some more flags for software SAT
            if (readIDDataLog)
            {
                bool copyOfIDData          = false;
                bool supportedCapabilities = false;
                bool zonedDeviceInfo       = false;
                safe_memset(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA,
                                                         ATA_ID_DATA_LOG_SUPPORTED_PAGES, logBuffer,
                                                         ATA_LOG_PAGE_LEN_BYTES, 0))
                {
                    uint8_t  pageNumber = logBuffer[2];
                    uint16_t revision   = M_BytesTo2ByteValue(logBuffer[1], logBuffer[0]);
                    if (pageNumber == C_CAST(uint8_t, ATA_ID_DATA_LOG_SUPPORTED_PAGES) &&
                        revision >= ATA_ID_DATA_VERSION_1)
                    {
                        // data is valid, so figure out supported pages
                        uint8_t listLen = logBuffer[ATA_ID_DATA_SUP_PG_LIST_LEN_OFFSET];
                        for (uint16_t iter = ATA_ID_DATA_SUP_PG_LIST_OFFSET;
                             iter < C_CAST(uint16_t, listLen + ATA_ID_DATA_SUP_PG_LIST_OFFSET) && iter < UINT16_C(512);
                             ++iter)
                        {
                            switch (logBuffer[iter])
                            {
                            case ATA_ID_DATA_LOG_SUPPORTED_PAGES:
                                break;
                            case ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA:
                                copyOfIDData = true;
                                break;
                            case ATA_ID_DATA_LOG_CAPACITY:
                                break;
                            case ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES:
                                supportedCapabilities = true;
                                break;
                            case ATA_ID_DATA_LOG_CURRENT_SETTINGS:
                            case ATA_ID_DATA_LOG_ATA_STRINGS:
                            case ATA_ID_DATA_LOG_SECURITY:
                            case ATA_ID_DATA_LOG_PARALLEL_ATA:
                            case ATA_ID_DATA_LOG_SERIAL_ATA:
                                break;
                            case ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION:
                                zonedDeviceInfo = true;
                                break;
                            default:
                                break;
                            }
                        }
                    }
                }
                safe_memset(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                if (copyOfIDData && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA,
                                                                         ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA,
                                                                         logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0))
                {
                    device->drive_info.softSATFlags.identifyDeviceDataLogSupported = true;
                }
                safe_memset(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                if (supportedCapabilities &&
                    SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA,
                                                         ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, logBuffer,
                                                         ATA_LOG_PAGE_LEN_BYTES, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4],
                                                          logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT &&
                        M_Byte2(qword0) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES &&
                        M_Word0(qword0) >= ATA_ID_DATA_VERSION_1)
                    {
                        uint64_t downloadCapabilities;
                        uint64_t supportedZACCapabilities;
                        uint64_t supportedCapabilitiesQWord =
                            M_BytesTo8ByteValue(logBuffer[15], logBuffer[14], logBuffer[13], logBuffer[12],
                                                logBuffer[11], logBuffer[10], logBuffer[9], logBuffer[8]);
                        if (supportedCapabilitiesQWord & ATA_ID_DATA_QWORD_VALID_BIT)
                        {
                            if (supportedCapabilitiesQWord & BIT51)
                            {
                                device->drive_info.ata_Options.sanitizeOverwriteDefinitiveEndingPattern = true;
                            }
                            if (supportedCapabilitiesQWord & BIT50)
                            {
                                device->drive_info.softSATFlags.dataSetManagementXLSupported = true;
                            }
                            if (supportedCapabilitiesQWord & BIT48)
                            {
                                device->drive_info.softSATFlags.zeroExtSupported = true;
                            }
                        }
                        downloadCapabilities =
                            M_BytesTo8ByteValue(logBuffer[23], logBuffer[22], logBuffer[21], logBuffer[20],
                                                logBuffer[19], logBuffer[18], logBuffer[17], logBuffer[16]);
                        if (downloadCapabilities & ATA_ID_DATA_QWORD_VALID_BIT && downloadCapabilities & BIT34)
                        {
                            device->drive_info.softSATFlags.deferredDownloadSupported = true;
                        }
                        supportedZACCapabilities =
                            M_BytesTo8ByteValue(logBuffer[119], logBuffer[118], logBuffer[117], logBuffer[116],
                                                logBuffer[115], logBuffer[114], logBuffer[113], logBuffer[112]);
                        if (supportedZACCapabilities & ATA_ID_DATA_QWORD_VALID_BIT) // qword valid
                        {
                            // check if any of the ZAC commands are supported.
                            if (supportedZACCapabilities & BIT0 || supportedZACCapabilities & BIT1 ||
                                supportedZACCapabilities & BIT2 || supportedZACCapabilities & BIT3 ||
                                supportedZACCapabilities & BIT4)
                            {
                                // according to what I can find in the spec, a HOST Managed drive reports a different
                                // signature, but doens't set any identify bytes like a host aware drive. because of
                                // this and not being able to get the real signature, this check is the only way to
                                // determine we are talking to an ATA host managed drive. - TJE
                                if (device->drive_info.zonedType == ZONED_TYPE_NOT_ZONED)
                                {
                                    device->drive_info.zonedType = ZONED_TYPE_HOST_MANAGED;
                                }
                            }
                        }
                    }
                }
                safe_memset(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                if (zonedDeviceInfo && SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_IDENTIFY_DEVICE_DATA,
                                                                            ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION,
                                                                            logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0))
                {
                    uint64_t qword0 = M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4],
                                                          logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                    if (qword0 & ATA_ID_DATA_QWORD_VALID_BIT &&
                        M_Byte2(qword0) == ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION &&
                        M_Word0(qword0) >= ATA_ID_DATA_VERSION_1) // validating we got the right page
                    {
                        // according to what I can find in the spec, a HOST Managed drive reports a different signature,
                        // but doens't set any identify bytes like a host aware drive. because of this and not being
                        // able to get the real signature, this check is the only way to determine we are talking to an
                        // ATA host managed drive. - TJE
                        if (device->drive_info.zonedType == ZONED_TYPE_NOT_ZONED)
                        {
                            device->drive_info.zonedType = ZONED_TYPE_HOST_MANAGED;
                        }
                    }
                }
            }
            if (readDeviceStatisticsLog)
            {
                safe_memset(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_LIST,
                                                         logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0))
                {
                    uint16_t iter            = ATA_DEV_STATS_SUP_PG_LIST_OFFSET;
                    uint8_t  numberOfEntries = logBuffer[ATA_DEV_STATS_SUP_PG_LIST_LEN_OFFSET];
                    for (iter = ATA_DEV_STATS_SUP_PG_LIST_OFFSET;
                         iter < (numberOfEntries + ATA_DEV_STATS_SUP_PG_LIST_OFFSET) && iter < ATA_LOG_PAGE_LEN_BYTES;
                         ++iter)
                    {
                        switch (logBuffer[iter])
                        {
                        case ATA_DEVICE_STATS_LOG_LIST:
                            break;
                        case ATA_DEVICE_STATS_LOG_GENERAL:
                            device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_FREE_FALL:
                            break;
                        case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA:
                            device->drive_info.softSATFlags.deviceStatsPages.rotatingMediaStatisticsPageSupported =
                                true;
                            break;
                        case ATA_DEVICE_STATS_LOG_GEN_ERR:
                            device->drive_info.softSATFlags.deviceStatsPages.generalErrorStatisticsSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_TEMP:
                            device->drive_info.softSATFlags.deviceStatsPages.temperatureStatisticsSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_TRANSPORT:
                            break;
                        case ATA_DEVICE_STATS_LOG_SSD:
                            device->drive_info.softSATFlags.deviceStatsPages.solidStateDeviceStatisticsSupported = true;
                            break;
                        default:
                            break;
                        }
                    }
                    if (device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported)
                    {
                        // need to read this page and check if the data and time timestamp statistic is supported
                        safe_memset(logBuffer, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                        if (SUCCESS == send_ATA_Read_Log_Ext_Cmd(device, ATA_LOG_DEVICE_STATISTICS,
                                                                 ATA_DEVICE_STATS_LOG_GENERAL, logBuffer,
                                                                 ATA_LOG_PAGE_LEN_BYTES, 0))
                        {
                            uint64_t qword0 =
                                M_BytesTo8ByteValue(logBuffer[7], logBuffer[6], logBuffer[5], logBuffer[4],
                                                    logBuffer[3], logBuffer[2], logBuffer[1], logBuffer[0]);
                            if (M_Byte2(qword0) == ATA_DEVICE_STATS_LOG_GENERAL &&
                                M_Word0(qword0) >= ATA_DEV_STATS_VERSION_1) // validating we got the right page
                            {
                                uint64_t dateAndTime =
                                    M_BytesTo8ByteValue(logBuffer[63], logBuffer[62], logBuffer[61], logBuffer[60],
                                                        logBuffer[59], logBuffer[58], logBuffer[57], logBuffer[56]);
                                if (dateAndTime & ATA_DEV_STATS_STATISTIC_SUPPORTED_BIT)
                                {
                                    device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported =
                                        true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
#ifdef _DEBUG
    printf("Drive type: %d\n", device->drive_info.drive_type);
    printf("Interface type: %d\n", device->drive_info.interface_type);
    printf("Media type: %d\n", device->drive_info.media_type);
    printf("SN: %s\n", device->drive_info.serialNumber);
    printf("%s <--\n", __FUNCTION__);
#endif
    return ret;
}

uint16_t ata_Is_Extended_Power_Conditions_Feature_Supported(uint16_t* pIdentify)
{
    ptAtaIdentifyData pIdent = C_CAST(ptAtaIdentifyData, pIdentify);
    // BIT7 according to ACS 3 rv 5 for EPC
    return (pIdent->Word119 & BIT7);
}

uint16_t ata_Is_One_Extended_Power_Conditions_Feature_Supported(uint16_t* pIdentify)
{
    ptAtaIdentifyData pIdent = C_CAST(ptAtaIdentifyData, pIdentify);
    return (pIdent->Word120 & BIT7);
}

void print_Verbose_ATA_Command_Information(ataPassthroughCommand* ataCommandOptions)
{
    printf("Sending SAT ATA Pass-Through Command:\n");
    // protocol
    printf("\tProtocol: ");
    switch (ataCommandOptions->commadProtocol)
    {
    case ATA_PROTOCOL_PIO:
        printf("PIO");
        break;
    case ATA_PROTOCOL_DMA:
        printf("DMA");
        break;
    case ATA_PROTOCOL_NO_DATA:
        printf("NON-Data");
        break;
    case ATA_PROTOCOL_DEV_RESET:
        printf("Device Reset");
        break;
    case ATA_PROTOCOL_DEV_DIAG:
        printf("Device Diagnostic");
        break;
    case ATA_PROTOCOL_DMA_QUE:
        printf("DMA Queued");
        break;
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PACKET_DMA:
        printf("Packet");
        break;
    case ATA_PROTOCOL_DMA_FPDMA:
        printf("FPDMA");
        break;
    case ATA_PROTOCOL_SOFT_RESET:
        printf("Soft Reset");
        break;
    case ATA_PROTOCOL_HARD_RESET:
        printf("Hard Reset");
        break;
    case ATA_PROTOCOL_RET_INFO:
        printf("Return Response Information");
        break;
    case ATA_PROTOCOL_UDMA:
        printf("UDMA");
        break;
    default:
        break;
    }
    printf("\n");
    printf("\tData Direction: ");
    // Data Direction:
    switch (ataCommandOptions->commandDirection)
    {
    case XFER_NO_DATA:
        printf("No Data");
        break;
    case XFER_DATA_IN:
        printf("Data In");
        break;
    case XFER_DATA_OUT:
        printf("Data Out");
        break;
    default:
        printf("Unknown");
        break;
    }
    printf("\n");
    // TFRs:
    printf("\tTask File Registers:\n");
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[FeatureExt] = %02" PRIX8 "h\n", ataCommandOptions->tfr.Feature48);
    }
    printf("\t[Feature] = %02" PRIX8 "h\n", ataCommandOptions->tfr.ErrorFeature);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[CountExt] = %02" PRIX8 "h\n", ataCommandOptions->tfr.SectorCount48);
    }
    printf("\t[Count] = %02" PRIX8 "h\n", ataCommandOptions->tfr.SectorCount);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[LBA Lo Ext] = %02" PRIX8 "h\n", ataCommandOptions->tfr.LbaLow48);
    }
    printf("\t[LBA Lo] = %02" PRIX8 "h\n", ataCommandOptions->tfr.LbaLow);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[LBA Mid Ext] = %02" PRIX8 "h\n", ataCommandOptions->tfr.LbaMid48);
    }
    printf("\t[LBA Mid] = %02" PRIX8 "h\n", ataCommandOptions->tfr.LbaMid);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[LBA Hi Ext] = %02" PRIX8 "h\n", ataCommandOptions->tfr.LbaHi48);
    }
    printf("\t[LBA Hi] = %02" PRIX8 "h\n", ataCommandOptions->tfr.LbaHi);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        // AUX and ICC registers
        printf("\t[ICC] = %02" PRIX8 "h\n", ataCommandOptions->tfr.icc);
        printf("\t[Aux (7:0)] = %02" PRIX8 "h\n", ataCommandOptions->tfr.aux1);
        printf("\t[Aux (15:8)] = %02" PRIX8 "h\n", ataCommandOptions->tfr.aux2);
        printf("\t[Aux (23:16)] = %02" PRIX8 "h\n", ataCommandOptions->tfr.aux3);
        printf("\t[Aux (31:24)] = %02" PRIX8 "h\n", ataCommandOptions->tfr.aux4);
    }
    printf("\t[DeviceHead] = %02" PRIX8 "h\n", ataCommandOptions->tfr.DeviceHead);
    printf("\t[Command] = %02" PRIX8 "h\n", ataCommandOptions->tfr.CommandStatus);
    // printf("\t[Device Control] = %02"PRIX8"h\n", ataCommandOptions->tfr.DeviceControl);
    printf("\n");
}

// is this a read/write command that the rtfr output needs to know might report a bit specific to these commands
// ex: corrected data (old) or alignment error or address mark not found (old)
// NOTE: Streaming cmds not included in here!
// This is only meant for use printing out error bits at this time!
static bool is_User_Data_Access_Command(ataPassthroughCommand* ataCommandOptions)
{
    bool userDataAccess = false;
    if (ataCommandOptions)
    {
        switch (ataCommandOptions->tfr.CommandStatus)
        {
        case ATA_READ_SECT:
        case ATA_READ_SECT_NORETRY:
        case ATA_READ_SECT_EXT:
        case ATA_READ_DMA_EXT:
        case ATA_READ_DMA_QUE_EXT:
        case ATA_READ_READ_MULTIPLE_EXT:
        case ATA_WRITE_SECT:
        case ATA_WRITE_SECT_NORETRY:
        case ATA_WRITE_SECT_EXT:
        case ATA_WRITE_DMA_EXT:
        case ATA_WRITE_DMA_QUE_EXT:
        case ATA_WRITE_MULTIPLE_EXT:
        case ATA_WRITE_SECTV_RETRY: // write & verify...old. ATA, ATA2, ATA3, then obsolete ever since then
        case ATA_WRITE_DMA_FUA_EXT:
        case ATA_WRITE_DMA_QUE_FUA_EXT:
        case ATA_READ_VERIFY_RETRY:
        case ATA_READ_VERIFY_NORETRY:
        case ATA_READ_VERIFY_EXT:
        case ATA_READ_FPDMA_QUEUED_CMD:
        case ATA_WRITE_FPDMA_QUEUED_CMD:
        case ATA_SEEK_CMD:
        case ATA_READ_MULTIPLE_CMD:
        case ATA_WRITE_MULTIPLE_CMD:
        case ATA_READ_DMA_QUEUED_CMD:
        case ATA_READ_DMA_RETRY_CMD:
        case ATA_READ_DMA_NORETRY:
        case ATA_WRITE_DMA_RETRY_CMD:
        case ATA_WRITE_DMA_NORETRY:
        case ATA_WRITE_DMA_QUEUED_CMD:
        case ATA_WRITE_MULTIPLE_FUA_EXT:
            userDataAccess = true;
            break;
        case ATA_READ_BUF_DMA:
            // case ATA_LEGACY_WRITE_SAME:
            // special case: opcode was reused from old write same to read buf DMA
            // check command fields to figure out which command this is.
            // write same was in ATA and ATA2 and obsolete in ATA 3 and retired until reuse for read buf DMA
            if (ataCommandOptions->commandDirection == XFER_DATA_OUT &&
                ataCommandOptions->commadProtocol ==
                    ATA_PROTOCOL_PIO) // this seems like the easiest way to figure out which command it is -TJE
            {
                userDataAccess = true;
            }
            break;
        case ATA_NOP_CMD:
        case ATA_CFA_REQUEST_SENSE:
        case ATASET:
        case ATA_DATA_SET_MANAGEMENT_CMD:
        case ATA_DATA_SET_MANAGEMENT_XL_CMD:
        case ATA_DEV_RESET:
        case ATA_REQUEST_SENSE_DATA:
        case ATA_RECALIBRATE_CMD:
        case ATA_GET_PHYSICAL_ELEMENT_STATUS:
        case ATA_READ_LONG_RETRY_CMD:
        case ATA_READ_LONG_NORETRY:
        case ATA_READ_MAX_ADDRESS_EXT:
        case ATA_READ_STREAM_DMA_EXT:
        case ATA_READ_STREAM_EXT:
        case ATA_READ_LOG_EXT:
        case ATA_WRITE_LONG_RETRY_CMD:
        case ATA_WRITE_LONG_NORETRY:
        case ATA_SET_MAX_EXT:
        case ATA_CFA_WRITE_SECTORS_WITHOUT_ERASE:
        case ATA_WRITE_STREAM_DMA_EXT:
        case ATA_WRITE_STREAM_EXT:
        case ATA_WRITE_LOG_EXT_CMD:
        case ATA_ZEROS_EXT:
        case ATA_WRITE_UNCORRECTABLE_EXT:
        case ATA_READ_LOG_EXT_DMA:
        case ATA_ZONE_MANAGEMENT_IN:
        case ATA_FORMAT_TRACK_CMD:
        case ATA_CONFIGURE_STREAM:
        case ATA_WRITE_LOG_EXT_DMA:
        case ATA_TRUSTED_NON_DATA:
        case ATA_TRUSTED_RECEIVE:
        case ATA_TRUSTED_RECEIVE_DMA:
        case ATA_TRUSTED_SEND:
        case ATA_TRUSTED_SEND_DMA:
        case ATA_FPDMA_NON_DATA:
        case ATA_SEND_FPDMA:
        case ATA_RECEIVE_FPDMA:
        case ATA_SET_DATE_AND_TIME_EXT:
        case ATA_ACCESSABLE_MAX_ADDR:
        case ATA_REMOVE_AND_TRUNCATE:
        case ATA_RESTORE_AND_REBUILD:
        case ATA_CFA_TRANSLATE_SECTOR:
        case ATA_EXEC_DRV_DIAG:
        case ATA_INIT_DRV_PARAM:
        case ATA_DOWNLOAD_MICROCODE_CMD:
        case ATA_DOWNLOAD_MICROCODE_DMA:
        case ATA_LEGACY_ALT_STANDBY_IMMEDIATE:
        case ATA_LEGACY_ALT_IDLE_IMMEDIATE:
        case ATA_LEGACY_ALT_STANDBY:
        case ATA_LEGACY_ALT_IDLE:
        case ATA_LEGACY_ALT_CHECK_POWER_MODE:
        case ATA_LEGACY_ALT_SLEEP:
        case ATA_ZONE_MANAGEMENT_OUT:
        case ATAPI_COMMAND:
        case ATAPI_IDENTIFY:
        case ATA_SMART_CMD:
        case ATA_DCO:
        case ATA_SET_SECTOR_CONFIG_EXT:
        case ATA_SANITIZE:
        case ATA_NV_CACHE:
        case ATA_CFA_EXTENDED_IDENTIFY:
        case ATA_CFA_KEY_MANAGEMENT:
        case ATA_CFA_STREAMING_PERFORMANCE:
        case ATA_CFA_ERASE_SECTORS:
        case ATA_SET_MULTIPLE:
        case ATA_CFA_WRITE_MULTIPLE_WITHOUT_ERASE:
        case ATA_GET_MEDIA_STATUS:
        case ATA_ACK_MEDIA_CHANGE:
        case ATA_POST_BOOT:
        case ATA_PRE_BOOT:
        case ATA_DOOR_LOCK_CMD:
        case ATA_DOOR_UNLOCK_CMD:
        case ATA_STANDBY_IMMD:
        case ATA_IDLE_IMMEDIATE_CMD:
        case ATA_STANDBY_CMD:
        case ATA_IDLE_CMD:
        case ATA_READ_BUF:
        case ATA_CHECK_POWER_MODE_CMD:
        case ATA_SLEEP_CMD:
        case ATA_FLUSH_CACHE_CMD:
        case ATA_WRITE_BUF:
        case ATA_FLUSH_CACHE_EXT:
        case ATA_WRITE_BUF_DMA:
        case ATA_IDENTIFY:
        case ATA_MEDIA_EJECT:
        case ATA_IDENTIFY_DMA:
        case ATA_SET_FEATURE:
        case ATA_SECURITY_SET_PASS:
        case ATA_SECURITY_UNLOCK_CMD:
        case ATA_SECURITY_ERASE_PREP:
        case ATA_SECURITY_ERASE_UNIT_CMD:
        case ATA_SECURITY_FREEZE_LOCK_CMD: // also CFA wear level, but both of these will be false
        case ATA_SECURITY_DISABLE_PASS:
        case ATA_LEGACY_TRUSTED_RECEIVE:
        case ATA_READ_MAX_ADDRESS:
        case ATA_SET_MAX:
        case ATA_LEGACY_TRUSTED_SEND:
        case ATA_SEEK_EXT: // this is VU so not including with above commands
            break;
        }
    }
    return userDataAccess;
}

static bool is_Streaming_Command(ataPassthroughCommand* ataCommandOptions)
{
    bool isStreaming = false;
    if (ataCommandOptions)
    {
        switch (ataCommandOptions->tfr.CommandStatus)
        {
        case ATA_READ_STREAM_DMA_EXT:
        case ATA_READ_STREAM_EXT:
        case ATA_WRITE_STREAM_DMA_EXT:
        case ATA_WRITE_STREAM_EXT:
        case ATA_CONFIGURE_STREAM:
            isStreaming = true;
            break;
        default:
            break;
        }
    }
    return isStreaming;
}

// added interpretation of status and error fields to print out more info and make it easier to interpret results
// This is not perfect, but a good enhancement to the output.
// Note that not all combinations of bits are handled. Specifically some ATAPI specific meanings are not handled right
// now. Support for old commands and old bits may not be fully functional, but it should cover most cases.
// TODO: Better ATAPI support
// TODO: Use supported ATA versions from identify (not just most recent, but anything with a bit set) to help better
// identify some status and error outputs
//       ex: bad block for ATA1, corr for up to ata 3 (or so), etc
void print_Verbose_ATA_Command_Result_Information(ataPassthroughCommand* ataCommandOptions, tDevice* device)
{
    printf("Return Task File Registers:\n");
    printf("\t[Error] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.error);
    if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_ERROR) // assuming NOT a packet command
    {
        // print out error bit meanings
        // bit7 means either bad block (really old - ATA-1) or interface CRC error
        if (ataCommandOptions->rtfr.error & BIT7)
        {
            // CRC error will only be possible to detect with SATA or UDMA transfers
            // NOTE: Since some translators only allow SAT set to DMA, need to make sure drive's mode is UDMA and
            // protocol can be DMA. Not perfect and can be further improved
            if (is_SATA(device) || (device->drive_info.ata_Options.dmaMode == ATA_DMA_MODE_UDMA &&
                                    (ataCommandOptions->commadProtocol == ATA_PROTOCOL_UDMA ||
                                     ataCommandOptions->commadProtocol == ATA_PROTOCOL_DMA)))
            {
                printf("\t\tInterface CRC error\n");
            }
            else if (is_User_Data_Access_Command(ataCommandOptions))
            {
                printf("\t\tBad Block (?)\n");
            }
            else
            {
                printf("\t\tUnknown Error bit 7\n");
            }
        }
        // bit6 means either uncorrectable data or write protected (removable medium)
        if (ataCommandOptions->rtfr.error & BIT5)
        {
            if (is_Removable_Media(device))
            {
                printf("\t\tWrite Protected\n");
            }
            else
            {
                printf("\t\tUncorrectable Data\n");
            }
        }
        // bit5 means media change
        if (ataCommandOptions->rtfr.error & ATA_ERROR_BIT_MEDIA_CHANGE) // atapi only
        {
            printf("\t\tMedia Change\n");
        }
        // bit 4 means id not found
        if (ataCommandOptions->rtfr.error & ATA_ERROR_BIT_ID_NOT_FOUND)
        {
            printf("\t\tID Not Found\n");
        }
        // bit3 means media change request
        if (ataCommandOptions->rtfr.error & ATA_ERROR_BIT_MEDIA_CHANGE_REQUEST) // atapi only
        {
            printf("\t\tMedia Change\n");
        }
        // bit2 means abort
        if (ataCommandOptions->rtfr.error & ATA_ERROR_BIT_ABORT)
        {
            printf("\t\tAbort\n");
        }
        // Bit 1 can mean Track zero not found, end of media, no media
        if (ataCommandOptions->rtfr.error & BIT1)
        {
            if (ataCommandOptions->tfr.CommandStatus == ATA_RECALIBRATE_CMD)
            {
                printf("\t\tTrack 0 not found\n");
            }
            else if (ataCommandOptions->tfr.CommandStatus == ATA_NV_CACHE &&
                     ataCommandOptions->tfr.ErrorFeature == NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET)
            {
                printf("\t\tInsufficient LBA Range Entries Remaining\n");
            }
            else if (is_Removable_Media(device))
            {
                printf("\t\tNo Media\n");
            }
            // atapi = end of media
            else
            {
                printf("\t\tUnknown Error Bit 1\n");
            }
        }
        // bit 0 can mean various things depending on the command that was issued
        if (ataCommandOptions->rtfr.error & BIT0)
        {
            // address mark not found or command completion time out (streaming)
            // address mark not found basically matched ID not found being set...or when it should be set (LBA/CHS user
            // data access commands, but not seek)
            if (is_Streaming_Command(ataCommandOptions))
            {
                printf("\t\tCommand Completion Time out (Streaming)\n");
            }
            // more checks for specific commands here
            // nv cache commands
            else if (ataCommandOptions->tfr.CommandStatus == ATA_NV_CACHE &&
                     ataCommandOptions->tfr.ErrorFeature == NV_ADD_LBAS_TO_NV_CACHE_PINNED_SET)
            {
                printf("\t\tInsufficient NV Cache Space\n");
            }
            else if (ataCommandOptions->tfr.CommandStatus == ATA_NV_CACHE &&
                     ataCommandOptions->tfr.ErrorFeature == NV_REMOVE_LBAS_FROM_NV_CACHE_PINNED_SET)
            {
                printf("\t\tAttempted Partial Range Removal\n");
            }
            else if (is_User_Data_Access_Command(ataCommandOptions) &&
                     ataCommandOptions->tfr.CommandStatus != ATA_SEEK_CMD)
            {
                // if this is also set, for a user data access, possibly the old address mark not found bit
                // not: does not apply to old seek commands
                printf("\t\tAddress Mark Not Found\n");
            }
            // TODO: atapi illegal length indicator and media error
            else
            {
                printf("\t\tUnknown Error Bit 0\n");
            }
        }
    }
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[Count Ext] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.secCntExt);
    }
    printf("\t[Count] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.secCnt);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[LBA Lo Ext] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.lbaLowExt);
    }
    printf("\t[LBA Lo] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.lbaLow);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[LBA Mid Ext] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.lbaMidExt);
    }
    printf("\t[LBA Mid] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.lbaMid);
    if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE ||
        ataCommandOptions->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        printf("\t[LBA Hi Ext] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.lbaHiExt);
    }
    printf("\t[LBA Hi] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.lbaHi);
    printf("\t[Device] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.device);
    printf("\t[Status] = %02" PRIX8 "h\n", ataCommandOptions->rtfr.status);
    // Bit 7 is busy (unlikely to actually see this returned)
    if (ataCommandOptions->commadProtocol != ATA_PROTOCOL_DMA_FPDMA &&
        ataCommandOptions->commadProtocol != ATA_PROTOCOL_DMA_QUE &&
        ataCommandOptions->rtfr.status & ATA_STATUS_BIT_BUSY)
    {
        printf("\t\tBusy\n");
    }
    // Bit 6 is ready
    if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_READY)
    {
        printf("\t\tReady\n");
    }
    // bit5 is device fault, or stream error
    // Stream error only for streaming feature commands and read/write continuos set to 1 in the command
    if ((ataCommandOptions->tfr.CommandStatus == ATA_WRITE_STREAM_DMA_EXT ||
         ataCommandOptions->tfr.CommandStatus == ATA_WRITE_STREAM_EXT ||
         ataCommandOptions->tfr.CommandStatus == ATA_READ_STREAM_DMA_EXT ||
         ataCommandOptions->tfr.CommandStatus == ATA_READ_STREAM_EXT) &&
        ataCommandOptions->tfr.ErrorFeature & BIT6) // read or write continuous must be set!
    {
        printf("\t\tStream Error\n");
    }
    else if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_DEVICE_FAULT)
    {
        printf("\t\tDevice Fault\n");
    }
    // Bit 4 can be seek complete, service (dma queued), or deferred write error
    if (ataCommandOptions->rtfr.status & BIT4)
    {
        if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_DMA_QUE)
        {
            printf("\t\tService\n");
        }
        else if (ataCommandOptions->tfr.CommandStatus == ATA_WRITE_STREAM_DMA_EXT ||
                 ataCommandOptions->tfr.CommandStatus == ATA_WRITE_STREAM_EXT)
        {
            printf("\t\tDeferred Write Error\n");
        }
        else
        {
            printf("\t\tSeek Complete\n");
        }
    }
    if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_DATA_REQUEST)
    {
        printf("\t\tData Request\n");
    }
    // Bit 2 is either corrected data or alignment error.
    //       corrected data only for read user data access
    //       alignment only for writes
    if (ataCommandOptions->rtfr.status & BIT2)
    {
        if (is_User_Data_Access_Command(ataCommandOptions))
        {
            // corr only for reads and alignment only for writes???
            if (ataCommandOptions->commandDirection == XFER_DATA_IN)
            {
                printf("\t\tCorrected Data\n");
            }
            else if (ataCommandOptions->commandDirection == XFER_DATA_OUT)
            {
                // todo: alignment error needs lps misalignment reporting to be supported and error reporting set to 01b
                // or 10b and only on write commands
                printf("\t\tAlignment Error\n");
            }
            else
            {
                printf("\t\tUnknown Status bit 2 (CORR or ALIGNMENT?)\n");
            }
        }
        else
        {
            printf("\t\tUnknown Status bit 2 (CORR or ALIGNMENT?)\n");
        }
    }
    // Bit 1 is either index (flips with rev) or sense data available. Sense data reporting must at least be supported
    // for this one to be useful
    if (ataCommandOptions->rtfr.status & BIT1)
    {
        if (device->drive_info.ata_Options.senseDataReportingEnabled)
        {
            printf("\t\tSense Data Available\n");
        }
        else
        {
            printf("\t\tUnknown Status bit 1\n");
        }
    }
    // Bit 0 is error...or for ATAPI it will be check condition Packet commands only
    if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_ERROR) // assuming not ATAPI
    {
        printf("\t\tError\n");
    }

    printf("\n");
}

uint8_t calculate_ATA_Checksum(const uint8_t* ptrData)
{
    uint32_t checksum = UINT32_C(0);
    uint32_t counter  = UINT32_C(0);

    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return UINT8_C(0);
    }
    RESTORE_NONNULL_COMPARE
    for (counter = 0; counter < 511; ++counter)
    {
        checksum = checksum + ptrData[counter];
    }
    return M_Byte0(checksum); // (~checksum + 1);//return this? or just the checksum?
}

bool is_Checksum_Valid(const uint8_t* ptrData, uint32_t dataSize, uint32_t* firstInvalidSector)
{
    bool     isValid      = false;
    uint32_t checksumCalc = UINT32_C(0);
    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR || firstInvalidSector == M_NULLPTR)
    {
        return false;
    }
    RESTORE_NONNULL_COMPARE
    for (uint32_t blockIter = UINT32_C(0); blockIter < (dataSize / LEGACY_DRIVE_SEC_SIZE); ++blockIter)
    {
        for (uint32_t counter = UINT32_C(0); counter <= 511; ++counter)
        {
            checksumCalc = checksumCalc + ptrData[counter + (blockIter * 512)];
        }
        if (M_Byte0(checksumCalc) == 0)
        {
            isValid = true;
        }
        else
        {
            *firstInvalidSector = blockIter;
            isValid             = false;
            break;
        }
    }
    return isValid;
}

eReturnValues set_ATA_Checksum_Into_Data_Buffer(uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret      = SUCCESS;
    uint32_t      checksum = UINT32_C(0);
    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    for (uint32_t blockIter = UINT32_C(0); blockIter < (dataSize / LEGACY_DRIVE_SEC_SIZE); ++blockIter)
    {
        checksum                 = calculate_ATA_Checksum(&ptrData[blockIter]);
        ptrData[blockIter + 511] = (~M_Byte0(checksum) + UINT8_C(1));
    }
    return ret;
}

bool is_LBA_Mode_Supported(tDevice* device)
{
    bool lbaSupported = true;
    if (!(le16_to_host(device->drive_info.IdentifyData.ata.Word049) & BIT9))
    {
        lbaSupported = false;
    }
    return lbaSupported;
}

bool is_CHS_Mode_Supported(tDevice* device)
{
    bool chsSupported = true;
    // Check words 1, 3, 6
    if (le16_to_host(device->drive_info.IdentifyData.ata.Word001) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word003) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word006) == 0)
    {
        chsSupported = false;
    }

    return chsSupported;
}

static bool is_Current_CHS_Info_Valid(tDevice* device)
{
    bool     chsSupported = true;
    uint8_t* identifyPtr  = M_REINTERPRET_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000);
    uint32_t userAddressableCapacityCHS =
        M_BytesTo4ByteValue(identifyPtr[117], identifyPtr[116], identifyPtr[115], identifyPtr[114]);
    // Check words 1, 3, 6, 54, 55, 56, 58:57 for values
    if (!(le16_to_host(device->drive_info.IdentifyData.ata.Word053) &
          BIT0) || // if this bit is set, then the current fields are valid. If not, they may or may not be valid
        le16_to_host(device->drive_info.IdentifyData.ata.Word001) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word003) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word006) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word054) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word055) == 0 ||
        le16_to_host(device->drive_info.IdentifyData.ata.Word056) == 0 || userAddressableCapacityCHS == 0)
    {
        chsSupported = false;
    }

    return chsSupported;
}

// Code to test LBA to CHS conversion Below
// FILE * chsTest = fopen("chsToLBATest.txt", "w");
// if (chsTest)
//{
//     fprintf(chsTest, "%5s | %2s | %2s - LBA\n", "C", "H", "S");
//     uint32_t cyl = UINT32_C(0);
//     uint16_t head = UINT16_C(0);, sector = 0;
//     for (cyl = 0; cyl < deviceList[deviceIter].drive_info.IdentifyData.ata.Word054; ++cyl)
//     {
//         for (head = 0; head < deviceList[deviceIter].drive_info.IdentifyData.ata.Word055; ++head)
//         {
//             for (sector = 1; sector <= deviceList[deviceIter].drive_info.IdentifyData.ata.Word056; ++sector)
//             {
//                 uint32_t lba = UINT32_C(0);
//                 convert_CHS_To_LBA(&deviceList[deviceIter], cyl, head, sector, &lba);
//                 fprintf(chsTest, "%5" PRIu32 " | %2" PRIu16 " | %2" PRIu16 " - %" PRIu32 "\n", cyl, head, sector,
//                 lba);
//             }
//         }
//     }
//     fflush(chsTest);
//     fclose(chsTest);
// }
//
// FILE *lbaToCHSTest = fopen("lbaToCHSTest.txt", "w");
// if (lbaToCHSTest)
//{
//     fprintf(lbaToCHSTest, "%8s - %5s | %2s | %2s\n", "LBA", "C", "H", "S");
//     for (uint32_t lba = UINT32_C(0); lba < 12706470; ++lba)
//     {
//         uint16_t cylinder = UINT16_C(0);
//         uint8_t head = UINT8_C(0);
//         uint8_t sector = UINT8_C(0);
//         convert_LBA_To_CHS(&deviceList[deviceIter], lba, &cylinder, &head, &sector);
//         fprintf(lbaToCHSTest, "%8" PRIu32 " - %5" PRIu16 " | %2" PRIu8 " | %2" PRIu8 "\n", lba, cylinder, head,
//         sector);
//     }
//     fflush(lbaToCHSTest);
//     fclose(lbaToCHSTest);
// }

// device parameter needed so we can see the current CHS configuration and translate properly...
eReturnValues convert_CHS_To_LBA(tDevice* device, uint16_t cylinder, uint8_t head, uint16_t sector, uint32_t* lba)
{
    eReturnValues ret = SUCCESS;
    DISABLE_NONNULL_COMPARE
    if (lba != M_NULLPTR)
    {
        if (is_CHS_Mode_Supported(device))
        {
            uint16_t headsPerCylinder =
                le16_to_host(device->drive_info.IdentifyData.ata.Word055); // from current ID configuration
            uint16_t sectorsPerTrack =
                le16_to_host(device->drive_info.IdentifyData.ata.Word056); // from current ID configuration
            *lba = UINT32_MAX;
            *lba = ((((C_CAST(uint32_t, cylinder)) * C_CAST(uint32_t, headsPerCylinder)) + C_CAST(uint32_t, head)) *
                    C_CAST(uint32_t, sectorsPerTrack)) +
                   C_CAST(uint32_t, sector) - UINT32_C(1);
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}

eReturnValues convert_LBA_To_CHS(tDevice* device, uint32_t lba, uint16_t* cylinder, uint8_t* head, uint8_t* sector)
{
    eReturnValues ret = SUCCESS;
    lba &= MAX_28_BIT_LBA;
    DISABLE_NONNULL_COMPARE
    if (cylinder != M_NULLPTR && head != M_NULLPTR && sector != M_NULLPTR)
    {
        uint8_t* identifyPtr = M_REINTERPRET_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000);
        uint32_t userAddressableCapacityCHS = M_BytesTo4ByteValue(identifyPtr[117], identifyPtr[116], identifyPtr[115],
                                                                  identifyPtr[114]); // CHS max sector capacity
        if (is_CHS_Mode_Supported(device))
        {
            if (is_Current_CHS_Info_Valid(device))
            {
                uint32_t headsPerCylinder = le16_to_host(device->drive_info.IdentifyData.ata.Word055);
                uint32_t sectorsPerTrack  = le16_to_host(device->drive_info.IdentifyData.ata.Word056);
                *cylinder = C_CAST(uint16_t, lba / C_CAST(uint32_t, headsPerCylinder * sectorsPerTrack));
                *head     = C_CAST(uint8_t, (lba / sectorsPerTrack) % headsPerCylinder);
                *sector   = C_CAST(uint8_t, (lba % sectorsPerTrack) + UINT8_C(1));
                // check that this isn't above the value of words 58:57
                uint32_t currentSector =
                    C_CAST(uint32_t, (*cylinder)) * C_CAST(uint32_t, (*head)) * C_CAST(uint32_t, (*sector));
                if (currentSector > userAddressableCapacityCHS)
                {
                    // change the return value, but leave the calculated values as they are
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                uint32_t headsPerCylinder = le16_to_host(device->drive_info.IdentifyData.ata.Word003);
                uint32_t sectorsPerTrack  = le16_to_host(device->drive_info.IdentifyData.ata.Word006);
                *cylinder = C_CAST(uint16_t, lba / C_CAST(uint32_t, headsPerCylinder * sectorsPerTrack));
                *head     = C_CAST(uint8_t, (lba / sectorsPerTrack) % headsPerCylinder);
                *sector   = C_CAST(uint8_t, (lba % sectorsPerTrack) + UINT8_C(1));
                userAddressableCapacityCHS = C_CAST(uint32_t, device->drive_info.IdentifyData.ata.Word001) *
                                             C_CAST(uint32_t, device->drive_info.IdentifyData.ata.Word003) *
                                             C_CAST(uint32_t, device->drive_info.IdentifyData.ata.Word006);
                // check that this isn't above the value of words 58:57
                uint32_t currentSector =
                    C_CAST(uint32_t, (*cylinder)) * C_CAST(uint32_t, (*head)) * C_CAST(uint32_t, (*sector));
                if (currentSector > userAddressableCapacityCHS)
                {
                    // change the return value, but leave the calculated values as they are
                    ret = NOT_SUPPORTED;
                }
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}
