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
// \file scsi_cmds.c   Implementation for SCSI command functions
//                     The intention of the file is to be generic & not OS specific

#include "scsi_helper_func.h"
#include "common_public.h"
#include "platform_helper.h"

int scsi_Send_Cdb(tDevice *device, uint8_t *cdb, eCDBLen cdbLen, uint8_t *pdata, uint32_t dataLen, eDataTransferDirection dataDirection, uint8_t *senseData, uint32_t senseDataLen, uint32_t timeoutSeconds)
{
    int ret = UNKNOWN;
    ScsiIoCtx scsiIoCtx;
	senseDataFields senseFields;
	memset(&senseFields, 0, sizeof(senseDataFields));
    memset(&scsiIoCtx, 0, sizeof(ScsiIoCtx));
    uint8_t *senseBuffer = senseData;
    //if we were not given a sense buffer, assume we want to use the last command sense data that is part of the device struct
    if (!senseBuffer || senseDataLen == 0)
    {
        senseBuffer = device->drive_info.lastCommandSenseData;
        senseDataLen = SPC3_SENSE_LEN;
    }
    else
    {
        memset(senseBuffer, 0, senseDataLen);
    }
    //check a couple of the parameters before continuing
    if (!device)
    {
        perror("device struct is NULL!");
        return BAD_PARAMETER;
    }
    if (!cdb)
    {
        perror("cdb array is NULL!");
        return BAD_PARAMETER;
    }
    if (cdbLen == CDB_LEN_UNKNOWN)
    {
        perror("Invalid CDB length specified!");
        return BAD_PARAMETER;
    }
    if (!pdata && dataLen != 0)
    {
        perror("Datalen must be set to 0 when pdata is NULL");
        return BAD_PARAMETER;
    }

    //set up the context
    scsiIoCtx.device = device;
    scsiIoCtx.psense = senseBuffer;
    scsiIoCtx.senseDataSize = senseDataLen;
    memcpy(&scsiIoCtx.cdb[0], &cdb[0], cdbLen);
    scsiIoCtx.cdbLength = cdbLen;
    scsiIoCtx.direction = dataDirection;
    scsiIoCtx.pdata = pdata;
    scsiIoCtx.dataLength = dataLen;
    scsiIoCtx.verbose = 0;
    scsiIoCtx.timeout = M_Max(timeoutSeconds, device->drive_info.defaultTimeoutSeconds);
    if (timeoutSeconds == 0)
    {
        scsiIoCtx.timeout = M_Max(15, device->drive_info.defaultTimeoutSeconds);
    }

    //clear the last command sense data every single time before we issue any commands
    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
    {
        printf("\n  CDB:\n");
        print_Data_Buffer(scsiIoCtx.cdb, scsiIoCtx.cdbLength, false);
    }
    #if defined (_DEBUG)
    //This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a command.
    //This was very important for debugging windows issues, which is why I have this ifdef in place for debug builds. - TJE
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && pdata != NULL)
    #else
    //Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && pdata != NULL && dataDirection == XFER_DATA_OUT)
    #endif
    {
        printf("\t  Data Buffer being sent:\n");
        print_Data_Buffer(pdata, dataLen, true);
        printf("\n");
    }
    //send the command
    int sendIOret = send_IO(&scsiIoCtx);
    if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity && scsiIoCtx.psense)
    {
        printf("\n  Sense Data Buffer:\n");
        print_Data_Buffer(scsiIoCtx.psense, get_Returned_Sense_Data_Length(scsiIoCtx.psense), false);
        printf("\n");
    }
    //get_Sense_Key_ASC_ASCQ_FRU(scsiIoCtx.psense, senseDataLen, &scsiIoCtx.returnStatus.senseKey, &scsiIoCtx.returnStatus.asc, &scsiIoCtx.returnStatus.ascq, &scsiIoCtx.returnStatus.fru);
	get_Sense_Data_Fields(scsiIoCtx.psense, scsiIoCtx.senseDataSize, &senseFields);
    ret = check_Sense_Key_ASC_ASCQ_And_FRU(device, senseFields.scsiStatusCodes.senseKey, senseFields.scsiStatusCodes.asc, senseFields.scsiStatusCodes.ascq, senseFields.scsiStatusCodes.fru);
	//if verbose mode and sense data is non-NULL, we should try to print out all the relavent information we can
	if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity && scsiIoCtx.psense)
	{
		print_Sense_Fields(&senseFields);
	}
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        //print command timing information
        print_Command_Time(device->drive_info.lastCommandTimeNanoSeconds);
    }
    #if defined (_DEBUG)
    //This is different for debug because sometimes we need to see if the data buffer actually changed after issuing a command.
    //This was very important for debugging windows issues, which is why I have this ifdef in place for debug builds. - TJE
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && pdata != NULL)
    #else
    //Only print the data buffer being sent when it is a data transfer to the drive (data out command)
    if (VERBOSITY_BUFFERS <= device->deviceVerbosity && pdata != NULL && dataDirection == XFER_DATA_IN)
    #endif
    {
        printf("\t  Data Buffer being returned:\n");
        print_Data_Buffer(pdata, dataLen, true);
        printf("\n");
    }
    if (senseData && senseDataLen > 0 && senseData != device->drive_info.lastCommandSenseData)
    {
        memcpy(device->drive_info.lastCommandSenseData, senseBuffer, M_Min(SPC3_SENSE_LEN, senseDataLen));
    }
    if (ret == SUCCESS && sendIOret != SUCCESS)
    {
        ret = sendIOret;
    }
    if ((device->drive_info.lastCommandTimeNanoSeconds / 1000000000) >= scsiIoCtx.timeout)
    {
        ret = COMMAND_TIMEOUT;
    }
    return ret;
}

int scsi_SecurityProtocol_In(tDevice *device, uint8_t securityProtocol, uint16_t securityProtocolSpecific, bool inc512, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12]       = { 0 };
    uint32_t  dataLength = allocationLength;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Security Protocol In\n");
    }

    cdb[OPERATION_CODE] = SECURITY_PROTOCOL_IN;
    cdb[1] = securityProtocol;
    cdb[2] = M_Byte1(securityProtocolSpecific);
    cdb[3] = M_Byte0(securityProtocolSpecific);
    if (inc512)
    {
        cdb[4] |= BIT7;
        dataLength *= LEGACY_DRIVE_SEC_SIZE;
    }
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;

    if (ptrData && allocationLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Protocol In", ret);
    }
    return ret;
}

int scsi_Report_Supported_Operation_Codes(tDevice *device, bool rctd, uint8_t reportingOptions, uint8_t requestedOperationCode, uint16_t reequestedServiceAction, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_12]       = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Requesting SCSI Supported Op Codes\n");
    }

    cdb[OPERATION_CODE] = REPORT_SUPPORTED_OPERATION_CODES_CMD;
    cdb[1] = 0x0C; // This is always 0x0C per SPC spec
    if (rctd)
    {
        cdb[2] |= BIT7;
    }
    cdb[2] |= (reportingOptions & 0x07); //bit 0,1,2 only valid
    cdb[3] = requestedOperationCode;
    cdb[4] = M_Byte1(reequestedServiceAction);
    cdb[5] = M_Byte0(reequestedServiceAction);
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;

    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Supported Op Codes", ret);
    }
    return ret;
}

int scsi_Sanitize_Cmd(tDevice *device, eScsiSanitizeFeature sanitizeFeature, bool immediate, bool znr, bool ause, uint16_t parameterListLength, uint8_t *ptrData)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_10]       = { 0 };
    eDataTransferDirection dataDir = XFER_NO_DATA;
    
    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);
    
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Sanitize Command\n");
    }

    cdb[OPERATION_CODE] = SANITIZE_CMD;
    cdb[1] = sanitizeFeature & 0x1F;//make sure we don't set any higher bits
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
    //parameter list length
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

    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, parameterListLength, dataDir, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Sanitize", ret);
    }
    return ret;
}

int scsi_Sanitize_Block_Erase(tDevice *device, bool allowUnrestrictedSanitizeExit, bool immediate, bool znr)
{
    return scsi_Sanitize_Cmd(device, SCSI_SANITIZE_BLOCK_ERASE, immediate, znr, allowUnrestrictedSanitizeExit, 0, NULL);
}

int scsi_Sanitize_Cryptographic_Erase(tDevice *device, bool allowUnrestrictedSanitizeExit, bool immediate, bool znr)
{
    return scsi_Sanitize_Cmd(device, SCSI_SANITIZE_CRYPTOGRAPHIC_ERASE, immediate, znr, allowUnrestrictedSanitizeExit, 0, NULL);
}

int scsi_Sanitize_Exit_Failure_Mode(tDevice *device)
{
    return scsi_Sanitize_Cmd(device, SCSI_SANITIZE_EXIT_FAILURE_MODE, false, false, false, 0, NULL);
}

int scsi_Sanitize_Overwrite(tDevice *device, bool allowUnrestrictedSanitizeExit, bool znr, bool immediate, bool invertBetweenPasses, eScsiSanitizeOverwriteTest test, uint8_t overwritePasses, uint8_t *pattern, uint16_t patternLengthBytes)
{
    int ret = UNKNOWN;
    if ((patternLengthBytes != 0 && pattern == NULL) || (patternLengthBytes > device->drive_info.deviceBlockSize))
    {
        return BAD_PARAMETER;
    }
    uint8_t *overwriteBuffer = calloc(patternLengthBytes + 4, sizeof(uint8_t));
    if (!overwriteBuffer)
    {
        return MEMORY_FAILURE;
    }
    overwriteBuffer[0] = overwritePasses & 0x1F;
    overwriteBuffer[0] |= (test & 0x03) << 5;
    if (invertBetweenPasses)
    {
        overwriteBuffer[0] |= BIT7;
    }
    overwriteBuffer[1] = RESERVED;
    overwriteBuffer[2] = M_Byte1(patternLengthBytes);
    overwriteBuffer[3] = M_Byte0(patternLengthBytes);
    if (patternLengthBytes > 0)
    {
        memcpy(&overwriteBuffer[4], pattern, patternLengthBytes);
    }
    ret = scsi_Sanitize_Cmd(device, SCSI_SANITIZE_OVERWRITE, immediate, znr, allowUnrestrictedSanitizeExit, patternLengthBytes + 4, overwriteBuffer);
    safe_Free(overwriteBuffer);
    return ret;
}

