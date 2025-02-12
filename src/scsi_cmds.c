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
// \file scsi_cmds.c   Implementation for SCSI command functions
//                     The intention of the file is to be generic & not OS specific

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

#include "common_public.h"
#include "platform_helper.h"
#include "scsi_helper_func.h"

// This is the private function so that it can be called by the ATA layer as well and make everything follow one single
// code path instead of multiple. This will enhance debug output since it will consistently be in one place for SCSI
// passthrough commands.
eReturnValues private_SCSI_Send_CDB(ScsiIoCtx* scsiIoCtx, ptrSenseDataFields pSenseFields)
{
    eReturnValues      ret                       = UNKNOWN;
    bool               localSenseFieldsAllocated = false;
    ptrSenseDataFields localSenseFields          = M_NULLPTR;
    if (pSenseFields == M_NULLPTR)
    {
        localSenseFields = M_REINTERPRET_CAST(ptrSenseDataFields, safe_calloc(1, sizeof(senseDataFields)));
        if (!localSenseFields)
        {
            return MEMORY_FAILURE;
        }
        localSenseFieldsAllocated = true;
        pSenseFields              = localSenseFields;
    }
    // clear the last command sense data every single time before we issue any commands
    safe_memset(scsiIoCtx->device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0, SPC3_SENSE_LEN);
    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("\n  CDB:\n");
        print_Data_Buffer(scsiIoCtx->cdb, scsiIoCtx->cdbLength, false);
    }
#if defined(_DEBUG)
    // This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a
    // command. This was very important for debugging windows issues, which is why I have this ifdef in place for debug
    // builds. - TJE
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity && scsiIoCtx->pdata != M_NULLPTR)
#else
    // Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity && scsiIoCtx->pdata != M_NULLPTR &&
        scsiIoCtx->direction == XFER_DATA_OUT)
#endif
    {
        printf("\t  Data Buffer being sent:\n");
        print_Data_Buffer(scsiIoCtx->pdata, scsiIoCtx->dataLength, true);
        printf("\n");
    }
    // send the command
    eReturnValues sendIOret = send_IO(scsiIoCtx);
    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity && scsiIoCtx->psense)
    {
        printf("\n  Sense Data Buffer:\n");
        print_Data_Buffer(scsiIoCtx->psense, get_Returned_Sense_Data_Length(scsiIoCtx->psense), false);
        printf("\n");
    }
    get_Sense_Data_Fields(scsiIoCtx->psense, scsiIoCtx->senseDataSize, pSenseFields);
    ret = check_Sense_Key_ASC_ASCQ_And_FRU(scsiIoCtx->device, pSenseFields->scsiStatusCodes.senseKey,
                                           pSenseFields->scsiStatusCodes.asc, pSenseFields->scsiStatusCodes.ascq,
                                           pSenseFields->scsiStatusCodes.fru);
    // if verbose mode and sense data is non-M_NULLPTR, we should try to print out all the relavent information we can
    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity && scsiIoCtx->psense)
    {
        print_Sense_Fields(pSenseFields);
    }
    if (scsiIoCtx->device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        // print command timing information
        print_Command_Time(scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds);
    }
#if defined(_DEBUG)
    // This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a
    // command. This was very important for debugging windows issues, which is why I have this ifdef in place for debug
    // builds. - TJE
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity && scsiIoCtx->pdata != M_NULLPTR)
#else
    // Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity && scsiIoCtx->pdata != M_NULLPTR &&
        scsiIoCtx->direction == XFER_DATA_IN)
#endif
    {
        printf("\t  Data Buffer being returned:\n");
        print_Data_Buffer(scsiIoCtx->pdata, scsiIoCtx->dataLength, true);
        printf("\n");
    }
    if (ret == SUCCESS && sendIOret != SUCCESS)
    {
        ret = sendIOret;
    }

    if ((scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds / UINT64_C(1000000000)) > scsiIoCtx->timeout)
    {
        ret = OS_COMMAND_TIMEOUT;
    }

    // Send a test unit ready command if a problem was found to keep the device performing optimally
    if (scsiIoCtx->device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure &&
        scsiIoCtx->device->drive_info.passThroughHacks.turfValue >= TURF_LIMIT &&
        scsiIoCtx->cdb[0] != TEST_UNIT_READY_CMD)
    {
        switch (ret)
        {
        case SUCCESS:
        case FAILURE:
        case OS_PASSTHROUGH_FAILURE:
        case OS_COMMAND_BLOCKED:
        case OS_COMMAND_NOT_AVAILABLE:
            break;
        default:
            // send a test unit ready
            // backup last sense data and time before we issue the TUR
            {
                uint64_t lastCommandTime = scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds;
                DECLARE_ZERO_INIT_ARRAY(uint8_t, lastSenseData, SPC3_SENSE_LEN);
                safe_memcpy(lastSenseData, SPC3_SENSE_LEN, scsiIoCtx->device->drive_info.lastCommandSenseData,
                            SPC3_SENSE_LEN);
                // issue test unit ready
                scsi_Test_Unit_Ready(scsiIoCtx->device, M_NULLPTR);
                // copy everything back now.
                scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = lastCommandTime;
                safe_memcpy(scsiIoCtx->device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, lastSenseData,
                            SPC3_SENSE_LEN);
            }
        }
    }

    if (localSenseFieldsAllocated)
    {
        safe_free_sensefields(&localSenseFields);
    }
    return ret;
}

// created this function as internal where we can add more flags for now so we can preserve previous functionality at
// this time. Did this so that write buffer can set the first and last segment flags for FWDL commands
static eReturnValues scsi_Send_Cdb_Int(tDevice*               device,
                                       uint8_t*               cdb,
                                       eCDBLen                cdbLen,
                                       uint8_t*               pdata,
                                       uint32_t               dataLen,
                                       eDataTransferDirection dataDirection,
                                       uint8_t*               senseData,
                                       uint32_t               senseDataLen,
                                       uint32_t               timeoutSeconds,
                                       bool                   fwdlFirstSegment,
                                       bool                   fwdlLastSegment)
{
    eReturnValues ret = UNKNOWN;
    ScsiIoCtx     scsiIoCtx;
    uint8_t*      senseBuffer = senseData;
    safe_memset(&scsiIoCtx, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));

    if (senseBuffer == M_NULLPTR || senseDataLen == UINT32_C(0))
    {
        senseBuffer  = device->drive_info.lastCommandSenseData;
        senseDataLen = SPC3_SENSE_LEN;
    }
    else
    {
        safe_memset(senseBuffer, senseDataLen, 0, senseDataLen);
    }
    // check a couple of the parameters before continuing
    if (device == M_NULLPTR)
    {
        perror("device struct is M_NULLPTR!");
        return BAD_PARAMETER;
    }
    if (cdb == M_NULLPTR)
    {
        perror("cdb array is M_NULLPTR!");
        return BAD_PARAMETER;
    }
    if (cdbLen == CDB_LEN_UNKNOWN)
    {
        perror("Invalid CDB length specified!");
        return BAD_PARAMETER;
    }
    if (pdata == M_NULLPTR && dataLen != UINT32_C(0))
    {
        perror("Datalen must be set to 0 when pdata is M_NULLPTR");
        return BAD_PARAMETER;
    }

    // set up the context
    scsiIoCtx.device        = device;
    scsiIoCtx.psense        = senseBuffer;
    scsiIoCtx.senseDataSize = senseDataLen;
    safe_memcpy(&scsiIoCtx.cdb[0], SCSI_IO_CTX_MAX_CDB_LEN, &cdb[0],
                C_CAST(size_t, cdbLen)); // this cast to size_t should be safe since cdbLen should never be negative and
                                         // should match a common value in the enum-TJE
    scsiIoCtx.cdbLength        = C_CAST(uint8_t, cdbLen);
    scsiIoCtx.direction        = dataDirection;
    scsiIoCtx.pdata            = pdata;
    scsiIoCtx.dataLength       = dataLen;
    scsiIoCtx.verbose          = 0;
    scsiIoCtx.timeout          = M_Max(timeoutSeconds, device->drive_info.defaultTimeoutSeconds);
    scsiIoCtx.fwdlFirstSegment = fwdlFirstSegment;
    scsiIoCtx.fwdlLastSegment  = fwdlLastSegment;
    if (timeoutSeconds == 0)
    {
        scsiIoCtx.timeout = M_Max(15, device->drive_info.defaultTimeoutSeconds);
    }

    ret = private_SCSI_Send_CDB(&scsiIoCtx, M_NULLPTR);

    if (senseData && senseDataLen > 0 && senseData != device->drive_info.lastCommandSenseData)
    {
        safe_memcpy(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, senseBuffer,
                    M_Min(SPC3_SENSE_LEN, senseDataLen));
    }

    return ret;
}

eReturnValues scsi_Send_Cdb(tDevice*               device,
                            uint8_t*               cdb,
                            eCDBLen                cdbLen,
                            uint8_t*               pdata,
                            uint32_t               dataLen,
                            eDataTransferDirection dataDirection,
                            uint8_t*               senseData,
                            uint32_t               senseDataLen,
                            uint32_t               timeoutSeconds)
{
    return scsi_Send_Cdb_Int(device, cdb, cdbLen, pdata, dataLen, dataDirection, senseData, senseDataLen,
                             timeoutSeconds, false, false);
}

eReturnValues scsi_SecurityProtocol_In(tDevice* device,
                                       uint8_t  securityProtocol,
                                       uint16_t securityProtocolSpecific,
                                       bool     inc512,
                                       uint32_t allocationLength,
                                       uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
    uint32_t dataLength = allocationLength;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Security Protocol In\n");
    }

    cdb[OPERATION_CODE] = SECURITY_PROTOCOL_IN;
    cdb[1]              = securityProtocol;
    cdb[2]              = M_Byte1(securityProtocolSpecific);
    cdb[3]              = M_Byte0(securityProtocolSpecific);
    if (inc512)
    {
        cdb[4] |= BIT7;
        dataLength *= LEGACY_DRIVE_SEC_SIZE;
    }
    cdb[5]  = RESERVED;
    cdb[6]  = M_Byte3(allocationLength);
    cdb[7]  = M_Byte2(allocationLength);
    cdb[8]  = M_Byte1(allocationLength);
    cdb[9]  = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;

    if (ptrData && allocationLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Protocol In", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Supported_Operation_Codes(tDevice* device,
                                                    bool     rctd,
                                                    uint8_t  reportingOptions,
                                                    uint8_t  requestedOperationCode,
                                                    uint16_t reequestedServiceAction,
                                                    uint32_t allocationLength,
                                                    uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
    senseDataFields senseFields;
    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Requesting SCSI Supported Op Codes\n");
    }

    cdb[OPERATION_CODE] = REPORT_SUPPORTED_OPERATION_CODES_CMD;
    cdb[1]              = 0x0C; // This is always 0x0C per SPC spec
    if (rctd)
    {
        cdb[2] |= BIT7;
    }
    cdb[2] |= C_CAST(uint8_t, reportingOptions & UINT8_C(0x07)); // bit 0,1,2 only valid
    cdb[3]  = requestedOperationCode;
    cdb[4]  = M_Byte1(reequestedServiceAction);
    cdb[5]  = M_Byte0(reequestedServiceAction);
    cdb[6]  = M_Byte3(allocationLength);
    cdb[7]  = M_Byte2(allocationLength);
    cdb[8]  = M_Byte1(allocationLength);
    cdb[9]  = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;

    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Supported Op Codes", ret);
    }
    if (ret != SUCCESS)
    {
        get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
        if (senseFields.validStructure)
        {
            // if invalid operation code, set hack that this is not supported. Do not block this command in this
            // function, just set that so upper layers can choose what to do.
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                     senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                // If invalid field in CDB, check the field pointer (if available) and see if it doesn't like the report
                // type if the field pointer is not available, assume it does not support the report type...-not great,
                // but will probably work well enough for translated devices.
                if (senseFields.senseKeySpecificInformation.senseKeySpecificValid &&
                    senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
                {
                    if (senseFields.senseKeySpecificInformation.field.cdbOrData &&
                        senseFields.senseKeySpecificInformation.field.fieldPointer == 2)
                    {
                        if ((senseFields.senseKeySpecificInformation.field.bitPointerValid &&
                             senseFields.senseKeySpecificInformation.field.bitPointer == 2) ||
                            !rctd)
                        {
                            // reporting options is not liked.
                            if (reportingOptions == REPORT_ALL)
                            {
                                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = false;
                            }
                            else // assume all other report types are not supported for single operation codes being
                                 // requested.
                            {
                                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = false;
                            }
                        }
                    }
                }
                else
                {
                    // assuming report type was not liked...should mostly be translators here. Native SCSI devices
                    // should give the field pointer.
                    if (reportingOptions == REPORT_ALL)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = false;
                    }
                    else // assume all other report types are not supported for single operation codes being requested.
                    {
                        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = false;
                    }
                }
            }
        }
    }
    return ret;
}

