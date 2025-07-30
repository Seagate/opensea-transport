// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2025-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file bsd_ata_passthrough.c issues a ata passthrough request for openbsd and netbsd

#include "common_types.h"
#include "error_translation.h"
#include "precision_timer.h"
#include "type_conversion.h"

#include "ata_helper.h"
#include "common_public.h"
#include "sat_helper_func.h"
#include "scsi_helper.h"

#include "bsd_ata_passthrough.h"

#include <sys/ataio.h>
#include <sys/ioctl.h>

#define BSD_ATA_PT_MAX_CMD_TIMEOUT_SECONDS M_STATIC_CAST(uint32_t, (INT_MAX / 1000))

static eReturnValues bsd_ata_io(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // 48bit commands are not supported by this ioctl, so filter that with OS_COMMAND_NOT_AVAILABLE
    if (scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48 != 0 || scsiIoCtx->pAtaCmdOpts->tfr.Feature48 != 0 ||
        scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48 != 0 || scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48 != 0 ||
        scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48 != 0 || scsiIoCtx->pAtaCmdOpts->tfr.aux1 != 0 ||
        scsiIoCtx->pAtaCmdOpts->tfr.aux2 != 0 || scsiIoCtx->pAtaCmdOpts->tfr.aux3 != 0 ||
        scsiIoCtx->pAtaCmdOpts->tfr.aux4 != 0)
    {
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    else
    {
        int iocret = 0;
        DECLARE_SEATIMER(commandTimer);
        atareq_t atacmd;
        safe_memset(&atacmd, sizeof(atareq_t), 0, sizeof(atareq_t));
        atacmd.flags |= ATACMD_READREG;
        switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
        {
        case XFER_NO_DATA:
            atacmd.databuf = scsiIoCtx->pAtaCmdOpts->ptrData = M_NULLPTR;
            atacmd.datalen = scsiIoCtx->pAtaCmdOpts->dataSize = 0;
            break;
        case XFER_DATA_IN:
            atacmd.flags |= ATACMD_READ;
            atacmd.databuf = scsiIoCtx->pAtaCmdOpts->ptrData;
            atacmd.datalen = scsiIoCtx->pAtaCmdOpts->dataSize;
            break;
        case XFER_DATA_OUT:
            atacmd.flags |= ATACMD_WRITE;
            atacmd.databuf = scsiIoCtx->pAtaCmdOpts->ptrData;
            atacmd.datalen = scsiIoCtx->pAtaCmdOpts->dataSize;
            break;
        case XFER_DATA_IN_OUT:
        case XFER_DATA_OUT_IN:
            return OS_COMMAND_NOT_AVAILABLE;
        }
        atacmd.command   = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
        atacmd.features  = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
        atacmd.sec_count = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
        atacmd.sec_num   = scsiIoCtx->pAtaCmdOpts->tfr.SectorNumber;
        atacmd.head =
            scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead; // TODO: Does this accept full 8bits or only 4bits for head?
        atacmd.cylinder =
            M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.CylinderHigh, scsiIoCtx->pAtaCmdOpts->tfr.CylinderLow);
#if defined(ATACMD_LBA)
        // This flag exists in NetBSD, but not openBSD, so need ifdef
        if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & LBA_MODE_BIT)
        {
            atacmd.flags |= ATACMD_LBA;
        }
#else
// TODO: Error if attempting LBA mode???
#endif // ATACMD_LBA
        uint32_t timeoutmilliseconds = UINT32_C(0);
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
            scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            timeoutmilliseconds = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
            // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security)
            // that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < BSD_ATA_PT_MAX_CMD_TIMEOUT_SECONDS)
            {
                timeoutmilliseconds *= UINT32_C(1000); // convert to milliseconds
            }
            else
            {
                timeoutmilliseconds = INT_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            if (scsiIoCtx->timeout != UINT32_C(0))
            {
                timeoutmilliseconds = scsiIoCtx->timeout;
                // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
                // security) that we DON'T do a conversion and leave the time as the max...
                if (scsiIoCtx->timeout < BSD_ATA_PT_MAX_CMD_TIMEOUT_SECONDS)
                {
                    timeoutmilliseconds *= UINT32_C(1000); // convert to milliseconds
                }
                else
                {
                    timeoutmilliseconds = INT_MAX; // no timeout or maximum timeout
                }
            }
            else
            {
                timeoutmilliseconds = DEFAULT_COMMAND_TIMEOUT * UINT32_C(1000); // default to 15 second timeout
            }
        }
        atacmd.timeout = timeoutmilliseconds > INT_MAX ? INT_MAX : M_STATIC_CAST(int, timeoutmilliseconds);
        start_Timer(&commandTimer);
        iocret = ioctl(scsiIoCtx->device->os_info.fd, ATAIOCCOMMAND, &atacmd);
        stop_Timer(&commandTimer);
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        if (iocret < 0)
        {
            // something went wrong with the ioctl.
            scsiIoCtx->device->os_info.last_error = errno;
            ret                                   = OS_PASSTHROUGH_FAILURE;
            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                if (scsiIoCtx->device->os_info.last_error != 0)
                {
                    print_str("Error: ");
                    print_Errno_To_Screen(errno);
                }
            }
        }
        else
        {
            if (atacmd.retsts == ATACMD_OK)
            {
                scsiIoCtx->pAtaCmdOpts->rtfr.error  = atacmd.error;
                scsiIoCtx->pAtaCmdOpts->rtfr.status = atacmd.command;
                scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = atacmd.sec_count;
                scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow = atacmd.sec_num;
                scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid = M_Byte0(atacmd.cylinder);
                scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi  = M_Byte1(atacmd.cylinder);
                scsiIoCtx->pAtaCmdOpts->rtfr.device = atacmd.head;
            }
            else
            {
                // check the error and dummy up the response registers
                scsiIoCtx->pAtaCmdOpts->rtfr.status = ATA_STATUS_BIT_SEEK_COMPLETE | ATA_STATUS_BIT_READY; // start here
                if (atacmd.retsts & ATACMD_ERROR)
                {
                    scsiIoCtx->pAtaCmdOpts->rtfr.status |= ATA_STATUS_BIT_ERROR;
                    scsiIoCtx->pAtaCmdOpts->rtfr.error = atacmd.error;
                }
                if (atacmd.retsts & ATACMD_DF)
                {
                    scsiIoCtx->pAtaCmdOpts->rtfr.status |= ATA_STATUS_BIT_DEVICE_FAULT;
                }
                if (atacmd.retsts & ATACMD_TIMEOUT)
                {
                    ret = OS_COMMAND_TIMEOUT;
                }
            }
            if (scsiIoCtx->psense != M_NULLPTR) // check that the pointer is valid
            {
                if (scsiIoCtx->senseDataSize >= 22) // check that the sense data buffer is big enough to fill in
                                                    // our rtfrs using descriptor format
                {
                    scsiIoCtx->returnStatus.format = 0x72;
                    scsiIoCtx->returnStatus.senseKey =
                        0x01; // Not setting check condition since the IO was in fact successful
                    // setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->returnStatus.asc  = 0x00;
                    scsiIoCtx->returnStatus.ascq = 0x1D;
                    // now fill in the sens buffer
                    scsiIoCtx->psense[0] = 0x72;
                    scsiIoCtx->psense[1] = 0x01; // recovered error
                    // setting ASC/ASCQ to ATA Passthrough Information Available
                    scsiIoCtx->psense[2]  = 0x00; // ASC
                    scsiIoCtx->psense[3]  = 0x1D; // ASCQ
                    scsiIoCtx->psense[4]  = 0;
                    scsiIoCtx->psense[5]  = 0;
                    scsiIoCtx->psense[6]  = 0;
                    scsiIoCtx->psense[7]  = 0x0E; // additional sense length
                    scsiIoCtx->psense[8]  = 0x09; // descriptor code
                    scsiIoCtx->psense[9]  = 0x0C; // additional descriptor length
                    scsiIoCtx->psense[10] = 0;
                    // No ext since this passthorugh only supports 28bit commands
                    // fill in the returned 28bit registers
                    scsiIoCtx->psense[11] = scsiIoCtx->pAtaCmdOpts->rtfr.error;  // Error
                    scsiIoCtx->psense[13] = scsiIoCtx->pAtaCmdOpts->rtfr.secCnt; // Sector Count
                    scsiIoCtx->psense[15] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow; // LBA Lo
                    scsiIoCtx->psense[17] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid; // LBA Mid
                    scsiIoCtx->psense[19] = scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi;  // LBA Hi
                    scsiIoCtx->psense[20] = scsiIoCtx->pAtaCmdOpts->rtfr.device; // Device/Head
                    scsiIoCtx->psense[21] = scsiIoCtx->pAtaCmdOpts->rtfr.status; // Status
                }
            }
        }
    }
    return ret;
}

eReturnValues send_BSD_ATA_Reset(int fd)
{
#if defined(ATABUSIORESET)
    if (ioctl(fd, ATABUSIORESET) >= 0)
    {
        return SUCCESS;
    }
    else
    {
        return FAILURE;
    }
#else
    M_USE_UNUSED(fd);
    return OS_COMMAND_NOT_AVAILABLE;
#endif
}

eReturnValues send_BSD_ATA_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx != M_NULLPTR)
    {
        if (scsiIoCtx->pAtaCmdOpts == M_NULLPTR)
        {
            ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        }
        else
        {
            ret = bsd_ata_io(scsiIoCtx);
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}