int scsi_Request_Sense_Cmd(tDevice *device, bool descriptorBit, uint8_t *pdata, uint16_t dataSize)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_6]       = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Request Sense Command\n");
    }

    if (pdata == NULL)
    {
        return BAD_PARAMETER;
    }

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

    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), pdata, dataSize, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Request Sense", ret);
    }
    return ret;
}

int scsi_Log_Sense_Cmd(tDevice *device, bool saveParameters, uint8_t pageControl, uint8_t pageCode, uint8_t subpageCode, uint16_t paramPointer, uint8_t *ptrData, uint16_t dataSize)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_10]       = { 0 };

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
    cdb[2] |= (pageControl & 0x03) << 6;
    cdb[2] |= pageCode & 0x3F;
    cdb[3] = subpageCode;
    cdb[4] = RESERVED;
    cdb[5] = M_Byte1(paramPointer);
    cdb[6] = M_Byte0(paramPointer);
    cdb[7] = M_Byte1(dataSize);
    cdb[8] = M_Byte0(dataSize);

    if (dataSize > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Log Sense", ret);
    }
    return ret;
}

int scsi_Log_Select_Cmd(tDevice *device, bool pcr, bool sp, uint8_t pageControl, uint8_t pageCode, uint8_t subpageCode, uint16_t parameterListLength, uint8_t* ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    uint8_t cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Log Select Command, page code 0x%02" PRIx8 "\n",pageCode);
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = LOG_SELECT_CMD;
    if (sp)
    {
        cdb[1] |= 0x01;
    }
    if (pcr)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] |= (pageControl & 0x03) << 6;
    cdb[2] |= pageCode & 0x3F;
    cdb[3] = subpageCode;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Log Select", ret);
    }
    return ret;
}

int scsi_Send_Diagnostic(tDevice *device, uint8_t selfTestCode, uint8_t pageFormat, uint8_t selfTestBit, uint8_t deviceOffLIne, uint8_t unitOffLine, uint16_t parameterListLength, uint8_t *pdata, uint16_t dataSize, uint32_t timeoutSeconds)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_6]       = { 0 };
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Send Diagnostic Command\n");
    }
    // Set up the CDB.
    cdb[OPERATION_CODE] = SEND_DIAGNOSTIC_CMD; //Send Diagnostic
    cdb[1] |= selfTestCode << 5;
    cdb[1] |= (pageFormat & 0x01) << 4;
    cdb[1] |= (selfTestBit & 0x01) << 2;
    cdb[1] |= (deviceOffLIne & 0x01) << 1;
    cdb[1] |= (unitOffLine & 0x01);
    cdb[3] = M_Byte1(parameterListLength);
    cdb[4] = M_Byte0(parameterListLength);
    //send the command
    if (!pdata)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), pdata, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Send Diagnostic", ret);
    }
    return ret;
}

int scsi_Read_Capacity_10(tDevice *device, uint8_t *pdata, uint16_t dataSize)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_10]       = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Capacity 10 command\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_CAPACITY_10;
    //send the command
    if (dataSize > 0 && pdata)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), pdata, dataSize, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Capacity 10", ret);
    }
    return ret;
}

int scsi_Read_Capacity_16(tDevice *device, uint8_t *pdata, uint32_t dataSize)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_16]       = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Capacity 16 command\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_CAPACITY_16;
    cdb[1] = 0x10;
    cdb[10] = M_Byte3(dataSize);
    cdb[11] = M_Byte2(dataSize);
    cdb[12] = M_Byte1(dataSize);
    cdb[13] = M_Byte0(dataSize);
    //send the command
    if (dataSize > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), pdata, dataSize, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Capacity 16", ret);
    }
    return ret;
}

int scsi_Mode_Sense_6(tDevice * device, uint8_t pageCode, uint8_t allocationLength, uint8_t subPageCode, bool DBD, eScsiModePageControl pageControl, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    uint8_t cdb[CDB_LEN_6] = { 0 };
    
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Mode Sense 6, page 0x%02"PRIx8"\n", pageCode);
    }
    cdb[OPERATION_CODE] = MODE_SENSE_6_CMD;
    if (DBD)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] |= (pageControl & 0x03) << 6;
    cdb[2] |= pageCode & 0x3F;
    cdb[3] = subPageCode;
    cdb[4] = allocationLength;
    cdb[5] = 0;//control
    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Sense 6", ret);
    }
    return ret;
}

int scsi_Mode_Sense_10(tDevice *device, uint8_t pageCode, uint32_t allocationLength, uint8_t subPageCode, bool DBD, bool LLBAA, eScsiModePageControl pageControl, uint8_t *ptrData)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_10]       = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Mode Sense 10, page 0x%02"PRIx8"\n", pageCode);
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
    cdb[2] |= (pageControl & 0x03) << 6;
    cdb[2] |= pageCode & 0x3F;
    cdb[3] = subPageCode;
    cdb[4] = RESERVED; //reserved
    cdb[5] = RESERVED; //reserved
    cdb[6] = RESERVED; //reserved
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    cdb[9] = 0; //control
    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Sense 10", ret);
    }
    return ret;
}

int scsi_Mode_Select_6(tDevice *device, uint8_t parameterListLength, bool pageFormat, bool savePages, bool resetToDefaults, uint8_t *ptrData, uint32_t dataSize)
{
    int ret = UNKNOWN;
    uint8_t cdb[CDB_LEN_6] = { 0 };

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
    cdb[5] = 0;//control
    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Select 6", ret);
    }
    return ret;
}

int scsi_Mode_Select_10(tDevice *device, uint16_t parameterListLength, bool pageFormat, bool savePages, bool resetToDefaults, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret       = FAILURE;
    uint8_t   cdb[CDB_LEN_10]       = { 0 };

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
    cdb[9] = 0; //control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Mode Select 10", ret);
    }
    return ret;
}

int scsi_Write_Buffer(tDevice *device, eWriteBufferMode mode, uint8_t modeSpecific, uint8_t bufferID, uint32_t bufferOffset, uint32_t parameterListLength, uint8_t *ptrData)
{
    int ret = UNKNOWN;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Buffer\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = WRITE_BUFFER_CMD;
    cdb[1] = (uint8_t)mode;
    cdb[1] |= (modeSpecific & 0x07) << 5;
    cdb[2] = bufferID;
    cdb[3] = M_Byte2(bufferOffset);
    cdb[4] = M_Byte1(bufferOffset);
    cdb[5] = M_Byte0(bufferOffset);
    cdb[6] = M_Byte2(parameterListLength);
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    cdb[9] = 0; //control

    //send the command
    if (ptrData && parameterListLength != 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, parameterListLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Buffer", ret);
    }
    return ret;
}

int scsi_Inquiry(tDevice *device, uint8_t *pdata, uint32_t dataLength, uint8_t pageCode, bool evpd, bool cmdDt)
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_6] = { 0 };

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
    cdb[5] = 0;//control

    //send the command
    if (dataLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), pdata, dataLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
        if (ret == SUCCESS && !evpd && !cmdDt && pageCode == 0)
        {
            if (pdata != device->drive_info.scsiVpdData.inquiryData)
            {
                //this should only be copying std inquiry data to thislocation in the device struct to keep it up to date each time an inquiry is sent to the drive.
                memcpy(device->drive_info.scsiVpdData.inquiryData, pdata, M_Min(dataLength, 96));
            }
            uint8_t version = pdata[2];
            switch (version) //convert some versions since old standards broke the version number into ANSI vs ECMA vs ISO standard numbers
            {
            case 0x81:
                version = 1;//changing to 1 for SCSI
                break;
            case 0x80:
            case 0x82:
                version = 2;//changing to 2 for SCSI 2
                break;
            case 0x83:
                version = 3;//changing to 3 for SPC
                break;
            case 0x84:
                version = 4;//changing to 4 for SPC2
                break;
            default:
                //convert some versions since old standards broke the version number into ANSI vs ECMA vs ISO standard numbers
                if ((version >= 0x08 && version <= 0x0C) ||
                    (version >= 0x40 && version <= 0x44) ||
                    (version >= 0x48 && version <= 0x4C) ||
                    (version >= 0x80 && version <= 0x84) ||
                    (version >= 0x88 && version <= 0x8C))
                {
                    //these are obsolete version numbers
                    version = M_GETBITRANGE(version, 3, 0);
                }
                break;
            }
            device->drive_info.scsiVersion = version;//changing this to one of these version numbers to keep the rest of the library code that would use this simple. - TJE
        }
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Inquiry", ret);
    }

    return ret;
}

int scsi_Read_Media_Serial_Number(tDevice *device, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Media Serial Number\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_MEDIA_SERIAL_NUMBER;
    cdb[1] |= 0x01;//service action
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Media Serial Number", ret);
    }
    return ret;
}

int scsi_Read_Attribute(tDevice *device, uint8_t serviceAction, uint32_t restricted, uint8_t logicalVolumeNumber, uint8_t partitionNumber, uint16_t firstAttributeIdentifier, uint32_t allocationLength, bool cacheBit, uint8_t*ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Attribute\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_ATTRIBUTE;
    cdb[1] = serviceAction & 0x1F;
    cdb[2] = M_Byte2(restricted);
    cdb[3] = M_Byte1(restricted);
    cdb[4] = M_Byte0(restricted);
    cdb[5] = logicalVolumeNumber;
    cdb[6] = RESERVED;
    cdb[7] = partitionNumber;
    cdb[8] = M_Byte1(firstAttributeIdentifier);
    cdb[9] = M_Byte0(firstAttributeIdentifier);
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    cdb[14] = RESERVED;
    if (cacheBit)
    {
        cdb[14] |= BIT0;
    }
    cdb[15] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Attribute", ret);
    }
    return ret;
}