static eSCSICmdSupport is_SCSI_Operation_Code_Supported_InqDT(tDevice* device, ptrScsiOperationCodeInfoRequest request)
{
    eSCSICmdSupport cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN;
    if (request->serviceActionValid == false || request->operationCode == WRITE_BUFFER_CMD)
    {
        // Use cmdDT
        uint8_t* inqDT = M_REINTERPRET_CAST(
            uint8_t*, safe_calloc_aligned(UINT8_MAX, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (inqDT != M_NULLPTR)
        {
            if (SUCCESS == scsi_Inquiry(device, inqDT, UINT8_MAX, request->operationCode, false, true))
            {
                // Check the support field for return code and copy back CDB usage data
                // Set return code. The enum is setup to exactly match the values in the support field
                cmdsupport                  = M_STATIC_CAST(eSCSICmdSupport, get_bit_range_uint8(inqDT[1], 2, 0));
                request->cdbUsageDataLength = inqDT[5];
                safe_memcpy(request->cdbUsageData, CDB_LEN_MAX, &inqDT[6], inqDT[5]);
                if (request->serviceAction != UINT16_C(0))
                {
                    request->requestRetriedWithoutSA = true;
                }
            }
            else
            {
                // This should not happen, but assume not supported in case this is a firmware error
                cmdsupport = SCSI_CMD_SUPPORT_NOT_SUPPORTED;
            }
            safe_free_aligned(&inqDT);
        }
        else
        {
            cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN_RETRY;
        }
    }
    else
    {
        // We cannot determine if this command is valid with a service action using cmddt
        cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN;
    }
    return cmdsupport;
}

static eSCSICmdSupport is_SCSI_Operation_Code_Supported_ReportOP(tDevice*                        device,
                                                                 ptrScsiOperationCodeInfoRequest request)
{
    eSCSICmdSupport cmdsupport    = SCSI_CMD_SUPPORT_UNKNOWN;
    uint32_t        requestOpSize = CDB_LEN_MAX + UINT32_C(4);
    uint8_t*        requestOpCode = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(requestOpSize, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (requestOpCode != M_NULLPTR)
    {
// use report supported operation codes to check this CDB
// TODO: Handle write buffer special case. Behavior changes based on standard it complies to
#define MAX_OPCODE_RETRY UINT8_C(2)
        uint8_t retrycount      = UINT8_C(0);
        bool    requestComplete = false;
        do
        {
            if (request->serviceActionValid)
            {
                // request with service action
                if (SUCCESS == scsi_Report_Supported_Operation_Codes(
                                   device, false, REPORT_OPERATION_CODE_AND_SERVICE_ACTION, request->operationCode,
                                   request->serviceAction, requestOpSize, requestOpCode))
                {
                    requestComplete = true;
                    break;
                }
                else
                {
                    // Invalid field in CDB can occur if a service action is associated with this command.
                    // Retry with service action disabled ONLY if it is zero
                    if (request->serviceAction == UINT16_C(0) || request->operationCode == WRITE_BUFFER_CMD)
                    {
                        request->serviceActionValid      = false;
                        request->requestRetriedWithoutSA = true;
                    }
                    else
                    {
                        // do not retry
                        break;
                    }
                }
            }
            else
            {
                // request without a service action
                if (SUCCESS == scsi_Report_Supported_Operation_Codes(device, false, REPORT_OPERATION_CODE,
                                                                     request->operationCode, UINT16_C(0), requestOpSize,
                                                                     requestOpCode))
                {
                    requestComplete = true;
                    break;
                }
                else
                {
                    // Invalid field in CDB can occur if a service action is associated with this command.
                    request->serviceActionValid = true;
                }
            }
            ++retrycount;
        } while (!requestComplete && retrycount < MAX_OPCODE_RETRY);
        if (requestComplete)
        {
            cmdsupport                    = M_STATIC_CAST(eSCSICmdSupport, get_bit_range_uint8(requestOpCode[1], 2, 0));
            request->multipleLogicalUnits = get_bit_range_uint8(requestOpCode[1], 6, 5);
            request->cdbUsageDataLength   = bytes_To_Uint16(requestOpCode[2], requestOpCode[3]);
            safe_memcpy(request->cdbUsageData, CDB_LEN_MAX, &requestOpCode[4],
                        M_Min(request->cdbUsageDataLength, CDB_LEN_MAX));
        }
        safe_free_aligned(&requestOpCode);
    }
    else
    {
        cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN_RETRY;
    }
    return cmdsupport;
}

eSCSICmdSupport is_SCSI_Operation_Code_Supported(tDevice* device, ptrScsiOperationCodeInfoRequest request)
{
    eSCSICmdSupport cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN;
    // Special cases to handle:
    //  Write buffer (firmware download) - may be reported in different ways depending on drive/standard. Sometimes
    //  service action applies, sometimes it doesn't
    DISABLE_NONNULL_COMPARE
    if (device != M_NULLPTR && request != M_NULLPTR)
    {
        bool checkCmd = true;
        if (device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations &&
            device->drive_info.passThroughHacks.scsiHacks.cmdDTchecked &&
            device->drive_info.passThroughHacks.scsiHacks.cmdDTSupported)
        {
            // Cannot do anything to check for this command
            cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN;
            checkCmd   = false;
        }
        else if (!device->drive_info.passThroughHacks.scsiHacks.cmdDTchecked)
        {
            // try a CMD DT request for the inquiry command to see if it passes or not to figure out if this method is
            // supported
            uint8_t* inqDT = M_REINTERPRET_CAST(
                uint8_t*, safe_calloc_aligned(18, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (inqDT != M_NULLPTR)
            {
                if (SUCCESS == scsi_Inquiry(device, inqDT, UINT32_C(18), INQUIRY_CMD, false, true))
                {
                    // While it is a good idea to check the data response, it is also not necessary.
                    // Inquiry is a required command and if CMD DT is not supported a check condition with invalid field
                    // in CDB is provided as the response so getting a good response means this is supported. - TJE
                    device->drive_info.passThroughHacks.scsiHacks.cmdDTchecked   = true;
                    device->drive_info.passThroughHacks.scsiHacks.cmdDTSupported = true;
                }
                else
                {
                    // if this is rejected, consider this checked and not supported
                    device->drive_info.passThroughHacks.scsiHacks.cmdDTchecked   = true;
                    device->drive_info.passThroughHacks.scsiHacks.cmdDTSupported = false;
                }
                safe_free_aligned(&inqDT);
            }
            else
            {
                cmdsupport = SCSI_CMD_SUPPORT_UNKNOWN_RETRY;
                checkCmd   = false;
            }
        }
        if (checkCmd)
        {
            request->requestRetriedWithoutSA = false;
            request->multipleLogicalUnits    = 0;
            if (device->drive_info.scsiVersion >= SCSI_VERSION_SPC_3 &&
                !device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations)
            {
                cmdsupport = is_SCSI_Operation_Code_Supported_ReportOP(device, request);
            }
            else
            {
                cmdsupport = is_SCSI_Operation_Code_Supported_InqDT(device, request);
            }
        }
    }
    RESTORE_NONNULL_COMPARE
    return cmdsupport;
}

eReturnValues scsi_Sanitize_Cmd(tDevice*             device,
                                eScsiSanitizeFeature sanitizeFeature,
                                bool                 immediate,
                                bool                 znr,
                                bool                 ause,
                                uint16_t             parameterListLength,
                                uint8_t*             ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
    eDataTransferDirection dataDir = XFER_NO_DATA;

    safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0, SPC3_SENSE_LEN);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Sanitize Command\n");
    }

    cdb[OPERATION_CODE] = SANITIZE_CMD;
    cdb[1]              = sanitizeFeature & 0x1F; // make sure we don't set any higher bits
    if (immediate)
    {
        cdb[1] |= BIT7;
    }
    if (znr)
    {
        cdb[1] |= BIT6;
    }
    if (ause)
    {
        cdb[1] |= BIT5;
    }
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    // parameter list length
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);

    switch (sanitizeFeature)
    {
    case SCSI_SANITIZE_OVERWRITE:
        dataDir = XFER_DATA_OUT;
        break;
    default:
        dataDir = XFER_NO_DATA;
        break;
    }

    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, parameterListLength, dataDir,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Sanitize", ret);
    }
    return ret;
}

eReturnValues scsi_Sanitize_Block_Erase(tDevice* device, bool allowUnrestrictedSanitizeExit, bool immediate, bool znr)
{
    return scsi_Sanitize_Cmd(device, SCSI_SANITIZE_BLOCK_ERASE, immediate, znr, allowUnrestrictedSanitizeExit, 0,
                             M_NULLPTR);
}

eReturnValues scsi_Sanitize_Cryptographic_Erase(tDevice* device,
                                                bool     allowUnrestrictedSanitizeExit,
                                                bool     immediate,
                                                bool     znr)
{
    return scsi_Sanitize_Cmd(device, SCSI_SANITIZE_CRYPTOGRAPHIC_ERASE, immediate, znr, allowUnrestrictedSanitizeExit,
                             0, M_NULLPTR);
}

eReturnValues scsi_Sanitize_Exit_Failure_Mode(tDevice* device)
{
    return scsi_Sanitize_Cmd(device, SCSI_SANITIZE_EXIT_FAILURE_MODE, false, false, false, 0, M_NULLPTR);
}

eReturnValues scsi_Sanitize_Overwrite(tDevice*                   device,
                                      bool                       allowUnrestrictedSanitizeExit,
                                      bool                       znr,
                                      bool                       immediate,
                                      bool                       invertBetweenPasses,
                                      eScsiSanitizeOverwriteTest test,
                                      uint8_t                    overwritePasses,
                                      uint8_t*                   pattern,
                                      uint16_t                   patternLengthBytes)
{
    eReturnValues ret = UNKNOWN;
    if ((patternLengthBytes != 0 && pattern == M_NULLPTR) || (patternLengthBytes > device->drive_info.deviceBlockSize))
    {
        return BAD_PARAMETER;
    }
    uint8_t* overwriteBuffer =
        safe_calloc_aligned(patternLengthBytes + 4, sizeof(uint8_t), device->os_info.minimumAlignment);
    if (!overwriteBuffer)
    {
        return MEMORY_FAILURE;
    }
    overwriteBuffer[0] = overwritePasses & UINT8_C(0x1F);
    overwriteBuffer[0] |= C_CAST(uint8_t, (C_CAST(uint8_t, test) & UINT8_C(0x03)) << 5);
    if (invertBetweenPasses)
    {
        overwriteBuffer[0] |= BIT7;
    }
    overwriteBuffer[1] = RESERVED;
    overwriteBuffer[2] = M_Byte1(patternLengthBytes);
    overwriteBuffer[3] = M_Byte0(patternLengthBytes);
    if (patternLengthBytes > 0)
    {
        safe_memcpy(&overwriteBuffer[4], patternLengthBytes, pattern, patternLengthBytes);
    }
    ret = scsi_Sanitize_Cmd(device, SCSI_SANITIZE_OVERWRITE, immediate, znr, allowUnrestrictedSanitizeExit,
                            patternLengthBytes + 4, overwriteBuffer);
    safe_free_aligned(&overwriteBuffer);
    return ret;
}

eReturnValues scsi_Request_Sense_Cmd(tDevice* device, bool descriptorBit, uint8_t* pdata, uint16_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Request Sense Command\n");
    }
    DISABLE_NONNULL_COMPARE
    if (pdata == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    // Set up the CDB.
    cdb[OPERATION_CODE] = REQUEST_SENSE_CMD; // REQUEST_SENSE;
    if (descriptorBit)
    {
        cdb[1] |= SCSI_REQUEST_SENSE_DESC_BIT_SET;
    }
    if (dataSize > SPC3_SENSE_LEN)
    {
        cdb[4] = SPC3_SENSE_LEN;
    }
    else
    {
        cdb[4] = M_Byte0(dataSize);
    }

    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), pdata, dataSize, XFER_DATA_IN,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Request Sense", ret);
    }
    return ret;
}

eReturnValues scsi_Log_Sense_Cmd(tDevice* device,
                                 bool     saveParameters,
                                 uint8_t  pageControl,
                                 uint8_t  pageCode,
                                 uint8_t  subpageCode,
                                 uint16_t paramPointer,
                                 uint8_t* ptrData,
                                 uint16_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
    senseDataFields senseFields;
    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Log Sense Command, page code: 0x%02" PRIx8 "\n", pageCode);
    }
    // Set up the CDB.
    cdb[OPERATION_CODE] = LOG_SENSE_CMD;
    if (saveParameters)
    {
        cdb[1] |= 0x01;
    }
    cdb[2] |= C_CAST(uint8_t, (pageControl & UINT8_C(0x03)) << 6);
    cdb[2] |= C_CAST(uint8_t, pageCode & UINT8_C(0x3F));
    cdb[3] = subpageCode;
    cdb[4] = RESERVED;
    cdb[5] = M_Byte1(paramPointer);
    cdb[6] = M_Byte0(paramPointer);
    cdb[7] = M_Byte1(dataSize);
    cdb[8] = M_Byte0(dataSize);

    if (dataSize > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Log Sense", ret);
    }
    if (ret != SUCCESS)
    {
        get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
        if (senseFields.validStructure)
        {
            // if invalid operation code, set hack that this is not supported. Do not block this command in this
            // function, just set that so upper layers can choose what to do.
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                     senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                // If invalid field in CDB, check the field pointer (if available) and see if it doesn't like the report
                // type if the field pointer is not available, assume it does not support the report type...-not great,
                // but will probably work well enough for translated devices.
                if (senseFields.senseKeySpecificInformation.senseKeySpecificValid &&
                    senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
                {
                    if (senseFields.senseKeySpecificInformation.field.cdbOrData &&
                        senseFields.senseKeySpecificInformation.field.fieldPointer == 3)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                    }
                }
                else
                {
                    // no sense key specific information, so we need to check a few other things to decide when this is
                    // not supported.
                    if (device->drive_info.passThroughHacks.scsiHacks.attemptedLPs < UINT8_MAX)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.attemptedLPs += 1;
                    }
                    // only come into here if we have not previously read a log page page successfully.
                    if (device->drive_info.passThroughHacks.scsiHacks.successfulLPs == 0)
                    {
                        if (pageCode == 0 && subpageCode == 0xFF)
                        {
                            // since list of page and subpages supported returned an error, assume subpages are not
                            // supported.
                            device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                        }
                        else if ((pageCode == 0 && subpageCode == 0) ||
                                 (device->drive_info.passThroughHacks.scsiHacks.attemptedLPs >= MAX_LP_ATTEMPTS))
                        {
                            // assume that since the list of supported pages was requested that this device does not
                            // support log pages at all. This is a reasonable assumption to make and should help with
                            // USB drives
                            // we've attempted at least MAX_LP_ATTEMPTS to read a log page page and it has not been
                            // successful, so assume this device does not support log pages.
                            device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                        }
                    }
                }
            }
        }
    }
    else if (ret == SUCCESS)
    {
        if (device->drive_info.passThroughHacks.scsiHacks.successfulLPs < UINT8_MAX)
        {
            device->drive_info.passThroughHacks.scsiHacks.successfulLPs += 1;
        }
    }
    return ret;
}

eReturnValues scsi_Log_Select_Cmd(tDevice* device,
                                  bool     pcr,
                                  bool     sp,
                                  uint8_t  pageControl,
                                  uint8_t  pageCode,
                                  uint8_t  subpageCode,
                                  uint16_t parameterListLength,
                                  uint8_t* ptrData,
                                  uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Log Select Command, page code 0x%02" PRIx8 "\n", pageCode);
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = LOG_SELECT_CMD;
    if (sp)
    {
        cdb[1] |= UINT8_C(0x01);
    }
    if (pcr)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] |= C_CAST(uint8_t, (pageControl & UINT8_C(0x03)) << 6);
    cdb[2] |= C_CAST(uint8_t, pageCode & UINT8_C(0x3F));
    cdb[3] = subpageCode;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Log Select", ret);
    }
    return ret;
}

eReturnValues scsi_Send_Diagnostic(tDevice* device,
                                   uint8_t  selfTestCode,
                                   uint8_t  pageFormat,
                                   uint8_t  selfTestBit,
                                   uint8_t  deviceOffLIne,
                                   uint8_t  unitOffLine,
                                   uint16_t parameterListLength,
                                   uint8_t* pdata,
                                   uint16_t dataSize,
                                   uint32_t timeoutSeconds)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Send Diagnostic Command\n");
    }
    // Set up the CDB.
    cdb[OPERATION_CODE] = SEND_DIAGNOSTIC_CMD; // Send Diagnostic
    cdb[1] |= C_CAST(uint8_t, selfTestCode << 5);
    cdb[1] |= C_CAST(uint8_t, (pageFormat & UINT8_C(0x01)) << 4);
    cdb[1] |= C_CAST(uint8_t, (selfTestBit & UINT8_C(0x01)) << 2);
    cdb[1] |= C_CAST(uint8_t, (deviceOffLIne & UINT8_C(0x01)) << 1);
    cdb[1] |= C_CAST(uint8_t, (unitOffLine & UINT8_C(0x01)));
    cdb[3] = M_Byte1(parameterListLength);
    cdb[4] = M_Byte0(parameterListLength);
    // send the command
    if (!pdata)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), pdata, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Send Diagnostic", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Capacity_10(tDevice* device, uint8_t* pdata, uint16_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Capacity 10 command\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_CAPACITY_10;
    // send the command
    if (dataSize > 0 && pdata)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), pdata, dataSize, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Capacity 10", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Capacity_16(tDevice* device, uint8_t* pdata, uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Capacity 16 command\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_CAPACITY_16;
    cdb[1]              = 0x10;
    cdb[10]             = M_Byte3(dataSize);
    cdb[11]             = M_Byte2(dataSize);
    cdb[12]             = M_Byte1(dataSize);
    cdb[13]             = M_Byte0(dataSize);
    // send the command
    if (dataSize > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), pdata, dataSize, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Capacity 16", ret);
    }
    return ret;
}

