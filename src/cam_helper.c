//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2020 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#include <stdio.h>
#include <dirent.h>
#include "cam_helper.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"
#include "sat_helper_func.h"
#include "usb_hacks.h"

extern bool validate_Device_Struct(versionBlock);

static struct cam_device *cam_dev = NULL;

int get_Device( const char *filename, tDevice *device )
{
    struct ccb_getdev cgd;
    struct ccb_pathinq cpi;
    union ccb         *ccb = NULL;
    int               ret  = SUCCESS, this_drive_type = 0;
    char devName[20] = { 0 };
    int devUnit = 0;
    
    if (cam_get_device(filename, devName, 20, &devUnit) == -1)
    {
        ret = FAILURE;
        device->os_info.fd = -1;
        printf("%s failed\n", __FUNCTION__);
    }
    else
    {
        //printf("%s fd %d name %s\n",__FUNCTION__, device->os_info.fd, device->os_info.name);
        // cam_dev = cam_open_device(filename, O_RDWR)
        // The following API function looks more approriate to call
        cam_dev = cam_open_spec_device(devName, devUnit, O_RDWR, NULL);
        if (cam_dev != NULL)
        {
            //Set name and friendly name
            //name
            strcpy(device->os_info.name, filename);
            //friendly name
            sprintf(device->os_info.friendlyName, "%s%d", devName, devUnit);

            device->os_info.fd = devUnit;

            //set the OS Type
            device->os_info.osType = OS_FREEBSD;
            device->os_info.minimumAlignment = sizeof(void *);
            
            if (device->dFlags == OPEN_HANDLE_ONLY)
            {
                return ret;
            }

            //printf("%s Successfully opened\n",__FUNCTION__);
            ccb = cam_getccb(cam_dev);
            if (ccb != NULL)
            {
                bzero(&(&ccb->ccb_h)[1],
                      sizeof(struct ccb_pathinq) - sizeof(struct ccb_hdr));
                ccb->ccb_h.func_code = XPT_GDEV_TYPE;
                if (cam_send_ccb(cam_dev, ccb) >= 0)
                {
                    if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
                    {
                        bcopy(&ccb->cgd, &cgd, sizeof(struct ccb_getdev));
                        bcopy(&ccb->cpi, &cpi, sizeof(struct ccb_pathinq));
                        //default to scsi drive and scsi interface
                        device->drive_info.drive_type = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                        //start checking what information we got from the OS
                        if (cgd.protocol == PROTO_SCSI)
                        {
                            device->drive_info.interface_type = SCSI_INTERFACE;

                            memcpy(&device->drive_info.T10_vendor_ident, cgd.inq_data.vendor, SID_VENDOR_SIZE);
                            memcpy(&device->drive_info.product_identification, cgd.inq_data.product,\
                                       M_Min(MODEL_NUM_LEN, SID_PRODUCT_SIZE));
                            memcpy(&device->drive_info.product_revision, cgd.inq_data.revision,\
                                       M_Min(FW_REV_LEN, SID_REVISION_SIZE));
                            memcpy(&device->drive_info.serialNumber, cgd.serial_num, SERIAL_NUM_LEN);

                            // 0 - means ATA. 1 - means SCSI
                            this_drive_type = memcmp(device->drive_info.T10_vendor_ident, "ATA", 3);
                            if (this_drive_type == 0)
                            {
                                device->drive_info.drive_type = ATA_DRIVE;
                            }
                            else
                            {
                                device->drive_info.drive_type = SCSI_DRIVE;
                            }

                        }
                        else if (cgd.protocol == PROTO_ATA || cgd.protocol == PROTO_ATAPI)
                        {
                            device->drive_info.interface_type = IDE_INTERFACE;
                            device->drive_info.drive_type = ATA_DRIVE;
                            if (cgd.protocol == PROTO_ATAPI)
                            {
                                device->drive_info.drive_type = ATAPI_DRIVE;
                            }
                            memcpy(&device->drive_info.T10_vendor_ident, "ATA", 3);
                            memcpy(&device->drive_info.product_identification, cgd.ident_data.model,\
                                       M_Min(MODEL_NUM_LEN, 40)); //40 comes from ata_param stuct in the ata.h
                            memcpy(&device->drive_info.product_revision, cgd.ident_data.revision,\
                                       M_Min(FW_REV_LEN, 8)); //8 comes from ata_param stuct in the ata.h
                            memcpy(&device->drive_info.serialNumber, cgd.ident_data.serial,\
                                       M_Min(SERIAL_NUM_LEN, 20)); //20 comes from ata_param stuct in the ata.h
                        }
                        else
                        {
                            printf("Unsupported interface %d\n", cgd.protocol);
                        }
                        //set the interface from a ccb_pathing struct
                        /*
                        switch (cpi.transport)
                        {
                        case XPORT_SATA:
                        case XPORT_ATA:
                            device->drive_info.interface_type = IDE_INTERFACE;
                            break;
                        case XPORT_USB:
                            device->drive_info.interface_type = USB_INTERFACE;
                            break;
                        case XPORT_SAS:
                        case XPORT_ISCSI:
                        case XPORT_SSA:
                        case XPORT_FC:
                        case XPORT_SPI:
                        case XPORT_UNSPECIFIED:
                        case XPORT_UNKNOWN:
                        default:
                            device->drive_info.interface_type = SCSI_INTERFACE;
                            break;
                        }
                        /*/
                        //comment switch-do nothing
                        //*/
                        //let the library now go out and set up the device struct after sending some commands.
                        if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
                        {
                            //TODO: Actually get the VID and PID set before calling this.
                            setup_Passthrough_Hacks_By_ID(device);
                        }
                        ret = fill_Drive_Info_Data(device);
                    }
                    else
                    {
                        printf("WARN: XPT_GDEV_TYPE I/O status failed\n");
                        ret = FAILURE;
                    }
                }
                else
                {
                    printf("WARN: XPT_GDEV_TYPE I/O failed\n");
                    ret = FAILURE;
                }
            }
            else
            {
                printf("WARN: Could not allocate CCB\n");
                ret = FAILURE;
            }
        }
        else
        {
            printf("%s Opened Failed\n", __FUNCTION__);
            ret = FAILURE;
        }
    }

    if (ccb != NULL)
    {
        cam_freeccb(ccb);
    }

    return ret;
}