int scsi_Read_Buffer(tDevice *device, uint8_t mode, uint8_t bufferID, uint32_t bufferOffset, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Buffer\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_BUFFER_CMD;
    //set the mode
    //TODO: the mentioned reserved bits are now "mode specific" field...will need to add support for this.
    cdb[1] = mode;// &0x1F;//removed this &0x1F in order to get internal status log going. Looks like some reserved bits may be used in a newer spec or something that I don't have yet. - TJE
    //buffer ID
    cdb[2] = bufferID;
    cdb[3] = M_Byte2(bufferOffset);
    cdb[4] = M_Byte1(bufferOffset);
    cdb[5] = M_Byte0(bufferOffset);
    cdb[6] = M_Byte2(allocationLength);
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    cdb[9] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Buffer", ret);
    }
    return ret;
}

int scsi_Read_Buffer_16(tDevice *device, uint8_t mode, uint8_t modeSpecific, uint8_t bufferID, uint64_t bufferOffset, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Buffer 16\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = READ_BUFFER_16_CMD;
    //set the mode
    //TODO: the mentioned reserved bits are now "mode specific" field...will need to add support for this.
    cdb[1] = mode;// &0x1F;//removed this &0x1F in order to get internal status log going. Looks like some reserved bits may be used in a newer spec or something that I don't have yet. - TJE
    cdb[2] = M_Byte7(bufferOffset);
    cdb[3] = M_Byte6(bufferOffset);
    cdb[4] = M_Byte5(bufferOffset);
    cdb[5] = M_Byte4(bufferOffset);
    cdb[6] = M_Byte3(bufferOffset);
    cdb[7] = M_Byte2(bufferOffset);
    cdb[8] = M_Byte1(bufferOffset);
    cdb[9] = M_Byte0(bufferOffset);
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    //buffer ID
    cdb[14] = bufferID;
    //control
    cdb[15] = 0;

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Buffer 16", ret);
    }
    return ret;
}

int scsi_Receive_Diagnostic_Results(tDevice *device, bool pcv, uint8_t pageCode, uint16_t allocationLength, uint8_t *ptrData, uint32_t timeoutSeconds)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Receive Diagnostic Results, page code = 0x%02" PRIX8 "\n",pageCode);
    }
    if (timeoutSeconds == 0)
    {
        timeoutSeconds = 15;
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
    cdb[5] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Receive Diagnostic Results", ret);
    }
    return ret;
}

int scsi_Remove_I_T_Nexus(tDevice *device, uint32_t parameterListLength, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Remove I_T Nexus\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REMOVE_I_T_NEXUS;
    cdb[1] = 0x0C;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(parameterListLength);
    cdb[7] = M_Byte2(parameterListLength);
    cdb[8] = M_Byte1(parameterListLength);
    cdb[9] = M_Byte0(parameterListLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Remove I_T Nexus", ret);
    }
    return ret;
}

int scsi_Report_Aliases(tDevice *device, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Aliases\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_ALIASES_CMD;
    cdb[1] = 0x0B;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Aliases", ret);
    }
    return ret;
}

int scsi_Report_Identifying_Information(tDevice *device, uint16_t restricted, uint32_t allocationLength, uint8_t identifyingInformationType, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Identifying Information\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_IDENTIFYING_INFORMATION;
    cdb[1] = 0x05;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = M_Byte1(restricted);//SCC2
    cdb[5] = M_Byte0(restricted);
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = (identifyingInformationType & 0x7F) >> 1;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Identifying Information", ret);
    }
    return ret;
}

int scsi_Report_Luns(tDevice *device, uint8_t selectReport, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report LUNs\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_LUNS_CMD;
    cdb[1] = RESERVED;
    cdb[2] = selectReport;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report LUNs", ret);
    }
    return ret;
}

int scsi_Report_Priority(tDevice *device, uint8_t priorityReported, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Priority\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_PRIORITY_CMD;
    cdb[1] = 0x0E;
    cdb[2] = (priorityReported & 0x03) << 6;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Priority", ret);
    }
    return ret;
}

int scsi_Report_Supported_Task_Management_Functions(tDevice *device, bool repd, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Supported Task Management Functions\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCS;
    cdb[1] = 0x0D;
    if (repd)
    {
        cdb[2] |= BIT7;
    }
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Supported Task Management Functions", ret);
    }
    return ret;
}

int scsi_Report_Timestamp(tDevice *device, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };
    ScsiIoCtx scsiIoCtx;
    memset(&scsiIoCtx, 0, sizeof(ScsiIoCtx));

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Timestamp\n");
    }

    // Set up the CDB.
    cdb[OPERATION_CODE] = REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCS;
    cdb[1] = 0x0F;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Timestamp", ret);
    }
    return ret;
}

int scsi_SecurityProtocol_Out(tDevice *device, uint8_t securityProtocol, uint16_t securityProtocolSpecific, bool inc512, uint32_t transferLength, uint8_t *ptrData, uint32_t timeout)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };
    uint32_t  dataLength = transferLength;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Security Protocol Out\n");
    }

    cdb[OPERATION_CODE] = SECURITY_PROTOCOL_OUT;
    cdb[1] = securityProtocol;
    cdb[2] = M_Byte1(securityProtocolSpecific);
    cdb[3] = M_Byte0(securityProtocolSpecific);
    if (inc512)
    {
        cdb[4] |= BIT7;
        dataLength *= LEGACY_DRIVE_SEC_SIZE;
    }
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(transferLength);
    cdb[7] = M_Byte2(transferLength);
    cdb[8] = M_Byte1(transferLength);
    cdb[9] = M_Byte0(transferLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;

    //send the command
    if (ptrData && transferLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeout);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Security Protocol Out", ret);
    }

    return ret;
}

int scsi_Set_Identifying_Information(tDevice *device, uint16_t restricted, uint32_t parameterListLength, uint8_t identifyingInformationType, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Identifying Information\n");
    }

    cdb[OPERATION_CODE] = SET_IDENTIFYING_INFORMATION;
    cdb[1] = 0x06;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = M_Byte1(restricted);//SCC
    cdb[5] = M_Byte0(restricted);
    cdb[6] = M_Byte3(parameterListLength);
    cdb[7] = M_Byte2(parameterListLength);
    cdb[8] = M_Byte1(parameterListLength);
    cdb[9] = M_Byte0(parameterListLength);
    cdb[10] = identifyingInformationType << 1;
    cdb[11] = 0;

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Identifying Information", ret);
    }
    return ret;
}
int scsi_Set_Priority(tDevice *device, uint8_t I_T_L_NexusToSet, uint32_t parameterListLength, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Priority\n");
    }

    cdb[OPERATION_CODE] = SET_PRIORITY_CMD;
    cdb[1] = 0x0E;
    cdb[2] = (I_T_L_NexusToSet & 0x03) << 6;//only bits 1:0 are valid on this input
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(parameterListLength);
    cdb[7] = M_Byte2(parameterListLength);
    cdb[8] = M_Byte1(parameterListLength);
    cdb[9] = M_Byte0(parameterListLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Priority", ret);
    }
    return ret;
}

int scsi_Set_Target_Port_Groups(tDevice *device, uint32_t parameterListLength, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Target Port Groups\n");
    }

    cdb[OPERATION_CODE] = SET_TARGET_PORT_GROUPS_CMD;
    cdb[1] = 0x0A;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(parameterListLength);
    cdb[7] = M_Byte2(parameterListLength);
    cdb[8] = M_Byte1(parameterListLength);
    cdb[9] = M_Byte0(parameterListLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Target Port Groups", ret);
    }
    return ret;
}

int scsi_Set_Timestamp(tDevice *device, uint32_t parameterListLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Set Timestamp\n");
    }

    cdb[OPERATION_CODE] = SET_TIMESTAMP_CMD;
    cdb[1] = 0x0F;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(parameterListLength);
    cdb[7] = M_Byte2(parameterListLength);
    cdb[8] = M_Byte1(parameterListLength);
    cdb[9] = M_Byte0(parameterListLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, parameterListLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Set Timestamp", ret);
    }
    return ret;
}

int scsi_Test_Unit_Ready(tDevice *device, scsiStatus * pReturnStatus)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Test Unit Ready\n");
    }

    cdb[OPERATION_CODE] = TEST_UNIT_READY_CMD;
    cdb[1] = RESERVED;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (pReturnStatus)
    {
        get_Sense_Key_ASC_ASCQ_FRU(device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, &pReturnStatus->senseKey, &pReturnStatus->asc, &pReturnStatus->ascq, &pReturnStatus->fru);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        //leave this here or else the verbose output gets confusing to look at when debugging- this only prints the ret for the function, not the acs/acsq stuff
        print_Return_Enum("Test Unit Ready", ret);
    }
    return ret;
}

int scsi_Write_Attribute(tDevice *device, bool wtc, uint32_t restricted, uint8_t logicalVolumeNumber, uint8_t partitionNumber, uint32_t parameterListLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Attribute\n");
    }

    cdb[OPERATION_CODE] = WRITE_ATTRIBUTE_CMD;
    if (wtc)
    {
        cdb[1] |= BIT0;
    }
    cdb[2] = M_Byte2(restricted);
    cdb[3] = M_Byte1(restricted);
    cdb[4] = M_Byte0(restricted);
    cdb[5] = logicalVolumeNumber;
    cdb[6] = RESERVED;
    cdb[7] = partitionNumber;
    cdb[8] = RESERVED;
    cdb[9] = RESERVED;
    cdb[10] = M_Byte3(parameterListLength);
    cdb[11] = M_Byte2(parameterListLength);
    cdb[12] = M_Byte1(parameterListLength);
    cdb[13] = M_Byte0(parameterListLength);
    cdb[14] = RESERVED;
    cdb[15] = 0;//control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, parameterListLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Attribute", ret);
    }
    return ret;
}

int scsi_Compare_And_Write(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, uint64_t logicalBlockAddress, uint8_t numberOfLogicalBlocks, uint8_t groupNumber, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Compare And Write\n");
    }

    cdb[OPERATION_CODE] = COMPARE_AND_WRITE;
    cdb[1] = (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = RESERVED;
    cdb[11] = RESERVED;
    cdb[12] = RESERVED;
    cdb[13] = numberOfLogicalBlocks;
    cdb[14] = groupNumber & 0x1F;
    cdb[15] = 0;//control
    
    //send the command
    if (numberOfLogicalBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Compare And Write", ret);
    }
    return ret;
}