eReturnValues scsi_Mode_Sense_6(tDevice*             device,
                                uint8_t              pageCode,
                                uint8_t              allocationLength,
                                uint8_t              subPageCode,
                                bool                 DBD,
                                eScsiModePageControl pageControl,
                                uint8_t*             ptrData)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    senseDataFields senseFields;
    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Mode Sense 6, page 0x%02" PRIx8 "\n", pageCode);
    }
    cdb[OPERATION_CODE] = MODE_SENSE_6_CMD;
    if (DBD)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] |= C_CAST(uint8_t, (pageControl & UINT8_C(0x03)) << 6);
    cdb[2] |= C_CAST(uint8_t, pageCode & UINT8_C(0x3F));
    cdb[3] = subPageCode;
    cdb[4] = allocationLength;
    cdb[5] = 0; // control
    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Sense 6", ret);
    }
    if (ret != SUCCESS) // && !device->drive_info.passThroughHacks.hacksSetByReportedID)//only setup these hacks if the
                        // device has not been looked up for results in our internal database-TJE
    {
        get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
        if (senseFields.validStructure)
        {
            // if invalid operation code, set hack that this is not supported. Do not block this command in this
            // function, just set that so upper layers can choose what to do.
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                // This is only accurate for drives that ONLY support the mode sense/select 6 byte commands.
                // May need to expand this condition further to make sure it does not cause more impact.
                // by default, almost all opensea-operations code uses the 10 byte command instead for modern drives.
                // This is expected to have little to no impact on modern devices - TJE
                if (device->drive_info.passThroughHacks.scsiHacks.successfulMP6s == 0 &&
                    device->drive_info.passThroughHacks.scsiHacks.attemptedMP6s >= MAX_MP6_ATTEMPTS)
                {
                    device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                }
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                     senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                // If invalid field in CDB, check the field pointer (if available) and see if it doesn't like the report
                // type if the field pointer is not available, assume it does not support the report type...-not great,
                // but will probably work well enough for translated devices.
                if (senseFields.senseKeySpecificInformation.senseKeySpecificValid &&
                    senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
                {
                    if (senseFields.senseKeySpecificInformation.field.cdbOrData &&
                        senseFields.senseKeySpecificInformation.field.fieldPointer == 3)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                    }
                }
                else
                {
                    // no sense key specific information, so we need to check a few other things to decide when this is
                    // not supported.
                    if (device->drive_info.passThroughHacks.scsiHacks.attemptedMP6s < UINT8_MAX)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.attemptedMP6s += 1;
                    }
                    // only come into here if we have not previously read a log page page successfully.
                    if (device->drive_info.passThroughHacks.scsiHacks.successfulMP6s == 0 &&
                        device->drive_info.passThroughHacks.scsiHacks.attemptedMP6s >= MAX_MP6_ATTEMPTS)
                    {
                        // we've attempted at least MAX_MP_ATTEMPTS to read a log page page and it has not been
                        // successful, so assume this device does not support log pages.
                        device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                    }
                }
            }
        }
    }
    else
    {
        if (device->drive_info.passThroughHacks.scsiHacks.successfulMP6s < UINT8_MAX)
        {
            device->drive_info.passThroughHacks.scsiHacks.successfulMP6s += 1;
        }
        if (subPageCode == 0 && device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success < UINT8_MAX)
        {
            device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success += 1;
        }
    }
    return ret;
}

eReturnValues scsi_Mode_Sense_10(tDevice*             device,
                                 uint8_t              pageCode,
                                 uint32_t             allocationLength,
                                 uint8_t              subPageCode,
                                 bool                 DBD,
                                 bool                 LLBAA,
                                 eScsiModePageControl pageControl,
                                 uint8_t*             ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
    senseDataFields senseFields;
    safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Mode Sense 10, page 0x%02" PRIx8 "\n", pageCode);
    }
    // Set up the CDB.
    cdb[OPERATION_CODE] = MODE_SENSE10;
    if (LLBAA)
    {
        cdb[1] |= BIT4;
    }
    if (DBD)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] |= C_CAST(uint8_t, (pageControl & UINT8_C(0x03)) << 6);
    cdb[2] |= C_CAST(uint8_t, pageCode & UINT8_C(0x3F));
    cdb[3] = subPageCode;
    cdb[4] = RESERVED; // reserved
    cdb[5] = RESERVED; // reserved
    cdb[6] = RESERVED; // reserved
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    cdb[9] = 0; // control
    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Sense 10", ret);
    }
    if (ret != SUCCESS)
    {
        get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
        if (senseFields.validStructure)
        {
            // if invalid operation code, set hack that this is not supported. Do not block this command in this
            // function, just set that so upper layers can choose what to do.
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                senseFields.scsiStatusCodes.asc == 0x20 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                // do NOT set mode pages not supported. Tell this to retry with 6 byte by setting this first
                if (device->drive_info.passThroughHacks.scsiHacks.successfulMP10s == 0)
                {
                    device->drive_info.passThroughHacks.scsiHacks.mode6bytes = true;
                }
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                     senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0x00)
            {
                // If invalid field in CDB, check the field pointer (if available) and see if it doesn't like the report
                // type if the field pointer is not available, assume it does not support the report type...-not great,
                // but will probably work well enough for translated devices.
                if (senseFields.senseKeySpecificInformation.senseKeySpecificValid &&
                    senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
                {
                    if (senseFields.senseKeySpecificInformation.field.cdbOrData &&
                        senseFields.senseKeySpecificInformation.field.fieldPointer == 3)
                    {
                        // Do not set this here since it may just be a page code that isn't supported-TJE
                        // device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                    }
                }
                else
                {
                    // no sense key specific information, so we need to check a few other things to decide when this is
                    // not supported.
                    if (device->drive_info.passThroughHacks.scsiHacks.attemptedMP10s < UINT8_MAX)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.attemptedMP10s += 1;
                    }
                    // only come into here if we have not previously read a log page page successfully.
                    if (device->drive_info.passThroughHacks.scsiHacks.successfulMP10s == 0 &&
                        device->drive_info.passThroughHacks.scsiHacks.attemptedMP10s >= MAX_MP10_ATTEMPTS &&
                        device->drive_info.passThroughHacks.scsiHacks.successfulMP6s == 0 &&
                        device->drive_info.passThroughHacks.scsiHacks.attemptedMP6s >= MAX_MP6_ATTEMPTS)
                    {
                        // we've attempted at least MAX_MP_ATTEMPTS to read a log page page and it has not been
                        // successful, so assume this device does not support log pages.
                        device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                    }
                    else if (device->drive_info.passThroughHacks.scsiHacks.successfulMP10s == 0 &&
                             device->drive_info.passThroughHacks.scsiHacks.mp6sp0Success > 0 && subPageCode == 0)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero = true;
                    }
                    else if (device->drive_info.passThroughHacks.scsiHacks.successfulMP10s == 0 &&
                             device->drive_info.passThroughHacks.scsiHacks.attemptedMP10s >= MAX_MP10_ATTEMPTS &&
                             device->drive_info.passThroughHacks.scsiHacks.successfulMP6s > 0 &&
                             !device->drive_info.passThroughHacks.scsiHacks.useMode6BForSubpageZero)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.mode6bytes = true;
                    }
                }
            }
        }
    }
    else
    {
        if (device->drive_info.passThroughHacks.scsiHacks.successfulMP10s < UINT8_MAX)
        {
            device->drive_info.passThroughHacks.scsiHacks.successfulMP10s += 1;
        }
    }
    return ret;
}

eReturnValues scsi_Mode_Select_6(tDevice* device,
                                 uint8_t  parameterListLength,
                                 bool     pageFormat,
                                 bool     savePages,
                                 bool     resetToDefaults,
                                 uint8_t* ptrData,
                                 uint32_t dataSize)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Mode Select 6\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = MODE_SELECT10;
    if (pageFormat)
    {
        cdb[1] |= BIT4;
    }
    if (savePages)
    {
        cdb[1] |= BIT0;
    }
    if (resetToDefaults)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = parameterListLength;
    cdb[5] = 0; // control
    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Select 6", ret);
    }
    return ret;
}

eReturnValues scsi_Mode_Select_10(tDevice* device,
                                  uint16_t parameterListLength,
                                  bool     pageFormat,
                                  bool     savePages,
                                  bool     resetToDefaults,
                                  uint8_t* ptrData,
                                  uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Mode Select 10\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = MODE_SELECT10;
    if (pageFormat)
    {
        cdb[1] |= BIT4;
    }
    if (savePages)
    {
        cdb[1] |= BIT0;
    }
    if (resetToDefaults)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    cdb[9] = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Select 10", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Buffer(tDevice*         device,
                                eWriteBufferMode mode,
                                uint8_t          modeSpecific,
                                uint8_t          bufferID,
                                uint32_t         bufferOffset,
                                uint32_t         parameterListLength,
                                uint8_t*         ptrData,
                                bool             firstSegment,
                                bool             lastSegment,
                                uint32_t         timeoutSeconds)
{
    eReturnValues ret = UNKNOWN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
    uint32_t writeBufferTimeout = timeoutSeconds;
    if (writeBufferTimeout == 0)
    {
        writeBufferTimeout = 30; // default to 30 seconds since this is most often used for FWDL and that activation can
                                 // sometimes take more time
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Buffer\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = WRITE_BUFFER_CMD;
    cdb[1]              = C_CAST(uint8_t, mode);
    cdb[1] |= C_CAST(uint8_t, (modeSpecific & UINT8_C(0x07)) << 5);
    cdb[2] = bufferID;
    cdb[3] = M_Byte2(bufferOffset);
    cdb[4] = M_Byte1(bufferOffset);
    cdb[5] = M_Byte0(bufferOffset);
    cdb[6] = M_Byte2(parameterListLength);
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    cdb[9] = 0; // control

    // send the command
    if (ptrData && parameterListLength != 0)
    {
        ret = scsi_Send_Cdb_Int(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, parameterListLength, XFER_DATA_OUT,
                                device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, writeBufferTimeout,
                                firstSegment, lastSegment);
    }
    else
    {
        ret = scsi_Send_Cdb_Int(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                                device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, writeBufferTimeout,
                                firstSegment, lastSegment);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Buffer", ret);
    }
    return ret;
}

eReturnValues scsi_Inquiry(tDevice* device,
                           uint8_t* pdata,
                           uint32_t dataLength,
                           uint8_t  pageCode,
                           bool     evpd,
                           bool     cmdDt)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        if (evpd)
        {
            printf("Sending SCSI Inquiry, VPD = %02" PRIX8 "h\n", pageCode);
        }
        else if (cmdDt)
        {
            printf("Sending SCSI Inquiry, CmdDt = %02" PRIX8 "h\n", pageCode);
        }
        else
        {
            printf("Sending SCSI Inquiry\n");
        }
    }

    cdb[OPERATION_CODE] = INQUIRY_CMD;
    if (evpd)
    {
        cdb[1] |= BIT0;
    }
    if (cmdDt)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = pageCode;
    cdb[3] = M_Byte1(dataLength);
    cdb[4] = M_Byte0(dataLength);
    cdb[5] = 0; // control

    // send the command
    if (dataLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), pdata, dataLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
        if (ret == SUCCESS && !evpd && !cmdDt && pageCode == 0)
        {
            uint8_t version;
            if (pdata != device->drive_info.scsiVpdData.inquiryData)
            {
                // this should only be copying std inquiry data to thislocation in the device struct to keep it up to
                // date each time an inquiry is sent to the drive.
                safe_memcpy(device->drive_info.scsiVpdData.inquiryData, SPC_INQ_DATA_LEN, pdata,
                            M_Min(dataLength, SPC_INQ_DATA_LEN));
            }
            version = pdata[2];
            switch (version) // convert some versions since old standards broke the version number into ANSI vs ECMA vs
                             // ISO standard numbers
            {
            case 0x81:
                version = SCSI_VERSION_SCSI; // changing to 1 for SCSI
                break;
            case 0x80:
            case 0x82:
                version = SCSI_VERSION_SCSI2; // changing to 2 for SCSI 2
                break;
            case 0x83:
                version = SCSI_VERSION_SPC; // changing to 3 for SPC
                break;
            case 0x84:
                version = SCSI_VERSION_SPC_2; // changing to 4 for SPC2
                break;
            default:
                // convert some versions since old standards broke the version number into ANSI vs ECMA vs ISO standard
                // numbers
                if ((version >= 0x08 && version <= 0x0C) || (version >= 0x40 && version <= 0x44) ||
                    (version >= 0x48 && version <= 0x4C) || (version >= 0x80 && version <= 0x84) ||
                    (version >= 0x88 && version <= 0x8C))
                {
                    // these are obsolete version numbers
                    version = get_bit_range_uint8(version, 3, 0);
                }
                break;
            }
            device->drive_info.scsiVersion = version; // changing this to one of these version numbers to keep the rest
                                                      // of the library code that would use this simple. - TJE
        }
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Inquiry", ret);
    }
    if (ret != SUCCESS && evpd)
    {
        // check if invalid field in CDB for VPD pages.
        senseDataFields senseFields;
        safe_memset(&senseFields, sizeof(senseDataFields), 0, sizeof(senseDataFields));
        get_Sense_Data_Fields(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &senseFields);
        if (senseFields.validStructure)
        {
            if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST &&
                senseFields.scsiStatusCodes.asc == 0x24 && senseFields.scsiStatusCodes.ascq == 0 &&
                senseFields.senseKeySpecificInformation.senseKeySpecificValid &&
                senseFields.senseKeySpecificInformation.type == SENSE_KEY_SPECIFIC_FIELD_POINTER)
            {
                // this reported enough information to know what the error is, so we can use it to determine if we set a
                // hack or not.
                if (senseFields.senseKeySpecificInformation.field.cdbOrData &&
                    senseFields.senseKeySpecificInformation.field.fieldPointer == 1)
                {
                    // assume it did not like the evpd bit
                    if (!cmdDt)
                    {
                        device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                    }
                }
            }
            else if (senseFields.scsiStatusCodes.senseKey == SENSE_KEY_ILLEGAL_REQUEST)
            {
                // only checking for illegal request because not all USB devices are reporting correct asc, ascq for
                // unsupported pages.-TJE If hacks are not already set, we can set them here if there have not already
                // been other successful VPD reads. In the most common case, this code will read the list of supported
                // pages first, then only read those pages. However, it is possible that some code will just request a
                // VPD page. If there has been at least 1 successful read before and the no VPD hack is not set, then do
                // not turn off VPD pages for no reason.
                if (device->drive_info.passThroughHacks.scsiHacks.attemptedVPDs < UINT8_MAX)
                {
                    device->drive_info.passThroughHacks.scsiHacks.attemptedVPDs += 1;
                }
                // only come into here if we have not previously read a VPD page successfully.
                if (device->drive_info.passThroughHacks.scsiHacks.successfulVPDs == 0 && pageCode == 0 &&
                    !device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)
                {
                    // assume that since the list of supported pages was requested that this device does not
                    // support VPD pages at all. This is a reasonable assumption to make and should help with USB drives
                    device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                }
                if (device->drive_info.passThroughHacks.scsiHacks.successfulVPDs == 0 &&
                    device->drive_info.passThroughHacks.scsiHacks.attemptedVPDs >= MAX_VPD_ATTEMPTS)
                {
                    // we've attempted at least MAX_VPD_ATTEMPTS to read a VPD page and it has not been successful,
                    // so assume this device does not support VPD pages.
                    device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                }
            }
        }
    }
    else if (ret == SUCCESS && evpd)
    {
        // increment this since we got a successful command completion.
        // NOTE: This does not validate the page code is correct, but that should be added at some point-TJE
        if (device->drive_info.passThroughHacks.scsiHacks.successfulVPDs < UINT8_MAX)
        {
            device->drive_info.passThroughHacks.scsiHacks.successfulVPDs += 1;
        }
    }

    return ret;
}