int send_IO( ScsiIoCtx *scsiIoCtx )
{
    int ret = FAILURE;
    //printf("%s -->\n",__FUNCTION__);

    if (scsiIoCtx->device->drive_info.interface_type == SCSI_INTERFACE)
    {
        ret = send_Scsi_Cam_IO(scsiIoCtx);
    }
    else if (scsiIoCtx->device->drive_info.interface_type == IDE_INTERFACE)
    {
        if (scsiIoCtx->pAtaCmdOpts)
        {
            ret = send_Ata_Cam_IO(scsiIoCtx);
        }
        else
        {
            ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        }
    }
    else if (scsiIoCtx->device->drive_info.interface_type == RAID_INTERFACE)
    {
        if (scsiIoCtx->device->issue_io != NULL)
        {
            ret = scsiIoCtx->device->issue_io(scsiIoCtx);
        }
        else
        {
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("No Raid PassThrough IO Routine present for this device\n");
            }
        }
    }
    else
    {
        if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Target Device does not have a valid interface %d\n",\
                       scsiIoCtx->device->drive_info.interface_type);
        }
    }
    //printf("<-- %s\n",__FUNCTION__);
    return ret;
}

int send_Ata_Cam_IO( ScsiIoCtx *scsiIoCtx )
{
    int              ret       = SUCCESS;
    union ccb        *ccb      = NULL;
    struct ccb_ataio *ataio    = NULL;
    u_int32_t        direction = 0;

    ccb = cam_getccb(cam_dev);

    if (ccb != NULL)
    {
        ataio = &ccb->ataio;

        /* cam_getccb cleans up the header, caller has to zero the payload */
        bzero(&(&ccb->ccb_h)[1],
              sizeof(struct ccb_ataio) - sizeof(struct ccb_hdr));

        switch (scsiIoCtx->direction)
        {
        case XFER_NO_DATA:
            direction = CAM_DIR_NONE;
            break;
        case XFER_DATA_IN:
            direction = CAM_DIR_IN;
            break;
        case XFER_DATA_OUT:
            direction = CAM_DIR_OUT;
            break;
        case XFER_DATA_OUT_IN:
        case XFER_DATA_IN_OUT:
            direction = CAM_DIR_BOTH;
            break;
        default:
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("%s Didn't understand I/O direction\n", __FUNCTION__);
            }
            return -1;
        }

        uint32_t camTimeout = scsiIoCtx->timeout;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            camTimeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
            //this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < 4294966)
            {
                camTimeout *= 1000;//convert to milliseconds
            }
            else
            {
                camTimeout = UINT32_MAX;//no timeout or maximum timeout
            }
        }
        else
        {
            if (scsiIoCtx->timeout != 0)
            {
                camTimeout = scsiIoCtx->timeout;
                //this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that we DON'T do a conversion and leave the time as the max...
                if (scsiIoCtx->timeout < 4294966)
                {
                    camTimeout *= 1000;//convert to milliseconds
                }
                else
                {
                    camTimeout = UINT32_MAX;//no timeout or maximum timeout
                }
            }
            else
            {
                camTimeout = 15 * 1000;//default to 15 second timeout
            }
        }

        cam_fill_ataio(&ccb->ataio,
                       0, /* retry_count */
                       NULL,
                       direction, /*flags*/
                       MSG_SIMPLE_Q_TAG,
                       (u_int8_t *)scsiIoCtx->pdata, /*data_ptr*/
                       scsiIoCtx->dataLength, /*dxfer_len*/
                       camTimeout);

        /* Disable freezing the device queue */
        ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
        /* We set this flag here because cam_fill_atatio clears the flags*/
        if (scsiIoCtx->direction == XFER_NO_DATA)
        {
            ccb->ataio.cmd.flags |= CAM_ATAIO_NEEDRESULT;
        }

        if (scsiIoCtx->pAtaCmdOpts != NULL)
        {
            bzero(&ataio->cmd, sizeof(ataio->cmd));
            if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_TASKFILE)
            {
                //ataio->cmd.flags = 0;
                if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_QUE ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_FPDMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UDMA
                    )
                {
                    ataio->cmd.flags |= CAM_ATAIO_DMA;
                }
                ataio->cmd.command = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
                ataio->cmd.features = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
                ataio->cmd.lba_low = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
                ataio->cmd.lba_mid = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
                ataio->cmd.lba_high = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
                ataio->cmd.device = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
                ataio->cmd.sector_count = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
            }
            else if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
            {
                ataio->cmd.flags |= CAM_ATAIO_48BIT;
                if (scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_QUE ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_PACKET_DMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_DMA_FPDMA ||
                    scsiIoCtx->pAtaCmdOpts->commadProtocol == ATA_PROTOCOL_UDMA
                    )
                {
                    ataio->cmd.flags |= CAM_ATAIO_DMA;
                }
                ataio->cmd.command = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
                ataio->cmd.lba_low = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
                ataio->cmd.lba_mid = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
                ataio->cmd.lba_high = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
                ataio->cmd.device = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
                ataio->cmd.lba_low_exp = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;
                ataio->cmd.lba_mid_exp = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;
                ataio->cmd.lba_high_exp = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;
                ataio->cmd.features = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
                ataio->cmd.features_exp = scsiIoCtx->pAtaCmdOpts->tfr.Feature48;
                ataio->cmd.sector_count = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
                ataio->cmd.sector_count_exp = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;
            }
            else
            {
                ret = BAD_PARAMETER;
                printf("WARN: Unsupported ATA Command type\n");
            }

            if (ret == SUCCESS)
            {
                seatimer_t commandTimer;
                memset(&commandTimer, 0, sizeof(seatimer_t));
                #if defined (_DEBUG)
                printf("ATAIO: cmd=0x%02"PRIX8" feat=0x%02"PRIX8" lbalow=0x%02"PRIX8" lbamid=0x%02"PRIX8" lbahi=0x%02"PRIX8" sc=0x%02"PRIX8"\n",\
                           ataio->cmd.command, ataio->cmd.features, ataio->cmd.lba_low, ataio->cmd.lba_mid,\
                           ataio->cmd.lba_high, ataio->cmd.sector_count);
                printf("\tfeatext=0x%02"PRIX8" lbalowExp=0x%02"PRIX8" lbamidExp=0x%02"PRIX8" lbahiExp=0x%02"PRIX8" scExp=0x%02"PRIX8"\n",\
                           ataio->cmd.features_exp, ataio->cmd.lba_low_exp, ataio->cmd.lba_mid_exp,\
                           ataio->cmd.lba_high_exp, ataio->cmd.sector_count_exp);

                printf("\tData Ptr %p, xfer len %d\n", ataio->data_ptr, ataio->dxfer_len);
                #endif
                start_Timer(&commandTimer);
                ret = cam_send_ccb(cam_dev, ccb);
                stop_Timer(&commandTimer);
                if (ret < 0)
                {
                    perror("error sending ATA I/O");
                    ret = FAILURE;
                }
                else
                {
                    if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
                    {
                        if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_ATA_STATUS_ERROR)
                        {
                            ret = COMMAND_FAILURE;
                            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                            {
                                printf("WARN: I/O went through but drive returned status=0x%02"PRIX8" error=0x%02"PRIX8"\n",\
                                           ataio->res.status, ataio->res.error);
                            }
                        }
                        else if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_CMD_TIMEOUT)
                        {
                            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                            {
                                printf("WARN: I/O CAM_CMD_TIMEOUT occured\n");
                            }
                        }
                        else
                        {
                            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
                            {
                                printf("WARN: I/O error occurred %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
                            }
                        }
                    }
                    else
                    {
                        #if defined (_DEBUG)
                        printf("I/O went through status %d\n",\
                                   (ccb->ccb_h.status & CAM_STATUS_MASK));
                        #endif
                    }
                    ret = SUCCESS;

                    //get the rtfrs and put them into a "sense buffer". In other words, fill in the sense buffer with the rtfrs in descriptor format
                    if (scsiIoCtx->psense != NULL)//check that the pointer is valid
                    {
                        if (scsiIoCtx->senseDataSize >= 22)//check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
                        {
                            scsiIoCtx->returnStatus.format = 0x72;
                            scsiIoCtx->returnStatus.senseKey = 0x01;//Not setting check condition since the IO was in fact successful
                            //setting ASC/ASCQ to ATA Passthrough Information Available
                            scsiIoCtx->returnStatus.asc = 0x00;
                            scsiIoCtx->returnStatus.ascq = 0x1D;
                            //now fill in the sens buffer
                            scsiIoCtx->psense[0] = 0x72;
                            scsiIoCtx->psense[1] = 0x01;//recovered error
                            //setting ASC/ASCQ to ATA Passthrough Information Available
                            scsiIoCtx->psense[2] = 0x00;//ASC
                            scsiIoCtx->psense[3] = 0x1D;//ASCQ
                            scsiIoCtx->psense[4] = 0;
                            scsiIoCtx->psense[5] = 0;
                            scsiIoCtx->psense[6] = 0;
                            scsiIoCtx->psense[7] = 0x0E;//additional sense length
                            scsiIoCtx->psense[8] = 0x09;//descriptor code
                            scsiIoCtx->psense[9] = 0x0C;//additional descriptor length
                            scsiIoCtx->psense[10] = 0;
                            if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
                            {
                                scsiIoCtx->psense[10] |= 0x01;//set the extend bit
                                //fill in the ext registers while we're in this if...no need for another one
                                scsiIoCtx->psense[12] = ataio->res.sector_count_exp;// Sector Count Ext
                                scsiIoCtx->psense[14] = ataio->res.lba_low_exp;// LBA Lo Ext
                                scsiIoCtx->psense[16] = ataio->res.lba_mid_exp;// LBA Mid Ext
                                scsiIoCtx->psense[18] = ataio->res.lba_high_exp;// LBA Hi
                            }
                            //fill in the returned 28bit registers
                            scsiIoCtx->psense[11] = ataio->res.error;// Error
                            scsiIoCtx->psense[13] = ataio->res.sector_count;// Sector Count
                            scsiIoCtx->psense[15] = ataio->res.lba_low;// LBA Lo
                            scsiIoCtx->psense[17] = ataio->res.lba_mid;// LBA Mid
                            scsiIoCtx->psense[19] = ataio->res.lba_high;// LBA Hi
                            scsiIoCtx->psense[20] = ataio->res.device;// Device/Head
                            scsiIoCtx->psense[21] = ataio->res.status;// Status
                        }
                    }
                }
                scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
            }
        }
        else
        {
            if (VERBOSITY_DEFAULT < scsiIoCtx->device->deviceVerbosity)
            {
                printf("WARN: Sending non-ATA commnad to ATA Drive [FreeBSD CAM driver does not support SAT Specification]\n");
            }
            ret = BAD_PARAMETER;
        }

    }
    else
    {
        printf("WARN: couldn't allocate CCB");
    }

    return ret;
}