int scsi_Format_Unit(tDevice *device, uint8_t fmtpInfo, bool longList, bool fmtData, bool cmplst, uint8_t defectListFormat, uint8_t vendorSpecific, uint8_t *ptrData, uint32_t dataSize, uint8_t ffmt, uint32_t timeoutSeconds)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Format Unit\n");
    }

    if (!ptrData && fmtData)
    {
        return BAD_PARAMETER;
    }

    cdb[OPERATION_CODE] = SCSI_FORMAT_UNIT_CMD;
    cdb[1] = (fmtpInfo & 0x03) << 6;
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
    cdb[1] |= (defectListFormat & 0x07);
    cdb[2] = vendorSpecific;
    cdb[3] = RESERVED;//used to be marked obsolete
    cdb[4] = ffmt & 0x03;//used to be marked obsolete
    cdb[5] = 0;//control

    //send the command
    if (fmtData)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, timeoutSeconds);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Format Unit", ret);
    }
    return ret;
}

int scsi_Get_Lba_Status(tDevice *device, uint64_t logicalBlockAddress, uint32_t allocationLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Get LBA Status\n");
    }

    cdb[OPERATION_CODE] = GET_LBA_STATUS;
    cdb[1] = 0x12;
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    cdb[14] = RESERVED;
    cdb[15] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get LBA Status", ret);
    }
    return ret;
}

int scsi_Orwrite_16(tDevice *device, uint8_t orProtect, bool dpo, bool fua, uint64_t logicalBlockAddress, uint32_t transferLengthBlocks, uint8_t groupNumber, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI ORWrite 16\n");
    }

    cdb[OPERATION_CODE] = ORWRITE_16;
    cdb[1] = (orProtect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(transferLengthBlocks);
    cdb[11] = M_Byte2(transferLengthBlocks);
    cdb[12] = M_Byte1(transferLengthBlocks);
    cdb[13] = M_Byte0(transferLengthBlocks);
    cdb[14] = groupNumber & 0x1F;
    cdb[15] = 0;//control

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("ORWrite 16", ret);
    }
    return ret;
}

int scsi_Orwrite_32(tDevice *device, uint8_t bmop, uint8_t previousGenProcessing, uint8_t groupNumber, uint8_t orProtect, bool dpo, bool fua, uint64_t logicalBlockAddress, uint32_t expectedORWgen, uint32_t newORWgen, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_32] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI ORWrite 32\n");
    }

    cdb[OPERATION_CODE] = ORWRITE_32;
    cdb[1] = 0;//control
    cdb[2] = bmop & 0x07;
    cdb[3] = previousGenProcessing & 0x0F;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = groupNumber & 0x1F;
    cdb[7] = 0x18;//additional CDB length. This is defined as this value in the spec
    cdb[8] = 0x00;//service action
    cdb[9] = 0x0E;//service action
    cdb[10] = (orProtect & 0x07) << 5;
    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    if (fua)
    {
        cdb[10] |= BIT3;
    }
    cdb[11] = RESERVED;
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = M_Byte3(expectedORWgen);
    cdb[21] = M_Byte2(expectedORWgen);
    cdb[22] = M_Byte1(expectedORWgen);
    cdb[23] = M_Byte0(expectedORWgen);
    cdb[24] = M_Byte3(newORWgen);
    cdb[25] = M_Byte2(newORWgen);
    cdb[26] = M_Byte1(newORWgen);
    cdb[27] = M_Byte0(newORWgen);
    cdb[28] = M_Byte3(transferLengthBlocks);
    cdb[29] = M_Byte2(transferLengthBlocks);
    cdb[30] = M_Byte1(transferLengthBlocks);
    cdb[31] = M_Byte0(transferLengthBlocks);
    
    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("ORWrite 32", ret);
    }
    return ret;
}

int scsi_Prefetch_10(tDevice *device, bool immediate, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t prefetchLength)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

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
    cdb[6] = groupNumber & 0x1F;
    cdb[7] = M_Byte1(prefetchLength);
    cdb[8] = M_Byte0(prefetchLength);
    cdb[9] = 0;//control
    
    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Pre-Fetch 10", ret);
    }
    return ret;
}

int scsi_Prefetch_16(tDevice *device, bool immediate, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t prefetchLength)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Pre-Fetch 16\n");
    }

    cdb[OPERATION_CODE] = PRE_FETCH_16;
    if (immediate)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(prefetchLength);
    cdb[11] = M_Byte2(prefetchLength);
    cdb[12] = M_Byte1(prefetchLength);
    cdb[13] = M_Byte0(prefetchLength);
    cdb[14] = groupNumber & 0x1F;
    cdb[15] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Pre-Fetch 16", ret);
    }
    return ret;
}

int scsi_Prevent_Allow_Medium_Removal(tDevice *device, uint8_t prevent)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };
    
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Prevent Allow Medium Removal\n");
    }

    cdb[OPERATION_CODE] = PREVENT_ALLOW_MEDIUM_REMOVAL;
    cdb[1] = RESERVED;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = prevent & 0x03;
    cdb[5] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Prevent Allow Medium Removal", ret);
    }
    return ret;
}

int scsi_Read_6(tDevice *device, uint32_t logicalBlockAddress, uint8_t transferLengthBlocks, uint8_t* ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

    if (!ptrData && transferLengthBlocks == 0)
    {
        //In read 6, transferlengthBlocks is zero, then we are reading 256 sectors of data, so we need to say this is a bad parameter combination!!!
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 6\n");
    }

    cdb[OPERATION_CODE] = READ6;
    cdb[1] = M_Byte2(logicalBlockAddress) & 0x1F;
    cdb[2] = M_Byte1(logicalBlockAddress);
    cdb[3] = M_Byte0(logicalBlockAddress);
    cdb[4] = transferLengthBlocks;
    cdb[5] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 6", ret);
    }
    return ret;
}

int scsi_Read_10(tDevice *device, uint8_t rdProtect, bool dpo, bool fua, bool rarc, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 10\n");
    }

    cdb[OPERATION_CODE] = READ10;
    cdb[1] = (rdProtect & 0x07) << 5;
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
    cdb[6] = groupNumber & 0x1F;
    cdb[7] = M_Byte1(transferLengthBlocks);
    cdb[8] = M_Byte0(transferLengthBlocks);
    cdb[9] = 0;//control

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 10", ret);
    }
    return ret;
}

int scsi_Read_12(tDevice *device, uint8_t rdProtect, bool dpo, bool fua, bool rarc, uint32_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 12\n");
    }

    cdb[OPERATION_CODE] = READ12;
    cdb[1] = (rdProtect & 0x07) << 5;
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
    cdb[6] = M_Byte3(transferLengthBlocks);
    cdb[7] = M_Byte2(transferLengthBlocks);
    cdb[8] = M_Byte1(transferLengthBlocks);
    cdb[9] = M_Byte0(transferLengthBlocks);
    cdb[10] = groupNumber & 0x1F;
    cdb[11] = 0;//control

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 12", ret);
    }
    return ret;
}

int scsi_Read_16(tDevice *device, uint8_t rdProtect, bool dpo, bool fua, bool rarc, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 16\n");
    }

    cdb[OPERATION_CODE] = READ16;
    cdb[1] = (rdProtect & 0x07) << 5;
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
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(transferLengthBlocks);
    cdb[11] = M_Byte2(transferLengthBlocks);
    cdb[12] = M_Byte1(transferLengthBlocks);
    cdb[13] = M_Byte0(transferLengthBlocks);
    cdb[14] = groupNumber & 0x1F;
    cdb[15] = 0;//control

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 16", ret);
    }
    return ret;
}

int scsi_Read_32(tDevice *device, uint8_t rdProtect, bool dpo, bool fua, bool rarc, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t expectedInitialLogicalBlockRefTag, uint16_t expectedLogicalBlockAppTag, uint16_t logicalBlockAppTagMask, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_32] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read 32\n");
    }

    cdb[OPERATION_CODE] = READ32;
    cdb[1] = 0;//control
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = groupNumber & 0x1F;
    cdb[7] = 0x18;//additional cdb length
    cdb[8] = 0x00;//service action MSB
    cdb[9] = 0x09;//service action LSB
    cdb[10] = (rdProtect & 0x07) << 5;
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
    cdb[11] = RESERVED;
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = M_Byte3(expectedInitialLogicalBlockRefTag);
    cdb[21] = M_Byte2(expectedInitialLogicalBlockRefTag);
    cdb[22] = M_Byte1(expectedInitialLogicalBlockRefTag);
    cdb[23] = M_Byte0(expectedInitialLogicalBlockRefTag);
    cdb[24] = M_Byte1(expectedLogicalBlockAppTag);
    cdb[25] = M_Byte0(expectedLogicalBlockAppTag);
    cdb[26] = M_Byte1(logicalBlockAppTagMask);
    cdb[27] = M_Byte0(logicalBlockAppTagMask);
    cdb[28] = M_Byte3(transferLengthBlocks);
    cdb[29] = M_Byte2(transferLengthBlocks);
    cdb[30] = M_Byte1(transferLengthBlocks);
    cdb[31] = M_Byte0(transferLengthBlocks);
    
    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read 32", ret);
    }
    return ret;
}

int scsi_Read_Defect_Data_10(tDevice *device, bool requestPList, bool requestGList, uint8_t defectListFormat, uint16_t allocationLength, uint8_t* ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Defect Data 10\n");
    }

    cdb[OPERATION_CODE] = READ_DEFECT_DATA_10_CMD;
    cdb[1] = RESERVED;
    if (requestPList)
    {
        cdb[2] |= BIT4;
    }
    if (requestGList)
    {
        cdb[2] |= BIT3;
    }
    cdb[2] |= defectListFormat & 0x07;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    cdb[9] = 0;//control
    
    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Defect Data 10", ret);
    }
    return ret;
}