eReturnValues scsi_Read_Media_Serial_Number(tDevice* device, uint32_t allocationLength, uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Media Serial Number\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_MEDIA_SERIAL_NUMBER;
    cdb[1] |= 0x01; // service action
    cdb[2]  = RESERVED;
    cdb[3]  = RESERVED;
    cdb[4]  = RESERVED;
    cdb[5]  = RESERVED;
    cdb[6]  = M_Byte3(allocationLength);
    cdb[7]  = M_Byte2(allocationLength);
    cdb[8]  = M_Byte1(allocationLength);
    cdb[9]  = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Media Serial Number", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Attribute(tDevice* device,
                                  uint8_t  serviceAction,
                                  uint32_t restricted,
                                  uint8_t  logicalVolumeNumber,
                                  uint8_t  partitionNumber,
                                  uint16_t firstAttributeIdentifier,
                                  uint32_t allocationLength,
                                  bool     cacheBit,
                                  uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Attribute\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_ATTRIBUTE;
    cdb[1]              = serviceAction & 0x1F;
    cdb[2]              = M_Byte2(restricted);
    cdb[3]              = M_Byte1(restricted);
    cdb[4]              = M_Byte0(restricted);
    cdb[5]              = logicalVolumeNumber;
    cdb[6]              = RESERVED;
    cdb[7]              = partitionNumber;
    cdb[8]              = M_Byte1(firstAttributeIdentifier);
    cdb[9]              = M_Byte0(firstAttributeIdentifier);
    cdb[10]             = M_Byte3(allocationLength);
    cdb[11]             = M_Byte2(allocationLength);
    cdb[12]             = M_Byte1(allocationLength);
    cdb[13]             = M_Byte0(allocationLength);
    cdb[14]             = RESERVED;
    if (cacheBit)
    {
        cdb[14] |= BIT0;
    }
    cdb[15] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Attribute", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Buffer(tDevice* device,
                               uint8_t  mode,
                               uint8_t  bufferID,
                               uint32_t bufferOffset,
                               uint32_t allocationLength,
                               uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Buffer\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_BUFFER_CMD;
    // set the mode
    cdb[1] = mode; // &0x1F;//removed this &0x1F in order to get internal status log going. Looks like some reserved
                   // bits may be used in a newer spec or something that I don't have yet. - TJE
    // buffer ID
    cdb[2] = bufferID;
    cdb[3] = M_Byte2(bufferOffset);
    cdb[4] = M_Byte1(bufferOffset);
    cdb[5] = M_Byte0(bufferOffset);
    cdb[6] = M_Byte2(allocationLength);
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    cdb[9] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Buffer", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Buffer_16(tDevice* device,
                                  uint8_t  mode,
                                  uint8_t  modeSpecific,
                                  uint8_t  bufferID,
                                  uint64_t bufferOffset,
                                  uint32_t allocationLength,
                                  uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Buffer 16\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_BUFFER_16_CMD;
    // set the mode
    cdb[1] = mode & UINT8_C(0x1F);
    cdb[1] |= C_CAST(uint8_t, modeSpecific << 5);
    cdb[2]  = M_Byte7(bufferOffset);
    cdb[3]  = M_Byte6(bufferOffset);
    cdb[4]  = M_Byte5(bufferOffset);
    cdb[5]  = M_Byte4(bufferOffset);
    cdb[6]  = M_Byte3(bufferOffset);
    cdb[7]  = M_Byte2(bufferOffset);
    cdb[8]  = M_Byte1(bufferOffset);
    cdb[9]  = M_Byte0(bufferOffset);
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    // buffer ID
    cdb[14] = bufferID;
    // control
    cdb[15] = 0;

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Buffer 16", ret);
    }
    return ret;
}

eReturnValues scsi_Receive_Diagnostic_Results(tDevice* device,
                                              bool     pcv,
                                              uint8_t  pageCode,
                                              uint16_t allocationLength,
                                              uint8_t* ptrData,
                                              uint32_t timeoutSeconds)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Receive Diagnostic Results, page code = 0x%02" PRIX8 "\n", pageCode);
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = RECEIVE_DIAGNOSTIC_RESULTS;
    if (pcv)
    {
        cdb[1] |= BIT0;
    }
    cdb[2] = pageCode;
    cdb[3] = M_Byte1(allocationLength);
    cdb[4] = M_Byte0(allocationLength);
    cdb[5] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Receive Diagnostic Results", ret);
    }
    return ret;
}

eReturnValues scsi_Remove_I_T_Nexus(tDevice* device, uint32_t parameterListLength, uint8_t* ptrData, uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Remove I_T Nexus\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REMOVE_I_T_NEXUS;
    cdb[1]              = 0x0C;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(parameterListLength);
    cdb[7]              = M_Byte2(parameterListLength);
    cdb[8]              = M_Byte1(parameterListLength);
    cdb[9]              = M_Byte0(parameterListLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Remove I_T Nexus", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Aliases(tDevice* device, uint32_t allocationLength, uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Aliases\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_ALIASES_CMD;
    cdb[1]              = 0x0B;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(allocationLength);
    cdb[7]              = M_Byte2(allocationLength);
    cdb[8]              = M_Byte1(allocationLength);
    cdb[9]              = M_Byte0(allocationLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Aliases", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Identifying_Information(tDevice* device,
                                                  uint16_t restricted,
                                                  uint32_t allocationLength,
                                                  uint8_t  identifyingInformationType,
                                                  uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Identifying Information\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_IDENTIFYING_INFORMATION;
    cdb[1]              = 0x05;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = M_Byte1(restricted); // SCC2
    cdb[5]              = M_Byte0(restricted);
    cdb[6]              = M_Byte3(allocationLength);
    cdb[7]              = M_Byte2(allocationLength);
    cdb[8]              = M_Byte1(allocationLength);
    cdb[9]              = M_Byte0(allocationLength);
    cdb[10]             = (identifyingInformationType & 0x7F) >> 1;
    cdb[11]             = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Identifying Information", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Luns(tDevice* device, uint8_t selectReport, uint32_t allocationLength, uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report LUNs\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_LUNS_CMD;
    cdb[1]              = RESERVED;
    cdb[2]              = selectReport;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(allocationLength);
    cdb[7]              = M_Byte2(allocationLength);
    cdb[8]              = M_Byte1(allocationLength);
    cdb[9]              = M_Byte0(allocationLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report LUNs", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Priority(tDevice* device,
                                   uint8_t  priorityReported,
                                   uint32_t allocationLength,
                                   uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Priority\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_PRIORITY_CMD;
    cdb[1]              = 0x0E;
    cdb[2]              = C_CAST(uint8_t, (priorityReported & 0x03) << 6);
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(allocationLength);
    cdb[7]              = M_Byte2(allocationLength);
    cdb[8]              = M_Byte1(allocationLength);
    cdb[9]              = M_Byte0(allocationLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Priority", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Supported_Task_Management_Functions(tDevice* device,
                                                              bool     repd,
                                                              uint32_t allocationLength,
                                                              uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Supported Task Management Functions\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCS;
    cdb[1]              = 0x0D;
    if (repd)
    {
        cdb[2] |= BIT7;
    }
    cdb[3]  = RESERVED;
    cdb[4]  = RESERVED;
    cdb[5]  = RESERVED;
    cdb[6]  = M_Byte3(allocationLength);
    cdb[7]  = M_Byte2(allocationLength);
    cdb[8]  = M_Byte1(allocationLength);
    cdb[9]  = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Supported Task Management Functions", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Timestamp(tDevice* device, uint32_t allocationLength, uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
    ScsiIoCtx scsiIoCtx;
    safe_memset(&scsiIoCtx, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Timestamp\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCS;
    cdb[1]              = 0x0F;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(allocationLength);
    cdb[7]              = M_Byte2(allocationLength);
    cdb[8]              = M_Byte1(allocationLength);
    cdb[9]              = M_Byte0(allocationLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Timestamp", ret);
    }
    return ret;
}

eReturnValues scsi_SecurityProtocol_Out(tDevice* device,
                                        uint8_t  securityProtocol,
                                        uint16_t securityProtocolSpecific,
                                        bool     inc512,
                                        uint32_t transferLength,
                                        uint8_t* ptrData,
                                        uint32_t timeout)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
    uint32_t dataLength = transferLength;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Security Protocol Out\n");
    }

    cdb[OPERATION_CODE] = SECURITY_PROTOCOL_OUT;
    cdb[1]              = securityProtocol;
    cdb[2]              = M_Byte1(securityProtocolSpecific);
    cdb[3]              = M_Byte0(securityProtocolSpecific);
    if (inc512)
    {
        cdb[4] |= BIT7;
        dataLength *= LEGACY_DRIVE_SEC_SIZE;
    }
    cdb[5]  = RESERVED;
    cdb[6]  = M_Byte3(transferLength);
    cdb[7]  = M_Byte2(transferLength);
    cdb[8]  = M_Byte1(transferLength);
    cdb[9]  = M_Byte0(transferLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;

    // send the command
    if (ptrData && transferLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Protocol Out", ret);
    }

    return ret;
}

eReturnValues scsi_Set_Identifying_Information(tDevice* device,
                                               uint16_t restricted,
                                               uint32_t parameterListLength,
                                               uint8_t  identifyingInformationType,
                                               uint8_t* ptrData,
                                               uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Identifying Information\n");
    }

    cdb[OPERATION_CODE] = SET_IDENTIFYING_INFORMATION;
    cdb[1]              = 0x06;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = M_Byte1(restricted); // SCC
    cdb[5]              = M_Byte0(restricted);
    cdb[6]              = M_Byte3(parameterListLength);
    cdb[7]              = M_Byte2(parameterListLength);
    cdb[8]              = M_Byte1(parameterListLength);
    cdb[9]              = M_Byte0(parameterListLength);
    cdb[10]             = C_CAST(uint8_t, identifyingInformationType << 1);
    cdb[11]             = 0;

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Identifying Information", ret);
    }
    return ret;
}

eReturnValues scsi_Set_Priority(tDevice* device,
                                uint8_t  I_T_L_NexusToSet,
                                uint32_t parameterListLength,
                                uint8_t* ptrData,
                                uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Priority\n");
    }

    cdb[OPERATION_CODE] = SET_PRIORITY_CMD;
    cdb[1]              = 0x0E;
    cdb[2]              = C_CAST(uint8_t, (I_T_L_NexusToSet & 0x03) << 6); // only bits 1:0 are valid on this input
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(parameterListLength);
    cdb[7]              = M_Byte2(parameterListLength);
    cdb[8]              = M_Byte1(parameterListLength);
    cdb[9]              = M_Byte0(parameterListLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Priority", ret);
    }
    return ret;
}

eReturnValues scsi_Set_Target_Port_Groups(tDevice* device,
                                          uint32_t parameterListLength,
                                          uint8_t* ptrData,
                                          uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Target Port Groups\n");
    }

    cdb[OPERATION_CODE] = SET_TARGET_PORT_GROUPS_CMD;
    cdb[1]              = 0x0A;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(parameterListLength);
    cdb[7]              = M_Byte2(parameterListLength);
    cdb[8]              = M_Byte1(parameterListLength);
    cdb[9]              = M_Byte0(parameterListLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Target Port Groups", ret);
    }
    return ret;
}

eReturnValues scsi_Set_Timestamp(tDevice* device, uint32_t parameterListLength, uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Timestamp\n");
    }

    cdb[OPERATION_CODE] = SET_TIMESTAMP_CMD;
    cdb[1]              = 0x0F;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = RESERVED;
    cdb[6]              = M_Byte3(parameterListLength);
    cdb[7]              = M_Byte2(parameterListLength);
    cdb[8]              = M_Byte1(parameterListLength);
    cdb[9]              = M_Byte0(parameterListLength);
    cdb[10]             = RESERVED;
    cdb[11]             = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, parameterListLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Timestamp", ret);
    }
    return ret;
}

eReturnValues scsi_Test_Unit_Ready(tDevice* device, scsiStatus* pReturnStatus)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Test Unit Ready\n");
    }

    cdb[OPERATION_CODE] = TEST_UNIT_READY_CMD;
    cdb[1]              = RESERVED;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = RESERVED;
    cdb[5]              = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (pReturnStatus)
    {
        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &pReturnStatus->senseKey,
                                   &pReturnStatus->asc, &pReturnStatus->ascq, &pReturnStatus->fru);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        // leave this here or else the verbose output gets confusing to look at when debugging- this only prints the ret
        // for the function, not the acs/acsq stuff
        print_Return_Enum("Test Unit Ready", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Attribute(tDevice* device,
                                   bool     wtc,
                                   uint32_t restricted,
                                   uint8_t  logicalVolumeNumber,
                                   uint8_t  partitionNumber,
                                   uint32_t parameterListLength,
                                   uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Attribute\n");
    }

    cdb[OPERATION_CODE] = WRITE_ATTRIBUTE_CMD;
    if (wtc)
    {
        cdb[1] |= BIT0;
    }
    cdb[2]  = M_Byte2(restricted);
    cdb[3]  = M_Byte1(restricted);
    cdb[4]  = M_Byte0(restricted);
    cdb[5]  = logicalVolumeNumber;
    cdb[6]  = RESERVED;
    cdb[7]  = partitionNumber;
    cdb[8]  = RESERVED;
    cdb[9]  = RESERVED;
    cdb[10] = M_Byte3(parameterListLength);
    cdb[11] = M_Byte2(parameterListLength);
    cdb[12] = M_Byte1(parameterListLength);
    cdb[13] = M_Byte0(parameterListLength);
    cdb[14] = RESERVED;
    cdb[15] = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, parameterListLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Attribute", ret);
    }
    return ret;
}

eReturnValues scsi_Compare_And_Write(tDevice* device,
                                     uint8_t  wrprotect,
                                     bool     dpo,
                                     bool     fua,
                                     uint64_t logicalBlockAddress,
                                     uint8_t  numberOfLogicalBlocks,
                                     uint8_t  groupNumber,
                                     uint8_t* ptrData,
                                     uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Compare And Write\n");
    }

    cdb[OPERATION_CODE] = COMPARE_AND_WRITE;
    cdb[1]              = C_CAST(uint8_t, C_CAST(uint8_t, (wrprotect & UINT8_C(0x07)) << 5));
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2]  = M_Byte7(logicalBlockAddress);
    cdb[3]  = M_Byte6(logicalBlockAddress);
    cdb[4]  = M_Byte5(logicalBlockAddress);
    cdb[5]  = M_Byte4(logicalBlockAddress);
    cdb[6]  = M_Byte3(logicalBlockAddress);
    cdb[7]  = M_Byte2(logicalBlockAddress);
    cdb[8]  = M_Byte1(logicalBlockAddress);
    cdb[9]  = M_Byte0(logicalBlockAddress);
    cdb[10] = RESERVED;
    cdb[11] = RESERVED;
    cdb[12] = RESERVED;
    cdb[13] = numberOfLogicalBlocks;
    cdb[14] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[15] = 0; // control

    // send the command
    if (numberOfLogicalBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Compare And Write", ret);
    }
    return ret;
}

eReturnValues scsi_Format_Unit(tDevice* device,
                               uint8_t  fmtpInfo,
                               bool     longList,
                               bool     fmtData,
                               bool     cmplst,
                               uint8_t  defectListFormat,
                               uint8_t  vendorSpecific,
                               uint8_t* ptrData,
                               uint32_t dataSize,
                               uint8_t  ffmt,
                               uint32_t timeoutSeconds)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Format Unit\n");
    }

    if (!ptrData && fmtData)
    {
        return BAD_PARAMETER;
    }

    cdb[OPERATION_CODE] = SCSI_FORMAT_UNIT_CMD;
    cdb[1]              = C_CAST(uint8_t, (fmtpInfo & UINT8_C(0x03)) << 6);
    if (longList)
    {
        cdb[1] |= BIT5;
    }
    if (fmtData)
    {
        cdb[1] |= BIT4;
    }
    if (cmplst)
    {
        cdb[1] |= BIT3;
    }
    cdb[1] |= C_CAST(uint8_t, (defectListFormat & UINT8_C(0x07)));
    cdb[2] = vendorSpecific;
    cdb[3] = RESERVED;             // used to be marked obsolete
    cdb[4] = ffmt & UINT8_C(0x03); // used to be marked obsolete
    cdb[5] = 0;                    // control

    // send the command
    if (fmtData)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Format Unit", ret);
    }
    return ret;
}