int send_Scsi_Cam_IO( ScsiIoCtx *scsiIoCtx )
{
    #if defined (_DEBUG)
    printf("--> %s\n", __FUNCTION__);
    #endif
    int ret = 0;
    //device * device = scsiIoCtx->device;
    struct ccb_scsiio *csio = NULL;
    union ccb         *ccb  = NULL;

    if (cam_dev == NULL)
    {
        printf("%s dev is NULL\n", __FUNCTION__);
        return FAILURE;
    }
    else if (scsiIoCtx->cdbLength > IOCDBLEN)
    {
        printf("%s too big CDB\n", __FUNCTION__);
        return BAD_PARAMETER;
    }

    ccb = cam_getccb(cam_dev);

    if (ccb != NULL)
    {
        // Following is copy/paste from different funtions in camcontrol.c
        /* cam_getccb cleans up the header, caller has to zero the payload */
        bzero(&(&ccb->ccb_h)[1],
              sizeof(struct ccb_scsiio) - sizeof(struct ccb_hdr));

        csio = &ccb->csio;

        csio->ccb_h.func_code = XPT_SCSI_IO;
        csio->ccb_h.retry_count = 0; // should we change it to 1?
        csio->ccb_h.cbfcnp = NULL;
        uint32_t camTimeout = scsiIoCtx->timeout;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            camTimeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
            //this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that we DON'T do a conversion and leave the time as the max...
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds < 4294966)
            {
                camTimeout *= 1000;//convert to milliseconds
            }
            else
            {
                camTimeout = UINT32_MAX;//no timeout or maximum timeout
            }
        }
        else
        {
            if (scsiIoCtx->timeout != 0)
            {
                camTimeout = scsiIoCtx->timeout;
                //this check is to make sure on commands that set a very VERY large timeout (*cough* *cough* ata security) that we DON'T do a conversion and leave the time as the max...
                if (scsiIoCtx->timeout < 4294966)
                {
                    camTimeout *= 1000;//convert to milliseconds
                }
                else
                {
                    camTimeout = UINT32_MAX;//no timeout or maximum timeout
                }
            }
            else
            {
                camTimeout = 15 * 1000;//default to 15 second timeout
            }
        }
        csio->ccb_h.timeout = camTimeout;
        csio->cdb_len = scsiIoCtx->cdbLength;
        csio->sense_len = scsiIoCtx->senseDataSize; //So it seems the csio has it's own buffer for Sense...so revist.
        csio->tag_action = MSG_SIMPLE_Q_TAG; // TODO: will we have anything else ever?

        switch (scsiIoCtx->direction)
        {
        case XFER_NO_DATA:
            csio->ccb_h.flags = CAM_DIR_NONE;
            break;
        case XFER_DATA_IN:
            csio->ccb_h.flags = CAM_DIR_IN;
            break;
        case XFER_DATA_OUT:
            csio->ccb_h.flags = CAM_DIR_OUT;
            break;
        case XFER_DATA_OUT_IN:
        case XFER_DATA_IN_OUT:
            csio->ccb_h.flags = CAM_DIR_BOTH;
            break;
            //case SG_DXFER_UNKNOWN:
            //io_hdr.dxfer_direction = SG_DXFER_UNKNOWN;
            //break;
        default:
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("%s Didn't understand direction\n", __FUNCTION__);
            }
            return -1;
        }

        csio->dxfer_len = scsiIoCtx->dataLength;
        csio->data_ptr = scsiIoCtx->pdata;

        /* Disable freezing the device queue */
        ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
        //ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER; // Needed?

        memcpy(&csio->cdb_io.cdb_bytes[0], &scsiIoCtx->cdb[0], IOCDBLEN);
        #if defined (_DEBUG)
        printf("%s cdb [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x] \n\t \
               [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x]\n",\
                   __FUNCTION__,\
                   csio->cdb_io.cdb_bytes[0],\
                   csio->cdb_io.cdb_bytes[1],\
                   csio->cdb_io.cdb_bytes[2],\
                   csio->cdb_io.cdb_bytes[3],\
                   csio->cdb_io.cdb_bytes[4],\
                   csio->cdb_io.cdb_bytes[5],\
                   csio->cdb_io.cdb_bytes[6],\
                   csio->cdb_io.cdb_bytes[7],\
                   csio->cdb_io.cdb_bytes[8],\
                   csio->cdb_io.cdb_bytes[9],\
                   csio->cdb_io.cdb_bytes[10],\
                   csio->cdb_io.cdb_bytes[11],\
                   csio->cdb_io.cdb_bytes[12],\
                   csio->cdb_io.cdb_bytes[13],\
                   csio->cdb_io.cdb_bytes[14],\
                   csio->cdb_io.cdb_bytes[15]
              );
        #endif
        seatimer_t commandTimer;
        memset(&commandTimer, 0, sizeof(seatimer_t));
        start_Timer(&commandTimer);
        ret = cam_send_ccb(cam_dev, ccb);
        stop_Timer(&commandTimer);
        if (ret < 0)
        {
            perror("cam_send_cdb");
        }

        if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
        {
            #if defined (_DEBUG)  
            printf("%s success with ret %d & valid sense=%d\n",\
                       __FUNCTION__, ret, (ccb->ccb_h.status & CAM_AUTOSNS_VALID));
            printf("%s error code %d, sense [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x] \n\t \
               [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x]\n",\
                       __FUNCTION__,\
                       csio->sense_data.error_code,\
                       csio->sense_data.sense_buf[0],\
                       csio->sense_data.sense_buf[1],\
                       csio->sense_data.sense_buf[2],\
                       csio->sense_data.sense_buf[3],\
                       csio->sense_data.sense_buf[4],\
                       csio->sense_data.sense_buf[5],\
                       csio->sense_data.sense_buf[6],\
                       csio->sense_data.sense_buf[7],\
                       csio->sense_data.sense_buf[8],\
                       csio->sense_data.sense_buf[9],\
                       csio->sense_data.sense_buf[10],\
                       csio->sense_data.sense_buf[11],\
                       csio->sense_data.sense_buf[12],\
                       csio->sense_data.sense_buf[13],\
                       csio->sense_data.sense_buf[14],\
                       csio->sense_data.sense_buf[15]
                  );
            #endif
            scsiIoCtx->returnStatus.senseKey = csio->scsi_status;

            if ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) == 0)
            {
                // Since we have no sense data fake it for ATA
                if (scsiIoCtx->device->drive_info.drive_type == ATA_DRIVE)
                {
                    if (scsiIoCtx->returnStatus.senseKey == SCSI_STATUS_OK)
                    {
                        //scsiIoCtx->rtfrs.status = ATA_GOOD_STATUS;
                        //scsiIoCtx->rtfrs.error = 0;
                    }
                    else
                    {
                        //scsiIoCtx->rtfrs.status = 0x51;
                        //scsiIoCtx->rtfrs.error = 0x4;
                    }
                }
            }
            else // we have some valid sense?
            {

            }
        }
        else
        {
            ret = COMMAND_FAILURE;

            if (VERBOSITY_DEFAULT < scsiIoCtx->device->deviceVerbosity)
            {
                printf("%s cam error %d, scsi error %d\n",\
                           __FUNCTION__, (ccb->ccb_h.status & CAM_STATUS_MASK), ccb->csio.scsi_status);
            }

            if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR)
                && (ccb->csio.scsi_status == SCSI_STATUS_CHECK_COND)
                && ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) != 0))
            {
                //memcpy(scsiIoCtx->psense, &csio->sense_data.sense_buf[0], scsiIoCtx->senseDataSize);
                memcpy(scsiIoCtx->psense, &csio->sense_data.error_code, sizeof(uint8_t));
                memcpy(scsiIoCtx->psense+1, &csio->sense_data.sense_buf[0], (scsiIoCtx->senseDataSize)-1);
                #if defined (_DEBUG)  
                printf("%s error code %d, sense [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x] \n\t \
                   [%x] [%x] [%x] [%x] [%x] [%x] [%x] [%x]\n",\
                           __FUNCTION__,\
                           csio->sense_data.error_code,\
                           csio->sense_data.sense_buf[0],\
                           csio->sense_data.sense_buf[1],\
                           csio->sense_data.sense_buf[2],\
                           csio->sense_data.sense_buf[3],\
                           csio->sense_data.sense_buf[4],\
                           csio->sense_data.sense_buf[5],\
                           csio->sense_data.sense_buf[6],\
                           csio->sense_data.sense_buf[7],\
                           csio->sense_data.sense_buf[8],\
                           csio->sense_data.sense_buf[9],\
                           csio->sense_data.sense_buf[10],\
                           csio->sense_data.sense_buf[11],\
                           csio->sense_data.sense_buf[12],\
                           csio->sense_data.sense_buf[13],\
                           csio->sense_data.sense_buf[14],\
                           csio->sense_data.sense_buf[15]
                      );
                #endif
            }
        }
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    }
    else
    {
        printf("ccb is Null\n");
        ret = BAD_PARAMETER; //Should this be MEMORY FAILURE?
    }

    cam_freeccb(ccb);

    #if defined (_DEBUG)  
    printf("<-- %s ret=[%d]\n", __FUNCTION__, ret);
    #endif

    return ret;
}