int scsi_Read_Defect_Data_12(tDevice *device, bool requestPList, bool requestGList, uint8_t defectListFormat, uint32_t addressDescriptorIndex, uint32_t allocationLength, uint8_t* ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

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
    cdb[1] |= defectListFormat & 0x07;
    cdb[2] = M_Byte3(addressDescriptorIndex);
    cdb[3] = M_Byte2(addressDescriptorIndex);
    cdb[4] = M_Byte1(addressDescriptorIndex);
    cdb[5] = M_Byte0(addressDescriptorIndex);
    cdb[6] = M_Byte3(allocationLength);
    cdb[7] = M_Byte2(allocationLength);
    cdb[8] = M_Byte1(allocationLength);
    cdb[9] = M_Byte0(allocationLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Defect Data 12", ret);
    }
    return ret;
}

int scsi_Read_Long_10(tDevice *device, bool physicalBlock, bool correctBit, uint32_t logicalBlockAddress, uint16_t byteTransferLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

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
    cdb[9] = 0;//control

    //send the command
    if (byteTransferLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, byteTransferLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Long 10", ret);
    }
    return ret;
}

int scsi_Read_Long_16(tDevice *device, bool physicalBlock, bool correctBit, uint64_t logicalBlockAddress, uint16_t byteTransferLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Read Long 16\n");
    }

    cdb[OPERATION_CODE] = READ_LONG_16;
    cdb[1] = 0x11;//service action
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = RESERVED;
    cdb[11] = RESERVED;
    cdb[12] = M_Byte1(byteTransferLength);
    cdb[13] = M_Byte0(byteTransferLength);
    if (physicalBlock)
    {
        cdb[14] |= BIT1;
    }
    if (correctBit)
    {
        cdb[14] |= BIT0;
    }
    cdb[15] = 0;//control

    //send the command
    if (byteTransferLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, byteTransferLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Read Long 16", ret);
    }
    return ret;
}

int scsi_Reassign_Blocks(tDevice *device, bool longLBA, bool longList, uint32_t dataSize, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

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
    cdb[5] = 0;//control
    

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Reassign Blocks", ret);
    }
    return ret;
}

int scsi_Report_Referrals(tDevice *device, uint64_t logicalBlockAddress, uint32_t allocationLength, bool one_seg, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Report Referrals\n");
    }

    cdb[OPERATION_CODE] = REPORT_REFERRALS;
    cdb[1] = 0x13;//service action
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    if (one_seg)
    {
        cdb[14] |= BIT0;
    }
    cdb[15] = 0;//control

    //send the command
    if (allocationLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Report Referrals", ret);
    }
    return ret;
}

int scsi_Start_Stop_Unit(tDevice *device, bool immediate, uint8_t powerConditionModifier, uint8_t powerCondition, bool noFlush, bool loej, bool start)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

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
    cdb[3] |= powerConditionModifier & 0x0F;
    cdb[4] |= (powerCondition & 0x0F) << 4;
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
        cdb[5] |= BIT0;
    }
    cdb[5] = 0;//control
    
    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Start Stop Unit", ret);
    }
    return ret;
}

int scsi_Synchronize_Cache_10(tDevice *device, bool immediate, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t numberOfLogicalBlocks)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

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
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = M_Byte1(numberOfLogicalBlocks);
    cdb[8] = M_Byte0(numberOfLogicalBlocks);
    cdb[9] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Synchronize Cache 10", ret);
    }
    return ret;
}

int scsi_Synchronize_Cache_16(tDevice *device, bool immediate, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t numberOfLogicalBlocks)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Synchronize Cache 16\n");
    }

    cdb[OPERATION_CODE] = SYNCHRONIZE_CACHE_16_CMD;
    if (immediate)
    {
        cdb[1] |= BIT1;
    }
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(numberOfLogicalBlocks);
    cdb[11] = M_Byte2(numberOfLogicalBlocks);
    cdb[12] = M_Byte1(numberOfLogicalBlocks);
    cdb[13] = M_Byte0(numberOfLogicalBlocks);
    cdb[14] |= groupNumber & 0x1F;
    cdb[15] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Synchronize Cache 16", ret);
    }
    return ret;
}

int scsi_Unmap(tDevice *device, bool anchor, uint8_t groupNumber, uint16_t parameterListLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

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
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    cdb[9] = 0;//control

    //send the command
    if (parameterListLength > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, parameterListLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Unmap", ret);
    }
    return ret;
}

int scsi_Verify_10(tDevice *device, uint8_t vrprotect, bool dpo, uint8_t byteCheck, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t verificationLength, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 10\n");
    }

    cdb[OPERATION_CODE] = VERIFY10;
    cdb[1] |= (vrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= (byteCheck & 0x03) << 1;
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = M_Byte1(verificationLength);
    cdb[8] = M_Byte0(verificationLength);
    cdb[9] = 0;//control

    //if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 10", ret);
    }
    return ret;
}

int scsi_Verify_12(tDevice *device, uint8_t vrprotect, bool dpo, uint8_t byteCheck, uint32_t logicalBlockAddress, uint8_t groupNumber, uint32_t verificationLength, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 12\n");
    }

    cdb[OPERATION_CODE] = VERIFY12;
    cdb[1] |= (vrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= (byteCheck & 0x03) << 1;
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = M_Byte3(verificationLength);
    cdb[7] = M_Byte2(verificationLength);
    cdb[8] = M_Byte1(verificationLength);
    cdb[9] = M_Byte0(verificationLength);
    cdb[10] |= groupNumber & 0x1F;
    cdb[11] = 0;//control

    //if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 12", ret);
    }
    return ret;
}

int scsi_Verify_16(tDevice *device, uint8_t vrprotect, bool dpo, uint8_t byteCheck, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t verificationLength, uint8_t *ptrData, uint32_t dataSize)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 16\n");
    }

    cdb[OPERATION_CODE] = VERIFY16;
    cdb[1] |= (vrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= (byteCheck & 0x03) << 1;
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(verificationLength);
    cdb[11] = M_Byte2(verificationLength);
    cdb[12] = M_Byte1(verificationLength);
    cdb[13] = M_Byte0(verificationLength);
    cdb[14] |= groupNumber & 0x1F;
    cdb[15] = 0;//control

    //if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 16", ret);
    }
    return ret;
}

int scsi_Verify_32(tDevice *device, uint8_t vrprotect, bool dpo, uint8_t byteCheck, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t verificationLength, uint8_t *ptrData, uint32_t dataSize, uint32_t expectedInitialLogicalBlockRefTag, uint16_t expectedLogicalBlockAppTag, uint16_t logicalBlockAppTagMask)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_32] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Verify 32\n");
    }

    cdb[OPERATION_CODE] = VERIFY32;
    cdb[1] = 0;//control
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = 0x18;//additional CDB length
    cdb[8] = 0x00;//service action MSB
    cdb[9] = 0x0A;//service action LSB
    cdb[10] |= (vrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    cdb[10] |= (byteCheck & 0x03) << 1;
    cdb[11] = RESERVED;
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = M_Byte3(expectedInitialLogicalBlockRefTag);
    cdb[21] = M_Byte2(expectedInitialLogicalBlockRefTag);
    cdb[22] = M_Byte1(expectedInitialLogicalBlockRefTag);
    cdb[23] = M_Byte0(expectedInitialLogicalBlockRefTag);
    cdb[24] = M_Byte1(expectedLogicalBlockAppTag);
    cdb[25] = M_Byte0(expectedLogicalBlockAppTag);
    cdb[26] = M_Byte1(logicalBlockAppTagMask);
    cdb[27] = M_Byte0(logicalBlockAppTagMask);
    cdb[28] = M_Byte3(verificationLength);
    cdb[29] = M_Byte2(verificationLength);
    cdb[30] = M_Byte1(verificationLength);
    cdb[31] = M_Byte0(verificationLength);

    //if byteCheck is set to 00b or 10b, then no data is transfered according to spec....not sure if this check should be here of it should always say data out even when the transfer wont occur-TJE
    if (((byteCheck & 0x03) == 0 || (byteCheck & 0x03) == 0x02) || verificationLength == 0)
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, dataSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Verify 32", ret);
    }
    return ret;
}

int scsi_Write_6(tDevice *device, uint32_t logicalBlockAddress, uint8_t transferLengthBlocks, uint8_t* ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_6] = { 0 };

    if (!ptrData && transferLengthBlocks == 0)
    {
        //In write 6, transferlengthBlocks is zero, then we are reading 256 sectors of data, so we need to say this is a bad parameter combination!!!
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write 6\n");
    }

    cdb[OPERATION_CODE] = WRITE6;
    cdb[1] = M_Byte2(logicalBlockAddress) & 0x1F;
    cdb[2] = M_Byte1(logicalBlockAddress);
    cdb[3] = M_Byte0(logicalBlockAddress);
    cdb[4] = transferLengthBlocks;
    cdb[5] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 6", ret);
    }
    return ret;
}

int scsi_Write_10(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write 10\n");
    }

    cdb[OPERATION_CODE] = WRITE10;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = M_Byte1(transferLengthBlocks);
    cdb[8] = M_Byte0(transferLengthBlocks);
    cdb[9] = 0;//control
    
    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 10", ret);
    }
    return ret;
}

int scsi_Write_12(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, uint32_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write 12\n");
    }

    cdb[OPERATION_CODE] = WRITE12;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = M_Byte3(transferLengthBlocks);
    cdb[7] = M_Byte2(transferLengthBlocks);
    cdb[8] = M_Byte1(transferLengthBlocks);
    cdb[9] = M_Byte0(transferLengthBlocks);
    cdb[10] |= groupNumber & 0x1F;
    cdb[11] = 0;//control

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 12", ret);
    }
    return ret;
}

int scsi_Write_16(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write 16\n");
    }

    cdb[OPERATION_CODE] = WRITE16;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    if (fua)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(transferLengthBlocks);
    cdb[11] = M_Byte2(transferLengthBlocks);
    cdb[12] = M_Byte1(transferLengthBlocks);
    cdb[13] = M_Byte0(transferLengthBlocks);
    cdb[14] |= groupNumber & 0x1F;
    cdb[15] = 0;//control

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 16", ret);
    }
    return ret;
}