eReturnValues scsi_Format_With_Preset(tDevice* device,
                                      bool     immed,
                                      bool     fmtmaxlba,
                                      uint32_t presetID,
                                      uint32_t timeoutSeconds)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Format With Preset\n");
    }

    cdb[OPERATION_CODE] = SCSI_FORMAT_WITH_PRESET_CMD;
    if (immed)
    {
        cdb[1] |= BIT7;
    }
    if (fmtmaxlba)
    {
        cdb[1] |= BIT6;
    }
    cdb[2] = M_Byte3(presetID);
    cdb[3] = M_Byte2(presetID);
    cdb[4] = M_Byte1(presetID);
    cdb[5] = M_Byte0(presetID);
    cdb[6] = RESERVED;
    cdb[7] = RESERVED;
    cdb[8] = RESERVED;
    cdb[9] = 0; // control byte

    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Format With Preset", ret);
    }
    return ret;
}

eReturnValues scsi_Get_Lba_Status(tDevice* device,
                                  uint64_t logicalBlockAddress,
                                  uint32_t allocationLength,
                                  uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Get LBA Status\n");
    }

    cdb[OPERATION_CODE] = GET_LBA_STATUS;
    cdb[1]              = 0x12;
    cdb[2]              = M_Byte7(logicalBlockAddress);
    cdb[3]              = M_Byte6(logicalBlockAddress);
    cdb[4]              = M_Byte5(logicalBlockAddress);
    cdb[5]              = M_Byte4(logicalBlockAddress);
    cdb[6]              = M_Byte3(logicalBlockAddress);
    cdb[7]              = M_Byte2(logicalBlockAddress);
    cdb[8]              = M_Byte1(logicalBlockAddress);
    cdb[9]              = M_Byte0(logicalBlockAddress);
    cdb[10]             = M_Byte3(allocationLength);
    cdb[11]             = M_Byte2(allocationLength);
    cdb[12]             = M_Byte1(allocationLength);
    cdb[13]             = M_Byte0(allocationLength);
    cdb[14]             = RESERVED;
    cdb[15]             = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get LBA Status", ret);
    }
    return ret;
}

eReturnValues scsi_Orwrite_16(tDevice* device,
                              uint8_t  orProtect,
                              bool     dpo,
                              bool     fua,
                              uint64_t logicalBlockAddress,
                              uint32_t transferLengthBlocks,
                              uint8_t  groupNumber,
                              uint8_t* ptrData,
                              uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI ORWrite 16\n");
    }

    cdb[OPERATION_CODE] = ORWRITE_16;
    cdb[1]              = C_CAST(uint8_t, (orProtect & 0x07) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2]  = M_Byte7(logicalBlockAddress);
    cdb[3]  = M_Byte6(logicalBlockAddress);
    cdb[4]  = M_Byte5(logicalBlockAddress);
    cdb[5]  = M_Byte4(logicalBlockAddress);
    cdb[6]  = M_Byte3(logicalBlockAddress);
    cdb[7]  = M_Byte2(logicalBlockAddress);
    cdb[8]  = M_Byte1(logicalBlockAddress);
    cdb[9]  = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(transferLengthBlocks);
    cdb[11] = M_Byte2(transferLengthBlocks);
    cdb[12] = M_Byte1(transferLengthBlocks);
    cdb[13] = M_Byte0(transferLengthBlocks);
    cdb[14] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[15] = 0; // control

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("ORWrite 16", ret);
    }
    return ret;
}

eReturnValues scsi_Orwrite_32(tDevice* device,
                              uint8_t  bmop,
                              uint8_t  previousGenProcessing,
                              uint8_t  groupNumber,
                              uint8_t  orProtect,
                              bool     dpo,
                              bool     fua,
                              uint64_t logicalBlockAddress,
                              uint32_t expectedORWgen,
                              uint32_t newORWgen,
                              uint32_t transferLengthBlocks,
                              uint8_t* ptrData,
                              uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI ORWrite 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, ORWRITE_32, 0x000E, logicalBlockAddress, transferLengthBlocks, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, orProtect, 0, 0, 0);

    cdb[2] = bmop & 0x07;
    cdb[3] = previousGenProcessing & 0x0F;
    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    if (fua)
    {
        cdb[10] |= BIT3;
    }
    cdb[11] = RESERVED;
    cdb[20] = M_Byte3(expectedORWgen);
    cdb[21] = M_Byte2(expectedORWgen);
    cdb[22] = M_Byte1(expectedORWgen);
    cdb[23] = M_Byte0(expectedORWgen);
    cdb[24] = M_Byte3(newORWgen);
    cdb[25] = M_Byte2(newORWgen);
    cdb[26] = M_Byte1(newORWgen);
    cdb[27] = M_Byte0(newORWgen);

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("ORWrite 32", ret);
    }
    return ret;
}

eReturnValues scsi_Prefetch_10(tDevice* device,
                               bool     immediate,
                               uint32_t logicalBlockAddress,
                               uint8_t  groupNumber,
                               uint16_t prefetchLength)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Pre-Fetch 10\n");
    }

    cdb[OPERATION_CODE] = PRE_FETCH_10;
    if (immediate)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[7] = M_Byte1(prefetchLength);
    cdb[8] = M_Byte0(prefetchLength);
    cdb[9] = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Pre-Fetch 10", ret);
    }
    return ret;
}

eReturnValues scsi_Prefetch_16(tDevice* device,
                               bool     immediate,
                               uint64_t logicalBlockAddress,
                               uint8_t  groupNumber,
                               uint32_t prefetchLength)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Pre-Fetch 16\n");
    }

    cdb[OPERATION_CODE] = PRE_FETCH_16;
    if (immediate)
    {
        cdb[1] |= BIT1;
    }
    cdb[2]  = M_Byte7(logicalBlockAddress);
    cdb[3]  = M_Byte6(logicalBlockAddress);
    cdb[4]  = M_Byte5(logicalBlockAddress);
    cdb[5]  = M_Byte4(logicalBlockAddress);
    cdb[6]  = M_Byte3(logicalBlockAddress);
    cdb[7]  = M_Byte2(logicalBlockAddress);
    cdb[8]  = M_Byte1(logicalBlockAddress);
    cdb[9]  = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(prefetchLength);
    cdb[11] = M_Byte2(prefetchLength);
    cdb[12] = M_Byte1(prefetchLength);
    cdb[13] = M_Byte0(prefetchLength);
    cdb[14] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[15] = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Pre-Fetch 16", ret);
    }
    return ret;
}

eReturnValues scsi_Prevent_Allow_Medium_Removal(tDevice* device, uint8_t prevent)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Prevent Allow Medium Removal\n");
    }

    cdb[OPERATION_CODE] = PREVENT_ALLOW_MEDIUM_REMOVAL;
    cdb[1]              = RESERVED;
    cdb[2]              = RESERVED;
    cdb[3]              = RESERVED;
    cdb[4]              = prevent & 0x03;
    cdb[5]              = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Prevent Allow Medium Removal", ret);
    }
    return ret;
}

eReturnValues scsi_Read_6(tDevice* device,
                          uint32_t logicalBlockAddress,
                          uint8_t  transferLengthBlocks,
                          uint8_t* ptrData,
                          uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR && transferLengthBlocks == 0)
    {
        // In read 6, transferlengthBlocks is zero, then we are reading 256 sectors of data, so we need to say this is a
        // bad parameter combination!!!
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 6\n");
    }

    set_Typical_SCSI_6B_CDB_Fields(cdb, READ6, logicalBlockAddress, transferLengthBlocks, 0);

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_IN,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 6", ret);
    }
    return ret;
}

eReturnValues scsi_Read_10(tDevice* device,
                           uint8_t  rdProtect,
                           bool     dpo,
                           bool     fua,
                           bool     rarc,
                           uint32_t logicalBlockAddress,
                           uint8_t  groupNumber,
                           uint16_t transferLengthBlocks,
                           uint8_t* ptrData,
                           uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 10\n");
    }

    cdb[OPERATION_CODE] = READ10;
    cdb[1]              = C_CAST(uint8_t, (rdProtect & 0x07) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    if (rarc)
    {
        cdb[1] |= BIT2;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[7] = M_Byte1(transferLengthBlocks);
    cdb[8] = M_Byte0(transferLengthBlocks);
    cdb[9] = 0; // control

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 10", ret);
    }
    return ret;
}

eReturnValues scsi_Read_12(tDevice* device,
                           uint8_t  rdProtect,
                           bool     dpo,
                           bool     fua,
                           bool     rarc,
                           uint32_t logicalBlockAddress,
                           uint8_t  groupNumber,
                           uint32_t transferLengthBlocks,
                           uint8_t* ptrData,
                           uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 12\n");
    }

    cdb[OPERATION_CODE] = READ12;
    cdb[1]              = C_CAST(uint8_t, (rdProtect & 0x07) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    if (rarc)
    {
        cdb[1] |= BIT2;
    }
    cdb[2]  = M_Byte3(logicalBlockAddress);
    cdb[3]  = M_Byte2(logicalBlockAddress);
    cdb[4]  = M_Byte1(logicalBlockAddress);
    cdb[5]  = M_Byte0(logicalBlockAddress);
    cdb[6]  = M_Byte3(transferLengthBlocks);
    cdb[7]  = M_Byte2(transferLengthBlocks);
    cdb[8]  = M_Byte1(transferLengthBlocks);
    cdb[9]  = M_Byte0(transferLengthBlocks);
    cdb[10] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[11] = 0; // control

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 12", ret);
    }
    return ret;
}

eReturnValues scsi_Read_16(tDevice* device,
                           uint8_t  rdProtect,
                           bool     dpo,
                           bool     fua,
                           bool     rarc,
                           uint64_t logicalBlockAddress,
                           uint8_t  groupNumber,
                           uint32_t transferLengthBlocks,
                           uint8_t* ptrData,
                           uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 16\n");
    }

    cdb[OPERATION_CODE] = READ16;
    cdb[1]              = C_CAST(uint8_t, (rdProtect & 0x07) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    if (rarc)
    {
        cdb[1] |= BIT2;
    }
    cdb[2]  = M_Byte7(logicalBlockAddress);
    cdb[3]  = M_Byte6(logicalBlockAddress);
    cdb[4]  = M_Byte5(logicalBlockAddress);
    cdb[5]  = M_Byte4(logicalBlockAddress);
    cdb[6]  = M_Byte3(logicalBlockAddress);
    cdb[7]  = M_Byte2(logicalBlockAddress);
    cdb[8]  = M_Byte1(logicalBlockAddress);
    cdb[9]  = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(transferLengthBlocks);
    cdb[11] = M_Byte2(transferLengthBlocks);
    cdb[12] = M_Byte1(transferLengthBlocks);
    cdb[13] = M_Byte0(transferLengthBlocks);
    cdb[14] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[15] = 0; // control

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 16", ret);
    }
    return ret;
}

eReturnValues scsi_Read_32(tDevice* device,
                           uint8_t  rdProtect,
                           bool     dpo,
                           bool     fua,
                           bool     rarc,
                           uint64_t logicalBlockAddress,
                           uint8_t  groupNumber,
                           uint32_t transferLengthBlocks,
                           uint8_t* ptrData,
                           uint32_t expectedInitialLogicalBlockRefTag,
                           uint16_t expectedLogicalBlockAppTag,
                           uint16_t logicalBlockAppTagMask,
                           uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, READ32, 0x0009, logicalBlockAddress, transferLengthBlocks, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, rdProtect, expectedInitialLogicalBlockRefTag, expectedLogicalBlockAppTag,
                           logicalBlockAppTagMask);

    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    if (fua)
    {
        cdb[10] |= BIT3;
    }
    if (rarc)
    {
        cdb[10] |= BIT2;
    }

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 32", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Defect_Data_10(tDevice* device,
                                       bool     requestPList,
                                       bool     requestGList,
                                       uint8_t  defectListFormat,
                                       uint16_t allocationLength,
                                       uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Defect Data 10\n");
    }

    cdb[OPERATION_CODE] = READ_DEFECT_DATA_10_CMD;
    cdb[1]              = RESERVED;
    if (requestPList)
    {
        cdb[2] |= BIT4;
    }
    if (requestGList)
    {
        cdb[2] |= BIT3;
    }
    cdb[2] |= C_CAST(uint8_t, defectListFormat & UINT8_C(0x07));
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    cdb[9] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Defect Data 10", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Defect_Data_12(tDevice* device,
                                       bool     requestPList,
                                       bool     requestGList,
                                       uint8_t  defectListFormat,
                                       uint32_t addressDescriptorIndex,
                                       uint32_t allocationLength,
                                       uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Defect Data 12\n");
    }

    cdb[OPERATION_CODE] = READ_DEFECT_DATA_12_CMD;
    if (requestPList)
    {
        cdb[1] |= BIT4;
    }
    if (requestGList)
    {
        cdb[1] |= BIT3;
    }
    cdb[1] |= C_CAST(uint8_t, defectListFormat & UINT8_C(0x07));
    cdb[2]  = M_Byte3(addressDescriptorIndex);
    cdb[3]  = M_Byte2(addressDescriptorIndex);
    cdb[4]  = M_Byte1(addressDescriptorIndex);
    cdb[5]  = M_Byte0(addressDescriptorIndex);
    cdb[6]  = M_Byte3(allocationLength);
    cdb[7]  = M_Byte2(allocationLength);
    cdb[8]  = M_Byte1(allocationLength);
    cdb[9]  = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Defect Data 12", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Long_10(tDevice* device,
                                bool     physicalBlock,
                                bool     correctBit,
                                uint32_t logicalBlockAddress,
                                uint16_t byteTransferLength,
                                uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Long 10\n");
    }

    cdb[OPERATION_CODE] = READ_LONG_10;
    if (physicalBlock)
    {
        cdb[1] |= BIT2;
    }
    if (correctBit)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(byteTransferLength);
    cdb[8] = M_Byte0(byteTransferLength);
    cdb[9] = 0; // control

    // send the command
    if (byteTransferLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, byteTransferLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Long 10", ret);
    }
    return ret;
}

eReturnValues scsi_Read_Long_16(tDevice* device,
                                bool     physicalBlock,
                                bool     correctBit,
                                uint64_t logicalBlockAddress,
                                uint16_t byteTransferLength,
                                uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Long 16\n");
    }

    cdb[OPERATION_CODE] = READ_LONG_16;
    cdb[1]              = 0x11; // service action
    cdb[2]              = M_Byte7(logicalBlockAddress);
    cdb[3]              = M_Byte6(logicalBlockAddress);
    cdb[4]              = M_Byte5(logicalBlockAddress);
    cdb[5]              = M_Byte4(logicalBlockAddress);
    cdb[6]              = M_Byte3(logicalBlockAddress);
    cdb[7]              = M_Byte2(logicalBlockAddress);
    cdb[8]              = M_Byte1(logicalBlockAddress);
    cdb[9]              = M_Byte0(logicalBlockAddress);
    cdb[10]             = RESERVED;
    cdb[11]             = RESERVED;
    cdb[12]             = M_Byte1(byteTransferLength);
    cdb[13]             = M_Byte0(byteTransferLength);
    if (physicalBlock)
    {
        cdb[14] |= BIT1;
    }
    if (correctBit)
    {
        cdb[14] |= BIT0;
    }
    cdb[15] = 0; // control

    // send the command
    if (byteTransferLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, byteTransferLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Long 16", ret);
    }
    return ret;
}