static int da_filter( const struct dirent *entry )
{
    int daHandle = strncmp("da", entry->d_name, 2);
    if(daHandle != 0)
    {
      return !daHandle;
    }
    char* partition = strpbrk(entry->d_name,"pP");
    if(partition != NULL)
    {
        return 0;
    }
    else
    {
        return !daHandle;
    }
}

static int ada_filter( const struct dirent *entry )
{
    int adaHandle = strncmp("ada", entry->d_name, 3);
    if(adaHandle != 0)
    {
      return !adaHandle;
    }
    char* partition = strpbrk(entry->d_name,"pP");
    if(partition != NULL)
    {
        return 0;
    }
    else
    {
        return !adaHandle;
    }
}

int close_Device(tDevice *dev)
{
    if (cam_dev)
    {
        cam_close_device(cam_dev);
    }
    return SUCCESS;
}

//-----------------------------------------------------------------------------
//
//  get_Device_Count()
//
//! \brief   Description:  Get the count of devices in the system that this library
//!                        can talk to. This function is used in conjunction with
//!                        get_Device_List, so that enough memory is allocated.
//
//  Entry:
//!   \param[out] numberOfDevices = integer to hold the number of devices found. 
//!   \param[in] flags = eScanFlags based mask to let application control. 
//!                      NOTE: currently flags param is not being used.  
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
{
    int  num_da_devs = 0, num_ada_devs = 0;

    struct dirent **danamelist;
    struct dirent **adanamelist;
    num_da_devs = scandir("/dev", &danamelist, da_filter, alphasort);
    num_ada_devs = scandir("/dev", &adanamelist, ada_filter, alphasort);

    //free the list of names to not leak memory
    for (int iter = 0; iter < num_da_devs; ++iter)
    {
        safe_Free(danamelist[iter]);
    }
    safe_Free(danamelist);
    //free the list of names to not leak memory
    for (int iter = 0; iter < num_ada_devs; ++iter)
    {
        safe_Free(adanamelist[iter]);
    }
    safe_Free(adanamelist);

    *numberOfDevices = num_da_devs + num_ada_devs;
    M_USE_UNUSED(flags);  
    return SUCCESS;
}

