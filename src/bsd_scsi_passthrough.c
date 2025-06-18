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
// \file bsd_scsi_passthrough.c issues a scsi passthrough request for openbsd and netbsd

#include "common_types.h"
#include "error_translation.h"
#include "math_utils.h"
#include "precision_timer.h"
#include "type_conversion.h"

#include "common_public.h"
#include "scsi_helper.h"

#include "bsd_scsi_passthrough.h"

#include <sys/ioctl.h>
#include <sys/scsiio.h>

// This may not be defined. It is for openBSD, but not netBSD
//  16 is what we need to set it to.
#if !defined(CMDBUFLEN)
#    define CMDBUFLEN 16
#endif

#define BSD_SCSI_PT_MAX_CMD_TIMEOUT_SECONDS M_STATIC_CAST(uint32_t, (ULONG_MAX / 1000UL))

eReturnValues get_BSD_SCSI_Address(int fd, int* type, int* bus, int* target, int* lun)
{
    eReturnValues    ret = SUCCESS;
    struct scsi_addr address;
    safe_memset(&address, sizeof(struct scsi_addr), 0, sizeof(struct scsi_addr));
    if (ioctl(fd, SCIOCIDENTIFY, &address) < 0)
    {
        ret = FAILURE;
    }
    else
    {
        // copy address back out
        if (type != M_NULLPTR)
        {
#if defined(TYPE_SCSI) && defined(TYPE_ATAPI)
            // this field was added in 1997
            *type = address.type;
#else
            *type = 0;
#endif // TYPE_SCSI & TYPE_ATAPI
        }
#if defined(__NetBSD__) && defined(TYPE_SCSI) && defined(TYPE_ATAPI)
        // has a substructure since ATAPI support in 1997
        if (bus != M_NULLPTR)
        {
            *bus = address.addr.scsi.scbus;
        }
        if (target != M_NULLPTR)
        {
            *target = address.addr.scsi.target;
        }
        if (lun != M_NULLPTR)
        {
            *lun = address.addr.scsi.lun;
        }
#else
        // substructure for ATAPI vs SCSI no longer present
        if (bus != M_NULLPTR)
        {
            *bus = address.scbus;
        }
        if (target != M_NULLPTR)
        {
            *target = address.target;
        }
        if (lun != M_NULLPTR)
        {
            *lun = address.lun;
        }
#endif
    }
    return ret;
}

eReturnValues send_BSD_SCSI_Reset(int fd)
{
    eReturnValues ret = SUCCESS;
    if (ioctl(fd, SCIOCRESET) < 0)
    {
        ret = FAILURE;
    }
    return ret;
}

eReturnValues send_BSD_SCSI_Bus_Reset(int fd)
{
#if defined(SCBUSIORESET)
    eReturnValues ret = SUCCESS;
    if (ioctl(fd, SCBUSIORESET) < 0)
    {
        ret = FAILURE;
    }
    return ret;
#else
    M_USE_UNUSED(fd);
    return OS_COMMAND_NOT_AVAILABLE;
#endif
}

eReturnValues send_BSD_SCSI_IO(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    if (scsiIoCtx != M_NULLPTR && scsiIoCtx->device != M_NULLPTR)
    {
        if (scsiIoCtx->cdbLength > CMDBUFLEN)
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
        else
        {
            int iocret = 0;
            DECLARE_SEATIMER(commandTimer);
            scsireq_t scsicmd;
            safe_memset(&scsicmd, sizeof(scsireq_t), 0, sizeof(scsireq_t));
            switch (scsiIoCtx->direction)
            {
            case XFER_NO_DATA:
                scsicmd.databuf = M_NULLPTR;
                scsicmd.datalen = 0;
                break;
            case XFER_DATA_IN:
                scsicmd.flags |= SCCMD_READ;
                scsicmd.databuf = scsiIoCtx->pdata;
                scsicmd.datalen = scsiIoCtx->dataLength;
                break;
            case XFER_DATA_OUT:
                scsicmd.flags |= SCCMD_WRITE;
                scsicmd.databuf = scsiIoCtx->pdata;
                scsicmd.datalen = scsiIoCtx->dataLength;
                break;
            case XFER_DATA_IN_OUT:
            case XFER_DATA_OUT_IN:
                scsicmd.flags |= (SCCMD_READ | SCCMD_WRITE);
                scsicmd.databuf = scsiIoCtx->pdata;
                scsicmd.datalen = scsiIoCtx->dataLength;
                break;
            }
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
                scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
            {
                scsicmd.timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
                // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
                // security) that we DON'T do a conversion and leave the time as the max...
                if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < BSD_SCSI_PT_MAX_CMD_TIMEOUT_SECONDS)
                {
                    scsicmd.timeout *= 1000UL; // convert to milliseconds
                }
                else
                {
                    scsicmd.timeout = ULONG_MAX; // no timeout or maximum timeout
                }
            }
            else
            {
                if (scsiIoCtx->timeout != 0)
                {
                    scsicmd.timeout = scsiIoCtx->timeout;
                    // this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata
                    // security) that we DON'T do a conversion and leave the time as the max...
                    if (scsiIoCtx->timeout < BSD_SCSI_PT_MAX_CMD_TIMEOUT_SECONDS)
                    {
                        scsicmd.timeout *= 1000UL; // convert to milliseconds
                    }
                    else
                    {
                        scsicmd.timeout = ULONG_MAX; // no timeout or maximum timeout
                    }
                }
                else
                {
                    scsicmd.timeout = DEFAULT_COMMAND_TIMEOUT * 1000UL; // default to 15 second timeout
                }
            }
            safe_memcpy(scsicmd.cmd, CMDBUFLEN, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
            scsicmd.cmdlen   = scsiIoCtx->cdbLength;
            scsicmd.senselen = M_Min(SENSEBUFLEN, scsiIoCtx->senseDataSize);

            start_Timer(&commandTimer);
            iocret = ioctl(scsiIoCtx->device->os_info.fd, SCIOCCOMMAND, &scsicmd);
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
                        printf("Error: ");
                        print_Errno_To_Screen(errno);
                    }
                }
            }
            else
            {
                if (scsicmd.retsts == SCCMD_OK)
                {
                    ret = SUCCESS;
                    safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
                }
                else if (scsicmd.retsts & SCCMD_SENSE)
                {
                    // copy back sense data
                    safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, scsicmd.sense,
                                M_Min(scsicmd.senselen_used, scsiIoCtx->senseDataSize));
                }
                if (scsicmd.retsts & SCCMD_TIMEOUT)
                {
                    ret = OS_COMMAND_TIMEOUT;
                }
                else if (scsicmd.retsts & SCCMD_BUSY || scsicmd.retsts & SCCMD_UNKNOWN)
                {
                    ret = OS_PASSTHROUGH_FAILURE;
                }
            }
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}