eReturnValues scsi_Reassign_Blocks(tDevice* device, bool longLBA, bool longList, uint32_t dataSize, uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Reassign Blocks\n");
    }

    cdb[OPERATION_CODE] = REASSIGN_BLOCKS_6;
    if (longLBA)
    {
        cdb[1] |= BIT1;
    }
    if (longList)
    {
        cdb[1] |= BIT0;
    }
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Reassign Blocks", ret);
    }
    return ret;
}

eReturnValues scsi_Report_Referrals(tDevice* device,
                                    uint64_t logicalBlockAddress,
                                    uint32_t allocationLength,
                                    bool     one_seg,
                                    uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Referrals\n");
    }

    cdb[OPERATION_CODE] = REPORT_REFERRALS;
    cdb[1]              = 0x13; // service action
    cdb[2]              = M_Byte7(logicalBlockAddress);
    cdb[3]              = M_Byte6(logicalBlockAddress);
    cdb[4]              = M_Byte5(logicalBlockAddress);
    cdb[5]              = M_Byte4(logicalBlockAddress);
    cdb[6]              = M_Byte3(logicalBlockAddress);
    cdb[7]              = M_Byte2(logicalBlockAddress);
    cdb[8]              = M_Byte1(logicalBlockAddress);
    cdb[9]              = M_Byte0(logicalBlockAddress);
    cdb[10]             = M_Byte3(allocationLength);
    cdb[11]             = M_Byte2(allocationLength);
    cdb[12]             = M_Byte1(allocationLength);
    cdb[13]             = M_Byte0(allocationLength);
    if (one_seg)
    {
        cdb[14] |= BIT0;
    }
    cdb[15] = 0; // control

    // send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Referrals", ret);
    }
    return ret;
}

eReturnValues scsi_Start_Stop_Unit(tDevice* device,
                                   bool     immediate,
                                   uint8_t  powerConditionModifier,
                                   uint8_t  powerCondition,
                                   bool     noFlush,
                                   bool     loej,
                                   bool     start)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Start Stop Unit\n");
    }

    cdb[OPERATION_CODE] = START_STOP_UNIT_CMD;
    if (immediate)
    {
        cdb[1] |= BIT0;
    }
    cdb[2] = RESERVED;
    cdb[3] |= C_CAST(uint8_t, powerConditionModifier & UINT8_C(0x0F));
    cdb[4] |= C_CAST(uint8_t, (powerCondition & UINT8_C(0x0F)) << 4);
    if (noFlush)
    {
        cdb[4] |= BIT2;
    }
    if (loej)
    {
        cdb[4] |= BIT1;
    }
    if (start)
    {
        cdb[4] |= BIT0;
    }
    cdb[5] = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 30);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Start Stop Unit", ret);
    }
    return ret;
}

eReturnValues scsi_Synchronize_Cache_10(tDevice* device,
                                        bool     immediate,
                                        uint32_t logicalBlockAddress,
                                        uint8_t  groupNumber,
                                        uint16_t numberOfLogicalBlocks)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Synchronize Cache 10\n");
    }

    cdb[OPERATION_CODE] = SYNCHRONIZE_CACHE_10;
    if (immediate)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] |= C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[7] = M_Byte1(numberOfLogicalBlocks);
    cdb[8] = M_Byte0(numberOfLogicalBlocks);
    cdb[9] = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Synchronize Cache 10", ret);
    }
    return ret;
}

eReturnValues scsi_Synchronize_Cache_16(tDevice* device,
                                        bool     immediate,
                                        uint64_t logicalBlockAddress,
                                        uint8_t  groupNumber,
                                        uint32_t numberOfLogicalBlocks)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Synchronize Cache 16\n");
    }

    cdb[OPERATION_CODE] = SYNCHRONIZE_CACHE_16_CMD;
    if (immediate)
    {
        cdb[1] |= BIT1;
    }
    cdb[2]  = M_Byte7(logicalBlockAddress);
    cdb[3]  = M_Byte6(logicalBlockAddress);
    cdb[4]  = M_Byte5(logicalBlockAddress);
    cdb[5]  = M_Byte4(logicalBlockAddress);
    cdb[6]  = M_Byte3(logicalBlockAddress);
    cdb[7]  = M_Byte2(logicalBlockAddress);
    cdb[8]  = M_Byte1(logicalBlockAddress);
    cdb[9]  = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(numberOfLogicalBlocks);
    cdb[11] = M_Byte2(numberOfLogicalBlocks);
    cdb[12] = M_Byte1(numberOfLogicalBlocks);
    cdb[13] = M_Byte0(numberOfLogicalBlocks);
    cdb[14] |= C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[15] = 0; // control

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Synchronize Cache 16", ret);
    }
    return ret;
}

eReturnValues scsi_Unmap(tDevice* device,
                         bool     anchor,
                         uint8_t  groupNumber,
                         uint16_t parameterListLength,
                         uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Unmap\n");
    }

    cdb[OPERATION_CODE] = UNMAP_CMD;
    if (anchor)
    {
        cdb[1] |= BIT0;
    }
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] |= C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    cdb[9] = 0; // control

    // send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, parameterListLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Unmap", ret);
    }
    return ret;
}

eReturnValues scsi_Verify_10(tDevice* device,
                             uint8_t  vrprotect,
                             bool     dpo,
                             uint8_t  byteCheck,
                             uint32_t logicalBlockAddress,
                             uint8_t  groupNumber,
                             uint16_t verificationLength,
                             uint8_t* ptrData,
                             uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 10\n");
    }

    cdb[OPERATION_CODE] = VERIFY10;
    cdb[1] |= C_CAST(uint8_t, (vrprotect & UINT8_C(0x07)) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] |= C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[7] = M_Byte1(verificationLength);
    cdb[8] = M_Byte0(verificationLength);
    cdb[9] = 0; // control

    // if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should
    // be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 10", ret);
    }
    return ret;
}

eReturnValues scsi_Verify_12(tDevice* device,
                             uint8_t  vrprotect,
                             bool     dpo,
                             uint8_t  byteCheck,
                             uint32_t logicalBlockAddress,
                             uint8_t  groupNumber,
                             uint32_t verificationLength,
                             uint8_t* ptrData,
                             uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 12\n");
    }

    cdb[OPERATION_CODE] = VERIFY12;
    cdb[1] |= C_CAST(uint8_t, (vrprotect & UINT8_C(0x07)) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = M_Byte3(verificationLength);
    cdb[7] = M_Byte2(verificationLength);
    cdb[8] = M_Byte1(verificationLength);
    cdb[9] = M_Byte0(verificationLength);
    cdb[10] |= C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[11] = 0; // control

    // if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should
    // be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 12", ret);
    }
    return ret;
}

eReturnValues scsi_Verify_16(tDevice* device,
                             uint8_t  vrprotect,
                             bool     dpo,
                             uint8_t  byteCheck,
                             uint64_t logicalBlockAddress,
                             uint8_t  groupNumber,
                             uint32_t verificationLength,
                             uint8_t* ptrData,
                             uint32_t dataSize)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 16\n");
    }

    cdb[OPERATION_CODE] = VERIFY16;
    cdb[1] |= C_CAST(uint8_t, (vrprotect & UINT8_C(0x07)) << 5);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[2]  = M_Byte7(logicalBlockAddress);
    cdb[3]  = M_Byte6(logicalBlockAddress);
    cdb[4]  = M_Byte5(logicalBlockAddress);
    cdb[5]  = M_Byte4(logicalBlockAddress);
    cdb[6]  = M_Byte3(logicalBlockAddress);
    cdb[7]  = M_Byte2(logicalBlockAddress);
    cdb[8]  = M_Byte1(logicalBlockAddress);
    cdb[9]  = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(verificationLength);
    cdb[11] = M_Byte2(verificationLength);
    cdb[12] = M_Byte1(verificationLength);
    cdb[13] = M_Byte0(verificationLength);
    cdb[14] |= C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    cdb[15] = 0; // control

    // if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should
    // be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 16", ret);
    }
    return ret;
}

eReturnValues scsi_Verify_32(tDevice* device,
                             uint8_t  vrprotect,
                             bool     dpo,
                             uint8_t  byteCheck,
                             uint64_t logicalBlockAddress,
                             uint8_t  groupNumber,
                             uint32_t verificationLength,
                             uint8_t* ptrData,
                             uint32_t dataSize,
                             uint32_t expectedInitialLogicalBlockRefTag,
                             uint16_t expectedLogicalBlockAppTag,
                             uint16_t logicalBlockAppTagMask)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, VERIFY32, 0x000A, logicalBlockAddress, verificationLength, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, vrprotect, expectedInitialLogicalBlockRefTag, expectedLogicalBlockAppTag,
                           logicalBlockAppTagMask);

    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    cdb[10] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[11] = RESERVED;

    // if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should
    // be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, dataSize, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 32", ret);
    }
    return ret;
}

eReturnValues scsi_Write_6(tDevice* device,
                           uint32_t logicalBlockAddress,
                           uint8_t  transferLengthBlocks,
                           uint8_t* ptrData,
                           uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    DISABLE_NONNULL_COMPARE
    if (ptrData == M_NULLPTR && transferLengthBlocks == 0)
    {
        // In write 6, transferlengthBlocks is zero, then we are reading 256 sectors of data, so we need to say this is
        // a bad parameter combination!!!
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write 6\n");
    }

    set_Typical_SCSI_6B_CDB_Fields(cdb, WRITE6, logicalBlockAddress, transferLengthBlocks, 0);

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 6", ret);
    }
    return ret;
}

eReturnValues scsi_Write_10(tDevice* device,
                            uint8_t  wrprotect,
                            bool     dpo,
                            bool     fua,
                            uint32_t logicalBlockAddress,
                            uint8_t  groupNumber,
                            uint16_t transferLengthBlocks,
                            uint8_t* ptrData,
                            uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write 10\n");
    }

    set_Typical_SCSI_10B_CDB_Fields(cdb, WRITE10, NO_SERVICE_ACTION, logicalBlockAddress, transferLengthBlocks, 0);

    cdb[1] = GET_PROTECT_VAL(wrprotect);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[6] = GET_GROUP_CODE(groupNumber);

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 10", ret);
    }
    return ret;
}

eReturnValues scsi_Write_12(tDevice* device,
                            uint8_t  wrprotect,
                            bool     dpo,
                            bool     fua,
                            uint32_t logicalBlockAddress,
                            uint8_t  groupNumber,
                            uint32_t transferLengthBlocks,
                            uint8_t* ptrData,
                            uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write 12\n");
    }

    set_Typical_SCSI_12B_CDB_Fields(cdb, WRITE12, NO_SERVICE_ACTION, logicalBlockAddress, transferLengthBlocks, 0);

    cdb[1] = GET_PROTECT_VAL(wrprotect);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[10] = GET_GROUP_CODE(groupNumber);

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 12", ret);
    }
    return ret;
}

eReturnValues scsi_Write_16(tDevice* device,
                            uint8_t  wrprotect,
                            bool     dpo,
                            bool     fua,
                            uint64_t logicalBlockAddress,
                            uint8_t  groupNumber,
                            uint32_t transferLengthBlocks,
                            uint8_t* ptrData,
                            uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write 16\n");
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, WRITE16, NO_SERVICE_ACTION, logicalBlockAddress,
                                              transferLengthBlocks, 0);

    cdb[1] = GET_PROTECT_VAL(wrprotect);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[14] = GET_GROUP_CODE(groupNumber);

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 16", ret);
    }
    return ret;
}

eReturnValues scsi_Write_32(tDevice* device,
                            uint8_t  wrprotect,
                            bool     dpo,
                            bool     fua,
                            uint64_t logicalBlockAddress,
                            uint8_t  groupNumber,
                            uint32_t transferLengthBlocks,
                            uint8_t* ptrData,
                            uint32_t expectedInitialLogicalBlockRefTag,
                            uint16_t expectedLogicalBlockAppTag,
                            uint16_t logicalBlockAppTagMask,
                            uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Write 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, WRITE32, 0x000B, logicalBlockAddress, transferLengthBlocks, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, wrprotect, expectedInitialLogicalBlockRefTag, expectedLogicalBlockAppTag,
                           logicalBlockAppTagMask);

    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    if (fua)
    {
        cdb[10] |= BIT3;
    }
    cdb[11] = RESERVED;

    // send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 32", ret);
    }
    return ret;
}

eReturnValues scsi_Write_And_Verify_10(tDevice* device,
                                       uint8_t  wrprotect,
                                       bool     dpo,
                                       uint8_t  byteCheck,
                                       uint32_t logicalBlockAddress,
                                       uint8_t  groupNumber,
                                       uint16_t transferLengthBlocks,
                                       uint8_t* ptrData,
                                       uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write and Verify 10\n");
    }

    set_Typical_SCSI_10B_CDB_Fields(cdb, WRITE_AND_VERIFY_10, NO_SERVICE_ACTION, logicalBlockAddress,
                                    transferLengthBlocks, 0);

    cdb[1] = GET_PROTECT_VAL(wrprotect);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[6] = GET_GROUP_CODE(groupNumber);

    // send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 10", ret);
    }
    return ret;
}

eReturnValues scsi_Write_And_Verify_12(tDevice* device,
                                       uint8_t  wrprotect,
                                       bool     dpo,
                                       uint8_t  byteCheck,
                                       uint32_t logicalBlockAddress,
                                       uint8_t  groupNumber,
                                       uint32_t transferLengthBlocks,
                                       uint8_t* ptrData,
                                       uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write and Verify 12\n");
    }

    set_Typical_SCSI_12B_CDB_Fields(cdb, WRITE_AND_VERIFY_12, NO_SERVICE_ACTION, logicalBlockAddress,
                                    transferLengthBlocks, 0);

    cdb[1] = GET_PROTECT_VAL(wrprotect);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[10] = GET_GROUP_CODE(groupNumber);

    // send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 12", ret);
    }
    return ret;
}

eReturnValues scsi_Write_And_Verify_16(tDevice* device,
                                       uint8_t  wrprotect,
                                       bool     dpo,
                                       uint8_t  byteCheck,
                                       uint64_t logicalBlockAddress,
                                       uint8_t  groupNumber,
                                       uint32_t transferLengthBlocks,
                                       uint8_t* ptrData,
                                       uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write and Verify 16\n");
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, WRITE_AND_VERIFY_16, NO_SERVICE_ACTION, logicalBlockAddress,
                                              transferLengthBlocks, 0);

    cdb[1] = GET_PROTECT_VAL(wrprotect);
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[14] |= GET_GROUP_CODE(groupNumber);

    // send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 16", ret);
    }
    return ret;
}