//-----------------------------------------------------------------------------
//
//  get_Device_List()
//
//! \brief   Description:  Get a list of devices that the library supports. 
//!                        Use get_Device_Count to figure out how much memory is
//!                        needed to be allocated for the device list. The memory 
//!                        allocated must be the multiple of device structure. 
//!                        The application can pass in less memory than needed 
//!                        for all devices in the system, in which case the library 
//!                        will fill the provided memory with how ever many device 
//!                        structures it can hold. 
//  Entry:
//!   \param[out] ptrToDeviceList = pointer to the allocated memory for the device list
//!   \param[in]  sizeInBytes = size of the entire list in bytes. 
//!   \param[in]  versionBlock = versionBlock structure filled in by application for 
//!                              sanity check by library. 
//!   \param[in] flags = eScanFlags based mask to let application control. 
//!                      NOTE: currently flags param is not being used.  
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    int returnValue = SUCCESS;
    int numberOfDevices = 0;
    int driveNumber = 0, found = 0, failedGetDeviceCount = 0, permissionDeniedCount = 0;
    char name[80]; //Because get device needs char
    int fd;
    tDevice * d = NULL;
    int  num_da_devs = 0, num_ada_devs = 0;

    struct dirent **danamelist;
    struct dirent **adanamelist;
    num_da_devs = scandir("/dev", &danamelist, da_filter, alphasort);
    num_ada_devs = scandir("/dev", &adanamelist, ada_filter, alphasort);
    
    char **devs = (char **)calloc(num_da_devs + num_ada_devs + 1, sizeof(char *));
    int i = 0, j = 0;
    for (i = 0; i < num_da_devs; ++i)
    {
        devs[i] = (char *)malloc((strlen("/dev/") + strlen(danamelist[i]->d_name) + 1) * sizeof(char));
        strcpy(devs[i], "/dev/");
        strcat(devs[i], danamelist[i]->d_name);
        safe_Free(danamelist[i]);
    }
    for (j = 0; i < (num_da_devs + num_ada_devs) && j < num_ada_devs; ++i, j++)
    {
        devs[i] = (char *)malloc((strlen("/dev/") + strlen(adanamelist[j]->d_name) + 1) * sizeof(char));
        strcpy(devs[i], "/dev/");
        strcat(devs[i], adanamelist[j]->d_name);
        safe_Free(adanamelist[j]);
    }
    devs[i] = NULL; //Added this so the for loop down doesn't cause a segmentation fault.
    safe_Free(danamelist);
    safe_Free(adanamelist);

    //TODO: Check if sizeInBytes is a multiple of 
    if (!(ptrToDeviceList) || (!sizeInBytes))
    {
        returnValue = BAD_PARAMETER;
    }
    else if ((!(validate_Device_Struct(ver))))
    {
        returnValue = LIBRARY_MISMATCH;
    }
    else
    {
        numberOfDevices = sizeInBytes / sizeof(tDevice);
        d = ptrToDeviceList;
        for (driveNumber = 0; ((driveNumber >= 0 && (unsigned int)driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < (num_da_devs + num_ada_devs)) && (found < numberOfDevices)); ++driveNumber)
        {
            if(!devs[driveNumber] || strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            memset(name, 0, sizeof(name));//clear name before reusing it
            strcpy(name, devs[driveNumber]);
            fd = -1;
            //lets try to open the device.      
            fd = cam_get_device(name, d->os_info.name, sizeof(d->os_info.name), &d->os_info.fd);
            if (fd >= 0)
            {
                if (cam_dev)
                {
                    cam_close_device(cam_dev);
                }
                eVerbosityLevels temp = d->deviceVerbosity;
                memset(d, 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size = ver.size;
                d->sanity.version = ver.version;
                int ret = get_Device(name, d);
                if (ret != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                found++;
                d++;
            }
            else if (errno == EACCES) //quick fix for opening drives without sudo
            {
                ++permissionDeniedCount;
                failedGetDeviceCount++;
            }
            else
            {
                failedGetDeviceCount++;
            }
            //free the dev[deviceNumber] since we are done with it now.
            safe_Free(devs[driveNumber]);
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if(permissionDeniedCount == (num_da_devs + num_ada_devs))
        {
            returnValue = PERMISSION_DENIED;
        }
	    else if (failedGetDeviceCount && returnValue != PERMISSION_DENIED)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    safe_Free(devs);
    return returnValue;
}

int os_Read(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool async, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Write(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool async, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Verify(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

int os_Flush(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}

//TODO: Add code for CAM resets. There should be XPT function codes to do some amount of resetting
int os_Device_Reset(M_ATTR_UNUSED tDevice *device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}
    
int os_Bus_Reset(M_ATTR_UNUSED tDevice *device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

int os_Controller_Reset(M_ATTR_UNUSED tDevice *device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

#if !defined(DISABLE_NVME_PASSTHROUGH)
int send_NVMe_IO(M_ATTR_UNUSED nvmeCmdCtx *nvmeIoCtx)
{
    return NOT_SUPPORTED;
}

int os_nvme_Reset(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}

int os_nvme_Subsystem_Reset(M_ATTR_UNUSED tDevice *device)
{
    return NOT_SUPPORTED;
}

int pci_Read_Bar_Reg(M_ATTR_UNUSED tDevice * device, M_ATTR_UNUSED uint8_t * pData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}
#endif