int scsi_Write_32(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t expectedInitialLogicalBlockRefTag, uint16_t expectedLogicalBlockAppTag, uint16_t logicalBlockAppTagMask, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_32] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Write 32\n");
    }

    cdb[OPERATION_CODE] = WRITE32;
    cdb[1] = 0;//control
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = 0x18;//additional CDB length
    cdb[8] = 0x00;//service action MSB
    cdb[9] = 0x0B;//service action LSB
    cdb[10] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    if (fua)
    {
        cdb[10] |= BIT3;
    }
    cdb[11] = RESERVED;
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = M_Byte3(expectedInitialLogicalBlockRefTag);
    cdb[21] = M_Byte2(expectedInitialLogicalBlockRefTag);
    cdb[22] = M_Byte1(expectedInitialLogicalBlockRefTag);
    cdb[23] = M_Byte0(expectedInitialLogicalBlockRefTag);
    cdb[24] = M_Byte1(expectedLogicalBlockAppTag);
    cdb[25] = M_Byte0(expectedLogicalBlockAppTag);
    cdb[26] = M_Byte1(logicalBlockAppTagMask);
    cdb[27] = M_Byte0(logicalBlockAppTagMask);
    cdb[28] = M_Byte3(transferLengthBlocks);
    cdb[29] = M_Byte2(transferLengthBlocks);
    cdb[30] = M_Byte1(transferLengthBlocks);
    cdb[31] = M_Byte0(transferLengthBlocks);

    //send the command
    if (transferLengthBlocks > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write 32", ret);
    }
    return ret;
}

int scsi_Write_And_Verify_10(tDevice *device, uint8_t wrprotect, bool dpo, uint8_t byteCheck, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write and Verify 10\n");
    }

    cdb[OPERATION_CODE] = WRITE_AND_VERIFY_10;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= (byteCheck & 0x03) << 1;
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = M_Byte1(transferLengthBlocks);
    cdb[8] = M_Byte0(transferLengthBlocks);
    cdb[9] = 0;//control

    //send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 10", ret);
    }
    return ret;
}

int scsi_Write_And_Verify_12(tDevice *device, uint8_t wrprotect, bool dpo, uint8_t byteCheck, uint32_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_12] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write and Verify 12\n");
    }

    cdb[OPERATION_CODE] = WRITE_AND_VERIFY_12;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= (byteCheck & 0x03) << 1;
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = M_Byte3(transferLengthBlocks);
    cdb[7] = M_Byte2(transferLengthBlocks);
    cdb[8] = M_Byte1(transferLengthBlocks);
    cdb[9] = M_Byte0(transferLengthBlocks);
    cdb[10] |= groupNumber & 0x1F;
    cdb[11] = 0;//control

    //send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 12", ret);
    }
    return ret;
}

int scsi_Write_And_Verify_16(tDevice *device, uint8_t wrprotect, bool dpo, uint8_t byteCheck, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Write and Verify 16\n");
    }

    cdb[OPERATION_CODE] = WRITE_AND_VERIFY_16;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[1] |= BIT4;
    }
    cdb[1] |= (byteCheck & 0x03) << 1;
    cdb[6] = M_Byte7(logicalBlockAddress);
    cdb[6] = M_Byte6(logicalBlockAddress);
    cdb[6] = M_Byte5(logicalBlockAddress);
    cdb[6] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(transferLengthBlocks);
    cdb[11] = M_Byte2(transferLengthBlocks);
    cdb[12] = M_Byte1(transferLengthBlocks);
    cdb[13] = M_Byte0(transferLengthBlocks);
    cdb[14] |= groupNumber & 0x1F;
    cdb[15] = 0;//control

    //send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 16", ret);
    }
    return ret;
}

int scsi_Write_And_Verify_32(tDevice *device, uint8_t wrprotect, bool dpo, uint8_t byteCheck, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLengthBlocks, uint8_t *ptrData, uint32_t expectedInitialLogicalBlockRefTag, uint16_t expectedLogicalBlockAppTag, uint16_t logicalBlockAppTagMask, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_32] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending Write and Verify 32\n");
    }

    cdb[OPERATION_CODE] = WRITE_AND_VERIFY_32;
    cdb[1] = 0;//control
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] |= groupNumber & 0x1F;
    cdb[7] = 0x18;//additional CDB length
    cdb[8] = 0x00;//service action MSB
    cdb[9] = 0x0C;//service action LSB
    cdb[10] |= (wrprotect & 0x07) << 5;
    if (dpo)
    {
        cdb[10] |= BIT4;
    }
    cdb[10] |= (byteCheck & 0x03) << 1;
    cdb[11] = RESERVED;
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = M_Byte3(expectedInitialLogicalBlockRefTag);
    cdb[21] = M_Byte2(expectedInitialLogicalBlockRefTag);
    cdb[22] = M_Byte1(expectedInitialLogicalBlockRefTag);
    cdb[23] = M_Byte0(expectedInitialLogicalBlockRefTag);
    cdb[24] = M_Byte1(expectedLogicalBlockAppTag);
    cdb[25] = M_Byte0(expectedLogicalBlockAppTag);
    cdb[26] = M_Byte1(logicalBlockAppTagMask);
    cdb[27] = M_Byte0(logicalBlockAppTagMask);
    cdb[28] = M_Byte3(transferLengthBlocks);
    cdb[29] = M_Byte2(transferLengthBlocks);
    cdb[30] = M_Byte1(transferLengthBlocks);
    cdb[31] = M_Byte0(transferLengthBlocks);

    //send the command
    if (transferLengthBytes > 0)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write and Verify 32", ret);
    }
    return ret;
}

int scsi_Write_Long_10(tDevice *device, bool correctionDisabled, bool writeUncorrectable, bool physicalBlock, uint32_t logicalBlockAddress, uint16_t byteTransferLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Long 10\n");
    }

    cdb[OPERATION_CODE] = WRITE_LONG_10_CMD;
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
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = RESERVED;
    cdb[7] = M_Byte1(byteTransferLength);
    cdb[8] = M_Byte0(byteTransferLength);
    cdb[9] = 0;//control

    //send the command
    if (ptrData)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, byteTransferLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Long 10", ret);
    }
    return ret;
}

int scsi_Write_Long_16(tDevice *device, bool correctionDisabled, bool writeUncorrectable, bool physicalBlock, uint64_t logicalBlockAddress, uint16_t byteTransferLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Long 16\n");
    }

    cdb[OPERATION_CODE] = WRITE_LONG_16_CMD;
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
    cdb[1] |= 0x11;//service action
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = RESERVED;
    cdb[11] = RESERVED;
    cdb[12] = M_Byte1(byteTransferLength);
    cdb[13] = M_Byte0(byteTransferLength);
    cdb[14] = RESERVED;
    cdb[15] = 0;//control

    //send the command
    if (ptrData)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, byteTransferLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Long 16", ret);
    }
    return ret;
}

int scsi_Write_Same_10(tDevice *device, uint8_t wrprotect, bool anchor, bool unmap, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t numberOfLogicalBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (ptrData == NULL)//write Same 10 requires a data transfer
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Same 10\n");
    }

    cdb[OPERATION_CODE] = WRITE_SAME_10_CMD;
    cdb[1] |= (wrprotect & 0x07) << 5;
    if (anchor)
    {
        cdb[1] |= BIT4;
    }
    if (unmap)
    {
        cdb[1] |= BIT3;
    }
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = groupNumber & 0x0F;
    cdb[7] = M_Byte1(numberOfLogicalBlocks);
    cdb[8] = M_Byte0(numberOfLogicalBlocks);
    cdb[9] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same 10", ret);
    }
    return ret;
}

int scsi_Write_Same_16(tDevice *device, uint8_t wrprotect, bool anchor, bool unmap, bool noDataOut, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t numberOfLogicalBlocks, uint8_t *ptrData, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_16] = { 0 };

    if (ptrData == NULL && !noDataOut)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Same 16\n");
    }

    cdb[OPERATION_CODE] = WRITE_SAME_16_CMD;
    cdb[1] |= (wrprotect & 0x07) << 5;
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
    cdb[2] = M_Byte7(logicalBlockAddress);
    cdb[3] = M_Byte6(logicalBlockAddress);
    cdb[4] = M_Byte5(logicalBlockAddress);
    cdb[5] = M_Byte4(logicalBlockAddress);
    cdb[6] = M_Byte3(logicalBlockAddress);
    cdb[7] = M_Byte2(logicalBlockAddress);
    cdb[8] = M_Byte1(logicalBlockAddress);
    cdb[9] = M_Byte0(logicalBlockAddress);
    cdb[10] = M_Byte3(numberOfLogicalBlocks);
    cdb[11] = M_Byte2(numberOfLogicalBlocks);
    cdb[12] = M_Byte1(numberOfLogicalBlocks);
    cdb[13] = M_Byte0(numberOfLogicalBlocks);
    cdb[14] = groupNumber & 0x0F;
    cdb[15] = 0;//control

    if (noDataOut)
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLengthBytes, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write Same 16", ret);
    }
    return ret;
}