eReturnValues scsi_Write_And_Verify_32(tDevice* device,
                                       uint8_t  wrprotect,
                                       bool     dpo,
                                       uint8_t  byteCheck,
                                       uint64_t logicalBlockAddress,
                                       uint8_t  groupNumber,
                                       uint32_t transferLengthBlocks,
                                       uint8_t* ptrData,
                                       uint32_t expectedInitialLogicalBlockRefTag,
                                       uint16_t expectedLogicalBlockAppTag,
                                       uint16_t logicalBlockAppTagMask,
                                       uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Write and Verify 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, WRITE_AND_VERIFY_32, 0x000C, logicalBlockAddress, transferLengthBlocks, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, wrprotect, expectedInitialLogicalBlockRefTag, expectedLogicalBlockAppTag,
                           logicalBlockAppTagMask);

    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    cdb[10] |= C_CAST(uint8_t, (byteCheck & UINT8_C(0x03)) << 1);
    cdb[11] = RESERVED;

    // send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 32", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Long_10(tDevice* device,
                                 bool     correctionDisabled,
                                 bool     writeUncorrectable,
                                 bool     physicalBlock,
                                 uint32_t logicalBlockAddress,
                                 uint16_t byteTransferLength,
                                 uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Long 10\n");
    }

    set_Typical_SCSI_10B_CDB_Fields(cdb, WRITE_LONG_10_CMD, NO_SERVICE_ACTION, logicalBlockAddress, byteTransferLength,
                                    0);

    if (correctionDisabled)
    {
        cdb[1] |= BIT7;
    }
    if (writeUncorrectable)
    {
        cdb[1] |= BIT6;
    }
    if (physicalBlock)
    {
        cdb[1] |= BIT5;
    }
    cdb[6] = RESERVED;

    // send the command
    if (ptrData)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, byteTransferLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Long 10", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Long_16(tDevice* device,
                                 bool     correctionDisabled,
                                 bool     writeUncorrectable,
                                 bool     physicalBlock,
                                 uint64_t logicalBlockAddress,
                                 uint16_t byteTransferLength,
                                 uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Long 16\n");
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, WRITE_LONG_16_CMD, 0x11, logicalBlockAddress, byteTransferLength, 0);

    if (correctionDisabled)
    {
        cdb[1] |= BIT7;
    }
    if (writeUncorrectable)
    {
        cdb[1] |= BIT6;
    }
    if (physicalBlock)
    {
        cdb[1] |= BIT5;
    }
    cdb[14] = RESERVED;

    // send the command
    if (ptrData)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, byteTransferLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Long 16", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Same_10(tDevice* device,
                                 uint8_t  wrprotect,
                                 bool     anchor,
                                 bool     unmap,
                                 uint32_t logicalBlockAddress,
                                 uint8_t  groupNumber,
                                 uint16_t numberOfLogicalBlocks,
                                 uint8_t* ptrData,
                                 uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    uint32_t timeout = UINT32_C(0);
    if (os_Is_Infinite_Timeout_Supported())
    {
        timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    if (ptrData == M_NULLPTR) // write Same 10 requires a data transfer
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Same 10\n");
    }

    set_Typical_SCSI_10B_CDB_Fields(cdb, WRITE_SAME_10_CMD, NO_SERVICE_ACTION, logicalBlockAddress,
                                    numberOfLogicalBlocks, 0);
    cdb[1] |= GET_PROTECT_VAL(wrprotect);
    if (anchor)
    {
        cdb[1] |= BIT4;
    }
    if (unmap)
    {
        cdb[1] |= BIT3;
    }
    cdb[6] = GET_GROUP_CODE(groupNumber);

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same 10", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Same_16(tDevice* device,
                                 uint8_t  wrprotect,
                                 bool     anchor,
                                 bool     unmap,
                                 bool     noDataOut,
                                 uint64_t logicalBlockAddress,
                                 uint8_t  groupNumber,
                                 uint32_t numberOfLogicalBlocks,
                                 uint8_t* ptrData,
                                 uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    uint32_t timeout = UINT32_C(0);
    if (os_Is_Infinite_Timeout_Supported())
    {
        timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    if (ptrData == M_NULLPTR && !noDataOut)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Same 16\n");
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, WRITE_SAME_16_CMD, NO_SERVICE_ACTION, logicalBlockAddress,
                                              numberOfLogicalBlocks, 0);

    cdb[1] |= GET_PROTECT_VAL(wrprotect);
    if (anchor)
    {
        cdb[1] |= BIT4;
    }
    if (unmap)
    {
        cdb[1] |= BIT3;
    }
    if (noDataOut)
    {
        cdb[1] |= BIT0;
    }
    cdb[14] = GET_GROUP_CODE(groupNumber);

    if (noDataOut)
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    else
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same 16", ret);
    }
    return ret;
}

eReturnValues scsi_Write_Same_32(tDevice* device,
                                 uint8_t  wrprotect,
                                 bool     anchor,
                                 bool     unmap,
                                 bool     noDataOut,
                                 uint64_t logicalBlockAddress,
                                 uint8_t  groupNumber,
                                 uint32_t numberOfLogicalBlocks,
                                 uint8_t* ptrData,
                                 uint32_t expectedInitialLogicalBlockRefTag,
                                 uint16_t expectedLogicalBlockAppTag,
                                 uint16_t logicalBlockAppTagMask,
                                 uint32_t transferLengthBytes)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    uint32_t timeout = UINT32_C(0);
    if (os_Is_Infinite_Timeout_Supported())
    {
        timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    if (ptrData == M_NULLPTR && !noDataOut)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Same 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, WRITE_SAME_32_CMD, 0x000D, logicalBlockAddress, numberOfLogicalBlocks, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, wrprotect, expectedInitialLogicalBlockRefTag, expectedLogicalBlockAppTag,
                           logicalBlockAppTagMask);

    if (anchor)
    {
        cdb[10] |= BIT4;
    }
    if (unmap)
    {
        cdb[10] |= BIT3;
    }
    if (noDataOut)
    {
        cdb[10] |= BIT0;
    }
    cdb[11] = RESERVED;

    if (!noDataOut)
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    else
    {
        // send the command
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same 32", ret);
    }
    return ret;
}

//-----------------------------------------------------------------------------
//
//  scsi_xd_write_read_10()
//
//! \brief   Description:  Send a SCSI XD Write Read 10 command
//
//  Entry:
//!   \param tDevice - pointer to the device structure
//!   \param wrprotect - the wrprotect field. Only bits2:0 are valid
//!   \param dpo - set the dpo bit
//!   \param fua - set the fua bit
//!   \param disableWrite - set the disable write bit
//!   \param xoprinfo - set the xorpinfo bit
//!   \param logicalBlockAddress - LBA
//!   \param groupNumber - the groupNumber field. only bits 4:0 are valid
//!   \param transferLength - the length of the data to read/write/transfer. Buffers must be this big
//!   \param ptrDataOut - pointer to the data out buffer. Must be non-M_NULLPTR
//!   \param ptrDataIn - pointer to the data in buffer. Must be non-M_NULLPTR
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
// eReturnValues scsi_xd_Write_Read_10(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, bool disableWrite, bool
// xoprinfo, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t transferLength, uint8_t *ptrDataOut, uint8_t
// *ptrDataIn)
//{
//    eReturnValues ret = FAILURE;
//    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
//    ScsiIoCtx scsiIoCtx;
//    safe_memset(&scsiIoCtx, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
//
//    if (ptrDataOut == M_NULLPTR || ptrDataIn == M_NULLPTR)
//    {
//        return BAD_PARAMETER;
//    }
//
//    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
//    {
//        printf("Sending SCSI XD Write Read 10\n");
//    }
//
//    cdb[OPERATION_CODE] = XDWRITEREAD_10;
//    cdb[1] |= C_CAST(uint8_t, (wrprotect & UINT8_C(0x07)) << 5);
//    if (dpo == true)
//    {
//        cdb[1] |= BIT4;
//    }
//    if (fua == true)
//    {
//        cdb[1] |= BIT3;
//    }
//    if (disableWrite == true)
//    {
//        cdb[1] |= BIT2;
//    }
//    if (xoprinfo == true)
//    {
//        cdb[1] |= BIT0;
//    }
//    cdb[2] = C_CAST(uint8_t, logicalBlockAddress >> 24);
//    cdb[3] = C_CAST(uint8_t, logicalBlockAddress >> 16);
//    cdb[4] = C_CAST(uint8_t, logicalBlockAddress >> 8);
//    cdb[5] = C_CAST(uint8_t, logicalBlockAddress);
//    cdb[6] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
//    cdb[7] = C_CAST(uint8_t, transferLength >> 8);
//    cdb[8] = C_CAST(uint8_t, transferLength);
//    cdb[9] = 0;//control
//
//    // Set up the CTX
//    scsiIoCtx.device = device;
//    safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0, SPC3_SENSE_LEN);
//    scsiIoCtx.psense = device->drive_info.lastCommandSenseData;
//    scsiIoCtx.senseDataSize = SPC3_SENSE_LEN;
//    safe_memcpy(&scsiIoCtx.cdb[0], SCSI_IO_CTX_MAX_CDB_LEN, &cdb[0], CDB_LEN_10);
//    scsiIoCtx.cdbLength = CDB_LEN_10;
//    scsiIoCtx.direction = XFER_DATA_OUT_IN;
//    //set the buffer info in the bidirectional command structure
//    scsiIoCtx.biDirectionalBuffers.dataInBuffer = ptrDataIn;
//    scsiIoCtx.biDirectionalBuffers.dataInBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.biDirectionalBuffers.dataOutBuffer = ptrDataOut;
//    scsiIoCtx.biDirectionalBuffers.dataOutBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.verbose = 0;
//
//    //while this command is all typed up the lower level windows or linux passthrough code needs some work before this
//    command is actually ready to be used return NOT_SUPPORTED; ret = send_IO(&scsiIoCtx);
//    get_Sense_Key_ACQ_ACSQ(device->drive_info.lastCommandSenseData, &scsiIoCtx.returnStatus.senseKey,
//    &scsiIoCtx.returnStatus.asc, &scsiIoCtx.returnStatus.ascq); ret =
//    check_Sense_Key_ACQ_And_ACSQ(scsiIoCtx.returnStatus.senseKey, scsiIoCtx.returnStatus.asc,
//    scsiIoCtx.returnStatus.ascq); print_Return_Enum("XD Write Read 10", ret);
//
//    return ret;
//}
//
//-----------------------------------------------------------------------------
//
//  scsi_xd_write_read_32()
//
//! \brief   Description:  Send a SCSI XD Write Read 32 command
//
//  Entry:
//!   \param tDevice - pointer to the device structure
//!   \param wrprotect - the wrprotect field. Only bits2:0 are valid
//!   \param dpo - set the dpo bit
//!   \param fua - set the fua bit
//!   \param disableWrite - set the disable write bit
//!   \param xoprinfo - set the xorpinfo bit
//!   \param logicalBlockAddress - LBA
//!   \param groupNumber - the groupNumber field. only bits 4:0 are valid
//!   \param transferLength - the length of the data to read/write/transfer. Buffers must be this big
//!   \param ptrDataOut - pointer to the data out buffer. Must be non-M_NULLPTR
//!   \param ptrDataIn - pointer to the data in buffer. Must be non-M_NULLPTR
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
// eReturnValues scsi_xd_Write_Read_32(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, bool disableWrite, bool
// xoprinfo, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLength, uint8_t *ptrDataOut, uint8_t
// *ptrDataIn)
//{
//    eReturnValues ret = FAILURE;
//    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);
//    ScsiIoCtx scsiIoCtx;
//    safe_memset(&scsiIoCtx, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
//
//    if (ptrDataOut == M_NULLPTR || ptrDataIn == M_NULLPTR)
//    {
//        return BAD_PARAMETER;
//    }
//
//    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
//    {
//        printf("Sending SCSI XD Write Read 32\n");
//    }
//
//    cdb[OPERATION_CODE] = XDWRITEREAD_32;
//    cdb[1] = 0;//control
//    cdb[2] = RESERVED;
//    cdb[3] = RESERVED;
//    cdb[4] = RESERVED;
//    cdb[5] = RESERVED;
//    cdb[6] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
//    cdb[7] = 0x18;//additional CDB length
//    cdb[8] = 0x00;//service action MSB
//    cdb[9] = 0x07;//service action LSB
//    cdb[10] |= C_CAST(uint8_t, (wrprotect & UINT8_C(0x07)) << 5);
//    if (dpo == true)
//    {
//        cdb[10] |= BIT4;
//    }
//    if (fua == true)
//    {
//        cdb[10] |= BIT3;
//    }
//    if (disableWrite == true)
//    {
//        cdb[10] |= BIT2;
//    }
//    if (xoprinfo == true)
//    {
//        cdb[10] |= BIT0;
//    }
//    cdb[11] = RESERVED;
//    cdb[12] = C_CAST(uint8_t, logicalBlockAddress >> 56);
//    cdb[13] = C_CAST(uint8_t, logicalBlockAddress >> 48);
//    cdb[14] = C_CAST(uint8_t, logicalBlockAddress >> 40);
//    cdb[15] = C_CAST(uint8_t, logicalBlockAddress >> 32);
//    cdb[16] = C_CAST(uint8_t, logicalBlockAddress >> 24);
//    cdb[17] = C_CAST(uint8_t, logicalBlockAddress >> 16);
//    cdb[18] = C_CAST(uint8_t, logicalBlockAddress >> 8);
//    cdb[19] = C_CAST(uint8_t, logicalBlockAddress);
//    cdb[20] = RESERVED;
//    cdb[21] = RESERVED;
//    cdb[22] = RESERVED;
//    cdb[23] = RESERVED;
//    cdb[24] = RESERVED;
//    cdb[25] = RESERVED;
//    cdb[26] = RESERVED;
//    cdb[27] = RESERVED;
//    cdb[28] = C_CAST(uint8_t, transferLength >> 24);
//    cdb[29] = C_CAST(uint8_t, transferLength >> 16);
//    cdb[30] = C_CAST(uint8_t, transferLength >> 8);
//    cdb[31] = C_CAST(uint8_t, transferLength);
//
//    // Set up the CTX
//    scsiIoCtx.device = device;
//    safe_memset(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 0, SPC3_SENSE_LEN);
//    scsiIoCtx.psense = device->drive_info.lastCommandSenseData;
//    scsiIoCtx.senseDataSize = SPC3_SENSE_LEN;
//    safe_memcpy(&scsiIoCtx.cdb[0], SCSI_IO_CTX_MAX_CDB_LEN, &cdb[0], CDB_LEN_32);
//    scsiIoCtx.cdbLength = CDB_LEN_32;
//    scsiIoCtx.direction = XFER_DATA_OUT_IN;
//    //set the buffer info in the bidirectional command structure
//    scsiIoCtx.biDirectionalBuffers.dataInBuffer = ptrDataIn;
//    scsiIoCtx.biDirectionalBuffers.dataInBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.biDirectionalBuffers.dataOutBuffer = ptrDataOut;
//    scsiIoCtx.biDirectionalBuffers.dataOutBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.verbose = 0;
//
//    //while this command is all typed up the lower level windows or linux passthrough code needs some work before this
//    command is actually ready to be used return NOT_SUPPORTED; ret = send_IO(&scsiIoCtx);
//    get_Sense_Key_ACQ_ACSQ(device->drive_info.lastCommandSenseData, &scsiIoCtx.returnStatus.senseKey,
//    &scsiIoCtx.returnStatus.asc, &scsiIoCtx.returnStatus.ascq); ret =
//    check_Sense_Key_ACQ_And_ACSQ(scsiIoCtx.returnStatus.senseKey, scsiIoCtx.returnStatus.asc,
//    scsiIoCtx.returnStatus.ascq); print_Return_Enum("XD Write Read 32", ret);
//
//    return ret;
//}

eReturnValues scsi_xp_Write_10(tDevice* device,
                               bool     dpo,
                               bool     fua,
                               bool     xoprinfo,
                               uint32_t logicalBlockAddress,
                               uint8_t  groupNumber,
                               uint16_t transferLength,
                               uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI XP Write 10\n");
    }

    set_Typical_SCSI_10B_CDB_Fields(cdb, XPWRITE_10, NO_SERVICE_ACTION, logicalBlockAddress, transferLength, 0);

    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    if (xoprinfo)
    {
        cdb[1] |= BIT0;
    }
    cdb[6] = GET_GROUP_CODE(groupNumber);

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData,
                        transferLength * device->drive_info.deviceBlockSize, XFER_DATA_OUT,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write XD Write 10", ret);
    }
    return ret;
}

