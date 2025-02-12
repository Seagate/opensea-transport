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

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "prng.h"
#include "sleep.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper_func.h"
#include "platform_helper.h"
#include "sat_helper.h"
#include "sat_helper_func.h"
#include "scsi_helper_func.h"
#include <stdio.h>
#include <string.h>

// the define below is to switch between different levels of SAT spec support. It is recommended that this is set to the
// highest version available valid values are 1 - 4 1 = SAT 2 = SAT2 3 = SAT3 4 = SAT4 This flag is not fully
// implemented at this time, but should be largely functional. All current SAT translation is according to SAT4 (or as
// close as possible)
#define SAT_SPEC_SUPPORTED 4

#if !defined SAT_SPEC_SUPPORTED || SAT_SPEC_SUPPORTED > 4 || SAT_SPEC_SUPPORTED < 1
#    error Invalid SAT_SPEC_SUPPORTED defined in sat_helper.c. Values 1 - 4 are valid. At least 1 SAT spec version should be specified!
#endif

//(1 = on, 0 = off)
// SAT 4 optional feature flags
#define SAT_4_ERROR_HISTORY_FEATURE 1
// SAT 3 optional feature flags
// SAT 2 optional feature flags
// SAT optional feature flags

// TODO: SPC and SBC optional feature flag translations. A good SATL should enable everything to make the drive look
// more and act more like a SCSI device

#if defined(_MSC_VER)
// Visual studio level 4 produces lots of warnings for "assignment within conditional expression" which is normally a
// good warning, but it is used HEAVILY in this file by the software SAT translator to return the field pointer on
// errors. So for VS only, this warning will be disabled in this file.
#    pragma warning(push)
#    pragma warning(disable : 4706)
#endif

eReturnValues get_Return_TFRs_From_Passthrough_Results_Log(tDevice*       device,
                                                           ataReturnTFRs* ataRTFRs,
                                                           uint16_t       parameterCode)
{
    eReturnValues ret              = NOT_SUPPORTED; // Many devices don't support this log page.
    uint8_t*      sense70logBuffer = C_CAST(
        uint8_t*,
        safe_calloc_aligned(14 + LOG_PAGE_HEADER_LENGTH, sizeof(uint8_t),
                                 device->os_info.minimumAlignment)); // allocate a buffer to get the rtfrs in. the size of 12
                                                                // is ATA Passthrough Descriptor + 4byte log page header
    if (!sense70logBuffer)
    {
        perror("Calloc Failure!\n");
        return MEMORY_FAILURE;
    }
    // 0x16 is the ATA-pass through results log..which should have a descriptor format sense data to parse
    if (SUCCESS == scsi_Log_Sense_Cmd(device, false, LPC_CUMULATIVE_VALUES, LP_ATA_PASSTHROUGH_RESULTS, 0,
                                      parameterCode & 0x0F, sense70logBuffer, 14 + LOG_PAGE_HEADER_LENGTH))
    {
        if ((sense70logBuffer[0] & 0x3F) == LP_ATA_PASSTHROUGH_RESULTS) // check that we got the correct page back
        {
            // first parameter starts at byte 4...and since we only requested 1, that should be it
            // 4 bytes after the start of the parameter, we get the values
            // check the descriptor code
            if (sense70logBuffer[LOG_PAGE_HEADER_LENGTH] == SAT_DESCRIPTOR_CODE &&
                sense70logBuffer[1 + LOG_PAGE_HEADER_LENGTH] == SAT_ADDT_DESC_LEN)
            {
                // CHECK THESE OFFSETS!
                ataRTFRs->error  = sense70logBuffer[3 + LOG_PAGE_HEADER_LENGTH];
                ataRTFRs->secCnt = sense70logBuffer[5 + LOG_PAGE_HEADER_LENGTH];
                ataRTFRs->lbaLow = sense70logBuffer[7 + LOG_PAGE_HEADER_LENGTH];
                ataRTFRs->lbaMid = sense70logBuffer[9 + LOG_PAGE_HEADER_LENGTH];
                ataRTFRs->lbaHi  = sense70logBuffer[11 + LOG_PAGE_HEADER_LENGTH];
                ataRTFRs->device = sense70logBuffer[12 + LOG_PAGE_HEADER_LENGTH];
                ataRTFRs->status = sense70logBuffer[13 + LOG_PAGE_HEADER_LENGTH];
                // check the extend bit
                if (sense70logBuffer[10] & BIT0 ||
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit)
                {
                    // copy back the extended registers
                    ataRTFRs->secCntExt = sense70logBuffer[4 + LOG_PAGE_HEADER_LENGTH];
                    ataRTFRs->lbaLowExt = sense70logBuffer[6 + LOG_PAGE_HEADER_LENGTH];
                    ataRTFRs->lbaMidExt = sense70logBuffer[8 + LOG_PAGE_HEADER_LENGTH];
                    ataRTFRs->lbaHiExt  = sense70logBuffer[10 + LOG_PAGE_HEADER_LENGTH];
                }
                else
                {
                    // set the registers to zero
                    ataRTFRs->secCntExt = 0;
                    ataRTFRs->lbaLowExt = 0;
                    ataRTFRs->lbaMidExt = 0;
                    ataRTFRs->lbaHiExt  = 0;
                }
                ret = SUCCESS;
            }
        }
    }
    safe_free_aligned(&sense70logBuffer);
    return ret;
}

eReturnValues get_RTFRs_From_Descriptor_Format_Sense_Data(const uint8_t* ptrSenseData,
                                                          uint32_t       senseDataSize,
                                                          ataReturnTFRs* rtfr)
{
    eReturnValues ret             = FAILURE;
    uint8_t       senseDataFormat = ptrSenseData[0] & 0x7F;
    if (senseDataFormat == SCSI_SENSE_CUR_INFO_DESC || senseDataFormat == SCSI_SENSE_DEFER_ERR_DESC)
    {
        uint32_t descriptorOffset        = SCSI_DESC_FORMAT_DESC_INDEX;
        uint8_t  currentDescriptorLength = ptrSenseData[descriptorOffset + 1] + 2;
        // loop through the descriptors in case we were returned multiple...we only want the ATA pass through
        // descriptor.
        for (descriptorOffset = SCSI_DESC_FORMAT_DESC_INDEX;
             descriptorOffset <
             M_Min(senseDataSize - SCSI_DESC_FORMAT_DESC_INDEX, M_STATIC_CAST(uint32_t, ptrSenseData[7]) + 7);
             descriptorOffset += currentDescriptorLength)
        {
            // set the current descriptor length
            currentDescriptorLength = ptrSenseData[descriptorOffset + 1] + 2;
            // check the descriptor code
            if (ptrSenseData[descriptorOffset] == SAT_DESCRIPTOR_CODE &&
                ptrSenseData[descriptorOffset + 1] == SAT_ADDT_DESC_LEN)
            {
                ret          = SUCCESS;
                rtfr->error  = ptrSenseData[descriptorOffset + 3];
                rtfr->secCnt = ptrSenseData[descriptorOffset + 5];
                rtfr->lbaLow = ptrSenseData[descriptorOffset + 7];
                rtfr->lbaMid = ptrSenseData[descriptorOffset + 9];
                rtfr->lbaHi  = ptrSenseData[descriptorOffset + 11];
                rtfr->device = ptrSenseData[descriptorOffset + 12];
                rtfr->status = ptrSenseData[descriptorOffset + 13];
                // check for the extend bit
                if (ptrSenseData[descriptorOffset + 2] & BIT0)
                {
                    // copy back the extended registers
                    rtfr->secCntExt = ptrSenseData[descriptorOffset + 4];
                    rtfr->lbaLowExt = ptrSenseData[descriptorOffset + 6];
                    rtfr->lbaMidExt = ptrSenseData[descriptorOffset + 8];
                    rtfr->lbaHiExt  = ptrSenseData[descriptorOffset + 10];
                }
                else
                {
                    // set the registers to zero
                    rtfr->secCntExt = 0;
                    rtfr->lbaLowExt = 0;
                    rtfr->lbaMidExt = 0;
                    rtfr->lbaHiExt  = 0;
                }
                break;
            }
            if (currentDescriptorLength == 0)
            {
                break;
            }
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

eReturnValues get_RTFRs_From_Fixed_Format_Sense_Data(tDevice*               device,
                                                     const uint8_t*         ptrSenseData,
                                                     uint32_t               senseDataSize,
                                                     ataPassthroughCommand* ataCmd)
{
    eReturnValues ret             = FAILURE;
    uint8_t       senseDataFormat = ptrSenseData[0] & 0x7F;
    M_USE_UNUSED(senseDataSize);
    if ((senseDataFormat == SCSI_SENSE_CUR_INFO_FIXED || senseDataFormat == SCSI_SENSE_DEFER_ERR_FIXED)
#if !defined(UNALIGNED_WRITE_SENSE_DATA_WORKAROUND)
        && (ptrSenseData[12] == 0x00 && ptrSenseData[13] == 0x1D) // ATA passthrough information available
#endif                                                            //(UNALIGNED_WRITE_SENSE_DATA_WORKAROUND)
    )
    {
        ret = SUCCESS; // assume everything works right now...
        // first check if the returned RTFRs have nonzero ext registers and if a log page is available to read to get
        // them
        bool    extendBitSet        = (ptrSenseData[8] & BIT7) != 0;
        bool    secCntExtNonZero    = (ptrSenseData[8] & BIT6) != 0;
        bool    LBAExtNonZero       = (ptrSenseData[8] & BIT5) != 0;
        bool    unknownExtRegisters = false; // to be used below when deciding to issue additional commands to get rtfrs
        uint8_t resultsLogPageParameterPointer = ptrSenseData[8] & 0x0F;
        // first save whatever we did get back from the sense data based on these flags
        // from fixed format information field
        ataCmd->rtfr.error  = ptrSenseData[3];
        ataCmd->rtfr.status = ptrSenseData[4];
        ataCmd->rtfr.device = ptrSenseData[5];
        ataCmd->rtfr.secCnt = ptrSenseData[6];
        // from fixed format command specific information field
        ataCmd->rtfr.lbaHi  = ptrSenseData[9];
        ataCmd->rtfr.lbaMid = ptrSenseData[10];
        ataCmd->rtfr.lbaLow = ptrSenseData[11];
        // now set the ext registers based on the boolean flags above
        if (extendBitSet)
        {
            if (secCntExtNonZero)
            {
                ataCmd->rtfr.secCntExt = 0xFF;
                unknownExtRegisters    = true;
            }
            else
            {
                ataCmd->rtfr.secCntExt = 0;
            }
            if (LBAExtNonZero)
            {
                ataCmd->rtfr.lbaLowExt = 0xFF;
                ataCmd->rtfr.lbaMidExt = 0xFF;
                ataCmd->rtfr.lbaHiExt  = 0xFF;
                unknownExtRegisters    = true;
            }
            else
            {
                ataCmd->rtfr.lbaLowExt = 0;
                ataCmd->rtfr.lbaMidExt = 0;
                ataCmd->rtfr.lbaHiExt  = 0;
            }
        }
        else
        {
            ataCmd->rtfr.secCntExt = 0;
            ataCmd->rtfr.lbaLowExt = 0;
            ataCmd->rtfr.lbaMidExt = 0;
            ataCmd->rtfr.lbaHiExt  = 0;
        }

#if defined(UNALIGNED_WRITE_SENSE_DATA_WORKAROUND)
        // TODO: Extra validation of the RTFRs
        uint8_t senseKey = M_Nibble0(ptrSenseData[2]);
        uint8_t asc      = ptrSenseData[12];
        uint8_t ascq     = ptrSenseData[13];

        if (asc != 0x00 && ascq != 0x1D) // 00,1D means that this was DEFINITELY related to ATA passthrough results
        {
            // need to do a little validation that the returned registers make some amount of sense
            if (asc == 0x21 && ascq == 0x04) // unaligned write command
            {
                // for unaligned write command, this seems to happen from SATLs when the alignment status bit is set.
                if (senseKey == SENSE_KEY_ILLEGAL_REQUEST)
                {
                    // most likely has good rtfrs, but not in the locations from SAT
                    ataCmd->rtfr.lbaHi     = 0;
                    ataCmd->rtfr.lbaMid    = 0;
                    ataCmd->rtfr.lbaLow    = 0;
                    ataCmd->rtfr.lbaLowExt = 0;
                    ataCmd->rtfr.lbaMidExt = 0;
                    ataCmd->rtfr.lbaHiExt  = 0;
                    // In Linux's LibATA, I have observed these to be returned in these positions.
                    ataCmd->rtfr.status    = ptrSenseData[9];
                    ataCmd->rtfr.error     = ptrSenseData[8];
                    ataCmd->rtfr.secCnt    = ptrSenseData[11];
                    ataCmd->rtfr.secCntExt = ptrSenseData[10]; // This is a guess!
                    ataCmd->rtfr.device    = ptrSenseData[16]; // This is a best guess from observation
                    ataCmd->rtfr.lbaLow    = ptrSenseData[17]; // this is a best guess from observation
                    // Other offsets that MIGHT contain data, but I cannot confirm:
                    // 3, 4, 5, 6, 15
                }
                else if (senseKey == SENSE_KEY_ABORTED_COMMAND)
                {
                    // TODO: check if a valid LBA was returned in information rather than RTFRs
                }
            }
            // THis need to be at the end.
            // if the status is empty, this is going to be treated as though no rtfrs were received from the device
            if (ataCmd->rtfr.status == 0 || !(ataCmd->rtfr.status & ATA_STATUS_BIT_READY))
            {
                ataCmd->rtfr.error     = 0;
                ataCmd->rtfr.status    = 0;
                ataCmd->rtfr.device    = 0;
                ataCmd->rtfr.secCnt    = 0;
                ataCmd->rtfr.lbaHi     = 0;
                ataCmd->rtfr.lbaMid    = 0;
                ataCmd->rtfr.lbaLow    = 0;
                ataCmd->rtfr.secCntExt = 0;
                ataCmd->rtfr.lbaLowExt = 0;
                ataCmd->rtfr.lbaMidExt = 0;
                ataCmd->rtfr.lbaHiExt  = 0;
                unknownExtRegisters    = false;
                ret                    = FAILURE;
            }
        }
#endif // UNALIGNED_WRITE_SENSE_DATA_WORKAROUND

        // now, on the mini D4 firmware (and possibly other satls), the descriptor with the complete rtfrs may also be
        // present in byte 18 onwards, so check if it is there or not to grab the data
        if (ptrSenseData[7] == 24 &&
            unknownExtRegisters) // this length is very specific since it is the normal 10 bytes returned in fixed
                                 // format data + the 14 bytes for the ata return descriptor in SAT
        {
            // check bytes 18 and 19 to make sure they match the descriptor
            uint8_t descriptorOffset = UINT8_C(18); // using this offset variable to make it easier to read and compare
                                                    // to the sat spec offsets for the descriptor
            if (ptrSenseData[descriptorOffset] == SAT_DESCRIPTOR_CODE &&
                ptrSenseData[descriptorOffset + 1] == SAT_ADDT_DESC_LEN)
            {
                ataCmd->rtfr.error  = ptrSenseData[descriptorOffset + 3];
                ataCmd->rtfr.secCnt = ptrSenseData[descriptorOffset + 5];
                ataCmd->rtfr.lbaLow = ptrSenseData[descriptorOffset + 7];
                ataCmd->rtfr.lbaMid = ptrSenseData[descriptorOffset + 9];
                ataCmd->rtfr.lbaHi  = ptrSenseData[descriptorOffset + 11];
                ataCmd->rtfr.device = ptrSenseData[descriptorOffset + 12];
                ataCmd->rtfr.status = ptrSenseData[descriptorOffset + 13];
                // check for the extend bit
                if (ptrSenseData[descriptorOffset + 2] & BIT0)
                {
                    unknownExtRegisters = false;
                    // copy back the extended registers
                    ataCmd->rtfr.secCntExt = ptrSenseData[descriptorOffset + 4];
                    ataCmd->rtfr.lbaLowExt = ptrSenseData[descriptorOffset + 6];
                    ataCmd->rtfr.lbaMidExt = ptrSenseData[descriptorOffset + 8];
                    ataCmd->rtfr.lbaHiExt  = ptrSenseData[descriptorOffset + 10];
                }
                else
                {
                    // set the registers to zero
                    ataCmd->rtfr.secCntExt = 0;
                    ataCmd->rtfr.lbaLowExt = 0;
                    ataCmd->rtfr.lbaMidExt = 0;
                    ataCmd->rtfr.lbaHiExt  = 0;
                }
            }
        }

        if (unknownExtRegisters)
        {
            ret = WARN_INCOMPLETE_RFTRS; // assume that the following code to get the ext registers fails for some
                                         // reason or another.
            // ok, so we don't know all of the ext registers, so now we need to check if there is a log page available,
            // then try a follow up command (if supported), then try request sense for descriptor format data
            if (resultsLogPageParameterPointer > 0)
            {
                // rtfrs from passthrough results log
                ret = get_Return_TFRs_From_Passthrough_Results_Log(device, &ataCmd->rtfr, (ptrSenseData[8] & 0x0F) - 1);
            }
            if (ret != SUCCESS &&
                !device->drive_info.passThroughHacks.ataPTHacks
                     .returnResponseInfoSupported) // rerequest only if the return response info is not supported
            {
                // request descriptor format data
                uint8_t* descriptorFormatSenseData = C_CAST(
                    uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!descriptorFormatSenseData)
                {
                    return MEMORY_FAILURE;
                }
                // try a request sense to get descriptor sense data
                if (SUCCESS == scsi_Request_Sense_Cmd(device, true, descriptorFormatSenseData, SPC3_SENSE_LEN))
                {
                    ret = get_RTFRs_From_Descriptor_Format_Sense_Data(descriptorFormatSenseData, SPC3_SENSE_LEN,
                                                                      &ataCmd->rtfr);
                    if (ret == FAILURE)
                    {
                        // preserve the incomplete RTFR warning
                        ret = WARN_INCOMPLETE_RFTRS;
                    }
                }
                safe_free_aligned(&descriptorFormatSenseData);
            }
        }
        // Some devices say passthrough info available, but populate nothing...so need to set this error!
        if (ataCmd->rtfr.error == 0 && ataCmd->rtfr.status == 0 && ataCmd->rtfr.device == 0 &&
            ataCmd->rtfr.secCnt == 0 && ataCmd->rtfr.lbaHi == 0 && ataCmd->rtfr.lbaMid == 0 && ataCmd->rtfr.lbaLow == 0)
        {
            if ((ataCmd->commadProtocol == ATA_PROTOCOL_PIO && ataCmd->commandDirection == XFER_DATA_IN) ||
                ataCmd->commadProtocol == ATA_PROTOCOL_DMA_FPDMA)
            {
                // PIO-in and FPDMA will only give valid RTFRs on a failure.
                // If it succeeds and data came back, these may be empty on some translators.
                // Do not consider this a failure, but success
                ataCmd->rtfr.status = ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
                ret                 = SUCCESS;
            }
            else
            {
                ret = FAILURE;
            }
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

bool get_Return_TFRs_From_Sense_Data(tDevice*               device,
                                     ataPassthroughCommand* ataCommandOptions,
                                     eReturnValues          ioRet,
                                     eReturnValues          senseRet)
{
    bool gotRTFRsFromSenseData = false;
    M_USE_UNUSED(senseRet);
    if (ataCommandOptions->ptrSenseData)
    {
        eReturnValues ret             = SUCCESS;
        uint8_t       senseDataFormat = ataCommandOptions->ptrSenseData[0] & 0x7F;
        // attempt to copy back RTFRs from the sense data buffer if we got sense data at all
        // parse the descriptor format sense data
        if (senseDataFormat == SCSI_SENSE_CUR_INFO_DESC || senseDataFormat == SCSI_SENSE_DEFER_ERR_DESC)
        {
            ret = get_RTFRs_From_Descriptor_Format_Sense_Data(
                ataCommandOptions->ptrSenseData, ataCommandOptions->senseDataSize, &ataCommandOptions->rtfr);
            if (ret == SUCCESS)
            {
                gotRTFRsFromSenseData = true;
            }
        }
        // Parse the fixed format sense data if it says there is ATA Pass through Information available.
        else if ((senseDataFormat == SCSI_SENSE_CUR_INFO_FIXED || senseDataFormat == SCSI_SENSE_DEFER_ERR_FIXED)
#if !defined(UNALIGNED_WRITE_SENSE_DATA_WORKAROUND)
                 && (ataCommandOptions->ptrSenseData[12] == 0x00 &&
                     ataCommandOptions->ptrSenseData[13] == 0x1D) // ATA passthrough information available
#endif                                                            //(UNALIGNED_WRITE_SENSE_DATA_WORKAROUND)
        )
        {
            ret = get_RTFRs_From_Fixed_Format_Sense_Data(device, ataCommandOptions->ptrSenseData,
                                                         ataCommandOptions->senseDataSize, ataCommandOptions);
            // if the RTFRs are incomplete, but the device supports the request command, send the request command to get
            // the RTFRs
            if ((ret == WARN_INCOMPLETE_RFTRS || ret == FAILURE) &&
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported)
            {
                ret = request_Return_TFRs_From_Device(device, &ataCommandOptions->rtfr);
                if (ret == SUCCESS)
                {
                    gotRTFRsFromSenseData = true;
                }
            }
            if (ret == SUCCESS || ret == WARN_INCOMPLETE_RFTRS)
            {
                gotRTFRsFromSenseData = true;
            }
        }
        else if (device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported)
        {
            ret = request_Return_TFRs_From_Device(device, &ataCommandOptions->rtfr);
            if (ret == SUCCESS)
            {
                gotRTFRsFromSenseData = true;
            }
        }
        else
        {
            // did not get sense data or RTFRs from the sense data.
        }
        if (!gotRTFRsFromSenseData && ioRet != OS_PASSTHROUGH_FAILURE)
        {
        }
    }
    return gotRTFRsFromSenseData;
}

eReturnValues set_Protocol_Field(uint8_t*               satCDB,
                                 eAtaProtocol           commadProtocol,
                                 eDataTransferDirection dataDirection,
                                 uint8_t                protocolOffset)
{
    eReturnValues ret = SUCCESS;
    switch (commadProtocol)
    {
    case ATA_PROTOCOL_PIO:
        if (dataDirection == XFER_DATA_IN)
        {
            satCDB[protocolOffset] |= SAT_PIO_DATA_IN;
        }
        else if (dataDirection == XFER_DATA_OUT)
        {
            satCDB[protocolOffset] |= SAT_PIO_DATA_OUT;
        }
        else
        {
            ret = BAD_PARAMETER;
        }
        break;
    case ATA_PROTOCOL_DMA:
        satCDB[protocolOffset] |= SAT_DMA;
        break;
    case ATA_PROTOCOL_NO_DATA:
        satCDB[protocolOffset] |= SAT_NON_DATA;
        break;
    case ATA_PROTOCOL_DEV_RESET:
        satCDB[protocolOffset] |= SAT_NODATA_RESET;
        break;
    case ATA_PROTOCOL_DEV_DIAG:
        satCDB[protocolOffset] |= SAT_EXE_DEV_DIAG;
        break;
    case ATA_PROTOCOL_DMA_QUE:
        satCDB[protocolOffset] |= SAT_DMA_QUEUED;
        break;
    case ATA_PROTOCOL_DMA_FPDMA:
        satCDB[protocolOffset] |= SAT_FPDMA;
        break;
    case ATA_PROTOCOL_SOFT_RESET:
        satCDB[protocolOffset] |= SAT_ATA_SW_RESET;
        break;
    case ATA_PROTOCOL_HARD_RESET:
        satCDB[protocolOffset] |= SAT_ATA_HW_RESET;
        break;
    case ATA_PROTOCOL_RET_INFO:
        satCDB[protocolOffset] |= SAT_RET_RESP_INFO;
        break;
    case ATA_PROTOCOL_UDMA:
        if (dataDirection == XFER_DATA_IN)
        {
            satCDB[protocolOffset] |= SAT_UDMA_DATA_IN;
        }
        else if (dataDirection == XFER_DATA_OUT)
        {
            satCDB[protocolOffset] |= SAT_UDMA_DATA_OUT;
        }
        else
        {
            ret = BAD_PARAMETER;
        }
        break;
    case ATA_PROTOCOL_PACKET:
    case ATA_PROTOCOL_PACKET_DMA:
    default:
        ret = BAD_PARAMETER;
        break;
    }
    return ret;
}

eReturnValues set_Transfer_Bits(uint8_t*                      satCDB,
                                eATAPassthroughLength         tLength,
                                eATAPassthroughTransferBlocks ttype,
                                eDataTransferDirection        dataDirection,
                                uint8_t                       transferBitsOffset)
{
    eReturnValues ret = SUCCESS;
    // set tLength First
    satCDB[transferBitsOffset] |= (tLength & 0x03);
    switch (tLength)
    {
    case ATA_PT_LEN_NO_DATA:
        // other bits don't matter here since it's non-data so just break
        break;
    case ATA_PT_LEN_FEATURES_REGISTER:
    case ATA_PT_LEN_SECTOR_COUNT:
        // set t_dir flag (only needs to be set for DATAIN)
        if (dataDirection == XFER_DATA_IN)
        {
            satCDB[transferBitsOffset] |= SAT_T_DIR_DATA_IN;
        }
        // now check the t_type and set byte_block flag as necessary
        switch (ttype)
        {
        case ATA_PT_512B_BLOCKS:
            // set byte-block bit
            satCDB[transferBitsOffset] |= SAT_BYTE_BLOCK_BIT_SET;
            break;
        case ATA_PT_LOGICAL_SECTOR_SIZE:
            // set byte-block bit
            satCDB[transferBitsOffset] |= SAT_BYTE_BLOCK_BIT_SET;
            // set the t-type bit
            satCDB[transferBitsOffset] |= SAT_T_TYPE_BIT_SET;
            break;
        case ATA_PT_NUMBER_OF_BYTES:
            // no additional bits required to be set.
        case ATA_PT_NO_DATA_TRANSFER:
            // no additional bits required to be set.
            break;
        }
        break;
    case ATA_PT_LEN_TPSIU:
        if (dataDirection == XFER_DATA_IN)
        {
            satCDB[transferBitsOffset] |= SAT_T_DIR_DATA_IN;
        }
        break;
    }
    return ret;
}

eReturnValues set_Multiple_Count(uint8_t* satCDB, uint8_t multipleCount, uint8_t protocolOffset)
{
    satCDB[protocolOffset] |= (multipleCount & (BIT0 | BIT1 | BIT2)) << 5;
    return SUCCESS;
}

/*
 * NOTE: Setting the off-line field of a SAT command is not really necessary in current cases (SATA).
 * It was added as a way to work around some issues between certain combinations of PATA devices and HBAs where
 * there could be an occasional error/glitch where the status register was invalid by reading it too quickly. Setting
 * this field will resolve this problem on these troublesome combinations. None are currently known in this code, but if
 * you happen to read this while debugging a really strange error that might be this, add a special case for setting
 * these bits on the HBA this was detected on.
 */
// Timeout should be set to 0, 2, 6, or 14 seconds when setting these bits.
// This is different from the command's timeout to set for the OS.
eReturnValues set_Offline_Bits(uint8_t* satCDB, uint32_t timeout, uint8_t transferBitsOffset)
{
    uint8_t satTimeout = UINT8_C(0);
    switch (timeout)
    {
    case 0:
        break;
    case 2:
        satTimeout = BIT0;
        break;
    case 6:
        satTimeout = BIT1;
        break;
    case 14:
        satTimeout = BIT0 | BIT1;
        break;
    default:
        break;
    }
    satCDB[transferBitsOffset] |= (satTimeout << 6);
    return SUCCESS;
}

eReturnValues set_Check_Condition_Bit(uint8_t* satCDB, uint8_t transferBitsOffset)
{
    satCDB[transferBitsOffset] |= BIT5;
    return SUCCESS;
}

eReturnValues set_Registers(uint8_t* satCDB, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret = SUCCESS;
    if (ataCommandOptions->commandDirection != XFER_NO_DATA && ataCommandOptions->tfr.SectorCount == 0 &&
        ataCommandOptions->ataCommandLengthLocation != ATA_PT_LEN_TPSIU &&
        ataCommandOptions->commandType == ATA_CMD_TYPE_TASKFILE && ataCommandOptions->dataSize == 512)
    {
        // special case for some commands (all 28bit corrected in here. Extended commands do not seem to have this
        // issue) Some commands perform a data transfer, but the sector count was N/A and may be set to zero. Example:
        // Identify, DCO, read/write buffer, security, set max address Other commands may use sector count + lbalow for
        // transfer length. Example: Download microcode or trusted send/receive. These should not fall into here. all
        // read/write userspace commands use zero to do a "maximum" transfer of 256 (28bit) or 65536 (48bit) sectors.

        // The next check is to detect format tracks and write same legacy/retired/obsolete commands. These CANNOT be
        // corrected, but every other command I found can.-TJE Note: old write same uses same opcode as read buffer DMA,
        // so need to check feature register as well and verify the protocol field
        if (!(ataCommandOptions->tfr.CommandStatus == ATA_FORMAT_TRACK_CMD &&
              ataCommandOptions->commadProtocol == ATA_PROTOCOL_PIO) &&
            !(ataCommandOptions->tfr.CommandStatus == ATA_LEGACY_WRITE_SAME &&
              ataCommandOptions->commadProtocol == ATA_PROTOCOL_PIO &&
              (ataCommandOptions->tfr.ErrorFeature == LEGACY_WRITE_SAME_INITIALIZE_SPECIFIED_SECTORS ||
               ataCommandOptions->tfr.ErrorFeature == LEGACY_WRITE_SAME_INITIALIZE_ALL_SECTORS)))
        {
            ataCommandOptions->tfr.SectorCount = 1;
        }
    }
    if (satCDB[OPERATION_CODE] == ATA_PASS_THROUGH_16)
    {
        switch (ataCommandOptions->commandType)
        {
        case ATA_CMD_TYPE_EXTENDED_TASKFILE:
            // first set the extend bit
            satCDB[1] |= BIT0;
            // now set the LBA ext registers
            satCDB[3]  = ataCommandOptions->tfr.Feature48;
            satCDB[5]  = ataCommandOptions->tfr.SectorCount48;
            satCDB[7]  = ataCommandOptions->tfr.LbaLow48;
            satCDB[9]  = ataCommandOptions->tfr.LbaMid48;
            satCDB[11] = ataCommandOptions->tfr.LbaHi48;
            M_FALLTHROUGH;
        case ATA_CMD_TYPE_TASKFILE:
            satCDB[4]  = ataCommandOptions->tfr.ErrorFeature;
            satCDB[6]  = ataCommandOptions->tfr.SectorCount;
            satCDB[8]  = ataCommandOptions->tfr.LbaLow;
            satCDB[10] = ataCommandOptions->tfr.LbaMid;
            satCDB[12] = ataCommandOptions->tfr.LbaHi;
            satCDB[13] = ataCommandOptions->tfr.DeviceHead;
            satCDB[14] = ataCommandOptions->tfr.CommandStatus;
            satCDB[15] = 0; // control
            break;
        default:
            ret = BAD_PARAMETER;
            break;
        }
    }
    else if (satCDB[OPERATION_CODE] == ATA_PASS_THROUGH_12)
    {
        /*
        //This is a hack that MAY help some devices that only support A1h op code.
        //Currently removed since it isn't working on one device I am able to test and poke around with.
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            satCDB[1] |= BIT0;
        }
        */
        satCDB[3]  = ataCommandOptions->tfr.ErrorFeature;
        satCDB[4]  = ataCommandOptions->tfr.SectorCount;
        satCDB[5]  = ataCommandOptions->tfr.LbaLow;
        satCDB[6]  = ataCommandOptions->tfr.LbaMid;
        satCDB[7]  = ataCommandOptions->tfr.LbaHi;
        satCDB[8]  = ataCommandOptions->tfr.DeviceHead;
        satCDB[9]  = ataCommandOptions->tfr.CommandStatus;
        satCDB[10] = RESERVED;
        satCDB[11] = 0; // control
    }
    else if (satCDB[OPERATION_CODE] == 0x7F) // 32B SAT CDB
    {
        switch (ataCommandOptions->commandType)
        {
        case ATA_CMD_TYPE_COMPLETE_TASKFILE:
            // set ICC and AUX
            satCDB[27] = ataCommandOptions->tfr.icc;
            satCDB[28] = ataCommandOptions->tfr.aux4;
            satCDB[29] = ataCommandOptions->tfr.aux3;
            satCDB[30] = ataCommandOptions->tfr.aux2;
            satCDB[31] = ataCommandOptions->tfr.aux1;
            M_FALLTHROUGH;
        case ATA_CMD_TYPE_EXTENDED_TASKFILE:
            // first set the extend bit
            satCDB[10] |= BIT0;
            // now set the LBA ext registers
            satCDB[20] = ataCommandOptions->tfr.Feature48;
            satCDB[22] = ataCommandOptions->tfr.SectorCount48;
            satCDB[16] = ataCommandOptions->tfr.LbaLow48;
            satCDB[15] = ataCommandOptions->tfr.LbaMid48;
            satCDB[14] = ataCommandOptions->tfr.LbaHi48;
            M_FALLTHROUGH;
        case ATA_CMD_TYPE_TASKFILE:
            satCDB[21] = ataCommandOptions->tfr.ErrorFeature;
            satCDB[23] = ataCommandOptions->tfr.SectorCount;
            satCDB[19] = ataCommandOptions->tfr.LbaLow;
            satCDB[18] = ataCommandOptions->tfr.LbaMid;
            satCDB[17] = ataCommandOptions->tfr.LbaHi;
            satCDB[24] = ataCommandOptions->tfr.DeviceHead;
            satCDB[25] = ataCommandOptions->tfr.CommandStatus;
            break;
        default:
            ret = BAD_PARAMETER;
            break;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

eReturnValues request_Return_TFRs_From_Device(tDevice* device, ataReturnTFRs* rtfr)
{
    // try and issue a request for the RTFRs...we'll see if this actually works
    eReturnValues rtfrRet = NOT_SUPPORTED; // by default, most devices don't actually support this SAT command
    uint8_t*      rtfrBuffer =
        C_CAST(uint8_t*,
               safe_calloc_aligned(
                   14, sizeof(uint8_t),
                   device->os_info.minimumAlignment)); // this size is the size of the ATA pass through descriptor which
                                                       // is all that should be returned from the SATL with this command
    uint8_t* rtfr_senseData = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
    uint8_t* requestRTFRs       = M_NULLPTR;
    uint8_t  cdbLen             = CDB_LEN_12;
    uint8_t  protocolOffset     = SAT_PROTOCOL_OFFSET;
    uint8_t  transferBitsOffset = SAT_TRANSFER_BITS_OFFSET;
    if (device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported)
    {
        cdbLen = CDB_LEN_16;
    }
    requestRTFRs =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(cdbLen, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!rtfrBuffer || !rtfr_senseData || !requestRTFRs)
    {
        perror("Calloc aligned Failure!\n");
        safe_free_aligned(&rtfrBuffer);
        safe_free_aligned(&rtfr_senseData);
        safe_free_aligned(&requestRTFRs);
        return MEMORY_FAILURE;
    }
    // Set the op code up for the size of the CDB
    if (cdbLen == CDB_LEN_12)
    {
        requestRTFRs[OPERATION_CODE] = ATA_PASS_THROUGH_12;
    }
    else if (cdbLen == CDB_LEN_16)
    {
        requestRTFRs[OPERATION_CODE] = ATA_PASS_THROUGH_16;
    }
    else if (cdbLen == CDB_LEN_32)
    {
        requestRTFRs[OPERATION_CODE] = 0x7F;
        requestRTFRs[1]              = 0; // Control field
        requestRTFRs[2]              = RESERVED;
        requestRTFRs[3]              = RESERVED;
        requestRTFRs[4]              = RESERVED;
        requestRTFRs[5]              = RESERVED;
        requestRTFRs[6]              = RESERVED;
        requestRTFRs[7]              = 0x18; // Additional length
        // Set the service action
        requestRTFRs[8]    = 0x1F;
        requestRTFRs[9]    = 0xF0;
        protocolOffset     = UINT8_C(10);
        transferBitsOffset = UINT8_C(11);
    }
    else
    {
        safe_free_aligned(&rtfrBuffer);
        safe_free_aligned(&rtfr_senseData);
        safe_free_aligned(&requestRTFRs);
        return BAD_PARAMETER;
    }
    // set the protocol to Fh (15) to request the return TFRs be returned in that data in buffer.
    set_Protocol_Field(requestRTFRs, ATA_PROTOCOL_RET_INFO, XFER_DATA_IN, protocolOffset);
    // SAT says these bits should be ignored.
    // I'm setting them anyways because I have found a few SATLs that NEED the T_DIR bit at minimum to determine that
    // this is a read since they do not validate the protocol as a read in and of itself.
    requestRTFRs[protocolOffset] |= SAT_T_DIR_DATA_IN;
    requestRTFRs[transferBitsOffset] |=
        ATA_PT_LEN_TPSIU; // While this may not be necessary, it at least makes the most sense for this protocol and
                          // should be ignored by a good SATL

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SAT Return Response Information\n");
    }

    if (SUCCESS ==
        scsi_Send_Cdb(device, requestRTFRs, cdbLen, rtfrBuffer, 14, XFER_DATA_IN, rtfr_senseData, SPC3_SENSE_LEN, 15))
    {
        // check the descriptor code
        if (rtfrBuffer[0] == SAT_DESCRIPTOR_CODE && rtfrBuffer[1] == SAT_ADDT_DESC_LEN)
        {
            rtfrRet      = SUCCESS;
            rtfr->error  = rtfrBuffer[3];
            rtfr->secCnt = rtfrBuffer[5];
            rtfr->lbaLow = rtfrBuffer[7];
            rtfr->lbaMid = rtfrBuffer[9];
            rtfr->lbaHi  = rtfrBuffer[11];
            rtfr->device = rtfrBuffer[12];
            rtfr->status = rtfrBuffer[13];
            // check for the extend bit
            if (rtfrBuffer[2] & BIT0 || device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit)
            {
                // copy back the extended registers
                rtfr->secCntExt = rtfrBuffer[4];
                rtfr->lbaLowExt = rtfrBuffer[6];
                rtfr->lbaMidExt = rtfrBuffer[8];
                rtfr->lbaHiExt  = rtfrBuffer[10];
            }
            else
            {
                // set the registers to zero
                rtfr->secCntExt = UINT8_C(0);
                rtfr->lbaLowExt = UINT8_C(0);
                rtfr->lbaMidExt = UINT8_C(0);
                rtfr->lbaHiExt  = UINT8_C(0);
            }
        }
    }
    // any other return value doesn't matter since this will not affect pass fail of our command. After this we will be
    // dummying up a status anyways
    safe_free_aligned(&rtfrBuffer);
    safe_free_aligned(&rtfr_senseData);
    safe_free_aligned(&requestRTFRs);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("SAT Return Response Information", rtfrRet);
    }
    return rtfrRet;
}

eReturnValues build_SAT_CDB(tDevice*               device,
                            uint8_t**              satCDB,
                            eCDBLen*               cdbLen,
                            ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret                = SUCCESS;
    uint8_t       protocolOffset     = SAT_PROTOCOL_OFFSET;
    uint8_t       transferBitsOffset = SAT_TRANSFER_BITS_OFFSET;
    if (device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough)
    {
        // override whatever came in here so that commands go through successfully....mostly for USB
        ataCommandOptions->ataCommandLengthLocation = ATA_PT_LEN_TPSIU;
    }
    if (ataCommandOptions->forceCDBSize > 0)
    {
        // before proceeding to allocate memory, need to place some checks on some commands or things won't work exactly
        // right. This force is rarely useful other than troubleshooting devices or support for certain features of a
        // given translator.
        switch (ataCommandOptions->forceCDBSize)
        {
        case 12:
            // if any ext registers are set, then the command cannot be issued.
            if (ataCommandOptions->tfr.SectorCount48 || ataCommandOptions->tfr.LbaLow48 ||
                ataCommandOptions->tfr.LbaMid48 || ataCommandOptions->tfr.LbaHi48 || ataCommandOptions->tfr.Feature48 ||
                ataCommandOptions->tfr.aux1 || ataCommandOptions->tfr.aux2 || ataCommandOptions->tfr.aux3 ||
                ataCommandOptions->tfr.aux4 || ataCommandOptions->tfr.icc)
            {
                return BAD_PARAMETER;
            }
            else
            {
                *satCDB =
                    M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(ataCommandOptions->forceCDBSize, sizeof(uint8_t),
                                                                     device->os_info.minimumAlignment));
                if (!*satCDB)
                {
                    return MEMORY_FAILURE;
                }
                *cdbLen = ataCommandOptions->forceCDBSize;
                // set the OP Code
                (*satCDB)[OPERATION_CODE] = ATA_PASS_THROUGH_12;
            }
            break;
        case 16:
            // If aux or ICC are set with this force flag, then it needs to be rejected since it is not possible to
            // issue in 16B CDBs
            if (ataCommandOptions->tfr.aux1 || ataCommandOptions->tfr.aux2 || ataCommandOptions->tfr.aux3 ||
                ataCommandOptions->tfr.aux4 || ataCommandOptions->tfr.icc)
            {
                return BAD_PARAMETER;
            }
            else
            {
                *satCDB =
                    M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(ataCommandOptions->forceCDBSize, sizeof(uint8_t),
                                                                     device->os_info.minimumAlignment));
                if (!*satCDB)
                {
                    return MEMORY_FAILURE;
                }
                *cdbLen = ataCommandOptions->forceCDBSize;
                // set the OP Code
                (*satCDB)[OPERATION_CODE] = ATA_PASS_THROUGH_16;
            }
            break;
        case 32:
            *satCDB = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(ataCommandOptions->forceCDBSize, sizeof(uint8_t),
                                                                       device->os_info.minimumAlignment));
            if (!*satCDB)
            {
                return MEMORY_FAILURE;
            }
            *cdbLen = ataCommandOptions->forceCDBSize;
            // Set the OP Code
            *satCDB[OPERATION_CODE] = 0x7F; // variable length CDB
            (*satCDB)[1]            = 0;    // Control field
            (*satCDB)[2]            = RESERVED;
            (*satCDB)[3]            = RESERVED;
            (*satCDB)[4]            = RESERVED;
            (*satCDB)[5]            = RESERVED;
            (*satCDB)[6]            = RESERVED;
            (*satCDB)[7]            = 0x18; // Additional length
            // Set the service action
            (*satCDB)[8]       = 0x1F;
            (*satCDB)[9]       = 0xF0;
            protocolOffset     = 10;
            transferBitsOffset = 11;
            (*satCDB)[12]      = RESERVED;
            (*satCDB)[13]      = RESERVED;
            (*satCDB)[26]      = RESERVED;
            break;
        default:
            // Do nothing as this is not a valid value and let the rest of the code figure out what to do
            break;
        }
    }
    if (!*satCDB)
    {
        switch (ataCommandOptions->commandType)
        {
        case ATA_CMD_TYPE_TASKFILE:
            if (!device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported)
            {
                // 12B CDB
                *satCDB = C_CAST(uint8_t*,
                                 safe_calloc_aligned(CDB_LEN_12, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!*satCDB)
                {
                    return MEMORY_FAILURE;
                }
                *cdbLen = CDB_LEN_12;
                // set the OP Code
                (*satCDB)[OPERATION_CODE] = ATA_PASS_THROUGH_12;
            }
            else
            {
                // 16B CDB
                *satCDB = C_CAST(uint8_t*,
                                 safe_calloc_aligned(CDB_LEN_16, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!*satCDB)
                {
                    return MEMORY_FAILURE;
                }
                *cdbLen = CDB_LEN_16;
                // Set the OP Code
                (*satCDB)[OPERATION_CODE] = ATA_PASS_THROUGH_16;
            }
            break;
        case ATA_CMD_TYPE_EXTENDED_TASKFILE:
            if (device->drive_info.passThroughHacks.ataPTHacks.a1ExtCommandWhenPossible)
            {
                // validate that there are no ext registers to set. If there are, then do nothing, otherwise allocate
                // the memory to build the CDB
                if (!(ataCommandOptions->tfr.SectorCount48 || ataCommandOptions->tfr.LbaLow48 ||
                      ataCommandOptions->tfr.LbaMid48 || ataCommandOptions->tfr.LbaHi48 ||
                      ataCommandOptions->tfr.Feature48))
                {
                    // No ext registers are set, so we will issue the command with a 12B CDB. This is a major hack, but
                    // might help some devices get some more support. 12B CDB
                    *satCDB = C_CAST(
                        uint8_t*, safe_calloc_aligned(CDB_LEN_12, sizeof(uint8_t), device->os_info.minimumAlignment));
                    if (!*satCDB)
                    {
                        return MEMORY_FAILURE;
                    }
                    *cdbLen = CDB_LEN_12;
                    // Set the OP Code
                    (*satCDB)[OPERATION_CODE] = ATA_PASS_THROUGH_12;
                }
            }
            if (!*satCDB) // should fall into here if the above check did not work and allocate the satCDB memory.
            {
                // 16B CDB
                *satCDB = C_CAST(uint8_t*,
                                 safe_calloc_aligned(CDB_LEN_16, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!*satCDB)
                {
                    return MEMORY_FAILURE;
                }
                *cdbLen = CDB_LEN_16;
                // Set the OP Code
                (*satCDB)[OPERATION_CODE] = ATA_PASS_THROUGH_16;
            }
            break;
        case ATA_CMD_TYPE_COMPLETE_TASKFILE:
            // 32B CDB
            *satCDB = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(CDB_LEN_32, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!*satCDB)
            {
                return MEMORY_FAILURE;
            }
            *cdbLen = CDB_LEN_32;
            // Set the OP Code
            (*satCDB)[OPERATION_CODE] = 0x7F; // variable length CDB
            (*satCDB)[1]              = 0;    // Control field
            (*satCDB)[2]              = RESERVED;
            (*satCDB)[3]              = RESERVED;
            (*satCDB)[4]              = RESERVED;
            (*satCDB)[5]              = RESERVED;
            (*satCDB)[6]              = RESERVED;
            (*satCDB)[7]              = 0x18; // Additional length
            // Set the service action
            (*satCDB)[8]       = 0x1F;
            (*satCDB)[9]       = 0xF0;
            protocolOffset     = 10;
            transferBitsOffset = 11;
            (*satCDB)[12]      = RESERVED;
            (*satCDB)[13]      = RESERVED;
            (*satCDB)[26]      = RESERVED;
            break;
            // TODO: handle hard/soft reset here to generate those CDBs properly as well. For now, they are not
            // supported. - TJE
        default:
            return BAD_PARAMETER;
        }
    }
    // set protocol
    ret = set_Protocol_Field(*satCDB, ataCommandOptions->commadProtocol, ataCommandOptions->commandDirection,
                             protocolOffset);
    if (ret != SUCCESS) // nothing else to setup for hardware reset
    {
        return ret;
    }
    if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_SOFT_RESET ||
        ataCommandOptions->commadProtocol == ATA_PROTOCOL_HARD_RESET)
    {
        // set offline field then return
        set_Offline_Bits(*satCDB, ataCommandOptions->timeout, transferBitsOffset);
        return ret;
    }
    // set multiple count
    set_Multiple_Count(*satCDB, ataCommandOptions->multipleCount, protocolOffset);
    // set transfer bits
    ret = set_Transfer_Bits(*satCDB, ataCommandOptions->ataCommandLengthLocation, ataCommandOptions->ataTransferBlocks,
                            ataCommandOptions->commandDirection, transferBitsOffset);
    if (ret != SUCCESS)
    {
        return ret;
    }
    // set offline bits - This is currently disabled and not used. It's a workaround for some strange PATA-HBA
    // interactions. See full comment around this function above set_Offline_Bits(*satCDB, 0, transferBitsOffset); set
    // registers
    ret = set_Registers(*satCDB, ataCommandOptions);
    // set the check condition bit as we need it
    if (device->os_info.osType == OS_WINDOWS && device->drive_info.interface_type == IDE_INTERFACE)
    {
        // always set the check condition bit since in this case we won't get RTFRs even if there is an error...Windows
        // low level driver workaround
        set_Check_Condition_Bit(*satCDB, transferBitsOffset);
    }
    else if (ataCommandOptions->needRTFRs)
    {
        if (!((ataCommandOptions->commadProtocol == ATA_PROTOCOL_PIO &&
               ataCommandOptions->commandDirection == XFER_DATA_IN) ||
              ataCommandOptions->commadProtocol == ATA_PROTOCOL_DMA_FPDMA))
        {
            if ((!device->drive_info.passThroughHacks.ataPTHacks.disableCheckCondition ||
                 device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable) &&
                !device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty)
            {
                set_Check_Condition_Bit(*satCDB, transferBitsOffset);
            }
        }
    }
    return ret;
}

eReturnValues send_SAT_Passthrough_Command(tDevice* device, ataPassthroughCommand* ataCommandOptions)
{
    eReturnValues ret            = UNKNOWN;
    uint8_t*      satCDB         = M_NULLPTR;
    eCDBLen       satCDBLength   = 0;
    uint8_t*      senseData      = M_NULLPTR; // only allocate if the pointer in the ataCommandOptions is M_NULLPTR
    bool          localSenseData = false;
    bool          dmaRetry       = false;
    if (!ataCommandOptions->ptrSenseData)
    {
        senseData = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(SPC3_SENSE_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!senseData)
        {
            return MEMORY_FAILURE;
        }
        localSenseData                   = true;
        ataCommandOptions->ptrSenseData  = senseData;
        ataCommandOptions->senseDataSize = SPC3_SENSE_LEN;
    }
    ataCommandOptions->timeout = M_Max(ataCommandOptions->timeout, device->drive_info.defaultTimeoutSeconds);
    if (ataCommandOptions->timeout == 0)
    {
        ataCommandOptions->timeout = M_Max(15, device->drive_info.defaultTimeoutSeconds);
    }
    // First build the CDB
    ret = build_SAT_CDB(device, &satCDB, &satCDBLength, ataCommandOptions);
    if (ret == SUCCESS)
    {
        ScsiIoCtx       scsiIoCtx;
        senseDataFields senseFields;
        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // Print out ATA Command Information in appropriate verbose mode.
            print_Verbose_ATA_Command_Information(ataCommandOptions);
        }
        // Now setup the scsiioctx and send the CDB
        safe_memset(&scsiIoCtx, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
        safe_memcpy(scsiIoCtx.cdb, SCSI_IO_CTX_MAX_CDB_LEN, satCDB,
                    C_CAST(size_t, satCDBLength)); // should only ever be a known positive integer: 12, 16, or 32
        safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
        scsiIoCtx.cdbLength     = C_CAST(uint8_t, satCDBLength);
        scsiIoCtx.dataLength    = ataCommandOptions->dataSize;
        scsiIoCtx.pdata         = ataCommandOptions->ptrData;
        scsiIoCtx.device        = device;
        scsiIoCtx.direction     = ataCommandOptions->commandDirection;
        scsiIoCtx.pAtaCmdOpts   = ataCommandOptions;
        scsiIoCtx.psense        = ataCommandOptions->ptrSenseData;
        scsiIoCtx.senseDataSize = ataCommandOptions->senseDataSize;
        scsiIoCtx.timeout       = ataCommandOptions->timeout;
        // clear the last command sense data every single time before we issue any commands
        safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0, SPC3_SENSE_LEN);
        device->drive_info.lastCommandTimeNanoSeconds = UINT64_C(0);

        ret = private_SCSI_Send_CDB(&scsiIoCtx, &senseFields);

        // Before attempting anything else to read RTFRs, send a follow up command, etc, check if the sense fields is
        // already parsed with what we need. -TJE
        bool gotRTFRs = false;
        if (senseFields.validStructure && senseFields.ataStatusReturnDescriptor.valid)
        {
            gotRTFRs                       = true;
            ataCommandOptions->rtfr.status = senseFields.ataStatusReturnDescriptor.status;
            ataCommandOptions->rtfr.error  = senseFields.ataStatusReturnDescriptor.error;
            ataCommandOptions->rtfr.secCnt = senseFields.ataStatusReturnDescriptor.sectorCount;
            ataCommandOptions->rtfr.lbaLow = senseFields.ataStatusReturnDescriptor.lbaLow;
            ataCommandOptions->rtfr.lbaMid = senseFields.ataStatusReturnDescriptor.lbaMid;
            ataCommandOptions->rtfr.lbaHi  = senseFields.ataStatusReturnDescriptor.lbaHi;
            ataCommandOptions->rtfr.device = senseFields.ataStatusReturnDescriptor.device;
            if (senseFields.ataStatusReturnDescriptor.extend ||
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit)
            {
                ataCommandOptions->rtfr.secCntExt = senseFields.ataStatusReturnDescriptor.sectorCountExt;
                ataCommandOptions->rtfr.lbaLowExt = senseFields.ataStatusReturnDescriptor.lbaLowExt;
                ataCommandOptions->rtfr.lbaMidExt = senseFields.ataStatusReturnDescriptor.lbaMidExt;
                ataCommandOptions->rtfr.lbaHiExt  = senseFields.ataStatusReturnDescriptor.lbaHiExt;
            }
            else
            {
                ataCommandOptions->rtfr.secCntExt = UINT8_C(0);
                ataCommandOptions->rtfr.lbaLowExt = UINT8_C(0);
                ataCommandOptions->rtfr.lbaMidExt = UINT8_C(0);
                ataCommandOptions->rtfr.lbaHiExt  = UINT8_C(0);
            }
        }
        else
        {
            // Before attempting anything else, check if sense data is fixed format
            // If fixed format, parse information and command specific data out per SAT-2 and later
            //     also check if ATA return descriptor is present in the vendor specific area of the sense data. - TJE
            if (senseFields.validStructure && senseFields.fixedFormat)
            {
                // NOTE: Spec does not require this sense code to be reported, however it is the most reliable to use
                // and most SATLs support it properly - TJE
                if (senseFields.scsiStatusCodes.asc == 0x00 && senseFields.scsiStatusCodes.ascq == 0x1D)
                {
                    gotRTFRs = true;
                    // this is set to "ATA Passthrough information available" so we can expect result registers in the
                    // correct place. NOTE: The spec does not specify this must be set for ATA passthrough so other
                    // cases may also need to be handled. - TJE
                    ataCommandOptions->rtfr.error  = M_Byte3(senseFields.fixedInformation);
                    ataCommandOptions->rtfr.status = M_Byte2(senseFields.fixedInformation);
                    ataCommandOptions->rtfr.device = M_Byte1(senseFields.fixedInformation);
                    ataCommandOptions->rtfr.secCnt = M_Byte0(senseFields.fixedInformation);
                    // now command specific information field
                    if (M_Byte3(senseFields.fixedCommandSpecificInformation) & BIT7)
                    {
                        if (M_Byte3(senseFields.fixedCommandSpecificInformation) & BIT6)
                        {
                            ataCommandOptions->rtfr.secCntExt = UINT8_MAX;
                            ret                               = WARN_INCOMPLETE_RFTRS;
                        }
                        if (M_Byte3(senseFields.fixedCommandSpecificInformation) & BIT5)
                        {
                            ataCommandOptions->rtfr.lbaLowExt = UINT8_MAX;
                            ataCommandOptions->rtfr.lbaMidExt = UINT8_MAX;
                            ataCommandOptions->rtfr.lbaHiExt  = UINT8_MAX;
                            ret                               = WARN_INCOMPLETE_RFTRS;
                        }
                        // if a non-zero log index is available, then we can read that to get full result
                        uint8_t resultsLogIndex = M_Nibble0(M_Byte3(senseFields.fixedCommandSpecificInformation));
                        if (resultsLogIndex > UINT8_C(0))
                        {
                            // scsi log sense to passthrough results log page with the value in the log index
                            ret = get_Return_TFRs_From_Passthrough_Results_Log(device, &ataCommandOptions->rtfr,
                                                                               resultsLogIndex - UINT8_C(1));
                        }
                        else if (senseFields.additionalDataAvailable &&
                                 scsiIoCtx.psense[senseFields.additionalDataOffset] == 0x09 &&
                                 scsiIoCtx.psense[senseFields.additionalDataOffset + 1] == 0x0C)
                        {
                            // non-standard where the ATA status descriptor is reported in the vendor specific bytes of
                            // the fixed format sense data
                            ret                            = SUCCESS;
                            ataCommandOptions->rtfr.status = scsiIoCtx.psense[senseFields.additionalDataOffset + 13];
                            ataCommandOptions->rtfr.error  = scsiIoCtx.psense[senseFields.additionalDataOffset + 3];
                            ataCommandOptions->rtfr.secCnt = scsiIoCtx.psense[senseFields.additionalDataOffset + 5];
                            ataCommandOptions->rtfr.lbaLow = scsiIoCtx.psense[senseFields.additionalDataOffset + 7];
                            ataCommandOptions->rtfr.lbaMid = scsiIoCtx.psense[senseFields.additionalDataOffset + 9];
                            ataCommandOptions->rtfr.lbaHi  = scsiIoCtx.psense[senseFields.additionalDataOffset + 11];
                            ataCommandOptions->rtfr.device = scsiIoCtx.psense[senseFields.additionalDataOffset + 12];
                            if (scsiIoCtx.psense[senseFields.additionalDataOffset + 2] & BIT0 ||
                                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit)
                            {
                                ataCommandOptions->rtfr.secCntExt =
                                    scsiIoCtx.psense[senseFields.additionalDataOffset + 4];
                                ataCommandOptions->rtfr.lbaLowExt =
                                    scsiIoCtx.psense[senseFields.additionalDataOffset + 6];
                                ataCommandOptions->rtfr.lbaMidExt =
                                    scsiIoCtx.psense[senseFields.additionalDataOffset + 8];
                                ataCommandOptions->rtfr.lbaHiExt =
                                    scsiIoCtx.psense[senseFields.additionalDataOffset + 10];
                            }
                            else
                            {
                                ataCommandOptions->rtfr.secCntExt = UINT8_C(0);
                                ataCommandOptions->rtfr.lbaLowExt = UINT8_C(0);
                                ataCommandOptions->rtfr.lbaMidExt = UINT8_C(0);
                                ataCommandOptions->rtfr.lbaHiExt  = UINT8_C(0);
                            }
                        }
                        else if (device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported)
                        {
                            if (NOT_SUPPORTED == request_Return_TFRs_From_Device(device, &ataCommandOptions->rtfr))
                            {
                                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = false;
                            }
                        }
                    }
                    else
                    {
                        ataCommandOptions->rtfr.secCntExt = UINT8_C(0);
                        ataCommandOptions->rtfr.lbaLowExt = UINT8_C(0);
                        ataCommandOptions->rtfr.lbaMidExt = UINT8_C(0);
                        ataCommandOptions->rtfr.lbaHiExt  = UINT8_C(0);
                    }
                    ataCommandOptions->rtfr.lbaLow = M_Byte2(senseFields.fixedCommandSpecificInformation);
                    ataCommandOptions->rtfr.lbaMid = M_Byte1(senseFields.fixedCommandSpecificInformation);
                    ataCommandOptions->rtfr.lbaHi  = M_Byte0(senseFields.fixedCommandSpecificInformation);
                }
            }
            else // translate the result to common rtfr responses
            {
                uint8_t senseKey = UINT8_C(0);
                uint8_t asc      = UINT8_C(0);
                uint8_t ascq     = UINT8_C(0);
                uint8_t fru      = UINT8_C(0);
                if (senseFields.validStructure)
                {
                    senseKey = senseFields.scsiStatusCodes.senseKey;
                    asc      = senseFields.scsiStatusCodes.asc;
                    ascq     = senseFields.scsiStatusCodes.ascq;
                    fru      = senseFields.scsiStatusCodes.fru;
                }
                switch (senseKey)
                {
                case SENSE_KEY_HARDWARE_ERROR:
                    if (asc == 0x44 && ascq == 0x00 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_DEVICE_FAULT;
                    }
                    break;
                case SENSE_KEY_NOT_READY:
                    if (asc == 0x3A && ascq == 0x00 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_NO_MEDIA; // nm bit
                    }
                    break;
                case SENSE_KEY_MEDIUM_ERROR:
                    if (asc == 0x11 && ascq == 0x00 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_UNCORRECTABLE_DATA; // unc bit
                    }
                    else if (asc == 0x14 && ascq == 0x01 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_ID_NOT_FOUND; // idnf bit
                    }
                    break;
                case SENSE_KEY_DATA_PROTECT:
                    if (asc == 0x27 && ascq == 0x00 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_WRITE_PROTECTED; // wp bit
                    }
                    break;
                case SENSE_KEY_ILLEGAL_REQUEST:
                    if (asc == 0x21 && ascq == 0x00 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_ID_NOT_FOUND; // idnf bit
                    }
                    else if (asc == 0x24 && ascq == 0x00) // invalid field in CDB
                    {
                        if (ataCommandOptions->commadProtocol == ATA_PROTOCOL_UDMA)
                        {
                            dmaRetry = true;
                        }
                        // TODO: Check condition bit check for retry with follow up command instead?
                    }
                    break;
                case SENSE_KEY_ABORTED_COMMAND:
                    if (asc == 0x47 && ascq == 0x03 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_INTERFACE_CRC; // icrc bit
                    }
                    else
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_ABORT; // abort
                    }
                    break;
                case SENSE_KEY_UNIT_ATTENTION:
                    if (asc == 0x28 && ascq == 0x00 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_MEDIA_CHANGE; // mc bit
                    }
                    else if (asc == 0x47 && ascq == 0x03 && fru == 0)
                    {
                        ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_ERROR;
                        ataCommandOptions->rtfr.error  = ATA_ERROR_BIT_MEDIA_CHANGE_REQUEST; // mcr bit
                    }
                    break;
                case SENSE_KEY_NO_ERROR:
                    // if no error is reported, then consider this a passing command.
                    ataCommandOptions->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY;
                    ataCommandOptions->rtfr.error  = 0;
                    break;
                default:
                    // Don't do anything! We don't have enough information to decide what happened. The SATL is
                    // returning it's own error that is not related to the command being sent.
                    break;
                }
            }
        }

        if (gotRTFRs)
        {
            // Check if they are empty because this is a device that may not truly support the check condition bit
            // NOTE: Only checking status and error at this time since those should basically always have something in
            // them when the RTFRs come back.
            if (ataCommandOptions->rtfr.status == 0 ||
                ((ataCommandOptions->rtfr.status & ATA_STATUS_BIT_ERROR) && ataCommandOptions->rtfr.error == 0))
            {
                device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty = true;
                if (!device->drive_info.passThroughHacks.hacksSetByReportedID &&
                    !device->drive_info.passThroughHacks.ataPTHacks.noRTFRsPossible)
                {
                    // turn on return response info to try that since the check condition came back empty
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                }
            }
        }

        if ((!gotRTFRs || device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty) &&
            ataCommandOptions->needRTFRs &&
            device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported &&
            scsiIoCtx.psense[0] == SCSI_SENSE_NO_SENSE_DATA && ret != BAD_PARAMETER && ret != OS_PASSTHROUGH_FAILURE &&
            ret != OS_COMMAND_NOT_AVAILABLE)
        {
            if (SUCCESS != request_Return_TFRs_From_Device(device, &ataCommandOptions->rtfr))
            {
                if (!device->drive_info.passThroughHacks.hacksSetByReportedID)
                {
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = false;
                    if (device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty)
                    {
                        // Set this disable check condition because we already auto tried check condition and got an
                        // empty result We are now here where the follow up command also failed. So to prevent future
                        // retries, also set this field. Basically reusing an existing hack to determine when a retry
                        // has already been tested.
                        device->drive_info.passThroughHacks.ataPTHacks.noRTFRsPossible = true;
                    }
                }
            }
        }

        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
        {
            // Print out the RTFRs that we got
            print_Verbose_ATA_Command_Result_Information(ataCommandOptions, device);
        }
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            // print command timing information
            print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
        }

        device->drive_info.ataSenseData.validData = false; // clear this everytime. will be changed if we got anything
        if (gotRTFRs)
        {
            // Check the status register to see if the busy bit is set
            bool          checkError          = false;
            bool          requestATASenseData = false;
            eReturnValues driveStatusRet      = SUCCESS;
            if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_BUSY)
            {
                // When this bit is set, all other status register bits are invalid/unused.
                driveStatusRet = IN_PROGRESS;
            }
            else
            {
                if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_READY ||
                    ataCommandOptions->rtfr.status ==
                        0) // if drive is ready, or we got all zeros, consider it good for now
                {
                    driveStatusRet = SUCCESS;
                    if (ataCommandOptions->rtfr.status == 0)
                    {
                        ataCommandOptions->rtfr.status |= ATA_STATUS_BIT_READY | ATA_STATUS_BIT_SEEK_COMPLETE;
                    }
                }
                if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_DEVICE_FAULT)
                {
                    // device fault bit means the drive has gone into an unrecoverable state and is basically dead. Just
                    // set failure here
                    driveStatusRet = FAILURE;
                }
                else
                {
                    if (ataCommandOptions->rtfr.status & ATA_STATUS_BIT_ERROR)
                    {
                        checkError = true;
                    }
                    if (device->drive_info.ata_Options.senseDataReportingEnabled &&
                        ataCommandOptions->rtfr.status & ATA_STATUS_BIT_SENSE_DATA_AVAILABLE &&
                        ataCommandOptions->tfr.CommandStatus != ATA_REQUEST_SENSE_DATA)
                    {
                        requestATASenseData = true;
                    }
                }
                // now check if we need to look at the error register for errors
                if (checkError)
                {
                    if (ataCommandOptions->rtfr.error & ATA_ERROR_BIT_ABORT)
                    {
                        driveStatusRet = ABORTED;
                    }
                    else
                    {
                        driveStatusRet = FAILURE;
                    }
                }
                // if we have requestATASenseData set, then we will request sense, and set it into the device struct and
                // check it's meaning to set the return status
                if (requestATASenseData)
                {
                    uint8_t ataSenseKey                     = UINT8_C(0);
                    uint8_t ataAdditionalSenseCode          = UINT8_C(0);
                    uint8_t ataAdditionalSenseCodeQualifier = UINT8_C(0);
                    if (SUCCESS == ata_Request_Sense_Data(device, &ataSenseKey, &ataAdditionalSenseCode,
                                                          &ataAdditionalSenseCodeQualifier))
                    {
                        eReturnValues ataSenseRet;
                        device->drive_info.ataSenseData.validData                    = true;
                        device->drive_info.ataSenseData.senseKey                     = ataSenseKey;
                        device->drive_info.ataSenseData.additionalSenseCode          = ataAdditionalSenseCode;
                        device->drive_info.ataSenseData.additionalSenseCodeQualifier = ataAdditionalSenseCodeQualifier;
                        if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
                        {
                            printf("\t  ATA Sense Data reported:\n");
                        }
                        ataSenseRet = check_Sense_Key_ASC_ASCQ_And_FRU(device, ataSenseKey, ataAdditionalSenseCode,
                                                                       ataAdditionalSenseCodeQualifier, 0);
                        if (driveStatusRet != ataSenseRet &&
                            (ataSenseKey != 0 || ataAdditionalSenseCode != 0 || ataAdditionalSenseCodeQualifier != 0))
                        {
                            driveStatusRet = ataSenseRet;
                        }
                    }
                }
            }
            if (ret != WARN_INCOMPLETE_RFTRS)
            {
                ret = driveStatusRet;
            }

            // Windows has a problem where if a command fails, the next command that is sent will return the same stale
            // status even if it was good. We need to flush the command out with a check power more command
            if (device->os_info.osType == OS_WINDOWS && device->drive_info.interface_type == IDE_INTERFACE &&
                ret != SUCCESS && ataCommandOptions->tfr.CommandStatus != ATA_CHECK_POWER_MODE_CMD)
            {
                // On Windows AHCI controller (IDE Interface), for whatever reason, the controller sometimes caches the
                // bad status and will return it on the very next command we issue
                //...SO send a "check Power Mode" command to force it to refresh and prevent this issue from happening
                // again..basically a test unit ready command - TJE
                uint64_t commandTimeNanoseconds =
                    device->drive_info
                        .lastCommandTimeNanoSeconds; // back this up first since they'll want the time from the command
                                                     // they issued, not the check power mode.
                uint8_t powerMode = UINT8_C(0);      // we don't actually care about this...just holding it for now
                ata_Check_Power_Mode(device, &powerMode);
                // The rtfrs from this command will get overwritten below so this will appear transparent.
                device->drive_info.lastCommandTimeNanoSeconds = commandTimeNanoseconds;
            }
        }
    }
    safe_free_aligned(&satCDB);
    if ((device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) > ataCommandOptions->timeout)
    {
        ret = OS_COMMAND_TIMEOUT;
    }
    safe_memcpy(&device->drive_info.lastCommandRTFRs, sizeof(ataReturnTFRs), &ataCommandOptions->rtfr,
                sizeof(ataReturnTFRs));
    safe_free_aligned(&senseData);
    if (localSenseData)
    {
        ataCommandOptions->ptrSenseData  = M_NULLPTR;
        ataCommandOptions->senseDataSize = 0;
    }
    if (dmaRetry)
    {
        // if the sense data says "NOT_SUPPORTED", it's highly likely that the SATL didn't like something in the
        // command. Local testing shows that sometimes a SATL likes the mode set to DMA instead of UDMA, so retry
        // the command with the protocol set to DMA.
        ataCommandOptions->commadProtocol = ATA_PROTOCOL_DMA;
        ret                               = send_SAT_Passthrough_Command(device, ataCommandOptions);
        if (ret == SUCCESS)
        {
            // if changing back to DMA worked, then we're changing some flags in the ataOptions struct to make sure
            // we have success with future commands.
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
        }
    }
    return ret;
}

////////////////////////////////////////////////////////////////////
/// The software SAT layer is implemented below.                 ///
/// This is used in operating systems where there is not a SATL. ///
/// Ex: Windows ATA_Passthrough or FreeBSD ATA passthrough       ///
////////////////////////////////////////////////////////////////////

static void set_Sense_Key_Specific_Descriptor_Invalid_Field(uint8_t  data[8],
                                                            bool     cd,
                                                            bool     bpv,
                                                            uint8_t  bitPointer,
                                                            uint16_t fieldPointer)
{
    if (data)
    {
        data[0] = 0x02;
        data[1] = 0x06;
        data[2] = RESERVED;
        data[3] = RESERVED;
        data[4] = get_bit_range_uint8(bitPointer, 2, 0);
        data[4] |= BIT7; // if this function is being called, then this bit is assumed to be set
        if (cd)
        {
            data[4] |= BIT6;
        }
        if (bpv)
        {
            data[4] |= BIT3;
        }
        data[5] = M_Byte1(fieldPointer);
        data[6] = M_Byte0(fieldPointer);
        data[7] = RESERVED;
    }
}

static void set_Sense_Key_Specific_Descriptor_Progress_Indicator(uint8_t data[8], uint16_t progressValue)
{
    if (data)
    {
        data[0] = 0x02;
        data[1] = 0x06;
        data[2] = RESERVED;
        data[3] = RESERVED;
        data[4] = RESERVED;
        data[4] |= BIT7; // if this function is being called, then this bit is assumed to be set
        data[5] = M_Byte1(progressValue);
        data[6] = M_Byte0(progressValue);
        data[7] = RESERVED;
    }
}

static void set_Sense_Data_For_Translation(
    uint8_t* sensePtr,
    uint32_t senseDataLength,
    uint8_t  senseKey,
    uint8_t  asc,
    uint8_t  ascq,
    bool     descriptorFormat,
    uint8_t* descriptor,
    uint8_t  descriptorCount /* this will probably only be 1, but up to 2 or 3 max */)
{
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    uint8_t additionalSenseLength = UINT8_C(0);
    if (descriptorFormat)
    {
        senseData[0] = SCSI_SENSE_CUR_INFO_DESC;
        // sense key
        senseData[1] |= M_Nibble0(senseKey);
        // asc
        senseData[2] = asc;
        // ascq
        senseData[3] = ascq;
        if (descriptor)
        {
            // loop through descriptor copying each one to the sense data buffer
            uint8_t  senseDataOffset  = UINT8_C(8);
            uint8_t  descriptorLength = UINT8_C(0);
            uint8_t  counter          = UINT8_C(0);
            uint32_t descriptorOffset = UINT32_C(0);
            while (counter < descriptorCount)
            {
                descriptorLength = descriptor[descriptorOffset + 1] + 1;
                safe_memcpy(&senseData[senseDataOffset], SPC3_SENSE_LEN - senseDataOffset, descriptor,
                            descriptorLength);
                additionalSenseLength += descriptorLength;
                ++counter;
                descriptorOffset += descriptorLength;
                senseDataOffset += descriptorLength;
                if (descriptor[descriptorOffset] == 9)
                {
                    descriptorOffset +=
                        1; // adding 1 since we put the log index in one extra byte, which is non standard and not part
                           // of the actual descriptor. This handles going on to the next descriptor in the list
                           // properly. We don't return this byte in the descriptor anyways
                }
            }
        }
        // set the additional sense length
        senseData[7] = additionalSenseLength;
    }
    else
    {
        senseData[0] = SCSI_SENSE_CUR_INFO_FIXED;
        // sense key
        senseData[2] |= M_Nibble0(senseKey);
        // asc
        senseData[12] = asc;
        // ascq
        senseData[13] = ascq;
        // additional sense length
        additionalSenseLength = 10;
        senseData[7]          = additionalSenseLength;
        if (descriptor)
        {
            uint8_t /*senseDataOffset = 8,*/ descriptorLength = 0, counter = 0;
            uint32_t                         descriptorOffset = UINT32_C(0);
            while (counter < descriptorCount)
            {
                uint8_t descriptorType = descriptor[0];
                descriptorLength       = descriptor[descriptorCount] + 1;
                switch (descriptorType)
                {
                case 0: // information
                {
                    uint64_t descriptorInformation;
                    if (descriptor[descriptorOffset + 2] & BIT7)
                    {
                        senseData[0] |= BIT7; // set the valid bit
                    }
                    descriptorInformation =
                        M_BytesTo8ByteValue(descriptor[descriptorOffset + 4], descriptor[descriptorOffset + 5],
                                            descriptor[descriptorOffset + 6], descriptor[descriptorOffset + 7],
                                            descriptor[descriptorOffset + 8], descriptor[descriptorOffset + 9],
                                            descriptor[descriptorOffset + 10], descriptor[descriptorOffset + 11]);
                    if (descriptorInformation > UINT32_MAX)
                    {
                        safe_memset(&senseData[3], SPC3_SENSE_LEN - 3, UINT8_MAX, 4);
                    }
                    else
                    {
                        // copy lowest 4 bytes
                        safe_memcpy(&senseData[3], SPC3_SENSE_LEN - 3, &descriptor[descriptorOffset + 8], 4);
                    }
                }
                break;
                case 1: // command specific information
                {
                    uint64_t descriptorCmdInformation =
                        M_BytesTo8ByteValue(descriptor[descriptorOffset + 4], descriptor[descriptorOffset + 5],
                                            descriptor[descriptorOffset + 6], descriptor[descriptorOffset + 7],
                                            descriptor[descriptorOffset + 8], descriptor[descriptorOffset + 9],
                                            descriptor[descriptorOffset + 10], descriptor[descriptorOffset + 11]);
                    if (descriptorCmdInformation > UINT32_MAX)
                    {
                        safe_memset(&senseData[8], SPC3_SENSE_LEN - 8, UINT8_MAX, 4);
                    }
                    else
                    {
                        // copy lowest 4 bytes
                        safe_memcpy(&senseData[8], SPC3_SENSE_LEN - 8, &descriptor[descriptorOffset + 8], 4);
                    }
                }
                break;
                case 2: // sense key specific
                    // bytes 4, 5 , and 6
                    safe_memcpy(&senseData[15], SPC3_SENSE_LEN - 15, &descriptor[descriptorOffset + 4], 3);
                    break;
                case 3: // FRU
                    senseData[14] = descriptor[descriptorOffset + 3];
                    break;
                case 4: // Stream Commands
                    if (descriptor[descriptorOffset + 3] & BIT7)
                    {
                        // set filemark bit
                        senseData[2] |= BIT7;
                    }
                    if (descriptor[descriptorOffset + 3] & BIT6)
                    {
                        // set end of media bit
                        senseData[2] |= BIT6;
                    }
                    if (descriptor[descriptorOffset + 3] & BIT5)
                    {
                        // set illegal length indicator bit
                        senseData[2] |= BIT5;
                    }
                    break;
                case 5: // Block Commands
                    if (descriptor[descriptorOffset + 3] & BIT5)
                    {
                        // set illegal length indicator bit
                        senseData[2] |= BIT5;
                    }
                    break;
                case 6: // OSD Object Identification
                case 7: // OSD Response Integrity Value
                case 8: // OSD Attribute identification
                    // can't handle this type
                    break;
                case 9:                   // ATA Status return
                    senseData[0] |= BIT7; // set the valid bit since we are filling in the information field
                    // parse out the registers as best we can
                    // information offsets bytes 3-7
                    senseData[3] = descriptor[descriptorOffset + 3];  // error
                    senseData[4] = descriptor[descriptorOffset + 13]; // status
                    senseData[5] = descriptor[descriptorOffset + 12]; // device
                    senseData[6] = descriptor[descriptorOffset + 5];  // count 7:0
                    // command specific information bytes 8-11
                    if (descriptor[descriptorOffset + 2] & BIT0)
                    {
                        // extend bit set from issuing an extended command
                        senseData[8] |= BIT7;
                    }
                    if (descriptor[descriptorOffset + 10] || descriptor[descriptorOffset + 8] ||
                        descriptor[descriptorOffset + 6])
                    {
                        // set upper LBA non-zero bit
                        senseData[8] |= BIT5;
                    }
                    if (descriptor[descriptorOffset + 4])
                    {
                        // set sector count 15:8 non-zero bit
                        senseData[8] |= BIT6;
                    }
                    senseData[8] |=
                        M_Nibble0(descriptor[descriptorOffset +
                                             14]); // setting the log index...this offset is nonstandard, so we will
                                                   // need to increment the offset one before leaving this case
                    senseData[9]  = descriptor[descriptorOffset + 7];  // lba
                    senseData[10] = descriptor[descriptorOffset + 9];  // lba
                    senseData[11] = descriptor[descriptorOffset + 11]; // lba
                    descriptorOffset += 1; // setting this since we added the log index in the last byte of this
                                           // descriptor to make this easier to pass around
                    break;
                case 10: // Another Progress Indication
                case 11: // User Data Segment Referral
                case 12: // Forwarded Sense data
                    // cannot handle this in fixed format
                    break;
                case 13: // Direct Access Block Device
                    if (descriptor[descriptorOffset + 2] & BIT7)
                    {
                        senseData[0] |= BIT7; // set the valid bit
                    }
                    if (descriptor[descriptorOffset + 2] & BIT5)
                    {
                        // set illegal length indicator bit
                        senseData[2] |= BIT5;
                    }
                    // bytes 4, 5 , and 6 for sense key specific information
                    safe_memcpy(&senseData[15], SPC3_SENSE_LEN - 15, &descriptor[descriptorOffset + 4], 3);
                    // fru code
                    senseData[14] = descriptor[descriptorOffset + 7];
                    {
                        // information
                        uint64_t descriptorInformation =
                            M_BytesTo8ByteValue(descriptor[descriptorOffset + 8], descriptor[descriptorOffset + 9],
                                                descriptor[descriptorOffset + 10], descriptor[descriptorOffset + 11],
                                                descriptor[descriptorOffset + 12], descriptor[descriptorOffset + 13],
                                                descriptor[descriptorOffset + 14], descriptor[descriptorOffset + 15]);
                        uint64_t descriptorCmdInformation;
                        if (descriptorInformation > UINT32_MAX)
                        {
                            safe_memset(&senseData[3], SPC3_SENSE_LEN - 3, UINT8_MAX, 4);
                        }
                        else
                        {
                            // copy lowest 4 bytes
                            safe_memcpy(&senseData[3], SPC3_SENSE_LEN - 3, &descriptor[descriptorOffset + 12], 4);
                        }
                        // command specific information
                        descriptorCmdInformation =
                            M_BytesTo8ByteValue(descriptor[descriptorOffset + 16], descriptor[descriptorOffset + 17],
                                                descriptor[descriptorOffset + 18], descriptor[descriptorOffset + 19],
                                                descriptor[descriptorOffset + 20], descriptor[descriptorOffset + 21],
                                                descriptor[descriptorOffset + 22], descriptor[descriptorOffset + 23]);
                        if (descriptorCmdInformation > UINT32_MAX)
                        {
                            safe_memset(&senseData[8], SPC3_SENSE_LEN - 8, UINT8_MAX, 4);
                        }
                        else
                        {
                            // copy lowest 4 bytes
                            safe_memcpy(&senseData[8], SPC3_SENSE_LEN - 8, &descriptor[descriptorOffset + 20], 4);
                        }
                    }
                    break;
                default: // unsupported, break
                    // can't handle this type
                    break;
                }
                ++counter;
                descriptorOffset += descriptorLength;
                // senseDataOffset += descriptorLength;
            }
        }
    }
    if (sensePtr)
    {
        safe_memcpy(sensePtr, senseDataLength, senseData, senseDataLength);
    }
}

static void set_Sense_Data_By_RTFRs(tDevice* device, ataReturnTFRs* rtfrs, uint8_t* sensePtr, uint32_t senseDataLength)
{
    // first check if sense data reporting is supported
    uint8_t senseKey                   = UINT8_C(0);
    uint8_t asc                        = UINT8_C(0);
    uint8_t ascq                       = UINT8_C(0);
    bool    returnSenseKeySpecificInfo = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, informationSenseDescriptor, 12);

    // process rtfrs from here on out
    if (rtfrs->status & BIT5) // device fault
    {
        senseKey = SENSE_KEY_HARDWARE_ERROR;
        asc      = 0x44;
        ascq     = 0;
    }
    else if (rtfrs->status & BIT0)
    {
        // nm - no media? - BIT1 (ATA/ATAPI7)
        if (rtfrs->error & BIT6) // uncorrectable read error
        {
            uint64_t returnedLBA = M_BytesTo8ByteValue(0, 0, rtfrs->lbaHiExt, rtfrs->lbaMidExt, rtfrs->lbaLowExt,
                                                       rtfrs->lbaHi, rtfrs->lbaMid, rtfrs->lbaLow);
            senseKey             = SENSE_KEY_MEDIUM_ERROR;
            asc                  = 0x11;
            ascq                 = 0;
            // Need to set the LBA into a descriptor to return
            returnSenseKeySpecificInfo    = true;
            informationSenseDescriptor[0] = 0;
            informationSenseDescriptor[1] = 0x0A;
            informationSenseDescriptor[2] = RESERVED;
            informationSenseDescriptor[2] |= BIT7;
            informationSenseDescriptor[3] = RESERVED;
            // bytes 4 through 11 should hold the lba
            informationSenseDescriptor[4]  = M_Byte7(returnedLBA);
            informationSenseDescriptor[5]  = M_Byte6(returnedLBA);
            informationSenseDescriptor[6]  = M_Byte5(returnedLBA);
            informationSenseDescriptor[7]  = M_Byte4(returnedLBA);
            informationSenseDescriptor[8]  = M_Byte3(returnedLBA);
            informationSenseDescriptor[9]  = M_Byte2(returnedLBA);
            informationSenseDescriptor[10] = M_Byte1(returnedLBA);
            informationSenseDescriptor[11] = M_Byte0(returnedLBA);
        }
        // wp - write protected? also bit 6....not sure how to differentiate between both cases(ATA/ATAPI7)
        else if (rtfrs->error & BIT4) // idnf
        {
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
            asc      = 0x21;
            ascq     = 0x00;
        }
        // mc - medium changed? - BIT5(ATA/ATAPI7)
        // mcr - medium change removal? -BIT3(ATA/ATAPI7)
        else if (rtfrs->error & BIT7) // interface crc error
        {
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            asc      = 0x47;
            ascq     = 0x03;
        }
        // NOLINTBEGIN(bugprone-branch-clone)
        else if (rtfrs->error & BIT2) // abort - only valid when no other bits set
        {
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            asc      = 0x00;
            ascq     = 0x00;
        }
        else
        {
            // uhh..we shouldnt be here
            senseKey = SENSE_KEY_ABORTED_COMMAND;
            asc      = 0x00;
            ascq     = 0x00;
        }
        // NOLINTEND(bugprone-branch-clone)
    }
    // We processed the error above according to other RTFRs, but if the sense data available bit is set, then we should
    // request sense data and return that info back up instead of our translated info
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /*words 119, 120 valid*/
        && (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT6 && rtfrs->status & BIT1))
    {
        ataReturnTFRs rtfrBackup;
        safe_memcpy(&rtfrBackup, sizeof(ataReturnTFRs), rtfrs, sizeof(ataReturnTFRs));
        // if everything goes according to plan, we will return success, otherwise restore back up rtfrs and process
        // based on those.
        if (SUCCESS != ata_Request_Sense_Data(device, &senseKey, &asc, &ascq))
        {
            // restore backed up rtfrs
            safe_memcpy(rtfrs, sizeof(ataReturnTFRs), &rtfrBackup, sizeof(ataReturnTFRs));
        }
    }
    if (returnSenseKeySpecificInfo)
    {
        set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       informationSenseDescriptor, 1);
    }
    else
    {
        set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    }
}

// To be used by the SATL when issuing a single read command (non-ncq)
// not using NCQ as this software translation cannot issue NCQ commands reliably, so issuing what we can issue here.-TJE
static eReturnValues satl_Read_Command(ScsiIoCtx* scsiIoCtx,
                                       uint64_t   lba,
                                       uint8_t*   ptrData,
                                       uint32_t   dataSize,
                                       bool       fua)
{
    eReturnValues ret          = SUCCESS;
    bool          dmaSupported = false;
    if (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word049) & BIT8) // lba mode
    {
        if ((le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word063) & (BIT0 | BIT1 | BIT2)) ||
            (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word088) & 0xFF))
        {
            dmaSupported = true;
        }
    }

    // check if 48bit
    if (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word083) & BIT10)
    {
        uint16_t scnt = C_CAST(uint16_t, dataSize / scsiIoCtx->device->drive_info.deviceBlockSize);
        if (dataSize > (UINT32_C(65536) * scsiIoCtx->device->drive_info.deviceBlockSize))
        {
            // return an error
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           M_NULLPTR, 0);
            return SUCCESS;
        }
        if (fua && le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word085) & BIT5)
        {
            // send a read verify command first
            ret = ata_Read_Verify_Sectors(scsiIoCtx->device, true, scnt, lba);
        }
        if (dmaSupported)
        {
            ret = ata_Read_DMA(scsiIoCtx->device, lba, ptrData, scnt, dataSize, true);
        }
        else // pio
        {
            ret = ata_Read_Sectors(scsiIoCtx->device, lba, ptrData, scnt, dataSize, true);
        }
    }
    else // 28bit command
    {
        uint16_t scnt;
        if (dataSize > (256 * scsiIoCtx->device->drive_info.deviceBlockSize))
        {
            // return an error
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           M_NULLPTR, 0);
            return SUCCESS;
        }
        scnt = C_CAST(uint16_t, dataSize / scsiIoCtx->device->drive_info.deviceBlockSize);
        if (scnt == 256)
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            scnt = 0;
        }
        if (fua && le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word085) & BIT5)
        {
            // send a read verify command first
            ret = ata_Read_Verify_Sectors(scsiIoCtx->device, false, scnt, lba);
        }
        if (dmaSupported)
        {
            ret = ata_Read_DMA(scsiIoCtx->device, lba, ptrData, scnt, dataSize, false);
        }
        else // pio
        {
            ret = ata_Read_Sectors(scsiIoCtx->device, lba, ptrData, scnt, dataSize, false);
        }
    }
    // now set sense data
    set_Sense_Data_By_RTFRs(scsiIoCtx->device, &scsiIoCtx->device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                            scsiIoCtx->senseDataSize);
    return ret;
}
// To be used by the SATL when issuing a single write command
static eReturnValues satl_Write_Command(ScsiIoCtx* scsiIoCtx,
                                        uint64_t   lba,
                                        uint8_t*   ptrData,
                                        uint32_t   dataSize,
                                        bool       fua)
{
    eReturnValues ret          = SUCCESS;
    bool          dmaSupported = false;
    if (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word049) & BIT8) // lba mode
    {
        if ((le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word063) & (BIT0 | BIT1 | BIT2)) ||
            (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word088) & 0xFF))
        {
            dmaSupported = true;
        }
    }
    // check if 48bit
    if (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word083) & BIT10)
    {
        uint32_t scnt = dataSize / scsiIoCtx->device->drive_info.deviceBlockSize;
        if (dataSize > (UINT32_C(65536) * scsiIoCtx->device->drive_info.deviceBlockSize))
        {
            // return an error
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           M_NULLPTR, 0);
            return SUCCESS;
        }
        if (scnt == UINT32_C(65536))
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            scnt = 0;
        }
        if (dmaSupported)
        {
            ret = ata_Write_DMA(scsiIoCtx->device, lba, ptrData, dataSize, true, fua);
        }
        else // pio
        {
            ret = ata_Write_Sectors(scsiIoCtx->device, lba, ptrData, dataSize, true);
            if (fua)
            {
                // read verify command
                ret = ata_Read_Verify_Sectors(scsiIoCtx->device, true, C_CAST(uint16_t, scnt), lba);
            }
        }
    }
    else // 28bit command
    {
        uint16_t scnt = C_CAST(uint16_t, dataSize / scsiIoCtx->device->drive_info.deviceBlockSize);
        if (scnt > 256)
        {
            // return an error
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           M_NULLPTR, 0);
            return SUCCESS;
        }
        if (scnt == 256)
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            scnt = 0;
        }
        if (dmaSupported)
        {
            ret = ata_Write_DMA(scsiIoCtx->device, lba, ptrData, dataSize, false, false);
        }
        else // pio
        {
            ret = ata_Write_Sectors(scsiIoCtx->device, lba, ptrData, dataSize, false);
        }
        if (fua)
        {
            // read verify command
            ret = ata_Read_Verify_Sectors(scsiIoCtx->device, false, scnt, lba);
        }
    }
    // now set sense data
    set_Sense_Data_By_RTFRs(scsiIoCtx->device, &scsiIoCtx->device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                            scsiIoCtx->senseDataSize);
    return ret;
}
// To be used by the SATL when issuing a single read-verify command (or read and compare data bytes if bytecheck is 1)
static eReturnValues satl_Read_Verify_Command(ScsiIoCtx* scsiIoCtx, uint64_t lba, uint32_t dataSize, uint8_t byteCheck)
{
    eReturnValues ret                = SUCCESS;
    bool          dmaSupported       = false;
    uint32_t      verificationLength = dataSize / scsiIoCtx->device->drive_info.deviceBlockSize;
    if (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word049) & BIT8) // lba mode
    {
        if ((le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word063) & (BIT0 | BIT1 | BIT2)) ||
            (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word088) & 0xFF))
        {
            dmaSupported = true;
        }
    }
    // check if 48bit
    if (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word083) & BIT10)
    {
        if (verificationLength == UINT32_C(65536))
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            verificationLength = 0;
        }
        if (byteCheck == 0)
        {
            // send ata read-verify
            ret = ata_Read_Verify_Sectors(scsiIoCtx->device, true, C_CAST(uint16_t, verificationLength), lba);
        }
        else
        {
            uint8_t* compareBuf = M_NULLPTR;
            // uint32_t compareLength = UINT32_C(0);
            if (byteCheck == 0x02)
            {
                // return an error
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return SUCCESS;
            }
            compareBuf = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t),
                                                                          scsiIoCtx->device->os_info.minimumAlignment));
            if (!compareBuf)
            {
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return MEMORY_FAILURE;
            }
            if (dmaSupported)
            {
                ret = ata_Read_DMA(scsiIoCtx->device, lba, compareBuf, C_CAST(uint16_t, verificationLength),
                                   scsiIoCtx->dataLength, true);
            }
            else // pio
            {
                ret = ata_Read_Sectors(scsiIoCtx->device, lba, compareBuf, C_CAST(uint16_t, verificationLength),
                                       scsiIoCtx->dataLength, true);
            }
            bool errorFound = false;
            if (byteCheck == 0x01)
            {
                if (memcmp(compareBuf, scsiIoCtx->pdata, scsiIoCtx->dataLength) != 0)
                {
                    // does not match - set miscompare error
                    errorFound = true;
                }
            }
            else if (byteCheck == 0x03)
            {
                // compare each logical sector to the data sent into this command

                uint32_t iter = UINT32_C(0);
                for (; iter < scsiIoCtx->dataLength; iter += scsiIoCtx->device->drive_info.deviceBlockSize)
                {
                    if (memcmp(&compareBuf[iter], scsiIoCtx->pdata,
                               M_Min(scsiIoCtx->device->drive_info.deviceBlockSize, scsiIoCtx->dataLength)) != 0)
                    {
                        errorFound = true;
                        break;
                    }
                }
            }
            safe_free_aligned(&compareBuf);
            if (errorFound)
            {
                // set failure
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MISCOMPARE, 0x1D, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return SUCCESS;
            }
            else
            {
                // set success
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return SUCCESS;
            }
        }
    }
    else // 28bit command
    {
        if (verificationLength > 256)
        {
            // return an error
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           M_NULLPTR, 0);
            return SUCCESS;
        }
        if (verificationLength == 256)
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            verificationLength = 0;
        }
        if (byteCheck == 0)
        {
            // send ata read-verify
            ret = ata_Read_Verify_Sectors(scsiIoCtx->device, false, C_CAST(uint16_t, verificationLength), lba);
        }
        else
        {
            uint8_t* compareBuf = M_NULLPTR;
            // uint32_t compareLength = UINT32_C(0);
            if (byteCheck == 0x02)
            {
                // return an error
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return SUCCESS;
            }
            compareBuf = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t),
                                                                          scsiIoCtx->device->os_info.minimumAlignment));
            if (!compareBuf)
            {
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return MEMORY_FAILURE;
            }
            if (dmaSupported)
            {
                ret = ata_Read_DMA(scsiIoCtx->device, lba, compareBuf, C_CAST(uint16_t, verificationLength),
                                   scsiIoCtx->dataLength, false);
            }
            else // pio
            {
                ret = ata_Read_Sectors(scsiIoCtx->device, lba, compareBuf, C_CAST(uint16_t, verificationLength),
                                       scsiIoCtx->dataLength, false);
            }
            bool errorFound = false;
            if (byteCheck == 0x01)
            {
                if (memcmp(compareBuf, scsiIoCtx->pdata, scsiIoCtx->dataLength) != 0)
                {
                    // does not match - set miscompare error
                    errorFound = true;
                }
            }
            else if (byteCheck == 0x03)
            {
                // compare each logical sector to the data sent into this command

                uint32_t iter = UINT32_C(0);
                for (; iter < scsiIoCtx->dataLength; iter += scsiIoCtx->device->drive_info.deviceBlockSize)
                {
                    if (memcmp(&compareBuf[iter], scsiIoCtx->pdata,
                               M_Min(scsiIoCtx->device->drive_info.deviceBlockSize, scsiIoCtx->dataLength)) != 0)
                    {
                        errorFound = true;
                        break;
                    }
                }
            }
            safe_free_aligned(&compareBuf);
            if (errorFound)
            {
                // set failure
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MISCOMPARE, 0x1D, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return SUCCESS;
            }
            else
            {
                // set success
                set_Sense_Data_For_Translation(
                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00, 0x00,
                    scsiIoCtx->device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                return SUCCESS;
            }
        }
    }
    // now set sense data
    set_Sense_Data_By_RTFRs(scsiIoCtx->device, &scsiIoCtx->device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                            scsiIoCtx->senseDataSize);
    return ret;
}

static eReturnValues satl_Sequential_Write_Commands(ScsiIoCtx* scsiIoCtx,
                                                    uint64_t   startLba,
                                                    uint64_t   range,
                                                    uint8_t*   pattern,
                                                    uint32_t   patternLength)
{
    eReturnValues ret = SUCCESS;
    // use this function to issue the writes.
    // eReturnValues satl_Write_Command(ScsiIoCtx *scsiIoCtx, uint64_t lba, uint8_t *ptrData, uint32_t dataSize, bool
    // fua)
    uint64_t endingLBA    = startLba + range;
    uint32_t numberOfLBAs = patternLength / scsiIoCtx->device->drive_info.deviceBlockSize;
    for (; startLba < endingLBA; startLba += numberOfLBAs)
    {
        if ((startLba + numberOfLBAs) > endingLBA)
        {
            // end of range...don't over do it!
            numberOfLBAs  = C_CAST(uint32_t, endingLBA - startLba);
            patternLength = numberOfLBAs * scsiIoCtx->device->drive_info.deviceBlockSize;
        }
        ret = satl_Write_Command(scsiIoCtx, startLba, pattern, patternLength, false);
        if (ret != SUCCESS)
        {
            set_Sense_Data_By_RTFRs(scsiIoCtx->device, &scsiIoCtx->device->drive_info.lastCommandRTFRs,
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize);
        }
    }
    return ret;
}

static bool are_RTFRs_Non_Zero_From_Identify(ataReturnTFRs* identRTFRs)
{
    bool nonZero = false;
    if (identRTFRs->device != 0 || identRTFRs->lbaHi != 0 || identRTFRs->lbaMid != 0 || identRTFRs->lbaLow != 0 ||
        identRTFRs->secCnt != 0
        // NOTE: Not checking error or status because those could be dummied up in low level passthrough stuff on some
        // occasions - TJE
    )
    {
        nonZero = true;
    }
    return nonZero;
}

static eReturnValues translate_ATA_Information_VPD_Page_89h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret              = SUCCESS;
    uint8_t       peripheralDevice = UINT8_C(0);
    uint8_t       commandCode      = ATA_IDENTIFY;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, identifyDriveData, 512);
#define SAT_ATA_INFO_VPD_PAGE_LEN_SOFTSATL (572)
    DECLARE_ZERO_INIT_ARRAY(uint8_t, ataInformation, SAT_ATA_INFO_VPD_PAGE_LEN_SOFTSATL);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.softSATFlags.identifyDeviceDataLogSupported)
    {
        if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_COPY_OF_IDENTIFY_DATA,
                                        identifyDriveData, LEGACY_DRIVE_SEC_SIZE,
                                        device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
        {
            if (SUCCESS != ata_Identify(device, identifyDriveData, LEGACY_DRIVE_SEC_SIZE))
            {
                return FAILURE;
            }
        }
        else
        {
            if (device->drive_info.ata_Options.readLogWriteLogDMASupported)
            {
                commandCode = ATA_READ_LOG_EXT_DMA;
            }
            else
            {
                commandCode = ATA_READ_LOG_EXT;
            }
        }
    }
    else
#endif // SAT_SPEC_SUPPORTED
        if (SUCCESS != ata_Identify(device, identifyDriveData, LEGACY_DRIVE_SEC_SIZE))
        {
            // that failed, so try an identify packet device
            if (SUCCESS != ata_Identify_Packet_Device(device, identifyDriveData, LEGACY_DRIVE_SEC_SIZE))
            {
                // if we still didn't get anything, then it's time to return a failure
                return FAILURE;
            }
            peripheralDevice = 0x05;
            commandCode      = ATAPI_IDENTIFY;
        }
    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                   device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    ataInformation[0] = peripheralDevice;
    ataInformation[1] = ATA_INFORMATION;
    ataInformation[2] = M_Byte1(VPD_ATA_INFORMATION_LEN);
    ataInformation[3] = M_Byte0(VPD_ATA_INFORMATION_LEN);
    ataInformation[4] = RESERVED;
    ataInformation[5] = RESERVED;
    ataInformation[6] = RESERVED;
    ataInformation[7] = RESERVED;
    // SAT Vendor -set to SEAGATE
    ataInformation[8]  = 'S';
    ataInformation[9]  = 'E';
    ataInformation[10] = 'A';
    ataInformation[11] = 'G';
    ataInformation[12] = 'A';
    ataInformation[13] = 'T';
    ataInformation[14] = 'E';
    ataInformation[15] = ' ';
    // SAT Product ID -set to opensea
    ataInformation[16] = 'o';
    ataInformation[17] = 'p';
    ataInformation[18] = 'e';
    ataInformation[19] = 'n';
    ataInformation[20] = 's';
    ataInformation[21] = 'e';
    ataInformation[22] = 'a';
    ataInformation[23] = ' ';
    DECLARE_ZERO_INIT_ARRAY(char, openseaVersionString, 9);

    snprintf_err_handle(openseaVersionString, 9, "%d.%d.%d", OPENSEA_TRANSPORT_MAJOR_VERSION,
                        OPENSEA_TRANSPORT_MINOR_VERSION, OPENSEA_TRANSPORT_PATCH_VERSION);

    if (safe_strlen(openseaVersionString) < 8)
    {
        ataInformation[24] = ' ';
        safe_memcpy(&ataInformation[25], SAT_ATA_INFO_VPD_PAGE_LEN_SOFTSATL - 25, openseaVersionString,
                    safe_strlen(openseaVersionString));
        // snprintf_err_handle(C_CAST(char*, &ataInformation[24]), 8, " %-s", openseaVersionString);
    }
    else
    {
        safe_memcpy(&ataInformation[24], SAT_ATA_INFO_VPD_PAGE_LEN_SOFTSATL - 24, openseaVersionString, 8);
        // snprintf_err_handle(C_CAST(char*, &ataInformation[24]), 8, "%-s", openseaVersionString);
    }
    // SAT Product Revision -set to SAT Version supported by library
    ataInformation[32] = 'S';
    ataInformation[33] = 'A';
    ataInformation[34] = 'T';
#if defined(SAT_SPEC_SUPPORTED)
    ataInformation[35] =
        48 + SAT_SPEC_SUPPORTED; // set's the ascii character for which sat level the code is enabled for
#else
    ataInformation[35] = '?';
#endif // SAT_SPEC_SUPPORTED
    // device signature (response from identify command)
    if (is_ATA_Identify_Word_Valid_SATA(
            device->drive_info.IdentifyData.ata.Word076)) // Only Serial ATA Devices will set the bits in words 76-79
    {
        ataInformation[36] = 0x34;
    }
    else // PATA
    {
        ataInformation[36] = 0x00;
    }
    // leaving PMPort and other stuff at 0 since I don't know those-TJE
    if ((commandCode == ATA_IDENTIFY || commandCode == ATAPI_IDENTIFY) &&
        are_RTFRs_Non_Zero_From_Identify(&device->drive_info.lastCommandRTFRs))
    {
        // In this case, we got the rtfrs from the drive and can use them to set the signature instead of dummying it up
        ataInformation[38] = device->drive_info.lastCommandRTFRs.status;
        ataInformation[39] = device->drive_info.lastCommandRTFRs.error;
        ataInformation[40] = device->drive_info.lastCommandRTFRs.lbaLow;
        ataInformation[41] = device->drive_info.lastCommandRTFRs.lbaMid;
        ataInformation[42] = device->drive_info.lastCommandRTFRs.lbaHi;
        ataInformation[43] = device->drive_info.lastCommandRTFRs.device;
        ataInformation[44] = device->drive_info.lastCommandRTFRs.lbaLowExt;
        ataInformation[45] = device->drive_info.lastCommandRTFRs.lbaMidExt;
        ataInformation[46] = device->drive_info.lastCommandRTFRs.lbaHiExt;
        ataInformation[47] = RESERVED;
        ataInformation[48] = device->drive_info.lastCommandRTFRs.secCnt;
        ataInformation[49] = device->drive_info.lastCommandRTFRs.secCntExt;
        ataInformation[50] = RESERVED;
        ataInformation[51] = RESERVED;
        ataInformation[52] = RESERVED;
        ataInformation[53] = RESERVED;
        ataInformation[54] = RESERVED;
        ataInformation[55] = RESERVED;
    }
    else
    {
        if (peripheralDevice == 0 || peripheralDevice == 0x14) // ATA
        {
            // Set ATA Device signature
            // this is set by what I could find in the SATA spec. I'm assuming if we made it to here in the code, the
            // drive sent a good signature. - TJE
            ataInformation[38] = 0x50; // status - 00h..70h
            ataInformation[39] = 0x01; // error - 01h
            ataInformation[40] = 0x01; // lbalo - 01h
            ataInformation[41] = 0x00; // lbamid - 00h
            ataInformation[42] = 0x00; // lbahi - 00h
            if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
            {
                ataInformation[43] |= DEVICE_REG_BACKWARDS_COMPATIBLE_BITS; // device - NA
            }
            if (device->drive_info.ata_Options.isDevice1)
            {
                ataInformation[43] |= DEVICE_SELECT_BIT;
            }
            ataInformation[44] = 0x00; // lbaloext - not specified
            ataInformation[45] = 0x00; // lbamidext - not specified
            ataInformation[46] = 0x00; // lbahiext - not specified
            ataInformation[47] = RESERVED;
            ataInformation[48] = 0x01; // seccnt - 01h
            ataInformation[49] = 0x00; // seccntext - not specified
            ataInformation[50] = RESERVED;
            ataInformation[51] = RESERVED;
            ataInformation[52] = RESERVED;
            ataInformation[53] = RESERVED;
            ataInformation[54] = RESERVED;
            ataInformation[55] = RESERVED;
            if (peripheralDevice == 0x14)
            {
                ataInformation[41] = 0xCD; // lbamid - CDh
                ataInformation[42] = 0xAB; // lbahi - ABh
            }
        }
        else // ATAPI
        {
            // this is set by what I could find in the SATA spec. I'm assuming if we made it to here in the code, the
            // drive sent a good signature. - TJE
            ataInformation[38] = 0x00; // status - 00h
            ataInformation[39] = 0x01; // error - 01h
            ataInformation[40] = 0x01; // lbalo - 01h
            ataInformation[41] = 0x14; // lbamid - 14h
            ataInformation[42] = 0xEB; // lbahi - EBh
            if (!device->drive_info.ata_Options.noNeedLegacyDeviceHeadCompatBits)
            {
                ataInformation[43] |= DEVICE_REG_BACKWARDS_COMPATIBLE_BITS; // device - NA
            }
            if (device->drive_info.ata_Options.isDevice1)
            {
                ataInformation[43] |= DEVICE_SELECT_BIT;
            }
            ataInformation[44] = 0x00; // lbaloext - not specified
            ataInformation[45] = 0x00; // lbamidext - not specified
            ataInformation[46] = 0x00; // lbahiext - not specified
            ataInformation[47] = RESERVED;
            ataInformation[48] = 0x01; // seccnt - 01h
            ataInformation[49] = 0x00; // seccntext - not specified
            ataInformation[50] = RESERVED;
            ataInformation[51] = RESERVED;
            ataInformation[52] = RESERVED;
            ataInformation[53] = RESERVED;
            ataInformation[54] = RESERVED;
            ataInformation[55] = RESERVED;
        }
    }
    // command code
    ataInformation[56] = commandCode;
    // reserved
    ataInformation[57] = RESERVED;
    ataInformation[58] = RESERVED;
    ataInformation[59] = RESERVED;
    // identify device data
    safe_memcpy(&ataInformation[60], SAT_ATA_INFO_VPD_PAGE_LEN_SOFTSATL - 60, identifyDriveData, LEGACY_DRIVE_SEC_SIZE);
    // now copy all the data we set up back to the scsi io ctx
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, ataInformation,
                    M_Min(scsiIoCtx->dataLength, SAT_ATA_INFO_VPD_PAGE_LEN_SOFTSATL));
    }
    return ret;
}

static eReturnValues translate_Unit_Serial_Number_VPD_Page_80h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
#define SOFT_SATL_UNIT_SN_LEN (24)
    DECLARE_ZERO_INIT_ARRAY(uint8_t, unitSerialNumber, SOFT_SATL_UNIT_SN_LEN);
    DECLARE_ZERO_INIT_ARRAY(char, ataSerialNumber, SERIAL_NUM_LEN + 1);
    uint8_t peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    unitSerialNumber[0] = peripheralDevice;
    // use the cached information
    safe_memcpy(ataSerialNumber, ATA_IDENTIFY_SN_LENGTH + 1, device->drive_info.IdentifyData.ata.SerNum,
                SERIAL_NUM_LEN);
    // now byteswap the string
    byte_Swap_String_Len(ataSerialNumber, SERIAL_NUM_LEN);
    unitSerialNumber[1] = UNIT_SERIAL_NUMBER;
    unitSerialNumber[2] = M_Byte1(safe_strlen(ataSerialNumber));
    unitSerialNumber[3] = M_Byte0(safe_strlen(ataSerialNumber));
    // set the string into the data
    safe_memcpy(&unitSerialNumber[4], SOFT_SATL_UNIT_SN_LEN - 4, ataSerialNumber, safe_strlen(ataSerialNumber));
    // now copy all the data we set up back to the scsi io ctx
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, unitSerialNumber,
                    M_Min(SOFT_SATL_UNIT_SN_LEN, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Device_Identification_VPD_Page_83h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // naa designator
    uint8_t  naaDesignatorLength = UINT8_C(0); // will be set if drive supports the WWN
    uint8_t* naaDesignator       = M_NULLPTR;
    // scsi name string designator
    uint8_t  SCSINameStringDesignatorLength = UINT8_C(0);
    uint8_t* SCSINameStringDesignator       = M_NULLPTR;
    uint8_t  peripheralDevice               = UINT8_C(0);
// vars for t10 vendor id designator
#define SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN UINT32_C(72)
    DECLARE_ZERO_INIT_ARRAY(uint8_t, t10VendorIdDesignator, SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN);
    DECLARE_ZERO_INIT_ARRAY(char, ataModelNumber, ATA_IDENTIFY_MN_LENGTH + 1);
    DECLARE_ZERO_INIT_ARRAY(char, ataSerialNumber, ATA_IDENTIFY_SN_LENGTH + 1);
    const char* ataVendorId = "ATA     ";
    // will hold the complete data to return
    uint8_t* deviceIdentificationPage = M_NULLPTR;
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT8) // NAA and SCSI Name String
    {
        uint64_t wwn = M_WordsTo8ByteValue(le16_to_host(device->drive_info.IdentifyData.ata.Word108),
                                           le16_to_host(device->drive_info.IdentifyData.ata.Word109),
                                           le16_to_host(device->drive_info.IdentifyData.ata.Word110),
                                           device->drive_info.IdentifyData.ata.Word111);
#define SAT_SCSI_NAME_STRING_LENGTH 21
        DECLARE_ZERO_INIT_ARRAY(char, scsiNameString, SAT_SCSI_NAME_STRING_LENGTH);
        naaDesignatorLength = 12;
        // WWN Supported
        naaDesignator =
            M_REINTERPRET_CAST(uint8_t*, safe_calloc(naaDesignatorLength * sizeof(uint8_t), sizeof(uint8_t)));
        if (naaDesignator != M_NULLPTR)
        {
            naaDesignator[0]  = 1; // code set-set to one
            naaDesignator[1]  = 3; // designator type set to 3. Association set to zero. PIV set to zero
            naaDesignator[2]  = RESERVED;
            naaDesignator[3]  = 0x08; // length
            naaDesignator[4]  = M_Byte1(device->drive_info.IdentifyData.ata.Word108);
            naaDesignator[5]  = M_Byte0(device->drive_info.IdentifyData.ata.Word108);
            naaDesignator[6]  = M_Byte1(device->drive_info.IdentifyData.ata.Word109);
            naaDesignator[7]  = M_Byte0(device->drive_info.IdentifyData.ata.Word109);
            naaDesignator[8]  = M_Byte1(device->drive_info.IdentifyData.ata.Word110);
            naaDesignator[9]  = M_Byte0(device->drive_info.IdentifyData.ata.Word110);
            naaDesignator[10] = M_Byte1(device->drive_info.IdentifyData.ata.Word111);
            naaDesignator[11] = M_Byte0(device->drive_info.IdentifyData.ata.Word111);

            // now set up the scsi name string identifier
            snprintf_err_handle(&scsiNameString[0], SAT_SCSI_NAME_STRING_LENGTH, "naa.%" PRIX64, wwn);
            SCSINameStringDesignatorLength = 24;
            SCSINameStringDesignator       = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc(SCSINameStringDesignatorLength * sizeof(uint8_t), sizeof(uint8_t)));
            if (SCSINameStringDesignator != M_NULLPTR)
            {
                // now set this into the buffer
                SCSINameStringDesignator[0] = 3; // code set-set to three
                SCSINameStringDesignator[1] = 8; // designator type set to 8. Association set to zero. PIV set to zero
                SCSINameStringDesignator[2] = RESERVED;
                SCSINameStringDesignator[3] = 20U; // length
                safe_memcpy(&SCSINameStringDesignator[4], SCSINameStringDesignatorLength - 4, scsiNameString, 20);
            }
            else
            {
                SCSINameStringDesignatorLength = 0;
            }
        }
        else
        {
            naaDesignatorLength = 0;
        }
    }
    // always generate this designator from ATA Identifying data
    t10VendorIdDesignator[0] = 2; // code set-set to two
    t10VendorIdDesignator[1] = 1; // designator type set to 1. Association set to zero. PIV set to zero
    t10VendorIdDesignator[2] = RESERVED;
    t10VendorIdDesignator[3] = 68U; // length
    // set vendor ID to ATA padded with spaces
    safe_memcpy(&t10VendorIdDesignator[4], SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN - 4, ataVendorId, 8);
    // now set MN
    safe_memcpy(ataModelNumber, ATA_IDENTIFY_MN_LENGTH + 1, device->drive_info.IdentifyData.ata.ModelNum,
                ATA_IDENTIFY_MN_LENGTH);
    byte_Swap_String_Len(ataModelNumber, ATA_IDENTIFY_MN_LENGTH);
    safe_memcpy(&t10VendorIdDesignator[12], SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN - 12, ataModelNumber,
                ATA_IDENTIFY_MN_LENGTH);
    // now set SN
    safe_memcpy(ataSerialNumber, ATA_IDENTIFY_SN_LENGTH + 1, device->drive_info.IdentifyData.ata.SerNum,
                ATA_IDENTIFY_SN_LENGTH);
    byte_Swap_String_Len(ataSerialNumber, ATA_IDENTIFY_SN_LENGTH);
    safe_memcpy(&t10VendorIdDesignator[52], SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN - 52, ataSerialNumber,
                ATA_IDENTIFY_SN_LENGTH);

    // now setup the device identification page
    size_t devIDPageLen = UINT32_C(4) + SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN + C_CAST(uint32_t, naaDesignatorLength) +
                          C_CAST(uint32_t, SCSINameStringDesignatorLength);
    deviceIdentificationPage =
        M_REINTERPRET_CAST(uint8_t*, safe_calloc(devIDPageLen * sizeof(uint8_t), sizeof(uint8_t)));
    if (!deviceIdentificationPage)
    {
        safe_free(&SCSINameStringDesignator);
        safe_free(&naaDesignator);
        return MEMORY_FAILURE;
    }

#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    deviceIdentificationPage[0] = peripheralDevice;
    deviceIdentificationPage[1] = DEVICE_IDENTIFICATION;
    deviceIdentificationPage[2] =
        M_Byte1(SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN + naaDesignatorLength + SCSINameStringDesignatorLength);
    deviceIdentificationPage[3] =
        M_Byte0(SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN + naaDesignatorLength + SCSINameStringDesignatorLength);
    // copy naa first
    if (naaDesignatorLength > 0)
    {
        safe_memcpy(&deviceIdentificationPage[4], devIDPageLen - 4, naaDesignator, naaDesignatorLength);
    }
    // scsi name string second
    if (SCSINameStringDesignatorLength > 0)
    {
        safe_memcpy(&deviceIdentificationPage[4 + naaDesignatorLength], devIDPageLen - 4 - naaDesignatorLength,
                    SCSINameStringDesignator, SCSINameStringDesignatorLength);
    }
    // t10 vendor identification last
    safe_memcpy(&deviceIdentificationPage[4 + naaDesignatorLength + SCSINameStringDesignatorLength],
                devIDPageLen - 4 - naaDesignatorLength - SCSINameStringDesignatorLength, t10VendorIdDesignator,
                SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN);
    // now free the memory we no longer need
    safe_free(&naaDesignator);
    safe_free(&SCSINameStringDesignator);
    // copy the final data back for the command
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, deviceIdentificationPage,
                    M_Min(SOFT_SAT_T10_VENDOR_ID_DESIGNATOR_LEN + naaDesignatorLength + SCSINameStringDesignatorLength,
                          scsiIoCtx->dataLength));
    }
    safe_free(&deviceIdentificationPage);
    return ret;
}

static eReturnValues translate_Block_Device_Characteristics_VPD_Page_B1h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, blockDeviceCharacteriticsPage, 64);
    uint8_t peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    blockDeviceCharacteriticsPage[0] = peripheralDevice;
    blockDeviceCharacteriticsPage[1] = BLOCK_DEVICE_CHARACTERISTICS;
    blockDeviceCharacteriticsPage[2] = 0x00;
    blockDeviceCharacteriticsPage[3] = 0x3C;
    // rotation rate
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word217)))
    {
        blockDeviceCharacteriticsPage[4] = M_Byte1(device->drive_info.IdentifyData.ata.Word217);
        blockDeviceCharacteriticsPage[5] = M_Byte0(device->drive_info.IdentifyData.ata.Word217);
    }
    // product type
    blockDeviceCharacteriticsPage[6] = 0;
    // form factor
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word168)))
    {
        blockDeviceCharacteriticsPage[7] = M_Nibble0(device->drive_info.IdentifyData.ata.Word168);
    }
    switch (device->drive_info.zonedType)
    {
    case 1: // host aware
        blockDeviceCharacteriticsPage[8] |= BIT4;
        break;
    case 2: // device managed
        blockDeviceCharacteriticsPage[8] |= BIT5;
        break;
    case 3: // reserved
        blockDeviceCharacteriticsPage[8] |= BIT4 | BIT5;
        break;
    default: // not supported or host managed (host managed reported in the peripheral device type)
        break;
    }
    // set FUAB and VBULS to 1
    blockDeviceCharacteriticsPage[8] |= BIT1; // FUAB
    blockDeviceCharacteriticsPage[8] |= BIT0; // VBULS
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, blockDeviceCharacteriticsPage,
                    M_Min(64, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Power_Condition_VPD_Page_8Ah(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // assuming the drive supports the page since identify bit was checked before getting here.-TJE
    DECLARE_ZERO_INIT_ARRAY(
        uint8_t, powerConditionsLog,
        1024); // currently, this log is only two pages long and we want to read both for the data we're filling in-TJE
    if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_POWER_CONDITIONS, 0, powerConditionsLog, 1024,
                                    device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
    {
        ataPowerConditionsDescriptor* descriptor = M_NULLPTR;
        DECLARE_ZERO_INIT_ARRAY(uint8_t, powerConditionPage, 18);
        uint8_t peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
        if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
        {
            peripheralDevice = 0x14;
        }
#endif // SAT_SPEC_SUPPORTED
        powerConditionPage[0] = peripheralDevice;
        powerConditionPage[1] = POWER_CONDITION;
        powerConditionPage[2] = 0x00;
        powerConditionPage[3] = 0x0E;
        // stopped condition recovery time
        powerConditionPage[6] = 0x00;
        powerConditionPage[7] = 0x00;
        // standby y
        descriptor =
            C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[384 + LEGACY_DRIVE_SEC_SIZE]); // in second sector
        if (descriptor->powerConditionFlags & BIT7)
        {
            powerConditionPage[4] |= BIT1;
        }
        if (le32_to_host(descriptor->nomincalRecoveryTimeToPM0) > 0xFFFE)
        {
            powerConditionPage[10] = 0xFF;
            powerConditionPage[11] = 0xFF;
        }
        else
        {
            powerConditionPage[10] = M_Byte1(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
            powerConditionPage[11] = M_Byte0(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
        }
        // standby z
        descriptor =
            C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[448 + LEGACY_DRIVE_SEC_SIZE]); // in second sector
        if (descriptor->powerConditionFlags & BIT7)
        {
            powerConditionPage[4] |= BIT0;
        }
        if (le32_to_host(descriptor->nomincalRecoveryTimeToPM0) > 0xFFFE)
        {
            powerConditionPage[8] = 0xFF;
            powerConditionPage[9] = 0xFF;
        }
        else
        {
            powerConditionPage[8] = M_Byte1(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
            powerConditionPage[9] = M_Byte0(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
        }
        // idle a
        descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[0]);
        if (descriptor->powerConditionFlags & BIT7)
        {
            powerConditionPage[5] |= BIT0;
        }
        if (le32_to_host(descriptor->nomincalRecoveryTimeToPM0) > 0xFFFE)
        {
            powerConditionPage[8] = 0xFF;
            powerConditionPage[9] = 0xFF;
        }
        else
        {
            powerConditionPage[8] = M_Byte1(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
            powerConditionPage[9] = M_Byte0(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
        }
        // idle b
        descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[64]);
        if (descriptor->powerConditionFlags & BIT7)
        {
            powerConditionPage[5] |= BIT1;
        }
        if (le32_to_host(descriptor->nomincalRecoveryTimeToPM0) > 0xFFFE)
        {
            powerConditionPage[8] = 0xFF;
            powerConditionPage[9] = 0xFF;
        }
        else
        {
            powerConditionPage[8] = M_Byte1(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
            powerConditionPage[9] = M_Byte0(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
        }
        // idle c
        descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[128]);
        if (descriptor->powerConditionFlags & BIT7)
        {
            powerConditionPage[5] |= BIT2;
        }
        if (le32_to_host(descriptor->nomincalRecoveryTimeToPM0) > 0xFFFE)
        {
            powerConditionPage[8] = 0xFF;
            powerConditionPage[9] = 0xFF;
        }
        else
        {
            powerConditionPage[8] = M_Byte1(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
            powerConditionPage[9] = M_Byte0(le32_to_host(descriptor->nomincalRecoveryTimeToPM0));
        }
        // copy the data back
        if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, powerConditionPage, M_Min(18, scsiIoCtx->dataLength));
        }
    }
    else
    {
        // something went wrong so set failure from RTFRs
        ret = NOT_SUPPORTED;
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
    }
    return ret;
}

static eReturnValues translate_Logical_Block_Provisioning_VPD_Page_B2h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logicalBlockProvisioning, 8);
    uint8_t peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    logicalBlockProvisioning[0] = peripheralDevice;
    logicalBlockProvisioning[1] = 0xB2;
    logicalBlockProvisioning[2] = 0x00;
    logicalBlockProvisioning[3] = 0x04;
    // threshold exponent
    logicalBlockProvisioning[4] = 0;
    // lbpu bit
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word169)))
    {
        if (le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0)
        {
            logicalBlockProvisioning[5] |= BIT7;
            // lbpws bit (set to zero since we don't support unmap during write same yet)
            // logicalBlockProvisioning[5] |= BIT6;
            // lbpws10 bit (set to zero since we don't support unmap during write same yet)
            // logicalBlockProvisioning[5] |= BIT5;
            // lbprz
            if (le16_to_host(device->drive_info.IdentifyData.ata.Word069) & BIT5)
            {
                logicalBlockProvisioning[5] |= BIT2;
            }
            // anc_sup
            if (le16_to_host(device->drive_info.IdentifyData.ata.Word069) & BIT14)
            {
                logicalBlockProvisioning[5] |= BIT1;
            }
        }
    }
    // dp (set to zero since we don't support a resource descriptor
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, logicalBlockProvisioning, M_Min(8, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Block_Limits_VPD_Page_B0h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, blockLimits, 64);
    uint8_t peripheralDevice = UINT8_C(0);
    // maximum transfer length (unspecified....we decide) - set to 65535 or 255 depending on if we support 48 bit or not
    uint32_t maxTransferLength = UINT32_C(255);

#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    blockLimits[0] = peripheralDevice;
    blockLimits[1] = 0xB0;
    blockLimits[2] = 0x00;
    blockLimits[3] = 0x3C;
    // wsnz bit - leave as zero since we support a value of zero to overwrite the full drive
    // max compare and write length - set to 0
    blockLimits[5] = 0;
    // optimal transfer length granularity (unspecified....we decide) - setting to number of logical per physical
    // sectors
    if (device->drive_info.devicePhyBlockSize > 0)
    {
        uint16_t logicalPerPhysical =
            C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
        blockLimits[6] = M_Byte1(logicalPerPhysical);
        blockLimits[7] = M_Byte0(logicalPerPhysical);
    }

    if (device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported)
    {
        maxTransferLength = 65535;
    }
    blockLimits[8]  = M_Byte3(maxTransferLength);
    blockLimits[9]  = M_Byte2(maxTransferLength);
    blockLimits[10] = M_Byte1(maxTransferLength);
    blockLimits[11] = M_Byte0(maxTransferLength);
    // optimal transfer length (unspecified....we decide) - set to 128 for hopefully highest compatibility.
    blockLimits[12] = M_Byte3(128);
    blockLimits[13] = M_Byte2(128);
    blockLimits[14] = M_Byte1(128);
    blockLimits[15] = M_Byte0(128);
    // maximum prefetch length (unspecified....we decide) - leave at zero since we don't support the prefetch command

    // unmap stuff
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word169)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0 &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word069) & BIT14)
    {
        uint32_t maxBlocks = UINT32_C(1);
        uint8_t  maxDescriptorsPerBlock;
        uint64_t maxUnmapRangePerDescriptor;
        uint64_t maxLBAsPerUnmap;
        uint32_t unmapLBACount;
        uint32_t unmapMaxBlockDescriptors;

        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word105)))
        {
            maxBlocks = le16_to_host(device->drive_info.IdentifyData.ata.Word105);
        }
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
        maxDescriptorsPerBlock = device->drive_info.softSATFlags.dataSetManagementXLSupported ? 32 : 64;
        maxUnmapRangePerDescriptor =
            device->drive_info.softSATFlags.dataSetManagementXLSupported ? UINT64_MAX : UINT16_MAX;
        maxLBAsPerUnmap =
            C_CAST(uint64_t, maxDescriptorsPerBlock) * C_CAST(uint64_t, maxBlocks) * maxUnmapRangePerDescriptor;
        unmapLBACount            = maxLBAsPerUnmap > UINT32_MAX ? UINT32_MAX : C_CAST(uint32_t, maxLBAsPerUnmap);
        unmapMaxBlockDescriptors = maxDescriptorsPerBlock * maxBlocks;
#else  // SAT_SPEC_SUPPORTED <= 3
        unmapLBACount            = 64 * maxBlocks * UINT16_MAX;
        unmapMaxBlockDescriptors = 64 * maxBlocks;
#endif // SAT_SPEC_SUPPORTED
       // maximum unmap LBA count (unspecified....we decide)
        blockLimits[20] = M_Byte3(unmapLBACount);
        blockLimits[21] = M_Byte2(unmapLBACount);
        blockLimits[22] = M_Byte1(unmapLBACount);
        blockLimits[23] = M_Byte0(unmapLBACount);
        // maximum unmap block descriptor count (unspecified....we decide)
        blockLimits[24] = M_Byte3(unmapMaxBlockDescriptors);
        blockLimits[25] = M_Byte2(unmapMaxBlockDescriptors);
        blockLimits[26] = M_Byte1(unmapMaxBlockDescriptors);
        blockLimits[27] = M_Byte0(unmapMaxBlockDescriptors);
        // optimal unmap granularity (unspecified....we decide) - leave at zero

        // uga valid bit (unspecified....we decide) - leave at zero

        // unmap granularity alignment (unspecified....we decide) - leave at zero
    }
    // maximum write same length (unspecified....we decide). We will allow the full drive to be write same'd
    blockLimits[36] = M_Byte7(device->drive_info.deviceMaxLba);
    blockLimits[37] = M_Byte6(device->drive_info.deviceMaxLba);
    blockLimits[38] = M_Byte5(device->drive_info.deviceMaxLba);
    blockLimits[39] = M_Byte4(device->drive_info.deviceMaxLba);
    blockLimits[40] = M_Byte3(device->drive_info.deviceMaxLba);
    blockLimits[41] = M_Byte2(device->drive_info.deviceMaxLba);
    blockLimits[42] = M_Byte1(device->drive_info.deviceMaxLba);
    blockLimits[43] = M_Byte0(device->drive_info.deviceMaxLba);
    // maximum atomic length - leave at zero

    // atomic alignment - leave at zero

    // atomic transfer length granularity - leave at zero

    // maximum atomic transfer length with atomic boundary - leave at zero

    // maximum atomic boundary size - leave at zero

    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, blockLimits, M_Min(64, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Mode_Page_Policy_VPD_Page_87h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, modePagePolicy, LEGACY_DRIVE_SEC_SIZE);
    uint16_t pageOffset       = UINT16_C(4); // set to where we'll write the first descriptor
    uint8_t  peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    modePagePolicy[0] = peripheralDevice;
    modePagePolicy[1] = 0x87;
    // set each page policy descriptor
    // read write error recovery
    modePagePolicy[pageOffset + 0] = 0x01;     // pageCode
    modePagePolicy[pageOffset + 1] = 0x00;     // subpage code
    modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
    modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
    pageOffset += 4;
    // caching
    modePagePolicy[pageOffset + 0] = 0x08;     // pageCode
    modePagePolicy[pageOffset + 1] = 0x00;     // subpage code
    modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
    modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
    pageOffset += 4;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
    // power condition
    modePagePolicy[pageOffset + 0] = 0x1A;     // pageCode
    modePagePolicy[pageOffset + 1] = 0x00;     // subpage code
    modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
    modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
    pageOffset += 4;
#endif // SAT_SPEC_SUPPORTED
    // ATA power condition
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT3)
    {
        modePagePolicy[pageOffset + 0] = 0x1A;     // pageCode
        modePagePolicy[pageOffset + 1] = 0xF1;     // subpage code
        modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
        modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
        pageOffset += 4;
    }
    // informational exceptions control
    modePagePolicy[pageOffset + 0] = 0x1C;     // pageCode
    modePagePolicy[pageOffset + 1] = 0x00;     // subpage code
    modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
    modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
    pageOffset += 4;
    // control
    modePagePolicy[pageOffset + 0] = 0xA0;     // pageCode
    modePagePolicy[pageOffset + 1] = 0x00;     // subpage code
    modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
    modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
    pageOffset += 4;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
    // control extension
    modePagePolicy[pageOffset + 0] = 0xA0;     // pageCode
    modePagePolicy[pageOffset + 1] = 0x01;     // subpage code
    modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
    modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
    pageOffset += 4;
#endif // SAT_SPEC_SUPPORTED
    if (is_ATA_Identify_Word_Valid_SATA(
            device->drive_info.IdentifyData.ata
                .Word076)) // Only Serial ATA Devices will set the bits in words 76-79. Bit zero should always be set to
                           // zero, so the FFFF case won't be an issue
    {
        // pata control
        modePagePolicy[pageOffset + 0] = 0xA0;     // pageCode
        modePagePolicy[pageOffset + 1] = 0xF1;     // subpage code
        modePagePolicy[pageOffset + 2] = 0x01;     // mlus = 0 | modePagePolicy = 01b (per target port)
        modePagePolicy[pageOffset + 3] = RESERVED; // Reserved
        pageOffset += 4;
    }
    // set the page length last
    modePagePolicy[2] = M_Byte1(pageOffset - 4);
    modePagePolicy[3] = M_Byte0(pageOffset - 4);
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, modePagePolicy, M_Min(pageOffset, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Zoned_Block_Device_Characteristics_VPD_Page_B6h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, zonedDeviceInformation, LEGACY_DRIVE_SEC_SIZE);
    if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION,
                                    zonedDeviceInformation, LEGACY_DRIVE_SEC_SIZE,
                                    device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
    {
        // validate that we got a valid page from the ID Data log (it can return all zeros on this page if it isn't
        // supported, but read successfully)
        uint64_t zonedQword = M_BytesTo8ByteValue(
            zonedDeviceInformation[7], zonedDeviceInformation[6], zonedDeviceInformation[5], zonedDeviceInformation[4],
            zonedDeviceInformation[3], zonedDeviceInformation[2], zonedDeviceInformation[1], zonedDeviceInformation[0]);
        uint8_t pageNumber = M_Byte2(zonedQword);
        DECLARE_ZERO_INIT_ARRAY(uint8_t, zonedDeviceCharacteristics, 64);
        uint8_t peripheralDevice = UINT8_C(0);
        if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
        {
            peripheralDevice = 0x14;
        }
        zonedDeviceCharacteristics[0] = peripheralDevice;
        zonedDeviceCharacteristics[1] = 0xB6;
        zonedDeviceCharacteristics[2] = 0x00;
        zonedDeviceCharacteristics[3] = 0x3C;
        if (zonedQword & ATA_ID_DATA_QWORD_VALID_BIT && pageNumber == ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION)
        {
            uint32_t optimalNumberOfOpenSequentialWritePreferredZones =
                M_BytesTo4ByteValue(zonedDeviceInformation[19], zonedDeviceInformation[18], zonedDeviceInformation[17],
                                    zonedDeviceInformation[16]);
            uint32_t optimalNumberOfNonSequentiallyWrittenSequentialWritePreferredZones =
                M_BytesTo4ByteValue(zonedDeviceInformation[27], zonedDeviceInformation[26], zonedDeviceInformation[25],
                                    zonedDeviceInformation[24]);
            uint32_t maximumNumberOfOpenSequentialWriteRequiredZoned =
                M_BytesTo4ByteValue(zonedDeviceInformation[35], zonedDeviceInformation[34], zonedDeviceInformation[33],
                                    zonedDeviceInformation[32]);
            // URSWRZ bit
            if (zonedDeviceInformation[15] & BIT7 &&
                zonedDeviceInformation[8] & BIT0) // checking bit63 of this qword and bit 0
            {
                zonedDeviceCharacteristics[4] |= BIT0;
            }
            zonedDeviceCharacteristics[5] = RESERVED;
            zonedDeviceCharacteristics[6] = RESERVED;
            zonedDeviceCharacteristics[7] = RESERVED;
            // Optimal Number of open sequential write preferred zones
            zonedDeviceCharacteristics[8]  = M_Byte3(optimalNumberOfOpenSequentialWritePreferredZones);
            zonedDeviceCharacteristics[9]  = M_Byte2(optimalNumberOfOpenSequentialWritePreferredZones);
            zonedDeviceCharacteristics[10] = M_Byte1(optimalNumberOfOpenSequentialWritePreferredZones);
            zonedDeviceCharacteristics[11] = M_Byte0(optimalNumberOfOpenSequentialWritePreferredZones);
            // Optimal Number of Non-Sequentially Written Sequential Write Preferred Zones
            zonedDeviceCharacteristics[12] =
                M_Byte3(optimalNumberOfNonSequentiallyWrittenSequentialWritePreferredZones);
            zonedDeviceCharacteristics[13] =
                M_Byte2(optimalNumberOfNonSequentiallyWrittenSequentialWritePreferredZones);
            zonedDeviceCharacteristics[14] =
                M_Byte1(optimalNumberOfNonSequentiallyWrittenSequentialWritePreferredZones);
            zonedDeviceCharacteristics[15] =
                M_Byte0(optimalNumberOfNonSequentiallyWrittenSequentialWritePreferredZones);
            // Maximum Number Of Open Sequential Write Required Zones
            zonedDeviceCharacteristics[16] = M_Byte3(maximumNumberOfOpenSequentialWriteRequiredZoned);
            zonedDeviceCharacteristics[17] = M_Byte2(maximumNumberOfOpenSequentialWriteRequiredZoned);
            zonedDeviceCharacteristics[18] = M_Byte1(maximumNumberOfOpenSequentialWriteRequiredZoned);
            zonedDeviceCharacteristics[19] = M_Byte0(maximumNumberOfOpenSequentialWriteRequiredZoned);
            // remaining data is reserved
        }
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, zonedDeviceCharacteristics,
                        M_Min(64, scsiIoCtx->dataLength));
        }
    }
    else
    {
        // This data only exists in this log, so if we can't read it, time to return a failure.
        ret = NOT_SUPPORTED;
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
    }
    return ret;
}

static eReturnValues translate_Extended_Inquiry_Data_VPD_Page_86h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, extendedInquiry, 64);
    uint8_t peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    extendedInquiry[0] = peripheralDevice;
    extendedInquiry[1] = EXTENDED_INQUIRY_DATA;
    extendedInquiry[2] = 0x00;
    extendedInquiry[3] = 0x3C;
    // SAT 4 only specifies that the HSSRELEF is set to 1b
    extendedInquiry[8] |= BIT1;
    // activate microcode may be 0 (not specified) or 01b (activates before completion of final command in write buffer
    // sequence)
    extendedInquiry[4] |= BIT6; // 01b
    // WU_SUP - set to value of ATA Write Uncorrectable command support from identify data.
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /*words 119, 120 valid*/
    {
        if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT2) ||
            (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT2))
        {
            extendedInquiry[6] |= BIT3;
            // CRD_SUP (Obsolete is SPC5) set to value of WU_SUP
            extendedInquiry[6] |= BIT2;
        }
    }
    // extended self test completion time minutes to value from SMART Read Data command
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0 &&
        is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT1) // smart enabled and self test supported
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, smartData, LEGACY_DRIVE_SEC_SIZE);
        if (SUCCESS == ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE))
        {
            uint16_t extendedSelfTestCompletionTimeMinutes = UINT16_C(0);
            if (smartData[373] == 0xFF)
            {
                extendedSelfTestCompletionTimeMinutes = M_BytesTo2ByteValue(smartData[376], smartData[375]);
            }
            else
            {
                extendedSelfTestCompletionTimeMinutes = smartData[373];
            }
            extendedInquiry[10] = M_Byte1(extendedSelfTestCompletionTimeMinutes);
            extendedInquiry[11] = M_Byte0(extendedSelfTestCompletionTimeMinutes);
        }
    }
    // max supported sense data length to 252
    extendedInquiry[13] = SPC3_SENSE_LEN;
    // if deferred download is supported, set the poa_sup bit to 1
    if (device->drive_info.softSATFlags.deferredDownloadSupported)
    {
        // DO NOT Set the HRA, or VSA supported bits since the SATL has no way of knowing/doing those activation events!
        extendedInquiry[12] |= BIT7;
    }
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, extendedInquiry, M_Min(64, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Supported_VPD_Pages_00h(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedPages, LEGACY_DRIVE_SEC_SIZE);
    uint16_t pageOffset       = UINT16_C(4);
    uint8_t  peripheralDevice = UINT8_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
    {
        peripheralDevice = 0x14;
    }
#endif // SAT_SPEC_SUPPORTED
    supportedPages[0] = peripheralDevice;
    supportedPages[1] = 0; // page 0
    // set page 0 in here
    supportedPages[pageOffset] = SUPPORTED_VPD_PAGES;
    pageOffset++;
    // unit serial number
    supportedPages[pageOffset] = UNIT_SERIAL_NUMBER;
    pageOffset++;
    // device identification
    supportedPages[pageOffset] = DEVICE_IDENTIFICATION;
    pageOffset++;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 && SAT_4_ERROR_HISTORY_FEATURE
    // extended inquiry data
    supportedPages[pageOffset] = EXTENDED_INQUIRY_DATA;
    pageOffset++;
#endif // SAT_SPEC_SUPPORTED
    // mode page policy
    supportedPages[pageOffset] = MODE_PAGE_POLICY;
    pageOffset++;
    // ATA Information
    supportedPages[pageOffset] = ATA_INFORMATION;
    pageOffset++;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
    // power condition (only is EPC is supported on the drive)
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /*words 119, 120 valid*/
    {
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT7)
        {
            supportedPages[pageOffset] = POWER_CONDITION;
            pageOffset++;
        }
    }
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
    // block limits
    supportedPages[pageOffset] = BLOCK_LIMITS;
    pageOffset++;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
    // block device characteristics
    supportedPages[pageOffset] = BLOCK_DEVICE_CHARACTERISTICS;
    pageOffset++;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
    // logical block provisioning (only show when we have a drive that supports the TRIM command...otherwise this page
    // has little to no meaning)
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word169)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0)
    {
        supportedPages[pageOffset] = LOGICAL_BLOCK_PROVISIONING;
        pageOffset++;
    }
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
    // zoned device characteristics
    if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
        device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
    {
        supportedPages[pageOffset] = ZONED_BLOCK_DEVICE_CHARACTERISTICS;
        pageOffset++;
    }
#endif // SAT_SPEC_SUPPORTED
    // set the page length last
    supportedPages[2] = M_Byte1(pageOffset - 4);
    supportedPages[3] = M_Byte0(pageOffset - 4);
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, supportedPages, M_Min(pageOffset, scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_SCSI_Inquiry_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret          = SUCCESS;
    uint8_t       bitPointer   = UINT8_C(0);
    uint16_t      fieldPointer = UINT16_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    // Check to make sure cmdDT and reserved bits aren't set
    if (scsiIoCtx->cdb[1] & 0xFE)
    {
        uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
        uint8_t counter         = UINT8_C(0);
        fieldPointer            = UINT16_C(1);
        // One of the bits we don't support is set, so return invalid field in CDB

        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    else
    {
        // check EVPD bit
        if (scsiIoCtx->cdb[1] & BIT0)
        {
            // check the VPD page to set up that data correctly
            switch (scsiIoCtx->cdb[2])
            {
            case SUPPORTED_VPD_PAGES:
                // update this as more supported pages are added!
                ret = translate_Supported_VPD_Pages_00h(device, scsiIoCtx);
                break;
            case UNIT_SERIAL_NUMBER:
                ret = translate_Unit_Serial_Number_VPD_Page_80h(device, scsiIoCtx);
                break;
            case DEVICE_IDENTIFICATION:
                ret = translate_Device_Identification_VPD_Page_83h(device, scsiIoCtx);
                break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 && SAT_4_ERROR_HISTORY_FEATURE
            case EXTENDED_INQUIRY_DATA:
                ret = translate_Extended_Inquiry_Data_VPD_Page_86h(device, scsiIoCtx);
                break;
#endif // SAT_SPEC_SUPPORTED
            case MODE_PAGE_POLICY:
                ret = translate_Mode_Page_Policy_VPD_Page_87h(device, scsiIoCtx);
                break;
            case ATA_INFORMATION:
                ret = translate_ATA_Information_VPD_Page_89h(device, scsiIoCtx);
                break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            case POWER_CONDITION:
                if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
                     le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /*words 119, 120 valid*/
                    && (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                            le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
                        le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT7))
                {
                    ret = translate_Power_Condition_VPD_Page_8Ah(device, scsiIoCtx);
                }
                else
                {
                    ret          = NOT_SUPPORTED;
                    fieldPointer = UINT16_C(2);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                break;
            case BLOCK_LIMITS:
                ret = translate_Block_Limits_VPD_Page_B0h(device, scsiIoCtx);
                break;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
            case BLOCK_DEVICE_CHARACTERISTICS:
                ret = translate_Block_Device_Characteristics_VPD_Page_B1h(device, scsiIoCtx);
                break;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            case LOGICAL_BLOCK_PROVISIONING:
                // only bother supporting this page if the drive supports trim
                if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word169)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0)
                {
                    ret = translate_Logical_Block_Provisioning_VPD_Page_B2h(device, scsiIoCtx);
                }
                else
                {
                    ret          = NOT_SUPPORTED;
                    fieldPointer = UINT16_C(2);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                break;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
            case ZONED_BLOCK_DEVICE_CHARACTERISTICS:
                if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
                    device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
                {
                    ret = translate_Zoned_Block_Device_Characteristics_VPD_Page_B6h(device, scsiIoCtx);
                }
                else
                {
                    ret          = NOT_SUPPORTED;
                    fieldPointer = UINT16_C(2);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                break;
#endif // SAT_SPEC_SUPPORTED
            default:
                ret          = NOT_SUPPORTED;
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
            }
        }
        else
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, inquiryData, INQ_RETURN_DATA_LENGTH);
            DECLARE_ZERO_INIT_ARRAY(uint8_t, iddata, LEGACY_DRIVE_SEC_SIZE);
            // standard inquiry data
            // read identify data
            uint8_t peripheralDevice = UINT8_C(0);
            // Product ID (first 16bytes of the ata model number
            DECLARE_ZERO_INIT_ARRAY(char, ataSN, ATA_IDENTIFY_SN_LENGTH + 1);
            DECLARE_ZERO_INIT_ARRAY(char, ataMN, ATA_IDENTIFY_MN_LENGTH + 1);
            DECLARE_ZERO_INIT_ARRAY(char, ataFW, ATA_IDENTIFY_FW_LENGTH + 1);
            uint16_t versionOffset = UINT16_C(58);

            if (scsiIoCtx->cdb[2] != 0) // if page code is non-zero, we need to return an error
            {
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if (SUCCESS != ata_Identify(device, iddata, LEGACY_DRIVE_SEC_SIZE))
            {
                // that failed, so try an identify packet device
                if (SUCCESS != ata_Identify_Packet_Device(device, iddata, LEGACY_DRIVE_SEC_SIZE))
                {
                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NOT_READY,
                                                   0x04, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                                   M_NULLPTR, 0);
                    return FAILURE;
                }
                peripheralDevice = 0x05;
            }
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                           device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
            if (device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
            {
                peripheralDevice = 0x14;
            }
#endif // SAT_SPEC_SUPPORTED
            inquiryData[0] = peripheralDevice;
            if (le16_to_host(device->drive_info.IdentifyData.ata.Word000) & BIT7)
            {
                inquiryData[1] |= BIT7;
            }
            // version
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED == 1
            // SPC3
            inquiryData[2] = 0x05;
#elif defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED == 2
            // SPC4
            inquiryData[2] = 0x06;
#elif defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED == 3
            // SPC4
            inquiryData[2] = 0x06;
#else  // SAT_SPEC_SUPPORTED
       // SPC5
            inquiryData[2] = 0x07;
#endif // SAT_SPEC_SUPPORTED
       // response format
            inquiryData[3] = 2;
            // additional length
            inquiryData[3] = 92;
            // vendorID
            inquiryData[8]  = 'A';
            inquiryData[9]  = 'T';
            inquiryData[10] = 'A';
            inquiryData[11] = ' ';
            inquiryData[12] = ' ';
            inquiryData[13] = ' ';
            inquiryData[14] = ' ';
            inquiryData[15] = ' ';
            // Product ID (first 16bytes of the ata model number
            fill_ATA_Strings_From_Identify_Data(C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000), ataMN,
                                                ataSN, ataFW);

            safe_memcpy(&inquiryData[16], INQ_RETURN_DATA_LENGTH - 16, ataMN, INQ_DATA_PRODUCT_ID_LEN);
            // product revision (truncates to 4 bytes)
            if (safe_strlen(ataFW) > 4)
            {
                safe_memcpy(&inquiryData[32], INQ_RETURN_DATA_LENGTH - 32, &ataFW[4], INQ_DATA_PRODUCT_REV_LEN);
            }
            else
            {
                safe_memcpy(&inquiryData[32], INQ_RETURN_DATA_LENGTH - 32, &ataFW[0], INQ_DATA_PRODUCT_REV_LEN);
            }
            // Vendor specific...we'll set the SN here
            safe_memcpy(&inquiryData[36], INQ_RETURN_DATA_LENGTH - 36, ataSN,
                        M_Min(safe_strlen(ataSN), ATA_IDENTIFY_SN_LENGTH));
            // version descriptors (bytes 58 to 73) (8 max)

#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED < 2
            // SAM3
            inquiryData[versionOffset]     = 0x00;
            inquiryData[versionOffset + 1] = 0x60;
            versionOffset += 2;
            // SAT
            inquiryData[versionOffset]     = 0x1E;
            inquiryData[versionOffset + 1] = 0xA0;
            versionOffset += 2;
            // SPC3
            inquiryData[versionOffset]     = 0x03;
            inquiryData[versionOffset + 1] = 0x00;
            versionOffset += 2;
            // SBC2
            inquiryData[versionOffset]     = 0x03;
            inquiryData[versionOffset + 1] = 0x20;
            versionOffset += 2;
#elif defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED < 3
            // SAM4
            inquiryData[versionOffset]     = 0x00;
            inquiryData[versionOffset + 1] = 0x80;
            versionOffset += 2;
            // SAT2
            inquiryData[versionOffset]     = 0x1E;
            inquiryData[versionOffset + 1] = 0xC0;
            versionOffset += 2;
            // SPC4
            inquiryData[versionOffset]     = 0x04;
            inquiryData[versionOffset + 1] = 0x60;
            versionOffset += 2;
            // SBC3
            inquiryData[versionOffset]     = 0x04;
            inquiryData[versionOffset + 1] = 0xC0;
            versionOffset += 2;
#elif defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED < 4
            // SAM5
            inquiryData[versionOffset]     = 0x00;
            inquiryData[versionOffset + 1] = 0xA0;
            versionOffset += 2;
            // SAT3
            inquiryData[versionOffset]     = 0x1E;
            inquiryData[versionOffset + 1] = 0xE0;
            versionOffset += 2;
            // SPC4
            inquiryData[versionOffset]     = 0x04;
            inquiryData[versionOffset + 1] = 0x60;
            versionOffset += 2;
            // SBC3
            inquiryData[versionOffset]     = 0x04;
            inquiryData[versionOffset + 1] = 0xC0;
            versionOffset += 2;
#else  // SAT_SPEC_SUPPORTED
       // SAM6 -
            inquiryData[versionOffset]     = 0x00;
            inquiryData[versionOffset + 1] = 0xC0;
            versionOffset += 2;
            // SAT4 - 1F00h
            inquiryData[versionOffset]     = 0x1F;
            inquiryData[versionOffset + 1] = 0x00;
            versionOffset += 2;
            // SPC5 - 05C0h
            inquiryData[versionOffset]     = 0x05;
            inquiryData[versionOffset + 1] = 0xC0;
            versionOffset += 2;
            // SBC4 - 0600h
            inquiryData[versionOffset]     = 0x06;
            inquiryData[versionOffset + 1] = 0x00;
            versionOffset += 2;
            // If zoned, ZBC/ZAC spec 0620h
            if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
                device->drive_info.zonedType == ZONED_TYPE_DEVICE_MANAGED)
            {
                inquiryData[versionOffset]     = 0x06;
                inquiryData[versionOffset + 1] = 0x20;
                versionOffset += 2;
            }
#endif // SAT_SPEC_SUPPORTED
       // Transport...skipping this one since I'm not sure of what exactly to set (we could try to do parallel vs
       // serial, but we only get to set ATA8-APT or ATA8-AST, which may not be enough)-TJE ATA Version(s) ATA/ATAPI-6
       // through ACS-4 searching for specs between ATA/ATAPI-6 and ACS-3 since ACS3 is the highest thing with a value
       // at this time.
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word080)))
            {
                uint16_t specSupportedWord = le16_to_host(device->drive_info.IdentifyData.ata.Word080);
                uint16_t ataSpecVersion    = UINT16_C(0);
                uint8_t  specCounter       = UINT8_C(1);
                while (specSupportedWord > 0x0001)
                {
                    if (specSupportedWord & 0x0001)
                    {
                        switch (specCounter)
                        {
                        case 6: // ATA/ATAPI-6 15E0h
                            ataSpecVersion = 0x15E0;
                            break;
                        case 7: // ATA/ATAPI-7 1600h
                            ataSpecVersion = 0x1600;
                            break;
                        case 8: // ACS(ATA8) 1623h
                            ataSpecVersion = 0x1623;
                            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
                        case 9: // ACS2 1761h
                            ataSpecVersion = 0x1761;
                            break;
#    if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
                        case 10: // ACS3 1765h
                            ataSpecVersion = 0x1765;
                            break;
#        if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
                        case 11: // ACS4 1767h
                            ataSpecVersion = 0x1767;
                            break;
#        endif // SAT_SPEC_SUPPORTED >3
#    endif     // SAT_SPEC_SUPPORTED >2
#endif         // SAT_SPEC_SUPPORTED >1
                        default:
                            break;
                        }
                    }
                    specSupportedWord >>= 1;
                    specCounter++;
                }
                inquiryData[versionOffset]     = M_Byte1(ataSpecVersion);
                inquiryData[versionOffset + 1] = M_Byte0(ataSpecVersion);
                versionOffset += 2;
            }
            // now copy the data back
            if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, inquiryData,
                            M_Min(INQ_RETURN_DATA_LENGTH, scsiIoCtx->dataLength));
            }
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Read_Capacity_Command(tDevice* device, bool readCapacity16, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                        = SUCCESS;
    uint64_t      maxLBA                     = UINT64_C(0);
    uint32_t      logicalSectorSize          = UINT32_C(0);
    uint8_t       physicalSectorSizeExponent = UINT8_C(0);
    uint16_t      sectorAlignment            = UINT16_C(0);
    uint16_t      fieldPointer               = UINT16_C(0);
    uint8_t       bitPointer                 = UINT8_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    bool rzatLBPRZ = false;
    bool dratLBPME = false;
    // Check that reserved and obsolete bits aren't set
    if (readCapacity16)
    {
        // 16byte field filter
        if ((fieldPointer = 1 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0) ||
            (fieldPointer = 2 && scsiIoCtx->cdb[2] != 0) || (fieldPointer = 3 && scsiIoCtx->cdb[3] != 0) ||
            (fieldPointer = 4 && scsiIoCtx->cdb[4] != 0) || (fieldPointer = 5 && scsiIoCtx->cdb[5] != 0) ||
            (fieldPointer = 6 && scsiIoCtx->cdb[6] != 0) || (fieldPointer = 7 && scsiIoCtx->cdb[7] != 0) ||
            (fieldPointer = 8 && scsiIoCtx->cdb[8] != 0) || (fieldPointer = 9 && scsiIoCtx->cdb[9] != 0) ||
            (fieldPointer = 14 && scsiIoCtx->cdb[14] != 0))
        {
            // invalid field in CDB
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
            return ret;
        }
    }
    else
    {
        uint8_t reservedByteVal;
        uint8_t counter = UINT8_C(0);

        // 10 byte field filter
        if ((fieldPointer = 1 && scsiIoCtx->cdb[1] != 0) || (fieldPointer = 2 && scsiIoCtx->cdb[2] != 0) ||
            (fieldPointer = 3 && scsiIoCtx->cdb[3] != 0) || (fieldPointer = 4 && scsiIoCtx->cdb[4] != 0) ||
            (fieldPointer = 5 && scsiIoCtx->cdb[5] != 0) || (fieldPointer = 6 && scsiIoCtx->cdb[6] != 0) ||
            (fieldPointer = 7 && scsiIoCtx->cdb[7] != 0) || (fieldPointer = 8 && scsiIoCtx->cdb[8] != 0))
        {
            // invalid field in CDB
            ret             = NOT_SUPPORTED;
            bitPointer      = UINT8_C(7);
            reservedByteVal = scsiIoCtx->cdb[fieldPointer];

            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return ret;
        }
    }
    // issue an identify command
    DECLARE_ZERO_INIT_ARRAY(uint8_t, identifyData, LEGACY_DRIVE_SEC_SIZE);
    if (SUCCESS == ata_Identify(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
    {
        uint16_t* ident_word = C_CAST(uint16_t*, &device->drive_info.IdentifyData.ata);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        // get the MaxLBA
        bool     lbaModeSupported      = false;
        uint16_t cylinder              = UINT16_C(0);
        uint8_t  head                  = UINT8_C(0);
        uint8_t  spt                   = UINT8_C(0);
        bool     words64to70Valid      = false;
        bool     extendedLBAFieldValid = false;

        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        // get the MaxLBA

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[1])) &&
            is_ATA_Identify_Word_Valid(le16_to_host(ident_word[3])) &&
            is_ATA_Identify_Word_Valid(le16_to_host(ident_word[6])))
        {
            cylinder = M_BytesTo2ByteValue(identifyData[3], identifyData[2]); // word 1
            head     = identifyData[6];                                       // Word3
            spt      = identifyData[12];                                      // Word6
            // According to ATA, word 53, bit 0 set to 1 means the words 54,-58 are valid.
            // if set to zero they MAY be valid....so just check validity on everything
        }

        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[49])))
        {
            if (le16_to_host(ident_word[49]) & BIT9)
            {
                lbaModeSupported = true;
            }
            if (le16_to_host(ident_word[53]) & BIT1)
            {
                words64to70Valid = true;
            }
        }
        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[53])))
        {
            if ((le16_to_host(ident_word[53]) & BIT0) || (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[54])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[55])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[56])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[57])) &&
                                                          is_ATA_Identify_Word_Valid(le16_to_host(ident_word[58]))))
            {
                // only override if these are non-zero. If all are zero, then we cannot determine the current
                // configuration and should rely on the defaults read earlier. This is being checked again sincea device
                // may set bit0 of word 53 meaning this is a valid field. however if the values are zero, we do not want
                // to use them.
                if (M_BytesTo2ByteValue(identifyData[109], identifyData[108]) > 0 && identifyData[110] > 0 &&
                    identifyData[112] > 0)
                {
                    cylinder = M_BytesTo2ByteValue(identifyData[109], identifyData[108]); // word 54
                    head     = identifyData[110];                                         // Word55
                    spt      = identifyData[112];                                         // Word56
                }
            }
        }
        maxLBA = C_CAST(uint64_t, cylinder) * C_CAST(uint64_t, head) * C_CAST(uint64_t, spt);

        if (lbaModeSupported || (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[60])) ||
                                 is_ATA_Identify_Word_Valid(le16_to_host(ident_word[61]))))
        {
            lbaModeSupported = true; // workaround for some USB devices that do support lbamode as can be seen by
                                     // reading this LBA value
            maxLBA = M_BytesTo4ByteValue(identifyData[123], identifyData[122], identifyData[121], identifyData[120]);
        }

        if (words64to70Valid && is_ATA_Identify_Word_Valid(le16_to_host(ident_word[69])))
        {
            if (le16_to_host(ident_word[69]) & BIT3)
            {
                extendedLBAFieldValid = true;
            }
            if (le16_to_host(ident_word[69]) & BIT5)
            {
                rzatLBPRZ = true;
            }
            if (le16_to_host(ident_word[69]) & BIT14)
            {
                dratLBPME = true;
            }
        }
        if (lbaModeSupported && maxLBA == MAX_28BIT)
        {
            // max LBA from other words since 28bit max field is maxed out
            // check words 100-103 are valid values
            if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[100])) ||
                is_ATA_Identify_Word_Valid(le16_to_host(ident_word[101])) ||
                is_ATA_Identify_Word_Valid(le16_to_host(ident_word[102])) ||
                is_ATA_Identify_Word_Valid(le16_to_host(ident_word[103])))
            {
                maxLBA =
                    M_BytesTo8ByteValue(identifyData[207], identifyData[206], identifyData[205], identifyData[204],
                                        identifyData[203], identifyData[202], identifyData[201], identifyData[200]);
            }
        }
        if (extendedLBAFieldValid) // TODO: SAT version check???
        {
            maxLBA = M_BytesTo8ByteValue(identifyData[467], identifyData[466], identifyData[465], identifyData[464],
                                         identifyData[463], identifyData[462], identifyData[461], identifyData[460]);
        }
        if (lbaModeSupported)
        {
            maxLBA -= 1;
        }
        // get the Sector Sizes
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(ident_word[106])))
        {
            // word 117 is only valid when word 106 bit 12 is set
            if ((le16_to_host(ident_word[106]) & BIT12) == BIT12)
            {
                logicalSectorSize = M_WordsTo4ByteValue(le16_to_host(ident_word[118]), le16_to_host(ident_word[117]));
                logicalSectorSize *= 2; // convert to words to bytes
            }
            else // means that logical sector size is 512bytes
            {
                logicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
            }
            if ((le16_to_host(ident_word[106]) & BIT13) == 0)
            {
                physicalSectorSizeExponent = 0;
            }
            else // multiple logical sectors per physical sector
            {
                physicalSectorSizeExponent = le16_to_host(ident_word[106]) & 0x000F;
            }
        }
        else
        {
            logicalSectorSize          = LEGACY_DRIVE_SEC_SIZE;
            physicalSectorSizeExponent = 0;
        }
        if (is_ATA_Identify_Word_Valid(le16_to_host(ident_word[209])) && le16_to_host(ident_word[209]) & BIT14)
        {
            sectorAlignment = le16_to_host(ident_word[209]) & 0x3F;
        }
        // TODO: Zoned info (RC Basis)...need update to SAT 4 before I can fill this in

        // Got the data we wanted, now check for the Identify Device Data Log to get zoned information (add in later
        // when translation becomes available)
    }
    else
    {
        ret = FAILURE;
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
    }
    if (ret != FAILURE && scsiIoCtx->pdata)
    {
        // set the data in the buffer
        if (readCapacity16)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, readCapacityData, 32);
            readCapacityData[0]  = M_Byte7(maxLBA);
            readCapacityData[1]  = M_Byte6(maxLBA);
            readCapacityData[2]  = M_Byte5(maxLBA);
            readCapacityData[3]  = M_Byte4(maxLBA);
            readCapacityData[4]  = M_Byte3(maxLBA);
            readCapacityData[5]  = M_Byte2(maxLBA);
            readCapacityData[6]  = M_Byte1(maxLBA);
            readCapacityData[7]  = M_Byte0(maxLBA);
            readCapacityData[8]  = M_Byte3(logicalSectorSize);
            readCapacityData[9]  = M_Byte2(logicalSectorSize);
            readCapacityData[10] = M_Byte1(logicalSectorSize);
            readCapacityData[11] = M_Byte0(logicalSectorSize);
            // ZBC bits 5 and 4 to come later - TJE
            // sector size exponent
            readCapacityData[13] = M_Nibble0(physicalSectorSizeExponent);
            // set the alignment first
            readCapacityData[14] = M_Byte1(sectorAlignment);
            readCapacityData[15] = M_Byte0(sectorAlignment);
            // now bits related to TRIM support
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            if (dratLBPME)
            {
                readCapacityData[14] |= BIT7;
            }
            if (rzatLBPRZ)
            {
                readCapacityData[14] |= BIT6;
            }
#endif
            // remaining bytes are reserved

            if (scsiIoCtx->pdata)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readCapacityData,
                            M_Min(32, scsiIoCtx->dataLength));
            }
        }
        else
        {
            // data length is required by spec so return an error if we don't have at least that length
            if (scsiIoCtx->dataLength < 8 || !scsiIoCtx->pdata)
            {
                return MEMORY_FAILURE;
            }
            if (maxLBA > UINT32_MAX)
            {
                scsiIoCtx->pdata[0] = 0xFF;
                scsiIoCtx->pdata[1] = 0xFF;
                scsiIoCtx->pdata[2] = 0xFF;
                scsiIoCtx->pdata[3] = 0xFF;
            }
            else
            {
                scsiIoCtx->pdata[0] = M_Byte3(maxLBA);
                scsiIoCtx->pdata[1] = M_Byte2(maxLBA);
                scsiIoCtx->pdata[2] = M_Byte1(maxLBA);
                scsiIoCtx->pdata[3] = M_Byte0(maxLBA);
            }
            scsiIoCtx->pdata[4] = M_Byte3(logicalSectorSize);
            scsiIoCtx->pdata[5] = M_Byte2(logicalSectorSize);
            scsiIoCtx->pdata[6] = M_Byte1(logicalSectorSize);
            scsiIoCtx->pdata[7] = M_Byte0(logicalSectorSize);
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_ATA_Passthrough_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t               bitPointer   = UINT8_C(0);
    uint16_t              fieldPointer = UINT16_C(0);
    ataPassthroughCommand ataCommand;
    uint8_t*              protocolByte         = &scsiIoCtx->cdb[1];
    uint8_t*              transferInfoByte     = &scsiIoCtx->cdb[2];
    bool                  thirtyTwoByteCommand = false;
    uint8_t               protocol;
    safe_memset(&ataCommand, sizeof(ataPassthroughCommand), 0, sizeof(ataPassthroughCommand));

    if (scsiIoCtx->cdb[OPERATION_CODE] != ATA_PASS_THROUGH_12 && scsiIoCtx->cdb[OPERATION_CODE] != ATA_PASS_THROUGH_16)
    {
        // check if it's the 32byte command
        uint16_t serviceAction = M_BytesTo2ByteValue(scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        if (scsiIoCtx->cdb[OPERATION_CODE] == 0x7F && serviceAction == 0x1FF0)
        {
            thirtyTwoByteCommand = true;
            protocolByte         = &scsiIoCtx->cdb[10];
            transferInfoByte     = &scsiIoCtx->cdb[11];
        }
        else
        {
            fieldPointer = UINT16_C(8);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return BAD_PARAMETER;
        }
    }
    if (thirtyTwoByteCommand)
    {
        // make sure reserved bits (which are multiple count in 16&12 byte) aren't set. That command was made obsolete
        // so don't need it here...let's follow the spec.
        uint8_t reservedBits = get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 5);
        if (reservedBits > 0)
        {
            // invalid field in cdb
            uint8_t reservedByteVal =
                scsiIoCtx->cdb[10] & 0xE0; // don't care about lower bits...those are checked elsewhere
            uint8_t counter = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return BAD_PARAMETER;
        }
    }
    else
    {
        uint8_t multipleCount = get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5);
        if (multipleCount > 0)
        {
            // This can only be non-zero if it's a read or write multiple command operation code!
            uint8_t commandOpCodeToCheck = UINT8_C(0);
            if (scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_12)
            {
                commandOpCodeToCheck = scsiIoCtx->cdb[9];
            }
            else // 16byte
            {
                commandOpCodeToCheck = scsiIoCtx->cdb[14];
            }
            switch (commandOpCodeToCheck)
            {
            case ATA_READ_MULTIPLE_CMD:
            case ATA_READ_READ_MULTIPLE_EXT:
            case ATA_WRITE_MULTIPLE_CMD:
            case ATA_WRITE_MULTIPLE_EXT:
            case ATA_WRITE_MULTIPLE_FUA_EXT:
                break;
            default:
                // return an error saying the multple count field is wrong
                fieldPointer = UINT16_C(1);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return BAD_PARAMETER;
            }
        }
    }
    scsiIoCtx->pAtaCmdOpts = &ataCommand;
    ataCommand.commandType = ATA_CMD_TYPE_TASKFILE;
    protocol               = M_Nibble0(*protocolByte >> 1);
    // check bytes 1 and 2 of the CDB for the different BITs to make sure everything get's set up correctly for a lower
    // layer
    ataCommand.dataSize = scsiIoCtx->dataLength;
    ataCommand.ptrData  = scsiIoCtx->pdata;
    switch (protocol)
    {
    case 3: // nondata
        ataCommand.commadProtocol = ATA_PROTOCOL_NO_DATA;
        break;
    case 4: // pio in
    case 5: // pio out
        ataCommand.commadProtocol = ATA_PROTOCOL_PIO;
        break;
    case 6: // dma
        ataCommand.commadProtocol = ATA_PROTOCOL_DMA;
        break;
    case 10: // udma in
    case 11: // udma out
        ataCommand.commadProtocol = ATA_PROTOCOL_UDMA;
        break;
    case 15: // return response information
        // just get whatever is in the last command rtfrs of the device, and set that into the data return buffer and
        // return success
        if (device->drive_info.interface_type == IDE_INTERFACE)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, response, 14);
            response[0]  = 0x09;
            response[1]  = 0x0C;
            response[3]  = device->drive_info.lastCommandRTFRs.error;
            response[5]  = device->drive_info.lastCommandRTFRs.secCnt;
            response[7]  = device->drive_info.lastCommandRTFRs.lbaLow;
            response[9]  = device->drive_info.lastCommandRTFRs.lbaMid;
            response[11] = device->drive_info.lastCommandRTFRs.lbaHi;
            response[12] = device->drive_info.lastCommandRTFRs.device;
            response[13] = device->drive_info.lastCommandRTFRs.status;
            if (device->drive_info.lastCommandRTFRs.secCntExt || device->drive_info.lastCommandRTFRs.lbaLowExt ||
                device->drive_info.lastCommandRTFRs.lbaMidExt || device->drive_info.lastCommandRTFRs.lbaHiExt)
            {
                // set the extend bit
                response[2] |= BIT0;
                response[4]  = device->drive_info.lastCommandRTFRs.secCntExt;
                response[6]  = device->drive_info.lastCommandRTFRs.lbaLowExt;
                response[8]  = device->drive_info.lastCommandRTFRs.lbaMidExt;
                response[10] = device->drive_info.lastCommandRTFRs.lbaHiExt;
            }
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, response,
                            M_Min(scsiIoCtx->dataLength, SIZE_OF_STACK_ARRAY(response)));
            }
            scsiIoCtx->pAtaCmdOpts = M_NULLPTR;
            return SUCCESS;
        }
        break;
    case 12: // fpdma
    case 7:  // dma queued
    case 8:  // device diagnostic
    case 9:  // nondata-device reset
    case 0:  // hardware reset
    case 1:  // software reset
    default: // catch protocols that we don't support here
        if (thirtyTwoByteCommand)
        {
            fieldPointer = UINT16_C(10);
        }
        else
        {
            fieldPointer = UINT16_C(1);
        }
        bitPointer = UINT8_C(4);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        scsiIoCtx->pAtaCmdOpts = M_NULLPTR;
        return NOT_SUPPORTED;
    }
    // check tlength
    switch (*transferInfoByte & 0x03)
    {
    case 0:
        // no data
        break;
    case 1:
    case 2:
    case 3:
        // check the t-dir bit for transfer direction
        if (*transferInfoByte & BIT3)
        {
            ataCommand.commandDirection = XFER_DATA_IN;
        }
        else
        {
            ataCommand.commandDirection = XFER_DATA_OUT;
        }
        break;
    default:
        break;
    }
    // create the ata_Passthrough command structure first, then issue the io
    if (scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_12)
    {
        // get registers
        ataCommand.tfr.ErrorFeature  = scsiIoCtx->cdb[3];
        ataCommand.tfr.SectorCount   = scsiIoCtx->cdb[4];
        ataCommand.tfr.LbaLow        = scsiIoCtx->cdb[5];
        ataCommand.tfr.LbaMid        = scsiIoCtx->cdb[6];
        ataCommand.tfr.LbaHi         = scsiIoCtx->cdb[7];
        ataCommand.tfr.DeviceHead    = scsiIoCtx->cdb[8];
        ataCommand.tfr.CommandStatus = scsiIoCtx->cdb[9];
        if (scsiIoCtx->cdb[10])
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[10];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            fieldPointer = UINT16_C(10);
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            scsiIoCtx->pAtaCmdOpts = M_NULLPTR;
            return NOT_SUPPORTED;
        }
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == ATA_PASS_THROUGH_16)
    {
        // get registers
        ataCommand.tfr.ErrorFeature  = scsiIoCtx->cdb[4];
        ataCommand.tfr.SectorCount   = scsiIoCtx->cdb[6];
        ataCommand.tfr.LbaLow        = scsiIoCtx->cdb[8];
        ataCommand.tfr.LbaMid        = scsiIoCtx->cdb[10];
        ataCommand.tfr.LbaHi         = scsiIoCtx->cdb[12];
        ataCommand.tfr.DeviceHead    = scsiIoCtx->cdb[13];
        ataCommand.tfr.CommandStatus = scsiIoCtx->cdb[14];
        // get ext registers if the ext bit is set.
        if (*protocolByte & BIT0)
        {
            ataCommand.tfr.Feature48     = scsiIoCtx->cdb[3];
            ataCommand.tfr.SectorCount48 = scsiIoCtx->cdb[5];
            ataCommand.tfr.LbaLow48      = scsiIoCtx->cdb[7];
            ataCommand.tfr.LbaMid48      = scsiIoCtx->cdb[9];
            ataCommand.tfr.LbaHi48       = scsiIoCtx->cdb[11];
            ataCommand.commandType       = ATA_CMD_TYPE_EXTENDED_TASKFILE;
        }
    }
    else if (thirtyTwoByteCommand)
    {
        // get registers
        ataCommand.tfr.ErrorFeature  = scsiIoCtx->cdb[21];
        ataCommand.tfr.SectorCount   = scsiIoCtx->cdb[23];
        ataCommand.tfr.LbaLow        = scsiIoCtx->cdb[19];
        ataCommand.tfr.LbaMid        = scsiIoCtx->cdb[18];
        ataCommand.tfr.LbaHi         = scsiIoCtx->cdb[17];
        ataCommand.tfr.DeviceHead    = scsiIoCtx->cdb[24];
        ataCommand.tfr.CommandStatus = scsiIoCtx->cdb[25];
        // get ext registers if the ext bit is set.
        if (*protocolByte & BIT0)
        {
            ataCommand.tfr.Feature48     = scsiIoCtx->cdb[20];
            ataCommand.tfr.SectorCount48 = scsiIoCtx->cdb[22];
            ataCommand.tfr.LbaLow48      = scsiIoCtx->cdb[16];
            ataCommand.tfr.LbaMid48      = scsiIoCtx->cdb[15];
            ataCommand.tfr.LbaHi48       = scsiIoCtx->cdb[14];
            ataCommand.commandType       = ATA_CMD_TYPE_EXTENDED_TASKFILE;
            ataCommand.tfr.icc           = scsiIoCtx->cdb[27];
            ataCommand.tfr.aux4          = scsiIoCtx->cdb[28];
            ataCommand.tfr.aux3          = scsiIoCtx->cdb[29];
            ataCommand.tfr.aux2          = scsiIoCtx->cdb[30];
            ataCommand.tfr.aux1          = scsiIoCtx->cdb[31];
        }
        // If any of the ICC or AUX registers are set, we need to return an error since we cannot issue that command
        // since OS passthrough doesn't support it
        if ((fieldPointer = 27 && ataCommand.tfr.icc) || (fieldPointer = 28 && ataCommand.tfr.aux4) ||
            (fieldPointer = 29 && ataCommand.tfr.aux3) || (fieldPointer = 30 && ataCommand.tfr.aux2) ||
            (fieldPointer = 31 && ataCommand.tfr.aux1) || (fieldPointer = 26 && scsiIoCtx->cdb[26]) ||
            (fieldPointer = 12 && scsiIoCtx->cdb[12]) || (fieldPointer = 13 && scsiIoCtx->cdb[13]))
        {
            bitPointer = UINT8_C(7);
            if (fieldPointer == 26 || fieldPointer == 12 || fieldPointer == 13)
            {
                uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                uint8_t counter         = UINT8_C(0);
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer =
                    counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            }
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            scsiIoCtx->pAtaCmdOpts = M_NULLPTR;
            return NOT_SUPPORTED;
        }
    }
    // issue the IO
    ret = send_IO(scsiIoCtx);

    // now we need to dummy up sense data if we are on the IDE_INTERFACE (ATA) and it was unsuccessful or the check
    // condition bit was set, otherwise the SATL (USB or SAS) will do this for us
    if ((ret != SUCCESS || scsiIoCtx->cdb[2] & BIT5) && device->drive_info.interface_type == IDE_INTERFACE)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, ataReturnDescriptor,
                                15); // making this 1 byte larger than it needs to be. This is done so that the log
                                     // index can be set for fixed format sense data.
        ataReturnDescriptor[0] = 0x09;
        ataReturnDescriptor[1] = 0x0C;
        if (scsiIoCtx->cdb[1] & BIT0)
        {
            ataReturnDescriptor[2] |= BIT0;
        }
        ataReturnDescriptor[3]  = ataCommand.rtfr.error;
        ataReturnDescriptor[4]  = ataCommand.rtfr.secCntExt;
        ataReturnDescriptor[5]  = ataCommand.rtfr.secCnt;
        ataReturnDescriptor[6]  = ataCommand.rtfr.lbaLowExt;
        ataReturnDescriptor[7]  = ataCommand.rtfr.lbaLow;
        ataReturnDescriptor[8]  = ataCommand.rtfr.lbaMidExt;
        ataReturnDescriptor[9]  = ataCommand.rtfr.lbaMid;
        ataReturnDescriptor[10] = ataCommand.rtfr.lbaHiExt;
        ataReturnDescriptor[11] = ataCommand.rtfr.lbaHi;
        ataReturnDescriptor[12] = ataCommand.rtfr.device;
        ataReturnDescriptor[13] = ataCommand.rtfr.status;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED < 2
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0x1D, true,
                                       ataReturnDescriptor);
#else  // SAT_SPEC_SUPPORTED
       // copy the result to the ata-passthrough results log
        ++device->drive_info.softSATFlags.rtfrIndex;
        if (device->drive_info.softSATFlags.rtfrIndex == 0 || device->drive_info.softSATFlags.rtfrIndex > 0x0F)
        {
            device->drive_info.softSATFlags.rtfrIndex = 1;
        }
        // now copy the rtfr
        safe_memcpy(
            &device->drive_info.softSATFlags.ataPassthroughResults[device->drive_info.softSATFlags.rtfrIndex - 1],
            sizeof(ataReturnTFRs), &ataCommand.rtfr, sizeof(ataReturnTFRs));
        // set the log index
        ataReturnDescriptor[14] = device->drive_info.softSATFlags.rtfrIndex;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0x1D,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, ataReturnDescriptor,
                                       1);
#endif // SAT_SPEC_SUPPORTED
    }
    scsiIoCtx->pAtaCmdOpts = M_NULLPTR;
    return ret;
}

static eReturnValues translate_SCSI_Read_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    uint64_t lba            = UINT64_C(0);
    uint32_t transferLength = UINT32_C(0);
    bool     fua            = false;
    bool     invalidField   = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x08: // read 6
        lba            = M_BytesTo4ByteValue(0, (scsiIoCtx->cdb[1] & 0x1F), scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
        transferLength = scsiIoCtx->cdb[4];
        if (get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0)
        {
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(0);
            invalidField = true;
        }
        if (transferLength == 0) // read6 a zero is more like ATA to do a larger transfer. Other reads, just return
                                 // success without doing anything.
        {
            transferLength = 256;
        }
        break;
    case 0x28: // read 10
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 &&
             scsiIoCtx->cdb[1] & BIT0) // reladr bit. Obsolete.
                                       //|| ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] &
                                       // BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 &&
                scsiIoCtx->cdb[1] & BIT2) // cannot support RACR bit in this translation since we cannot do fpdma
            || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0xA8: // read 12
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 &&
             scsiIoCtx->cdb[1] & BIT0) // reladr bit. Obsolete.
                                       //|| ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] &
                                       // BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 &&
                scsiIoCtx->cdb[1] & BIT2) // cannot support RACR bit in this translation since we cannot do fpdma
            ||
            ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0x88: // read 16
        lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                  scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        transferLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        // sbc2 fua_nv bit can be ignored according to SAT.
        // We don't support RARC since was cannot do FPDMA in software SAT
        // We don't support DLD bits either
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 &&
             scsiIoCtx->cdb[1] & BIT0) // reladr bit. Obsolete.
                                       //|| ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] &
                                       // BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 &&
                scsiIoCtx->cdb[1] & BIT2) // cannot support RACR bit in this translation since we cannot do fpdma
            ||
            ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (transferLength == 0) // this is allowed and it means to validate inputs and return success
    {
        return SUCCESS;
    }
    else if (transferLength > UINT32_C(65536))
    {
        // return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x28: // read 10
            fieldPointer = UINT16_C(7);
            break;
        case 0xA8: // read 12
            fieldPointer = UINT16_C(6);
            break;
        case 0x88: // read 16
            fieldPointer = UINT16_C(10);
            break;
        }
        bitPointer = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    return satl_Read_Command(scsiIoCtx, lba, scsiIoCtx->pdata, transferLength * device->drive_info.deviceBlockSize,
                             fua);
}

static eReturnValues translate_SCSI_Write_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    bool     fua            = false;
    uint64_t lba            = UINT64_C(0);
    uint32_t transferLength = UINT32_C(0);
    bool     invalidField   = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x0A: // write 6
        lba            = M_BytesTo4ByteValue(0, (scsiIoCtx->cdb[1] & 0x1F), scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
        transferLength = scsiIoCtx->cdb[4];
        if (get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0)
        {
            bitPointer   = UINT8_C(0);
            fieldPointer = UINT16_C(1);
            invalidField = true;
        }
        if (transferLength == 0) // write 6, zero means a maximum possible transfer size, which is 256
        {
            transferLength = 256;
        }
        break;
    case 0x2A: // write 10
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 &&
             scsiIoCtx->cdb[1] & BIT0) // reladr bit. Obsolete.
                                       //|| ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] &
                                       // BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2) // reserved bit
            || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0xAA: // write 12
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 &&
             scsiIoCtx->cdb[1] & BIT0) // reladr bit. Obsolete.
                                       //|| ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] &
                                       // BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2) // reserved bit
            ||
            ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0x8A: // write 16
        lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                  scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        transferLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        // sbc2 fua_nv bit can be ignored according to SAT.
        // We don't support DLD bits either
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 &&
             scsiIoCtx->cdb[1] & BIT0) // reladr bit. Obsolete. also now the DLD2 bit
                                       //|| ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] &
                                       // BIT1)//FUA_NV bit. Can be ignored by SATLs or implemented
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2) // reserved bit
            ||
            ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (transferLength == 0)
    {
        // a transfer length of zero means do nothing but validate inputs and is not an error
        return SUCCESS;
    }
    else if (transferLength > UINT32_C(65536))
    {
        // return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x2A: // write 10
            fieldPointer = UINT16_C(7);
            break;
        case 0xAA: // write 12
            fieldPointer = UINT16_C(6);
            break;
        case 0x8A: // write 16
            fieldPointer = UINT16_C(10);
            break;
        }
        bitPointer = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    return satl_Write_Command(scsiIoCtx, lba, scsiIoCtx->pdata,
                              transferLength * scsiIoCtx->device->drive_info.deviceBlockSize, fua);
}

static eReturnValues translate_SCSI_Write_Same_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                   = SUCCESS;
    uint8_t       wrprotect             = get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5);
    bool          anchor                = scsiIoCtx->cdb[1] & BIT4;
    bool          unmap                 = scsiIoCtx->cdb[1] & BIT3;
    bool          logicalBlockData      = scsiIoCtx->cdb[1] & BIT1; // Obsolete (SAT2 supports this)
    bool          physicalBlockData     = scsiIoCtx->cdb[1] & BIT2; // Obsolete (SAT2 supports this)
    bool          relativeAddress       = false;                    // Long obsolete.
    bool          ndob                  = false;
    uint64_t      logicalBlockAddress   = UINT64_C(0);
    uint64_t      numberOflogicalBlocks = UINT64_C(0);
    uint8_t       groupNumber           = UINT8_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && wrprotect != 0) ||
        ((fieldPointer = 1) != 0 && (bitPointer = 4) != 0 && (!unmap && anchor)) ||
        ((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 &&
         (unmap && !(le16_to_host(device->drive_info.IdentifyData.ata.Word169) &
                     BIT0))) // drive doesn't support trim, so we cannot do this...
        || ((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && (logicalBlockData && unmap)) ||
        ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 &&
         physicalBlockData) // not supporting physical or logical block data bits at this time. Can be implemented
                            // according to SAT2 though!
        || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && logicalBlockData))
    {
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }

    if (scsiIoCtx->cdb[OPERATION_CODE] == 0x41) // write same 10
    {
        relativeAddress = scsiIoCtx->cdb[1] & BIT0;
        logicalBlockAddress =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        numberOflogicalBlocks = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        groupNumber           = get_bit_range_uint8(scsiIoCtx->cdb[6], 5, 0);
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && relativeAddress) ||
            ((fieldPointer = 6) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->cdb[6] & BIT7) ||
            ((fieldPointer = 6) != 0 && (bitPointer = 6) != 0 && scsiIoCtx->cdb[6] & BIT6) ||
            ((fieldPointer = 6) != 0 && (bitPointer = 5) != 0 && groupNumber != 0))
        {
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return NOT_SUPPORTED;
        }
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == 0x93) // write same 16
    {
        ndob = scsiIoCtx->cdb[1] & BIT0;
        logicalBlockAddress =
            M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        numberOflogicalBlocks =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        groupNumber = get_bit_range_uint8(scsiIoCtx->cdb[14], 5, 0);
        if (((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && (logicalBlockData && ndob)) ||
            ((fieldPointer = 14) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->cdb[6] & BIT7) ||
            ((fieldPointer = 14) != 0 && (bitPointer = 6) != 0 && scsiIoCtx->cdb[6] & BIT6) ||
            ((fieldPointer = 14) != 0 && (bitPointer = 5) != 0 && groupNumber != 0))
        {
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return NOT_SUPPORTED;
        }
    }
    else
    {
        // invalid operation code...this should be caught below in the function that calls this
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return BAD_PARAMETER;
    }
    if (numberOflogicalBlocks == 0)
    {
        // Support a value of zero to overwrite the whole drive.
        numberOflogicalBlocks = device->drive_info.deviceMaxLba;
    }
    // perform the write same operation...
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
    if (unmap)
    {
        // NDOP bit is not specified in this mode in SAT4...doesn't exist in SAT3
        // NOTE: We still need to add unmap bit support!
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(3);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    else
    {
#endif // SAT_SPEC_SUPPORTED
        bool useATAWriteCommands  = false;
        bool ataWritePatternZeros = false;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
        if (ndob)
        {
            // writing zeros
            // If the zeros ext command is supported, it can be used (if number of logical blocks is not zero)
            if (device->drive_info.softSATFlags.zeroExtSupported && numberOflogicalBlocks < UINT16_MAX)
            {
                ret = ata_Zeros_Ext(device, C_CAST(uint16_t, numberOflogicalBlocks), logicalBlockAddress, false);
            }
            else
            {
                // else if SCT write same, function 01 or 101 (foreground or background...SATL decides)
                if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word206)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT0 &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT2)
                {
                    DECLARE_ZERO_INIT_ARRAY(uint8_t, pattern, 4); // 32bits set to zero
                    uint32_t currentTimeout                  = device->drive_info.defaultTimeoutSeconds;
                    device->drive_info.defaultTimeoutSeconds = UINT32_MAX;
                    ret = send_ATA_SCT_Write_Same(device, 0x0101, logicalBlockAddress, numberOflogicalBlocks,
                                                  &pattern[0], 4);
                    device->drive_info.defaultTimeoutSeconds = currentTimeout;
                }
                else
                {
                    // else ATA Write commands
                    useATAWriteCommands  = true;
                    ataWritePatternZeros = true;
                }
            }
        }
        else
#endif // SAT_SPEC_SUPPORTED
        {
            // write the data block
#if 0 // remove this #if when this is supported
            if (logicalBlockData)
            {
                //Replace first 4 bytes with the least significant LBA bytes...uses ATA Write commands.
            }
            else
#endif
            {
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT0 &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT2)
                {
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
                    // If SCT - function 102 (foreground). 1 or more SCT commands
                    ret = send_ATA_SCT_Write_Same(device, 0x0102, logicalBlockAddress, numberOflogicalBlocks,
                                                  scsiIoCtx->pdata, scsiIoCtx->dataLength);
#else  // SAT_SPEC_SUPPORTED
       // If SCT - function 02 or 04 (background) 1 or more SCT commands
                ret = send_ATA_SCT_Write_Same(device, 0x0002, logicalBlockAddress, numberOflogicalBlocks,
                                              scsiIoCtx->pdata, scsiIoCtx->dataLength);
#endif // SAT_SPEC_SUPPORTED
                }
                else
                {
                    // Else - ATA Write commands to the medium
                    useATAWriteCommands = true;
                }
            }
        }
        if (useATAWriteCommands)
        {
            uint8_t* writePattern;
            uint32_t patternLength = scsiIoCtx->dataLength;
            if (ataWritePatternZeros)
            {
                patternLength = 65535; // 64k
            }
            writePattern = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(patternLength, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (writePattern)
            {
                if (!ataWritePatternZeros)
                {
                    safe_memcpy(writePattern, patternLength, scsiIoCtx->pdata, patternLength);
                }
                ret = satl_Sequential_Write_Commands(scsiIoCtx, logicalBlockAddress, numberOflogicalBlocks,
                                                     writePattern, patternLength);
                safe_free_aligned(&writePattern);
            }
            else
            {
                // the satl_sequential_write_commands function will handle setting the return sense data if it fails for
                // any reason
            }
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Synchronize_Cache_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // check the read command and get the LBA from it
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x35: // synchronize cache 10
        if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0) || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0))
        {
            // can't support these bits (including immediate)
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return SUCCESS;
        }
        break;
    case 0x91: // synchronize cache 16
        if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0) ||
            ((fieldPointer = 14) != 0 && scsiIoCtx->cdb[14] != 0))
        {
            if (bitPointer == 0)
            {
                uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                uint8_t counter         = UINT8_C(0);
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer =
                    counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            }
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            // can't support these bits (including immediate)
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return SUCCESS;
        }
        break;
    default:
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        return BAD_PARAMETER;
    }
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT13) // ext command
    {
        ret = ata_Flush_Cache(device, true);
    }
    else if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT12) // 28bit command
    {
        ret = ata_Flush_Cache(device, false);
    }
    // else //ata command not supported - shouldn't happen
    // now set sense data
    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

static eReturnValues translate_SCSI_Verify_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    uint8_t  byteCheck          = UINT8_C(0);
    uint64_t lba                = UINT64_C(0);
    uint32_t verificationLength = UINT32_C(0);
    bool     invalidField       = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x2F: // verify 10
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba       = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        verificationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) ||
            ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0xAF: // verify 12
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba       = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        verificationLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) ||
            ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0x8F: // verify 16
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba       = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                        scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        verificationLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) ||
            ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (verificationLength == 0) // this is allowed and it means to validate inputs and return success
    {
        return SUCCESS;
    }
    else if (verificationLength > UINT32_C(65536))
    {
        // return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x2F: // verify 10
            fieldPointer = UINT16_C(7);
            break;
        case 0xAF: // verify 12
            fieldPointer = UINT16_C(6);
            break;
        case 0x8F: // verify 16
            fieldPointer = UINT16_C(10);
            break;
        }
        bitPointer = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    if (byteCheck == 2) // this mode is reserved
    {
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(2);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    return satl_Read_Verify_Command(scsiIoCtx, lba, verificationLength * device->drive_info.deviceBlockSize, byteCheck);
}

static eReturnValues translate_SCSI_Write_And_Verify_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                = SUCCESS;
    uint8_t       byteCheck          = UINT8_C(0);
    bool          dmaSupported       = false;
    uint64_t      lba                = UINT64_C(0);
    uint32_t      verificationLength = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (le16_to_host(device->drive_info.IdentifyData.ata.Word049) & BIT8)
    {
        if ((le16_to_host(device->drive_info.IdentifyData.ata.Word063) & (BIT0 | BIT1 | BIT2)) ||
            le16_to_host(device->drive_info.IdentifyData.ata.Word088) & 0xFF)
        {
            dmaSupported = true;
        }
    }
    bool invalidField = false;
    // check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x2E: // write and verify 10
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba       = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        verificationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) ||
            ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[6], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0xAE: // write and verify 12
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba       = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        verificationLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) ||
            ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[10], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    case 0x8E: // write and verify 16
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba       = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                        scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        verificationLength =
            M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) ||
            ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 6) != 0))
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (verificationLength == 0) // this is allowed and it means to validate inputs and return success
    {
        return SUCCESS;
    }
    else if (verificationLength > UINT32_C(65536))
    {
        // return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x2E: // write & verify 10
            fieldPointer = UINT16_C(7);
            break;
        case 0xAE: // write & verify 12
            fieldPointer = UINT16_C(6);
            break;
        case 0x8E: // write & verify 16
            fieldPointer = UINT16_C(10);
            break;
        }
        bitPointer = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    // check if 48bit
    if (le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
    {
        if (verificationLength == UINT32_C(65536))
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            verificationLength = 0;
        }
        if (dmaSupported)
        {
            ret = ata_Write_DMA(device, lba, scsiIoCtx->pdata, scsiIoCtx->dataLength, true, false);
        }
        else // pio
        {
            ret = ata_Write_Sectors(device, lba, scsiIoCtx->pdata, scsiIoCtx->dataLength, true);
        }
        if (byteCheck == 0)
        {
            // send ata read-verify
            ret = ata_Read_Verify_Sectors(device, true, C_CAST(uint16_t, verificationLength), lba);
        }
        else
        {
            uint8_t* compareBuf = M_NULLPTR;
            // uint32_t compareLength = UINT32_C(0);
            if (byteCheck == 0x02)
            {
                fieldPointer = UINT16_C(1);
                bitPointer   = UINT8_C(2);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return SUCCESS;
            }
            compareBuf = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t),
                                                                          device->os_info.minimumAlignment));
            if (!compareBuf)
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                return MEMORY_FAILURE;
            }
            if (dmaSupported)
            {
                ret = ata_Read_DMA(device, lba, compareBuf, C_CAST(uint16_t, verificationLength), scsiIoCtx->dataLength,
                                   true);
            }
            else // pio
            {
                ret = ata_Read_Sectors(device, lba, compareBuf, C_CAST(uint16_t, verificationLength),
                                       scsiIoCtx->dataLength, true);
            }
            bool errorFound = false;
            if (byteCheck == 0x01)
            {
                if (memcmp(compareBuf, scsiIoCtx->pdata, scsiIoCtx->dataLength) != 0)
                {
                    // does not match - set miscompare error
                    errorFound = true;
                }
            }
            else if (byteCheck == 0x03)
            {
                // compare each logical sector to the data sent into this command

                uint32_t iter = UINT32_C(0);
                for (; iter < scsiIoCtx->dataLength; iter += device->drive_info.deviceBlockSize)
                {
                    if (memcmp(&compareBuf[iter], scsiIoCtx->pdata,
                               M_Min(device->drive_info.deviceBlockSize, scsiIoCtx->dataLength)) != 0)
                    {
                        errorFound = true;
                        break;
                    }
                }
            }
            safe_free_aligned(&compareBuf);
            if (errorFound)
            {
                // set failure
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MISCOMPARE, 0x1D,
                                               0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                return SUCCESS;
            }
            else
            {
                // set success
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00,
                                               0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                return SUCCESS;
            }
        }
    }
    else // 28bit command
    {
        if (verificationLength > 256)
        {
            // return an error
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR,
                                           0);
            return SUCCESS;
        }
        if (verificationLength == 256)
        {
            // ATA Spec says to transfer this may sectors, you must set the sector count to zero (Not that any
            // passthrough driver will actually allow this)
            verificationLength = 0;
        }
        if (dmaSupported)
        {
            ret = ata_Write_DMA(device, lba, scsiIoCtx->pdata, scsiIoCtx->dataLength, false, false);
        }
        else // pio
        {
            ret = ata_Write_Sectors(device, lba, scsiIoCtx->pdata, scsiIoCtx->dataLength, false);
        }
        if (byteCheck == 0)
        {
            // send ata read-verify
            ret = ata_Read_Verify_Sectors(device, false, C_CAST(uint16_t, verificationLength), lba);
        }
        else
        {
            uint8_t* compareBuf = M_NULLPTR;
            // uint32_t compareLength = UINT32_C(0);
            if (byteCheck == 0x02)
            {
                fieldPointer = UINT16_C(1);
                bitPointer   = UINT8_C(2);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return SUCCESS;
            }
            compareBuf = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(scsiIoCtx->dataLength, sizeof(uint8_t),
                                                                          device->os_info.minimumAlignment));
            if (!compareBuf)
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                return MEMORY_FAILURE;
            }
            if (dmaSupported)
            {
                ret = ata_Read_DMA(device, lba, compareBuf, C_CAST(uint16_t, verificationLength), scsiIoCtx->dataLength,
                                   false);
            }
            else // pio
            {
                ret = ata_Read_Sectors(device, lba, compareBuf, C_CAST(uint16_t, verificationLength),
                                       scsiIoCtx->dataLength, false);
            }
            bool errorFound = false;
            if (byteCheck == 0x01)
            {
                if (memcmp(compareBuf, scsiIoCtx->pdata, scsiIoCtx->dataLength) != 0)
                {
                    // does not match - set miscompare error
                    errorFound = true;
                }
            }
            else if (byteCheck == 0x03)
            {
                // compare each logical sector to the data sent into this command

                uint32_t iter = UINT32_C(0);
                for (; iter < scsiIoCtx->dataLength; iter += device->drive_info.deviceBlockSize)
                {
                    if (memcmp(&compareBuf[iter], scsiIoCtx->pdata,
                               M_Min(device->drive_info.deviceBlockSize, scsiIoCtx->dataLength)) != 0)
                    {
                        errorFound = true;
                        break;
                    }
                }
            }
            safe_free_aligned(&compareBuf);
            if (errorFound)
            {
                // set failure
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MISCOMPARE, 0x1D,
                                               0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                return SUCCESS;
            }
            else
            {
                // set success
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00,
                                               0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                return SUCCESS;
            }
        }
    }
    // now set sense data
    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

static eReturnValues translate_SCSI_Format_Unit_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret          = SUCCESS;
    uint8_t       bitPointer   = UINT8_C(0);
    uint16_t      fieldPointer = UINT16_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    bool    longList         = scsiIoCtx->cdb[1] & BIT5;
    bool    formatData       = scsiIoCtx->cdb[1] & BIT4;
    uint8_t defectListFormat = get_bit_range_uint8(scsiIoCtx->cdb[1], 2, 0);
    // Other bytes are newer than SBC3 or really old and obsolete (error checking is done top to bottom, left bit to
    // right bit)
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 6)) ||
        ((bitPointer = 3) != 0 && (fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT3) ||
        ((fieldPointer = 2) != 0 &&
         (bitPointer = 0 /*reset this value since we changed it in the previous comparison*/) == 0 &&
         scsiIoCtx->cdb[2]) ||
        ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3]) || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4]))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    else
    {
        if (formatData)
        {
            // Parameter header information
            // uint8_t protectionFieldUsage = get_bit_range_uint8(scsiIoCtx->pdata[0], 2, 0);
            bool formatOptionsValid = scsiIoCtx->pdata[1] & BIT7;
            // bool disablePrimary = scsiIoCtx->pdata[1] & BIT6; //SATL ignores this bit. Commented out so we can use it
            // if we ever need to.
            bool     disableCertification        = scsiIoCtx->pdata[1] & BIT5;
            bool     stopFormat                  = scsiIoCtx->pdata[1] & BIT4;
            bool     initializationPattern       = scsiIoCtx->pdata[1] & BIT3;
            uint8_t  initializationPatternOffset = UINT8_C(0);
            bool     disableSaveParameters       = scsiIoCtx->pdata[1] & BIT2; // long obsolete
            bool     immediate                   = scsiIoCtx->pdata[1] & BIT1;
            bool     vendorSpecific              = scsiIoCtx->pdata[1] & BIT0;
            uint8_t  p_i_information             = UINT8_C(0);
            uint8_t  protectionIntervalExponent  = UINT8_C(0);
            uint32_t defectListLength            = UINT32_C(0);
            if (longList)
            {
                p_i_information            = M_Nibble1(scsiIoCtx->pdata[3]);
                protectionIntervalExponent = M_Nibble0(scsiIoCtx->pdata[3]);
                defectListLength = M_BytesTo4ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5], scsiIoCtx->pdata[6],
                                                       scsiIoCtx->pdata[7]);
                if (initializationPattern)
                {
                    initializationPatternOffset = 8;
                }
            }
            else
            {
                defectListLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
                if (initializationPattern)
                {
                    initializationPatternOffset = 4;
                }
            }
            // check parameter data
            if (/*check all combinations that we don't support*/
                ((fieldPointer = 0) == 0 && (bitPointer = 0) == 0 &&
                 scsiIoCtx->pdata[0] !=
                     0) // we don't support protection information and reserved bytes should also be zeroed out
                || (formatOptionsValid && (fieldPointer = 1) != 0 && (bitPointer = 4) != 0 &&
                    stopFormat) // Doesn't make sense to support since we cannot accept a defect list. We could also
                                // ignore this, but this should be fine
                || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 &&
                    disableSaveParameters) // check that this obsolete bit isn't set
                || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 &&
                    immediate) // we cannot support the immediate bit in this implementation right now. We would need
                               // multi-threading to do this...
                || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 &&
                    vendorSpecific) // We don't have a vendor specific functionality in here
                || (longList && (fieldPointer = 2) != 0 && (bitPointer = 0) == 0 &&
                    scsiIoCtx->pdata[2] != 0) // checking if reserved byte in long header is set
                || (longList && (fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && p_i_information != 0) ||
                (longList && (fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && protectionIntervalExponent != 0) ||
                ((fieldPointer = UINT16_MAX) != 0 && (bitPointer = UINT8_MAX) != 0 &&
                 (defectListFormat == 0 || defectListFormat == 6) &&
                 defectListLength != 0) // See SAT spec CDB field definitions
                || ((fieldPointer = UINT16_MAX) != 0 && (bitPointer = 24) != 0 && defectListFormat != 0 &&
                    defectListFormat != 6) // See SAT spec CDB field definitions
            )
            {
                if (fieldPointer == UINT16_MAX)
                {
                    // defect list issue detected. Need to set which field is actually in error
                    if (bitPointer == UINT8_MAX)
                    {
                        // defect list length error
                        if (longList)
                        {
                            fieldPointer = UINT16_C(4);
                        }
                        else
                        {
                            fieldPointer = UINT16_C(2);
                        }
                        bitPointer = UINT8_C(7);
                    }
                    else
                    {
                        // error in defect list data. Need to figure out the offset of the defect list (if any)
                        if (initializationPattern)
                        {
                            uint16_t initializationPatternLength =
                                M_BytesTo2ByteValue(scsiIoCtx->pdata[initializationPatternOffset + 2],
                                                    scsiIoCtx->pdata[initializationPatternOffset + 3]);
                            if (longList)
                            {
                                fieldPointer = 8 + 4 + initializationPatternLength;
                            }
                            else
                            {
                                fieldPointer = 4 + 4 + initializationPatternLength;
                            }
                        }
                        else
                        {
                            if (longList)
                            {
                                fieldPointer = UINT16_C(8);
                            }
                            else
                            {
                                fieldPointer = UINT16_C(4);
                            }
                            bitPointer = UINT8_C(7);
                        }
                    }
                }
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                }
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                // invalid field in parameter list
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
            else
            {
                // Check FOV bit combinations
                if (formatOptionsValid)
                {
                    bool performWriteOperation   = false;
                    bool performCertifyOperation = false;
                    // Initialization pattern information. Check that initializationPattern bool is set to true!
                    bool    securityInitialize = false;
                    uint8_t initializationPatternModifier =
                        0; // This is obsolete since SBC2...we will just return an error
                    uint8_t initializationPatternByte0ReservedBits =
                        UINT8_C(0); // so we can make sure no invalid field was set.
                    uint8_t  initializationPatternType   = UINT8_C(0);
                    uint16_t initializationPatternLength = UINT16_C(0);
                    uint8_t* initializationPatternPtr    = M_NULLPTR;
                    if (initializationPattern)
                    {
                        // Set up the initialization pattern information since we were given one
                        initializationPatternModifier =
                            get_bit_range_uint8(scsiIoCtx->pdata[initializationPatternOffset + 0], 7, 6);
                        securityInitialize = scsiIoCtx->pdata[initializationPatternOffset + 1] & BIT5;
                        initializationPatternByte0ReservedBits =
                            get_bit_range_uint8(scsiIoCtx->pdata[initializationPatternOffset + 0], 4, 0);
                        initializationPatternType = scsiIoCtx->pdata[initializationPatternOffset + 1];
                        initializationPatternLength =
                            M_BytesTo2ByteValue(scsiIoCtx->pdata[initializationPatternOffset + 2],
                                                scsiIoCtx->pdata[initializationPatternOffset + 3]);
                        if (initializationPatternLength > 0)
                        {
                            initializationPatternPtr = &scsiIoCtx->pdata[initializationPatternOffset + 4];
                        }
                    }
                    // Check bit combinations to make sure we can do things the right way!
                    if (disableCertification && !initializationPattern)
                    {
                        // return good status and do nothing else
                        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR,
                                                       0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                                       M_NULLPTR, 0);
                    }
                    else if (!initializationPattern && !disableCertification)
                    {
                        // SATL may or may not support media certification. If not supported, then return invalid field
                        // in parameter list...otherwise we have stuff to do
                        performCertifyOperation = true;
                    }
                    else if (!disableCertification && initializationPattern)
                    {
                        // SATL may or may not support media certification
                        performCertifyOperation = true;
                        // SATL may or may not support write operation described by the initialization pattern
                        // descriptor to perform this translation
                        performWriteOperation = true;
                    }
                    else if (disableCertification && initializationPattern)
                    {
                        // SATL may or may not support write operation described by the initialization pattern
                        // descriptor to perform this translation
                        performWriteOperation = true;
                    }
                    // Before we begin writing or certification, we need to check some flags to make sure nothing
                    // invalid is set.
                    if (initializationPattern && // this must be set for us to care about any of these other fields...
                        (((fieldPointer = initializationPatternOffset) != 0 && (bitPointer = 7) != 0 &&
                          initializationPatternModifier) // check if obsolete bits are set
                         || ((fieldPointer = initializationPatternOffset) != 0 && (bitPointer = 5) != 0 &&
                             securityInitialize) /*not supporting this since we cannot write to the reallocated sectors
                                                    on the drive like this asks*/
                         || ((fieldPointer = initializationPatternOffset) != 0 && (bitPointer = 0) != 0 &&
                             initializationPatternByte0ReservedBits != 0) ||
                         ((fieldPointer = initializationPatternOffset + 1) != 0 && (bitPointer = 7) != 0 &&
                          (initializationPatternType != 0x00 && initializationPatternType != 0x01))))
                    {
                        if (bitPointer == 0)
                        {
                            uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                            uint8_t counter         = UINT8_C(0);
                            while (reservedByteVal > 0 && counter < 8)
                            {
                                reservedByteVal >>= 1;
                                ++counter;
                            }
                            bitPointer = counter - 1; // because we should always get a count of at least 1 if here and
                                                      // bits are zero indexed
                        }
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        // invalid field in parameter list
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    else
                    {
                        // If we need to do a write operation, we need to do it first!
                        if (performWriteOperation)
                        {
                            if (initializationPatternLength == 0x0004 &&
                                is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word206)) &&
                                le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT0 &&
                                le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT2)
                            {
                                // SCT write same
                                ret = send_ATA_SCT_Write_Same(device, WRITE_SAME_BACKGROUND_USE_PATTERN_FIELD, 0,
                                                              device->drive_info.deviceMaxLba, initializationPatternPtr,
                                                              initializationPatternLength);
                                if (ret != SUCCESS)
                                {
                                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs,
                                                            scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                                }
                            }
                            else
                            {
                                // make sure the initialization pattern is less than or equal to the logical block
                                // length
                                if (initializationPatternLength > device->drive_info.deviceBlockSize)
                                {
                                    fieldPointer = initializationPatternOffset + 2;
                                    bitPointer   = UINT8_C(7);
                                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false,
                                                                                    true, bitPointer, fieldPointer);
                                    // invalid field in parameter list
                                    ret = NOT_SUPPORTED;
                                    set_Sense_Data_For_Translation(
                                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                        device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                        senseKeySpecificDescriptor, 1);
                                }
                                else
                                {
                                    uint32_t writeSectors64K = UINT32_C(65535) / device->drive_info.deviceBlockSize;
                                    // ATA Write commands
                                    uint32_t ataWriteDataLength = writeSectors64K * device->drive_info.deviceBlockSize;
                                    uint8_t* ataWritePattern    = M_REINTERPRET_CAST(
                                        uint8_t*, safe_calloc_aligned(ataWriteDataLength, sizeof(uint8_t),
                                                                         device->os_info.minimumAlignment));
                                    if (ataWritePattern)
                                    {
                                        if (initializationPatternLength > 0)
                                        {
                                            // copy the provided pattern into our buffer
                                            for (uint32_t copyIter = UINT32_C(0); copyIter < ataWriteDataLength;
                                                 copyIter += device->drive_info.deviceBlockSize)
                                            {
                                                safe_memcpy(&ataWritePattern[copyIter],
                                                            ataWriteDataLength -
                                                                (copyIter * device->drive_info.deviceBlockSize),
                                                            initializationPatternPtr, initializationPatternLength);
                                            }
                                        }

                                        ret = satl_Sequential_Write_Commands(scsiIoCtx, 0,
                                                                             device->drive_info.deviceMaxLba,
                                                                             ataWritePattern, ataWriteDataLength);
                                        if (ret != SUCCESS)
                                        {
                                            set_Sense_Data_For_Translation(
                                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR,
                                                0x03, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                                M_NULLPTR, 0);
                                        }
                                        safe_free_aligned(&ataWritePattern);
                                    }
                                    else
                                    {
                                        // Fatal Error!
                                        ret = FAILURE;
                                    }
                                }
                            }
                        }
                        // If we need to do a certify operation, we need to do it first!
                        if (performCertifyOperation)
                        {
                            // If an error is found while performing certification,
                            // the SATL will continually try write commands to the sector until it is fixed or an error
                            // other than unrecoverable read error is returned make sure the initialization pattern is
                            // less than or equal to the logical block length
                            if (initializationPatternLength > device->drive_info.deviceBlockSize)
                            {
                                fieldPointer = initializationPatternOffset + 2;
                                bitPointer   = UINT8_C(7);
                                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                                bitPointer, fieldPointer);
                                // invalid field in parameter list
                                ret = NOT_SUPPORTED;
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                    senseKeySpecificDescriptor, 1);
                            }
                            else
                            {
                                uint32_t verifySectors64K = UINT32_C(65535) / device->drive_info.deviceBlockSize;
                                bool     deviceFault      = false;
                                for (uint64_t lbaIter = UINT64_C(0);
                                     lbaIter < device->drive_info.deviceMaxLba && !deviceFault;
                                     lbaIter += verifySectors64K)
                                {
                                    if (lbaIter + verifySectors64K > device->drive_info.deviceMaxLba)
                                    {
                                        // make sure we don't try to write off the end of the drive!
                                        verifySectors64K = C_CAST(uint32_t, device->drive_info.deviceMaxLba - lbaIter);
                                    }
                                    ret = satl_Read_Verify_Command(scsiIoCtx, lbaIter, verifySectors64K, 0);
                                    if (ret != SUCCESS)
                                    {
                                        // if this was an unrecovered error, then we need to exit with bad sense
                                        // data...otherwise continue onwards
                                        if (device->drive_info.lastCommandRTFRs.status == 0x51 &&
                                            device->drive_info.lastCommandRTFRs.error & BIT6)
                                        {
                                            // TODO: add logic to check the RTFRs to see which LBA was the first bad one
                                            // during the verify so that we only touch it... Problem with this TODO is
                                            // if we are talking to something that doesn't give back all RTFRs (SAT to
                                            // OS) or is a broken driver, this won't work
                                            uint32_t writeSectors64K =
                                                UINT32_C(65536) / device->drive_info.deviceBlockSize;
                                            // ATA Write commands
                                            uint32_t ataWriteDataLength =
                                                writeSectors64K * device->drive_info.deviceBlockSize;
                                            uint8_t* ataWritePattern = C_CAST(
                                                uint8_t*, safe_calloc_aligned(ataWriteDataLength, sizeof(uint8_t),
                                                                              device->os_info.minimumAlignment));
                                            if (ataWritePattern)
                                            {
                                                if (initializationPatternLength > 0)
                                                {
                                                    // copy the provided pattern into our buffer
                                                    for (uint32_t copyIter = UINT32_C(0); copyIter < ataWriteDataLength;
                                                         copyIter += device->drive_info.deviceBlockSize)
                                                    {
                                                        safe_memcpy(&ataWritePattern[copyIter],
                                                                    ataWriteDataLength -
                                                                        (copyIter * device->drive_info.deviceBlockSize),
                                                                    initializationPatternPtr,
                                                                    initializationPatternLength);
                                                    }
                                                }
                                                bool unrecoveredReadError = true;
                                                bool exitTheLoop          = false;
                                                while ((unrecoveredReadError && !deviceFault) || exitTheLoop)
                                                {
                                                    unrecoveredReadError =
                                                        false; // set to false before writing and re-verifying. This
                                                               // will get changed if it's still an unrecovered error
                                                    if (lbaIter + writeSectors64K > device->drive_info.deviceMaxLba)
                                                    {
                                                        // make sure we don't try to write off the end of the drive!
                                                        writeSectors64K =
                                                            C_CAST(uint32_t, device->drive_info.deviceMaxLba - lbaIter);
                                                    }
                                                    ret = satl_Write_Command(scsiIoCtx, lbaIter, ataWritePattern,
                                                                             ataWriteDataLength, false);
                                                    if (ret != SUCCESS)
                                                    {
                                                        if (device->drive_info.lastCommandRTFRs.status & BIT5)
                                                        {
                                                            deviceFault = true;
                                                        }
                                                    }
                                                    if (!deviceFault)
                                                    {
                                                        // haven't had a device fault, so try verifying the sector, then
                                                        // continue if it worked,
                                                        ret = satl_Read_Verify_Command(scsiIoCtx, lbaIter,
                                                                                       verifySectors64K, 0);
                                                        if (ret != SUCCESS)
                                                        {
                                                            // if this was an unrecovered error, then we need to exit
                                                            // with bad sense data...otherwise continue onwards
                                                            if (device->drive_info.lastCommandRTFRs.status == 0x51 &&
                                                                device->drive_info.lastCommandRTFRs.error & BIT6)
                                                            {
                                                                unrecoveredReadError = true;
                                                            }
                                                            else if (device->drive_info.lastCommandRTFRs.status & BIT5)
                                                            {
                                                                deviceFault = true;
                                                            }
                                                            else
                                                            {
                                                                exitTheLoop = true;
                                                            }
                                                        }
                                                    }
                                                }
                                                safe_free_aligned(&ataWritePattern);
                                            }
                                            else
                                            {
                                                // Fatal error, break out of main loop
                                                ret = MEMORY_FAILURE;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (deviceFault || ret != SUCCESS)
                                {
                                    // medium error, record not found
                                    ret = NOT_SUPPORTED;
                                    set_Sense_Data_For_Translation(
                                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x14, 0x01,
                                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                }
                            }
                        }
                    }
                }
                else
                {
                    // Not checking disablePrimary below because the SATL ignores this bit
                    if (!initializationPattern && !disableCertification)
                    {
                        // return good status and do nothing else
                        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR,
                                                       0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                                       M_NULLPTR, 0);
                    }
                    else if (((fieldPointer = 1) != 0 && (bitPointer = 5) != 0 && disableCertification) ||
                             ((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && initializationPattern))
                    {
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        // invalid field in parameter list
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0x00,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
            }
        }
        else
        {
            // return good status since no parameter data was transferred
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                           device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Test_Unit_Ready_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret       = SUCCESS;
    uint8_t       powerMode = UINT8_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0) || ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) ||
        ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0) || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }

    // send the check power mode command
    if (SUCCESS != ata_Check_Power_Mode(device, &powerMode))
    {
        // check for the device fault bit
        if (device->drive_info.lastCommandRTFRs.status & ATA_STATUS_BIT_DEVICE_FAULT)
        {
            // set sense data to hardware error->Logical unit failure | internal target failure
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x44,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        }
        else
        {
            // check sanitize status...check if device supports sanitize before doing this?
            if (SUCCESS == ata_Sanitize_Status(device, false))
            {
                // In progress?
                if (device->drive_info.lastCommandRTFRs.secCntExt & BIT6)
                {
                    // set up a progress descriptor
                    set_Sense_Key_Specific_Descriptor_Progress_Indicator(
                        senseKeySpecificDescriptor, M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaMid,
                                                                        device->drive_info.lastCommandRTFRs.lbaLow));
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NOT_READY, 0x04, 0x1B,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                else if (device->drive_info.lastCommandRTFRs.secCntExt & BIT15)
                {
                    // sanitize completed without error
                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, 0, 0,
                                                   device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR,
                                                   0);
                }
                else
                {
                    // sanitize completed with error
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NOT_READY, 0x05,
                                               0, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR,
                                               0);
            }
        }
    }
    else
    {
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, 0, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    }
    return ret;
}

static eReturnValues translate_SCSI_Reassign_Blocks_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret               = SUCCESS;
    bool          longLba           = false;
    bool          extCommand        = false;
    uint32_t      iterator          = UINT32_C(4); // the lba list starts at this byte
    uint8_t       incrementAmount   = UINT8_C(4);  // short parameters are 4 bytes in size
    uint32_t      reassignLBALength = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    uint8_t* writeData    = M_NULLPTR;
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 2) != 0) ||
        ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0) ||
        ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    writeData = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(device->drive_info.deviceBlockSize, sizeof(uint8_t),
                                                                 device->os_info.minimumAlignment));
    if (writeData == M_NULLPTR)
    {
        return MEMORY_FAILURE;
    }
    if (scsiIoCtx->cdb[1] & BIT0) // check for the "long list" bit
    {
        reassignLBALength =
            M_BytesTo4ByteValue(scsiIoCtx->pdata[0], scsiIoCtx->pdata[1], scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
    }
    else
    {
        reassignLBALength = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
    }
    if (scsiIoCtx->cdb[1] & BIT1)
    {
        longLba = true;
        reassignLBALength =
            M_BytesTo4ByteValue(scsiIoCtx->pdata[0], scsiIoCtx->pdata[1], scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
        incrementAmount = 8; // long parameters are 8 bytes in size
    }
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10) // 48bit
    {
        extCommand = true;
    }
    for (iterator = 4; iterator < scsiIoCtx->dataLength && iterator < (reassignLBALength + 4);
         iterator += incrementAmount)
    {
        uint64_t currentLBA = UINT64_C(0);
        if (longLba)
        {
            currentLBA = M_BytesTo8ByteValue(scsiIoCtx->pdata[iterator + 0], scsiIoCtx->pdata[iterator + 1],
                                             scsiIoCtx->pdata[iterator + 2], scsiIoCtx->pdata[iterator + 3],
                                             scsiIoCtx->pdata[iterator + 4], scsiIoCtx->pdata[iterator + 5],
                                             scsiIoCtx->pdata[iterator + 6], scsiIoCtx->pdata[iterator + 7]);
        }
        else
        {
            currentLBA = M_BytesTo4ByteValue(scsiIoCtx->pdata[iterator + 0], scsiIoCtx->pdata[iterator + 1],
                                             scsiIoCtx->pdata[iterator + 2], scsiIoCtx->pdata[iterator + 3]);
        }
        if (currentLBA > device->drive_info.deviceMaxLba)
        {
            ret = FAILURE;
            // set LBA out of range
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x21,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR,
                                           0);
            break;
        }
        if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, currentLBA))
        {
            // issue a write command
            if (SUCCESS != ata_Write(device, currentLBA, false, writeData, device->drive_info.deviceBlockSize))
            {
                ret = FAILURE;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR,
                                               0x11, 0x04, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                break;
            }
            else
            {
                if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, currentLBA))
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x11, 0x04,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    ret = FAILURE;
                    break;
                }
            }
        }
    }
    safe_free_aligned(&writeData);
    return ret;
}

static eReturnValues translate_SCSI_Security_Protocol_In_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                      = SUCCESS;
    uint8_t       securityProtocol         = scsiIoCtx->cdb[1];
    uint16_t      securityProtocolSpecific = M_BytesTo2ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
    uint32_t      allocationLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    bool inc512 = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 4) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[4], 6, 0) != 0) ||
        ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) || ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00, 0x00,
                                   device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    if (scsiIoCtx->pdata)
    {
        safe_memset(scsiIoCtx->pdata, scsiIoCtx->dataLength, 0, scsiIoCtx->dataLength);
    }
    if (scsiIoCtx->cdb[4] & BIT7)
    {
        inc512 = true;
        if (allocationLength > 0xFFFF)
        {
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(6);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
    }
    else
    {
        if (allocationLength > 0x01FFFE00)
        {
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(6);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
    }

    if (securityProtocol == 0xEF) // ATA Security
    {
        if (inc512)
        {
            // error
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR,
                                           0);
            return FAILURE;
        }
        else
        {
            // return ata security information
            DECLARE_ZERO_INIT_ARRAY(uint8_t, ataSecurityInformation, 16);
            ataSecurityInformation[0] = RESERVED;
            ataSecurityInformation[1] = 0x0E; // parameter list length
            // security erase time
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word089)))
            {
                ataSecurityInformation[2] =
                    M_Byte1(le16_to_host(device->drive_info.IdentifyData.ata.Word089) & 0x7FFF); // remove bit 15
                ataSecurityInformation[3] = M_Byte0(device->drive_info.IdentifyData.ata.Word089);
            }
            // enhanced security erase time
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word090)))
            {
                ataSecurityInformation[4] =
                    M_Byte1(le16_to_host(device->drive_info.IdentifyData.ata.Word090) & 0x7FFF); // remove bit 15
                ataSecurityInformation[5] = M_Byte0(device->drive_info.IdentifyData.ata.Word090);
            }
            // master password identifier
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word092)))
            {
                ataSecurityInformation[6] = M_Byte1(device->drive_info.IdentifyData.ata.Word092);
                ataSecurityInformation[7] = M_Byte0(device->drive_info.IdentifyData.ata.Word092);
            }
            // maxset bit
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word128)))
            {
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word128) & BIT8)
                {
                    ataSecurityInformation[8] |= BIT0;
                }
                // enhanced security erase supported bit
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word128) & BIT5)
                {
                    ataSecurityInformation[9] |= BIT5;
                }
                // password attempt counter exceeded bit
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word128) & BIT4)
                {
                    ataSecurityInformation[9] |= BIT4;
                }
                // frozen bit
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word128) & BIT3)
                {
                    ataSecurityInformation[9] |= BIT3;
                }
                // locked bit
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word128) & BIT2)
                {
                    ataSecurityInformation[9] |= BIT2;
                }
            }
            // security enabled bit
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT1)
            {
                ataSecurityInformation[9] |= BIT1;
            }
            // security supported bit
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT1)
            {
                ataSecurityInformation[9] |= BIT0;
            }
            if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, ataSecurityInformation,
                            M_Min(16, scsiIoCtx->dataLength));
            }
        }
    }
    else
    {
        if (allocationLength == 0)
        {
            if (SUCCESS != ata_Trusted_Non_Data(device, securityProtocol, true, securityProtocolSpecific))
            {
                // allow it to pass in this case without an error so that the protocols can be read and ATA security
                // will be available - TJE
                if (securityProtocol != 0 && securityProtocolSpecific != 0)
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
            }
        }
        else
        {
            if (inc512)
            {
                if (SUCCESS != ata_Trusted_Receive(device, device->drive_info.ata_Options.dmaSupported,
                                                   securityProtocol, securityProtocolSpecific, scsiIoCtx->pdata,
                                                   scsiIoCtx->dataLength))
                {
                    // command failed, but we still want to add ATA Security to the protocol list - TJE
                    if (securityProtocol == 0 && securityProtocolSpecific == 0 && scsiIoCtx->pdata)
                    {
                        uint16_t listLength = UINT16_C(0);
                        // add security protocol EF to the list
                        scsiIoCtx->pdata[8 + listLength] = 0xEF;
                        // now reset the list length
                        listLength += 1;
                        scsiIoCtx->pdata[6] = M_Byte1(listLength);
                        scsiIoCtx->pdata[7] = M_Byte0(listLength);
                    }
                    else
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                }
                else
                {
                    if (securityProtocol == 0 && securityProtocolSpecific == 0 && scsiIoCtx->pdata)
                    {
                        uint16_t listLength = M_BytesTo2ByteValue(
                            scsiIoCtx->pdata[7], scsiIoCtx->pdata[6]); // ATA reports this as little endian - TJE
                        // add security protocol EF to the list
                        scsiIoCtx->pdata[8 + listLength] = 0xEF;
                        // now reset the list length
                        listLength += 1;
                        scsiIoCtx->pdata[6] = M_Byte1(listLength);
                        scsiIoCtx->pdata[7] = M_Byte0(listLength);
                    }
                    else if (securityProtocol == 0 && securityProtocolSpecific == 1 && scsiIoCtx->pdata)
                    {
                        // need to byte swap the certificates length
                        uint16_t certLength = M_BytesTo2ByteValue(
                            scsiIoCtx->pdata[3], scsiIoCtx->pdata[2]); // ATA reports this as little endian - TJE
                        scsiIoCtx->pdata[2] = M_Byte1(certLength);
                        scsiIoCtx->pdata[3] = M_Byte0(certLength);
                    }
                    else if (securityProtocol == 0 && securityProtocolSpecific == 2 && scsiIoCtx->pdata)
                    {
                        uint16_t reservedBytes;
                        uint32_t descriptorLength = UINT32_C(0); // will be changed within the loop
                        // need to byte swap the length of compliance descriptors, then any compliance descriptors that
                        // the drive reports.
                        uint32_t lengthOfComplianceDescriptors =
                            M_BytesTo4ByteValue(scsiIoCtx->pdata[3], scsiIoCtx->pdata[2], scsiIoCtx->pdata[1],
                                                scsiIoCtx->pdata[0]); // ATA reports this as little endian - TJE
                        scsiIoCtx->pdata[0] = M_Byte3(lengthOfComplianceDescriptors);
                        scsiIoCtx->pdata[1] = M_Byte2(lengthOfComplianceDescriptors);
                        scsiIoCtx->pdata[2] = M_Byte1(lengthOfComplianceDescriptors);
                        scsiIoCtx->pdata[3] = M_Byte0(lengthOfComplianceDescriptors);
                        // now go through the compliance descriptors

                        for (uint32_t offset = UINT32_C(4);
                             offset < (lengthOfComplianceDescriptors + 4) && offset < scsiIoCtx->dataLength;
                             offset += descriptorLength + 8)
                        {
                            uint16_t descriptorType =
                                M_BytesTo2ByteValue(scsiIoCtx->pdata[offset + 1], scsiIoCtx->pdata[offset + 0]);
                            scsiIoCtx->pdata[offset + 1] = M_Byte0(descriptorType);
                            scsiIoCtx->pdata[offset + 0] = M_Byte1(descriptorType);
                            reservedBytes                = M_BytesTo2ByteValue(
                                scsiIoCtx->pdata[offset + 3],
                                scsiIoCtx->pdata[offset + 2]); // one table shows this as a word, another as bytes - TJE
                            scsiIoCtx->pdata[offset + 3] = M_Byte0(reservedBytes);
                            scsiIoCtx->pdata[offset + 2] = M_Byte1(reservedBytes);
                            descriptorLength =
                                M_BytesTo4ByteValue(scsiIoCtx->pdata[offset + 4], scsiIoCtx->pdata[offset + 5],
                                                    scsiIoCtx->pdata[offset + 6], scsiIoCtx->pdata[offset + 7]);
                            scsiIoCtx->pdata[offset + 4] = M_Byte3(reservedBytes);
                            scsiIoCtx->pdata[offset + 5] = M_Byte2(reservedBytes);
                            scsiIoCtx->pdata[offset + 6] = M_Byte1(reservedBytes);
                            scsiIoCtx->pdata[offset + 7] = M_Byte0(reservedBytes);
                            switch (descriptorType)
                            {
                            case 0x0001: // FIPS 140-2 & FIPS140-3
                                // Byte 8 is a single character and can be skipped
                                // Byte 9 is a single character and can be skipped
                                // Byte 10 - 15 are reserved bytes and can be skipped
                                // Bytes 16 - 143 are ATA string for hardware version...must be swapped...up to the end
                                // of the buffer! Don't go over! Bytes 144 - 271 are ATA string for version...must be
                                // swapped...up to the end of the buffer! Don't go over! Bytes 272 - 527 are ATA string
                                // for module name...must be swapped...up to the end of the buffer! Don't go over!
                                for (uint32_t swapOffset = offset + 16 /*16 is the start of the ATA strings*/;
                                     swapOffset < scsiIoCtx->dataLength &&
                                     swapOffset < (offset + descriptorLength + 8) /*8 byte header*/;
                                     swapOffset += 2 /*swap 2 bytes at a time*/)
                                {
                                    uint8_t tempByte                 = scsiIoCtx->pdata[swapOffset];
                                    scsiIoCtx->pdata[swapOffset]     = scsiIoCtx->pdata[swapOffset + 1];
                                    scsiIoCtx->pdata[swapOffset + 1] = tempByte;
                                }
                                break;
                            default: // unknown descriptor format. Don't touch it.
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                uint32_t paddedLength = ((allocationLength + 511) / 512) * LEGACY_DRIVE_SEC_SIZE;
                // allocate memory and pad data....then copy back the amount that was requested
                uint8_t* tempSecurityMemory = C_CAST(
                    uint8_t*, safe_calloc_aligned(paddedLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!tempSecurityMemory)
                {
                    return MEMORY_FAILURE;
                }
                if (SUCCESS != ata_Trusted_Receive(device, device->drive_info.ata_Options.dmaSupported,
                                                   securityProtocol, securityProtocolSpecific, tempSecurityMemory,
                                                   paddedLength))
                {
                    // command failed but we still want to add ata security to the protocol list! - TJE
                    if (securityProtocol == 0 && securityProtocolSpecific == 0 && scsiIoCtx->pdata)
                    {
                        uint16_t listLength = M_BytesTo2ByteValue(
                            scsiIoCtx->pdata[6],
                            scsiIoCtx
                                ->pdata[7]); // check that these bbytes are being interpretted in the correct order!
                        listLength += 1;
                        // add security protocol EF to the list
                        scsiIoCtx->pdata[8 + listLength] = 0xEF;
                        // now reset the list length
                        scsiIoCtx->pdata[6] = M_Byte1(listLength);
                        scsiIoCtx->pdata[7] = M_Byte0(listLength);
                    }
                    else
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                }
                else
                {
                    if (securityProtocol == 0 && securityProtocolSpecific == 0)
                    {
                        uint16_t listLength = M_BytesTo2ByteValue(tempSecurityMemory[7], tempSecurityMemory[6]);
                        listLength += 1;
                        // add security protocol EF to the list
                        tempSecurityMemory[8 + listLength] = 0xEF;
                        // now reset the list length
                        tempSecurityMemory[6] = M_Byte1(listLength);
                        tempSecurityMemory[7] = M_Byte0(listLength);
                    }
                    else if (securityProtocol == 0 && securityProtocolSpecific == 1)
                    {
                        // need to byte swap the certificates length
                        uint16_t certLength = M_BytesTo2ByteValue(
                            tempSecurityMemory[3], tempSecurityMemory[2]); // ATA reports this as little endian - TJE
                        tempSecurityMemory[2] = M_Byte1(certLength);
                        tempSecurityMemory[3] = M_Byte0(certLength);
                    }
                    else if (securityProtocol == 0 && securityProtocolSpecific == 2)
                    {
                        uint32_t descriptorLength = UINT32_C(0); // will be changed within the loop
                        // need to byte swap the length of compliance descriptors, then any compliance descriptors that
                        // the drive reports.
                        uint32_t lengthOfComplianceDescriptors =
                            M_BytesTo4ByteValue(tempSecurityMemory[3], tempSecurityMemory[2], tempSecurityMemory[1],
                                                tempSecurityMemory[0]); // ATA reports this as little endian - TJE
                        tempSecurityMemory[0] = M_Byte3(lengthOfComplianceDescriptors);
                        tempSecurityMemory[1] = M_Byte2(lengthOfComplianceDescriptors);
                        tempSecurityMemory[2] = M_Byte1(lengthOfComplianceDescriptors);
                        tempSecurityMemory[3] = M_Byte0(lengthOfComplianceDescriptors);
                        // now go through the compliance descriptors

                        for (uint32_t offset = UINT32_C(4);
                             offset < (lengthOfComplianceDescriptors + 4) && offset < paddedLength;
                             offset += descriptorLength + 8)
                        {
                            uint16_t reservedBytes;
                            uint16_t descriptorType =
                                M_BytesTo2ByteValue(tempSecurityMemory[offset + 1], tempSecurityMemory[offset + 0]);
                            tempSecurityMemory[offset + 1] = M_Byte0(descriptorType);
                            tempSecurityMemory[offset + 0] = M_Byte1(descriptorType);
                            reservedBytes =
                                M_BytesTo2ByteValue(tempSecurityMemory[offset + 3],
                                                    tempSecurityMemory[offset + 2]); // one table shows this as a word,
                                                                                     // another as bytes - TJE
                            tempSecurityMemory[offset + 3] = M_Byte0(reservedBytes);
                            tempSecurityMemory[offset + 2] = M_Byte1(reservedBytes);
                            descriptorLength =
                                M_BytesTo4ByteValue(tempSecurityMemory[offset + 4], tempSecurityMemory[offset + 5],
                                                    tempSecurityMemory[offset + 6], tempSecurityMemory[offset + 7]);
                            tempSecurityMemory[offset + 4] = M_Byte3(reservedBytes);
                            tempSecurityMemory[offset + 5] = M_Byte2(reservedBytes);
                            tempSecurityMemory[offset + 6] = M_Byte1(reservedBytes);
                            tempSecurityMemory[offset + 7] = M_Byte0(reservedBytes);
                            switch (descriptorType)
                            {
                            case 0x0001: // FIPS 140-2 & FIPS140-3
                                // Byte 8 is a single character and can be skipped
                                // Byte 9 is a single character and can be skipped
                                // Byte 10 - 15 are reserved bytes and can be skipped
                                // Bytes 16 - 143 are ATA string for hardware version...must be swapped...up to the end
                                // of the buffer! Don't go over! Bytes 144 - 271 are ATA string for version...must be
                                // swapped...up to the end of the buffer! Don't go over! Bytes 272 - 527 are ATA string
                                // for module name...must be swapped...up to the end of the buffer! Don't go over!
                                for (uint32_t swapOffset = offset + 16 /*16 is the start of the ATA strings*/;
                                     swapOffset < paddedLength &&
                                     swapOffset < (offset + descriptorLength + 8) /*8 byte header*/;
                                     swapOffset += 2 /*swap 2 bytes at a time*/)
                                {
                                    uint8_t tempByte                   = tempSecurityMemory[swapOffset];
                                    tempSecurityMemory[swapOffset]     = tempSecurityMemory[swapOffset + 1];
                                    tempSecurityMemory[swapOffset + 1] = tempByte;
                                }
                                break;
                            default: // unknown descriptor format. Don't touch it.
                                break;
                            }
                        }
                    }
                    if (scsiIoCtx->pdata)
                    {
                        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, tempSecurityMemory, allocationLength);
                    }
                }
                safe_free_aligned(&tempSecurityMemory);
            }
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Security_Protocol_Out_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                      = SUCCESS;
    uint8_t       securityProtocol         = scsiIoCtx->cdb[1];
    uint16_t      securityProtocolSpecific = M_BytesTo2ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
    uint32_t      transferLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    bool inc512 = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 4) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[4], 6, 0) != 0) ||
        ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) || ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00, 0x00,
                                   device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    if (scsiIoCtx->cdb[4] & BIT7)
    {
        inc512 = true;
        if (transferLength > 0xFFFF)
        {
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(6);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
    }
    else
    {
        if (transferLength > 0x01FFFE00)
        {
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(6);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
    }

    if (securityProtocol == 0xEF) // ATA Security
    {
        if (inc512 ||
            (transferLength < 34 && (securityProtocolSpecific != 0x0003 && securityProtocolSpecific != 0x0005)))
        {
            // error
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
        else // TODO: Check that ATA security is supported before issuing commands???
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, ataSecurityCommandBuffer,
                                    LEGACY_DRIVE_SEC_SIZE); // for use in ATA security commands that transfer data
            uint16_t* ataSecurityWordPtr = C_CAST(uint16_t*, &ataSecurityCommandBuffer[0]);
            switch (securityProtocolSpecific)
            {
            case 0x0001: // set password
                // master password capability
                if (scsiIoCtx->pdata[0] & BIT0)
                {
                    ataSecurityWordPtr[0] |= BIT8;
                }
                // set the master password identifier when necessary
                if (scsiIoCtx->pdata[1] & BIT0)
                {
                    ataSecurityWordPtr[17] = M_BytesTo2ByteValue(scsiIoCtx->pdata[34], scsiIoCtx->pdata[35]);
                }
                // set the password
                ataSecurityWordPtr[1]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
                ataSecurityWordPtr[2]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5]);
                ataSecurityWordPtr[3]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                ataSecurityWordPtr[4]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[8], scsiIoCtx->pdata[9]);
                ataSecurityWordPtr[5]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[10], scsiIoCtx->pdata[11]);
                ataSecurityWordPtr[6]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[12], scsiIoCtx->pdata[13]);
                ataSecurityWordPtr[7]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[14], scsiIoCtx->pdata[15]);
                ataSecurityWordPtr[8]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[16], scsiIoCtx->pdata[17]);
                ataSecurityWordPtr[9]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[18], scsiIoCtx->pdata[19]);
                ataSecurityWordPtr[10] = M_BytesTo2ByteValue(scsiIoCtx->pdata[20], scsiIoCtx->pdata[21]);
                ataSecurityWordPtr[11] = M_BytesTo2ByteValue(scsiIoCtx->pdata[22], scsiIoCtx->pdata[23]);
                ataSecurityWordPtr[12] = M_BytesTo2ByteValue(scsiIoCtx->pdata[24], scsiIoCtx->pdata[25]);
                ataSecurityWordPtr[13] = M_BytesTo2ByteValue(scsiIoCtx->pdata[26], scsiIoCtx->pdata[27]);
                ataSecurityWordPtr[14] = M_BytesTo2ByteValue(scsiIoCtx->pdata[28], scsiIoCtx->pdata[29]);
                ataSecurityWordPtr[15] = M_BytesTo2ByteValue(scsiIoCtx->pdata[30], scsiIoCtx->pdata[31]);
                ataSecurityWordPtr[16] = M_BytesTo2ByteValue(scsiIoCtx->pdata[32], scsiIoCtx->pdata[33]);
                if (SUCCESS != ata_Security_Set_Password(device, ataSecurityCommandBuffer))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                break;
            case 0x0002: // unlock
                // user or master password identifier bit
                if (scsiIoCtx->pdata[1] & BIT0)
                {
                    ataSecurityWordPtr[0] |= BIT0;
                }
                // set the password
                ataSecurityWordPtr[1]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
                ataSecurityWordPtr[2]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5]);
                ataSecurityWordPtr[3]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                ataSecurityWordPtr[4]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[8], scsiIoCtx->pdata[9]);
                ataSecurityWordPtr[5]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[10], scsiIoCtx->pdata[11]);
                ataSecurityWordPtr[6]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[12], scsiIoCtx->pdata[13]);
                ataSecurityWordPtr[7]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[14], scsiIoCtx->pdata[15]);
                ataSecurityWordPtr[8]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[16], scsiIoCtx->pdata[17]);
                ataSecurityWordPtr[9]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[18], scsiIoCtx->pdata[19]);
                ataSecurityWordPtr[10] = M_BytesTo2ByteValue(scsiIoCtx->pdata[20], scsiIoCtx->pdata[21]);
                ataSecurityWordPtr[11] = M_BytesTo2ByteValue(scsiIoCtx->pdata[22], scsiIoCtx->pdata[23]);
                ataSecurityWordPtr[12] = M_BytesTo2ByteValue(scsiIoCtx->pdata[24], scsiIoCtx->pdata[25]);
                ataSecurityWordPtr[13] = M_BytesTo2ByteValue(scsiIoCtx->pdata[26], scsiIoCtx->pdata[27]);
                ataSecurityWordPtr[14] = M_BytesTo2ByteValue(scsiIoCtx->pdata[28], scsiIoCtx->pdata[29]);
                ataSecurityWordPtr[15] = M_BytesTo2ByteValue(scsiIoCtx->pdata[30], scsiIoCtx->pdata[31]);
                ataSecurityWordPtr[16] = M_BytesTo2ByteValue(scsiIoCtx->pdata[32], scsiIoCtx->pdata[33]);
                if (SUCCESS != ata_Security_Unlock(device, ataSecurityCommandBuffer))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                break;
            case 0x0003: // erase prepare
                if (SUCCESS != ata_Security_Erase_Prepare(device))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                break;
            case 0x0004: // erase unit
                // enhanced erase bit
                if (scsiIoCtx->pdata[0] & BIT0)
                {
                    ataSecurityWordPtr[0] |= BIT1;
                }
                // user or master password identifier bit
                if (scsiIoCtx->pdata[1] & BIT0)
                {
                    ataSecurityWordPtr[0] |= BIT0;
                }
                // set the password
                ataSecurityWordPtr[1]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
                ataSecurityWordPtr[2]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5]);
                ataSecurityWordPtr[3]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                ataSecurityWordPtr[4]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[8], scsiIoCtx->pdata[9]);
                ataSecurityWordPtr[5]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[10], scsiIoCtx->pdata[11]);
                ataSecurityWordPtr[6]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[12], scsiIoCtx->pdata[13]);
                ataSecurityWordPtr[7]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[14], scsiIoCtx->pdata[15]);
                ataSecurityWordPtr[8]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[16], scsiIoCtx->pdata[17]);
                ataSecurityWordPtr[9]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[18], scsiIoCtx->pdata[19]);
                ataSecurityWordPtr[10] = M_BytesTo2ByteValue(scsiIoCtx->pdata[20], scsiIoCtx->pdata[21]);
                ataSecurityWordPtr[11] = M_BytesTo2ByteValue(scsiIoCtx->pdata[22], scsiIoCtx->pdata[23]);
                ataSecurityWordPtr[12] = M_BytesTo2ByteValue(scsiIoCtx->pdata[24], scsiIoCtx->pdata[25]);
                ataSecurityWordPtr[13] = M_BytesTo2ByteValue(scsiIoCtx->pdata[26], scsiIoCtx->pdata[27]);
                ataSecurityWordPtr[14] = M_BytesTo2ByteValue(scsiIoCtx->pdata[28], scsiIoCtx->pdata[29]);
                ataSecurityWordPtr[15] = M_BytesTo2ByteValue(scsiIoCtx->pdata[30], scsiIoCtx->pdata[31]);
                ataSecurityWordPtr[16] = M_BytesTo2ByteValue(scsiIoCtx->pdata[32], scsiIoCtx->pdata[33]);
                if (SUCCESS != ata_Security_Erase_Unit(
                                   device, ataSecurityCommandBuffer,
                                   scsiIoCtx->timeout)) // using the timeout from the SCSI Io Ctx to make it match what
                                                        // the caller expects. This may cause the command to return
                                                        // before complete and the OS will then recover by issuing a
                                                        // hard reset and locking the drive....CHANGE THIS LATER! - TJE
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                break;
            case 0x0005: // freeze lock
                if (SUCCESS != ata_Security_Freeze_Lock(device))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                break;
            case 0x0006: // disable password
                // user or master password identifier bit
                if (scsiIoCtx->pdata[1] & BIT0)
                {
                    ataSecurityWordPtr[0] |= BIT0;
                }
                // set the password
                ataSecurityWordPtr[1]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]);
                ataSecurityWordPtr[2]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5]);
                ataSecurityWordPtr[3]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                ataSecurityWordPtr[4]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[8], scsiIoCtx->pdata[9]);
                ataSecurityWordPtr[5]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[10], scsiIoCtx->pdata[11]);
                ataSecurityWordPtr[6]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[12], scsiIoCtx->pdata[13]);
                ataSecurityWordPtr[7]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[14], scsiIoCtx->pdata[15]);
                ataSecurityWordPtr[8]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[16], scsiIoCtx->pdata[17]);
                ataSecurityWordPtr[9]  = M_BytesTo2ByteValue(scsiIoCtx->pdata[18], scsiIoCtx->pdata[19]);
                ataSecurityWordPtr[10] = M_BytesTo2ByteValue(scsiIoCtx->pdata[20], scsiIoCtx->pdata[21]);
                ataSecurityWordPtr[11] = M_BytesTo2ByteValue(scsiIoCtx->pdata[22], scsiIoCtx->pdata[23]);
                ataSecurityWordPtr[12] = M_BytesTo2ByteValue(scsiIoCtx->pdata[24], scsiIoCtx->pdata[25]);
                ataSecurityWordPtr[13] = M_BytesTo2ByteValue(scsiIoCtx->pdata[26], scsiIoCtx->pdata[27]);
                ataSecurityWordPtr[14] = M_BytesTo2ByteValue(scsiIoCtx->pdata[28], scsiIoCtx->pdata[29]);
                ataSecurityWordPtr[15] = M_BytesTo2ByteValue(scsiIoCtx->pdata[30], scsiIoCtx->pdata[31]);
                ataSecurityWordPtr[16] = M_BytesTo2ByteValue(scsiIoCtx->pdata[32], scsiIoCtx->pdata[33]);
                if (SUCCESS != ata_Security_Disable_Password(device, ataSecurityCommandBuffer))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                break;
            case 0x0000: // reserved
            default:
                bitPointer   = UINT8_C(7);
                fieldPointer = UINT16_C(2);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return FAILURE;
            }
        }
    }
    else
    {
        if (transferLength == 0)
        {
            if (SUCCESS != ata_Trusted_Non_Data(device, securityProtocol, false, securityProtocolSpecific))
            {
                ret = FAILURE;
                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                        scsiIoCtx->senseDataSize);
            }
        }
        else
        {
            if (inc512)
            {
                if (SUCCESS != ata_Trusted_Send(device, device->drive_info.ata_Options.dmaSupported, securityProtocol,
                                                securityProtocolSpecific, scsiIoCtx->pdata, scsiIoCtx->dataLength))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
            }
            else
            {
                uint32_t paddedLength = ((transferLength + 511) / 512);
                // allocate memory and pad data....then copy back the amount that was requested
                uint8_t* tempSecurityMemory = C_CAST(
                    uint8_t*, safe_calloc_aligned(paddedLength, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!tempSecurityMemory)
                {
                    return MEMORY_FAILURE;
                }
                safe_memcpy(tempSecurityMemory, paddedLength, scsiIoCtx->pdata, transferLength);
                if (SUCCESS != ata_Trusted_Send(device, device->drive_info.ata_Options.dmaSupported, securityProtocol,
                                                securityProtocolSpecific, tempSecurityMemory, paddedLength))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                safe_free_aligned(&tempSecurityMemory);
            }
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Write_Long(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_LONG_10_CMD &&
        (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 4, 0) != 0) ||
         ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0)))
    {
        // invalid field in CDB
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_LONG_16_CMD &&
             (((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0) ||
              ((fieldPointer = 11) != 0 && scsiIoCtx->cdb[11] != 0) ||
              ((fieldPointer = 14) != 0 && scsiIoCtx->cdb[14] != 0)))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
        &&
        ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
          le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT2) ||
         (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
          le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT2)))
    {
        bool     correctionDisabled      = false;
        bool     writeUncorrectableError = false;
        bool     physicalBlock           = false;
        uint64_t lba                     = UINT64_C(0);
        uint16_t byteTransferLength      = UINT16_C(0);
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case WRITE_LONG_10_CMD:
            if (scsiIoCtx->cdb[1] & BIT7)
            {
                correctionDisabled = true;
            }
            if (scsiIoCtx->cdb[1] & BIT6)
            {
                writeUncorrectableError = true;
            }
            if (scsiIoCtx->cdb[1] & BIT5)
            {
                physicalBlock = true;
            }
            lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            byteTransferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            break;
        case WRITE_LONG_16_CMD: // skipping checking service action as it should already be checked by now.-TJE
            if (scsiIoCtx->cdb[1] & BIT7)
            {
                correctionDisabled = true;
            }
            if (scsiIoCtx->cdb[1] & BIT6)
            {
                writeUncorrectableError = true;
            }
            if (scsiIoCtx->cdb[1] & BIT5)
            {
                physicalBlock = true;
            }
            lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                      scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            byteTransferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
            break;
        default:
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(0);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return NOT_SUPPORTED;
        }
        if (byteTransferLength == 0)
        {
            if (writeUncorrectableError)
            {
                if (correctionDisabled)
                {
                    if (SUCCESS != ata_Write_Uncorrectable(device, 0xAA, 1, lba))
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                }
                else
                {
                    if (physicalBlock)
                    {
                        if (SUCCESS != ata_Write_Uncorrectable(device, 0x55, 1, lba))
                        {
                            ret = FAILURE;
                            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                    scsiIoCtx->senseDataSize);
                        }
                    }
                    else
                    {
                        // check logical per physical blocks
                        uint8_t logPerPhys = M_Nibble0(device->drive_info.IdentifyData.ata.Word106);
                        if (logPerPhys == 0)
                        {
                            if (SUCCESS != ata_Write_Uncorrectable(device, 0x55, 1, lba))
                            {
                                ret = FAILURE;
                                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                        scsiIoCtx->senseDataSize);
                            }
                        }
                        else
                        {
                            bitPointer   = UINT8_C(5);
                            fieldPointer = UINT16_C(1);
                            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                            bitPointer, fieldPointer);
                            ret = NOT_SUPPORTED;
                            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize,
                                                           SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                                           device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                                           senseKeySpecificDescriptor, 1);
                        }
                    }
                }
            }
            else
            {
                // the write uncorrectable bit MUST be set!
                fieldPointer = UINT16_C(1);
                bitPointer   = UINT8_C(6);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
        }
        else
        {
            fieldPointer = UINT16_C(7);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
    }
    else
    {
        ret          = NOT_SUPPORTED;
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_SCSI_Sanitize_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED < 4
        ((fieldPointer = 1) != 0 && (bitPointer = 6) != 0 && scsiIoCtx->cdb[1] & BIT6) ||
#endif // SAT_SPEC_SUPPORTED
        ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0) ||
        ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0) ||
        ((fieldPointer = 4) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[4] != 0) ||
        ((fieldPointer = 5) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[5] != 0) ||
        ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[6] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word059)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT12)
    {
        uint8_t serviceAction = UINT8_C(0x1F) & scsiIoCtx->cdb[1];
        bool immediate = false; // this is ignored for now since there is no way to handle this without multi-threading
        bool znr       = false;
        bool ause      = false;
        uint16_t parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (scsiIoCtx->cdb[1] & BIT7)
        {
            immediate = true;
        }
        if (scsiIoCtx->cdb[1] & BIT6)
        {
            znr = true;
        }
        if (scsiIoCtx->cdb[1] & BIT5)
        {
            ause = true;
        }
        // begin validating the parameters
        switch (serviceAction)
        {
        case 0x01: // overwrite
            if (parameterListLength != 0x0008)
            {
                fieldPointer = UINT16_C(7);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else if (!scsiIoCtx->pdata) // if this pointer is invalid set sense data saying the cdb list is
                                        // invalid...which shouldn't ever happen, but just in case...
            {
                fieldPointer = UINT16_C(7);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = BAD_PARAMETER;
            }
            else
            {
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT13)
                {
                    // check the parameter data
                    bool     invert         = false;
                    uint8_t  numberOfPasses = scsiIoCtx->pdata[0] & 0x1F;
                    uint32_t pattern        = M_BytesTo4ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5],
                                                                  scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                    if (scsiIoCtx->pdata[0] & BIT7)
                    {
                        invert = true;
                    }
                    // validate the parameter data for the number of passes and pattern length according to SAT
                    if (((fieldPointer = 0) == 0 && (bitPointer = 6) != 0 && scsiIoCtx->pdata[0] & BIT6) ||
                        ((fieldPointer = 0) == 0 && (bitPointer = 5) != 0 && scsiIoCtx->pdata[0] & BIT5) ||
                        ((fieldPointer = 1) != 0 && (bitPointer = 0) != 0 && scsiIoCtx->pdata[1] != 0) ||
                        ((fieldPointer = 0) != 0 && (bitPointer = 4) != 0 &&
                         (numberOfPasses == 0 || numberOfPasses > 0x10)) ||
                        ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 &&
                         0x0004 != M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3])))
                    {
                        if (bitPointer == 0)
                        {
                            uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                            uint8_t counter         = UINT8_C(0);
                            while (reservedByteVal > 0 && counter < 8)
                            {
                                reservedByteVal >>= 1;
                                ++counter;
                            }
                            bitPointer = counter - 1; // because we should always get a count of at least 1 if here and
                                                      // bits are zero indexed
                        }
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (numberOfPasses == 0x10)
                    {
                        numberOfPasses =
                            0; // this needs to be set to zero to specify 16 passes as this is how ATA does it.
                    }
                    if (SUCCESS !=
                            ata_Sanitize_Overwrite_Erase(device, ause, invert, numberOfPasses, pattern, znr, false) &&
                        !immediate)
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                    else if (!immediate)
                    {
                        // poll until there is no longer a sanitize command in progress
                        while (device->drive_info.lastCommandRTFRs.secCntExt &
                               BIT6) // this should be bit 14 of the returned sector count register, but since we break
                                     // them up, I'm checking bit 6 - TJE
                        {
                            delay_Seconds(15);
                            if (SUCCESS != ata_Sanitize_Status(device, false))
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                ret = NOT_SUPPORTED;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    fieldPointer = UINT16_C(1);
                    bitPointer   = UINT8_C(4);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            break;
        case 0x02: // block erase
            if (parameterListLength != 0)
            {
                fieldPointer = UINT16_C(7);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else
            {
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT13)
                {
                    if (SUCCESS != ata_Sanitize_Block_Erase(device, ause, znr) && !immediate)
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                    else if (!immediate)
                    {
                        // poll until there is no longer a sanitize command in progress
                        while (device->drive_info.lastCommandRTFRs.secCntExt &
                               BIT6) // this should be bit 14 of the returned sector count register, but since we break
                                     // them up, I'm checking bit 6 - TJE
                        {
                            delay_Seconds(1);
                            if (SUCCESS != ata_Sanitize_Status(device, false))
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                ret = NOT_SUPPORTED;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    fieldPointer = UINT16_C(1);
                    bitPointer   = UINT8_C(4);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            break;
        case 0x03: // cryptographic erase
            if (parameterListLength != 0)
            {
                fieldPointer = UINT16_C(7);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else
            {
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT13)
                {
                    if (SUCCESS != ata_Sanitize_Crypto_Scramble(device, ause, znr) && !immediate)
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                    else if (!immediate)
                    {
                        // poll until there is no longer a sanitize command in progress
                        while (device->drive_info.lastCommandRTFRs.secCntExt &
                               BIT6) // this should be bit 14 of the returned sector count register, but since we break
                                     // them up, I'm checking bit 6 - TJE
                        {
                            delay_Seconds(1);
                            if (SUCCESS != ata_Sanitize_Status(device, false))
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                ret = NOT_SUPPORTED;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    fieldPointer = UINT16_C(1);
                    bitPointer   = UINT8_C(4);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            break;
        case 0x1F: // exit failure mode
            if (parameterListLength != 0)
            {
                fieldPointer = UINT16_C(7);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else
            {
                if (SUCCESS != ata_Sanitize_Status(device, true) && !immediate)
                {
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                    ret = NOT_SUPPORTED;
                }
                else if (!immediate)
                {
                    // poll until there is no longer a sanitize command in progress
                    while (device->drive_info.lastCommandRTFRs.secCntExt &
                           BIT6) // this should be bit 14 of the returned sector count register, but since we break them
                                 // up, I'm checking bit 6 - TJE
                    {
                        delay_Seconds(5); // this should be enough time in between even for an overwrite...not optimal
                                          // but it'll work.-TJE
                        if (SUCCESS != ata_Sanitize_Status(device, true))
                        {
                            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                    scsiIoCtx->senseDataSize);
                            ret = NOT_SUPPORTED;
                            break;
                        }
                    }
                }
            }
            break;
        default:
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
    }
    else // sanitize feature not supported.
    {
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_SCSI_Read_Buffer_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret  = SUCCESS;
    uint8_t       mode = UINT8_C(0x1F) & scsiIoCtx->cdb[1];
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 && SAT_4_ERROR_HISTORY_FEATURE
    // uint8_t modeSpecific = (scsiIoCtx->cdb[1] >> 5) & 0x07;
#endif // SAT_SPEC_SUPPORTED
    uint8_t  bufferID         = scsiIoCtx->cdb[2];
    uint32_t bufferOffset     = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    uint32_t allocationLength = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    switch (mode)
    {
    case 0x02: // data mode
        if (bufferID == 0)
        {
            if (bufferOffset == 0)
            {
                DECLARE_ZERO_INIT_ARRAY(uint8_t, readBufferData, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS !=
                    ata_Read_Buffer(device, readBufferData, device->drive_info.ata_Options.readBufferDMASupported))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                else
                {
                    if (allocationLength == 0)
                    {
                        if (scsiIoCtx->pdata && scsiIoCtx->dataLength == LEGACY_DRIVE_SEC_SIZE)
                        {
                            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readBufferData, LEGACY_DRIVE_SEC_SIZE);
                        }
                    }
                    else
                    {
                        if (scsiIoCtx->pdata)
                        {
                            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readBufferData,
                                        M_Min(allocationLength, LEGACY_DRIVE_SEC_SIZE));
                        }
                    }
                }
            }
            else
            {
                fieldPointer = UINT16_C(3);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
        }
        else
        {
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        break;
    case 0x03: // descriptor mode
        if (allocationLength < 4)
        {
            fieldPointer = UINT16_C(6);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        else
        {
            if (bufferID == 0)
            {
                DECLARE_ZERO_INIT_ARRAY(uint8_t, readBufferDescriptor, 4);
                readBufferDescriptor[0] = 0x09;
                readBufferDescriptor[1] = M_Byte2(LEGACY_DRIVE_SEC_SIZE);
                readBufferDescriptor[2] = M_Byte2(LEGACY_DRIVE_SEC_SIZE);
                readBufferDescriptor[3] = M_Byte2(LEGACY_DRIVE_SEC_SIZE);
                if (scsiIoCtx->pdata)
                {
                    safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readBufferDescriptor,
                                M_Min(4, allocationLength));
                }
            }
            else
            {
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
        }
        break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 && SAT_4_ERROR_HISTORY_FEATURE
    case 0x1C: // error history mode (optional)
        if (device->drive_info.ata_Options.generalPurposeLoggingSupported &&
            device->drive_info.softSATFlags.currentInternalStatusLogSupported)
        {
            if (bufferID <= 3)
            {
                DECLARE_ZERO_INIT_ARRAY(uint8_t, gplDirectory, ATA_LOG_PAGE_LEN_BYTES);
                // establish error history I_T nexus (do nothing in this software implementation)
                if (bufferID == 1 || bufferID == 3)
                {
                    // create current device internal status data
                    if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG, 0,
                                                    gplDirectory, ATA_LOG_PAGE_LEN_BYTES,
                                                    device->drive_info.ata_Options.readLogWriteLogDMASupported, 1))
                    {
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        break;
                    }
                    safe_memset(gplDirectory, ATA_LOG_PAGE_LEN_BYTES, 0, ATA_LOG_PAGE_LEN_BYTES);
                }
                // error history directory
                if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_DIRECTORY, 0, gplDirectory, ATA_LOG_PAGE_LEN_BYTES,
                                                device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                {
                    uint32_t currentStatusLogSize = get_ATA_Log_Size_From_Directory(
                        gplDirectory, ATA_LOG_PAGE_LEN_BYTES, ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG);
                    uint32_t savedStatusLogSize = get_ATA_Log_Size_From_Directory(
                        gplDirectory, ATA_LOG_PAGE_LEN_BYTES, ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG);
                    if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG, 0,
                                                    gplDirectory, ATA_LOG_PAGE_LEN_BYTES,
                                                    device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                    {
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    }
                    else
                    {
                        uint16_t area3          = M_BytesTo2ByteValue(gplDirectory[13], gplDirectory[12]);
                        uint8_t  savedAvailable = gplDirectory[382];
                        bool     currentID      = false;
                        bool     savedID        = false;
                        DECLARE_ZERO_INIT_ARRAY(uint8_t, errorHistoryData, ATA_LOG_PAGE_LEN_BYTES);
                        uint16_t offset = UINT16_C(32);
                        // set vendor ID to ATA
                        errorHistoryData[0] = 'A';
                        errorHistoryData[1] = 'T';
                        errorHistoryData[2] = 'A';
                        errorHistoryData[3] = ' ';
                        errorHistoryData[4] = ' ';
                        errorHistoryData[5] = ' ';
                        errorHistoryData[6] = ' ';
                        errorHistoryData[7] = ' ';
                        // version (vendor specific)
                        errorHistoryData[8] = 0;
                        // EHS retrieved = 00b, EHS source = 11b, CLR_SUP = 0
                        errorHistoryData[9] = BIT1 | BIT2;
                        // reserved bytes
                        errorHistoryData[10] = RESERVED;
                        errorHistoryData[11] = RESERVED;
                        errorHistoryData[12] = RESERVED;
                        errorHistoryData[13] = RESERVED;
                        errorHistoryData[14] = RESERVED;
                        errorHistoryData[15] = RESERVED;
                        errorHistoryData[16] = RESERVED;
                        errorHistoryData[17] = RESERVED;
                        errorHistoryData[18] = RESERVED;
                        errorHistoryData[19] = RESERVED;
                        errorHistoryData[20] = RESERVED;
                        errorHistoryData[21] = RESERVED;
                        errorHistoryData[22] = RESERVED;
                        errorHistoryData[23] = RESERVED;
                        errorHistoryData[24] = RESERVED;
                        errorHistoryData[25] = RESERVED;
                        errorHistoryData[26] = RESERVED;
                        errorHistoryData[27] = RESERVED;
                        errorHistoryData[28] = RESERVED;
                        errorHistoryData[29] = RESERVED;
                        // set directory length to 0, 1, or 2 entries based on some criteria
                        if (area3 == 0 && savedAvailable == 0)
                        {
                            // length set to zero
                            errorHistoryData[30] = 0x00;
                            errorHistoryData[31] = 0x00;
                        }
                        else if (area3 > 0 && savedAvailable == 0)
                        {
                            currentID = true;
                            // length set to 8
                            errorHistoryData[30] = 0x00;
                            errorHistoryData[31] = 0x08;
                        }
                        else if (area3 > 0 && savedAvailable == 1)
                        {
                            currentID = true;
                            savedID   = true;
                            // length set to 16
                            errorHistoryData[30] = 0x00;
                            errorHistoryData[31] = 0x10;
                        }
                        if (currentID)
                        {
                            // set up directory entry
                            errorHistoryData[offset + 0] = 0x10; // supported buffer ID
                            errorHistoryData[offset + 1] = 0x01; // buffer format (from SAT)
                            // buffer source
                            if (bufferID == 0x01 || bufferID == 0x03)
                            {
                                errorHistoryData[offset + 2] = 0x03;
                            }
                            else
                            {
                                errorHistoryData[offset + 2] = 0x04;
                            }
                            errorHistoryData[offset + 3] = RESERVED;
                            // set max length
                            errorHistoryData[offset + 4] = M_Byte3(currentStatusLogSize);
                            errorHistoryData[offset + 5] = M_Byte2(currentStatusLogSize);
                            errorHistoryData[offset + 6] = M_Byte1(currentStatusLogSize);
                            errorHistoryData[offset + 7] = M_Byte0(currentStatusLogSize);
                            offset += 8;
                        }
                        if (savedID)
                        {
                            // set up directory entry
                            errorHistoryData[offset + 0] = 0x11; // supported buffer ID
                            errorHistoryData[offset + 1] = 0x02; // buffer format (from SAT)
                            // buffer source
                            errorHistoryData[offset + 2] = 0x02;
                            errorHistoryData[offset + 3] = RESERVED;
                            // set max length
                            errorHistoryData[offset + 4] = M_Byte3(savedStatusLogSize);
                            errorHistoryData[offset + 5] = M_Byte2(savedStatusLogSize);
                            errorHistoryData[offset + 6] = M_Byte1(savedStatusLogSize);
                            errorHistoryData[offset + 7] = M_Byte0(savedStatusLogSize);
                            offset += 8;
                        }
                        if (scsiIoCtx->pdata)
                        {
                            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, errorHistoryData,
                                        M_Min(offset, allocationLength));
                        }
                    }
                }
                else
                {
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else if (bufferID >= 0x10 && bufferID <= 0xEF)
            {
                // return error history information
                // first make sure allocation length is a multiple of 512
                if (allocationLength % LEGACY_DRIVE_SEC_SIZE || bufferOffset % 512)
                {
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
                else
                {
                    // read the data (if the buffer ID matches what we set)
                    if (bufferID == 0x10) // current
                    {
                        if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG,
                                                        C_CAST(uint16_t, bufferOffset / LEGACY_DRIVE_SEC_SIZE),
                                                        scsiIoCtx->pdata, allocationLength,
                                                        device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                        {
                            ret = NOT_SUPPORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                        else if (bufferOffset == 0)
                        {
                            uint32_t IEEEOUIField = UINT32_C(0);
                            uint16_t dataArea1    = UINT16_C(0);
                            uint16_t dataArea2    = UINT16_C(0);
                            uint16_t dataArea3    = UINT16_C(0);
                            // We need to byte swap a few fields in the very first data sector because of ATA vs SCSI
                            // endianness. Bytes 0 - 3 are reserved on SCSI, so memset them to zero
                            safe_memset(&scsiIoCtx->pdata[0], scsiIoCtx->dataLength, 0, 4);
                            // Bytes 4 - 7 are the IEEE OUI, but this shows as a DWORD in ACS, so it needs swapping to
                            // SCSI endianness.
                            IEEEOUIField        = M_BytesTo4ByteValue(scsiIoCtx->pdata[7], scsiIoCtx->pdata[6],
                                                                      scsiIoCtx->pdata[5], scsiIoCtx->pdata[4]);
                            scsiIoCtx->pdata[4] = M_Byte3(IEEEOUIField);
                            scsiIoCtx->pdata[5] = M_Byte2(IEEEOUIField);
                            scsiIoCtx->pdata[6] = M_Byte1(IEEEOUIField);
                            scsiIoCtx->pdata[7] = M_Byte0(IEEEOUIField);
                            // Bytes 8 - 9 are data area 1 and are a WORD in ACS, so swap it.
                            dataArea1           = M_BytesTo2ByteValue(scsiIoCtx->pdata[9], scsiIoCtx->pdata[8]);
                            scsiIoCtx->pdata[8] = M_Byte1(dataArea1);
                            scsiIoCtx->pdata[9] = M_Byte0(dataArea1);
                            // Bytes 10 - 11 are data area 2 and are a WORD in ACS, so swap it.
                            dataArea2            = M_BytesTo2ByteValue(scsiIoCtx->pdata[11], scsiIoCtx->pdata[10]);
                            scsiIoCtx->pdata[10] = M_Byte1(dataArea2);
                            scsiIoCtx->pdata[11] = M_Byte0(dataArea2);
                            // Bytes 12 - 13 are data area 3 and are a WORD in ACS, so swap it.
                            dataArea3            = M_BytesTo2ByteValue(scsiIoCtx->pdata[13], scsiIoCtx->pdata[12]);
                            scsiIoCtx->pdata[12] = M_Byte1(dataArea3);
                            scsiIoCtx->pdata[13] = M_Byte0(dataArea3);
                            // Bytes 14 - 17 are mentioned only in SPC5, so we will memset them to zero to prevent
                            // issues with future devices.
                            safe_memset(&scsiIoCtx->pdata[14], scsiIoCtx->dataLength - 14, 0,
                                        369 /*this covers bytes 14 through 382*/); // memsetting all reserved fields to
                                                                                   // make sure this doesn't cause
                                                                                   // problems later. - TJE
                        }
                    }
                    else if (bufferID == 0x11) // saved
                    {
                        if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG,
                                                        C_CAST(uint16_t, bufferOffset / LEGACY_DRIVE_SEC_SIZE),
                                                        scsiIoCtx->pdata, allocationLength,
                                                        device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                        {
                            ret = NOT_SUPPORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                        else if (bufferOffset == 0)
                        {
                            uint32_t IEEEOUIField = UINT32_C(0);
                            uint16_t dataArea1    = UINT16_C(0);
                            uint16_t dataArea2    = UINT16_C(0);
                            uint16_t dataArea3    = UINT16_C(0);
                            // We need to byte swap a few fields in the very first data sector because of ATA vs SCSI
                            // endianness. Bytes 0 - 3 are reserved on SCSI, so memset them to zero
                            safe_memset(&scsiIoCtx->pdata[0], scsiIoCtx->dataLength, 0, 4);
                            // Bytes 4 - 7 are the IEEE OUI, but this shows as a DWORD in ACS, so it needs swapping to
                            // SCSI endianness.
                            IEEEOUIField        = M_BytesTo4ByteValue(scsiIoCtx->pdata[7], scsiIoCtx->pdata[6],
                                                                      scsiIoCtx->pdata[5], scsiIoCtx->pdata[4]);
                            scsiIoCtx->pdata[4] = M_Byte3(IEEEOUIField);
                            scsiIoCtx->pdata[5] = M_Byte2(IEEEOUIField);
                            scsiIoCtx->pdata[6] = M_Byte1(IEEEOUIField);
                            scsiIoCtx->pdata[7] = M_Byte0(IEEEOUIField);
                            // Bytes 8 - 9 are data area 1 and are a WORD in ACS, so swap it.
                            dataArea1           = M_BytesTo2ByteValue(scsiIoCtx->pdata[9], scsiIoCtx->pdata[8]);
                            scsiIoCtx->pdata[8] = M_Byte1(dataArea1);
                            scsiIoCtx->pdata[9] = M_Byte0(dataArea1);
                            // Bytes 10 - 11 are data area 2 and are a WORD in ACS, so swap it.
                            dataArea2            = M_BytesTo2ByteValue(scsiIoCtx->pdata[11], scsiIoCtx->pdata[10]);
                            scsiIoCtx->pdata[10] = M_Byte1(dataArea2);
                            scsiIoCtx->pdata[11] = M_Byte0(dataArea2);
                            // Bytes 12 - 13 are data area 3 and are a WORD in ACS, so swap it.
                            dataArea3            = M_BytesTo2ByteValue(scsiIoCtx->pdata[13], scsiIoCtx->pdata[12]);
                            scsiIoCtx->pdata[12] = M_Byte1(dataArea3);
                            scsiIoCtx->pdata[13] = M_Byte0(dataArea3);
                            // Bytes 14 - 17 are mentioned only in SPC5, so we will memset them to zero to prevent
                            // issues with future devices.
                            safe_memset(&scsiIoCtx->pdata[14], scsiIoCtx->dataLength, 0,
                                        369 /*this covers bytes 14 through 382*/); // memsetting all reserved fields to
                                                                                   // make sure this doesn't cause
                                                                                   // problems later. - TJE
                        }
                    }
                    else
                    {
                        // buffer ID not supported (set invalid field in CDB)
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(7);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
            }
            else if (bufferID == 0xFE || bufferID == 0xFF)
            {
                // FE shall clear the error history I_T nexus (Nothing to do here in this software implementation)
                // FF clear the error history I_T nexus as described (Nothing to do here in this software
                // implementation) FF release the current device internal status data using an unspecified method
                // (uhhh......no idea)
            }
            else
            {
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
            }
        }
        else
        {
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x2C,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        }
        break;
#endif // SAT_SPEC_SUPPORTED
    default:
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(5);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        break;
    }
    return ret;
}

static eReturnValues translate_SCSI_Send_Diagnostic_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                 = SUCCESS;
    uint8_t       selfTestCode        = (scsiIoCtx->cdb[1] >> 5) & 0x07;
    bool          selfTest            = false;
    uint16_t      parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[3], scsiIoCtx->cdb[4]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3)    // reserved
        || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1) // devoffline
        || ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0) // unitoffline
        || ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0)   // reserved
    )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[1] & BIT2)
    {
        selfTest = true;
    }
    if (parameterListLength == 0)
    {
        bool smartEnabled           = false;
        bool smartSelfTestSupported = false;
        // NOTE: On some old drives this is not a reliable enough check for smart self test.
        //       on these drives you must also issue the SMART read data command and check
        //       if self-test is reported as supported in the SMART data as well.-TJE
        if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT1) ||
            (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT1))
        {
            smartSelfTestSupported = true;
        }
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0)
        {
            smartEnabled = true;
        }
        if (selfTest)
        {
            if (smartEnabled && smartSelfTestSupported)
            {
                // short captive test (timeout set to 2 minutes - max allowed by spec)
                if (SUCCESS != ata_SMART_Offline(device, 0x81, 120))
                {
                    ret = FAILURE;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x3E, 0x03,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else // 3 read-verify commands
            {
                bool extCommand = false;
                if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                        le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
                {
                    extCommand = true;
                }
                if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, 0))
                {
                    ret = FAILURE;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x3E, 0x03,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
                else
                {
                    if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, device->drive_info.deviceMaxLba))
                    {
                        ret = FAILURE;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x3E, 0x03,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    }
                    else
                    {
                        uint64_t randomLba = UINT64_C(0);
                        seed_64(12432545);
                        randomLba = random_Range_64(0, device->drive_info.deviceMaxLba);
                        if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, randomLba))
                        {
                            ret = FAILURE;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x3E, 0x03,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                }
            }
        }
        else
        {
            if (smartSelfTestSupported)
            {
                if (smartEnabled)
                {
                    DECLARE_ZERO_INIT_ARRAY(uint8_t, smartReadData, LEGACY_DRIVE_SEC_SIZE);
                    uint16_t timeout = UINT16_C(15);
                    switch (selfTestCode)
                    {
                    case 0: // default self test
                        // return good status (not required to do anything according to the spec) - TJE
                        break;
                    case 1: // background short self test
                        ata_SMART_Offline(device, 0x01, timeout);
                        break;
                    case 2: // background extended self test
                        ata_SMART_Offline(device, 0x02, timeout);
                        break;
                    case 4: // abort background self test
                        if (SUCCESS != ata_SMART_Offline(device, 0x7F, timeout))
                        {
                            ret          = NOT_SUPPORTED;
                            bitPointer   = UINT8_C(7);
                            fieldPointer = UINT16_C(1);
                            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                            bitPointer, fieldPointer);
                            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize,
                                                           SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                                           device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                                           senseKeySpecificDescriptor, 1);
                        }
                        break;
                    case 5: // foreground short self test
                        timeout = 120;
                        if (SUCCESS != ata_SMART_Offline(device, 0x81, timeout))
                        {
                            ret = FAILURE;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x3E, 0x03,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                        break;
                    case 6: // foreground extended self test
                        // first get the timeout value from SMART Read Data command
                        if (SUCCESS == ata_SMART_Read_Data(device, smartReadData, LEGACY_DRIVE_SEC_SIZE))
                        {
                            timeout = smartReadData[373];
                            if (timeout == 0xFF)
                            {
                                timeout = M_BytesTo2ByteValue(smartReadData[376], smartReadData[375]);
                            }
                        }
                        else // this case shouldn't ever happen...
                        {
                            // set the timeout to max
                            timeout = UINT16_MAX;
                        }
                        if (SUCCESS != ata_SMART_Offline(device, 0x82, timeout))
                        {
                            ret = FAILURE;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_HARDWARE_ERROR, 0x3E, 0x03,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                        break;
                    case 7: // reserved
                    case 3: // reserved
                    default:
                        ret          = NOT_SUPPORTED;
                        bitPointer   = UINT8_C(7);
                        fieldPointer = UINT16_C(1);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        break;
                    }
                }
                else
                {
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x67, 0x0B,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else
            {
                bitPointer   = UINT8_C(7);
                fieldPointer = UINT16_C(1);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
        }
    }
    else
    {
        fieldPointer = UINT16_C(3);
        bitPointer   = UINT8_C(7);
        ret          = NOT_SUPPORTED;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_SCSI_Report_Luns_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, reportLunsData, 16);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    uint32_t allocationLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0) || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0) ||
        ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0) || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) ||
        ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    switch (scsiIoCtx->cdb[2])
    {
    case 0x00:
        // set list length to 16 bytes
        reportLunsData[0] = M_Byte3(16);
        reportLunsData[1] = M_Byte2(16);
        reportLunsData[2] = M_Byte1(16);
        reportLunsData[3] = M_Byte0(16);
        // set lun to zero since it's zero indexed
        reportLunsData[15] = 0;
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, reportLunsData, M_Min(16, allocationLength));
        }
        break;
    case 0x01:
    case 0x02:
    case 0x10:
    case 0x11:
    case 0x12:
        // nothing to report, so just copy back the data buffer as it is
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, reportLunsData, M_Min(16, allocationLength));
        }
        break;
    default:
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x25, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        break;
    }
    return ret;
}

static eReturnValues translate_SCSI_Request_Sense_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret       = SUCCESS;
    uint8_t       powerMode = UINT8_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseData, SPC3_SENSE_LEN);
    bool descriptorFormat = false;
    bool checkSMARTStatus = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 1) != 0) ||
        ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[1] & BIT0)
    {
        descriptorFormat = true;
    }
    ret = ata_Check_Power_Mode(device, &powerMode);
    if (SUCCESS != ret && is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word059)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT12)
    {
        ret = SUCCESS;
        // check sanitize status
        if (SUCCESS == ata_Sanitize_Status(device, false))
        {
            // check rtfrs for error and/or progress (if error, need to return error)
            if (device->drive_info.lastCommandRTFRs.secCntExt & BIT7)
            {
                // sanitize error
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03,
                                               descriptorFormat, M_NULLPTR, 0);
            }
            else if (device->drive_info.lastCommandRTFRs.secCntExt & BIT6)
            {
                // set progress
                set_Sense_Key_Specific_Descriptor_Progress_Indicator(
                    senseKeySpecificDescriptor, M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaMid,
                                                                    device->drive_info.lastCommandRTFRs.lbaLow));
                // encode progress into the sense key specific descriptor
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NOT_READY, 0x04, 0x1B,
                                               descriptorFormat, senseKeySpecificDescriptor, 1);
            }
            else
            {
                checkSMARTStatus = true;
            }
        }
        else
        {
            checkSMARTStatus = true;
        }
    }
    else if (SUCCESS != ret)
    {
        ret              = SUCCESS;
        checkSMARTStatus = true;
    }
    else
    {
        bool sanitizeInProgress = false;
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word059)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT12 &&
            SUCCESS == ata_Sanitize_Status(device, false))
        {
            if (device->drive_info.lastCommandRTFRs.secCntExt & BIT6)
            {
                sanitizeInProgress = true;
            }
        }
        if (sanitizeInProgress)
        {
            // set sanitize progress
            set_Sense_Key_Specific_Descriptor_Progress_Indicator(
                senseKeySpecificDescriptor, M_BytesTo2ByteValue(device->drive_info.lastCommandRTFRs.lbaMid,
                                                                device->drive_info.lastCommandRTFRs.lbaLow));
            // encode progress into the sense key specific descriptor
            set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NOT_READY, 0x04, 0x1B,
                                           descriptorFormat, senseKeySpecificDescriptor, 1);
        }
        else
        {
            bool checkDST = false;
            // check the value of the power mode (TODO: indicate by command vs by timer)
            switch (powerMode)
            {
            case 0x00: // standby_z
            case 0x01: // standby_y
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0x5E, 0x43,
                                               descriptorFormat, M_NULLPTR, 0);
                break;
            case 0x80: // idle
            case 0x81: // idle_a
            case 0x82: // idle_b
            case 0x83: // idle_c
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0x5E, 0x42,
                                               descriptorFormat, M_NULLPTR, 0);
                break;
            case 0xFF: // active
            default:
                checkDST = true; // only set this here because the drive will only be active when DST is running. It
                                 // won't be in standby or idle conditions.
                // all is well
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0, 0,
                                               descriptorFormat, M_NULLPTR, 0);
                break;
            }
            if (checkDST &&
                (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                 le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0) &&
                (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                     le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
                 le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT1))
            {
                // read SMART data and check for DST in progress.
                DECLARE_ZERO_INIT_ARRAY(uint8_t, smartData, 512);
                if (SUCCESS == ata_SMART_Read_Data(device, smartData, 512))
                {
                    if (M_Nibble1(smartData[363]) == 0x0F)
                    {
                        // in progress, setup a progress descriptor
                        uint16_t dstProgress =
                            UINT16_C(656) *
                            M_Nibble0(smartData[363]); // This comes out very close to the actual percent...close
                                                       // enough anyways. There is probably a way to scale it to
                                                       // more even round numbers but this will do fine.
                        set_Sense_Key_Specific_Descriptor_Progress_Indicator(senseKeySpecificDescriptor, dstProgress);
                        set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NOT_READY, 0x04, 0x09,
                                                       descriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
            }
        }
    }
    if (checkSMARTStatus)
    {
        if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0) &&
            SUCCESS == ata_SMART_Return_Status(device))
        {
            if (device->drive_info.lastCommandRTFRs.lbaMid == 0xF4 && device->drive_info.lastCommandRTFRs.lbaHi == 0x2C)
            {
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0x5D, 0x10,
                                               descriptorFormat, M_NULLPTR, 0);
            }
            else
            {
                // call it good status
                set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0, 0,
                                               descriptorFormat, M_NULLPTR, 0);
            }
        }
        else
        {
            // set some sort of generic/unknown failure
            set_Sense_Data_For_Translation(
                &senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NOT_READY, 0x08, 0x00, descriptorFormat, M_NULLPTR,
                0); // setting logical unit communication failure since that sounds about like what happened-TJE
        }
    }
    // copy back whatever data we set
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, senseData, M_Min(scsiIoCtx->cdb[4], SPC3_SENSE_LEN));
    }
    return ret;
}

static eReturnValues translate_SCSI_Write_Buffer_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                 = SUCCESS;
    uint8_t       mode                = scsiIoCtx->cdb[1] & 0x1F;
    uint8_t       modeSpecific        = (scsiIoCtx->cdb[1] >> 5) & 0x07;
    uint8_t       bufferID            = scsiIoCtx->cdb[2];
    uint32_t      bufferOffset        = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    uint32_t      parameterListLength = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    uint16_t      blockCount          = C_CAST(uint16_t, parameterListLength >> 9); // need bits 23:9
    uint16_t      offset              = C_CAST(uint16_t, bufferOffset >> 9);        // need bits 23:9
    bool          downloadCommandSupported = false;
    bool          downloadMode3Supported   = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT0) ||
        (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT0))
    {
        downloadCommandSupported = true;
    }
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
        &&
        ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
          le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT4) ||
         (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
          le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT4)))
    {
        downloadMode3Supported = true;
    }
    switch (mode)
    {
    case 0x02: // Write buffer command
        if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 &&
             modeSpecific == 0) // mode specific field is reserved in this command, so make sure it's zero!
            && ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && bufferID == 0) &&
            ((fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && bufferOffset == 0) &&
            ((fieldPointer = 6) != 0 && (bitPointer = 7) != 0 && parameterListLength == LEGACY_DRIVE_SEC_SIZE))
        {
            if (SUCCESS !=
                ata_Write_Buffer(device, scsiIoCtx->pdata, device->drive_info.ata_Options.writeBufferDMASupported))
            {
                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                        scsiIoCtx->senseDataSize);
            }
        }
        else
        {
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        break;
    case 0x05: // ATA Download Microcode
        if (downloadCommandSupported || device->drive_info.ata_Options.downloadMicrocodeDMASupported)
        {
            if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0) &&
                ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && bufferID == 0) &&
                ((fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && bufferOffset == 0) &&
                ((fieldPointer = 6) != 0 && (bitPointer = 7) != 0 && parameterListLength == 0))
            {
                if (SUCCESS != ata_Download_Microcode(device, ATA_DL_MICROCODE_SAVE_IMMEDIATE, blockCount, 0,
                                                      device->drive_info.ata_Options.downloadMicrocodeDMASupported,
                                                      scsiIoCtx->pdata, scsiIoCtx->dataLength,
                                                      scsiIoCtx->fwdlFirstSegment, scsiIoCtx->fwdlLastSegment,
                                                      scsiIoCtx->timeout))
                {
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                else
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_UNIT_ATTENTION, 0x3F, 0x01,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else // these fields are reserved or vendor specific so make sure they are zerod out
            {
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
        }
        else // download not supported, so mode not supported
        {
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        break;
    case 0x07: // ATA Download Microcode Segmented
        if ((downloadCommandSupported || device->drive_info.ata_Options.downloadMicrocodeDMASupported) &&
            downloadMode3Supported)
        {
            if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 &&
                 modeSpecific == 0) // mode specific is reserved in this mode
                && ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 &&
                    bufferID == 0) // buffer ID should be zero since we don't support other buffer IDs
            )
            {
                if ((bufferOffset & 0x1FF) == 0 && (parameterListLength & 0x1FF) == 0)
                {
                    // check min and max transfer sizes
                    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word234)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word234) > blockCount) ||
                        (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word235)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word235) < blockCount))
                    {
                        fieldPointer = UINT16_C(6);
                        bitPointer   = UINT8_C(7);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    else
                    {
                        if (SUCCESS !=
                            ata_Download_Microcode(device, ATA_DL_MICROCODE_OFFSETS_SAVE_IMMEDIATE, blockCount, offset,
                                                   device->drive_info.ata_Options.downloadMicrocodeDMASupported,
                                                   scsiIoCtx->pdata, scsiIoCtx->dataLength, scsiIoCtx->fwdlFirstSegment,
                                                   scsiIoCtx->fwdlLastSegment, scsiIoCtx->timeout))
                        {
                            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                    scsiIoCtx->senseDataSize);
                        }
                        else if (device->drive_info.lastCommandRTFRs.secCnt == 0x02)
                        {
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_UNIT_ATTENTION, 0x3F, 0x01,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                }
                else // invalid parameter list length! must be in 200h sizes only!
                {
                    fieldPointer = UINT16_C(6);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
            }
            else // these fields must be zeroed out
            {
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
        }
        else // mode not supported by drive, so we cannot support this command translation
        {
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        break;
    case 0x0D: // ATA Deferred Download
        // check mode specific for PO-ACT and HR-ACT bits
        if ((modeSpecific & BIT2) == 0 || modeSpecific & BIT1)
        {
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
        M_FALLTHROUGH;
    case 0x0E: // ATA Deferred Download
        if ((downloadCommandSupported || device->drive_info.ata_Options.downloadMicrocodeDMASupported) &&
            device->drive_info.softSATFlags.deferredDownloadSupported)
        {
            if (((mode == 0x0E && ((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0)) ||
                 mode == 0x0D) // mode specific is reserved in this mode (0x0E)
                && ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 &&
                    bufferID == 0) // buffer ID should be zero since we don't support other buffer IDs
            )
            {
                if ((bufferOffset & 0x1FF) == 0 && (parameterListLength & 0x1FF) == 0)
                {
                    // check minimum and maximum transfer size
                    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word234)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word234) > blockCount) ||
                        (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word235)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word235) < blockCount))
                    {
                        fieldPointer = UINT16_C(6);
                        bitPointer   = UINT8_C(7);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    else
                    {
                        if (SUCCESS !=
                            ata_Download_Microcode(device, ATA_DL_MICROCODE_OFFSETS_SAVE_FUTURE, blockCount, offset,
                                                   device->drive_info.ata_Options.downloadMicrocodeDMASupported,
                                                   scsiIoCtx->pdata, scsiIoCtx->dataLength, scsiIoCtx->fwdlFirstSegment,
                                                   scsiIoCtx->fwdlLastSegment, scsiIoCtx->timeout))
                        {
                            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                    scsiIoCtx->senseDataSize);
                        }
                        else if (device->drive_info.lastCommandRTFRs.secCnt == 0x03)
                        {
                            // need to save that microcode is ready to be activated which can be reported somewhere
                            // else. This is also needed within Sanitize to create an error condition that the code
                            // needs to be activated first.
                        }
                    }
                }
                else // invalid parameter list length! must be in 200h sizes only!
                {
                    fieldPointer = UINT16_C(6);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
            }
            else // these fields must be zeroed out
            {
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
        }
        else // mode not supported by drive
        {
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        break;
    case 0x0F: // ATA Activate Deferred Microcode
        if ((downloadCommandSupported || device->drive_info.ata_Options.downloadMicrocodeDMASupported) &&
            device->drive_info.softSATFlags.deferredDownloadSupported)
        {
            if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0) &&
                ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && bufferID == 0) &&
                ((fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && bufferOffset == 0) &&
                ((fieldPointer = 6) != 0 && (bitPointer = 7) != 0 && parameterListLength == 0))
            {
                if (SUCCESS != ata_Download_Microcode(device, ATA_DL_MICROCODE_ACTIVATE, blockCount, offset,
                                                      device->drive_info.ata_Options.downloadMicrocodeDMASupported,
                                                      scsiIoCtx->pdata, scsiIoCtx->dataLength,
                                                      scsiIoCtx->fwdlFirstSegment, scsiIoCtx->fwdlLastSegment,
                                                      scsiIoCtx->timeout))
                {
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
                else if (device->drive_info.lastCommandRTFRs.secCnt == 0x02)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_UNIT_ATTENTION, 0x3F, 0x01,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else
            {
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
        }
        else // mode not supported by drive
        {
            fieldPointer = UINT16_C(1);
            bitPointer   = UINT8_C(4);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        break;
    default: // unknown or unsupported mode
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(4);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        break;
    }
    return ret;
}

static eReturnValues translate_SCSI_Start_Stop_Unit_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                    = SUCCESS;
    bool          immediate              = false;
    uint8_t       powerConditionModifier = M_Nibble0(scsiIoCtx->cdb[3]);
    uint8_t       powerCondition         = M_Nibble1(scsiIoCtx->cdb[4]);
    bool          noFlush                = false;
    bool          loej                   = false;
    bool          start                  = false;
    bool          flushCacheExt          = false;
    bool          enable                 = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, powerConditionsLog, LEGACY_DRIVE_SEC_SIZE);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 1) != 0) ||
        ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) ||
        ((fieldPointer = 3) != 0 && M_Nibble1(scsiIoCtx->cdb[3]) != 0) ||
        ((fieldPointer = 4) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[4] & BIT3))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[1] & BIT0)
    {
        immediate = true;
    }
    if (scsiIoCtx->cdb[4] & BIT2)
    {
        noFlush = true;
    }
    if (scsiIoCtx->cdb[4] & BIT1)
    {
        loej = true;
    }
    if (scsiIoCtx->cdb[4] & BIT0)
    {
        start = true;
    }

    // save some information about the flush cache commands supported
    if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT13) // ext command
    {
        flushCacheExt = true;
    }

    // if EPC is supported, we do one thing....otherwise we do something else
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
        && (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT7))
    {
        // EPC drive
        switch (powerCondition)
        {
        case 0x00: // start valid
            // process start and loej bits
            if (start)
            {
                if (loej)
                {
                    fieldPointer = UINT16_C(4);
                    bitPointer   = UINT8_C(1);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                else
                {
                    // ata verify command
                    uint64_t randomLba  = UINT64_C(0);
                    bool     extCommand = false;
                    if (le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
                    {
                        extCommand = true;
                    }
                    seed_64(12432545);
                    randomLba = random_Range_64(0, device->drive_info.deviceMaxLba);
                    if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, randomLba) &&
                        !immediate) // uhh....the spec  doens't mention what happens if this command fails...I'll return
                                    // an error - TJE
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                    }
                }
            }
            else
            {
                if (loej)
                {
                    if (le16_to_host(device->drive_info.IdentifyData.ata.Word000) & BIT7)
                    {
                        // send a media eject command
                        if (SUCCESS != ata_Media_Eject(device))
                        {
                            ret = NOT_SUPPORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x53, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                    else
                    {
                        fieldPointer = UINT16_C(4);
                        bitPointer   = UINT8_C(1);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
                else
                {
                    if (noFlush)
                    {
                        // send flush command
                        if (SUCCESS != ata_Flush_Cache(device, flushCacheExt))
                        {
                            // if flush failed, send standby immediate with lba = 0
                            if (SUCCESS != ata_Standby_Immediate(device))
                            {
                                // if error set aborted command - command sequence error
                                ret = ABORTED;
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                            }
                        }
                    }
                    else
                    {
                        // send standby immediate with lba = 0
                        if (SUCCESS != ata_Standby_Immediate(device))
                        {
                            // if error set aborted command - command sequence error
                            ret = ABORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                }
            }
            break;
        case 0x01: // active
            if (SUCCESS != ata_EPC_Set_Power_Condition_State(device, 0xFF, false, false) && !immediate)
            {
                ret = ABORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
            }
            else
            {
                // ata verify command
                uint64_t randomLba  = UINT64_C(0);
                bool     extCommand = false;
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
                {
                    extCommand = true;
                }
                seed_64(1985733);
                randomLba = random_Range_64(0, device->drive_info.deviceMaxLba);
                if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, randomLba) && !immediate)
                {
                    ret = ABORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
                else
                {
                    // no longer in stopped state
                }
            }
            break;
        case 0x0A: // Force Idle (check power condition modifier for specific idle state)
            enable = true;
            M_FALLTHROUGH;
        case 0x02: // Idle (check power condition modifier for specific idle state)
            // first read the power conditions log....yes, do this first as that's what the SAT spec says. We could
            // probably cache this data, but we aren't doing that today
            if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_POWER_CONDITIONS, 0, powerConditionsLog,
                                            LEGACY_DRIVE_SEC_SIZE,
                                            device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
            {
                ataPowerConditionsDescriptor* descriptor = M_NULLPTR;
                switch (powerConditionModifier)
                {
                case 0: // idle a
                    descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[0]);
                    break;
                case 1: // idle b
                    descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[64]);
                    break;
                case 2: // idle c
                    descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[128]);
                    break;
                default: // unsupported modifier
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(3);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                    break;
                }
                if (descriptor != M_NULLPTR)
                {
                    if (descriptor->powerConditionFlags & BIT7)
                    {
                        if (!noFlush)
                        {
                            // send a flush command
                            if (SUCCESS != ata_Flush_Cache(device, flushCacheExt) && !immediate)
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                return ABORTED;
                            }
                        }
                        if (SUCCESS != ata_EPC_Set_Power_Condition_State(device, 0xFF, enable, false))
                        {
                            if (!immediate)
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                return ABORTED;
                            }
                            else
                            {
                                return SUCCESS; // should save this error condition to report in the request sense
                                                // command later. - TJE
                            }
                        }
                        if (SUCCESS !=
                                ata_EPC_Go_To_Power_Condition(device, powerConditionModifier + 0x81, false, false) &&
                            !immediate)
                        {
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                            ret = ABORTED;
                        }
                    }
                    else // drive doesn't support this power condition, so modifier is invalid
                    {
                        fieldPointer = UINT16_C(3);
                        bitPointer   = UINT8_C(3);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        ret = NOT_SUPPORTED;
                    }
                }
            }
            else
            {
                // command sequence error
                ret = ABORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
            }
            break;
        case 0x0B: // Force Standby (check power condition modifier for specific idle state)
            enable = true;
            M_FALLTHROUGH;
        case 0x03: // standby (check power condition modifier for specific idle state)
            // first read the power conditions log....yes, do this first as that's what the SAT spec says. We could
            // probably cache this data, but we aren't doing that today
            if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_POWER_CONDITIONS, 1, powerConditionsLog,
                                            LEGACY_DRIVE_SEC_SIZE,
                                            device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
            {
                ataPowerConditionsDescriptor* descriptor = M_NULLPTR;
                switch (powerConditionModifier)
                {
                case 0: // standby_z
                    descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[448]);
                    break;
                case 1: // standby_y
                    descriptor = C_CAST(ataPowerConditionsDescriptor*, &powerConditionsLog[384]);
                    break;
                default: // unsupported modifier
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(3);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                    break;
                }
                if (descriptor != M_NULLPTR)
                {
                    if (descriptor->powerConditionFlags & BIT7)
                    {
                        if (!noFlush)
                        {
                            // send a flush command
                            if (SUCCESS != ata_Flush_Cache(device, flushCacheExt) && !immediate)
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                return ABORTED;
                            }
                        }
                        if (SUCCESS != ata_EPC_Set_Power_Condition_State(device, 0xFF, enable, false))
                        {
                            if (!immediate)
                            {
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                                return ABORTED;
                            }
                            else
                            {
                                return SUCCESS; // should save this error condition to report in the request sense
                                                // command later. - TJE
                            }
                        }
                        if (SUCCESS !=
                                ata_EPC_Go_To_Power_Condition(device, powerConditionModifier + 0x01, false, false) &&
                            !immediate)
                        {
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                            ret = ABORTED;
                        }
                    }
                    else // power condition not supported by the drive, so modifier is bad
                    {
                        fieldPointer = UINT16_C(3);
                        bitPointer   = UINT8_C(3);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        ret = NOT_SUPPORTED;
                    }
                }
            }
            else
            {
                // command sequence error
                ret = ABORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
            }
            break;
        case 0x07: // LU Control
            if (powerConditionModifier == 0)
            {
                if (SUCCESS != ata_EPC_Set_Power_Condition_State(device, 0xFF, true, false) && !immediate)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    ret = ABORTED;
                }
            }
            else
            {
                fieldPointer = UINT16_C(3);
                bitPointer   = UINT8_C(3);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            break;
        default: // invalid power condition
            fieldPointer = UINT16_C(4);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
    }
    else
    {
        uint8_t powerMode                        = UINT8_C(0); // only used for LU_Control power condition
        bool    unload                           = false;
        bool    standbyTimersSpecifiedByStandard = false;
        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT13)
        {
            unload = true;
        }
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word049)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word049) & BIT13)
        {
            standbyTimersSpecifiedByStandard = true;
        }
        // Non-EPC drive
        switch (powerCondition)
        {
        case 0x00: // start valid
            // process start and loej bits
            if (start)
            {
                if (loej)
                {
                    fieldPointer = UINT16_C(4);
                    bitPointer   = UINT8_C(1);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                else
                {
                    // ata verify command
                    uint64_t randomLba  = UINT64_C(0);
                    bool     extCommand = false;
                    if (le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
                    {
                        extCommand = true;
                    }
                    seed_64(1985733);
                    randomLba = random_Range_64(0, device->drive_info.deviceMaxLba);
                    if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, randomLba) &&
                        !immediate) // uhh....the spec  doens't mention what happens if this command fails...I'll return
                                    // an error - TJE
                    {
                        ret = ABORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    }
                }
            }
            else
            {
                if (loej)
                {
                    if (le16_to_host(device->drive_info.IdentifyData.ata.Word000) & BIT7)
                    {
                        // send a media eject command
                        if (SUCCESS != ata_Media_Eject(device))
                        {
                            ret = NOT_SUPPORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x53, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                    else
                    {
                        fieldPointer = UINT16_C(4);
                        bitPointer   = UINT8_C(1);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
                else
                {
                    if (noFlush)
                    {
                        // send flush command
                        if (SUCCESS != ata_Flush_Cache(device, flushCacheExt))
                        {
                            // if flush failed, send standby immediate with lba = 0
                            if (SUCCESS != ata_Standby_Immediate(device))
                            {
                                // if error set aborted command - command sequence error
                                ret = ABORTED;
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                            }
                        }
                    }
                    else
                    {
                        // send standby immediate with lba = 0
                        if (SUCCESS != ata_Standby_Immediate(device))
                        {
                            // if error set aborted command - command sequence error
                            ret = ABORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                }
            }
            break;
        case 0x01: // active
            if (SUCCESS == ata_Idle(device, 0))
            {
                // ata verify command
                uint64_t randomLba  = UINT64_C(0);
                bool     extCommand = false;
                if (le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
                {
                    extCommand = true;
                }
                seed_64(189843);
                randomLba = random_Range_64(0, device->drive_info.deviceMaxLba);
                if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, randomLba) && !immediate)
                {
                    ret = ABORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
                else
                {
                    // no longer in stopped state
                }
            }
            else if (!immediate)
            {
                ret = ABORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
            }
            break;
        case 0x02: // Idle (check power condition modifier for specific idle state)
            if (!noFlush)
            {
                if (SUCCESS != ata_Flush_Cache(device, flushCacheExt) && !immediate)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    return ABORTED;
                }
            }
            if (powerConditionModifier == 0)
            {
                if (SUCCESS != ata_Idle(device, 0) && !immediate)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    ret = ABORTED;
                }
            }
            else
            {
                if (SUCCESS != ata_Idle(device, 0) && !immediate)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    ret = ABORTED;
                }
                else
                {
                    if (unload)
                    {
                        if (SUCCESS != ata_Idle_Immediate(device, true) && !immediate)
                        {
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                            ret = ABORTED;
                        }
                    }
                }
            }
            break;
        case 0x03: // standby (check power condition modifier for specific idle state)
            if (!noFlush)
            {
                if (SUCCESS != ata_Flush_Cache(device, flushCacheExt) && !immediate)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    return ABORTED;
                }
            }
            if (SUCCESS != ata_Standby(device, 0) && !immediate)
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                ret = ABORTED;
            }
            break;
        case 0x07: // LU Control
            if (SUCCESS == ata_Check_Power_Mode(device, &powerMode))
            {
                switch (powerMode)
                {
                case 0x00:
                    if (SUCCESS != ata_Standby(device, 0) && !immediate) // TODO: Switch to using saved timer value
                    {
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        ret = ABORTED;
                    }
                    break;
                case 0x80:
                    if (SUCCESS != ata_Idle(device, 0) && !immediate) // TODO: Switch to using saved timer value
                    {
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        ret = ABORTED;
                    }
                    break;
                case 0x40:
                case 0x41:
                case 0xFF:
                    if (SUCCESS != ata_Idle(device, 0) && !immediate) // TODO: Switch to using saved timer value
                    {
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        ret = ABORTED;
                    }
                    else
                    {
                        // ata verify command
                        uint64_t randomLba  = UINT64_C(0);
                        bool     extCommand = false;
                        if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                                device->drive_info.IdentifyData.ata.Word083) &&
                            le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT10)
                        {
                            extCommand = true;
                        }
                        seed_64(4894653);
                        randomLba = random_Range_64(0, device->drive_info.deviceMaxLba);
                        if (SUCCESS != ata_Read_Verify_Sectors(device, extCommand, 1, randomLba) && !immediate)
                        {
                            ret = ABORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                    break;
                default: // unknown power mode. Cannot say a specific thing in the CDB is bad
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    break;
                }
            }
            else if (!immediate)
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
                ret = ABORTED;
            }
            break;
        case 0x0B: // Force Standby (check power condition modifier for specific idle state)
            if (standbyTimersSpecifiedByStandard)
            {
                if (!noFlush)
                {
                    if (SUCCESS != ata_Flush_Cache(device, flushCacheExt) && !immediate)
                    {
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        return ABORTED;
                    }
                }
                if (SUCCESS != ata_Standby_Immediate(device) && !immediate)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    ret = ABORTED;
                }
            }
            else
            {
                fieldPointer = UINT16_C(4);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
            break;
        case 0x0A: // Force Idle (check power condition modifier for specific idle state)
        default:
            fieldPointer = UINT16_C(4);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
    }
    return ret;
}

static eReturnValues translate_Supported_Log_Pages(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret           = SUCCESS;
    bool          subpageFormat = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, supportedPages, LEGACY_DRIVE_SEC_SIZE); // this should be plenty big for now
    uint16_t offset    = UINT16_C(4);
    uint8_t  increment = UINT8_C(1);
    if (scsiIoCtx->cdb[3] == 0xFF)
    {
        subpageFormat = true;
        increment     = 2;
    }
    if (subpageFormat)
    {
        supportedPages[0] |= BIT6; // set the subpage format bit
        supportedPages[1] = 0xFF;
    }
    else
    {
        supportedPages[0] = 0;
        supportedPages[1] = 0;
    }
    // Set supported page page
    supportedPages[offset] = LP_SUPPORTED_LOG_PAGES;
    offset += increment;
    if (subpageFormat)
    {
        // set supported pages and subpages
        supportedPages[offset]     = 0;
        supportedPages[offset + 1] = 0xFF;
        offset += increment;
    }
    // read error counters log
    if (device->drive_info.softSATFlags.deviceStatsPages.rotatingMediaStatisticsPageSupported ||
        device->drive_info.softSATFlags.deviceStatsPages.generalErrorStatisticsSupported)
    {
        supportedPages[offset] = LP_READ_ERROR_COUNTERS;
        offset += increment;
    }
    // temperature log
    if (device->drive_info.softSATFlags.deviceStatsPages.temperatureStatisticsSupported)
    {
        supportedPages[offset] = LP_TEMPERATURE;
        offset += increment;
    }
    // application client
    if (device->drive_info.softSATFlags.hostLogsSupported)
    {
        supportedPages[offset] = LP_APPLICATION_CLIENT;
        offset += increment;
    }
    // If smart self test is supported, add the self test results log (10h)
    if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT1) ||
        (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT1))
    {
        supportedPages[offset] = LP_SELF_TEST_RESULTS;
        offset += increment;
    }
    // solid state media
    if (device->drive_info.softSATFlags.deviceStatsPages.solidStateDeviceStatisticsSupported)
    {
        supportedPages[offset] = LP_SOLID_STATE_MEDIA;
        offset += increment;
    }
    // background scan results and general statistics and performance
    if (device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported)
    {
        supportedPages[offset] = LP_BACKGROUND_SCAN_RESULTS;
        offset += increment;
        supportedPages[offset] = LP_GENERAL_STATISTICS_AND_PERFORMANCE;
        offset += increment;
    }

    // if smart is supported, add informational exceptions log page (2Fh)
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT0)
    {
        supportedPages[offset] = LP_INFORMATION_EXCEPTIONS;
        offset += increment;
    }

    // set the page length (Do this last)
    supportedPages[2] = M_Byte1(offset - 4);
    supportedPages[3] = M_Byte0(offset - 4);
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(
            scsiIoCtx->pdata, scsiIoCtx->dataLength, supportedPages,
            M_Min(scsiIoCtx->dataLength, C_CAST(uint16_t, M_Min(C_CAST(uint16_t, offset), LEGACY_DRIVE_SEC_SIZE))));
    }
    return ret;
}

static eReturnValues translate_Informational_Exceptions_Log_Page_2F(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, informationalExceptions, 11);
    if (SUCCESS == ata_SMART_Return_Status(device))
    {
        // set the header data
        informationalExceptions[0] = 0x2F;
        informationalExceptions[1] = 0x00;
        informationalExceptions[2] = 0x00;
        informationalExceptions[3] = 0x07;
        // start setting the remaining page data
        informationalExceptions[4] = 0;
        informationalExceptions[5] = 0;
        informationalExceptions[6] = 0x03;
        informationalExceptions[7] = 0x03;
        if (device->drive_info.lastCommandRTFRs.lbaMid == 0xF4 && device->drive_info.lastCommandRTFRs.lbaHi == 0x2C)
        {
            // fail status (Only set this when we actually know it was a failure)
            informationalExceptions[8] = 0x5D;
            informationalExceptions[9] = 0x10;
        }
        else
        {
            // pass status
            informationalExceptions[8] = 0x00;
            informationalExceptions[9] = 0x00;
        }
        // set temperature reading
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word206)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word206) & BIT0)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, sctData, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == ata_SMART_Read_Log(device, ATA_SCT_COMMAND_STATUS, sctData, LEGACY_DRIVE_SEC_SIZE))
            {
                if (sctData[200] == UINT8_C(0x80))
                {
                    informationalExceptions[10] = 0xFF;
                }
                else
                {
                    if (sctData[200] & 0x80) // if the sign bit is set, then the termperature is negative since this is
                                             // a 2's compliment value
                    {
                        informationalExceptions[10] = 0x00;
                    }
                    else
                    {
                        informationalExceptions[10] = sctData[200];
                    }
                }
            }
            else
            {
                // shouldn't happen...
                informationalExceptions[10] = 0xFF;
            }
        }
        else
        {
            informationalExceptions[10] = 0xFF;
        }
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, informationalExceptions,
                        M_Min(11U, scsiIoCtx->dataLength));
        }
    }
    else // hopefully this doesn't happen...
    {
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
        ret = ABORTED;
    }
    return ret;
}

static eReturnValues translate_Self_Test_Results_Log_0x10(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, selfTestResults, 404);
    uint16_t parameterCode = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (parameterCode > 0x0014)
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (parameterCode == 0)
    {
        parameterCode = 1;
    }
    // set the header
    selfTestResults[0] = 0x10;
    selfTestResults[1] = 0x00;
    selfTestResults[2] = 0x01;
    selfTestResults[3] = 0x90;
    // remaining data comes from the logs...check if GPL is supported since that can get us a larger LBA to return for
    // errors
    if (device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, extSelfTestLog,
                                LEGACY_DRIVE_SEC_SIZE); // 2 sectors in size in case we need to read 2 pages of the log
        // read log ext (up to 3449 pages, but we only need to read 2 pages at most, starting with the first page)
        if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, 0, extSelfTestLog,
                                        LEGACY_DRIVE_SEC_SIZE,
                                        device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
        {
            uint16_t selfTestIndex = M_BytesTo2ByteValue(extSelfTestLog[3], extSelfTestLog[2]);
            if (selfTestIndex != 0)
            {
                uint16_t lastPageRead = UINT16_C(0);
                uint16_t iter         = UINT16_C(4);
                uint32_t ataLogOffset = UINT32_C(4); // point to first descriptor
                // now we need to go through and save the most recent entries to the log we'll return
                for (; parameterCode <= 0x0014; iter += 20, parameterCode++)
                {
                    uint8_t  selfTestCode                 = UINT8_C(0);
                    uint8_t  selfTestResult               = UINT8_C(0);
                    uint8_t  selfTestNumber               = UINT8_C(0);
                    uint16_t accumulatedPowerOnHours      = UINT16_C(0);
                    uint64_t addressOfFirstFailure        = UINT64_C(0);
                    uint8_t  senseKey                     = UINT8_C(0);
                    uint8_t  additionalSenseCode          = UINT8_C(0);
                    uint8_t  additionalSenseCodeQualifier = UINT8_C(0);
                    int16_t  ataDescriptorNumber;
                    selfTestResults[iter]     = M_Byte1(parameterCode);
                    selfTestResults[iter + 1] = M_Byte0(parameterCode);
                    selfTestResults[iter + 2] = 0x03; // format and linking = 11b
                    selfTestResults[iter + 3] = 0x10; // parameter length = 10h
                    // remaining bytes 4 - 19 are translated from the data we have
                    // This translation is a little trickier to translate. We need to do: selfTestIndex - paramcode + 1
                    // and ONLY if that result is greater than zero do we use that descriptor value
                    ataDescriptorNumber = C_CAST(int16_t, selfTestIndex - parameterCode + INT16_C(1));
                    if (ataDescriptorNumber > 0)
                    {
                        uint16_t pageNumber;
                        // set the buffer offset from the descriptor number we got above - we may need to read a
                        // different page of the log if it's a multipage log
                        ataLogOffset = ((C_CAST(uint32_t, ataDescriptorNumber) * 26) - 26) + 4;
                        pageNumber   = C_CAST(uint16_t, ataLogOffset / LEGACY_DRIVE_SEC_SIZE);
                        if (pageNumber > 0 && lastPageRead != pageNumber)
                        {
                            // need to read a different page of the log
                            if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_EXTENDED_SMART_SELF_TEST_LOG, pageNumber,
                                                            extSelfTestLog, LEGACY_DRIVE_SEC_SIZE,
                                                            device->drive_info.ata_Options.readLogWriteLogDMASupported,
                                                            0))
                            {
                                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                        scsiIoCtx->senseDataSize);
                                return ABORTED;
                            }
                            // now adjust the offset back to within a single page
                            ataLogOffset -= (LEGACY_DRIVE_SEC_SIZE * pageNumber);
                            lastPageRead = pageNumber;
                        }
                        // set the self test code (unspecified - attempting to translate if possible)
                        switch (extSelfTestLog[ataLogOffset])
                        {
                        case 0x01: // short self test - offline
                            selfTestCode = 1;
                            break;
                        case 0x02: // extended self test - offline
                            selfTestCode = 2;
                            break;
                        case 0x81: // short self test - captive
                            selfTestCode = 5;
                            break;
                        case 0x82: // extended self test - captive
                            selfTestCode = 6;
                            break;
                        default:
                            // set zero since it doesn't translate
                            selfTestCode = 0;
                            break;
                        }
                        // set the self test results
                        selfTestResult = M_Nibble0(extSelfTestLog[ataLogOffset + 1]);
                        // set the self test number
                        selfTestNumber = extSelfTestLog[ataLogOffset + 4]; // unspecified - setting to the content of
                                                                           // the self test failure check point byte
                        // power on hours
                        accumulatedPowerOnHours =
                            M_BytesTo2ByteValue(extSelfTestLog[ataLogOffset + 3], extSelfTestLog[ataLogOffset + 2]);
                        // address of failure
                        addressOfFirstFailure = M_BytesTo8ByteValue(
                            0, 0, extSelfTestLog[ataLogOffset + 10], extSelfTestLog[ataLogOffset + 9],
                            extSelfTestLog[ataLogOffset + 8], extSelfTestLog[ataLogOffset + 7],
                            extSelfTestLog[ataLogOffset + 6], extSelfTestLog[ataLogOffset + 5]);
                        if (M_Nibble0(extSelfTestLog[ataLogOffset + 1]) !=
                            0x07) // only keep LBA field when we had a read failure. Otherwise, it should be set to all
                                  // F's
                        {
                            addressOfFirstFailure = UINT64_MAX;
                        }
                        // set sense information
                        switch (M_Nibble0(extSelfTestLog[ataLogOffset + 1]))
                        {
                        case 0xF:
                        case 0x0:
                            senseKey                     = SENSE_KEY_NO_ERROR;
                            additionalSenseCode          = 0;
                            additionalSenseCodeQualifier = 0;
                            break;
                        case 0x1:
                            senseKey                     = SENSE_KEY_ABORTED_COMMAND;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x81;
                            break;
                        case 0x2:
                            senseKey                     = SENSE_KEY_ABORTED_COMMAND;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x82;
                            break;
                        case 0x3:
                            senseKey                     = SENSE_KEY_ABORTED_COMMAND;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x83;
                            break;
                        case 0x4:
                            senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x84;
                            break;
                        case 0x5:
                            senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x85;
                            break;
                        case 0x6:
                            senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x86;
                            break;
                        case 0x7:
                            senseKey                     = SENSE_KEY_MEDIUM_ERROR;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x87;
                            break;
                        case 0x8:
                            senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                            additionalSenseCode          = 0x40;
                            additionalSenseCodeQualifier = 0x88;
                            break;
                        case 0x9: // unspecified
                        case 0xA: // unspecified
                        case 0xB: // unspecified
                        case 0xC: // unspecified
                        case 0xD: // unspecified
                        case 0xE: // unspecified
                        default:
                            senseKey                     = SENSE_KEY_NO_ERROR;
                            additionalSenseCode          = 0;
                            additionalSenseCodeQualifier = 0;
                            break;
                        }
                    }
                    else
                    {
                        selfTestCode                 = 0;
                        selfTestResult               = 0;
                        selfTestNumber               = 0;
                        accumulatedPowerOnHours      = 0;
                        addressOfFirstFailure        = 0;
                        senseKey                     = 0;
                        additionalSenseCode          = 0;
                        additionalSenseCodeQualifier = 0;
                    }
                    // set the data into the buffer now
                    selfTestResults[iter + 4] = C_CAST(uint8_t, selfTestCode << 5);
                    selfTestResults[iter + 4] |= selfTestResult;
                    selfTestResults[iter + 5]  = selfTestNumber;
                    selfTestResults[iter + 6]  = M_Byte1(accumulatedPowerOnHours);
                    selfTestResults[iter + 7]  = M_Byte0(accumulatedPowerOnHours);
                    selfTestResults[iter + 8]  = M_Byte7(addressOfFirstFailure);
                    selfTestResults[iter + 9]  = M_Byte6(addressOfFirstFailure);
                    selfTestResults[iter + 10] = M_Byte5(addressOfFirstFailure);
                    selfTestResults[iter + 11] = M_Byte4(addressOfFirstFailure);
                    selfTestResults[iter + 12] = M_Byte3(addressOfFirstFailure);
                    selfTestResults[iter + 13] = M_Byte2(addressOfFirstFailure);
                    selfTestResults[iter + 14] = M_Byte1(addressOfFirstFailure);
                    selfTestResults[iter + 15] = M_Byte0(addressOfFirstFailure);
                    selfTestResults[iter + 16] = senseKey;
                    selfTestResults[iter + 17] = additionalSenseCode;
                    selfTestResults[iter + 18] = additionalSenseCodeQualifier;
                    // vendor specific
                    selfTestResults[iter + 19] = 0;
                }
            }
            else
            {
                // no valid descriptors so set empty parameters
                uint16_t iter = UINT16_C(4);
                for (; iter < 403; iter += 20, parameterCode++)
                {
                    selfTestResults[iter]     = M_Byte1(parameterCode);
                    selfTestResults[iter + 1] = M_Byte0(parameterCode);
                    selfTestResults[iter + 2] = 0x03; // format and linking = 11b
                    selfTestResults[iter + 3] = 0x10; // parameter length = 10h
                    // remaining bytes 4 - 19 set to zero
                    selfTestResults[iter + 4]  = 0;
                    selfTestResults[iter + 5]  = 0;
                    selfTestResults[iter + 6]  = 0;
                    selfTestResults[iter + 7]  = 0;
                    selfTestResults[iter + 8]  = 0;
                    selfTestResults[iter + 9]  = 0;
                    selfTestResults[iter + 10] = 0;
                    selfTestResults[iter + 11] = 0;
                    selfTestResults[iter + 12] = 0;
                    selfTestResults[iter + 13] = 0;
                    selfTestResults[iter + 14] = 0;
                    selfTestResults[iter + 15] = 0;
                    selfTestResults[iter + 16] = 0;
                    selfTestResults[iter + 17] = 0;
                    selfTestResults[iter + 18] = 0;
                    selfTestResults[iter + 19] = 0;
                }
            }
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, selfTestResults,
                            M_Min(scsiIoCtx->dataLength, 404U));
            }
        }
    }
    else
    {
        // smart read log (single sector)
        DECLARE_ZERO_INIT_ARRAY(uint8_t, selfTestLog, LEGACY_DRIVE_SEC_SIZE);
        if (SUCCESS == ata_SMART_Read_Log(device, ATA_LOG_SMART_SELF_TEST_LOG, selfTestLog, LEGACY_DRIVE_SEC_SIZE))
        {
            uint8_t selfTestIndex = selfTestLog[508];
            if (selfTestIndex != 0)
            {
                // valid log data
                uint16_t iter = UINT16_C(4);
                uint16_t ataLogOffset =
                    2; // this should put us at the beginning of the descriptor we're going to read (SAT spec says, just
                       // match the parameter number to the self test descriptor number)
                for (; parameterCode <= 0x0014; iter += 20, parameterCode++, ataLogOffset += 24)
                {
                    uint8_t selfTestCode = UINT8_C(0);
                    // uint8_t selfTestResult = UINT8_C(0);
                    // uint8_t selfTestNumber = UINT8_C(0);
                    uint16_t accumulatedPowerOnHours      = UINT16_C(0);
                    uint64_t addressOfFirstFailure        = UINT64_C(0);
                    uint8_t  senseKey                     = UINT8_C(0);
                    uint8_t  additionalSenseCode          = UINT8_C(0);
                    uint8_t  additionalSenseCodeQualifier = UINT8_C(0);
                    selfTestResults[iter]                 = M_Byte1(parameterCode);
                    selfTestResults[iter + 1]             = M_Byte0(parameterCode);
                    selfTestResults[iter + 2]             = 0x03; // format and linking = 11b
                    selfTestResults[iter + 3]             = 0x10; // parameter length = 10h
                    // remaining bytes 4 - 19 are translated from the data we have
                    // set the self test code (unspecified - attempting to translate if possible)
                    switch (selfTestLog[ataLogOffset])
                    {
                    case 0x01: // short self test - offline
                        selfTestCode = 1;
                        break;
                    case 0x02: // extended self test - offline
                        selfTestCode = 2;
                        break;
                    case 0x81: // short self test - captive
                        selfTestCode = 5;
                        break;
                    case 0x82: // extended self test - captive
                        selfTestCode = 6;
                        break;
                    default:
                        // set zero since it doesn't translate
                        selfTestCode = 0;
                        break;
                    }
                    selfTestResults[iter + 4] = C_CAST(uint8_t, selfTestCode << 5);
                    // set theself test results
                    selfTestResults[iter + 4] |= M_Nibble0(selfTestLog[ataLogOffset + 1]);
                    // set the self test number
                    selfTestResults[iter + 5] =
                        selfTestLog[ataLogOffset + 4]; // unspecified - setting to the content of the self test failure
                                                       // check point byte
                    // power on hours
                    accumulatedPowerOnHours =
                        M_BytesTo2ByteValue(selfTestLog[ataLogOffset + 3], selfTestLog[ataLogOffset + 2]);
                    selfTestResults[iter + 6] = M_Byte1(accumulatedPowerOnHours);
                    selfTestResults[iter + 7] = M_Byte0(accumulatedPowerOnHours);
                    // address of failure
                    addressOfFirstFailure =
                        M_BytesTo4ByteValue(selfTestLog[ataLogOffset + 8], selfTestLog[ataLogOffset + 7],
                                            selfTestLog[ataLogOffset + 6], selfTestLog[ataLogOffset + 5]);
                    if (M_Nibble0(selfTestLog[ataLogOffset + 1]) !=
                        0x07) // only keep LBA field when we had a read failure. Otherwise, it should be set to all F's
                    {
                        addressOfFirstFailure = UINT64_MAX;
                    }
                    selfTestResults[iter + 8]  = M_Byte7(addressOfFirstFailure);
                    selfTestResults[iter + 9]  = M_Byte6(addressOfFirstFailure);
                    selfTestResults[iter + 10] = M_Byte5(addressOfFirstFailure);
                    selfTestResults[iter + 11] = M_Byte4(addressOfFirstFailure);
                    selfTestResults[iter + 12] = M_Byte3(addressOfFirstFailure);
                    selfTestResults[iter + 13] = M_Byte2(addressOfFirstFailure);
                    selfTestResults[iter + 14] = M_Byte1(addressOfFirstFailure);
                    selfTestResults[iter + 15] = M_Byte0(addressOfFirstFailure);
                    // set sense information
                    switch (M_Nibble0(selfTestLog[ataLogOffset + 1]))
                    {
                    case 0xF:
                    case 0x0:
                        senseKey                     = SENSE_KEY_NO_ERROR;
                        additionalSenseCode          = 0;
                        additionalSenseCodeQualifier = 0;
                        break;
                    case 0x1:
                        senseKey                     = SENSE_KEY_ABORTED_COMMAND;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x81;
                        break;
                    case 0x2:
                        senseKey                     = SENSE_KEY_ABORTED_COMMAND;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x82;
                        break;
                    case 0x3:
                        senseKey                     = SENSE_KEY_ABORTED_COMMAND;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x83;
                        break;
                    case 0x4:
                        senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x84;
                        break;
                    case 0x5:
                        senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x85;
                        break;
                    case 0x6:
                        senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x86;
                        break;
                    case 0x7:
                        senseKey                     = SENSE_KEY_MEDIUM_ERROR;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x87;
                        break;
                    case 0x8:
                        senseKey                     = SENSE_KEY_HARDWARE_ERROR;
                        additionalSenseCode          = 0x40;
                        additionalSenseCodeQualifier = 0x88;
                        break;
                    case 0x9: // unspecified
                    case 0xA: // unspecified
                    case 0xB: // unspecified
                    case 0xC: // unspecified
                    case 0xD: // unspecified
                    case 0xE: // unspecified
                    default:
                        senseKey                     = SENSE_KEY_NO_ERROR;
                        additionalSenseCode          = 0;
                        additionalSenseCodeQualifier = 0;
                        break;
                    }
                    selfTestResults[iter + 16] = senseKey;
                    selfTestResults[iter + 17] = additionalSenseCode;
                    selfTestResults[iter + 18] = additionalSenseCodeQualifier;
                    // vendor specific
                    selfTestResults[iter + 19] = 0;
                }
            }
            else
            {
                // no valid descriptors so set empy parameters
                uint16_t iter = UINT16_C(4);
                for (; iter < 403; iter += 20, parameterCode++)
                {
                    selfTestResults[iter]     = M_Byte1(parameterCode);
                    selfTestResults[iter + 1] = M_Byte0(parameterCode);
                    selfTestResults[iter + 2] = 0x03; // format and linking = 11b
                    selfTestResults[iter + 3] = 0x10; // parameter length = 10h
                    // remaining bytes 4 - 19 set to zero
                    selfTestResults[iter + 4]  = 0;
                    selfTestResults[iter + 5]  = 0;
                    selfTestResults[iter + 6]  = 0;
                    selfTestResults[iter + 7]  = 0;
                    selfTestResults[iter + 8]  = 0;
                    selfTestResults[iter + 9]  = 0;
                    selfTestResults[iter + 10] = 0;
                    selfTestResults[iter + 11] = 0;
                    selfTestResults[iter + 12] = 0;
                    selfTestResults[iter + 13] = 0;
                    selfTestResults[iter + 14] = 0;
                    selfTestResults[iter + 15] = 0;
                    selfTestResults[iter + 16] = 0;
                    selfTestResults[iter + 17] = 0;
                    selfTestResults[iter + 18] = 0;
                    selfTestResults[iter + 19] = 0;
                }
            }
            if (scsiIoCtx->pdata)
            {
                safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, selfTestResults,
                            M_Min(404U, scsiIoCtx->dataLength));
            }
        }
        else
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
            ret = ABORTED;
        }
    }
    return ret;
}

static eReturnValues translate_Read_Error_Counters_Log_0x03(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret              = SUCCESS;
    uint16_t      parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    // only parameters 4 and 6 are supported all others will be ommitted
    DECLARE_ZERO_INIT_ARRAY(uint8_t, readErrorCountersLog, 20);
    uint8_t offset = UINT8_C(4);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logPage, LEGACY_DRIVE_SEC_SIZE);
    uint64_t* qwordPtr                    = M_REINTERPRET_CAST(uint64_t*, &logPage[0]);
    bool      correctiveAlgValid          = false;
    bool      reportedUncorrectablesValid = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer     = UINT8_C(0);
    uint16_t fieldPointer   = UINT16_C(0);
    readErrorCountersLog[0] = 0x03;
    readErrorCountersLog[1] = 0x00;

    if (parameterPointer <= 0x0004 &&
        device->drive_info.softSATFlags.deviceStatsPages.rotatingMediaStatisticsPageSupported)
    {
        if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_ROTATING_MEDIA, logPage,
                                        LEGACY_DRIVE_SEC_SIZE,
                                        device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
            return FAILURE;
        }
        if (le64_to_host(qwordPtr[5]) & BIT63 && le64_to_host(qwordPtr[5]) & BIT62)
        {
            uint32_t correctiveAlgProcessedCnt = M_DoubleWord0(le64_to_host(qwordPtr[5]));
            correctiveAlgValid                 = true;
            readErrorCountersLog[offset + 0]   = 0x00;
            readErrorCountersLog[offset + 1]   = 0x04;
            readErrorCountersLog[offset + 2]   = 0x02; // format and linking = 10b
            readErrorCountersLog[offset + 3]   = 0x04; // parameter length
            // the statistic from the log
            readErrorCountersLog[offset + 4] = M_Byte3(correctiveAlgProcessedCnt);
            readErrorCountersLog[offset + 5] = M_Byte2(correctiveAlgProcessedCnt);
            readErrorCountersLog[offset + 6] = M_Byte1(correctiveAlgProcessedCnt);
            readErrorCountersLog[offset + 7] = M_Byte0(correctiveAlgProcessedCnt);
            offset += 8;
        }
    }
    if (parameterPointer <= 0x0006 && device->drive_info.softSATFlags.deviceStatsPages.generalErrorStatisticsSupported)
    {
        if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_GEN_ERR, logPage,
                                        LEGACY_DRIVE_SEC_SIZE,
                                        device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
            return FAILURE;
        }
        if (le64_to_host(qwordPtr[1]) & BIT63 && le64_to_host(qwordPtr[1]) & BIT62)
        {
            uint32_t totalUncorrectables     = M_DoubleWord0(le64_to_host(qwordPtr[1]));
            reportedUncorrectablesValid      = true;
            readErrorCountersLog[offset + 0] = 0x00;
            readErrorCountersLog[offset + 1] = 0x04;
            readErrorCountersLog[offset + 2] = 0x02; // format and linking = 10b
            readErrorCountersLog[offset + 3] = 0x04; // parameter length
            // the statistic from the log
            readErrorCountersLog[offset + 4] = M_Byte3(totalUncorrectables);
            readErrorCountersLog[offset + 5] = M_Byte2(totalUncorrectables);
            readErrorCountersLog[offset + 6] = M_Byte1(totalUncorrectables);
            readErrorCountersLog[offset + 7] = M_Byte0(totalUncorrectables);
            offset += 8;
        }
    }
    if (parameterPointer > 0x0006 || (parameterPointer > 0x0004 && !reportedUncorrectablesValid))
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    if (reportedUncorrectablesValid || correctiveAlgValid)
    {
        // set page length at the end
        readErrorCountersLog[2] = M_Byte1(offset - 4);
        readErrorCountersLog[3] = M_Byte0(offset - 4);
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readErrorCountersLog,
                        M_Min(M_Min(20U, offset), scsiIoCtx->dataLength));
        }
    }
    else // translatable fields aren't valid...
    {
        fieldPointer = UINT16_C(2);
        bitPointer   = UINT8_C(5);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_Temperature_Log_0x0D(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, temperatureLog, 16);
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t  offset           = UINT8_C(4);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logPage, LEGACY_DRIVE_SEC_SIZE);
    uint64_t* qwordPtr       = C_CAST(uint64_t*, &logPage[0]);
    bool      currentValid   = false;
    bool      referenceValid = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (parameterPointer > 1)
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_TEMP, logPage,
                                    LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported,
                                    0))
    {
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    temperatureLog[0] = 0x0D;
    temperatureLog[1] = 0x00;
    if (parameterPointer <= 0)
    {
        // current temp
        if (le64_to_host(qwordPtr[1]) & BIT63 && le64_to_host(qwordPtr[1]) & BIT62)
        {
            temperatureLog[offset + 0] = 0;
            temperatureLog[offset + 1] = 0;
            temperatureLog[offset + 2] = 0x03; // format and linking = 11b
            temperatureLog[offset + 3] = 0x02;
            temperatureLog[offset + 4] = RESERVED;
            if (le64_to_host(qwordPtr[1]) & BIT7)
            {
                temperatureLog[offset + 5] = 0;
            }
            else
            {
                currentValid               = true;
                temperatureLog[offset + 5] = M_Byte0(le64_to_host(qwordPtr[1]));
            }
            offset += 6;
        }
    }
    if (parameterPointer <= 1)
    {
        // reference temp
        if (le64_to_host(qwordPtr[11]) & BIT63 && le64_to_host(qwordPtr[11]) & BIT62)
        {
            temperatureLog[offset + 0] = 0;
            temperatureLog[offset + 1] = 1;
            temperatureLog[offset + 2] = 0x03; // format and linking = 11b
            temperatureLog[offset + 3] = 0x02;
            temperatureLog[offset + 4] = RESERVED;
            if (le64_to_host(qwordPtr[11]) & BIT7)
            {
                temperatureLog[offset + 5] = 0;
            }
            else
            {
                referenceValid             = true;
                temperatureLog[offset + 5] = M_Byte0(le64_to_host(qwordPtr[11]));
            }
            offset += 6;
        }
    }
    if (currentValid || referenceValid)
    {
        // set page length at the end
        temperatureLog[2] = M_Byte1(offset - 4);
        temperatureLog[3] = M_Byte0(offset - 4);
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, temperatureLog,
                        M_Min(M_Min(14U, offset), scsiIoCtx->dataLength));
        }
    }
    else // translatable fields aren't valid...so we need to set bad page code
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_Solid_State_Media_Log_0x11(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, solidStateMediaLog, 12);
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t  offset           = UINT8_C(4);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logPage, LEGACY_DRIVE_SEC_SIZE);
    uint64_t* qwordPtr       = C_CAST(uint64_t*, &logPage[0]);
    bool      endurancevalid = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (parameterPointer > 1)
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_SSD, logPage,
                                    LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported,
                                    0))
    {
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    solidStateMediaLog[0] = 0x11;
    solidStateMediaLog[1] = 0x00;
    if (parameterPointer <= 1)
    {
        // endurance
        if (le64_to_host(qwordPtr[1]) & BIT63 && le64_to_host(qwordPtr[1]) & BIT62)
        {
            endurancevalid                 = true;
            solidStateMediaLog[offset + 0] = 0x00;
            solidStateMediaLog[offset + 1] = 0x01;
            solidStateMediaLog[offset + 2] = 0x03; // format and linking = 11b
            solidStateMediaLog[offset + 3] = 0x04;
            solidStateMediaLog[offset + 4] = RESERVED;
            solidStateMediaLog[offset + 5] = RESERVED;
            solidStateMediaLog[offset + 6] = RESERVED;
            solidStateMediaLog[offset + 7] = M_Byte0(le64_to_host(qwordPtr[11]));
            offset += 8;
        }
    }
    if (endurancevalid)
    {
        // set page length at the end
        solidStateMediaLog[2] = M_Byte1(offset - 4);
        solidStateMediaLog[3] = M_Byte0(offset - 4);
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, solidStateMediaLog,
                        M_Min(M_Min(12U, offset), scsiIoCtx->dataLength));
        }
    }
    else // the translatable field is not valid, so return unsupported page code
    {
        fieldPointer = UINT16_C(2);
        bitPointer   = UINT8_C(5);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_Background_Scan_Results_Log_0x15(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, backgroundResults, 20);
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t  offset           = UINT8_C(4);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logPage, LEGACY_DRIVE_SEC_SIZE);
    uint64_t* qwordPtr = (uint64_t*)&logPage[0];
    bool      pohvalid = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (parameterPointer > 0)
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_GENERAL, logPage,
                                    LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported,
                                    0))
    {
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    backgroundResults[0] = 0x11;
    backgroundResults[1] = 0x00;
    if (parameterPointer <= 0)
    {
        // poh
        if (le64_to_host(qwordPtr[2]) & BIT63 && le64_to_host(qwordPtr[2]) & BIT62)
        {
            uint64_t pohMinutes            = UINT64_C(60) * C_CAST(uint64_t, M_DoubleWord0(le64_to_host(qwordPtr[2])));
            pohvalid                       = true;
            backgroundResults[offset + 0]  = 0x00;
            backgroundResults[offset + 1]  = 0x00;
            backgroundResults[offset + 2]  = 0x03; // format and linking = 11b
            backgroundResults[offset + 3]  = 0x0C;
            backgroundResults[offset + 4]  = M_Byte3(pohMinutes);
            backgroundResults[offset + 5]  = M_Byte2(pohMinutes);
            backgroundResults[offset + 6]  = M_Byte1(pohMinutes);
            backgroundResults[offset + 7]  = M_Byte0(pohMinutes);
            backgroundResults[offset + 8]  = RESERVED;
            backgroundResults[offset + 9]  = 0;
            backgroundResults[offset + 10] = 0;
            backgroundResults[offset + 11] = 0;
            backgroundResults[offset + 12] = 0;
            backgroundResults[offset + 13] = 0;
            backgroundResults[offset + 14] = 0;
            backgroundResults[offset + 15] = 0;
            offset += 8;
        }
    }
    if (pohvalid)
    {
        // set page length at the end
        backgroundResults[2] = M_Byte1(offset - 4);
        backgroundResults[3] = M_Byte0(offset - 4);
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, backgroundResults,
                        M_Min(M_Min(20U, offset), scsiIoCtx->dataLength));
        }
    }
    else // cannot translate the one translatable field, so set invalid page code
    {
        fieldPointer = UINT16_C(2);
        bitPointer   = UINT8_C(5);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_General_Statistics_And_Performance_Log_0x19(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, generalStatisticsAndPerformance, 72);
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t  offset           = UINT8_C(4);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, logPage, LEGACY_DRIVE_SEC_SIZE);
    uint64_t* qwordPtr                   = (uint64_t*)&logPage[0];
    bool      numReadsValid              = false;
    bool      numWritesValid             = false;
    bool      logicalSectorsWrittenValid = false;
    bool      logicalSectorsReadValid    = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // validity bools...used to make sure we set up SOME data
    if (parameterPointer > 1)
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (SUCCESS != ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_GENERAL, logPage,
                                    LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported,
                                    0))
    {
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    generalStatisticsAndPerformance[0] = 0x19;
    generalStatisticsAndPerformance[1] = 0x00;
    if (parameterPointer <= 1)
    {
        generalStatisticsAndPerformance[offset + 0] = 0x00;
        generalStatisticsAndPerformance[offset + 1] = 0x01;
        generalStatisticsAndPerformance[offset + 2] = 0x03; // format and linking = 11b
        generalStatisticsAndPerformance[offset + 3] = 0x40;
        // Number of read Commands
        if (le64_to_host(qwordPtr[6]) & BIT63 && le64_to_host(qwordPtr[6]) & BIT62)
        {
            uint64_t numberReads                         = le64_to_host(qwordPtr[6]) & MAX_48_BIT_LBA;
            numReadsValid                                = true;
            generalStatisticsAndPerformance[offset + 4]  = M_Byte7(numberReads);
            generalStatisticsAndPerformance[offset + 5]  = M_Byte6(numberReads);
            generalStatisticsAndPerformance[offset + 6]  = M_Byte5(numberReads);
            generalStatisticsAndPerformance[offset + 7]  = M_Byte4(numberReads);
            generalStatisticsAndPerformance[offset + 8]  = M_Byte3(numberReads);
            generalStatisticsAndPerformance[offset + 9]  = M_Byte2(numberReads);
            generalStatisticsAndPerformance[offset + 10] = M_Byte1(numberReads);
            generalStatisticsAndPerformance[offset + 11] = M_Byte0(numberReads);
        }
        // number of write commands
        if (le64_to_host(qwordPtr[4]) & BIT63 && le64_to_host(qwordPtr[4]) & BIT62)
        {
            uint64_t numberWrites                        = le64_to_host(qwordPtr[4]) & MAX_48_BIT_LBA;
            numWritesValid                               = true;
            generalStatisticsAndPerformance[offset + 12] = M_Byte7(numberWrites);
            generalStatisticsAndPerformance[offset + 13] = M_Byte6(numberWrites);
            generalStatisticsAndPerformance[offset + 14] = M_Byte5(numberWrites);
            generalStatisticsAndPerformance[offset + 15] = M_Byte4(numberWrites);
            generalStatisticsAndPerformance[offset + 16] = M_Byte3(numberWrites);
            generalStatisticsAndPerformance[offset + 17] = M_Byte2(numberWrites);
            generalStatisticsAndPerformance[offset + 18] = M_Byte1(numberWrites);
            generalStatisticsAndPerformance[offset + 19] = M_Byte0(numberWrites);
        }
        // number of logical blocks received
        if (le64_to_host(qwordPtr[3]) & BIT63 && le64_to_host(qwordPtr[3]) & BIT62)
        {
            uint64_t numLogBlocksWritten                 = le64_to_host(qwordPtr[3]) & MAX_48_BIT_LBA;
            logicalSectorsWrittenValid                   = true;
            generalStatisticsAndPerformance[offset + 20] = M_Byte7(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 21] = M_Byte6(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 22] = M_Byte5(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 23] = M_Byte4(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 24] = M_Byte3(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 25] = M_Byte2(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 26] = M_Byte1(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 27] = M_Byte0(numLogBlocksWritten);
        }
        // number of logical blocks transmitted
        if (le64_to_host(qwordPtr[5]) & BIT63 && le64_to_host(qwordPtr[5]) & BIT62)
        {
            uint64_t numLogBlocksRead                    = le64_to_host(qwordPtr[5]) & MAX_48_BIT_LBA;
            logicalSectorsReadValid                      = true;
            generalStatisticsAndPerformance[offset + 28] = M_Byte7(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 29] = M_Byte6(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 30] = M_Byte5(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 31] = M_Byte4(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 32] = M_Byte3(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 33] = M_Byte2(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 34] = M_Byte1(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 35] = M_Byte0(numLogBlocksRead);
        }
        // all other fields to be set to zero since translation is unspecified - TJE
        offset += 68;
    }
    if (numReadsValid || numWritesValid || logicalSectorsWrittenValid || logicalSectorsReadValid)
    {
        // set page length at the end
        generalStatisticsAndPerformance[2] = M_Byte1(offset - 4);
        generalStatisticsAndPerformance[3] = M_Byte0(offset - 4);
        if (scsiIoCtx->pdata)
        {
            safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, generalStatisticsAndPerformance,
                        M_Min(M_Min(UINT32_C(72), offset), scsiIoCtx->dataLength));
        }
    }
    else // none of the translatable fields are valid, so say this page is not supported
    {
        fieldPointer = UINT16_C(2);
        bitPointer   = UINT8_C(5);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_ATA_Passthrough_Results_Log_Page_16(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, ataPassthroughResults,
                            274); // 15 (number of results) * (14 + 4) (size of pass-through results descriptor and
                                  // parameter header) + 4 (log header)
    uint16_t parameterCode = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint16_t offset        = UINT16_C(4);
    if (parameterCode > 0x000E)
    {
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        return ret;
    }
    // set the header
    ataPassthroughResults[0] = 0x16;
    ataPassthroughResults[1] = 0;

    // now we need to go through and save the most recent entries to the log we'll return
    for (; parameterCode <= 0x000E && offset < 274; offset += 18, ++parameterCode)
    {
        ataPassthroughResults[offset + 0] = M_Byte1(parameterCode);
        ataPassthroughResults[offset + 1] = M_Byte0(parameterCode);
        ataPassthroughResults[offset + 2] = 0x03;         // format and linking = 11b
        ataPassthroughResults[offset + 3] = SAT_DESC_LEN; // length of ATA Pass-through results descriptor
        ataPassthroughResults[offset + 4] = SAT_DESCRIPTOR_CODE;
        ataPassthroughResults[offset + 5] = SAT_ADDT_DESC_LEN;
        // check if the extend bit needs to be set
        if (device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].secCntExt > 0 ||
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaHiExt > 0 ||
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaMidExt > 0 ||
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaLowExt > 0)
        {
            ataPassthroughResults[offset + 6] |= BIT0;
            ataPassthroughResults[offset + 8] =
                device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].secCntExt;
            ataPassthroughResults[offset + 10] =
                device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaLowExt;
            ataPassthroughResults[offset + 12] =
                device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaMidExt;
            ataPassthroughResults[offset + 14] =
                device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaHiExt;
        }
        ataPassthroughResults[offset + 7] = device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].error;
        ataPassthroughResults[offset + 9] = device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].secCnt;
        ataPassthroughResults[offset + 11] =
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaLow;
        ataPassthroughResults[offset + 13] =
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaMid;
        ataPassthroughResults[offset + 15] = device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].lbaHi;
        ataPassthroughResults[offset + 16] =
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].device;
        ataPassthroughResults[offset + 17] =
            device->drive_info.softSATFlags.ataPassthroughResults[parameterCode].status;
    }
    // set page length at the end
    ataPassthroughResults[2] = M_Byte1(offset - 4);
    ataPassthroughResults[3] = M_Byte0(offset - 4);
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, ataPassthroughResults,
                    M_Min(M_Min(274U, offset), scsiIoCtx->dataLength));
    }
    return ret;
}

static eReturnValues translate_Application_Client_Log_Sense_0x0F(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret              = SUCCESS;
    uint16_t      parameterCode    = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint16_t      allocationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    uint16_t numberOfParametersToReturn;
    uint8_t* applicationClientLog;
    uint16_t offset           = UINT16_C(0);
    uint16_t parameterCounter = UINT16_C(0);

    // support parameters 0 - 1FFh
    if (parameterCode > 0x01FF)
    {
        fieldPointer = UINT16_C(5);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    // calculate how many parameters we'll be returning.
    numberOfParametersToReturn =
        C_CAST(uint16_t,
               (allocationLength - 4) / (4 + 0xFC)); //(4 + 0xFC) is the size of a parameter for the application client.
                                                     // allocation length - 4 takes into account the header of the log
    // set the header
    applicationClientLog    = &scsiIoCtx->pdata[0];
    applicationClientLog[0] = 0x0F;
    applicationClientLog[1] = 0x00;
    offset                  = 4;
    // now we need to go through and save the most recent entries to the log we'll return
    parameterCounter = 0;
    while (parameterCode <= 0x01FF && offset < allocationLength && parameterCounter < numberOfParametersToReturn)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE);
        // The parameter headers may be written to the host vendor logs if they've been written...but if not, then we'll
        // need to create the header. It's much simpler to take the parameters and write directly to the logs for a
        // SATL, but this may not happen in this software implementation, so we need to handle that.
        uint16_t offsetOnATAPage  = UINT16_C(0);
        uint8_t  ataLogPageToRead = UINT8_C(0);
        if (/*parameterCode >= 0x00 &&*/ parameterCode <= 0x001F)
        {
            // ata log 0x90
            ataLogPageToRead = 0x90;
        }
        else if (parameterCode >= 0x20 && parameterCode <= 0x003F)
        {
            // ata log 0x91
            ataLogPageToRead = 0x91;
        }
        else if (parameterCode >= 0x40 && parameterCode <= 0x005F)
        {
            // ata log 0x92
            ataLogPageToRead = 0x92;
        }
        else if (parameterCode >= 0x60 && parameterCode <= 0x007F)
        {
            // ata log 0x93
            ataLogPageToRead = 0x93;
        }
        else if (parameterCode >= 0x80 && parameterCode <= 0x009F)
        {
            // ata log 0x94
            ataLogPageToRead = 0x94;
        }
        else if (parameterCode >= 0xA0 && parameterCode <= 0x00BF)
        {
            // ata log 0x95
            ataLogPageToRead = 0x95;
        }
        else if (parameterCode >= 0xC0 && parameterCode <= 0x00DF)
        {
            // ata log 0x96
            ataLogPageToRead = 0x96;
        }
        else if (parameterCode >= 0xE0 && parameterCode <= 0x00FF)
        {
            // ata log 0x97
            ataLogPageToRead = 0x97;
        }
        else if (parameterCode >= 0x100 && parameterCode <= 0x011F)
        {
            // ata log 0x98
            ataLogPageToRead = 0x98;
        }
        else if (parameterCode >= 0x120 && parameterCode <= 0x013F)
        {
            // ata log 0x99
            ataLogPageToRead = 0x99;
        }
        else if (parameterCode >= 0x140 && parameterCode <= 0x015F)
        {
            // ata log 0x9A
            ataLogPageToRead = 0x9A;
        }
        else if (parameterCode >= 0x160 && parameterCode <= 0x017F)
        {
            // ata log 0x9B
            ataLogPageToRead = 0x9B;
        }
        else if (parameterCode >= 0x180 && parameterCode <= 0x019F)
        {
            // ata log 0x9C
            ataLogPageToRead = 0x9C;
        }
        else if (parameterCode >= 0x1A0 && parameterCode <= 0x01BF)
        {
            // ata log 0x9D
            ataLogPageToRead = 0x9D;
        }
        else if (parameterCode >= 0x1C0 && parameterCode <= 0x01DF)
        {
            // ata log 0x9E
            ataLogPageToRead = 0x9E;
        }
        else if (parameterCode >= 0x1E0 && parameterCode <= 0x01FF)
        {
            // ata log 0x9F
            ataLogPageToRead = 0x9F;
        }
        else
        {
            // shouldn't get here...
            break;
        }
        // take the parameter code and figure out the offset we need to look at
        offsetOnATAPage = C_CAST(
            uint16_t,
            (parameterCode - (UINT16_C(2) * (ataLogPageToRead & 0x0F))) *
                UINT16_C(256)); // this should adjust the offset based on the parameter code and the page we're reading.
        // each iteration through the loop will read a different page for the request
        // Read all 16 sectors of the log page we need to, then go through and set up the data to return
        if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT5) ||
            (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT5)) // GPL
        {
            if (SUCCESS != ata_Read_Log_Ext(device, ataLogPageToRead, 0, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE,
                                            device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
            {
                // break and set an error code
                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                        scsiIoCtx->senseDataSize);
                break;
            }
        }
        else if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                 le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0) // SMART read log
        {
            if (SUCCESS != ata_SMART_Read_Log(device, ataLogPageToRead, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE))
            {
                // break and set an error code
                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                        scsiIoCtx->senseDataSize);
                break;
            }
        }
        else
        {
            // error...we shouldn't be here!
            break;
        }
        // need another for loop to go through the ATA log data we just read. Set the number of parameter's we've added.
        for (uint8_t perATAPageCounter = UINT8_C(0);
             parameterCounter <= numberOfParametersToReturn && perATAPageCounter < 32 &&
             offsetOnATAPage < (16 * LEGACY_DRIVE_SEC_SIZE) && offset < allocationLength;
             offsetOnATAPage += 256, ++parameterCounter, offset += 256, ++perATAPageCounter)
        {
            if (M_BytesTo2ByteValue(scsiIoCtx->pdata[offset + 0], scsiIoCtx->pdata[offset + 1]) != parameterCode
                /*check format and linking byte and length byte*/
                && scsiIoCtx->pdata[offset + 2] != 0x83 && scsiIoCtx->pdata[offset + 3] != 0xFC)
            {
                // Parameter code and control and length are not saved in the log so we need to set them up when
                // returning data to the host
                scsiIoCtx->pdata[offset + 0] = M_Byte1(parameterCode);
                scsiIoCtx->pdata[offset + 1] = M_Byte0(parameterCode);
                // set up parameter control byte
                scsiIoCtx->pdata[offset + 2] = 0x83;
                scsiIoCtx->pdata[offset + 3] = 0xFC;
                safe_memcpy(&scsiIoCtx->pdata[offset + 4], scsiIoCtx->dataLength - (offset + 4),
                            &hostLogData[offsetOnATAPage + 4], 0xFC);
            }
            else
            {
                // simple memcpy is all that's necessary
                safe_memcpy(&scsiIoCtx->pdata[offset], scsiIoCtx->dataLength - offset, &hostLogData[offsetOnATAPage],
                            256);
            }
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Log_Sense_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // we ignore the sp bit since it doesn't matter to us
    uint8_t  pageControl      = (scsiIoCtx->cdb[2] & 0xC0) >> 6;
    uint8_t  pageCode         = scsiIoCtx->cdb[2] & 0x3F;
    uint8_t  subpageCode      = scsiIoCtx->cdb[3];
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out unsupported bits
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 1) != 0) ||
        ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    else
    {
        // uint16_t allocationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (pageControl == LPC_CUMULATIVE_VALUES)
        {
            // check the pagecode
            switch (pageCode)
            {
            case 0x00: // supported pages
                switch (subpageCode)
                {
                case 0: // supported pages
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
                case 0xFF: // supported pages and subpages
#endif                     // SAT_SPEC_SUPPORTED
                    ret = translate_Supported_Log_Pages(
                        device, scsiIoCtx); // Update this page as additional pages of support are added!
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            case 0x03: // read error counters
                switch (subpageCode)
                {
                case 0:
                    if (device->drive_info.softSATFlags.deviceStatsPages.rotatingMediaStatisticsPageSupported ||
                        device->drive_info.softSATFlags.deviceStatsPages.generalErrorStatisticsSupported)
                    {
                        ret = translate_Read_Error_Counters_Log_0x03(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            case 0x0D: // temperature
                switch (subpageCode)
                {
                case 0:
                    if (device->drive_info.softSATFlags.deviceStatsPages.temperatureStatisticsSupported)
                    {
                        ret = translate_Temperature_Log_0x0D(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            case 0x0F: // application client
                switch (subpageCode)
                {
                case 0:
                    // First, make sure GPL or SMART are supported/enabled and then that the device supports the
                    // host-vendor specific logs
                    if ((device->drive_info.ata_Options.generalPurposeLoggingSupported ||
                         (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                          le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0)) &&
                        device->drive_info.softSATFlags.hostLogsSupported)
                    {
                        ret = translate_Application_Client_Log_Sense_0x0F(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#endif                                 // SAT_SPEC_SUPPORTED
            case LP_SELF_TEST_RESULTS: // self test results
                switch (subpageCode)
                {
                case 0:
                    if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                             le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT1) ||
                        (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                             le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT1))
                    {
                        ret = translate_Self_Test_Results_Log_0x10(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            case 0x11: // solid state media
                switch (subpageCode)
                {
                case 0:
                    if (device->drive_info.softSATFlags.deviceStatsPages.solidStateDeviceStatisticsSupported)
                    {
                        ret = translate_Solid_State_Media_Log_0x11(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            case 0x15: // background scan
                switch (subpageCode)
                {
                case 0:
                    if (device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported)
                    {
                        ret = translate_Background_Scan_Results_Log_0x15(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            case 0x19: // general statistics and performance
                switch (subpageCode)
                {
                case 0:
                    if (device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported)
                    {
                        ret = translate_General_Statistics_And_Performance_Log_0x19(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default:
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#endif                 // SAT_SPEC_SUPPORTED
            case 0x2F: // Informational Exceptions
                if (subpageCode == 0)
                {
                    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
                        le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT0) // check if SMART is supported
                    {
                        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                            le16_to_host(device->drive_info.IdentifyData.ata.Word085) &
                                BIT0) // check if SMART is enabled
                        {
                            if (parameterPointer == 0)
                            {
                                ret = translate_Informational_Exceptions_Log_Page_2F(device, scsiIoCtx);
                            }
                            else
                            {
                                fieldPointer = UINT16_C(5);
                                bitPointer   = UINT8_C(7);
                                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                                bitPointer, fieldPointer);
                                ret = NOT_SUPPORTED;
                                set_Sense_Data_For_Translation(
                                    scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                    device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                    senseKeySpecificDescriptor, 1);
                            }
                        }
                        else
                        {
                            ret = NOT_SUPPORTED;
                            set_Sense_Data_For_Translation(
                                scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x67, 0x0B,
                                device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                        }
                    }
                    else
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
                else
                {
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
            case 0x16: // ATA Pass-through results

                ret = translate_ATA_Passthrough_Results_Log_Page_16(device, scsiIoCtx);
                break;
#endif // SAT_SPEC_SUPPORTED
            default:
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(5);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
            }
        }
        else // page control
        {
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
    }
    return ret;
}

static eReturnValues translate_Application_Client_Log_Select_0x0F(tDevice*   device,
                                                                  ScsiIoCtx* scsiIoCtx,
                                                                  uint8_t*   ptrData,
                                                                  bool       parameterCodeReset,
                                                                  bool       saveParameters,
                                                                  uint16_t   totalParameterListLength)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (!saveParameters) // this must be set since we will be writing to the drive
    {
        bitPointer   = UINT8_C(0);
        fieldPointer = UINT16_C(1);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (parameterCodeReset)
    {
        // Support the "parameterCodeReset" option which will write zeros to all application client parameters.
        // We need to preserve the log parameters in the data (just the parameter header...4 bytes. A simple memset to
        // zero would probably work, but this way would be better for when the data is supposed to be read back
        uint8_t ataLogPageToWrite = UINT8_C(0x90); // SAT only uses pages 90h - 9Fh. 80h - 8Fh are left alone
        for (uint16_t parameterCode = UINT16_C(0); ataLogPageToWrite <= 0x9F && parameterCode <= 0x01FF;
             ++ataLogPageToWrite, ++parameterCode)
        {
            DECLARE_ZERO_INIT_ARRAY(
                uint8_t, hostLogData,
                SIZE_T_C(16) *
                    LEGACY_DRIVE_SEC_SIZE); // this memory should be all zeros, but doing a memset to be certain
            safe_memset(hostLogData, SIZE_T_C(16) * LEGACY_DRIVE_SEC_SIZE, 0, SIZE_T_C(16) * LEGACY_DRIVE_SEC_SIZE);
            // loop through and set the parameter bytes up correctly.
            for (uint16_t perATAPageCounter = UINT16_C(0), offset = UINT16_C(0); perATAPageCounter < UINT16_C(32);
                 ++perATAPageCounter, offset += UINT16_C(256))
            {
                // Parameter code and control and length are not saved in the log so we need to set them up when
                // returning data to the host
                hostLogData[offset + 0] = M_Byte1(parameterCode);
                hostLogData[offset + 1] = M_Byte0(parameterCode);
                // set up parameter control byte
                hostLogData[offset + 2] = 0x83;
                hostLogData[offset + 3] = 0xFC;
                // all other bytes will be left as zeros
            }
            // now write it to the drive
            if (scsiIoCtx->device->drive_info.ata_Options.generalPurposeLoggingSupported) // GPL
            {
                if (SUCCESS != ata_Write_Log_Ext(device, ataLogPageToWrite, 0, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE,
                                                 device->drive_info.ata_Options.readLogWriteLogDMASupported, false))
                {
                    // break and set an error code
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                    break;
                }
            }
            else if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                     le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0) // SMART read log
            {
                if (SUCCESS !=
                    ata_SMART_Write_Log(device, ataLogPageToWrite, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE, false))
                {
                    // break and set an error code
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                    break;
                }
            }
            else
            {
                // error...we shouldn't be here! This condition will be caught in the translate_scsi_Log_Select function
                break;
            }
        }
    }
    else if (totalParameterListLength >
             0) // this must be non-zero to enter here and do anything, otherwise it is not an error and nothing happens
    {
        // look at the parameter data. Determine the first parameter that is to be written.
        // SATL is required to preserve any data the host isn't requesting to modify
        // Also verify the page code is correct (length can vary depending on how many parameters we were sent)
        bool     disableSave   = ptrData[0] & BIT7;
        bool     subpageFormat = ptrData[0] & BIT6;
        uint8_t  pageCode      = ptrData[0] & 0x3F;
        uint8_t  subpageCode   = ptrData[1];
        uint16_t parameterListLength =
            M_BytesTo2ByteValue(ptrData[2], ptrData[3]); // from the data buffer sent to the SATL
        if (((fieldPointer = 0) == 0 && (bitPointer = 7) != 0 && disableSave) ||
            ((fieldPointer = 0) == 0 && (bitPointer = 6) != 0 && subpageFormat) ||
            ((fieldPointer = 0) == 0 && (bitPointer = 5) != 0 && pageCode != 0x0F) ||
            ((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && subpageCode != 0))
        {
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                            fieldPointer);
            // Error, invalid field in parameter list
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        else if (parameterListLength > 4 && totalParameterListLength > 4)
        {
            uint16_t parameterDataOffset = UINT16_C(4); // past the 4 byte header
            uint8_t  parameterLength =
                UINT8_C(0xFC); // ideally, this won't ever change if the command was formatted correctly.
            // Loop through the parameters and check lengths, parameter codes, and parameter control bytes to make sure
            // they are valid
            for (parameterDataOffset = UINT16_C(4);
                 parameterDataOffset < parameterListLength && parameterDataOffset < totalParameterListLength;
                 parameterDataOffset += C_CAST(uint16_t, parameterLength) + UINT16_C(4))
            {
                uint16_t parameterCode =
                    M_BytesTo2ByteValue(ptrData[parameterDataOffset + 0], ptrData[parameterDataOffset + 1]);
                bool disableUpdate        = ptrData[parameterDataOffset + 2] & BIT7;
                bool parameterDisableSave = ptrData[parameterDataOffset + 2] &
                                            BIT6; // obsolete. Likely why this is now in byte zero for the whole page
                bool targetSaveDisable         = ptrData[parameterDataOffset + 2] & BIT5;
                bool enableThresholdComparison = ptrData[parameterDataOffset + 2] & BIT4;
                // uint8_t thresholdCriteriaMet = get_bit_range_uint8(ptrData[parameterDataOffset + 2], 3, 2);//SATL
                // ignores this field since ETC must be zero
                uint8_t formatAndLinking = get_bit_range_uint8(ptrData[parameterDataOffset + 2], 1, 0);
                parameterLength          = ptrData[parameterDataOffset + 3];

                if (((fieldPointer = parameterDataOffset) != 0 && (bitPointer = 7) != 0 && parameterCode > 0x01FF) ||
                    ((fieldPointer = parameterDataOffset + 2) != 0 && (bitPointer = 7) != 0 && !disableUpdate) ||
                    ((fieldPointer = parameterDataOffset + 2) != 0 && (bitPointer = 6) != 0 && parameterDisableSave) ||
                    ((fieldPointer = parameterDataOffset + 2) != 0 && (bitPointer = 5) != 0 && targetSaveDisable) ||
                    ((fieldPointer = parameterDataOffset + 2) != 0 && (bitPointer = 4) != 0 &&
                     enableThresholdComparison) ||
                    ((fieldPointer = parameterDataOffset + 2) != 0 && (bitPointer = 1) != 0 &&
                     formatAndLinking != 0x03) ||
                    ((fieldPointer = parameterDataOffset + 3) != 0 && (bitPointer = 7) != 0 && parameterLength != 0xFC))
                {
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return ret;
                }
            }
            // we know the data we were sent is good, so time to start writing it to the correct log page.
            // NOTE: For simplicity, we'll read an ATA Host Log page, modify data in it as necessary, then write it back
            // to keep this fairly simple
            parameterDataOffset = UINT16_C(4);
            while (parameterDataOffset < parameterListLength && parameterDataOffset < totalParameterListLength)
            {
                uint16_t parameterCode =
                    M_BytesTo2ByteValue(ptrData[parameterDataOffset + 0], ptrData[parameterDataOffset + 1]);
                uint8_t* hostLogData      = M_NULLPTR;
                uint16_t offsetOnATAPage  = UINT16_C(0);
                uint8_t  ataLogPageToRead = UINT8_C(0);

                parameterLength = ptrData[parameterDataOffset + 3];
                hostLogData     = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(SIZE_T_C(16) * LEGACY_DRIVE_SEC_SIZE,
                                                                                   sizeof(uint8_t),
                                                                                   device->os_info.minimumAlignment));
                if (hostLogData == M_NULLPTR)
                {
                    return MEMORY_FAILURE;
                }
                // The parameter headers may be written to the host vendor logs if they've been written...but if not,
                // then we'll need to create the header. It's much simpler to take the parameters and write directly to
                // the logs for a SATL, but this may not happen in this software implementation, so we need to handle
                // that.

                if (/*parameterCode >= 0x00 &&*/ parameterCode <= 0x001F)
                {
                    // ata log 0x90
                    ataLogPageToRead = 0x90;
                }
                else if (parameterCode >= 0x20 && parameterCode <= 0x003F)
                {
                    // ata log 0x91
                    ataLogPageToRead = 0x91;
                }
                else if (parameterCode >= 0x40 && parameterCode <= 0x005F)
                {
                    // ata log 0x92
                    ataLogPageToRead = 0x92;
                }
                else if (parameterCode >= 0x60 && parameterCode <= 0x007F)
                {
                    // ata log 0x93
                    ataLogPageToRead = 0x93;
                }
                else if (parameterCode >= 0x80 && parameterCode <= 0x009F)
                {
                    // ata log 0x94
                    ataLogPageToRead = 0x94;
                }
                else if (parameterCode >= 0xA0 && parameterCode <= 0x00BF)
                {
                    // ata log 0x95
                    ataLogPageToRead = 0x95;
                }
                else if (parameterCode >= 0xC0 && parameterCode <= 0x00DF)
                {
                    // ata log 0x96
                    ataLogPageToRead = 0x96;
                }
                else if (parameterCode >= 0xE0 && parameterCode <= 0x00FF)
                {
                    // ata log 0x97
                    ataLogPageToRead = 0x97;
                }
                else if (parameterCode >= 0x100 && parameterCode <= 0x011F)
                {
                    // ata log 0x98
                    ataLogPageToRead = 0x98;
                }
                else if (parameterCode >= 0x120 && parameterCode <= 0x013F)
                {
                    // ata log 0x99
                    ataLogPageToRead = 0x99;
                }
                else if (parameterCode >= 0x140 && parameterCode <= 0x015F)
                {
                    // ata log 0x9A
                    ataLogPageToRead = 0x9A;
                }
                else if (parameterCode >= 0x160 && parameterCode <= 0x017F)
                {
                    // ata log 0x9B
                    ataLogPageToRead = 0x9B;
                }
                else if (parameterCode >= 0x180 && parameterCode <= 0x019F)
                {
                    // ata log 0x9C
                    ataLogPageToRead = 0x9C;
                }
                else if (parameterCode >= 0x1A0 && parameterCode <= 0x01BF)
                {
                    // ata log 0x9D
                    ataLogPageToRead = 0x9D;
                }
                else if (parameterCode >= 0x1C0 && parameterCode <= 0x01DF)
                {
                    // ata log 0x9E
                    ataLogPageToRead = 0x9E;
                }
                else if (parameterCode >= 0x1E0 && parameterCode <= 0x01FF)
                {
                    // ata log 0x9F
                    ataLogPageToRead = 0x9F;
                }
                else
                {
                    // shouldn't get here...
                    break;
                }
                // take the parameter code and figure out the offset we need to look at
                offsetOnATAPage = C_CAST(uint16_t, (parameterCode - (UINT16_C(2) * (ataLogPageToRead & 0x0F))) *
                                                       UINT16_C(256)); // this should adjust the offset based on the
                                                                       // parameter code and the page we're reading.
                // each iteration through the loop will read a different page for the request
                // Read all 16 sectors of the log page we need to, then go through and set up the data to return
                if (scsiIoCtx->device->drive_info.ata_Options.generalPurposeLoggingSupported) // GPL
                {
                    if (SUCCESS != ata_Read_Log_Ext(device, ataLogPageToRead, 0, hostLogData,
                                                    16 * LEGACY_DRIVE_SEC_SIZE,
                                                    device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                    {
                        // break and set an error code
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                        safe_free_aligned(&hostLogData);
                        break;
                    }
                }
                else if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0) // SMART read log
                {
                    if (SUCCESS !=
                        ata_SMART_Read_Log(device, ataLogPageToRead, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE))
                    {
                        // break and set an error code
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                        safe_free_aligned(&hostLogData);
                        break;
                    }
                }
                else
                {
                    // error...we shouldn't be here!
                    safe_free_aligned(&hostLogData);
                    break;
                }
                // need another for loop to go through the ATA log data we just read so that we can modify the data
                // before we write it.
                for (uint8_t perATAPageCounter = C_CAST(uint8_t, offsetOnATAPage / UINT16_C(256));
                     perATAPageCounter < UINT8_C(32) && offsetOnATAPage < (UINT16_C(16) * LEGACY_DRIVE_SEC_SIZE) &&
                     parameterDataOffset < parameterListLength && parameterDataOffset < totalParameterListLength;
                     offsetOnATAPage += UINT16_C(256),
                             parameterDataOffset += C_CAST(uint16_t, parameterLength + UINT16_C(4)),
                             ++perATAPageCounter)
                {
                    // first, we need to make sure that we write the correct parameters in the right place on the page
                    parameterCode =
                        M_BytesTo2ByteValue(ptrData[parameterDataOffset + 0], ptrData[parameterDataOffset + 1]);
                    parameterLength = ptrData[parameterDataOffset + 3];
                    offsetOnATAPage = C_CAST(uint16_t, (parameterCode - (UINT16_C(2) * (ataLogPageToRead & 0x0F))) *
                                                           UINT16_C(256)); // this should adjust the offset based on the
                                                                           // parameter code and the page we're reading.
                    perATAPageCounter = C_CAST(uint8_t, offsetOnATAPage / UINT16_C(256));
                    if (perATAPageCounter > UINT8_C(32))
                    {
                        // parameters aren't exactly next to each other, so we may have gotten to another page so break
                        // out. The next time through the outer loop, we'll pick it up
                        break;
                    }
                    // simple memcpy is all that's necessary. We're copying all of the parameter data (number, control,
                    // length, etc) into the ATA log buffer before we write
                    safe_memcpy(&hostLogData[offsetOnATAPage], (16 * LEGACY_DRIVE_SEC_SIZE) - offsetOnATAPage,
                                &scsiIoCtx->pdata[parameterDataOffset], 256);
                }
                if (device->drive_info.ata_Options.generalPurposeLoggingSupported) // GPL
                {
                    if (SUCCESS != ata_Write_Log_Ext(device, ataLogPageToRead, 0, hostLogData,
                                                     16 * LEGACY_DRIVE_SEC_SIZE,
                                                     device->drive_info.ata_Options.readLogWriteLogDMASupported, false))
                    {
                        // break and set an error code
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                        safe_free_aligned(&hostLogData);
                        break;
                    }
                }
                else if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                         le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0) // SMART read log
                {
                    if (SUCCESS !=
                        ata_SMART_Write_Log(device, ataLogPageToRead, hostLogData, 16 * LEGACY_DRIVE_SEC_SIZE, false))
                    {
                        // break and set an error code
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                scsiIoCtx->senseDataSize);
                        safe_free_aligned(&hostLogData);
                        break;
                    }
                }
                else
                {
                    // error...we shouldn't be here!
                    safe_free_aligned(&hostLogData);
                    break;
                }
                safe_free_aligned(&hostLogData);
            }
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Log_Select_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                 = SUCCESS;
    bool          parameterCodeReset  = false;
    bool          saveParameters      = false;
    uint8_t       pageControl         = UINT8_C(0);
    uint8_t       pageCode            = UINT8_C(0);
    uint8_t       subpageCode         = UINT8_C(0);
    uint16_t      parameterListLength = UINT16_C(0);

    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);

    parameterCodeReset  = scsiIoCtx->cdb[1] & BIT1;
    saveParameters      = scsiIoCtx->cdb[1] & BIT0;
    pageControl         = (scsiIoCtx->cdb[2] & 0xC0) >> 6;
    pageCode            = scsiIoCtx->cdb[2] & 0x3F;
    subpageCode         = scsiIoCtx->cdb[3];
    parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);

    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 2) != 0) ||
        ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0) || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) ||
        ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    else
    {
        if (pageControl == 0x01) // cumulative values only
        {
            // SPC3 and earlier the pageCode and Subpage code are reserved in the CDB....But that doesn't matter because
            // SAT2 and higher conform to SPC4 and only SAT2+ define this command's translation
            switch (pageCode)
            {
                // TODO: If we get page 0, this means there are 1 or more pages in the parameter data to go through
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
            case 0x0F: // application client
                switch (subpageCode)
                {
                case 0:
                    // First, make sure GPL or SMART are supported/enabled and then that the device supports the
                    // host-vendor specific logs
                    if ((device->drive_info.ata_Options.generalPurposeLoggingSupported ||
                         (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
                          le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0)) &&
                        device->drive_info.softSATFlags.hostLogsSupported)
                    {
                        ret = translate_Application_Client_Log_Select_0x0F(device, scsiIoCtx, scsiIoCtx->pdata,
                                                                           parameterCodeReset, saveParameters,
                                                                           parameterListLength);
                    }
                    else // invalid page code because application client log is not supported
                    {
                        fieldPointer = UINT16_C(2);
                        bitPointer   = UINT8_C(5);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                    break;
                default: // invalid subpage code
                    fieldPointer = UINT16_C(3);
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#endif               // SAT_SPEC_SUPPORTED
            default: // not a supported page code
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(5);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
            }
        }
        else // page control is not a supported value
        {
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(7);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Unmap_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // not supporting the ancor bit
    // group number should be zero
    uint16_t parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0) || ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) ||
        ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0) || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0) ||
        ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            if (fieldPointer == 6 && bitPointer < 5)
            {
                bitPointer = UINT8_C(5); // setting group number as invalid field
            }
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (parameterListLength > 0 && parameterListLength < 8)
    {
        fieldPointer = UINT16_C(7);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // parameter list length error
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x1A, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    else if (parameterListLength != 0 && scsiIoCtx->pdata)
    {
        // process the contents of the parameter data and send some commands to the drive
        // uint16_t unmapDataLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[0], scsiIoCtx->pdata[1]);
        uint16_t unmapBlockDescriptorLength =
            (M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]) / 16) *
            16; // this can be set to zero, which is NOT an error. Also, I'm making sure this is a multiple of 16 to
                // avoid partial block descriptors-TJE
        if (unmapBlockDescriptorLength > 0)
        {
            uint16_t trimBufferSize = UINT16_C(1);
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word105)))
            {
                trimBufferSize = le16_to_host(device->drive_info.IdentifyData.ata.Word105);
            }
            uint8_t* trimBuffer = C_CAST(
                uint8_t*, safe_calloc_aligned(
                              uint16_to_sizet(trimBufferSize) * LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t),
                              device->os_info.minimumAlignment)); // allocate the max size the device supports...we'll
                                                                  // fill in as much as we need to
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
            bool     useXL                  = device->drive_info.softSATFlags.dataSetManagementXLSupported;
            uint8_t  maxDescriptorsPerBlock = device->drive_info.softSATFlags.dataSetManagementXLSupported ? 32 : 64;
            uint8_t  descriptorSize         = device->drive_info.softSATFlags.dataSetManagementXLSupported ? 16 : 8;
            uint64_t maxUnmapRangePerDescriptor =
                device->drive_info.softSATFlags.dataSetManagementXLSupported ? UINT64_MAX : UINT16_MAX;
#else  // SAT_SPEC_SUPPORTED
            bool useXL = false uint8_t maxDescriptorsPerBlock     = UINT8_C(64);
            uint8_t                    descriptorSize             = UINT8_C(8);
            uint64_t                   maxUnmapRangePerDescriptor = UINT16_MAX;
#endif // SAT_SPEC_SUPPORTED
       // need to check to make sure there weren't any truncated block descriptors before we begin
            uint16_t minBlockDescriptorLength =
                C_CAST(uint16_t, M_Min(unmapBlockDescriptorLength + UINT16_C(8), parameterListLength));
            uint16_t unmapBlockDescriptorIter = UINT16_C(8);
            uint64_t numberOfLBAsToTRIM =
                0; // this will be checked later to make sure it isn't greater than what we reported on the VPD pages
            uint16_t numberOfBlockDescriptors =
                0; // this will be checked later to make sure it isn't greater than what we reported on the VPD pages
            uint16_t ataTrimOffset = UINT16_C(0);

            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word105)))
            {
                trimBufferSize = le16_to_host(device->drive_info.IdentifyData.ata.Word105);
            }
            trimBuffer = C_CAST(
                uint8_t*,
                calloc_aligned(uint16_to_sizet(trimBufferSize) * LEGACY_DRIVE_SEC_SIZE, sizeof(uint8_t),
                               device->os_info.minimumAlignment)); // allocate the max size the device supports...we'll
                                                                   // fill in as much as we need to

            if (!trimBuffer)
            {
                // lets just set this error for now...-TJE
                fieldPointer = UINT16_C(7);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x1A, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return MEMORY_FAILURE;
            }
            // start building the buffer to transfer with data set management
            for (; unmapBlockDescriptorIter < minBlockDescriptorLength;
                 unmapBlockDescriptorIter += 16, numberOfBlockDescriptors++)
            {
                bool     exitLoop                 = false;
                uint64_t unmapLogicalBlockAddress = M_BytesTo8ByteValue(
                    scsiIoCtx->pdata[unmapBlockDescriptorIter + 0], scsiIoCtx->pdata[unmapBlockDescriptorIter + 1],
                    scsiIoCtx->pdata[unmapBlockDescriptorIter + 2], scsiIoCtx->pdata[unmapBlockDescriptorIter + 3],
                    scsiIoCtx->pdata[unmapBlockDescriptorIter + 4], scsiIoCtx->pdata[unmapBlockDescriptorIter + 5],
                    scsiIoCtx->pdata[unmapBlockDescriptorIter + 6], scsiIoCtx->pdata[unmapBlockDescriptorIter + 7]);
                uint32_t unmapNumberOfLogicalBlocks = M_BytesTo4ByteValue(
                    scsiIoCtx->pdata[unmapBlockDescriptorIter + 8], scsiIoCtx->pdata[unmapBlockDescriptorIter + 9],
                    scsiIoCtx->pdata[unmapBlockDescriptorIter + 10], scsiIoCtx->pdata[unmapBlockDescriptorIter + 11]);
                if (unmapNumberOfLogicalBlocks == 0)
                {
                    // nothing to unmap...
                    continue;
                }
                // increment this so we can check if we were given more to do than we support (to be checked after the
                // loop)
                numberOfLBAsToTRIM += unmapNumberOfLogicalBlocks;
                // check we aren't trying to go over the end of the drive
                if (unmapLogicalBlockAddress > device->drive_info.deviceMaxLba)
                {
                    fieldPointer = unmapBlockDescriptorIter + 0;
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    ret = FAILURE;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x21, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    exitLoop = true;
                    break;
                }
                else if (unmapLogicalBlockAddress + unmapNumberOfLogicalBlocks > device->drive_info.deviceMaxLba)
                {
                    fieldPointer = unmapBlockDescriptorIter + 8;
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    ret = FAILURE;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x21, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    exitLoop = true;
                    break;
                }
                // check that we haven't had too many block descriptors yet
                if (numberOfBlockDescriptors > (maxDescriptorsPerBlock * trimBufferSize))
                {
                    // not setting sense key specific information because it's not clear in this condition what error we
                    // should point to
                    ret = FAILURE;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    exitLoop = true;
                    break;
                }
                // check that we haven't been asked to TRIM more LBAs than we care to support in this code
                if (numberOfLBAsToTRIM > (M_STATIC_CAST(uint64_t, maxDescriptorsPerBlock) *
                                          M_STATIC_CAST(uint64_t, trimBufferSize) * maxUnmapRangePerDescriptor))
                {
                    // not setting sense key specific information because it's not clear in this condition what error we
                    // should point to
                    ret = FAILURE;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                    exitLoop = true;
                    break;
                }
                // now that we've done all of our checks so far, start setting up the buffer
                // break this block descriptor into multiple ATA entries if necessary
                while (unmapNumberOfLogicalBlocks > 0)
                {
                    // lba
                    trimBuffer[ataTrimOffset + 0] = M_Byte5(unmapLogicalBlockAddress);
                    trimBuffer[ataTrimOffset + 1] = M_Byte4(unmapLogicalBlockAddress);
                    trimBuffer[ataTrimOffset + 2] = M_Byte3(unmapLogicalBlockAddress);
                    trimBuffer[ataTrimOffset + 3] = M_Byte2(unmapLogicalBlockAddress);
                    trimBuffer[ataTrimOffset + 4] = M_Byte1(unmapLogicalBlockAddress);
                    trimBuffer[ataTrimOffset + 5] = M_Byte0(unmapLogicalBlockAddress);
                    // range (set for XL vs non-XL commands!)
                    if (useXL)
                    {
                        trimBuffer[ataTrimOffset + 6]  = RESERVED;
                        trimBuffer[ataTrimOffset + 7]  = RESERVED;
                        trimBuffer[ataTrimOffset + 8]  = M_Byte7(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 9]  = M_Byte6(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 10] = M_Byte5(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 11] = M_Byte4(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 12] = M_Byte3(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 13] = M_Byte2(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 14] = M_Byte1(unmapNumberOfLogicalBlocks);
                        trimBuffer[ataTrimOffset + 15] = M_Byte0(unmapNumberOfLogicalBlocks);
                    }
                    else
                    {
                        if (unmapNumberOfLogicalBlocks > UINT16_MAX)
                        {
                            trimBuffer[ataTrimOffset + 6] = UINT8_MAX;
                            trimBuffer[ataTrimOffset + 7] = UINT8_MAX;
                            unmapNumberOfLogicalBlocks -= UINT16_MAX;
                            unmapLogicalBlockAddress += UINT16_MAX;
                        }
                        else
                        {
                            trimBuffer[ataTrimOffset + 6] = M_Byte1(unmapNumberOfLogicalBlocks);
                            trimBuffer[ataTrimOffset + 7] = M_Byte0(unmapNumberOfLogicalBlocks);
                        }
                    }
                    // now increment the ataTrimOffset
                    ataTrimOffset += descriptorSize;
                    // check if the ATA Trim buffer is full...if it is and there are more or potentially more block
                    // descriptors, send the command now
                    if ((ataTrimOffset > (trimBufferSize * LEGACY_DRIVE_SEC_SIZE)) &&
                        ((unmapBlockDescriptorIter + UINT16_C(16)) < minBlockDescriptorLength))
                    {
                        if (SUCCESS == ata_Data_Set_Management(device, true, trimBuffer,
                                                               trimBufferSize * LEGACY_DRIVE_SEC_SIZE, useXL))
                        {
                            // clear the buffer for reuse
                            safe_memset(trimBuffer, uint16_to_sizet(trimBufferSize) * LEGACY_DRIVE_SEC_SIZE, 0,
                                        uint16_to_sizet(trimBufferSize) * LEGACY_DRIVE_SEC_SIZE);
                            // reset the ataTrimOffset
                            ataTrimOffset = 0;
                        }
                        else
                        {
                            ret = FAILURE;
                            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                                    scsiIoCtx->senseDataSize);
                            exitLoop = true;
                            break;
                        }
                    }
                }
                if (exitLoop)
                {
                    break;
                }
            }
            if (ret == SUCCESS)
            {
                // send the data set management command with whatever is in the trim buffer at this point (all zeros is
                // safe to send if we do get that)
                if (SUCCESS !=
                    ata_Data_Set_Management(device, true, trimBuffer, trimBufferSize * LEGACY_DRIVE_SEC_SIZE, useXL))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                            scsiIoCtx->senseDataSize);
                }
            }
            safe_free_aligned(&trimBuffer);
        }
    }
    return ret;
}
// mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
// dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for
// short, or 16 for long (when longLBABit is set to true)
static eReturnValues translate_Mode_Sense_Control_0Ah(tDevice*   device,
                                                      ScsiIoCtx* scsiIoCtx,
                                                      uint8_t    pageControl,
                                                      bool       returnDataBlockDescriptor,
                                                      bool       longLBABit,
                                                      uint8_t*   dataBlockDescriptor,
                                                      bool       longHeader,
                                                      uint8_t*   modeParameterHeader,
                                                      uint16_t   allocationLength)
{
    eReturnValues ret         = SUCCESS;
    uint8_t*      controlPage = M_NULLPTR; // will be allocated later
    uint16_t pageLength = UINT16_C(12);    // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    controlPage = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!controlPage)
    {
        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&controlPage[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&controlPage[headerLength], pageLength - headerLength, dataBlockDescriptor, blockDescLength);
    }
    // set the remaining part of the page up
    controlPage[offset + 0] = 0x0A;
    controlPage[offset + 1] = 0x0A;
    if (pageControl == 0x01)
    {
        // nothing is required to be changeable so don't report changes for anything other than the d_sense bit
        controlPage[offset + 2] |= BIT2; // TST = 0, TMF_only = 0, dsense = 1, gltsd = 0, rlec = 0
    }
    else
    {
        uint16_t smartSelfTestTime = UINT16_C(0);
        if (pageControl == 0 || pageControl == 0x3) // current or saved page
        {
            if (device->drive_info.softSATFlags.senseDataDescriptorFormat)
            {
                controlPage[offset + 2] |= BIT2; // TST = 0, TMF_only = 0, dsense = 1, gltsd = 0, rlec = 0
            }
            else
            {
                controlPage[offset + 2] = 0; // TST = 0, TMF_only = 0, dsense = 0, gltsd = 0, rlec = 0
            }
        }
        else if (pageControl == 0x2) // default
        {
            // default to d_sense is zero for fixed format data. TODO: should this be a compile time flag?
            controlPage[offset + 2] = 0; // TST = 0, TMF_only = 0, dsense = 0, gltsd = 0, rlec = 0
        }
        controlPage[offset + 3] |= BIT4 | BIT1; // queued algorithm modifier = 1, nuar = 0, QErr = 01,
        controlPage[offset + 4] = 0;            // rac = 0, UA_INTLCK_CTRL = 0, swp = 0
        controlPage[offset + 5] = 0;            // ato = 0, tas = 0, atmpe = 0, rwwp = 0, autoload mode = 0
        controlPage[offset + 6] = OBSOLETE;
        controlPage[offset + 7] = OBSOLETE;
        controlPage[offset + 8] = 0xFF; // busy timeout period
        controlPage[offset + 9] = 0xFF; // busy timeout period

        if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT1) &&
            (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                 le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT1) &&
            (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT0))
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, smartData, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == ata_SMART_Read_Data(device, smartData, LEGACY_DRIVE_SEC_SIZE))
            {
                if (smartData[373] != UINT8_MAX)
                {
                    smartSelfTestTime = UINT16_C(60) * C_CAST(uint16_t, smartData[373]);
                }
                else
                {
                    smartSelfTestTime =
                        C_CAST(uint16_t, M_Min(UINT16_MAX, ((C_CAST(uint16_t, smartData[376]) * UINT16_C(256)) +
                                                            C_CAST(uint16_t, smartData[375])) *
                                                               UINT16_C(60)));
                }
            }
        }
        controlPage[offset + 10] = M_Byte1(smartSelfTestTime);
        controlPage[offset + 11] = M_Byte0(smartSelfTestTime);
    }
    // set the mode data length
    if (longHeader)
    {
        controlPage[0] = M_Byte1(pageLength - UINT16_C(2));
        controlPage[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        controlPage[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, controlPage, M_Min(pageLength, allocationLength));
    }
    safe_free(&controlPage);
    return ret;
}

static eReturnValues translate_Mode_Sense_PATA_Control_0Ah_F1h(ScsiIoCtx* scsiIoCtx,
                                                               uint8_t    pageControl,
                                                               bool       returnDataBlockDescriptor,
                                                               bool       longLBABit,
                                                               uint8_t*   dataBlockDescriptor,
                                                               bool       longHeader,
                                                               uint8_t*   modeParameterHeader,
                                                               uint16_t   allocationLength)
{
    eReturnValues ret             = SUCCESS;
    uint8_t*      pataControlPage = M_NULLPTR; // will be allocated later
    uint16_t      pageLength = UINT16_C(8); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    pataControlPage = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!pataControlPage)
    {
        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&pataControlPage[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&pataControlPage[headerLength], pageLength - headerLength, dataBlockDescriptor, blockDescLength);
    }
    // set the remaining part of the page up
    pataControlPage[offset + 0] = 0x0A;
    pataControlPage[offset + 1] = 0xF1; // subpage
    pataControlPage[offset + 2] = 0x00; // len
    pataControlPage[offset + 3] = 0x04; // len
    if (pageControl == 0x01)
    {
        // only show what is supported to be changed!
        // Don't actually change the mode though since this can cause a loss of communication with the OS/driver/or HBA!
    }
    else
    {
        // current saved and default pages will be the same since we don't allow changes...
        // read the current mode and use that as a "max" value. Then set bits for everything below that.
        uint16_t currentMode = UINT16_C(0); // 0-4 = PIO modes, 5-7 = SWDMA, 8-10 = MWDMA, 11-18 = UDMA
        bool     word88Valid = false;
        if (is_ATA_Identify_Word_Valid(scsiIoCtx->device->drive_info.IdentifyData.ata.Word053) &&
            (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word053) & BIT2))
        {
            word88Valid = true;
        }
        if (word88Valid && is_ATA_Identify_Word_Valid(scsiIoCtx->device->drive_info.IdentifyData.ata.Word088) &&
            le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word088) & 0x7F00)
        {
            // UDMA mode is set
            uint8_t udmaMode =
                C_CAST(uint8_t, (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word088) & 0x7F00) >> 8);
            currentMode = 11;
            while (udmaMode != 1)
            {
                udmaMode = udmaMode >> 1;
                ++currentMode;
            }
        }
        else if (is_ATA_Identify_Word_Valid(scsiIoCtx->device->drive_info.IdentifyData.ata.Word063) &&
                 le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word063) & 0x0700)
        {
            uint8_t mwdmaMode =
                C_CAST(uint8_t, (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word063) & 0x0700) >> 8);
            // MWDMA mode is set
            currentMode = 8;
            while (mwdmaMode != 1)
            {
                mwdmaMode = mwdmaMode >> 1;
                ++currentMode;
            }
        }
        else if (is_ATA_Identify_Word_Valid(scsiIoCtx->device->drive_info.IdentifyData.ata.Word062) &&
                 le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word062) & 0x0700)
        {
            uint8_t swdmaMode =
                C_CAST(uint8_t, (le16_to_host(scsiIoCtx->device->drive_info.IdentifyData.ata.Word062) & 0x0700) >> 8);
            // SWDMA mode is set (so obsolete this shouldn't ever happen)
            currentMode = 7;
            while (swdmaMode != 1)
            {
                swdmaMode = swdmaMode >> 1;
                ++currentMode;
            }
        }
        else
        {
            // PIO mode is set...we don't know the exact speed so leave value at zero
        }
        // now set up the current transfer mode. FYI, the code is the same for "changeable" values except there are no
        // break statements. But we don't support that here...-TJE
        switch (currentMode)
        {
        case 18: // udma 6
            pataControlPage[offset + 5] |= BIT6;
            break;
        case 17: // udma 5
            pataControlPage[offset + 5] |= BIT5;
            break;
        case 16: // udma 4
            pataControlPage[offset + 5] |= BIT4;
            break;
        case 15: // udma 3
            pataControlPage[offset + 5] |= BIT3;
            break;
        case 14: // udma 2
            pataControlPage[offset + 5] |= BIT2;
            break;
        case 13: // udma 1
            pataControlPage[offset + 5] |= BIT1;
            break;
        case 12: // udma 0
            pataControlPage[offset + 5] |= BIT0;
            break;
        case 11: // mwdma 2
            pataControlPage[offset + 4] |= BIT6;
            break;
        case 10: // mwdma 1
            pataControlPage[offset + 4] |= BIT5;
            break;
        case 9: // mwdma 0
            pataControlPage[offset + 4] |= BIT4;
            break;
        default:
            // All other cases are unknown since we cannot report or read them from the device...so don't do anything.
            break;
        }
    }
    // set the mode data length
    if (longHeader)
    {
        pataControlPage[0] = M_Byte1(pageLength - UINT16_C(2));
        pataControlPage[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        pataControlPage[0] = C_CAST(uint8_t, pageLength - UINT16_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, pataControlPage, M_Min(pageLength, allocationLength));
    }
    safe_free(&pataControlPage);
    return ret;
}

static eReturnValues translate_Mode_Sense_Control_Extension_0Ah_01h(ScsiIoCtx* scsiIoCtx,
                                                                    uint8_t    pageControl,
                                                                    bool       returnDataBlockDescriptor,
                                                                    bool       longLBABit,
                                                                    uint8_t*   dataBlockDescriptor,
                                                                    bool       longHeader,
                                                                    uint8_t*   modeParameterHeader,
                                                                    uint16_t   allocationLength)
{
    eReturnValues ret            = SUCCESS;
    uint8_t*      controlExtPage = M_NULLPTR; // will be allocated later
    uint16_t pageLength = UINT16_C(32); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    controlExtPage = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!controlExtPage)
    {
        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&controlExtPage[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&controlExtPage[headerLength], pageLength - headerLength, dataBlockDescriptor, blockDescLength);
    }
    // set the remaining part of the page up
    controlExtPage[offset + 0] = 0x0A;
    controlExtPage[offset + 0] |= BIT6; // set spf bit
    controlExtPage[offset + 1] = 0x01;
    controlExtPage[offset + 2] = 0x00;
    controlExtPage[offset + 3] = 0x1C;
    if (pageControl != 0x01) // default, current, & saved...nothing on this page will be changeable - TJE
    {
        controlExtPage[offset + 4]  = 0; // dlc = 0, tcmos = 0, scsip = 0, ialuae = 0
        controlExtPage[offset + 5]  = 0; // initial command priority = 0 (for no/vendor spcific priority)
        controlExtPage[offset + 6]  = SPC3_SENSE_LEN;
        controlExtPage[offset + 7]  = RESERVED;
        controlExtPage[offset + 8]  = RESERVED;
        controlExtPage[offset + 9]  = RESERVED;
        controlExtPage[offset + 10] = RESERVED;
        controlExtPage[offset + 11] = RESERVED;
        controlExtPage[offset + 12] = RESERVED;
        controlExtPage[offset + 13] = RESERVED;
        controlExtPage[offset + 14] = RESERVED;
        controlExtPage[offset + 15] = RESERVED;
        controlExtPage[offset + 16] = RESERVED;
        controlExtPage[offset + 17] = RESERVED;
        controlExtPage[offset + 18] = RESERVED;
        controlExtPage[offset + 19] = RESERVED;
        controlExtPage[offset + 20] = RESERVED;
        controlExtPage[offset + 21] = RESERVED;
        controlExtPage[offset + 22] = RESERVED;
        controlExtPage[offset + 23] = RESERVED;
        controlExtPage[offset + 24] = RESERVED;
        controlExtPage[offset + 25] = RESERVED;
        controlExtPage[offset + 26] = RESERVED;
        controlExtPage[offset + 27] = RESERVED;
        controlExtPage[offset + 28] = RESERVED;
        controlExtPage[offset + 29] = RESERVED;
        controlExtPage[offset + 30] = RESERVED;
        controlExtPage[offset + 31] = RESERVED;
    }
    // set the mode data length
    if (longHeader)
    {
        controlExtPage[0] = M_Byte1(pageLength - UINT16_C(2));
        controlExtPage[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        controlExtPage[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, controlExtPage, M_Min(pageLength, allocationLength));
    }
    safe_free(&controlExtPage);
    return ret;
}
// mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
// dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for
// short, or 16 for long (when longLBABit is set to true)
static eReturnValues translate_Mode_Sense_Power_Condition_1A(tDevice*   device,
                                                             ScsiIoCtx* scsiIoCtx,
                                                             uint8_t    pageControl,
                                                             bool       returnDataBlockDescriptor,
                                                             bool       longLBABit,
                                                             uint8_t*   dataBlockDescriptor,
                                                             bool       longHeader,
                                                             uint8_t*   modeParameterHeader,
                                                             uint16_t   allocationLength)
{
    eReturnValues ret                = SUCCESS;
    uint8_t*      powerConditionPage = M_NULLPTR; // will be allocated later
    uint16_t pageLength = UINT16_C(40); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += UINT16_C(8);
        offset += UINT16_C(8);
        headerLength = UINT8_C(8);
    }
    else
    {
        pageLength += UINT16_C(4);
        offset += UINT16_C(4);
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += UINT16_C(16);
            offset += UINT16_C(16);
            blockDescLength = UINT8_C(16);
        }
        else
        {
            pageLength += UINT16_C(8);
            offset += UINT16_C(8);
            blockDescLength = UINT8_C(8);
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    powerConditionPage = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!powerConditionPage)
    {
        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&powerConditionPage[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&powerConditionPage[headerLength], pageLength - headerLength, dataBlockDescriptor, blockDescLength);
    }
    // set the remaining part of the page up
    powerConditionPage[offset + 0] = 0x1A;
    powerConditionPage[offset + 1] = 0x26; // length
    // First, we need to check if EPC is supported or not
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
        && (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT7))
    {
        // EPC supported; perform EPC supported translation here
        // need to read the EPC log
        DECLARE_ZERO_INIT_ARRAY(uint8_t, ataPowerConditionsLog, UINT16_C(2) * LEGACY_DRIVE_SEC_SIZE);
        ata_Read_Log_Ext(device, ATA_LOG_POWER_CONDITIONS, 0, ataPowerConditionsLog, 2 * LEGACY_DRIVE_SEC_SIZE,
                         device->drive_info.ata_Options.readLogWriteLogDMASupported, 0);
        // TODO: handle command error
        switch (pageControl)
        {
        default:
        case 0: // current
            // ata page 0
            // idle_a is bytes 0-63
            if (ataPowerConditionsLog[0 + 1] & BIT2)
            {
                powerConditionPage[offset + 3] |= BIT1;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 4], pageLength - (offset + UINT32_C(4)),
                        &ataPowerConditionsLog[0 + 12], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 4]));
            // idle_b is bytes 64-127
            if (ataPowerConditionsLog[64 + 1] & BIT2)
            {
                powerConditionPage[offset + 3] |= BIT2;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 12], pageLength - (offset + UINT32_C(12)),
                        &ataPowerConditionsLog[64 + 12], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 12]));
            // idle_c is bytes 128-191
            if (ataPowerConditionsLog[128 + 1] & BIT2)
            {
                powerConditionPage[offset + 3] |= BIT3;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 16], pageLength - (offset + UINT32_C(16)),
                        &ataPowerConditionsLog[128 + 12], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 16]));
            // ata page 1
            // standby_y is bytes 384-447
            if (ataPowerConditionsLog[512 + 384 + 1] & BIT2)
            {
                powerConditionPage[offset + 2] |= BIT0;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 20], pageLength - (offset + UINT32_C(20)),
                        &ataPowerConditionsLog[512 + 384 + 12], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 20]));
            // standby_z is  bytes 448-511
            if (ataPowerConditionsLog[512 + 448 + 1] & BIT2)
            {
                powerConditionPage[offset + 3] |= BIT0;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 8], pageLength - (offset + UINT32_C(8)),
                        &ataPowerConditionsLog[512 + 448 + 12], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 8]));
            break;
        case 1: // changeable
            // ata page 0
            // idle_a is bytes 0-63
            if (ataPowerConditionsLog[0 + 1] & BIT5)
            {
                powerConditionPage[offset + 3] |= BIT1;
                powerConditionPage[offset + 4] = 0xFF;
                powerConditionPage[offset + 5] = 0xFF;
                powerConditionPage[offset + 6] = 0xFF;
                powerConditionPage[offset + 7] = 0xFF;
            }
            // idle_b is bytes 64-127
            if (ataPowerConditionsLog[64 + 1] & BIT5)
            {
                powerConditionPage[offset + 3] |= BIT2;
                powerConditionPage[offset + 12] = 0xFF;
                powerConditionPage[offset + 13] = 0xFF;
                powerConditionPage[offset + 14] = 0xFF;
                powerConditionPage[offset + 15] = 0xFF;
            }
            // idle_c is bytes 128-191
            if (ataPowerConditionsLog[128 + 1] & BIT5)
            {
                powerConditionPage[offset + 3] |= BIT3;
                powerConditionPage[offset + 16] = 0xFF;
                powerConditionPage[offset + 17] = 0xFF;
                powerConditionPage[offset + 18] = 0xFF;
                powerConditionPage[offset + 19] = 0xFF;
            }
            // ata page 1
            // standby_y is bytes 384-447
            if (ataPowerConditionsLog[512 + 384 + 1] & BIT5)
            {
                powerConditionPage[offset + 2] |= BIT0;
                powerConditionPage[offset + 20] = 0xFF;
                powerConditionPage[offset + 21] = 0xFF;
                powerConditionPage[offset + 22] = 0xFF;
                powerConditionPage[offset + 23] = 0xFF;
            }
            // standby_z is  bytes 448-511
            if (ataPowerConditionsLog[512 + 448 + 1] & BIT5)
            {
                powerConditionPage[offset + 3] |= BIT0;
                powerConditionPage[offset + 8]  = 0xFF;
                powerConditionPage[offset + 9]  = 0xFF;
                powerConditionPage[offset + 10] = 0xFF;
                powerConditionPage[offset + 11] = 0xFF;
            }
            break;
        case 2: // default
            // ata page 0
            // idle_a is bytes 0-63
            if (ataPowerConditionsLog[0 + 1] & BIT4)
            {
                powerConditionPage[offset + 3] |= BIT1;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 4], pageLength - (offset + UINT32_C(4)),
                        &ataPowerConditionsLog[0 + 4], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 4]));
            // idle_b is bytes 64-127
            if (ataPowerConditionsLog[64 + 1] & BIT4)
            {
                powerConditionPage[offset + 3] |= BIT2;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 12], pageLength - (offset + UINT32_C(12)),
                        &ataPowerConditionsLog[64 + 4], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 12]));
            // idle_c is bytes 128-191
            if (ataPowerConditionsLog[128 + 1] & BIT4)
            {
                powerConditionPage[offset + 3] |= BIT3;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 16], pageLength - (offset + UINT32_C(16)),
                        &ataPowerConditionsLog[128 + 4], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 16]));
            // ata page 1
            // standby_y is bytes 384-447
            if (ataPowerConditionsLog[512 + 384 + 1] & BIT4)
            {
                powerConditionPage[offset + 2] |= BIT0;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 20], pageLength - (offset + UINT32_C(20)),
                        &ataPowerConditionsLog[512 + 384 + 4], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 20]));
            // standby_z is  bytes 448-511
            if (ataPowerConditionsLog[512 + 448 + 1] & BIT4)
            {
                powerConditionPage[offset + 3] |= BIT0;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 8], pageLength - (offset + UINT32_C(8)),
                        &ataPowerConditionsLog[512 + 448 + 4], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 8]));
            break;
        case 3: // saved
            // ata page 0
            // idle_a is bytes 0-63
            if (ataPowerConditionsLog[0 + 1] & BIT3)
            {
                powerConditionPage[offset + 3] |= BIT1;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 4], pageLength - (offset + UINT32_C(4)),
                        &ataPowerConditionsLog[0 + 8], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 4]));
            // idle_b is bytes 64-127
            if (ataPowerConditionsLog[64 + 1] & BIT3)
            {
                powerConditionPage[offset + 3] |= BIT2;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 12], pageLength - (offset + UINT32_C(12)),
                        &ataPowerConditionsLog[64 + 8], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 12]));
            // idle_c is bytes 128-191
            if (ataPowerConditionsLog[128 + 1] & BIT3)
            {
                powerConditionPage[offset + 3] |= BIT3;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 16], pageLength - (offset + UINT32_C(16)),
                        &ataPowerConditionsLog[128 + 8], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 16]));
            // ata page 1
            // standby_y is bytes 384-447
            if (ataPowerConditionsLog[512 + 384 + 1] & BIT3)
            {
                powerConditionPage[offset + 2] |= BIT0;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 20], pageLength - (offset + UINT32_C(20)),
                        &ataPowerConditionsLog[512 + 384 + 8], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 20]));
            // standby_z is  bytes 448-511
            if (ataPowerConditionsLog[512 + 448 + 1] & BIT3)
            {
                powerConditionPage[offset + 3] |= BIT0;
            }
            // copy the timer value
            safe_memcpy(&powerConditionPage[offset + 8], pageLength - (offset + UINT32_C(8)),
                        &ataPowerConditionsLog[512 + 448 + 8], 4);
            // byte swap the value
            byte_Swap_32(C_CAST(uint32_t*, &powerConditionPage[offset + 8]));
            break;
        }
    }
    else
    {
        // EPC not supported; perform EPC not supported translation here
        // standby_y zero
        // idle_c is zero
        // idle_b is zero
        // idle_a is zero
        // standby_z
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word049)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word049) & BIT13)
        {
            powerConditionPage[offset + 3] |= BIT0;
            // TODO: store when the timer is changed by mode select so we can report what it was changed to...for now
            // set all F's
            powerConditionPage[offset + 8]  = UINT8_C(0xFF);
            powerConditionPage[offset + 9]  = UINT8_C(0xFF);
            powerConditionPage[offset + 10] = UINT8_C(0xFF);
            powerConditionPage[offset + 11] = UINT8_C(0xFF);
        }
    }
    // Set CCF bits
    if (pageControl != 0x01) // don't set this on the changable page since this isn't changable
    {
        powerConditionPage[offset + 39] = 0x54; // check condition while transitioning out of these states is disabled.
    }
    // set the mode data length
    if (longHeader)
    {
        powerConditionPage[0] = M_Byte1(pageLength - UINT16_C(2));
        powerConditionPage[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        powerConditionPage[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, powerConditionPage, M_Min(pageLength, allocationLength));
    }
    safe_free(&powerConditionPage);
    return ret;
}
// mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
// dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for
// short, or 16 for long (when longLBABit is set to true)
static eReturnValues translate_Mode_Sense_ATA_Power_Condition_1A_F1(tDevice*   device,
                                                                    ScsiIoCtx* scsiIoCtx,
                                                                    uint8_t    pageControl,
                                                                    bool       returnDataBlockDescriptor,
                                                                    bool       longLBABit,
                                                                    uint8_t*   dataBlockDescriptor,
                                                                    bool       longHeader,
                                                                    uint8_t*   modeParameterHeader,
                                                                    uint16_t   allocationLength)
{
    eReturnValues ret                = SUCCESS;
    uint8_t*      powerConditionPage = M_NULLPTR; // will be allocated later
    uint16_t pageLength = UINT16_C(16); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    powerConditionPage = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!powerConditionPage)
    {

        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&powerConditionPage[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&powerConditionPage[headerLength], pageLength - headerLength, dataBlockDescriptor, blockDescLength);
    }
    // set the remaining part of the page up
    powerConditionPage[offset + 0] = 0x1A;
    powerConditionPage[offset + 0] |= BIT6; // set the spf bit
    powerConditionPage[offset + 1] = 0xF1;  // subpage
    powerConditionPage[offset + 2] = 0;
    powerConditionPage[offset + 3] = 0x0C;
    //
    powerConditionPage[offset + 4] = RESERVED;
    if (pageControl == 0x00 || pageControl == 0x3 || pageControl == 0x2) // current and saved
    {
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT3)
        {
            powerConditionPage[offset + 5] |= BIT0;
            powerConditionPage[offset + 6] = M_Byte0(
                device->drive_info.IdentifyData.ata.Word091); // assuming this is valid since the bit above was checked
        }
    }
    // NOTE: We can add this back in if we store the default APM value on startup of the translator
    // else if (pageControl == 0x2) // default
    // {
    //     // TODO: how do we handle default? we should probably store what APM was when we started software SAT to know
    //     // for sure. For now, match the current/saved mode
    //     if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
    //         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT3)
    //     {
    //         powerConditionPage[offset + 5] |= BIT0;
    //         powerConditionPage[offset + 6] = M_Byte0(device->drive_info.IdentifyData.ata.Word091);
    //     }
    // }
    else // changeable
    {
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT3) // apm supported
        {
            // changes can be made
            powerConditionPage[offset + 5] |= BIT0;
            powerConditionPage[offset + 6] = 0xFF;
        }
    }

    powerConditionPage[offset + 7]  = RESERVED;
    powerConditionPage[offset + 8]  = RESERVED;
    powerConditionPage[offset + 9]  = RESERVED;
    powerConditionPage[offset + 10] = RESERVED;
    powerConditionPage[offset + 11] = RESERVED;
    powerConditionPage[offset + 12] = RESERVED;
    powerConditionPage[offset + 13] = RESERVED;
    powerConditionPage[offset + 14] = RESERVED;
    powerConditionPage[offset + 15] = RESERVED;
    // set the mode data length
    if (longHeader)
    {
        powerConditionPage[0] = M_Byte1(pageLength - UINT16_C(2));
        powerConditionPage[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        powerConditionPage[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, powerConditionPage, M_Min(pageLength, allocationLength));
    }
    safe_free(&powerConditionPage);
    return ret;
}

// mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
// dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for
// short, or 16 for long (when longLBABit is set to true)
static eReturnValues translate_Mode_Sense_Read_Write_Error_Recovery_01h(ScsiIoCtx* scsiIoCtx,
                                                                        uint8_t    pageControl,
                                                                        bool       returnDataBlockDescriptor,
                                                                        bool       longLBABit,
                                                                        uint8_t*   dataBlockDescriptor,
                                                                        bool       longHeader,
                                                                        uint8_t*   modeParameterHeader,
                                                                        uint16_t   allocationLength)
{
    eReturnValues ret                    = SUCCESS;
    uint8_t*      readWriteErrorRecovery = M_NULLPTR; // will be allocated later
    uint16_t pageLength = UINT16_C(12); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    readWriteErrorRecovery = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!readWriteErrorRecovery)
    {

        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&readWriteErrorRecovery[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&readWriteErrorRecovery[headerLength], pageLength - headerLength, dataBlockDescriptor,
                    blockDescLength);
    }
    // set the remaining part of the page up
    readWriteErrorRecovery[offset + 0] = 0x01; // page number
    readWriteErrorRecovery[offset + 1] = 0x0A; // page length
    if (pageControl != 0x1)                    // default, saved, and current pages. Nothing on this page is changable.
    {
        readWriteErrorRecovery[offset + 2]  = BIT7; // awre = 1, arre = 0, tb = 0, rc = 0, per = 0, dte = 0
        readWriteErrorRecovery[offset + 3]  = 0;    // read retry count (since we only issue 1 read command)
        readWriteErrorRecovery[offset + 4]  = OBSOLETE;
        readWriteErrorRecovery[offset + 5]  = OBSOLETE;
        readWriteErrorRecovery[offset + 6]  = OBSOLETE;
        readWriteErrorRecovery[offset + 7]  = 0; // lbpre = 0
        readWriteErrorRecovery[offset + 8]  = 0; // write retry count (since we only issue 1 write command)
        readWriteErrorRecovery[offset + 9]  = RESERVED;
        readWriteErrorRecovery[offset + 10] = 0; // recovery time limit
        readWriteErrorRecovery[offset + 11] = 0; // recovery time limit
    }
    // set the mode data length
    if (longHeader)
    {
        readWriteErrorRecovery[0] = M_Byte1(pageLength - UINT16_C(2));
        readWriteErrorRecovery[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        readWriteErrorRecovery[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, readWriteErrorRecovery,
                    M_Min(pageLength, allocationLength));
    }
    safe_free(&readWriteErrorRecovery);
    return ret;
}

// mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
// dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for
// short, or 16 for long (when longLBABit is set to true)
static eReturnValues translate_Mode_Sense_Caching_08h(tDevice*   device,
                                                      ScsiIoCtx* scsiIoCtx,
                                                      uint8_t    pageControl,
                                                      bool       returnDataBlockDescriptor,
                                                      bool       longLBABit,
                                                      uint8_t*   dataBlockDescriptor,
                                                      bool       longHeader,
                                                      uint8_t*   modeParameterHeader,
                                                      uint16_t   allocationLength)
{
    eReturnValues ret     = SUCCESS;
    uint8_t*      caching = M_NULLPTR;    // will be allocated later
    uint16_t pageLength   = UINT16_C(20); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    caching = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (caching == M_NULLPTR)
    {

        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&caching[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&caching[headerLength], pageLength - headerLength, dataBlockDescriptor, blockDescLength);
    }
    // set the remaining part of the page up
    // send an identify command to get up to date read/write cache info
    caching[offset + 0] = 0x08; // page number
    caching[offset + 1] = 0x12; // page length
    if (pageControl == 0x1)     // changeable
    {
        // check if write cache is supported
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT5)
        {
            caching[offset + 2] = BIT2;
        }
        // check if read-look-ahead is supported
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT6)
        {
            caching[offset + 12] = BIT5;
        }
    }
    else // saved, current, and default. TODO: Handle saving what the drive had when we started talking to it.
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, iddata, LEGACY_DRIVE_SEC_SIZE);
        ata_Identify(device, iddata, LEGACY_DRIVE_SEC_SIZE);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT5)
        {
            caching[offset + 2] = BIT2; // ic = 0, abpf = 0, cap = 0, disc = 0, size = 0, wce = 1, mf = 0, rcd = 0
        }
        else
        {
            caching[offset + 2] = 0; // ic = 0, abpf = 0, cap = 0, disc = 0, size = 0, wce = 0, mf = 0, rcd = 0
        }
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word085)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word085) & BIT6)
        {
            caching[offset + 12] =
                0; // fsw = 0, lbcss = 0, dra = 0, vendor specific (2bits) = 0, sync_prog(2bits) = 0, nv_dis = 0
        }
        else
        {
            caching[offset + 12] =
                BIT5; // fsw = 0, lbcss = 0, dra = 1, vendor specific (2bits) = 0, sync_prog(2bits) = 0, nv_dis = 0
        }
    }
    caching[offset + 3]  = 0; // demand read/write retention priorities
    caching[offset + 4]  = 0;
    caching[offset + 5]  = 0;
    caching[offset + 6]  = 0;
    caching[offset + 7]  = 0;
    caching[offset + 8]  = 0;
    caching[offset + 9]  = 0;
    caching[offset + 10] = 0;
    caching[offset + 11] = 0;
    // byte 12 is set above
    caching[offset + 13] = 0; // num cache segments
    caching[offset + 14] = 0; // cache segment size
    caching[offset + 15] = 0; // cache segment size
    caching[offset + 16] = RESERVED;
    caching[offset + 17] = OBSOLETE;
    caching[offset + 18] = OBSOLETE;
    caching[offset + 19] = OBSOLETE;
    // set the mode data length
    if (longHeader)
    {
        caching[0] = M_Byte1(pageLength - UINT16_C(2));
        caching[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        caching[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, caching, M_Min(pageLength, allocationLength));
    }
    safe_free(&caching);
    return ret;
}

// mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
// dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for
// short, or 16 for long (when longLBABit is set to true)
static eReturnValues translate_Mode_Sense_Informational_Exceptions_Control_1Ch(ScsiIoCtx* scsiIoCtx,
                                                                               uint8_t    pageControl,
                                                                               bool       returnDataBlockDescriptor,
                                                                               bool       longLBABit,
                                                                               uint8_t*   dataBlockDescriptor,
                                                                               bool       longHeader,
                                                                               uint8_t*   modeParameterHeader,
                                                                               uint16_t   allocationLength)
{
    eReturnValues ret                     = SUCCESS;
    uint8_t*      informationalExceptions = M_NULLPTR; // will be allocated later
    uint16_t pageLength = UINT16_C(12); // add onto this depending on the length of the header and block descriptors
    uint16_t offset = UINT16_C(0); // used later when we start setting data in the buffer since we need to account for
                                   // mode parameter header and DBDs
    uint8_t headerLength    = UINT8_C(4);
    uint8_t blockDescLength = UINT8_C(0);
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    // now that we know how many bytes we need for this, allocate memory
    informationalExceptions = M_REINTERPRET_CAST(uint8_t*, safe_calloc(pageLength, sizeof(uint8_t)));
    if (!informationalExceptions)
    {

        return MEMORY_FAILURE;
    }
    // copy header into place
    safe_memcpy(&informationalExceptions[0], pageLength, modeParameterHeader, headerLength);
    // copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        safe_memcpy(&informationalExceptions[headerLength], pageLength - headerLength, dataBlockDescriptor,
                    blockDescLength);
    }
    // set the remaining part of the page up
    informationalExceptions[offset + 0] = 0x1C; // page number
    informationalExceptions[offset + 1] = 0x0A; // page length
    if (pageControl != 0x1)
    {
        informationalExceptions[offset + 2] =
            0; // perf = 0, reserved = 0, ebf = 0 (no background functions), EWAsc = 0 (doesn't report warnings), DExcpt
               // = 0 (device does not disable reporting failure predictions), test = 0, ebackerr = 0, logerr = 0
        informationalExceptions[offset + 3]  = 0x06; // MRIE = 6h
        informationalExceptions[offset + 4]  = 0;    // interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 5]  = 0;    // interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 6]  = 0;    // interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 7]  = 0;    // interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 8]  = 0;    // report count = 0 (no limit)
        informationalExceptions[offset + 9]  = 0;    // report count = 0 (no limit)
        informationalExceptions[offset + 10] = 0;    // report count = 0 (no limit)
        informationalExceptions[offset + 11] = 0;    // report count = 0 (no limit)
    }
    else
    {
        // DExcpt can be changed. But we won't allow it right now. - TJE
        // MRIE modes other than 6 are allowed, but unspecified. Not going to support them right now - TJE
    }
    // set the mode data length
    if (longHeader)
    {
        informationalExceptions[0] = M_Byte1(pageLength - UINT16_C(2));
        informationalExceptions[1] = M_Byte0(pageLength - UINT16_C(2));
    }
    else
    {
        informationalExceptions[0] = C_CAST(uint8_t, pageLength - UINT8_C(1));
    }
    // now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, informationalExceptions,
                    M_Min(pageLength, allocationLength));
    }
    safe_free(&informationalExceptions);
    return ret;
}

static eReturnValues translate_SCSI_Mode_Sense_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                 = SUCCESS;
    bool    returnDataBlockDescriptor = true; // true means return a data block descriptor, false means don't return one
    bool    longLBABit                = false; // true = longlba format, false = standard format for block descriptor
    bool    longHeader                = false; // false for mode sense 6, true for mode sense 10
    uint8_t pageControl =
        (scsiIoCtx->cdb[2] & 0xC0) >> 6; // only current values needs to be supported...anything else is unspecified
    uint8_t  pageCode         = scsiIoCtx->cdb[2] & 0x3F;
    uint8_t  subpageCode      = scsiIoCtx->cdb[3];
    uint16_t allocationLength = UINT16_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, dataBlockDescriptor, 16);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, modeParameterHeader, 8);
    bool invalidField = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    uint8_t  byte1        = scsiIoCtx->cdb[1];
    if (scsiIoCtx->cdb[1] & BIT3)
    {
        returnDataBlockDescriptor = false;
    }
    if (scsiIoCtx->cdb[OPERATION_CODE] == MODE_SENSE_6_CMD)
    {
        allocationLength       = scsiIoCtx->cdb[5];
        modeParameterHeader[1] = 0;     // medium type
        modeParameterHeader[2] |= BIT4; // set the DPOFUA bit
        if (returnDataBlockDescriptor)
        {
            modeParameterHeader[MODE_HEADER_6_BLK_DESC_OFFSET] = 8; // 8 bytes for the short descriptor
        }
        // check for invalid fields
        byte1 &= 0xF7; // removing dbd bit since we can support that
        if (((fieldPointer = 1) != 0 && byte1 != 0))
        {
            invalidField = true;
        }
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == MODE_SENSE10)
    {
        // mode sense 10
        allocationLength       = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        longHeader             = true;
        modeParameterHeader[2] = 0;     // medium type
        modeParameterHeader[3] |= BIT4; // set the DPOFUA bit
        if (scsiIoCtx->cdb[1] & BIT4)
        {
            longLBABit = true;
            modeParameterHeader[4] |= BIT0; // set the longlba bit
        }
        if (returnDataBlockDescriptor)
        {
            if (longLBABit)
            {
                modeParameterHeader[MODE_HEADER_10_BLK_DESC_OFFSET]     = M_Byte1(16);
                modeParameterHeader[MODE_HEADER_10_BLK_DESC_OFFSET + 1] = M_Byte0(16);
            }
            else
            {
                modeParameterHeader[MODE_HEADER_10_BLK_DESC_OFFSET]     = M_Byte1(8);
                modeParameterHeader[MODE_HEADER_10_BLK_DESC_OFFSET + 1] = M_Byte0(8);
            }
        }
        byte1 &= 0xE7; // removing llbaa and DBD bits since we can support those
        // check for invalid fields
        if (((fieldPointer = 1) != 0 && byte1 != 0) || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0) ||
            ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0))
        {
            invalidField = true;
        }
    }
    else
    {
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            if (fieldPointer == 1)
            {
                reservedByteVal = byte1;
            }

            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            // 16 byte long format
            dataBlockDescriptor[0]  = M_Byte7(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[1]  = M_Byte6(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[2]  = M_Byte5(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[3]  = M_Byte4(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[4]  = M_Byte3(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[5]  = M_Byte2(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[6]  = M_Byte1(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[7]  = M_Byte0(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[8]  = RESERVED;
            dataBlockDescriptor[9]  = RESERVED;
            dataBlockDescriptor[10] = RESERVED;
            dataBlockDescriptor[11] = RESERVED;
            dataBlockDescriptor[12] = M_Byte3(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[13] = M_Byte2(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[14] = M_Byte1(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[15] = M_Byte0(device->drive_info.deviceBlockSize);
        }
        else
        {
            // 8 byte short format
            uint32_t maxLBA        = C_CAST(uint32_t, M_Min(UINT32_MAX, device->drive_info.deviceMaxLba));
            dataBlockDescriptor[0] = M_Byte3(maxLBA);
            dataBlockDescriptor[1] = M_Byte2(maxLBA);
            dataBlockDescriptor[2] = M_Byte1(maxLBA);
            dataBlockDescriptor[3] = M_Byte0(maxLBA);
            dataBlockDescriptor[4] = RESERVED;
            dataBlockDescriptor[5] = M_Byte2(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[6] = M_Byte1(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[7] = M_Byte0(device->drive_info.deviceBlockSize);
        }
    }
    switch (pageCode)
    {
    case 0xA0: // control and control extension and PATA control
        switch (subpageCode)
        {
        case 0: // control
            ret = translate_Mode_Sense_Control_0Ah(device, scsiIoCtx, pageControl, returnDataBlockDescriptor,
                                                   longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader,
                                                   allocationLength);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
        case 0x01: // control extension
            ret = translate_Mode_Sense_Control_Extension_0Ah_01h(scsiIoCtx, pageControl, returnDataBlockDescriptor,
                                                                 longLBABit, dataBlockDescriptor, longHeader,
                                                                 modeParameterHeader, allocationLength);
            break;
#endif             // SAT_SPEC_SUPPORTED
        case 0xF1: // PATA control. Report this information BUT DO NOT ALLOW CHANGES!
            if (!is_ATA_Identify_Word_Valid_SATA(
                    device->drive_info.IdentifyData.ata
                        .Word076)) // Only Serial ATA Devices will set the bits in words 76-79. Bit zero should always
                                   // be set to zero, so the FFFF case won't be an issue
            {
                ret = translate_Mode_Sense_PATA_Control_0Ah_F1h(scsiIoCtx, pageControl, returnDataBlockDescriptor,
                                                                longLBABit, dataBlockDescriptor, longHeader,
                                                                modeParameterHeader, allocationLength);
                break;
            }
            M_FALLTHROUGH;
        default:
            ret          = NOT_SUPPORTED;
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(5);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    case 0x01: // read-write error recovery
        switch (subpageCode)
        {
        case 0:
            ret = translate_Mode_Sense_Read_Write_Error_Recovery_01h(scsiIoCtx, pageControl, returnDataBlockDescriptor,
                                                                     longLBABit, dataBlockDescriptor, longHeader,
                                                                     modeParameterHeader, allocationLength);
            break;
        default:
            ret          = NOT_SUPPORTED;
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(5);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    case 0x08: // caching
        switch (subpageCode)
        {
        case 0:
            ret = translate_Mode_Sense_Caching_08h(device, scsiIoCtx, pageControl, returnDataBlockDescriptor,
                                                   longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader,
                                                   allocationLength);
            break;
        default:
            ret          = NOT_SUPPORTED;
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(5);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    case 0x1C: // informational exceptions control
        switch (subpageCode)
        {
        case 0:
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word082)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word082) & BIT0)
            {
                ret = translate_Mode_Sense_Informational_Exceptions_Control_1Ch(
                    scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader,
                    modeParameterHeader, allocationLength);
            }
            else
            {
                ret          = NOT_SUPPORTED;
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(5);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
            break;
        default:
            ret          = NOT_SUPPORTED;
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(5);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    case 0x1A: // Power Condition + ATA Power Condition
        switch (subpageCode)
        {
        case 0xF1: // ATA power condition (APM)
            if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                    le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word083) &
                    BIT3) // only support this page if APM is supported-TJE
            {
                ret = translate_Mode_Sense_ATA_Power_Condition_1A_F1(
                    device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor,
                    longHeader, modeParameterHeader, allocationLength);
            }
            else
            {
                ret          = NOT_SUPPORTED;
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(5);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
            }
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
        case 0: // power condition (EPC if supported or something else...)
            ret = translate_Mode_Sense_Power_Condition_1A(device, scsiIoCtx, pageControl, returnDataBlockDescriptor,
                                                          longLBABit, dataBlockDescriptor, longHeader,
                                                          modeParameterHeader, allocationLength);
            break;
#endif // SAT_SPEC_SUPPORTED
        default:
            ret          = NOT_SUPPORTED;
            fieldPointer = UINT16_C(2);
            bitPointer   = UINT8_C(5);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    default:
        ret          = NOT_SUPPORTED;
        fieldPointer = UINT16_C(2);
        bitPointer   = UINT8_C(5);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        break;
    }
    return ret;
}

static eReturnValues translate_Mode_Select_Caching_08h(tDevice*       device,
                                                       ScsiIoCtx*     scsiIoCtx,
                                                       const uint8_t* ptrToBeginningOfModePage,
                                                       uint16_t       pageLength)
{
    eReturnValues ret = SUCCESS;
    uint16_t      dataOffset =
        C_CAST(uint16_t, ptrToBeginningOfModePage -
                             scsiIoCtx->pdata); // to be used when setting which field is invalid in parameter list
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t       bitPointer   = UINT8_C(0);
    uint16_t      fieldPointer = UINT16_C(0);
    eReturnValues wceRet       = SUCCESS;
    eReturnValues draRet       = SUCCESS;
    // start checking everything to make sure it looks right before we issue commands
    if (pageLength != 0x12)
    {
        fieldPointer = dataOffset + 1;
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (ptrToBeginningOfModePage[2] & 0xFB) // all but WCE bit
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[2] & 0xFB;
        uint8_t counter         = UINT8_C(0);
        fieldPointer            = dataOffset + 2;

        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (((fieldPointer = 3) != 0 && ptrToBeginningOfModePage[3] != 0) ||
        ((fieldPointer = 4) != 0 && ptrToBeginningOfModePage[4] != 0) ||
        ((fieldPointer = 5) != 0 && ptrToBeginningOfModePage[5] != 0) ||
        ((fieldPointer = 6) != 0 && ptrToBeginningOfModePage[6] != 0) ||
        ((fieldPointer = 7) != 0 && ptrToBeginningOfModePage[7] != 0) ||
        ((fieldPointer = 8) != 0 && ptrToBeginningOfModePage[8] != 0) ||
        ((fieldPointer = 9) != 0 && ptrToBeginningOfModePage[9] != 0) ||
        ((fieldPointer = 10) != 0 && ptrToBeginningOfModePage[10] != 0) ||
        ((fieldPointer = 11) != 0 && ptrToBeginningOfModePage[11] != 0))
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
        uint8_t counter         = UINT8_C(0);
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        fieldPointer += C_CAST(uint16_t, dataOffset);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (ptrToBeginningOfModePage[12] & 0xDF) // all but DRA bit
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[12] & 0xDF;
        uint8_t counter         = UINT8_C(0);
        fieldPointer            = dataOffset + 12;

        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (((fieldPointer = 13) != 0 && ptrToBeginningOfModePage[13] != 0) ||
        ((fieldPointer = 14) != 0 && ptrToBeginningOfModePage[14] != 0) ||
        ((fieldPointer = 15) != 0 && ptrToBeginningOfModePage[15] != 0) ||
        ((fieldPointer = 16) != 0 && ptrToBeginningOfModePage[16] != 0) ||
        ((fieldPointer = 17) != 0 && ptrToBeginningOfModePage[17] != 0) ||
        ((fieldPointer = 18) != 0 && ptrToBeginningOfModePage[18] != 0) ||
        ((fieldPointer = 19) != 0 && ptrToBeginningOfModePage[19] != 0))
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
        uint8_t counter         = UINT8_C(0);
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        fieldPointer += dataOffset;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    // WCE
    if (ptrToBeginningOfModePage[2] & BIT2)
    {
        // enable write cache
        wceRet = ata_Set_Features(device, SF_ENABLE_VOLITILE_WRITE_CACHE, 0, 0, 0, 0);
    }
    else
    {
        // disable write cache
        wceRet = ata_Set_Features(device, SF_DISABLE_VOLITILE_WRITE_CACHE, 0, 0, 0, 0);
    }
    // DRA
    if (ptrToBeginningOfModePage[12] & BIT5)
    {
        // disable read-look-ahead
        draRet = ata_Set_Features(device, SF_DISABLE_READ_LOOK_AHEAD_FEATURE, 0, 0, 0, 0);
    }
    else
    {
        // enable read-look-ahead
        draRet = ata_Set_Features(device, SF_ENABLE_READ_LOOK_AHEAD_FEATURE, 0, 0, 0, 0);
    }
    if (draRet || wceRet)
    {
        ret = FAILURE;
        // set sense data to aborted command: ATA device failed set features
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x44,
                                       0x71, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    }
    else
    {
        ret = SUCCESS;
        // set good status
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, 0, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    }
    return ret;
}

static eReturnValues translate_Mode_Select_Control_0Ah(tDevice*       device,
                                                       ScsiIoCtx*     scsiIoCtx,
                                                       const uint8_t* ptrToBeginningOfModePage,
                                                       uint16_t       pageLength)
{
    eReturnValues ret = SUCCESS;
    uint16_t      dataOffset =
        C_CAST(uint16_t, ptrToBeginningOfModePage -
                             scsiIoCtx->pdata); // to be used when setting which field is invalid in parameter list
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (pageLength != 0x0A)
    {
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(7);
        fieldPointer += dataOffset;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (ptrToBeginningOfModePage[2] & 0xFB) // all but d_sense bit
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[2] & 0xFB;
        uint8_t counter         = UINT8_C(0);
        fieldPointer            = UINT16_C(2);
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        fieldPointer += dataOffset;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (M_Nibble1(ptrToBeginningOfModePage[2]) != 0x01) // queue alg modifier
    {
        fieldPointer = 3 + dataOffset;
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    // nuar
    if (ptrToBeginningOfModePage[2] & BIT3)
    {
        fieldPointer = 3 + dataOffset;
        bitPointer   = UINT8_C(3);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    // check qerr = 01
    if ((ptrToBeginningOfModePage[2] & 0x06) >> 1 != 0x01)
    {
        fieldPointer = 3 + dataOffset;
        bitPointer   = UINT8_C(2);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (ptrToBeginningOfModePage[2] & BIT0)
    {
        fieldPointer = 3 + dataOffset;
        bitPointer   = UINT8_C(0);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (((fieldPointer = 4) != 0 && ptrToBeginningOfModePage[4] != 0) ||
        ((fieldPointer = 5) != 0 && ptrToBeginningOfModePage[5] != 0) ||
        ((fieldPointer = 6) != 0 && ptrToBeginningOfModePage[6] != 0) ||
        ((fieldPointer = 7) != 0 && ptrToBeginningOfModePage[7] != 0) ||
        ((fieldPointer = 8) != 0 && ptrToBeginningOfModePage[8] != 0xFF) ||
        ((fieldPointer = 9) != 0 && ptrToBeginningOfModePage[9] != 0xFF))
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
        uint8_t counter         = UINT8_C(0);
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1; // because we should always get a count of at least 1 if here and bits are zero
                                  // indexed
        if (fieldPointer == 9)
        {
            --fieldPointer; // this is the same field in this mode page, just adjusting what we're setting so that we
                            // return the correct information.
        }
        if (fieldPointer == 8)
        {
            bitPointer = UINT8_C(7);
        }
        fieldPointer += dataOffset;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    // TODO: make sure they aren't trying to change the extended self test timeout! Ignoring it for now...-TJE
    if (ptrToBeginningOfModePage[2] & BIT2)
    {
        device->drive_info.softSATFlags.senseDataDescriptorFormat = true;
    }
    else
    {
        device->drive_info.softSATFlags.senseDataDescriptorFormat = false;
    }
    return ret;
}

static eReturnValues translate_Mode_Select_Power_Conditions_1A(tDevice*       device,
                                                               ScsiIoCtx*     scsiIoCtx,
                                                               const uint8_t* ptrToBeginningOfModePage,
                                                               uint16_t       pageLength)
{
    eReturnValues ret            = SUCCESS;
    bool          saveParameters = false;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (scsiIoCtx->cdb[1] & BIT0)
    {
        saveParameters = true;
    }
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
        && (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT7)) // EPC supported
    {
        if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && pageLength != 0x0026) ||
            ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 &&
             get_bit_range_uint8(ptrToBeginningOfModePage[2], 7, 6) != 0) // PM_BG_PRECEDENCE
            || ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[2], 5, 1) != 0) // reserved
            || ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[3], 7, 4) != 0)                            // reserved
            || ((fieldPointer = 24) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[24] == 0) // reserved
            || ((fieldPointer = 25) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[25] == 0) // reserved
            || ((fieldPointer = 26) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[26] == 0) // reserved
            || ((fieldPointer = 27) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[27] == 0) // reserved
            || ((fieldPointer = 28) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[28] == 0) // reserved
            || ((fieldPointer = 29) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[29] == 0) // reserved
            || ((fieldPointer = 30) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[30] == 0) // reserved
            || ((fieldPointer = 31) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[31] == 0) // reserved
            || ((fieldPointer = 32) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[32] == 0) // reserved
            || ((fieldPointer = 33) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[33] == 0) // reserved
            || ((fieldPointer = 34) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[34] == 0) // reserved
            || ((fieldPointer = 35) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[35] == 0) // reserved
            || ((fieldPointer = 36) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[36] == 0) // reserved
            || ((fieldPointer = 37) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[37] == 0) // reserved
            || ((fieldPointer = 38) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[38] == 0) // reserved
            || ((fieldPointer = 39) != 0 && (bitPointer = 7) != 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[39], 7, 6) != 1) // CCF IDLE
            || ((fieldPointer = 39) != 0 && (bitPointer = 5) != 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[39], 5, 4) != 1) // CCF STANDBY
            || ((fieldPointer = 39) != 0 && (bitPointer = 3) != 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[39], 3, 2) != 1) // CCF STOPPED
            || ((fieldPointer = 39) != 0 && (bitPointer = 1) != 0 && (ptrToBeginningOfModePage[39] & BIT1) != 0) ||
            ((fieldPointer = 39) != 0 && (bitPointer = 0) == 0 && (ptrToBeginningOfModePage[39] & BIT0) != 0))
        {
            if (bitPointer == 0)
            {
                uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
                uint8_t counter         = UINT8_C(0);
                if (fieldPointer == 2)
                {
                    reservedByteVal = ptrToBeginningOfModePage[fieldPointer] & 0x3E;
                }
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer =
                    counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            }
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        else
        {
            // Read the EPC log so we can check more bit fields before we make changes
            DECLARE_ZERO_INIT_ARRAY(uint8_t, epcLog, LEGACY_DRIVE_SEC_SIZE * 2);
            if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_POWER_CONDITIONS, 0, epcLog, LEGACY_DRIVE_SEC_SIZE * 2,
                                            device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
            {
                bool parameterRounded     = false;
                bool commandSequenceError = false;
                // these flags store if we need to make changes to the drive
                bool change_idle_a    = false;
                bool change_idle_b    = false;
                bool change_idle_c    = false;
                bool change_standby_y = false;
                bool change_standby_z = false;
                // bit flags to enable/disable the mode
                bool     idle_a       = ptrToBeginningOfModePage[3] & BIT1;
                bool     idle_b       = ptrToBeginningOfModePage[3] & BIT2;
                bool     idle_c       = ptrToBeginningOfModePage[3] & BIT3;
                bool     standby_y    = ptrToBeginningOfModePage[2] & BIT0;
                bool     standby_z    = ptrToBeginningOfModePage[3] & BIT0;
                uint32_t idle_a_timer = M_BytesTo4ByteValue(ptrToBeginningOfModePage[4], ptrToBeginningOfModePage[5],
                                                            ptrToBeginningOfModePage[6], ptrToBeginningOfModePage[7]);
                uint32_t idle_b_timer = M_BytesTo4ByteValue(ptrToBeginningOfModePage[12], ptrToBeginningOfModePage[13],
                                                            ptrToBeginningOfModePage[14], ptrToBeginningOfModePage[15]);
                uint32_t idle_c_timer = M_BytesTo4ByteValue(ptrToBeginningOfModePage[16], ptrToBeginningOfModePage[17],
                                                            ptrToBeginningOfModePage[18], ptrToBeginningOfModePage[19]);
                uint32_t standby_y_timer =
                    M_BytesTo4ByteValue(ptrToBeginningOfModePage[20], ptrToBeginningOfModePage[21],
                                        ptrToBeginningOfModePage[22], ptrToBeginningOfModePage[23]);
                uint32_t standby_z_timer =
                    M_BytesTo4ByteValue(ptrToBeginningOfModePage[8], ptrToBeginningOfModePage[9],
                                        ptrToBeginningOfModePage[10], ptrToBeginningOfModePage[11]);

                // Now check what modes/timers the request is for and make sure the device allows changes!
                uint16_t timerOffset = UINT16_C(0);
                // idle_a check...changable or not, we need to check if they are trying to request a change as well as
                // if the device supports the change
                if (!((idle_a && (epcLog[timerOffset + 1] & BIT2)) &&
                      (M_BytesTo4ByteValue(epcLog[timerOffset + 15], epcLog[timerOffset + 14], epcLog[timerOffset + 13],
                                           epcLog[timerOffset + 12])) == idle_a_timer))
                {
                    // change is requested
                    change_idle_a = true;
                    // check if changable
                    if (!(epcLog[timerOffset + 1] & BIT5))
                    {
                        // not changable...error
                        bitPointer   = UINT8_C(7);
                        fieldPointer = UINT16_C(4);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return ret;
                    }
                }
                timerOffset = 64;
                if (!((idle_b && (epcLog[timerOffset + 1] & BIT2)) &&
                      (M_BytesTo4ByteValue(epcLog[timerOffset + 15], epcLog[timerOffset + 14], epcLog[timerOffset + 13],
                                           epcLog[timerOffset + 12])) == idle_b_timer))
                {
                    // change is requested
                    change_idle_b = true;
                    // check if changable
                    if (!(epcLog[timerOffset + 1] & BIT5))
                    {
                        // not changable...error
                        bitPointer   = UINT8_C(7);
                        fieldPointer = UINT16_C(12);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return ret;
                    }
                }
                timerOffset = 128;
                if (!((idle_c && (epcLog[timerOffset + 1] & BIT2)) &&
                      (M_BytesTo4ByteValue(epcLog[timerOffset + 15], epcLog[timerOffset + 14], epcLog[timerOffset + 13],
                                           epcLog[timerOffset + 12])) == idle_c_timer))
                {
                    // change is requested
                    change_idle_c = true;
                    // check if changable
                    if (!(epcLog[timerOffset + 1] & BIT5))
                    {
                        // not changable...error
                        bitPointer   = UINT8_C(7);
                        fieldPointer = UINT16_C(16);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return ret;
                    }
                }
                timerOffset = LEGACY_DRIVE_SEC_SIZE + 384;
                if (!((standby_y && (epcLog[timerOffset + 1] & BIT2)) &&
                      (M_BytesTo4ByteValue(epcLog[timerOffset + 15], epcLog[timerOffset + 14], epcLog[timerOffset + 13],
                                           epcLog[timerOffset + 12])) == standby_y_timer))
                {
                    // change is requested
                    change_standby_y = true;
                    // check if changable
                    if (!(epcLog[timerOffset + 1] & BIT5))
                    {
                        // not changable...error
                        bitPointer   = UINT8_C(7);
                        fieldPointer = UINT16_C(20);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return ret;
                    }
                }
                timerOffset = LEGACY_DRIVE_SEC_SIZE + 448;
                if (!((standby_z && (epcLog[timerOffset + 1] & BIT2)) &&
                      (M_BytesTo4ByteValue(epcLog[timerOffset + 15], epcLog[timerOffset + 14], epcLog[timerOffset + 13],
                                           epcLog[timerOffset + 12])) == standby_z_timer))
                {
                    // change is requested
                    change_standby_z = true;
                    // check if changable
                    if (!(epcLog[timerOffset + 1] & BIT5))
                    {
                        // not changable...error
                        bitPointer   = UINT8_C(7);
                        fieldPointer = UINT16_C(8);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return ret;
                    }
                }
                if (change_idle_a)
                {
                    // first check if we need to round to a min or max for the drive...
                    uint32_t ata_Idle_a_min = M_BytesTo4ByteValue(epcLog[timerOffset + 23], epcLog[timerOffset + 22],
                                                                  epcLog[timerOffset + 21], epcLog[timerOffset + 20]);
                    uint32_t ata_Idle_a_max = M_BytesTo4ByteValue(epcLog[timerOffset + 24], epcLog[timerOffset + 25],
                                                                  epcLog[timerOffset + 26], epcLog[timerOffset + 27]);
                    uint16_t ata_idle_a     = UINT16_C(0);
                    bool     timerUnits     = false;
                    if (idle_a_timer < ata_Idle_a_min)
                    {
                        parameterRounded = true;
                        idle_a_timer     = ata_Idle_a_min;
                    }
                    else if (idle_a_timer > ata_Idle_a_max)
                    {
                        parameterRounded = true;
                        idle_a_timer     = ata_Idle_a_max;
                    }
                    // convert 32bit timer to 16bit ATA timer.

                    if (idle_a_timer == UINT32_C(0))
                    {
                        ata_idle_a = UINT16_C(1);
                    }
                    else if (idle_a_timer >= UINT32_C(1) && idle_a_timer <= UINT32_C(65535))
                    {
                        ata_idle_a = C_CAST(uint16_t, idle_a_timer);
                    }
                    else if (idle_a_timer >= UINT32_C(65536) && idle_a_timer <= UINT32_C(39321000))
                    {
                        timerUnits = true;
                        ata_idle_a = C_CAST(uint16_t, (idle_a_timer / UINT32_C(600)));
                        if (idle_a_timer % UINT32_C(600))
                        {
                            parameterRounded = true;
                        }
                    }
                    else
                    {
                        timerUnits = true;
                        ata_idle_a = UINT16_C(0xFFFF);
                    }
                    ret =
                        ata_EPC_Set_Power_Condition_Timer(device, 0x81, ata_idle_a, timerUnits, idle_a, saveParameters);
                    if (ret != SUCCESS)
                    {
                        commandSequenceError = true;
                    }
                }
                if (change_idle_b)
                {

                    // first check if we need to round to a min or max for the drive...
                    uint32_t ata_Idle_b_min = M_BytesTo4ByteValue(epcLog[timerOffset + 23], epcLog[timerOffset + 22],
                                                                  epcLog[timerOffset + 21], epcLog[timerOffset + 20]);
                    uint32_t ata_Idle_b_max = M_BytesTo4ByteValue(epcLog[timerOffset + 24], epcLog[timerOffset + 25],
                                                                  epcLog[timerOffset + 26], epcLog[timerOffset + 27]);
                    uint16_t ata_idle_b     = UINT16_C(0);
                    bool     timerUnits     = false;
                    timerOffset             = 64;
                    if (idle_b_timer < ata_Idle_b_min)
                    {
                        parameterRounded = true;
                        idle_b_timer     = ata_Idle_b_min;
                    }
                    else if (idle_b_timer > ata_Idle_b_max)
                    {
                        parameterRounded = true;
                        idle_b_timer     = ata_Idle_b_max;
                    }
                    // convert 32bit timer to 16bit ATA timer.

                    if (idle_b_timer == UINT32_C(0))
                    {
                        ata_idle_b = UINT16_C(1);
                    }
                    else if (idle_b_timer >= UINT32_C(1) && idle_b_timer <= UINT32_C(65535))
                    {
                        ata_idle_b = C_CAST(uint16_t, idle_b_timer);
                    }
                    else if (idle_b_timer >= UINT32_C(65536) && idle_b_timer <= UINT32_C(39321000))
                    {
                        timerUnits = true;
                        ata_idle_b = C_CAST(uint16_t, (idle_b_timer / UINT32_C(600)));
                        if (idle_b_timer % UINT32_C(600))
                        {
                            parameterRounded = true;
                        }
                    }
                    else
                    {
                        timerUnits = true;
                        ata_idle_b = UINT16_C(0xFFFF);
                    }
                    ret =
                        ata_EPC_Set_Power_Condition_Timer(device, 0x82, ata_idle_b, timerUnits, idle_b, saveParameters);
                    if (ret != SUCCESS)
                    {
                        commandSequenceError = true;
                    }
                }
                if (change_idle_c)
                {
                    // first check if we need to round to a min or max for the drive...
                    uint32_t ata_Idle_c_min = M_BytesTo4ByteValue(epcLog[timerOffset + 23], epcLog[timerOffset + 22],
                                                                  epcLog[timerOffset + 21], epcLog[timerOffset + 20]);
                    uint32_t ata_Idle_c_max = M_BytesTo4ByteValue(epcLog[timerOffset + 24], epcLog[timerOffset + 25],
                                                                  epcLog[timerOffset + 26], epcLog[timerOffset + 27]);
                    uint16_t ata_idle_c     = UINT16_C(0);
                    bool     timerUnits     = false;
                    timerOffset             = 128;
                    if (idle_c_timer < ata_Idle_c_min)
                    {
                        parameterRounded = true;
                        idle_c_timer     = ata_Idle_c_min;
                    }
                    else if (idle_c_timer > ata_Idle_c_max)
                    {
                        parameterRounded = true;
                        idle_c_timer     = ata_Idle_c_max;
                    }
                    // convert 32bit timer to 16bit ATA timer.

                    if (idle_c_timer == UINT32_C(0))
                    {
                        ata_idle_c = UINT16_C(1);
                    }
                    else if (idle_c_timer >= UINT32_C(1) && idle_c_timer <= UINT32_C(65535))
                    {
                        ata_idle_c = C_CAST(uint16_t, idle_c_timer);
                    }
                    else if (idle_c_timer >= UINT32_C(65536) && idle_c_timer <= UINT32_C(39321000))
                    {
                        timerUnits = true;
                        ata_idle_c = C_CAST(uint16_t, (idle_c_timer / UINT32_C(600)));
                        if (idle_c_timer % UINT32_C(600))
                        {
                            parameterRounded = true;
                        }
                    }
                    else
                    {
                        timerUnits = true;
                        ata_idle_c = UINT16_C(0xFFFF);
                    }
                    ret =
                        ata_EPC_Set_Power_Condition_Timer(device, 0x83, ata_idle_c, timerUnits, idle_c, saveParameters);
                    if (ret != SUCCESS)
                    {
                        commandSequenceError = true;
                    }
                }
                if (change_standby_y)
                {
                    // first check if we need to round to a min or max for the drive...
                    uint32_t ata_Standby_y_min =
                        M_BytesTo4ByteValue(epcLog[timerOffset + 23], epcLog[timerOffset + 22],
                                            epcLog[timerOffset + 21], epcLog[timerOffset + 20]);
                    uint32_t ata_Standby_y_max =
                        M_BytesTo4ByteValue(epcLog[timerOffset + 24], epcLog[timerOffset + 25],
                                            epcLog[timerOffset + 26], epcLog[timerOffset + 27]);
                    uint16_t ata_standby_y = UINT16_C(0);
                    bool     timerUnits    = false;
                    timerOffset            = 384 + LEGACY_DRIVE_SEC_SIZE;
                    if (standby_y_timer < ata_Standby_y_min)
                    {
                        parameterRounded = true;
                        standby_y_timer  = ata_Standby_y_min;
                    }
                    else if (standby_y_timer > ata_Standby_y_max)
                    {
                        parameterRounded = true;
                        standby_y_timer  = ata_Standby_y_max;
                    }
                    // convert 32bit timer to 16bit ATA timer.

                    if (standby_y_timer == UINT32_C(0))
                    {
                        ata_standby_y = UINT16_C(1);
                    }
                    else if (standby_y_timer >= UINT32_C(1) && standby_y_timer <= UINT32_C(65535))
                    {
                        ata_standby_y = C_CAST(uint16_t, standby_y_timer);
                    }
                    else if (standby_y_timer >= UINT32_C(65536) && standby_y_timer <= UINT32_C(39321000))
                    {
                        timerUnits    = true;
                        ata_standby_y = C_CAST(uint16_t, (standby_y_timer / UINT32_C(600)));
                        if (standby_y_timer % UINT32_C(600))
                        {
                            parameterRounded = true;
                        }
                    }
                    else
                    {
                        timerUnits    = true;
                        ata_standby_y = UINT16_C(0xFFFF);
                    }
                    ret = ata_EPC_Set_Power_Condition_Timer(device, 0x01, ata_standby_y, timerUnits, standby_y,
                                                            saveParameters);
                    if (ret != SUCCESS)
                    {
                        commandSequenceError = true;
                    }
                }
                if (change_standby_z)
                {
                    // first check if we need to round to a min or max for the drive...
                    uint32_t ata_Standby_z_min =
                        M_BytesTo4ByteValue(epcLog[timerOffset + 23], epcLog[timerOffset + 22],
                                            epcLog[timerOffset + 21], epcLog[timerOffset + 20]);
                    uint32_t ata_Standby_z_max =
                        M_BytesTo4ByteValue(epcLog[timerOffset + 24], epcLog[timerOffset + 25],
                                            epcLog[timerOffset + 26], epcLog[timerOffset + 27]);
                    uint16_t ata_standby_z = UINT16_C(0);
                    bool     timerUnits    = false;
                    timerOffset            = 448 + LEGACY_DRIVE_SEC_SIZE;
                    if (standby_z_timer < ata_Standby_z_min)
                    {
                        parameterRounded = true;
                        standby_z_timer  = ata_Standby_z_min;
                    }
                    else if (standby_z_timer > ata_Standby_z_max)
                    {
                        parameterRounded = true;
                        standby_z_timer  = ata_Standby_z_max;
                    }
                    // convert 32bit timer to 16bit ATA timer.

                    if (standby_z_timer == UINT32_C(0))
                    {
                        ata_standby_z = UINT16_C(1);
                    }
                    else if (standby_z_timer >= UINT32_C(1) && standby_z_timer <= UINT32_C(65535))
                    {
                        ata_standby_z = C_CAST(uint16_t, standby_z_timer);
                    }
                    else if (standby_z_timer >= UINT32_C(65536) && standby_z_timer <= UINT32_C(39321000))
                    {
                        timerUnits    = true;
                        ata_standby_z = C_CAST(uint16_t, (standby_z_timer / UINT32_C(600)));
                        if (standby_z_timer % UINT32_C(600))
                        {
                            parameterRounded = true;
                        }
                    }
                    else
                    {
                        timerUnits    = true;
                        ata_standby_z = UINT16_C(0xFFFF);
                    }
                    ret = ata_EPC_Set_Power_Condition_Timer(device, 0x00, ata_standby_z, timerUnits, standby_z,
                                                            saveParameters);
                    if (ret != SUCCESS)
                    {
                        commandSequenceError = true;
                    }
                }
                // set errors if we had any
                if (commandSequenceError)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
                else if (parameterRounded)
                {
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_RECOVERED_ERROR, 0x37, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else
            {
                // Set an error since we couldn't read the log!
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND,
                                               0x2C, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               M_NULLPTR, 0);
            }
        }
    }
    else
    {
        // device doesn't support EPC
        if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && pageLength != 0x0026) ||
            ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 &&
             get_bit_range_uint8(ptrToBeginningOfModePage[2], 7, 6) != 0) // PM_BG_PRECEDENCE
            || ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[2], 5, 1) != 0) // reserved
            ||
            ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[2] & BIT0) // standby y is set
            || ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[3], 7, 4) != 0)                            // reserved
            || ((fieldPointer = 3) != 0 && (bitPointer = 3) != 0 && ptrToBeginningOfModePage[3] & BIT3) // idle c is set
            || ((fieldPointer = 3) != 0 && (bitPointer = 2) != 0 && ptrToBeginningOfModePage[3] & BIT2) // idle b is set
            || ((fieldPointer = 3) != 0 && (bitPointer = 1) != 0 && ptrToBeginningOfModePage[3] & BIT1) // idle a is set
            || ((fieldPointer = 24) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[24] == 0) // reserved
            || ((fieldPointer = 25) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[25] == 0) // reserved
            || ((fieldPointer = 26) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[26] == 0) // reserved
            || ((fieldPointer = 27) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[27] == 0) // reserved
            || ((fieldPointer = 28) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[28] == 0) // reserved
            || ((fieldPointer = 29) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[29] == 0) // reserved
            || ((fieldPointer = 30) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[30] == 0) // reserved
            || ((fieldPointer = 31) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[31] == 0) // reserved
            || ((fieldPointer = 32) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[32] == 0) // reserved
            || ((fieldPointer = 33) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[33] == 0) // reserved
            || ((fieldPointer = 34) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[34] == 0) // reserved
            || ((fieldPointer = 35) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[35] == 0) // reserved
            || ((fieldPointer = 36) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[36] == 0) // reserved
            || ((fieldPointer = 37) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[37] == 0) // reserved
            || ((fieldPointer = 38) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[38] == 0) // reserved
            || ((fieldPointer = 39) != 0 && (bitPointer = 7) != 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[39], 7, 6) != 1) // CCF IDLE
            || ((fieldPointer = 39) != 0 && (bitPointer = 5) != 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[39], 5, 4) != 1) // CCF STANDBY
            || ((fieldPointer = 39) != 0 && (bitPointer = 3) != 0 &&
                get_bit_range_uint8(ptrToBeginningOfModePage[39], 3, 2) != 1) // CCF STOPPED
            || ((fieldPointer = 39) != 0 && (bitPointer = 1) != 0 && (ptrToBeginningOfModePage[39] & BIT1) != 0) ||
            ((fieldPointer = 39) != 0 && (bitPointer = 0) == 0 && (ptrToBeginningOfModePage[39] & BIT0) != 0))
        {
            if (bitPointer == 0)
            {
                uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
                uint8_t counter         = UINT8_C(0);
                if (fieldPointer == 2)
                {
                    reservedByteVal = ptrToBeginningOfModePage[fieldPointer] & 0x3E;
                }
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer =
                    counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
            }
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
        }
        else
        {
            if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word049)) &&
                le16_to_host(device->drive_info.IdentifyData.ata.Word049) & BIT13)
            {
                uint32_t standby_z_timer =
                    M_BytesTo4ByteValue(ptrToBeginningOfModePage[8], ptrToBeginningOfModePage[9],
                                        ptrToBeginningOfModePage[10], ptrToBeginningOfModePage[11]);
                if (standby_z_timer == 0)
                {
                    ret = ata_Standby_Immediate(device);
                }
                else
                {
                    uint8_t ataCountField = UINT8_C(0xFD); // to catch all other cases
                    if (standby_z_timer >= UINT32_C(1) && standby_z_timer <= UINT32_C(12000))
                    {
                        ataCountField = C_CAST(uint8_t, ((standby_z_timer - UINT32_C(1)) / UINT32_C(50)) + UINT32_C(1));
                    }
                    else if (standby_z_timer >= UINT32_C(12001) && standby_z_timer <= UINT32_C(12600))
                    {
                        ataCountField = 0xFC;
                    }
                    else if (standby_z_timer >= UINT32_C(12601) && standby_z_timer <= UINT32_C(12750))
                    {
                        ataCountField = 0xFF;
                    }
                    else if (standby_z_timer >= UINT32_C(12751) && standby_z_timer <= UINT32_C(17999))
                    {
                        ataCountField = 0xF1;
                    }
                    else if (standby_z_timer >= UINT32_C(18000) && standby_z_timer <= UINT32_C(198000))
                    {
                        ataCountField = C_CAST(uint8_t, (standby_z_timer / UINT32_C(18000)) + UINT32_C(240));
                    }
                    ret = ata_Standby(device, ataCountField);
                }
                if (ret != SUCCESS)
                {
                    // command sequence error
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x2C, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
                }
            }
            else
            {
                fieldPointer = UINT16_C(3);
                bitPointer   = UINT8_C(0);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
        }
    }
    return ret;
}

static eReturnValues translate_Mode_Select_ATA_Power_Condition_1A_F1(tDevice*       device,
                                                                     ScsiIoCtx*     scsiIoCtx,
                                                                     const uint8_t* ptrToBeginningOfModePage,
                                                                     uint16_t       pageLength)
{
    eReturnValues ret = SUCCESS;
    uint16_t      dataOffset =
        C_CAST(uint16_t, ptrToBeginningOfModePage -
                             scsiIoCtx->pdata); // to be used when setting which field is invalid in parameter list
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (pageLength != 0x000C)
    {
        fieldPointer = 2 + dataOffset;
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (((fieldPointer = 4) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[4] != 0) ||
        ((fieldPointer = 5) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[5] & 0xFE) ||
        ((fieldPointer = 7) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[7] != 0) ||
        ((fieldPointer = 8) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[8] != 0) ||
        ((fieldPointer = 9) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[9] != 0) ||
        ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[10] != 0) ||
        ((fieldPointer = 11) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[11] != 0) ||
        ((fieldPointer = 12) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[12] != 0) ||
        ((fieldPointer = 13) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[13] != 0) ||
        ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[14] != 0) ||
        ((fieldPointer = 15) != 0 && (bitPointer = 0) == 0 && ptrToBeginningOfModePage[15] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        fieldPointer += dataOffset;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    // check if the APMP bit is set. Only make changes if it is set
    if (ptrToBeginningOfModePage[5] & BIT0)
    {
        // make a change to the APM value
        if (ptrToBeginningOfModePage[6] == 0)
        {
            ret = ata_Set_Features(device, SF_DISABLE_APM_FEATURE, 0, 0, 0, 0);
        }
        else
        {
            ret = ata_Set_Features(device, SF_ENABLE_APM_FEATURE, ptrToBeginningOfModePage[6], 0, 0, 0);
        }
        if (ret != SUCCESS)
        {
            ret = FAILURE;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ABORTED_COMMAND, 0x44,
                                           0x71, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR,
                                           0);
        }
    }
    return ret;
}

static eReturnValues translate_SCSI_Mode_Select_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // There are only three pages that we care about changing...Power conditions and caching and control
    bool pageFormat = false;
    // bool saveParameters = false;
    bool     tenByteCommand      = false;
    uint16_t parameterListLength = UINT16_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (scsiIoCtx->cdb[OPERATION_CODE] == 0x15 || scsiIoCtx->cdb[OPERATION_CODE] == 0x55)
    {
        if (scsiIoCtx->cdb[1] & BIT4)
        {
            pageFormat = true;
        }
        //      if (scsiIoCtx->cdb[1] & BIT0)
        //      {
        //          saveParameters = true;
        //      }
        parameterListLength = scsiIoCtx->cdb[4];
        if (scsiIoCtx->cdb[OPERATION_CODE] == 0x15) // mode select 6
        {
            uint8_t byte1 =
                scsiIoCtx->cdb[1] & 0x11; // removing PF and SP bits since we can handle those, but not any other bits
            if (((fieldPointer = 1) != 0 && byte1 != 0) ||
                ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0) ||
                ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0))
            {
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    if (fieldPointer == 1)
                    {
                        reservedByteVal = byte1;
                    }

                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                }
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                // invalid field in cdb
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return ret;
            }
        }
        else if (scsiIoCtx->cdb[OPERATION_CODE] == 0x55) // mode select 10
        {
            uint8_t byte1 =
                scsiIoCtx->cdb[1] & 0x11; // removing PF and SP bits since we can handle those, but not any other bits
            tenByteCommand      = true;
            parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);

            if (((fieldPointer = 1) != 0 && byte1 != 0) ||
                ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0) ||
                ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0) ||
                ((fieldPointer = 4) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[4] != 0) ||
                ((fieldPointer = 5) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[5] != 0) ||
                ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[6] != 0))
            {
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    if (fieldPointer == 1)
                    {
                        reservedByteVal = byte1;
                    }

                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                }
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                                fieldPointer);
                // invalid field in cdb
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return ret;
            }
        }
    }
    else
    {
        // invalid operation code
        fieldPointer = UINT16_C(0);
        bitPointer   = UINT8_C(7);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    if (pageFormat)
    {
        bool     longLBA               = false;
        uint16_t blockDescriptorLength = scsiIoCtx->pdata[MODE_HEADER_6_BLK_DESC_OFFSET];
        uint8_t  headerLength;
        uint8_t  modePage;
        bool     subPageFormat;
        // bool parametersSaveble = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & BIT7;
        uint8_t  subpage;
        uint16_t pageLength;

        if (parameterListLength == 0)
        {
            // Spec says this is not to be considered an error. Since we didn't get any data, there is nothing to do but
            // return good status - TJE
            return SUCCESS;
        }
        bitPointer = UINT8_C(0);
        // uint16_t modeDataLength = scsiIoCtx->pdata[0];
        // uint8_t deviceSpecificParameter = scsiIoCtx->pdata[2];
        // TODO: Validate writeProtected and dpoFua bits.
        if (tenByteCommand)
        {
            // modeDataLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[0], scsiIoCtx->pdata[1]);
            if (scsiIoCtx->pdata[MODE_HEADER_10_MEDIUM_TYPE_OFFSET] != 0) // mediumType
            {
                fieldPointer = UINT16_C(2);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if ((scsiIoCtx->pdata[MODE_HEADER_10_DEV_SPECIFIC] & 0x7F) !=
                0) // device specific parameter - WP bit is ignored in SBC. dpofua is reserved.
            {
                fieldPointer = UINT16_C(3);
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                }
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if (scsiIoCtx->pdata[4])
            {
                longLBA = true;
            }
            blockDescriptorLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[MODE_HEADER_10_BLK_DESC_OFFSET],
                                                        scsiIoCtx->pdata[MODE_HEADER_10_BLK_DESC_OFFSET + 1]);
            if (((fieldPointer = 4) != 0 && scsiIoCtx->pdata[4] & 0xFE)  // reserved bits/bytes
                || ((fieldPointer = 5) != 0 && scsiIoCtx->pdata[5] != 0) // reserved bits/bytes
            )
            {
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                }
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
        }
        else
        {
            if (scsiIoCtx->pdata[MODE_HEADER_6_MEDIUM_TYPE_OFFSET] != 0) // mediumType
            {
                fieldPointer = UINT16_C(1);
                bitPointer   = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if ((scsiIoCtx->pdata[MODE_HEADER_6_DEV_SPECIFIC] & 0x7F) !=
                0) // device specific parameter - WP bit is ignored in SBC. dpofua is reserved.
            {
                fieldPointer = UINT16_C(2);
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                }
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
        }

        if (blockDescriptorLength > 0)
        {
            // check the block descriptor to make sure it's valid
            if (tenByteCommand)
            {
                if (longLBA)
                {
                    uint64_t numberOfLogicalBlocks =
                        M_BytesTo8ByteValue(scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 0],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 1],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 2],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 3],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 4],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 5],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 6],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 7]);
                    uint32_t logicalBlockLength =
                        M_BytesTo4ByteValue(scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 12],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 13],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 14],
                                            scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 15]);
                    if (numberOfLogicalBlocks != device->drive_info.deviceMaxLba)
                    {
                        bitPointer   = UINT8_C(7);
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 0;
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 8) != 0 &&
                         scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 8] != 0) ||
                        ((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 9) != 0 &&
                         scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 9] != 0) ||
                        ((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 10) != 0 &&
                         scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 10] != 0) ||
                        ((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 11) != 0 &&
                         scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 11] != 0))
                    {
                        uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                        uint8_t counter         = UINT8_C(0);
                        while (reservedByteVal > 0 && counter < 8)
                        {
                            reservedByteVal >>= 1;
                            ++counter;
                        }
                        bitPointer =
                            counter -
                            1; // because we should always get a count of at least 1 if here and bits are zero indexed
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (logicalBlockLength != device->drive_info.deviceBlockSize)
                    {
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 12;
                        bitPointer   = UINT8_C(7);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                }
                else
                {
                    // short block descriptor(s)...should only have 1!
                    uint32_t numberOfLogicalBlocks = M_BytesTo4ByteValue(scsiIoCtx->pdata[8], scsiIoCtx->pdata[9],
                                                                         scsiIoCtx->pdata[10], scsiIoCtx->pdata[11]);
                    uint32_t logicalBlockLength =
                        M_BytesTo4ByteValue(0, scsiIoCtx->pdata[13], scsiIoCtx->pdata[14], scsiIoCtx->pdata[15]);
                    if (numberOfLogicalBlocks != device->drive_info.deviceMaxLba)
                    {
                        bitPointer   = UINT8_C(7);
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 0;
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 4] != 0) // short header + 4 bytes
                    {

                        uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                        uint8_t counter         = UINT8_C(0);
                        fieldPointer            = MODE_PARAMETER_HEADER_10_LEN + 4;
                        while (reservedByteVal > 0 && counter < 8)
                        {
                            reservedByteVal >>= 1;
                            ++counter;
                        }
                        bitPointer =
                            counter -
                            1; // because we should always get a count of at least 1 if here and bits are zero indexed
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (logicalBlockLength != device->drive_info.deviceBlockSize)
                    {
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 5;
                        bitPointer   = UINT8_C(7);
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true,
                                                                        bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(
                            scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                            device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                }
            }
            else
            {
                // short block descriptor(s)...should only have 1!
                uint32_t numberOfLogicalBlocks = M_BytesTo4ByteValue(scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 0],
                                                                     scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 1],
                                                                     scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 2],
                                                                     scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 3]);
                uint32_t logicalBlockLength = M_BytesTo4ByteValue(0, scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 5],
                                                                  scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 6],
                                                                  scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 7]);
                if (numberOfLogicalBlocks != device->drive_info.deviceMaxLba)
                {
                    bitPointer   = UINT8_C(7);
                    fieldPointer = MODE_PARAMETER_HEADER_6_LEN + 0;
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return NOT_SUPPORTED;
                }
                if (scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 4] != 0) // short header + 4 bytes
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter         = UINT8_C(0);
                    fieldPointer            = MODE_PARAMETER_HEADER_6_LEN + 4;

                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer =
                        counter -
                        1; // because we should always get a count of at least 1 if here and bits are zero indexed
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return NOT_SUPPORTED;
                }
                if (logicalBlockLength != device->drive_info.deviceBlockSize)
                {
                    fieldPointer = MODE_PARAMETER_HEADER_6_LEN + 5;
                    bitPointer   = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return NOT_SUPPORTED;
                }
            }
        }
        headerLength = MODE_PARAMETER_HEADER_6_LEN;
        if (tenByteCommand)
        {
            headerLength = MODE_PARAMETER_HEADER_10_LEN;
        }
        // time to call the function that handles the changes for the mode page requested...save all this info and pass
        // it in for convenience in that function
        modePage      = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & 0x3F;
        subPageFormat = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & BIT6;
        // bool parametersSaveble = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & BIT7;
        subpage    = 0;
        pageLength = scsiIoCtx->pdata[headerLength + blockDescriptorLength + 1];
        if (subPageFormat)
        {
            subpage    = scsiIoCtx->pdata[headerLength + blockDescriptorLength + 1];
            pageLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[headerLength + blockDescriptorLength + 2],
                                             scsiIoCtx->pdata[headerLength + blockDescriptorLength + 3]);
        }
        switch (modePage)
        {
        case 0x08:
            switch (subpage)
            {
            case 0: // caching mode page
                ret = translate_Mode_Select_Caching_08h(
                    device, scsiIoCtx, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
                break;
            default:
                // invalid field in parameter list...we don't support this page
                fieldPointer =
                    C_CAST(uint16_t, headerLength + blockDescriptorLength + UINT16_C(1)); // plus one for subpage
                bitPointer = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
            }
            break;
        case 0x0A:
            switch (subpage)
            {
            case 0: // control mode page
                ret = translate_Mode_Select_Control_0Ah(
                    device, scsiIoCtx, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
                break;
            default:
                fieldPointer =
                    C_CAST(uint16_t, headerLength + blockDescriptorLength + UINT16_C(1)); // plus one for subpage
                bitPointer = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
            }
            break;
        case 0x1A:
            switch (subpage)
            {
            case 0: // power conditions
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
                ret = translate_Mode_Select_Power_Conditions_1A(
                    device, scsiIoCtx, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
                break;
#else                  // SAT_SPEC_SUPPORTED
                fieldPointer = C_CAST(
                    uint16_t, headerLength + blockDescriptorLength); // we don't support page 0 in this version of SAT
                bitPointer = UINT8_C(5);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
#endif                 // SAT_SPEC_SUPPORTED
            case 0xF1: // ATA power conditions (APM)
                if (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                        le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
                    le16_to_host(device->drive_info.IdentifyData.ata.Word083) &
                        BIT3) // only support this page if APM is supported-TJE
                {
                    ret = translate_Mode_Select_ATA_Power_Condition_1A_F1(
                        device, scsiIoCtx, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
                }
                else
                {
                    // invalid field in parameter list...we don't support this page
                    fieldPointer =
                        C_CAST(uint16_t, headerLength + blockDescriptorLength + UINT16_C(1)); // plus one for subpage
                    bitPointer = UINT8_C(7);
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                    fieldPointer);
                    ret = NOT_SUPPORTED;
                    set_Sense_Data_For_Translation(
                        scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0,
                        device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                break;
            default:
                fieldPointer =
                    C_CAST(uint16_t, headerLength + blockDescriptorLength + UINT16_C(1)); // plus one for subpage
                bitPointer = UINT8_C(7);
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                                fieldPointer);
                ret = NOT_SUPPORTED;
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST,
                                               0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                               senseKeySpecificDescriptor, 1);
                break;
            }
            break;
        default:
            // invalid field in parameter list...we don't support this page
            fieldPointer = headerLength + blockDescriptorLength;
            bitPointer   = UINT8_C(5);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer,
                                                            fieldPointer);
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            break;
        }
    }
    else
    {
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(4);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
    }
    return ret;
}

static eReturnValues translate_SCSI_Zone_Management_In_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // 95
    bool      partialBit    = false;
    bool      localMemory   = false;
    eZMAction serviceAction = scsiIoCtx->cdb[1] & 0x1F;
    uint64_t  zoneStartLBA =
        M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                            scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    uint8_t  reportingOptions = scsiIoCtx->cdb[14] & 0x3f;
    uint8_t* dataBuf          = M_NULLPTR;
    uint32_t dataBufLength    = UINT32_C(0);
    uint32_t allocationLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (scsiIoCtx->cdb[14] & BIT7)
    {
        partialBit = true;
    }
    if ((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        // invalid field in cdb (reserved field)
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    // allocate memory if it's not to a round 512 byte block size
    if ((allocationLength % 512) != 0)
    {
        dataBufLength = (allocationLength + 511) / 512;
        dataBuf       = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(dataBufLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        localMemory = true;
    }
    else if (allocationLength == 0)
    {
        dataBufLength = 512;
        dataBuf       = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(dataBufLength, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!dataBuf)
        {
            return MEMORY_FAILURE;
        }
        localMemory = true;
    }
    else
    {
        dataBufLength = allocationLength;
        dataBuf       = scsiIoCtx->pdata;
    }
    switch (serviceAction)
    {
    case ZM_ACTION_REPORT_ZONES:
        if ((fieldPointer = 14) != 0 && (bitPointer = 6) != 0 && (scsiIoCtx->cdb[14] & BIT6) != 0)
        {
            // reserved bit is set
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            // invalid field in CDB
            ret = NOT_SUPPORTED;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            return ret;
        }
        else
        {
            if (SUCCESS != ata_Report_Zones_Ext(device, reportingOptions, partialBit,
                                                C_CAST(uint16_t, dataBufLength / UINT16_C(512)), zoneStartLBA, dataBuf,
                                                dataBufLength))
            {
                set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                        scsiIoCtx->senseDataSize);
                return FAILURE;
            }
            else
            {
                // need to byte swap fields to SCSI endianness.
                // zone list length
                byte_Swap_32(C_CAST(uint32_t*, &dataBuf[0]));
                // max lba
                byte_Swap_64(C_CAST(uint64_t*, &dataBuf[8]));
                // now loop through the zone descriptors...
                for (uint32_t iter = UINT32_C(64); iter < dataBufLength; iter += 64)
                {
                    // first two bytes are bit fields that translate exactly the same...leave them alone
                    // zone length
                    byte_Swap_64(C_CAST(uint64_t*, &dataBuf[iter + 8]));
                    // zone start LBA
                    byte_Swap_64(C_CAST(uint64_t*, &dataBuf[iter + 16]));
                    // write pointer LBA
                    byte_Swap_64(C_CAST(uint64_t*, &dataBuf[iter + 24]));
                }
            }
        }
        break;
    default:
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(4);
        // invalid field in cdb (reserved field)
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (localMemory && ret == SUCCESS && allocationLength > 0 && dataBuf)
    {
        // copy the data based on allocation length
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, dataBuf, M_Min(scsiIoCtx->dataLength, dataBufLength));
    }
    safe_free_aligned(&dataBuf);
    return ret;
}

static eReturnValues translate_SCSI_Zone_Management_Out_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // 94
    bool allBit = false;
    // bool localMemory = false;
    eZMAction serviceAction = scsiIoCtx->cdb[1] & 0x1F;
    uint64_t  zoneID = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5],
                                           scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    // uint8_t reportingOptions = scsiIoCtx->cdb[14] & 0x3f;
    // uint8_t *dataBuf = M_NULLPTR;
    // uint32_t dataBufLength = UINT32_C(0);
    // uint32_t allocationLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12],
    // scsiIoCtx->cdb[13]); if (scsiIoCtx->cdb[14] & BIT0)
    //{
    //     allBit = true;
    // }
    ////allocate memory if it's not to a round 512 byte block size
    // if ((allocationLength % 512) != 0)
    //{
    //     dataBufLength = (allocationLength + 511) / 512;
    //     dataBuf = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataBufLength, sizeof(uint8_t),
    //     device->os_info.minimumAlignment)); localMemory = true;
    // }
    // else if (allocationLength == 0)
    //{
    //     dataBufLength = 512;
    //     dataBuf = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataBufLength, sizeof(uint8_t),
    //     device->os_info.minimumAlignment)); localMemory = true;
    // }
    // else
    //{
    //     dataBufLength = allocationLength;
    //     dataBuf = scsiIoCtx->pdata;
    // }
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0) ||
        ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0) ||
        ((fieldPointer = 11) != 0 && scsiIoCtx->cdb[11] != 0) ||
        ((fieldPointer = 12) != 0 && scsiIoCtx->cdb[12] != 0) ||
        ((fieldPointer = 13) != 0 && scsiIoCtx->cdb[13] != 0) ||
        ((fieldPointer = 14) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[14], 7, 1) != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        // invalid field in cdb (reserved field)
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    switch (serviceAction)
    {
    case ZM_ACTION_CLOSE_ZONE:
        if (SUCCESS != ata_Close_Zone_Ext(device, allBit, zoneID, 0))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
        }
        break;
    case ZM_ACTION_FINISH_ZONE:
        if (SUCCESS != ata_Finish_Zone_Ext(device, allBit, zoneID, 0))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
        }
        break;
    case ZM_ACTION_OPEN_ZONE:
        if (SUCCESS != ata_Open_Zone_Ext(device, allBit, zoneID, 0))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
        }
        break;
    case ZM_ACTION_RESET_WRITE_POINTERS:
        if (SUCCESS != ata_Reset_Write_Pointers_Ext(device, allBit, zoneID, 0))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
        }
        break;
    default:
        // invalid field in CDB
        fieldPointer = UINT16_C(1);
        bitPointer   = UINT8_C(4);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    // if (localMemory && ret == SUCCESS && allocationLength > 0 && dataBuf)
    //{
    //     //copy the data based on allocation length
    //     safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, dataBuf, M_Min(scsiIoCtx->dataLength, dataBufLength));
    // }
    // safe_free_aligned_core(C_CAST(void**, &dataBuf));
    return ret;
}

static eReturnValues translate_SCSI_Set_Timestamp_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    uint32_t      parameterListLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, dataBuf, 12);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0) ||
        ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0) ||
        ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0) || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) ||
        ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (parameterListLength > 0 && scsiIoCtx->pdata)
    {
        uint64_t timeStamp =
            M_BytesTo8ByteValue(0, 0, dataBuf[4], dataBuf[5], dataBuf[6], dataBuf[7], dataBuf[8], dataBuf[9]);
        safe_memcpy(dataBuf, 12, scsiIoCtx->pdata, M_Min(12, parameterListLength));

        if (SUCCESS != ata_Set_Date_And_Time(device, timeStamp))
        {
            set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                    scsiIoCtx->senseDataSize);
        }
    }
    else
    {
        // nothing to do!
        ret = SUCCESS;
    }
    return ret;
}

static eReturnValues translate_SCSI_Report_Timestamp_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, dataBuf, 12);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, generalStats, LEGACY_DRIVE_SEC_SIZE);
    uint32_t allocationLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0) ||
        ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0) || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0) ||
        ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0) || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0) ||
        ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (SUCCESS == ata_Read_Log_Ext(device, ATA_DEVICE_STATS_LOG_LIST, ATA_DEVICE_STATS_LOG_GENERAL, generalStats,
                                    LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported,
                                    0))
    {
        uint64_t* qwordPtr     = C_CAST(uint64_t*, &generalStats[0]);
        uint64_t  ataTimestamp = le64_to_host(qwordPtr[7]) & MAX_48_BIT_LBA;
        // set up general data
        dataBuf[0] = 0x00;
        dataBuf[1] = 0x0A;
        if (le64_to_host(qwordPtr[7]) & BIT62)
        {
            dataBuf[2] |= BIT1; // timestamp origin field set to 010b
        }
        dataBuf[3]  = RESERVED;
        dataBuf[4]  = M_Byte5(ataTimestamp);
        dataBuf[5]  = M_Byte4(ataTimestamp);
        dataBuf[6]  = M_Byte3(ataTimestamp);
        dataBuf[7]  = M_Byte2(ataTimestamp);
        dataBuf[8]  = M_Byte1(ataTimestamp);
        dataBuf[9]  = M_Byte0(ataTimestamp);
        dataBuf[10] = RESERVED;
        dataBuf[11] = RESERVED;
    }
    else
    {
        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense,
                                scsiIoCtx->senseDataSize);
    }
    if (allocationLength > 0 && scsiIoCtx->pdata && ret == SUCCESS)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, dataBuf, M_Min(12, allocationLength));
    }
    return ret;
}

static eReturnValues translate_SCSI_Read_Media_Serial_Number_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret          = SUCCESS;
    uint8_t       bitPointer   = UINT8_C(0);
    uint16_t      fieldPointer = UINT16_C(0);
    uint8_t*      iddataPtr    = M_REINTERPRET_CAST(uint8_t*, &device->drive_info.IdentifyData.ata);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, mediaSerialNumberPage, 65);
    uint32_t allocationLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    // filter out unsupported fields/bits (SPC3)
    if ((fieldPointer = 1 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0) ||
        (fieldPointer = 2 && scsiIoCtx->cdb[2] != 0) || (fieldPointer = 3 && scsiIoCtx->cdb[3] != 0) ||
        (fieldPointer = 4 && scsiIoCtx->cdb[4] != 0) || (fieldPointer = 5 && scsiIoCtx->cdb[5] != 0) ||
        (fieldPointer = 10 && scsiIoCtx->cdb[10] != 0))
    {
        // invalid field in CDB
        uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
        uint8_t counter         = UINT8_C(0);
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        ret = NOT_SUPPORTED;
        return ret;
    }
    // word 84 for supported, word 87 for validity
    if ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word084)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word084) & BIT2) &&
        (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word087)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word087) & BIT2))
    {
        DECLARE_ZERO_INIT_ARRAY(char, ataMediaSN, 61);
        safe_memcpy(ataMediaSN, 61, iddataPtr + 352, 60);
        byte_Swap_String_Len(ataMediaSN, 60);
        mediaSerialNumberPage[0] = 0;
        mediaSerialNumberPage[1] = 0;
        mediaSerialNumberPage[2] = 0;
        mediaSerialNumberPage[3] = 15; // length
        // now set the media serial number
        safe_memcpy(&mediaSerialNumberPage[4], 61, ataMediaSN, 60);
    }
    if (scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, mediaSerialNumberPage, M_Min(allocationLength, 64));
    }
    return ret;
}

static void set_Command_Timeouts_Descriptor(uint32_t  nominalCommandProcessingTimeout,
                                            uint32_t  recommendedCommandProcessingTimeout,
                                            uint8_t*  pdata,
                                            uint32_t* offset)
{
    if (pdata && offset)
    {
        pdata[*offset + 0] = 0x00;
        pdata[*offset + 1] = 0x0A;
        pdata[*offset + 2] = RESERVED;
        // command specfic
        pdata[*offset + 3] = 0x00;
        // nominal command processing timeout
        pdata[*offset + 4] = M_Byte3(nominalCommandProcessingTimeout);
        pdata[*offset + 5] = M_Byte2(nominalCommandProcessingTimeout);
        pdata[*offset + 6] = M_Byte1(nominalCommandProcessingTimeout);
        pdata[*offset + 7] = M_Byte0(nominalCommandProcessingTimeout);
        // recommended command timeout
        pdata[*offset + 8]  = M_Byte3(recommendedCommandProcessingTimeout);
        pdata[*offset + 9]  = M_Byte2(recommendedCommandProcessingTimeout);
        pdata[*offset + 10] = M_Byte1(recommendedCommandProcessingTimeout);
        pdata[*offset + 11] = M_Byte0(recommendedCommandProcessingTimeout);
        // increment the offset
        *offset += 12;
    }
}

static eReturnValues check_Operation_Code(tDevice*  device,
                                          uint8_t   operationCode,
                                          bool      rctd,
                                          uint8_t** pdata,
                                          uint32_t* dataLength)
{
    eReturnValues ret              = SUCCESS;
    uint32_t      offset           = UINT32_C(4); // use to keep track and setup the buffer
    uint16_t      cdbLength        = UINT16_C(1); // set to 1 for the default case
    uint8_t       controlByte      = UINT8_C(0);
    bool          commandSupported = true;
    *dataLength = 4; // add onto this for each of the different commands below, then allocate memory accordingly

    if (rctd)
    {
        // add 12 bytes for room for the command timeouts descriptor to be setup
        dataLength += 12;
    }
    switch (operationCode)
    {
    case INQUIRY_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT0;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case READ_CAPACITY_10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;
        pdata[0][offset + 2] = OBSOLETE;
        pdata[0][offset + 3] = OBSOLETE;
        pdata[0][offset + 4] = OBSOLETE;
        pdata[0][offset + 5] = OBSOLETE;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = RESERVED;
        pdata[0][offset + 8] = 0;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case ATA_PASS_THROUGH_12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = 0xFE;
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = RESERVED;
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case ATA_PASS_THROUGH_16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = 0xFF;
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0xFF;
        pdata[0][offset + 15] = controlByte; // control byte
        break;
    case LOG_SENSE_CMD:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;    // leave at zero since sp bit is ignored in translation
        pdata[0][offset + 2] = 0x7F; // PC only supports 01h, hence 7F instead of FF
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
    case LOG_SELECT_CMD:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT1 | BIT0;
        pdata[0][offset + 2] = 0x7F; // PC only supports 01h, hence 7F instead of FF
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = RESERVED;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
#endif // SAT_SPEC_SUPPORTED
    case MODE_SENSE_6_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;
        pdata[0][offset + 2] = 0x3F; // PC only valid for 00 (current mode page)
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case MODE_SENSE10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3 | BIT4;
        pdata[0][offset + 2] = 0x3F; // PC only valid for 00 (current mode page)
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = RESERVED;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case MODE_SELECT_6_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT4 | BIT0;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case MODE_SELECT10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT4 | BIT0;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = RESERVED;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case READ6:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0x1F;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = controlByte;
        break;
    case READ10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3; // RARC bit support to be added later, dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0; // group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case READ12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT3; // RARC bit support to be added later, dpo ignored
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0;           // group number should be zero
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case READ16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT3; // RARC bit support to be added later, dpo ignored
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;           // group number should be zero
        pdata[0][offset + 15] = controlByte; // control byte
        break;
    case REASSIGN_BLOCKS_6:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT0 | BIT1;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case REPORT_LUNS_CMD:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = RESERVED;
        pdata[0][offset + 2]  = BIT4 | BIT0 | BIT1;
        pdata[0][offset + 3]  = RESERVED;
        pdata[0][offset + 4]  = RESERVED;
        pdata[0][offset + 5]  = RESERVED;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = RESERVED;
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case REQUEST_SENSE_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT1;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case SECURITY_PROTOCOL_IN: // fallthrough
    case SECURITY_PROTOCOL_OUT:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = 0xFF; // security protocol
        pdata[0][offset + 2]  = 0xFF; // security protocol specific
        pdata[0][offset + 3]  = 0xFF; // security protocol specific
        pdata[0][offset + 4]  = BIT7; // inc512 bit
        pdata[0][offset + 5]  = RESERVED;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = RESERVED;
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case SEND_DIAGNOSTIC_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT7 | BIT6 | BIT5 | BIT2; // self test bit and self test code field
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = 0;
        pdata[0][offset + 4] = 0;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case START_STOP_UNIT_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = 0x0F;        // power condition modifier supported
        pdata[0][offset + 4] = 0xF7;        // power condition and no_flush and loej and start supported
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case SYNCHRONIZE_CACHE_10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0; // group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case SYNCHRONIZE_CACHE_16_CMD:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = 0;
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;           // group number should be zero
        pdata[0][offset + 15] = controlByte; // control byte
        break;
    case TEST_UNIT_READY_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case UNMAP_CMD:
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word169)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0)
        {
            cdbLength = 10;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = 0; // anchor bit ignored
            pdata[0][offset + 2] = RESERVED;
            pdata[0][offset + 3] = RESERVED;
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = RESERVED;
            pdata[0][offset + 6] = 0; // group number should be zero
            pdata[0][offset + 7] = 0xFF;
            pdata[0][offset + 8] = 0xFF;
            pdata[0][offset + 9] = controlByte; // control byte
        }
        else // not supported
        {
            commandSupported = false;
        }
        break;
    case VERIFY10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT2 | BIT1; // bytecheck 11 supported
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0; // group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case VERIFY12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT2 | BIT1; // bytecheck 11 supported
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0;           // group number should be zero
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case VERIFY16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT2 | BIT1; // bytecheck 11 supported
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;           // group number should be zero
        pdata[0][offset + 15] = controlByte; // control byte
        break;
    case WRITE6:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0x1F;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = controlByte;
        break;
    case WRITE10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3; // dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0; // group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case WRITE12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT3; // dpo ignored
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0;           // group number should be zero
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case WRITE16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT3; // dpo ignored
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;           // group number should be zero
        pdata[0][offset + 15] = controlByte; // control byte
        break;
    case WRITE_AND_VERIFY_10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT2 | BIT1; // bytecheck 11 supported
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0; // group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case WRITE_AND_VERIFY_12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT2 | BIT1; // bytecheck 11 supported
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0;           // group number should be zero
        pdata[0][offset + 11] = controlByte; // control byte
        break;
    case WRITE_AND_VERIFY_16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0]  = operationCode;
        pdata[0][offset + 1]  = BIT2 | BIT1; // bytecheck 11 supported
        pdata[0][offset + 2]  = 0xFF;
        pdata[0][offset + 3]  = 0xFF;
        pdata[0][offset + 4]  = 0xFF;
        pdata[0][offset + 5]  = 0xFF;
        pdata[0][offset + 6]  = 0xFF;
        pdata[0][offset + 7]  = 0xFF;
        pdata[0][offset + 8]  = 0xFF;
        pdata[0][offset + 9]  = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;           // group number should be zero
        pdata[0][offset + 15] = controlByte; // control byte
        break;
    case WRITE_LONG_10_CMD:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT7 | BIT6 | BIT5;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0; // group number should be zero
        pdata[0][offset + 7] = 0;
        pdata[0][offset + 8] = 0;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED < 2
    case WRITE_BUFFER_CMD:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0x1F;
        pdata[0][offset + 2] = 0;
        pdata[0][offset + 3] = 0x3F;
        pdata[0][offset + 4] = 0xFE;
        pdata[0][offset + 5] = 0x00;
        pdata[0][offset + 6] = 0x3F;
        pdata[0][offset + 7] = 0xFE;
        pdata[0][offset + 8] = 0x00;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
    case READ_BUFFER_CMD:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0x03;
        pdata[0][offset + 2] = 0;
        pdata[0][offset + 3] = 0;
        pdata[0][offset + 4] = 0;
        pdata[0][offset + 5] = 0;
        pdata[0][offset + 6] = 0;
        pdata[0][offset + 7] = 0x03;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte; // control byte
        break;
#endif // SAT_SPEC_SUPPORTED
    case SCSI_FORMAT_UNIT_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] =
            0x37; // protection information is not supported, complete list is not supported, but we are supposed to be
                  // able to take 2 different defect list formats even though we don't use them - TJE
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = controlByte; // control byte
        break;
    case WRITE_SAME_10_CMD: // TODO: add in once the command is supported
    case WRITE_SAME_16_CMD: // TODO: add in once the command is supported
    default:
        commandSupported = false;
        break;
    }
    if (!commandSupported)
    {
        // allocate memory
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset] = operationCode;
        ret              = NOT_SUPPORTED;
    }
    // set up the common data
    pdata[0][0] = RESERVED;
    if (commandSupported)
    {
        pdata[0][1] |= BIT0 | BIT1; // command supported by standard
    }
    else
    {
        pdata[0][1] |= BIT0; // command not supported
    }
    pdata[0][2] = M_Byte1(cdbLength);
    pdata[0][3] = M_Byte0(cdbLength);
    // increment the offset by the cdb length
    offset += cdbLength;
    if (rctd && ret == SUCCESS)
    {
        // set CTDP to 1
        pdata[0][1] |= BIT7;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, *pdata, &offset);
    }
    return ret;
}

static eReturnValues check_Operation_Code_and_Service_Action(tDevice*  device,
                                                             uint8_t   operationCode,
                                                             uint16_t  serviceAction,
                                                             bool      rctd,
                                                             uint8_t** pdata,
                                                             uint32_t* dataLength)
{
    eReturnValues ret              = SUCCESS;
    uint32_t      offset           = UINT32_C(4); // use to keep track and setup the buffer
    uint16_t      cdbLength        = UINT16_C(1); // set to 1 for the default case
    uint8_t       controlByte      = UINT8_C(0);
    bool          commandSupported = true;
    *dataLength = 4; // add onto this for each of the different commands below, then allocate memory accordingly

    if (rctd)
    {
        // add 12 bytes for room for the command timeouts descriptor to be setup
        dataLength += 12;
    }
    switch (operationCode)
    {
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 // SAT4 and higher
    case 0x7F:
        switch (serviceAction)
        {
        case 0x1FF0: // ATA Pass-through 32
            cdbLength = 32;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0]  = operationCode;
            pdata[0][offset + 1]  = controlByte; // control byte
            pdata[0][offset + 2]  = RESERVED;
            pdata[0][offset + 3]  = RESERVED;
            pdata[0][offset + 4]  = RESERVED;
            pdata[0][offset + 5]  = RESERVED;
            pdata[0][offset + 6]  = RESERVED;
            pdata[0][offset + 7]  = 0x18; // additional CDB length
            pdata[0][offset + 8]  = 0x1F; // service action
            pdata[0][offset + 9]  = 0xF0; // service action
            pdata[0][offset + 10] = 0x1F; // protocol and extend
            pdata[0][offset + 11] = 0xFF; // length bits
            pdata[0][offset + 12] = 0xFF;
            pdata[0][offset + 13] = 0xFF;
            pdata[0][offset + 14] = 0xFF;
            pdata[0][offset + 15] = 0xFF;
            pdata[0][offset + 16] = 0xFF;
            pdata[0][offset + 17] = 0xFF;
            pdata[0][offset + 18] = 0xFF;
            pdata[0][offset + 19] = 0xFF;
            pdata[0][offset + 20] = 0xFF;
            pdata[0][offset + 21] = 0xFF;
            pdata[0][offset + 22] = 0xFF;
            pdata[0][offset + 23] = 0xFF;
            pdata[0][offset + 24] = 0xFF;
            pdata[0][offset + 25] = 0xFF;
            pdata[0][offset + 26] = RESERVED;
            pdata[0][offset + 27] = 0x00; // ICC - we can't send this in this software version. OS doesn't allow it
            pdata[0][offset + 28] = 0x00; // AUX - we can't send this in this software version. OS doesn't allow it
            pdata[0][offset + 29] = 0x00; // AUX - we can't send this in this software version. OS doesn't allow it
            pdata[0][offset + 30] = 0x00; // AUX - we can't send this in this software version. OS doesn't allow it
            pdata[0][offset + 31] = 0x00; // AUX - we can't send this in this software version. OS doesn't allow it
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
#endif // SAT_SPEC_SUPPORTED
    case 0x9E:
        switch (serviceAction)
        {
        case 0x10: // read capacity 16
            cdbLength = 16;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0]  = operationCode;
            pdata[0][offset + 1]  = serviceAction & 0x001F;
            pdata[0][offset + 2]  = OBSOLETE;
            pdata[0][offset + 3]  = OBSOLETE;
            pdata[0][offset + 4]  = OBSOLETE;
            pdata[0][offset + 5]  = OBSOLETE;
            pdata[0][offset + 6]  = OBSOLETE;
            pdata[0][offset + 7]  = OBSOLETE;
            pdata[0][offset + 8]  = OBSOLETE;
            pdata[0][offset + 9]  = OBSOLETE;
            pdata[0][offset + 10] = 0xFF;
            pdata[0][offset + 11] = 0xFF;
            pdata[0][offset + 12] = 0xFF;
            pdata[0][offset + 13] = 0xFF;
            pdata[0][offset + 14] = 0;           // bit 0 is obsolete
            pdata[0][offset + 15] = controlByte; // control byte
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
    case 0xA3:
        switch (serviceAction)
        {
        case 0x0C: // report supported op codes
            cdbLength = 12;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0]  = operationCode;
            pdata[0][offset + 1]  = serviceAction & 0x001F;
            pdata[0][offset + 2]  = 0x87; // bits 0-2 & bit7
            pdata[0][offset + 3]  = 0xFF;
            pdata[0][offset + 4]  = 0xFF;
            pdata[0][offset + 5]  = 0xFF;
            pdata[0][offset + 6]  = 0xFF;
            pdata[0][offset + 7]  = 0xFF;
            pdata[0][offset + 8]  = 0xFF;
            pdata[0][offset + 9]  = 0xFF;
            pdata[0][offset + 10] = RESERVED;
            pdata[0][offset + 11] = controlByte; // control byte
            break;
        case 0x0F: // report timestamp
            if (device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported)
            {
                cdbLength = 12;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0]  = operationCode;
                pdata[0][offset + 1]  = serviceAction & 0x001F;
                pdata[0][offset + 2]  = RESERVED;
                pdata[0][offset + 3]  = RESERVED;
                pdata[0][offset + 4]  = RESERVED;
                pdata[0][offset + 5]  = RESERVED;
                pdata[0][offset + 6]  = 0xFF;
                pdata[0][offset + 7]  = 0xFF;
                pdata[0][offset + 8]  = 0xFF;
                pdata[0][offset + 9]  = 0xFF;
                pdata[0][offset + 10] = RESERVED;
                pdata[0][offset + 11] = controlByte; // control byte
                break;
            }
            else
            {
                commandSupported = false;
            }
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
    case SANITIZE_CMD:
        if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word059)) &&
            le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT12)
        {
            switch (serviceAction)
            {
            case 1: // overwrite
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = C_CAST(uint8_t, (serviceAction & UINT16_C(0x001F)) | BIT5);
                pdata[0][offset + 2] = RESERVED;
                pdata[0][offset + 3] = RESERVED;
                pdata[0][offset + 4] = RESERVED;
                pdata[0][offset + 5] = RESERVED;
                pdata[0][offset + 6] = RESERVED;
                pdata[0][offset + 7] = 0xFF;
                pdata[0][offset + 8] = 0xFF;
                pdata[0][offset + 9] = controlByte; // control byte
                break;
            case 2:    // block erase
            case 3:    // cryptographic erase
            case 0x1F: // exit failure mode
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = C_CAST(uint8_t, (serviceAction & UINT16_C(0x001F)) | BIT5);
                pdata[0][offset + 2] = RESERVED;
                pdata[0][offset + 3] = RESERVED;
                pdata[0][offset + 4] = RESERVED;
                pdata[0][offset + 5] = RESERVED;
                pdata[0][offset + 6] = RESERVED;
                pdata[0][offset + 7] = 0;
                pdata[0][offset + 8] = 0;
                pdata[0][offset + 9] = controlByte; // control byte
                break;
            default:
                commandSupported = false;
                break;
            }
        }
        else
        {
            commandSupported = false;
        }
        break;
    case 0xA4:
        switch (serviceAction)
        {
        case 0x0F: // set timestamp
            if (device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported)
            {
                cdbLength = 12;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0]  = operationCode;
                pdata[0][offset + 1]  = serviceAction & 0x001F;
                pdata[0][offset + 2]  = RESERVED;
                pdata[0][offset + 3]  = RESERVED;
                pdata[0][offset + 4]  = RESERVED;
                pdata[0][offset + 5]  = RESERVED;
                pdata[0][offset + 6]  = 0xFF;
                pdata[0][offset + 7]  = 0xFF;
                pdata[0][offset + 8]  = 0xFF;
                pdata[0][offset + 9]  = 0xFF;
                pdata[0][offset + 10] = RESERVED;
                pdata[0][offset + 11] = controlByte; // control byte
                break;
            }
            else
            {
                commandSupported = false;
            }
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
    case WRITE_BUFFER_CMD:
    {
        bool downloadCommandSupported = false;
        bool downloadMode3Supported   = false;
        if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT0) ||
            (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT0) ||
            device->drive_info.ata_Options.downloadMicrocodeDMASupported)
        {
            downloadCommandSupported = true;
        }
        if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
            && ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                     le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
                 le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT4) ||
                (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(
                     le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
                 le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT4)))
        {
            downloadMode3Supported = true;
        }
        switch (serviceAction)
        {
        case 0x02: // write data
            cdbLength = 10;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = serviceAction & 0x001F;
            pdata[0][offset + 2] = 0;
            pdata[0][offset + 3] = 0;
            pdata[0][offset + 4] = 0;
            pdata[0][offset + 5] = 0;
            pdata[0][offset + 6] = 0;
            pdata[0][offset + 7] = 0x02;
            pdata[0][offset + 8] = 0x00;
            pdata[0][offset + 9] = controlByte; // control byte
            break;
        case 0x05: // download
            if (downloadCommandSupported)
            {
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0;
                pdata[0][offset + 4] = 0;
                pdata[0][offset + 5] = 0;
                pdata[0][offset + 6] = 0xFF;
                pdata[0][offset + 7] = 0xFF;
                pdata[0][offset + 8] = 0xFF;
                pdata[0][offset + 9] = controlByte; // control byte
            }
            else
            {
                commandSupported = false;
            }
            break;
        case 0x07: // download offsets
            if (downloadCommandSupported && downloadMode3Supported)
            {
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0;
                pdata[0][offset + 4] = 0;
                pdata[0][offset + 5] = 0;
                pdata[0][offset + 6] = 0x3F;
                pdata[0][offset + 7] = 0xFE;
                pdata[0][offset + 8] = 0x00;
                pdata[0][offset + 9] = controlByte; // control byte
            }
            else
            {
                commandSupported = false;
            }
            break;
        case 0x0D: // download offsets defer
            if (downloadCommandSupported && device->drive_info.softSATFlags.deferredDownloadSupported)
            {
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] =
                    C_CAST(uint8_t, (serviceAction & UINT16_C(0x001F)) |
                                        BIT7); // service action plus mode specific set for power cycle activation
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0x3F;
                pdata[0][offset + 4] = 0xFE;
                pdata[0][offset + 5] = 0x00;
                pdata[0][offset + 6] = 0x3F;
                pdata[0][offset + 7] = 0xFE;
                pdata[0][offset + 8] = 0x00;
                pdata[0][offset + 9] = controlByte; // control byte
            }
            else
            {
                commandSupported = false;
            }
            break;
        case 0x0E: // download offsets defer
        case 0x0F: // activate deferred code
            if (downloadCommandSupported && device->drive_info.softSATFlags.deferredDownloadSupported)
            {
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0x3F;
                pdata[0][offset + 4] = 0xFE;
                pdata[0][offset + 5] = 0x00;
                pdata[0][offset + 6] = 0x3F;
                pdata[0][offset + 7] = 0xFE;
                pdata[0][offset + 8] = 0x00;
                pdata[0][offset + 9] = controlByte; // control byte
            }
            else
            {
                commandSupported = false;
            }
            break;
        default:
            commandSupported = false;
            break;
        }
    }
    break;
    case READ_BUFFER_CMD:
        switch (serviceAction)
        {
        case 0x02: // read buffer command
                   // fall through
        case 0x03: // descriptor
            cdbLength = 10;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = serviceAction & 0x001F;
            pdata[0][offset + 2] = 0;
            pdata[0][offset + 3] = 0;
            pdata[0][offset + 4] = 0;
            pdata[0][offset + 5] = 0;
            pdata[0][offset + 6] = 0;
            pdata[0][offset + 7] = 0x03;
            pdata[0][offset + 8] = 0xFF;
            pdata[0][offset + 9] = controlByte; // control byte
            break;
#    if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 && SAT_4_ERROR_HISTORY_FEATURE
        case 0x1C:
            if (device->drive_info.softSATFlags.currentInternalStatusLogSupported ||
                device->drive_info.softSATFlags.savedInternalStatusLogSupported)
            {
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0;
                pdata[0][offset + 4] = 0;
                pdata[0][offset + 5] = 0;
                pdata[0][offset + 6] = 0;
                pdata[0][offset + 7] = 0x03;
                pdata[0][offset + 8] = 0xFF;
                pdata[0][offset + 9] = controlByte; // control byte
            }
            else
            {
                commandSupported = false;
            }
            break;
#    endif // SAT_SPEC_SUPPORTED
        default:
            commandSupported = false;
            break;
        }
        break;
#endif // SAT_SPEC_SUPPORTED
    case 0x9F:
        switch (serviceAction)
        {
        case 0x11: // write long 16
            cdbLength = 16;
            *dataLength += cdbLength;
            *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0]  = operationCode;
            pdata[0][offset + 1]  = C_CAST(uint8_t, (serviceAction & 0x001F) | BIT7 | BIT6 | BIT5);
            pdata[0][offset + 2]  = 0xFF;
            pdata[0][offset + 3]  = 0xFF;
            pdata[0][offset + 4]  = 0xFF;
            pdata[0][offset + 5]  = 0xFF;
            pdata[0][offset + 6]  = 0xFF;
            pdata[0][offset + 7]  = 0xFF;
            pdata[0][offset + 8]  = 0xFF;
            pdata[0][offset + 9]  = 0xFF;
            pdata[0][offset + 10] = RESERVED;
            pdata[0][offset + 11] = RESERVED;
            pdata[0][offset + 12] = 0;
            pdata[0][offset + 13] = 0;
            pdata[0][offset + 14] = RESERVED;
            pdata[0][offset + 15] = controlByte; // control byte
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
    case ZONE_MANAGEMENT_IN:
        if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
            device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
        {
            switch (serviceAction)
            {
            case ZM_ACTION_REPORT_ZONES:
                cdbLength = 16;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0]  = operationCode;
                pdata[0][offset + 1]  = serviceAction & 0x001F;
                pdata[0][offset + 2]  = 0xFF;
                pdata[0][offset + 3]  = 0xFF;
                pdata[0][offset + 4]  = 0xFF;
                pdata[0][offset + 5]  = 0xFF;
                pdata[0][offset + 6]  = 0xFF;
                pdata[0][offset + 7]  = 0xFF;
                pdata[0][offset + 8]  = 0xFF;
                pdata[0][offset + 9]  = 0xFF;
                pdata[0][offset + 10] = 0xFF;
                pdata[0][offset + 11] = 0xFF;
                pdata[0][offset + 12] = 0xFF;
                pdata[0][offset + 13] = 0xFF;
                pdata[0][offset + 14] = 0xBF;
                pdata[0][offset + 15] = controlByte; // control byte
                break;
            default:
                commandSupported = false;
                break;
            }
        }
        else
        {
            commandSupported = false;
        }
        break;
    case ZONE_MANAGEMENT_OUT:
        if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
            device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
        {
            switch (serviceAction)
            {
            case ZM_ACTION_CLOSE_ZONE:
            case ZM_ACTION_FINISH_ZONE:
            case ZM_ACTION_OPEN_ZONE:
            case ZM_ACTION_RESET_WRITE_POINTERS:
                cdbLength = 16;
                *dataLength += cdbLength;
                *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0]  = operationCode;
                pdata[0][offset + 1]  = serviceAction & 0x001F;
                pdata[0][offset + 2]  = 0xFF;
                pdata[0][offset + 3]  = 0xFF;
                pdata[0][offset + 4]  = 0xFF;
                pdata[0][offset + 5]  = 0xFF;
                pdata[0][offset + 6]  = 0xFF;
                pdata[0][offset + 7]  = 0xFF;
                pdata[0][offset + 8]  = 0xFF;
                pdata[0][offset + 9]  = 0xFF;
                pdata[0][offset + 10] = RESERVED;
                pdata[0][offset + 11] = RESERVED;
                pdata[0][offset + 12] = RESERVED;
                pdata[0][offset + 13] = RESERVED;
                pdata[0][offset + 14] = BIT0;
                pdata[0][offset + 15] = controlByte; // control byte
                break;
            default:
                commandSupported = false;
                break;
            }
        }
        else
        {
            commandSupported = false;
        }
        break;
    default:
        commandSupported = false;
        break;
    }
    if (!commandSupported)
    {
        // allocate memory
        *dataLength += cdbLength;
        *pdata = M_REINTERPRET_CAST(uint8_t*, safe_calloc(*dataLength, sizeof(uint8_t)));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset] = operationCode;
        ret              = NOT_SUPPORTED;
    }
    // set up the common data
    pdata[0][0] = RESERVED;
    if (commandSupported)
    {
        pdata[0][1] |= BIT0 | BIT1; // command supported by standard
    }
    else
    {
        pdata[0][1] |= BIT0; // command not supported
    }
    pdata[0][2] = M_Byte1(cdbLength);
    pdata[0][3] = M_Byte0(cdbLength);
    // increment the offset by the cdb length
    offset += cdbLength;
    if (rctd && ret == SUCCESS)
    {
        // set CTDP to 1
        pdata[0][1] |= BIT7;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, *pdata, &offset);
    }
    return ret;
}

static eReturnValues create_All_Supported_Op_Codes_Buffer(tDevice*  device,
                                                          bool      rctd,
                                                          uint8_t** pdata,
                                                          uint32_t* dataLength)
{
    eReturnValues ret                = SUCCESS;
    uint32_t      reportAllMaxLength = UINT32_C(4) * LEGACY_DRIVE_SEC_SIZE;
    uint32_t      offset             = UINT32_C(4);
    *pdata                           = M_REINTERPRET_CAST(uint8_t*, safe_calloc(reportAllMaxLength, sizeof(uint8_t)));
    if (!*pdata)
    {
        return MEMORY_FAILURE;
    }
    // go through supported op codes &| service actions in order
    // TEST_UNIT_READY_CMD = 0x00
    pdata[0][offset + 0] = TEST_UNIT_READY_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // REQUEST_SENSE_CMD = 0x03
    pdata[0][offset + 0] = REQUEST_SENSE_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // SCSI_FORMAT_UNIT_CMD = 0x04
    pdata[0][offset + 0] = SCSI_FORMAT_UNIT_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // REASSIGN_BLOCKS_6 = 0x07
    pdata[0][offset + 0] = REASSIGN_BLOCKS_6;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // READ6 = 0x08
    pdata[0][offset + 0] = READ6;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE6 = 0x0A
    pdata[0][offset + 0] = WRITE6;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // INQUIRY_CMD = 0x12
    pdata[0][offset + 0] = INQUIRY_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // MODE_SELECT_6_CMD = 0x15
    pdata[0][offset + 0] = MODE_SELECT_6_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // MODE_SENSE_6_CMD = 0x1A
    pdata[0][offset + 0] = MODE_SENSE_6_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // START_STOP_UNIT_CMD = 0x1B
    pdata[0][offset + 0] = START_STOP_UNIT_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // SEND_DIAGNOSTIC_CMD = 0x1D
    pdata[0][offset + 0] = SEND_DIAGNOSTIC_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // READ_CAPACITY_10 = 0x25
    pdata[0][offset + 0] = READ_CAPACITY_10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // READ10 = 0x28
    pdata[0][offset + 0] = READ10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE10 = 0x2A
    pdata[0][offset + 0] = WRITE10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE_AND_VERIFY_10 = 0x2E
    pdata[0][offset + 0] = WRITE_AND_VERIFY_10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // VERIFY10 = 0x2F
    pdata[0][offset + 0] = VERIFY10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // SYNCHRONIZE_CACHE_10 = 0x35
    pdata[0][offset + 0] = SYNCHRONIZE_CACHE_10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
    // WRITE_BUFFER_CMD = 0x3B + modes
    pdata[0][offset + 0] = WRITE_BUFFER_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x02); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x02); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    bool downloadCommandSupported = false;
    bool downloadMode3Supported   = false;
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word083)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word083) & BIT0) ||
        (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT0) ||
        device->drive_info.ata_Options.downloadMicrocodeDMASupported)
    {
        downloadCommandSupported = true;
    }
    if ((is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word086)) &&
         le16_to_host(device->drive_info.IdentifyData.ata.Word086) & BIT15) /* words 119, 120 valid */
        &&
        ((is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word119)) &&
          le16_to_host(device->drive_info.IdentifyData.ata.Word119) & BIT4) ||
         (is_ATA_Identify_Word_Valid_With_Bits_14_And_15(le16_to_host(device->drive_info.IdentifyData.ata.Word120)) &&
          le16_to_host(device->drive_info.IdentifyData.ata.Word120) & BIT4)))
    {
        downloadMode3Supported = true;
    }
    if (downloadCommandSupported)
    {
        pdata[0][offset + 0] = WRITE_BUFFER_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x05); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x05); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        if (downloadMode3Supported)
        {
            pdata[0][offset + 0] = WRITE_BUFFER_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x07); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x07); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
        }
        if (device->drive_info.softSATFlags.deferredDownloadSupported)
        {
            pdata[0][offset + 0] = WRITE_BUFFER_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x0D); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x0D); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
            pdata[0][offset + 0] = WRITE_BUFFER_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x0E); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x0E); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
            pdata[0][offset + 0] = WRITE_BUFFER_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x0F); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x0F); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
        }
    }
    // READ_BUFFER_CMD = 0x3C + modes
    pdata[0][offset + 0] = READ_BUFFER_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x02); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x02); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    pdata[0][offset + 0] = READ_BUFFER_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x03); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x03); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }

#    if defined(SAT_4_ERROR_HISTORY_FEATURE) && SAT_4_ERROR_HISTORY_FEATURE > 0
    if (device->drive_info.softSATFlags.currentInternalStatusLogSupported ||
        device->drive_info.softSATFlags.savedInternalStatusLogSupported)
    {
        pdata[0][offset + 0] = READ_BUFFER_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x1C); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x1C); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
#    endif // SAT_4_ERROR_HISTORY_FEATURE
#else      // SAT_SPEC_SUPPORTED
    // WRITE_BUFFER_CMD
    pdata[0][offset + 0] = WRITE_BUFFER_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {

        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // READ_BUFFER_CMD
    pdata[0][offset + 0] = READ_BUFFER_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
#endif     // SAT_SPEC_SUPPORTED
    // WRITE_LONG_10_CMD = 0x3F
    pdata[0][offset + 0] = WRITE_LONG_10_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE_SAME_10_CMD = 0x41
    pdata[0][offset + 0] = WRITE_SAME_10_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // UNMAP_CMD = 0x42
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word169)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0)
    {
        pdata[0][offset + 0] = UNMAP_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0); // service action msb
        pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        // skipping offset 5 for this
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    // SANITIZE_CMD = 0x48//4 possible service actions
    if (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word059)) &&
        le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT12)
    {
        // check overwrite
        if (le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT14)
        {
            pdata[0][offset + 0] = SANITIZE_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x01); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x01); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
        }
        // check block erase
        if (le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT15)
        {
            pdata[0][offset + 0] = SANITIZE_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x02); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x02); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
        }
        // check crypto erase
        if (le16_to_host(device->drive_info.IdentifyData.ata.Word059) & BIT13)
        {
            pdata[0][offset + 0] = SANITIZE_CMD;
            pdata[0][offset + 1] = RESERVED;
            pdata[0][offset + 2] = M_Byte1(0x03); // service action msb
            pdata[0][offset + 3] = M_Byte0(0x03); // service action lsb if non zero set byte 5, bit0
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = BIT0;
            pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
            pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
            offset += 8;
            if (rctd)
            {
                // set CTPD to 1
                pdata[0][offset - 8 + 5] |= BIT1;
                // set up timeouts descriptor
                set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
            }
        }
        // set exit failure mode since it's always available
        pdata[0][offset + 0] = SANITIZE_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x1F); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x1F); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    // LOG_SELECT_CMD = 0x4C
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
    pdata[0][offset + 0] = LOG_SELECT_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
#endif // SAT_SPEC_SUPPORTED
    // LOG_SENSE_CMD = 0x4D
    pdata[0][offset + 0] = LOG_SENSE_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // MODE_SELECT10 = 0x55
    pdata[0][offset + 0] = MODE_SELECT10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // MODE_SENSE10 = 0x5A
    pdata[0][offset + 0] = MODE_SENSE10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3 // SAT4 and higher
    // ATA_PASS_THROUGH_32 = 0x7f/1ff0
    pdata[0][offset + 0] = 0x7F;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x1FF0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x1FF0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_32);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_32);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
#endif // SAT_SPEC_SUPPORTED
    // ATA_PASS_THROUGH_16 = 0x85
    pdata[0][offset + 0] = ATA_PASS_THROUGH_16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // READ16 = 0x88
    pdata[0][offset + 0] = READ16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE16 = 0x8A
    pdata[0][offset + 0] = WRITE16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE_AND_VERIFY_16 = 0x8E
    pdata[0][offset + 0] = WRITE_AND_VERIFY_16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // VERIFY16 = 0x8F
    pdata[0][offset + 0] = VERIFY16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // SYNCHRONIZE_CACHE_16_CMD = 0x91
    pdata[0][offset + 0] = SYNCHRONIZE_CACHE_16_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE_SAME_16_CMD = 0x93
    pdata[0][offset + 0] = WRITE_SAME_16_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    // skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
        device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
    {
        // ZONE_MANAGEMENT_OUT = 0x94//4 service actions
        pdata[0][offset + 0] = 0x94;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x01); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x01); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        pdata[0][offset + 0] = 0x94;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x02); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x02); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        pdata[0][offset + 0] = 0x94;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x03); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x03); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        pdata[0][offset + 0] = 0x94;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x04); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x04); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        // ZONE_MANAGEMENT_IN = 0x95//1 service action
        pdata[0][offset + 0] = 0x95;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x00); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x00); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    // 0x9E / 0x10//read capacity 16                 = 0x9E
    pdata[0][offset + 0] = 0x9E;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x10); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x10); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // 0x9F / 0x11//write long 16                    = 0x9F
    pdata[0][offset + 0] = 0x9F;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x11); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x11); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // REPORT_LUNS_CMD = 0xA0
    pdata[0][offset + 0] = REPORT_LUNS_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // ATA_PASS_THROUGH_12 = 0xA1
    pdata[0][offset + 0] = ATA_PASS_THROUGH_12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {

        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // SECURITY_PROTOCOL_IN = 0xA2
    pdata[0][offset + 0] = SECURITY_PROTOCOL_IN;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // 0xA3 / 0x0C//report supported op codes        = 0xA3
    pdata[0][offset + 0] = 0xA3;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x0C); // service action msb
    pdata[0][offset + 3] = M_Byte0(0x0C); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    if (device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported)
    {
        // 0xA3 / 0x0F//report timestamp                 = 0xA3
        pdata[0][offset + 0] = 0xA3;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x0F); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x0F); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        // 0xA4 / 0x0F//set timestamp                    = 0xA4
        pdata[0][offset + 0] = 0xA4;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x0F); // service action msb
        pdata[0][offset + 3] = M_Byte0(0x0F); // service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            // set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            // set up timeouts descriptor
            set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    // READ12 = 0xA8
    pdata[0][offset + 0] = READ12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE12 = 0xAA
    pdata[0][offset + 0] = WRITE12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // WRITE_AND_VERIFY_12 = 0xAE
    pdata[0][offset + 0] = WRITE_AND_VERIFY_12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // VERIFY12 = 0xAF
    pdata[0][offset + 0] = VERIFY12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // SECURITY_PROTOCOL_OUT = 0xB5
    pdata[0][offset + 0] = SECURITY_PROTOCOL_OUT;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0); // service action msb
    pdata[0][offset + 3] = M_Byte0(0); // service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        // set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        // set up timeouts descriptor
        set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    // now that we are here, we need to set the data length
    pdata[0][0] = M_Byte3(offset - 4);
    pdata[0][1] = M_Byte2(offset - 4);
    pdata[0][2] = M_Byte1(offset - 4);
    pdata[0][3] = M_Byte0(offset - 4);
    *dataLength = offset;
    return ret;
}

static eReturnValues translate_SCSI_Report_Supported_Operation_Codes_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret                    = SUCCESS;
    bool          rctd                   = false;
    uint8_t       reportingOptions       = scsiIoCtx->cdb[2] & 0x07;
    uint8_t       requestedOperationCode = scsiIoCtx->cdb[3];
    uint16_t      requestedServiceAction = M_BytesTo2ByteValue(scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    uint32_t      allocationLength =
        M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    uint8_t* supportedOpData       = M_NULLPTR;
    uint32_t supportedOpDataLength = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
    uint8_t  bitPointer   = UINT8_C(0);
    uint16_t fieldPointer = UINT16_C(0);
    // filter out invalid fields
    if (((fieldPointer = 1) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[1], 7, 5) != 0) ||
        ((fieldPointer = 2) != 0 && get_bit_range_uint8(scsiIoCtx->cdb[2], 6, 3) != 0) ||
        ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0))
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter         = UINT8_C(0);
            if (fieldPointer == 2)
            {
                reservedByteVal &= 0x7F; // strip off rctd bit
            }
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer =
                counter - 1; // because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0,
                                       device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[2] & BIT7)
    {
        rctd = true;
    }
    switch (reportingOptions)
    {
    case 0: // return all op codes (return not supported for now until we get the other methods working...)
        ret = create_All_Supported_Op_Codes_Buffer(device, rctd, &supportedOpData, &supportedOpDataLength);
        break;
    case 1: // check operation code, service action ignored
        // check op code func
        ret = check_Operation_Code(device, requestedOperationCode, rctd, &supportedOpData, &supportedOpDataLength);
        break;
    case 2: // check operation code and service action (error on commands that don't have service actions)
        // check opcode and service action func
        ret = check_Operation_Code_and_Service_Action(device, requestedOperationCode, requestedServiceAction, rctd,
                                                      &supportedOpData, &supportedOpDataLength);
        break;
    case 3: // case 1 or case 2 (SPC4+)
        if (SUCCESS ==
            check_Operation_Code(device, requestedOperationCode, rctd, &supportedOpData, &supportedOpDataLength))
        {
            ret = SUCCESS;
        }
        else
        {
            // free this memory since the last function allocated it, but failed, then check if the op/sa combination is
            // supported
            safe_free(&supportedOpData);
            supportedOpDataLength = 0;
            if (check_Operation_Code_and_Service_Action(device, requestedOperationCode, requestedServiceAction, rctd,
                                                        &supportedOpData, &supportedOpDataLength))
            {
                ret = SUCCESS;
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
        }
        break;
    default:
        bitPointer   = UINT8_C(2);
        fieldPointer = UINT16_C(2);
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
        break;
    }
    if (supportedOpData && scsiIoCtx->pdata)
    {
        safe_memcpy(scsiIoCtx->pdata, scsiIoCtx->dataLength, supportedOpData,
                    M_Min(supportedOpDataLength, allocationLength));
    }
    safe_free(&supportedOpData);
    return ret;
}

// always sets Descriptor type sense data
eReturnValues translate_SCSI_Command(tDevice* device, ScsiIoCtx* scsiIoCtx)
{
    static bool   deviceInfoAvailable  = false;
    eReturnValues ret                  = UNKNOWN;
    bool          invalidFieldInCDB    = false;
    bool          invalidOperationCode = false;
    uint16_t      fieldPointer         = UINT16_C(0);
    uint8_t       bitPointer           = UINT8_C(0);
    uint8_t       controlByteOffset;
    /*
    static bool satConfigInitialized = false;//this is static because we only want to initialize the struct once!
    if (!satConfigInitialized)
    {
    //TODO: initialize it with empty data
    safe_memset(&satConfig, sizeof(satPassthroughConfiguration), 0, sizeof(satPassthroughConfiguration));
    satConfigInitialized = true;
    }
    */
    // if we weren't given a sense data pointer, use the sense data in the device structure
    if (!scsiIoCtx->psense)
    {
        scsiIoCtx->psense        = device->drive_info.lastCommandSenseData;
        scsiIoCtx->senseDataSize = SPC3_SENSE_LEN;
    }
    safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
    controlByteOffset = scsiIoCtx->cdbLength - 1;
    if (scsiIoCtx->cdb[OPERATION_CODE] == 0x7E || scsiIoCtx->cdb[OPERATION_CODE] == 0x7F)
    {
        // variable length and 32byte CDBs have the control byte at offset 1
        controlByteOffset = 1;
    }
    // check for bits in the control byte that are set that aren't supported
    if (((bitPointer = 7) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT7)    // vendor specific
        || ((bitPointer = 6) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT6) // vendor specific
        || ((bitPointer = 5) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT5) // reserved
        || ((bitPointer = 4) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT4) // reserved
        || ((bitPointer = 3) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT3) // reserved
        || ((bitPointer = 2) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT2) // naca
        || ((bitPointer = 1) != 0 && scsiIoCtx->cdb[controlByteOffset] & BIT1) // flag (obsolete in SAM2)
        || ((bitPointer = 0) == 0 && scsiIoCtx->cdb[controlByteOffset] & BIT0) // link (obsolete in SAM4)
    )
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
        fieldPointer = controlByteOffset;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                        fieldPointer);
        // set up a sense key specific information descriptor to say that this bit is not valid
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                       0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                       senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                   device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
    // if the ataIdentify data is zero, send an identify at least once so we aren't sending that every time we do a read
    // or write command...inquiry, read capacity will always do one though to get the most recent data
    if (!deviceInfoAvailable)
    {
        DECLARE_ZERO_INIT_ARRAY(uint8_t, zeroData, LEGACY_DRIVE_SEC_SIZE);
        if (memcmp(&device->drive_info.IdentifyData.ata.Word000, zeroData, LEGACY_DRIVE_SEC_SIZE) == 0)
        {
            // call fill ata drive info to set up vars inside the device struct which the other commands will use.
            if (SUCCESS != fill_In_ATA_Drive_Info(device))
            {
                return FAILURE;
            }
            deviceInfoAvailable = true;
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0,
                                           device->drive_info.softSATFlags.senseDataDescriptorFormat, M_NULLPTR, 0);
            ////read identify data
            // if (SUCCESS != ata_Identify(device, C_CAST(uint8_t*, &device->drive_info.IdentifyData.ata.Word000),
            // LEGACY_DRIVE_SEC_SIZE))
            //{
            //     //that failed, so try an identify packet device
            //     if (SUCCESS == ata_Identify_Packet_Device(device, C_CAST(uint8_t*,
            //     &device->drive_info.IdentifyData.ata.Word000), LEGACY_DRIVE_SEC_SIZE))
            //     {
            //         //set that we are an ATAPI_DEVICE, then this function will just encapsulate every scsi command
            //         into an ATA_PACKET command device->drive_info.drive_type = ATAPI_DRIVE;
            //     }
            //     else //something is horribly wrong...return a failure
            //     {
            //         return FAILURE;
            //     }
            // }
        }
        else
        {
            deviceInfoAvailable = true;
        }
    }
    if (device->drive_info.drive_type == ATAPI_DRIVE)
    {
        // TODO: set up an ata packet command and send it to the device to let it handle the scsi command translation
        // NOTE: There are a few things that actually do need translation to an ATAPI:
        //       1) ATA Information VPD page
        //       2) A1h CDB. This could be a Blank command, or a SAT ATA Passthrough command. Need to do some checking
        //       of the fields to figure this out and handle it properly!!!
        return NOT_SUPPORTED;
    }
    else
    {
        // start checking the scsi command and call the function to translate it
        // All functions within this switch-case should dummy up their own sense data specific to the translation!
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case INQUIRY_CMD: // mostly identify information, but some log info for some pages.
            ret = translate_SCSI_Inquiry_Command(device, scsiIoCtx);
            break;
        case READ_CAPACITY_10: // identify
            ret = translate_SCSI_Read_Capacity_Command(device, false, scsiIoCtx);
            break;
        case 0xAB:
            // check the service action
            switch (scsiIoCtx->cdb[1] & 0x1F)
            {
            case 0x01: // Read Media Serial Number
                ret = translate_SCSI_Read_Media_Serial_Number_Command(device, scsiIoCtx);
                break;
            default:
                fieldPointer      = UINT16_C(1);
                bitPointer        = UINT8_C(4);
                invalidFieldInCDB = true;
                break;
            }
            break;
        case 0x9E:
            // check the service action
            switch (scsiIoCtx->cdb[1] & 0x1F)
            {
            case 0x10: // Read Capacity 16
                ret = translate_SCSI_Read_Capacity_Command(device, true, scsiIoCtx);
                break;
            default:
                fieldPointer      = UINT16_C(1);
                bitPointer        = UINT8_C(4);
                invalidFieldInCDB = true;
                break;
            }
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
        case 0x7F:
            // 32byte CDB...check service action. We're looking for ATA Pass-through 32
            {
                uint16_t serviceAction = M_BytesTo2ByteValue(scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
                switch (serviceAction)
                {
                case 0x1FF0: // ATA pass-through 32
                    // we likely won't ever hit this case unless we are purposely doing software SAT, but we are going
                    // to reverse the CDB and create the ataCommandOptions structure and then issue the command (or just
                    // issue it depending on the interface)
                    ret = translate_SCSI_ATA_Passthrough_Command(device, scsiIoCtx);
                    break;
                default:
                    fieldPointer      = UINT16_C(8);
                    bitPointer        = UINT8_C(7);
                    invalidFieldInCDB = true;
                    break;
                }
            }
            break;
#endif                            // SAT_SPEC_SUPPORTED
        case ATA_PASS_THROUGH_12: // any command (other than asynchronous or resets) is allowed
        case ATA_PASS_THROUGH_16:
            // we likely won't ever hit this case unless we are purposely doing software SAT, but we are going to
            // reverse the CDB and create the ataCommandOptions structure and then issue the command (or just issue it
            // depending on the interface)
            ret = translate_SCSI_ATA_Passthrough_Command(device, scsiIoCtx);
            break;
        case SCSI_FORMAT_UNIT_CMD:
            ret = translate_SCSI_Format_Unit_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1 // SAT2+ supports this command.
        case LOG_SELECT_CMD: // only needs to support the application client page as of SAT4
            translate_SCSI_Log_Select_Command(device, scsiIoCtx);
            break;
#endif // SAT_SPEC_SUPPORTED
        case LOG_SENSE_CMD:
            ret = translate_SCSI_Log_Sense_Command(device, scsiIoCtx);
            break;
        case MODE_SELECT_6_CMD: // usually set features commands
        case MODE_SELECT10:
            translate_SCSI_Mode_Select_Command(device, scsiIoCtx);
            break;
        case MODE_SENSE_6_CMD:
        case MODE_SENSE10:
            ret = translate_SCSI_Mode_Sense_Command(device, scsiIoCtx);
            break;
        case READ6:
        case READ10:
        case READ12:
        case READ16: // ata read commands
            ret = translate_SCSI_Read_Command(device, scsiIoCtx);
            break;
        case READ_BUFFER_CMD: // read buffer and read log
            ret = translate_SCSI_Read_Buffer_Command(device, scsiIoCtx);
            break;
        case REASSIGN_BLOCKS_6: // read verify and writes
            ret = translate_SCSI_Reassign_Blocks_Command(device, scsiIoCtx);
            break;
        case REPORT_LUNS_CMD:
            ret = translate_SCSI_Report_Luns_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
        case 0xA3: // check the service action for this one!
            switch (scsiIoCtx->cdb[1] & 0x1F)
            {
            case 0x0C: // report supported op codes <- this is essentially returning either a massive table of supported
                       // commands, or it is sending back data or an error based off a switch statement
                // update this as more supported op codes are added
                ret = translate_SCSI_Report_Supported_Operation_Codes_Command(device, scsiIoCtx);
                break;
            case 0x0F: // report timestamp
                if (device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported)
                {
                    ret = translate_SCSI_Report_Timestamp_Command(device, scsiIoCtx);
                }
                else
                {
                    fieldPointer      = UINT16_C(1);
                    bitPointer        = UINT8_C(4);
                    invalidFieldInCDB = true;
                }
                break;
            default:
                fieldPointer      = UINT16_C(1);
                bitPointer        = UINT8_C(4);
                invalidFieldInCDB = true;
                break;
            }
            break;
#endif                          // SAT_SPEC_SUPPORTED
        case REQUEST_SENSE_CMD: // bunch of different commands...or read the "last command sense data" and change it
                                // from fixed to descriptor, or the other way around
            ret = translate_SCSI_Request_Sense_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
        case SANITIZE_CMD: // ATA Sanitize
            ret = translate_SCSI_Sanitize_Command(device, scsiIoCtx);
            break;
#endif // SAT_SPEC_SUPPORTED
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
        case SECURITY_PROTOCOL_IN: // Trusted non-data or trusted receive or ata security
            ret = translate_SCSI_Security_Protocol_In_Command(device, scsiIoCtx);
            break;
        case SECURITY_PROTOCOL_OUT: // Trusted non-data or trusted send or ata security
            ret = translate_SCSI_Security_Protocol_Out_Command(device, scsiIoCtx);
            break;
#endif                            // SAT_SPEC_SUPPORTED
        case SEND_DIAGNOSTIC_CMD: // SMART execute offline immediate
            ret = translate_SCSI_Send_Diagnostic_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
        case 0xA4: // Check the service action for this one!
            switch (scsiIoCtx->cdb[1] & 0x1F)
            {
            case 0x0F: // set timestamp - ATA Set Date and Timestamp
                if (device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported)
                {
                    ret = translate_SCSI_Set_Timestamp_Command(device, scsiIoCtx);
                }
                else
                {
                    invalidOperationCode = true;
                }
                break;
            default:
                if (device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported)
                {
                    fieldPointer      = UINT16_C(1);
                    bitPointer        = UINT8_C(4);
                    invalidFieldInCDB = true;
                }
                else
                {
                    invalidOperationCode = true;
                }
                break;
            }
            break;
#endif                            // SAT_SPEC_SUPPORTED
        case START_STOP_UNIT_CMD: // Varies for EPC and NON-EPC drives
            ret = translate_SCSI_Start_Stop_Unit_Command(device, scsiIoCtx);
            break;
        case SYNCHRONIZE_CACHE_10:
        case SYNCHRONIZE_CACHE_16_CMD:
            ret = translate_SCSI_Synchronize_Cache_Command(device, scsiIoCtx); // ATA Flush cache command
            break;
        case TEST_UNIT_READY_CMD: // Check power mode
            ret = translate_SCSI_Test_Unit_Ready_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 2
        case UNMAP_CMD: // Data Set management-TRIM
            if (le16_to_host(device->drive_info.IdentifyData.ata.Word169) & BIT0)
            {
                ret = translate_SCSI_Unmap_Command(device, scsiIoCtx);
            }
            else // this op code is invalid when the drive doesn't support this command - TJE
            {
                invalidOperationCode = true;
            }
            break;
#endif // SAT_SPEC_SUPPORTED
        case VERIFY10:
        case VERIFY12:
        case VERIFY16:
            ret = translate_SCSI_Verify_Command(device, scsiIoCtx); // ATA Read-verify sectors command
            break;
        case WRITE6:
        case WRITE10:
        case WRITE12:
        case WRITE16:
            ret = translate_SCSI_Write_Command(device, scsiIoCtx); // ATA write command (PIO or DMA in our case)
            break;
        case WRITE_AND_VERIFY_10:
        case WRITE_AND_VERIFY_12:
        case WRITE_AND_VERIFY_16:
            ret = translate_SCSI_Write_And_Verify_Command(device, scsiIoCtx); // ATA Write, then read-verify commands
            break;
        case WRITE_BUFFER_CMD: // Download Microcode or ATA Write Buffer
            ret = translate_SCSI_Write_Buffer_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 1
        case WRITE_LONG_10_CMD: // Write Uncorrectable ext
            ret = translate_SCSI_Write_Long(device, scsiIoCtx);
            break;
        case 0x9F: // write uncorrectable ext-check service action for 11h
            switch (scsiIoCtx->cdb[1] & 0x1F)
            {
            case 0x11: // write uncorrectable ext
                ret = translate_SCSI_Write_Long(device, scsiIoCtx);
                break;
            default:
                fieldPointer      = UINT16_C(1);
                bitPointer        = UINT8_C(4);
                invalidFieldInCDB = true;
                break;
            }
            break;
#endif                          // SAT_SPEC_SUPPORTED
        case WRITE_SAME_10_CMD: // Sequential write commands
        case WRITE_SAME_16_CMD: // Sequential write commands
            ret = translate_SCSI_Write_Same_Command(device, scsiIoCtx);
            break;
#if defined(SAT_SPEC_SUPPORTED) && SAT_SPEC_SUPPORTED > 3
        case ZONE_MANAGEMENT_IN: // 0x95
            if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
                device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
            {
                ret = translate_SCSI_Zone_Management_In_Command(device, scsiIoCtx);
            }
            else
            {
                invalidOperationCode = true;
            }
            break;
        case ZONE_MANAGEMENT_OUT: // 0x94
            if (device->drive_info.zonedType == ZONED_TYPE_HOST_AWARE ||
                device->drive_info.zonedType == ZONED_TYPE_HOST_MANAGED)
            {
                ret = translate_SCSI_Zone_Management_Out_Command(device, scsiIoCtx);
            }
            else
            {
                invalidOperationCode = true;
            }
            break;
#endif // SAT_SPEC_SUPPORTED
        default:
            invalidOperationCode = true;
            break;
        }
        if (invalidFieldInCDB)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
        if (invalidOperationCode)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, senseKeySpecificDescriptor, 8);
            bitPointer   = UINT8_C(7);
            fieldPointer = UINT16_C(0); // operation code is not right
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer,
                                                            fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20,
                                           0, device->drive_info.softSATFlags.senseDataDescriptorFormat,
                                           senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
    }
    return ret;
}

#if defined(_MSC_VER)
// Visual studio level 4 produces lots of warnings for "assignment within conditional expression" which is normally a
// good warning, but it is used HEAVILY in this file by the software SAT translator to return the field pointer on
// errors. So for VS only, this warning will be disabled in this file.
#    pragma warning(pop)
#endif