int scsi_Write_Same_32(tDevice *device, uint8_t wrprotect, bool anchor, bool unmap, bool noDataOut, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t numberOfLogicalBlocks, uint8_t *ptrData, uint32_t expectedInitialLogicalBlockRefTag, uint16_t expectedLogicalBlockAppTag, uint16_t logicalBlockAppTagMask, uint32_t transferLengthBytes)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_32] = { 0 };

    if (ptrData == NULL && !noDataOut)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Write Same 32\n");
    }

    cdb[OPERATION_CODE] = WRITE_SAME_32_CMD;
    cdb[1] = 0;//control
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = groupNumber & 0x0F;
    cdb[7] = 0x18;//additional cdb length
    cdb[8] = 0x00;//service action MSB
    cdb[9] = 0x0D;//service action LSB
    cdb[10] |= (wrprotect & 0x07) << 5;
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
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = M_Byte3(expectedInitialLogicalBlockRefTag);
    cdb[21] = M_Byte2(expectedInitialLogicalBlockRefTag);
    cdb[22] = M_Byte1(expectedInitialLogicalBlockRefTag);
    cdb[23] = M_Byte0(expectedInitialLogicalBlockRefTag);
    cdb[24] = M_Byte1(expectedLogicalBlockAppTag);
    cdb[25] = M_Byte0(expectedLogicalBlockAppTag);
    cdb[26] = M_Byte1(logicalBlockAppTagMask);
    cdb[27] = M_Byte0(logicalBlockAppTagMask);
    cdb[28] = M_Byte3(numberOfLogicalBlocks);
    cdb[29] = M_Byte2(numberOfLogicalBlocks);
    cdb[30] = M_Byte1(numberOfLogicalBlocks);
    cdb[31] = M_Byte0(numberOfLogicalBlocks);

    if (!noDataOut)
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, device->drive_info.deviceBlockSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        //send the command
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
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
//!   \param ptrDataOut - pointer to the data out buffer. Must be non-NULL
//!   \param ptrDataIn - pointer to the data in buffer. Must be non-NULL
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
//int scsi_xd_Write_Read_10(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, bool disableWrite, bool xoprinfo, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t transferLength, uint8_t *ptrDataOut, uint8_t *ptrDataIn)
//{
//    int       ret = FAILURE;
//    uint8_t   cdb[CDB_LEN_10] = { 0 };
//    ScsiIoCtx scsiIoCtx;
//    memset(&scsiIoCtx, 0, sizeof(ScsiIoCtx));
//
//    if (ptrDataOut == NULL || ptrDataIn == NULL)
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
//    cdb[1] |= (wrprotect & 0x07) << 5;
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
//    cdb[2] = (uint8_t)(logicalBlockAddress >> 24);
//    cdb[3] = (uint8_t)(logicalBlockAddress >> 16);
//    cdb[4] = (uint8_t)(logicalBlockAddress >> 8);
//    cdb[5] = (uint8_t)logicalBlockAddress;
//    cdb[6] = groupNumber & 0x1F;
//    cdb[7] = (uint8_t)(transferLength >> 8);
//    cdb[8] = (uint8_t)transferLength;
//    cdb[9] = 0;//control
//
//    // Set up the CTX
//    scsiIoCtx.device = device;
//    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);
//    scsiIoCtx.psense = device->drive_info.lastCommandSenseData;
//    scsiIoCtx.senseDataSize = SPC3_SENSE_LEN;
//    memcpy(&scsiIoCtx.cdb[0], &cdb[0], sizeof(cdb));
//    scsiIoCtx.cdbLength = sizeof(cdb);
//    scsiIoCtx.direction = XFER_DATA_OUT_IN;
//    //set the buffer info in the bidirectional command structure
//    scsiIoCtx.biDirectionalBuffers.dataInBuffer = ptrDataIn;
//    scsiIoCtx.biDirectionalBuffers.dataInBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.biDirectionalBuffers.dataOutBuffer = ptrDataOut;
//    scsiIoCtx.biDirectionalBuffers.dataOutBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.verbose = 0;
//
//    //while this command is all typed up the lower level windows or linux passthrough code needs some work before this command is actually ready to be used
//    return NOT_SUPPORTED;
//    ret = send_IO(&scsiIoCtx);
//    get_Sense_Key_ACQ_ACSQ(device->drive_info.lastCommandSenseData, &scsiIoCtx.returnStatus.senseKey, &scsiIoCtx.returnStatus.asc, &scsiIoCtx.returnStatus.ascq);
//    ret = check_Sense_Key_ACQ_And_ACSQ(scsiIoCtx.returnStatus.senseKey, scsiIoCtx.returnStatus.asc, scsiIoCtx.returnStatus.ascq);
//    print_Return_Enum("XD Write Read 10", ret);
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
//!   \param ptrDataOut - pointer to the data out buffer. Must be non-NULL
//!   \param ptrDataIn - pointer to the data in buffer. Must be non-NULL
//!
//  Exit:
//!   \return SUCCESS = pass, !SUCCESS = something when wrong
//
//-----------------------------------------------------------------------------
//int scsi_xd_Write_Read_32(tDevice *device, uint8_t wrprotect, bool dpo, bool fua, bool disableWrite, bool xoprinfo, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLength, uint8_t *ptrDataOut, uint8_t *ptrDataIn)
//{
//    int       ret = FAILURE;
//    uint8_t   cdb[CDB_LEN_32] = { 0 };
//    ScsiIoCtx scsiIoCtx;
//    memset(&scsiIoCtx, 0, sizeof(ScsiIoCtx));
//
//    if (ptrDataOut == NULL || ptrDataIn == NULL)
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
//    cdb[6] = groupNumber & 0x1F;
//    cdb[7] = 0x18;//additional CDB length
//    cdb[8] = 0x00;//service action MSB
//    cdb[9] = 0x07;//service action LSB
//    cdb[10] |= (wrprotect & 0x07) << 5;
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
//    cdb[12] = (uint8_t)(logicalBlockAddress >> 56);
//    cdb[13] = (uint8_t)(logicalBlockAddress >> 48);
//    cdb[14] = (uint8_t)(logicalBlockAddress >> 40);
//    cdb[15] = (uint8_t)(logicalBlockAddress >> 32);
//    cdb[16] = (uint8_t)(logicalBlockAddress >> 24);
//    cdb[17] = (uint8_t)(logicalBlockAddress >> 16);
//    cdb[18] = (uint8_t)(logicalBlockAddress >> 8);
//    cdb[19] = (uint8_t)logicalBlockAddress;
//    cdb[20] = RESERVED;
//    cdb[21] = RESERVED;
//    cdb[22] = RESERVED;
//    cdb[23] = RESERVED;
//    cdb[24] = RESERVED;
//    cdb[25] = RESERVED;
//    cdb[26] = RESERVED;
//    cdb[27] = RESERVED;
//    cdb[28] = (uint8_t)(transferLength >> 24);
//    cdb[29] = (uint8_t)(transferLength >> 16);
//    cdb[30] = (uint8_t)(transferLength >> 8);
//    cdb[31] = (uint8_t)transferLength;
//
//    // Set up the CTX
//    scsiIoCtx.device = device;
//    memset(device->drive_info.lastCommandSenseData, 0, SPC3_SENSE_LEN);
//    scsiIoCtx.psense = device->drive_info.lastCommandSenseData;
//    scsiIoCtx.senseDataSize = SPC3_SENSE_LEN;
//    memcpy(&scsiIoCtx.cdb[0], &cdb[0], sizeof(cdb));
//    scsiIoCtx.cdbLength = sizeof(cdb);
//    scsiIoCtx.direction = XFER_DATA_OUT_IN;
//    //set the buffer info in the bidirectional command structure
//    scsiIoCtx.biDirectionalBuffers.dataInBuffer = ptrDataIn;
//    scsiIoCtx.biDirectionalBuffers.dataInBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.biDirectionalBuffers.dataOutBuffer = ptrDataOut;
//    scsiIoCtx.biDirectionalBuffers.dataOutBufferSize = transferLength * device->drive_info.deviceBlockSize;
//    scsiIoCtx.verbose = 0;
//
//    //while this command is all typed up the lower level windows or linux passthrough code needs some work before this command is actually ready to be used
//    return NOT_SUPPORTED;
//    ret = send_IO(&scsiIoCtx);
//    get_Sense_Key_ACQ_ACSQ(device->drive_info.lastCommandSenseData, &scsiIoCtx.returnStatus.senseKey, &scsiIoCtx.returnStatus.asc, &scsiIoCtx.returnStatus.ascq);
//    ret = check_Sense_Key_ACQ_And_ACSQ(scsiIoCtx.returnStatus.senseKey, scsiIoCtx.returnStatus.asc, scsiIoCtx.returnStatus.ascq);
//    print_Return_Enum("XD Write Read 32", ret);
//
//    return ret;
//}

int scsi_xp_Write_10(tDevice *device, bool dpo, bool fua, bool xoprinfo, uint32_t logicalBlockAddress, uint8_t groupNumber, uint16_t transferLength, uint8_t *ptrData)
{
    int       ret = FAILURE;
    uint8_t   cdb[CDB_LEN_10] = { 0 };

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI XP Write 10\n");
    }

    cdb[OPERATION_CODE] = XPWRITE_10;
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
    cdb[2] = M_Byte3(logicalBlockAddress);
    cdb[3] = M_Byte2(logicalBlockAddress);
    cdb[4] = M_Byte1(logicalBlockAddress);
    cdb[5] = M_Byte0(logicalBlockAddress);
    cdb[6] = groupNumber & 0x1F;
    cdb[7] = M_Byte1(transferLength);
    cdb[8] = M_Byte0(transferLength);
    cdb[9] = 0;//control

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLength * device->drive_info.deviceBlockSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Write XD Write 10", ret);
    }
    return ret;
}

int scsi_xp_Write_32(tDevice *device, bool dpo, bool fua, bool xoprinfo, uint64_t logicalBlockAddress, uint8_t groupNumber, uint32_t transferLength, uint8_t *ptrData)
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_32] = { 0 };

    if (!ptrData)
    {
        return BAD_PARAMETER;
    }

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI XP Write 32\n");
    }

    cdb[OPERATION_CODE] = XPWRITE_32;
    cdb[1] = 0;//control
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = groupNumber & 0x1F;
    cdb[7] = 0x18;//additional CDB length
    cdb[8] = 0x00;//service action MSB
    cdb[9] = 0x06;//service action LSB
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
    cdb[11] = RESERVED;
    cdb[12] = M_Byte7(logicalBlockAddress);
    cdb[13] = M_Byte6(logicalBlockAddress);
    cdb[14] = M_Byte5(logicalBlockAddress);
    cdb[15] = M_Byte4(logicalBlockAddress);
    cdb[16] = M_Byte3(logicalBlockAddress);
    cdb[17] = M_Byte2(logicalBlockAddress);
    cdb[18] = M_Byte1(logicalBlockAddress);
    cdb[19] = M_Byte0(logicalBlockAddress);
    cdb[20] = RESERVED;
    cdb[21] = RESERVED;
    cdb[22] = RESERVED;
    cdb[23] = RESERVED;
    cdb[24] = RESERVED;
    cdb[25] = RESERVED;
    cdb[26] = RESERVED;
    cdb[27] = RESERVED;
    cdb[28] = M_Byte3(transferLength);
    cdb[29] = M_Byte2(transferLength);
    cdb[30] = M_Byte1(transferLength);
    cdb[31] = M_Byte0(transferLength);

    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, transferLength * device->drive_info.deviceBlockSize, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("XP Write 32", ret);
    }
    return ret;
}