eReturnValues scsi_xp_Write_32(tDevice* device,
                               bool     dpo,
                               bool     fua,
                               bool     xoprinfo,
                               uint64_t logicalBlockAddress,
                               uint8_t  groupNumber,
                               uint32_t transferLength,
                               uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_32);

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI XP Write 32\n");
    }

    set_Typical_SCSI_32_CDB_Fields(cdb, XPWRITE_32, 0x0006, logicalBlockAddress, transferLength, 0);
    set_SCSI_32B_PI_Fields(cdb, groupNumber, 0, 0, 0, 0);

    cdb[6] = C_CAST(uint8_t, groupNumber & UINT8_C(0x1F));
    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    if (fua)
    {
        cdb[10] |= BIT3;
    }
    if (xoprinfo)
    {
        cdb[10] |= BIT0;
    }

    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData,
                        transferLength * device->drive_info.deviceBlockSize, XFER_DATA_OUT,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("XP Write 32", ret);
    }
    return ret;
}

eReturnValues scsi_Zone_Management_Out_Std_Format_CDB(tDevice*  device,
                                                      eZMAction action,
                                                      uint64_t  zoneID,
                                                      uint16_t  zoneCount,
                                                      bool      all,
                                                      uint16_t  commandSPecific_10_11,
                                                      uint8_t   cmdSpecificBits1,
                                                      uint8_t   actionSpecific14) // 94h
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    switch (action)
    {
    case ZM_ACTION_CLOSE_ZONE:
    case ZM_ACTION_FINISH_ZONE:
    case ZM_ACTION_OPEN_ZONE:
    case ZM_ACTION_RESET_WRITE_POINTERS:
    case ZM_ACTION_SEQUENTIALIZE_ZONE:
        break;
    default: // Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
    }

    // strip invalid bits from cmdspecific fields to avoid collision issues later
    cmdSpecificBits1 &= UINT8_C(0xE0); // remove bits 4:0 as these are the action field
    M_CLEAR_BIT8(actionSpecific14, 0); // remove possible collision with all bit

    cdb[OPERATION_CODE] = ZONE_MANAGEMENT_OUT;
    // set the service action
    cdb[1] = C_CAST(uint8_t, action) | cmdSpecificBits1;
    // set lba field
    cdb[2]  = M_Byte7(zoneID);
    cdb[3]  = M_Byte6(zoneID);
    cdb[4]  = M_Byte5(zoneID);
    cdb[5]  = M_Byte4(zoneID);
    cdb[6]  = M_Byte3(zoneID);
    cdb[7]  = M_Byte2(zoneID);
    cdb[8]  = M_Byte1(zoneID);
    cdb[9]  = M_Byte0(zoneID);
    cdb[10] = M_Byte1(commandSPecific_10_11);
    cdb[11] = M_Byte0(commandSPecific_10_11);
    cdb[12] = M_Byte1(zoneCount);
    cdb[13] = M_Byte0(zoneCount);
    // action specific
    cdb[14] = actionSpecific14;
    if (all)
    {
        cdb[14] |= all;
    }
    cdb[15] = 0; // control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Zone Management Out\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management Out", ret);
    }
    return ret;
}

eReturnValues scsi_Close_Zone(tDevice* device, bool all, uint64_t zoneID, uint16_t zoneCount)
{
    if (all)
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_CLOSE_ZONE, 0, 0, true, 0, 0, 0);
    }
    else
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_CLOSE_ZONE, zoneID, zoneCount, false, 0, 0, 0);
    }
}

eReturnValues scsi_Finish_Zone(tDevice* device, bool all, uint64_t zoneID, uint16_t zoneCount)
{
    if (all)
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_FINISH_ZONE, 0, 0, true, 0, 0, 0);
    }
    else
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_FINISH_ZONE, zoneID, zoneCount, false, 0, 0,
                                                       0);
    }
}

eReturnValues scsi_Open_Zone(tDevice* device, bool all, uint64_t zoneID, uint16_t zoneCount)
{
    if (all)
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_OPEN_ZONE, 0, 0, true, 0, 0, 0);
    }
    else
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_OPEN_ZONE, zoneID, zoneCount, false, 0, 0, 0);
    }
}

eReturnValues scsi_Reset_Write_Pointers(tDevice* device, bool all, uint64_t zoneID, uint16_t zoneCount)
{
    if (all)
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_CLOSE_ZONE, 0, 0, true, 0, 0, 0);
    }
    else
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_CLOSE_ZONE, zoneID, zoneCount, false, 0, 0, 0);
    }
}

eReturnValues scsi_Sequentialize_Zone(tDevice* device, bool all, uint64_t zoneID, uint16_t zoneCount)
{
    if (all)
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_SEQUENTIALIZE_ZONE, 0, 0, true, 0, 0, 0);
    }
    else
    {
        return scsi_Zone_Management_Out_Std_Format_CDB(device, ZM_ACTION_SEQUENTIALIZE_ZONE, zoneID, zoneCount, false,
                                                       0, 0, 0);
    }
}

eReturnValues scsi_Zone_Management_In_Report(tDevice*  device,
                                             eZMAction action,
                                             uint8_t   actionSpecific1,
                                             uint64_t  location,
                                             bool      partial,
                                             uint8_t   reportingOptions,
                                             uint32_t  allocationLength,
                                             uint8_t*  ptrData) // 95h
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);
    eDataTransferDirection dataDir = XFER_NO_DATA;

    switch (action)
    {
    case ZM_ACTION_REPORT_ZONES:
    case ZM_ACTION_REPORT_REALMS:
    case ZM_ACTION_REPORT_ZONE_DOMAINS:
        dataDir = XFER_DATA_IN;
        break;
        // zone activate and zone query are below
    default: // Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
    }

    if (dataDir == XFER_NO_DATA)
    {
        allocationLength = 0;
    }

    actionSpecific1 &= UINT8_C(0xE0);  // remove bits 4:0 as these are the service action
    reportingOptions &= UINT8_C(0x3F); // remove bits 7&6 since those are partial and reserved

    cdb[OPERATION_CODE] = ZONE_MANAGEMENT_IN;
    // set the service action
    cdb[1] = C_CAST(uint8_t, action) | actionSpecific1;
    // set lba field
    cdb[2] = M_Byte7(location);
    cdb[3] = M_Byte6(location);
    cdb[4] = M_Byte5(location);
    cdb[5] = M_Byte4(location);
    cdb[6] = M_Byte3(location);
    cdb[7] = M_Byte2(location);
    cdb[8] = M_Byte1(location);
    cdb[9] = M_Byte0(location);
    // allocation length
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    // action specific
    cdb[14] = reportingOptions;
    if (partial)
    {
        cdb[14] |= BIT7;
    }
    cdb[15] = 0; // control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Zone Management In\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, dataDir,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management In", ret);
    }
    return ret;
}

// for zone activate and zone query commands
eReturnValues scsi_Zone_Management_In_ZD(tDevice*  device,
                                         eZMAction action,
                                         bool      all,
                                         uint64_t  zoneID,
                                         uint16_t  numberOfZones,
                                         uint8_t   otherZoneDomainID,
                                         uint16_t  allocationLength,
                                         uint8_t*  ptrData) // 95h
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);
    eDataTransferDirection dataDir = XFER_NO_DATA;

    switch (action)
    {
    case ZM_ACTION_ZONE_ACTIVATE:
    case ZM_ACTION_ZONE_QUERY:
        dataDir = XFER_DATA_IN;
        break;
    default: // Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
    }

    if (dataDir == XFER_NO_DATA)
    {
        allocationLength = 0;
    }

    cdb[OPERATION_CODE] = ZONE_MANAGEMENT_IN;
    // set the service action
    cdb[1] = C_CAST(uint8_t, action);
    if (all)
    {
        cdb[1] |= BIT7;
    }
    // set lba field
    cdb[2] = M_Byte7(zoneID);
    cdb[3] = M_Byte6(zoneID);
    cdb[4] = M_Byte5(zoneID);
    cdb[5] = M_Byte4(zoneID);
    cdb[6] = M_Byte3(zoneID);
    cdb[7] = M_Byte2(zoneID);
    cdb[8] = M_Byte1(zoneID);
    cdb[9] = M_Byte0(zoneID);
    // number of zones
    cdb[10] = M_Byte1(numberOfZones);
    cdb[11] = M_Byte0(numberOfZones);
    // allocation length
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    // action specific
    cdb[14] = otherZoneDomainID;
    cdb[15] = 0; // control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Zone Management In\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, dataDir,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management In", ret);
    }
    return ret;
}

eReturnValues scsi_Zone_Activate(tDevice* device,
                                 bool     all,
                                 uint64_t zoneID,
                                 uint16_t numberOfZones,
                                 uint8_t  otherZoneDomainID,
                                 uint16_t allocationLength,
                                 uint8_t* ptrData)
{
    return scsi_Zone_Management_In_ZD(device, ZM_ACTION_ZONE_ACTIVATE, all, zoneID, numberOfZones, otherZoneDomainID,
                                      allocationLength, ptrData);
}

eReturnValues scsi_Zone_Query(tDevice* device,
                              bool     all,
                              uint64_t zoneID,
                              uint16_t numberOfZones,
                              uint8_t  otherZoneDomainID,
                              uint16_t allocationLength,
                              uint8_t* ptrData)
{
    return scsi_Zone_Management_In_ZD(device, ZM_ACTION_ZONE_QUERY, all, zoneID, numberOfZones, otherZoneDomainID,
                                      allocationLength, ptrData);
}

eReturnValues scsi_Report_Zones(tDevice*              device,
                                eZoneReportingOptions reportingOptions,
                                bool                  partial,
                                uint32_t              allocationLength,
                                uint64_t              zoneStartLBA,
                                uint8_t*              ptrData)
{
    return scsi_Zone_Management_In_Report(device, ZM_ACTION_REPORT_ZONES, 0, zoneStartLBA, partial,
                                          C_CAST(uint8_t, reportingOptions), allocationLength, ptrData);
}

eReturnValues scsi_Report_Realms(tDevice*                device,
                                 eRealmsReportingOptions reportingOptions,
                                 uint32_t                allocationLength,
                                 uint64_t                realmLocator,
                                 uint8_t*                ptrData)
{
    return scsi_Zone_Management_In_Report(device, ZM_ACTION_REPORT_REALMS, 0, realmLocator, false,
                                          C_CAST(uint8_t, reportingOptions), allocationLength, ptrData);
}

eReturnValues scsi_Report_Zone_Domains(tDevice*                    device,
                                       eZoneDomainReportingOptions reportingOptions,
                                       uint32_t                    allocationLength,
                                       uint64_t                    zoneDomainLocator,
                                       uint8_t*                    ptrData)
{
    return scsi_Zone_Management_In_Report(device, ZM_ACTION_REPORT_ZONE_DOMAINS, 0, zoneDomainLocator, false,
                                          C_CAST(uint8_t, reportingOptions), allocationLength, ptrData);
}

eReturnValues scsi_Get_Physical_Element_Status(tDevice* device,
                                               uint32_t startingElement,
                                               uint32_t allocationLength,
                                               uint8_t  filter,
                                               uint8_t  reportType,
                                               uint8_t* ptrData)
{
    eReturnValues          ret     = FAILURE;
    eDataTransferDirection dataDir = XFER_DATA_IN;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);
    cdb[OPERATION_CODE] = 0x9E;
    // set the service action
    cdb[1] = 0x17;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    // starting element
    cdb[6] = M_Byte3(startingElement);
    cdb[7] = M_Byte2(startingElement);
    cdb[8] = M_Byte1(startingElement);
    cdb[9] = M_Byte0(startingElement);
    // allocation length
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    // filter & report type bits
    cdb[14] =
        C_CAST(uint8_t, (filter << 6) |
                            (reportType & 0x0F)); // filter is 2 bits, report type is 4 bits. All others are reserved;
    cdb[15] = 0;                                  // control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Get Physical Element Status\n");
    }

    // send the command
    if (allocationLength == 0)
    {
        dataDir = XFER_NO_DATA;
    }
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, dataDir,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get Physical Element Status", ret);
    }
    return ret;
}

eReturnValues scsi_Remove_And_Truncate(tDevice* device, uint64_t requestedCapacity, uint32_t elementIdentifier)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    uint32_t timeout = UINT32_C(0);
    if (os_Is_Infinite_Timeout_Supported())
    {
        timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, 0x9E, 0x18, requestedCapacity, elementIdentifier, 0);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Remove And Truncate\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Remove And Truncate", ret);
    }
    return ret;
}

eReturnValues scsi_Remove_Element_And_Modify_Zones(tDevice* device, uint32_t elementIdentifier)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    uint32_t timeout = UINT32_C(0);
    if (os_Is_Infinite_Timeout_Supported())
    {
        timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, 0x9E, 0x1A, RESERVED, elementIdentifier, 0);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Remove Element And Modify Zones\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Remove Element And Modify Zones", ret);
    }
    return ret;
}

eReturnValues scsi_Restore_Elements_And_Rebuild(tDevice* device)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_16);

    uint32_t timeout = UINT32_C(0);
    if (os_Is_Infinite_Timeout_Supported())
    {
        timeout = INFINITE_TIMEOUT_VALUE;
    }
    else
    {
        timeout = MAX_CMD_TIMEOUT_SECONDS;
    }

    set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(cdb, 0x9E, 0x19, RESERVED, RESERVED, 0);

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Restore Elements and Rebuild\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Restore Elements and Rebuild", ret);
    }
    return ret;
}

eReturnValues scsi_Persistent_Reserve_In(tDevice* device,
                                         uint8_t  serviceAction,
                                         uint16_t allocationLength,
                                         uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
    cdb[OPERATION_CODE] = PERSISTENT_RESERVE_IN_CMD;
    // set the service action
    cdb[1] = get_bit_range_uint8(serviceAction, 4, 0);
    // reserved
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    // allocation length
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    // control
    cdb[9] = 0;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Persistent Reserve In - %" PRIu8 "\n",
               C_CAST(uint8_t, get_bit_range_uint8(serviceAction, 4, 0)));
    }
    // send the command
    if (ptrData && allocationLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, allocationLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Persistent Reserve In", ret);
    }
    return ret;
}

eReturnValues scsi_Persistent_Reserve_Out(tDevice* device,
                                          uint8_t  serviceAction,
                                          uint8_t  scope,
                                          uint8_t  type,
                                          uint32_t parameterListLength,
                                          uint8_t* ptrData)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_10);
    cdb[OPERATION_CODE] = PERSISTENT_RESERVE_OUT_CMD;
    // set the service action
    cdb[1] = get_bit_range_uint8(serviceAction, 4, 0);
    // scope & type
    cdb[2] = M_NibblesTo1ByteValue(M_Nibble0(scope), M_Nibble0(type));
    // reserved
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    // allocation length
    cdb[5] = M_Byte3(parameterListLength);
    cdb[6] = M_Byte2(parameterListLength);
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    // control
    cdb[9] = 0;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Persistent Reserve Out - %" PRIu8 "\n",
               C_CAST(uint8_t, get_bit_range_uint8(serviceAction, 4, 0)));
    }
    // send the command
    if (ptrData && parameterListLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), ptrData, parameterListLength, XFER_DATA_OUT,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Persistent Reserve Out", ret);
    }
    return ret;
}

eReturnValues scsi_Rezero_Unit(tDevice* device)
{
    eReturnValues ret = FAILURE;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_6);
    cdb[OPERATION_CODE] = REZERO_UNIT_CMD;
    cdb[1] = RESERVED; // technically has lun in here, but that is old SCSI2 ism that is long gone and is autofilled by
                       // low-level drivers on these old devices -TJE
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = 0;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Rezero Unit\n");
    }
    // send the command
    ret = scsi_Send_Cdb(device, &cdb[0], SIZE_OF_STACK_ARRAY(cdb), M_NULLPTR, 0, XFER_NO_DATA,
                        device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Rezero Unit", ret);
    }
    return ret;
}