int scsi_Zone_Management_In(tDevice *device, eZMAction action, uint8_t actionSpecific, uint32_t allocationLength, uint64_t actionSpecificLBA, uint8_t *ptrData)//95h
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_16] = { 0 };
    eDataTransferDirection dataDir = XFER_NO_DATA;

    switch (action)
    {
    case ZM_ACTION_REPORT_ZONES:
        dataDir = XFER_DATA_IN;
        break;
    default://Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
        break;
    }

    if (dataDir == XFER_NO_DATA)
    {
        allocationLength = 0;
    }

    cdb[OPERATION_CODE] = ZONE_MANAGEMENT_IN;
    //set the service action
    cdb[1] = action;
    //set lba field
    cdb[2] = M_Byte7(actionSpecificLBA);
    cdb[3] = M_Byte6(actionSpecificLBA);
    cdb[4] = M_Byte5(actionSpecificLBA);
    cdb[5] = M_Byte4(actionSpecificLBA);
    cdb[6] = M_Byte3(actionSpecificLBA);
    cdb[7] = M_Byte2(actionSpecificLBA);
    cdb[8] = M_Byte1(actionSpecificLBA);
    cdb[9] = M_Byte0(actionSpecificLBA);
    //allocation length
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    //action specific
    cdb[14] = actionSpecific;
    cdb[15] = 0;//control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Zone Management In\n");
    }
    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, dataDir, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management In", ret);
    }
    return ret;
}

int scsi_Zone_Management_Out(tDevice *device, eZMAction action, uint8_t actionSpecific, uint32_t allocationLength, uint64_t actionSpecificLBA, uint8_t *ptrData)//94h
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_16] = { 0 };
    eDataTransferDirection dataDir = XFER_NO_DATA;

    switch (action)
    {
    case ZM_ACTION_CLOSE_ZONE:
    case ZM_ACTION_FINISH_ZONE:
    case ZM_ACTION_OPEN_ZONE:
    case ZM_ACTION_RESET_WRITE_POINTERS:
        break;
    case ZM_ACTION_REPORT_ZONES://this is a zone management in command, so return bad parameter
    default://Need to add new zm actions as they are defined in the spec
        return BAD_PARAMETER;
        break;
    }

    if (dataDir == XFER_NO_DATA)
    {
        allocationLength = 0;
    }

    cdb[OPERATION_CODE] = ZONE_MANAGEMENT_OUT;
    //set the service action
    cdb[1] = action;
    //set lba field
    cdb[2] = M_Byte7(actionSpecificLBA);
    cdb[3] = M_Byte6(actionSpecificLBA);
    cdb[4] = M_Byte5(actionSpecificLBA);
    cdb[5] = M_Byte4(actionSpecificLBA);
    cdb[6] = M_Byte3(actionSpecificLBA);
    cdb[7] = M_Byte2(actionSpecificLBA);
    cdb[8] = M_Byte1(actionSpecificLBA);
    cdb[9] = M_Byte0(actionSpecificLBA);
    //allocation length
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    //action specific
    cdb[14] = actionSpecific;
    cdb[15] = 0;//control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Zone Management Out\n");
    }
    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, dataDir, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Zone Management Out", ret);
    }
    return ret;
}

int scsi_Close_Zone(tDevice *device, bool closeAll, uint64_t zoneID)
{
    if (closeAll)
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_CLOSE_ZONE, BIT0, RESERVED, 0, NULL);
    }
    else
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_CLOSE_ZONE, RESERVED, RESERVED, zoneID, NULL);
    }
}

int scsi_Finish_Zone(tDevice *device, bool finishAll, uint64_t zoneID)
{
    if (finishAll)
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_FINISH_ZONE, BIT0, RESERVED, 0, NULL);
    }
    else
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_FINISH_ZONE, RESERVED, RESERVED, zoneID, NULL);
    }
}

int scsi_Open_Zone(tDevice *device, bool openAll, uint64_t zoneID)
{
    if (openAll)
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_OPEN_ZONE, BIT0, RESERVED, 0, NULL);
    }
    else
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_OPEN_ZONE, RESERVED, RESERVED, zoneID, NULL);
    }
}

int scsi_Report_Zones(tDevice *device, eZoneReportingOptions reportingOptions, bool partial, uint32_t allocationLength, uint64_t zoneStartLBA, uint8_t *ptrData)
{
    uint8_t actionSpecific = reportingOptions;
    if (partial)
    {
        actionSpecific |= BIT7;
    }
    return scsi_Zone_Management_In(device, ZM_ACTION_REPORT_ZONES, actionSpecific, allocationLength, zoneStartLBA, ptrData);
}

int scsi_Reset_Write_Pointers(tDevice *device, bool resetAll, uint64_t zoneID)
{
    if (resetAll)
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_RESET_WRITE_POINTERS, BIT0, RESERVED, 0, NULL);
    }
    else
    {
        return scsi_Zone_Management_Out(device, ZM_ACTION_RESET_WRITE_POINTERS, RESERVED, RESERVED, zoneID, NULL);
    }
}

int scsi_Get_Physical_Element_Status(tDevice *device, uint32_t startingElement, uint32_t allocationLength, uint8_t filter, uint8_t reportType, uint8_t *ptrData)
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_16] = { 0 };
    cdb[OPERATION_CODE] = 0x9E;
    //set the service action
    cdb[1] = 0x17;
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    //starting element
    cdb[6] = M_Byte3(startingElement);
    cdb[7] = M_Byte2(startingElement);
    cdb[8] = M_Byte1(startingElement);
    cdb[9] = M_Byte0(startingElement);
    //allocation length
    cdb[10] = M_Byte3(allocationLength);
    cdb[11] = M_Byte2(allocationLength);
    cdb[12] = M_Byte1(allocationLength);
    cdb[13] = M_Byte0(allocationLength);
    //filter & report type bits
    cdb[14] = (filter << 6) | (reportType & 0x0F);//filter is 2 bits, report type is 4 bits. All others are reserved;
    cdb[15] = 0;//control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Get Physical Element Status\n");
    }
    eDataTransferDirection dataDir = XFER_DATA_IN;
    //send the command
    if (allocationLength == 0)
    {
        dataDir = XFER_NO_DATA;
    }
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, dataDir, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Get Physical Element Status", ret);
    }
    return ret;
}

int scsi_Remove_And_Truncate(tDevice *device, uint64_t requestedCapacity, uint32_t elementIdentifier)
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_16] = { 0 };
    cdb[OPERATION_CODE] = 0x9E;
    //set the service action
    cdb[1] = 0x18;
    //requested capacity
    cdb[2] = M_Byte3(requestedCapacity);
    cdb[3] = M_Byte3(requestedCapacity);
    cdb[4] = M_Byte3(requestedCapacity);
    cdb[5] = M_Byte3(requestedCapacity);
    cdb[6] = M_Byte3(requestedCapacity);
    cdb[7] = M_Byte2(requestedCapacity);
    cdb[8] = M_Byte1(requestedCapacity);
    cdb[9] = M_Byte0(requestedCapacity);
    //allocation length
    cdb[10] = M_Byte3(elementIdentifier);
    cdb[11] = M_Byte2(elementIdentifier);
    cdb[12] = M_Byte1(elementIdentifier);
    cdb[13] = M_Byte0(elementIdentifier);
    cdb[14] = RESERVED;
    cdb[15] = 0;//control

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Remove And Truncate\n");
    }
    //send the command
    ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Remove And Truncate", ret);
    }
    return ret;
}

int scsi_Persistent_Reserve_In(tDevice *device, uint8_t serviceAction, uint16_t allocationLength, uint8_t *ptrData)
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_10] = { 0 };
    cdb[OPERATION_CODE] = 0x5E;
    //set the service action
    cdb[1] = M_GETBITRANGE(serviceAction, 4, 0);
    //reserved
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    //allocation length
    cdb[7] = M_Byte1(allocationLength);
    cdb[8] = M_Byte0(allocationLength);
    //control
    cdb[9] = 0;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Persistent Reserve In - %" PRIu8 "\n", M_GETBITRANGE(serviceAction, 4, 0));
    }
    //send the command
    if (ptrData && allocationLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, allocationLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Persistent Reserve In", ret);
    }
    return ret;
}

int scsi_Persistent_Reserve_Out(tDevice *device, uint8_t serviceAction, uint8_t scope, uint8_t type, uint16_t parameterListLength, uint8_t *ptrData)
{
    int ret = FAILURE;
    uint8_t cdb[CDB_LEN_10] = { 0 };
    cdb[OPERATION_CODE] = 0x5E;
    //set the service action
    cdb[1] = M_GETBITRANGE(serviceAction, 4, 0);
    //scope & type
    cdb[2] = (M_Nibble0(scope) << 4) | M_Nibble0(type);
    //reserved
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = RESERVED;
    //allocation length
    cdb[7] = M_Byte1(parameterListLength);
    cdb[8] = M_Byte0(parameterListLength);
    //control
    cdb[9] = 0;

    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        printf("Sending SCSI Persistent Reserve Out - %" PRIu8 "\n", M_GETBITRANGE(serviceAction, 4, 0));
    }
    //send the command
    if (ptrData && parameterListLength)
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), ptrData, parameterListLength, XFER_DATA_OUT, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, &cdb[0], sizeof(cdb), NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
    {
        print_Return_Enum("Persistent Reserve Out", ret);
    }
    return ret;
}