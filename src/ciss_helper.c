//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file ciss_helper.c
// \brief Defines the constants structures to help with CISS implementation. This attempts to be generic for any unix-like OS. Windows support is through CSMI.

#if defined (ENABLE_CISS)
#if defined (__unix__) //this is only done in case someone sets weird defines for Windows even though this isn't supported
#include <fcntl.h>
#include <unistd.h> // for close
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h> //for basename function
#include <ctype.h>
#endif //__unix__

//Includes vary by platform
#if defined (__linux__)
//define this macro before these includes. Doesn't need to match exactly, just make the compiler happy.-TJE
#ifndef __user
#define __user
#endif //__user
//usr/include/linux has the cciss_defs and cciss_ioctl files.
//Include these instead of our own copy.
#include <linux/cciss_defs.h>
#include <linux/cciss_ioctl.h>
#elif defined (__FreeBSD__)
#include "external/ciss/freebsd/cissio.h"
//TODO: need anything special to handle smartpqi? https://github.com/FreeBSDDesktop/freebsd-base/blob/master/sys/dev/smartpqi/smartpqi_defines.h
//      looks compatible with ciss, but may need some changes to support.-TJE
#elif defined (__sun)
#include "external/ciss/solaris/cpqary3.h"
#include "external/ciss/solaris/cpqary3_ioctl.h"
#else //not a supported OS for CISS
#pragma message "CISS support is not available for this OS"
#endif //checking platform specific macros

#include "ciss_helper.h"
#include "ciss_helper_func.h"
#include "scsi_helper_func.h"
#include "common_platform.h"

extern bool validate_Device_Struct(versionBlock);

static bool is_SmartPQI_Unique_IOCTLs_Supported(int fd)
{
#if defined (__FreeBSD__)
    pqi_pci_info_t pciInfo;
    memset(&pciInfo, 0, sizeof(pqi_pci_info_t));
    if (0 == ioctl(fd, SMARTPQI_GETPCIINFO, &pciInfo))
    {
        supported = true;
    }
    else
#endif //__FreeBSD__
    {
        M_USE_UNUSED(fd);
        return false;
    }
}

//This function helps the scan and print determine if we can send ciss IOCTLs. 
//All it does is attempts an easy ioctl to get some basic info from the controller, which may vary between OSs
static bool supports_CISS_IOCTLs(int fd)
{
    bool supported = false;
#if defined (__linux__) || defined (__FreeBSD__)
    cciss_pci_info_struct pciInfo;
    memset(&pciInfo, 0, sizeof(cciss_pci_info_struct));
    if (0 == ioctl(fd, CCISS_GETPCIINFO, &pciInfo))
    {
        supported = true;
    }
    //If Linux SMARTPQI does not respond to this, maybe try some of the other non-passthrough IOCTLs to see if we get a valid response-TJE
    #if defined (__FreeBSD__)
    else
    {
        return is_SmartPQI_Unique_IOCTLs_Supported(fd);
    }
    #endif
#elif defined (__sun)
    cpqary3_ctlr_info_t ctrlInfo;
    memset(&ctrlInfo, 0, sizeof(cpqary3_ctlr_info_t));
    if (0 == ioctl(fd, CPQARY3_IOCTL_CTLR_INFO, &ctrlInfo))
    {
        supported = true;
    }
#endif
    return supported;
}

#define OS_CISS_HANDLE_MAX_LENGTH 20

//creates /dev/sg? or /dev/cciss/c?d? or /dev/ciss/?
//TODO: What should this look like for solaris/illumos?
static bool create_OS_CISS_Handle_Name(char *input, char *osHandle)
{
    bool success = false;
    if (input)
    {
        if (strncmp(input, "sg", 2) == 0 || strncmp(input, "ciss", 4) == 0  || strncmp(input, "smartpqi", 8) == 0)
        {
            //linux SG handle /dev/sg? or freeBSD ciss handle: /dev/ciss?
            snprintf(osHandle, OS_CISS_HANDLE_MAX_LENGTH, "/dev/%s", input);
            success = true;
        }
        else
        {
            //check for linux cciss handle: /dev/cciss/c?d?
            //in this case, the input should only be c?d?
            uint16_t controller = 0, device = 0;
            int scanResult = sscanf(input, "c%" SCNu16 "d%" SCNu16, &controller, &device);
            if (scanResult == 2)
            {
                snprintf(osHandle, OS_CISS_HANDLE_MAX_LENGTH, "/dev/cciss/%s", input);
                success = true;
            }
        }

    }
    return success;
}

#define PARSE_COUNT_SUCCESS 3

static uint8_t parse_CISS_Handle(const char * devName, char *osHandle, uint16_t *physicalDriveNumber)
{
    uint8_t parseCount = 0;
    if (devName)
    {
        //TODO: Check the format of the handle to see if it is ciss format that can be supported
        char* dup = strdup(devName);
        if (strstr(dup, CISS_HANDLE_BASE_NAME) == dup)
        {
            //starts with ciss, so now we should check to make sure we found everything else
            uint8_t counter = 0;
            char *token = strtok(dup, ":");
            while (token && counter < 3)
            {
                switch (counter)
                {
                case 0://ciss - already been validated above
                    ++parseCount;
                    break;
                case 1://partial system handle...need to append /dev/ to the front at least. ciss will need /dev/cciss/<> varies a little bit based on the format here
                    if (create_OS_CISS_Handle_Name(token, osHandle))
                    {
                        ++parseCount;
                    }
                    break;
                case 2://physical drive number
                    if (isdigit(token[0]))
                    {
                        *physicalDriveNumber = C_CAST(uint16_t, atoi(token));
                        ++parseCount;
                    }
                    break;
                default:
                    break;
                }
                ++counter;
                token = strtok(NULL, ":");
            }
        }
        safe_Free(dup);
    }
    return parseCount;
}

bool is_Supported_ciss_Dev(const char * devName)
{
    bool supported = false;
    uint16_t driveNumber = 0;
    char osHandle[OS_CISS_HANDLE_MAX_LENGTH] = { 0 };
    char *handlePtr = &osHandle[0];//this is done to prevent warnings
    if (PARSE_COUNT_SUCCESS == parse_CISS_Handle(devName, handlePtr, &driveNumber))
    {
        supported = true;
    }
    return supported;
}

#if defined (__linux__)
//linux /dev/sg?
// int sg_filter(const struct dirent *entry)
// {
//     return !strncmp("sg", entry->d_name, 2);
// }
//linux /dev/cciss/c?d?
int ciss_filter(const struct dirent *entry)
{
    return !strncmp("cciss/c", entry->d_name, 7);
}

int smartpqi_filter(const struct dirent *entry)
{
    M_USE_UNUSED(entry);
    return 0;
}
#elif defined (__FreeBSD__)
//freeBSD /dev/ciss?
int ciss_filter(const struct dirent *entry)
{
    return !strncmp("ciss", entry->d_name, 4);
}

//freeBSD /dev/smartpqi?
int smartpqi_filter(const struct dirent *entry)
{
    return !strncmp("smartpqi", entry->d_name, 8);
}

#elif defined (__sun)
int ciss_filter(const struct dirent *entry)
{
    //TODO: Figure out exactly what this handle would look like in solaris
    return !strncmp("ciss", entry->d_name, 4);
}

int smartpqi_filter(const struct dirent *entry)
{
    M_USE_UNUSED(entry);
    return 0;
}
#else
#error "Define an OS specific device handle filter here"
#endif //checking for OS macros

#define LUN_ADDR_LEN 8 /*for the physical location data needed to issue commands*/

#define CISS_REPORT_LOGICAL_LUNS_NO_EXTENDED_DATA 0
#define CISS_REPORT_LOGICAL_LUNS_EXTENDED_DATA 1

#define LOGICAL_LUN_DESCRIPTOR_LENGTH 8
#define LOGICAL_LUN_EXTENDED_DESCRIPTOR_LENGTH 24
//NOTE: "Other physical device info" does not indicate a length that I can see different from the 16B node data, but may be different. -TJE

/*
static int ciss_Scsi_Report_Logical_LUNs(tDevice *device, uint8_t extendedDataType, uint8_t* ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    uint8_t cdb[CDB_LEN_12] = { 0 };
    cdb[OPERATION_CODE] = CISS_REPORT_LOGICAL_LUNS_OP;
    cdb[1] = extendedDataType;//can set to receive extended information, but we don't care about this right now...
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(dataLength);
    cdb[7] = M_Byte2(dataLength);
    cdb[8] = M_Byte1(dataLength);
    cdb[9] = M_Byte0(dataLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control byte

    //CDB is created...let's send it!
    if(ptrData && dataLength > 0)
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, ptrData, dataLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    return ret;
}
*/

//TODO: Use what is in the ciss headers, but for now, this is ok.
#define CISS_MAX_PHYSICAL_DRIVES 1024

#define CISS_REPORT_PHYSICAL_LUNS_NO_EXTENDED_DATA 0
#define CISS_REPORT_PHYSICAL_LUNS_EXTENDED_WITH_NODES 1
#define CISS_REPORT_PHYSICAL_LUNS_EXTENDED_WITH_OTHER_INFO 2

#define PHYSICAL_LUN_DESCRIPTOR_LENGTH 8
#define PHYSICAL_LUN_EXTENDED_NODE_DESCRIPTOR_LENGTH 16
//NOTE: "Other physical device info" does not indicate a length that I can see different from the 16B node data, but may be different. -TJE

static int ciss_Scsi_Report_Physical_LUNs(tDevice *device, uint8_t extendedDataType, uint8_t* ptrData, uint32_t dataLength)
{
    int ret = SUCCESS;
    uint8_t cdb[CDB_LEN_12] = { 0 };
    cdb[OPERATION_CODE] = CISS_REPORT_PHYSICAL_LUNS_OP;
    cdb[1] = extendedDataType;//can set to receive extended information, but we don't care about this right now...
    cdb[2] = RESERVED;
    cdb[3] = RESERVED;
    cdb[4] = RESERVED;
    cdb[5] = RESERVED;
    cdb[6] = M_Byte3(dataLength);
    cdb[7] = M_Byte2(dataLength);
    cdb[8] = M_Byte1(dataLength);
    cdb[9] = M_Byte0(dataLength);
    cdb[10] = RESERVED;
    cdb[11] = 0;//control byte

    //CDB is created...let's send it!
    if(ptrData && dataLength > 0)
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, ptrData, dataLength, XFER_DATA_IN, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, NULL, 0, XFER_NO_DATA, device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    return ret;
}

//This is a reworked old function from first internal code to support CISS...is this still needed like this?-TJE
static int get_Physical_Device_Location_Data(tDevice *device, uint8_t *physicalLocationData)
{
    int ret = UNKNOWN;
    uint32_t dataLength = UINT32_C(8) + (PHYSICAL_LUN_DESCRIPTOR_LENGTH * CISS_MAX_PHYSICAL_DRIVES);
    uint8_t * physicalDrives = C_CAST(uint8_t*, calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if(physicalDrives)
    {
        ret = ciss_Scsi_Report_Physical_LUNs(device, CISS_REPORT_PHYSICAL_LUNS_NO_EXTENDED_DATA, physicalDrives, dataLength);
        if (ret == SUCCESS)
        {
            uint32_t lunListLength = M_BytesTo4ByteValue(physicalDrives[0], physicalDrives[1], physicalDrives[2], physicalDrives[3]);
            if (physicalDrives[4] == 0)//regular report. All other values are extended reports that we don't need
            {
                if (device->os_info.cissDeviceData->driveNumber <= (lunListLength / PHYSICAL_LUN_DESCRIPTOR_LENGTH))
                {
                    //should be able to find the device in the data
                    memcpy(physicalLocationData, &physicalDrives[(device->os_info.cissDeviceData->driveNumber + 1) * PHYSICAL_LUN_DESCRIPTOR_LENGTH], LUN_ADDR_LEN);//+1 to get past the header
                    ret = SUCCESS;
                }
                else
                {
                    //invalid physical drive number
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
        }
        safe_Free_aligned(physicalDrives);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

typedef enum _eCISSptCmdType
{
    CISS_CMD_CONTROLLER,
    CISS_CMD_LOGICAL_LUN,//not currently used or supported in this code, but may be added in the future if needed. -TJE
    CISS_CMD_PHYSICAL_LUN
}eCISSptCmdType;

//Standard passthrough
//physicalDriveCmd - true for most things. Only set to false when trying to issue a command directly to the controller.
//                   Only using this flag as part of device discovery. - TJE
static int ciss_Passthrough(ScsiIoCtx * scsiIoCtx, eCISSptCmdType cmdType)
{
    int ret = BAD_PARAMETER;
    if (scsiIoCtx)
    {
        if (scsiIoCtx->cdbLength <= 16 && scsiIoCtx->dataLength <= UINT16_MAX)
        {
            #if defined (__linux__) || defined (__FreeBSD__) //interface is the same on Linux and FreeBSD
                if (scsiIoCtx->device->os_info.cissDeviceData->smartpqi)
                {
                    #if defined (__FreeBSD__)
                        //if the smartpqi bool is set, use the structures from that driver to issue the command to ensure
                        //that the packing matches exactly to minimize compatibility problems.
                        pqi_IOCTL_Command_struct pqiCmd;
                        seatimer_t commandTimer;
                        memset(&commandTimer, 0, sizeof(seatimer_t));
                        memset(&pqiCmd, 0, sizeof(pqi_IOCTL_Command_struct));

                        switch (cmdType)
                        {
                        case CISS_CMD_CONTROLLER:
                            break;
                        default: //this will work OK for other cases. May need modifications for logical volume commands. -TJE
                            //set path to device
                            memcpy(&pqiCmd.LUN_info, scsiIoCtx->device->os_info.cissDeviceData->physicalLocation, LUN_ADDR_LEN);//this is 8 bytes in size maximum
                            break;
                        }
                        //now setup to send a CDB
                        pqiCmd.Request.CDBLen = scsiIoCtx->cdbLength;
                        pqiCmd.Request.Type.Type = TYPE_CMD;//TYPE_MSG also available for BMIC commands, which can be things like resets
                        pqiCmd.Request.Type.Attribute = ATTR_SIMPLE;//Can be UNTAGGED, SIMPLE, HEADOFQUEUE, ORDERED, ACA
                        switch (scsiIoCtx->direction)
                        {
                        case XFER_DATA_IN:
                            pqiCmd.Request.Type.Direction = XFER_READ;
                            pqiCmd.buf_size = scsiIoCtx->dataLength;
                            pqiCmd.buf = scsiIoCtx->pdata;
                            break;
                        case XFER_DATA_OUT:
                            pqiCmd.Request.Type.Direction = XFER_WRITE;
                            pqiCmd.buf_size = scsiIoCtx->dataLength;
                            pqiCmd.buf = scsiIoCtx->pdata;
                            break;
                        case XFER_NO_DATA:
                            pqiCmd.Request.Type.Direction = XFER_NONE;
                            pqiCmd.buf_size = 0;
                            pqiCmd.buf = NULL;
                            break;
                        default:
                            return OS_COMMAND_NOT_AVAILABLE;
                        }

                        if (scsiIoCtx->timeout)
                        {
                            pqiCmd.Request.Timeout = scsiIoCtx->timeout;
                        }
                        else
                        {
                            pqiCmd.Request.Timeout = 15;
                        }
                        memcpy(pqiCmd.Request.CDB, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

                        ret = OS_PASSTHROUGH_FAILURE;//OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

                        start_Timer(&commandTimer);
                        ret = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CCISS_PASSTHRU, &pqiCmd);
                        stop_Timer(&commandTimer);

                        //Copy and sense data we received, then need to check for errors
                        if (scsiIoCtx->psense)
                        {
                            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
                            if (pqiCmd.error_info.SenseLen)
                            {
                                memcpy(scsiIoCtx->psense, pqiCmd.error_info.SenseInfo, pqiCmd.error_info.SenseLen);
                            }
                        }
                        //set command time:
                        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

                        //check for errors to set ret properly
                        switch (pqiCmd.error_info.CommandStatus)
                        {
                        case CMD_SUCCESS:
                        case CMD_TARGET_STATUS:
                        case CMD_DATA_UNDERRUN:
                        case CMD_DATA_OVERRUN:
                            ret = SUCCESS;
                            break;
                        case CMD_INVALID:
                            ret = OS_COMMAND_BLOCKED;
                            break;
                        case CMD_TIMEOUT:
                            ret = OS_COMMAND_TIMEOUT;
                            break;
                        case CMD_PROTOCOL_ERR:
                        case CMD_HARDWARE_ERR:
                        case CMD_CONNECTION_LOST:
                        case CMD_ABORTED:
                        case CMD_ABORT_FAILED:
                        case CMD_UNSOLICITED_ABORT:
                        case CMD_UNABORTABLE:
                        default:
                            ret = OS_PASSTHROUGH_FAILURE;
                            break;
                        }
                    #else
                        ret = BAD_PARAMETER;
                    #endif
                }
                else
                {
                    IOCTL_Command_struct cissCmd;
                    seatimer_t commandTimer;
                    memset(&commandTimer, 0, sizeof(seatimer_t));
                    memset(&cissCmd, 0, sizeof(IOCTL_Command_struct));

                    switch (cmdType)
                    {
                    case CISS_CMD_CONTROLLER:
                        break;
                    default: //this will work OK for other cases. May need modifications for logical volume commands. -TJE
                        //set path to device
                        memcpy(&cissCmd.LUN_info, scsiIoCtx->device->os_info.cissDeviceData->physicalLocation, LUN_ADDR_LEN);//this is 8 bytes in size maximum
                        break;
                    }
                    //now setup to send a CDB
                    cissCmd.Request.CDBLen = scsiIoCtx->cdbLength;
                    cissCmd.Request.Type.Type = TYPE_CMD;//TYPE_MSG also available for BMIC commands, which can be things like resets
                    cissCmd.Request.Type.Attribute = ATTR_SIMPLE;//Can be UNTAGGED, SIMPLE, HEADOFQUEUE, ORDERED, ACA
                    switch (scsiIoCtx->direction)
                    {
                    case XFER_DATA_IN:
                        cissCmd.Request.Type.Direction = XFER_READ;
                        cissCmd.buf_size = scsiIoCtx->dataLength;
                        cissCmd.buf = scsiIoCtx->pdata;
                        break;
                    case XFER_DATA_OUT:
                        cissCmd.Request.Type.Direction = XFER_WRITE;
                        cissCmd.buf_size = scsiIoCtx->dataLength;
                        cissCmd.buf = scsiIoCtx->pdata;
                        break;
                    case XFER_NO_DATA:
                        cissCmd.Request.Type.Direction = XFER_NONE;
                        cissCmd.buf_size = 0;
                        cissCmd.buf = NULL;
                        break;
                    default:
                        return OS_COMMAND_NOT_AVAILABLE;
                    }

                    if (scsiIoCtx->timeout)
                    {
                        cissCmd.Request.Timeout = scsiIoCtx->timeout;
                    }
                    else
                    {
                        cissCmd.Request.Timeout = 15;
                    }
                    memcpy(cissCmd.Request.CDB, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

                    ret = OS_PASSTHROUGH_FAILURE;//OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

                    start_Timer(&commandTimer);
                    ret = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CCISS_PASSTHRU, &cissCmd);
                    stop_Timer(&commandTimer);

                    //Copy and sense data we received, then need to check for errors
                    if (scsiIoCtx->psense)
                    {
                        memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
                        if (cissCmd.error_info.SenseLen)
                        {
                            memcpy(scsiIoCtx->psense, cissCmd.error_info.SenseInfo, cissCmd.error_info.SenseLen);
                        }
                    }
                    //set command time:
                    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

                    //check for errors to set ret properly
                    switch (cissCmd.error_info.CommandStatus)
                    {
                    case CMD_SUCCESS:
                    case CMD_TARGET_STATUS:
                    case CMD_DATA_UNDERRUN:
                    case CMD_DATA_OVERRUN:
                        ret = SUCCESS;
                        break;
                    case CMD_INVALID:
                        ret = OS_COMMAND_BLOCKED;
                        break;
                    case CMD_TIMEOUT:
                        ret = OS_COMMAND_TIMEOUT;
                        break;
                    case CMD_PROTOCOL_ERR:
                    case CMD_HARDWARE_ERR:
                    case CMD_CONNECTION_LOST:
                    case CMD_ABORTED:
                    case CMD_ABORT_FAILED:
                    case CMD_UNSOLICITED_ABORT:
                    case CMD_UNABORTABLE:
                    default:
                        ret = OS_PASSTHROUGH_FAILURE;
                        break;
                    }
                }
                //TODO: More error handling???
            #elif defined (__sun)
                cpqary3_scsi_pass_t cissCmd;
                seatimer_t commandTimer;
                memset(&commandTimer, 0, sizeof(seatimer_t));
                memset(&cissCmd, 0, sizeof(cpqary3_scsi_pass_t));

                switch (cmdType)
                {
                case CISS_CMD_CONTROLLER:
                    break;
                default: //this will work OK for other cases. May need modifications for logical volume commands. -TJE
                    //set path to device
                    memcpy(&cissCmd.lun_addr, scsiIoCtx->device->os_info.cissDeviceData->physicalLocation, LUN_ADDR_LEN);//this is 8 bytes in size maximum
                    break;
                }

                //now setup to send a CDB
                cissCmd.cdb_len = scsiIoCtx->cdbLength;
                //NOTE: The .buf is a uint64. Not sure if this should be a pointer value, or overallocated for buffer space
                //      currently code assumes that it should be the pointer value.
                switch (scsiIoCtx->direction)
                {
                case XFER_DATA_IN:
                    cissCmd.io_direction = 0;//in is zero
                    cissCmd.buf_len = scsiIoCtx->dataLength;
                    cissCmd.buf = C_CAST(uintptr_t, scsiIoCtx->pdata);
                    break;
                case XFER_DATA_OUT:
                    cissCmd.io_direction = 1;//1 for out
                    cissCmd.buf_len = scsiIoCtx->dataLength;
                    cissCmd.buf = C_CAST(uintptr_t, scsiIoCtx->pdata);
                    break;
                case XFER_NO_DATA:
                    cissCmd.Request.Type.Direction = 0;//setting 0 for in
                    cissCmd.buf_len = 0;
                    cissCmd.buf = C_CAST(uintptr_t, NULL);
                    break;
                default:
                    return OS_COMMAND_NOT_AVAILABLE;
                }

                if (scsiIoCtx->timeout)
                {
                    cissCmd.Timeout = scsiIoCtx->timeout;
                }
                else
                {
                    cissCmd.Timeout = 15;
                }
                memcpy(cissCmd.cdb, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

                ret = OS_PASSTHROUGH_FAILURE;//OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

                start_Timer(&commandTimer);
                ret = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CPQARY3_IOCTL_SCSI_PASS, &cissCmd);
                stop_Timer(&commandTimer);

                //Copy and sense data we received, then need to check for errors
                if (scsiIoCtx->psense)
                {
                    memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
                    if (cissCmd.err_info.SenseLen)
                    {
                        memcpy(scsiIoCtx->psense, cissCmd.err_info.SenseInfo, cissCmd.err_info.SenseLen);
                    }
                }
                //set command time:
                scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

                //check for errors to set ret properly
                switch (cissCmd.error_info.CommandStatus)
                {
                case CMD_SUCCESS:
                    ret = SUCCESS;
                    break;
                case CMD_TARGET_STATUS:
                case CMD_DATA_UNDERRUN:
                case CMD_DATA_OVERRUN:
                    printf("Data Error\n");
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                case CMD_INVALID:
                    ret = OS_COMMAND_BLOCKED;
                    break;
                case CMD_TIMEOUT:
                    ret = OS_COMMAND_TIMEOUT;
                    break;
                case CMD_PROTOCOL_ERR:
                case CMD_HARDWARE_ERR:
                case CMD_CONNECTION_LOST:
                case CMD_ABORTED:
                case CMD_ABORT_FAILED:
                case CMD_UNSOLICITED_ABORT:
                case CMD_UNABORTABLE:
                default:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }

                //TODO: More error handling???
            #else
                ret = OS_COMMAND_NOT_AVAILABLE;
            #endif //checking __linux__, __FreeBSD__, __sun
        }
        else
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
    }
    return ret;
}

//Support big passthrough for Linux. Currently not found in other header files.
#if defined (CCISS_BIG_PASSTHRU)
static int ciss_Big_Passthrough(ScsiIoCtx * scsiIoCtx, eCISSptCmdType cmdType)
{
    int ret = BAD_PARAMETER;
    if (scsiIoCtx)
    {
        if (scsiIoCtx->cdbLength <= 16 && scsiIoCtx->dataLength <= MAX_KMALLOC_SIZE)
        {
            BIG_IOCTL_Command_struct cissCmd;
            seatimer_t commandTimer;
            memset(&commandTimer, 0, sizeof(seatimer_t));
            memset(&cissCmd, 0, sizeof(BIG_IOCTL_Command_struct));

            switch (cmdType)
            {
            case CISS_CMD_CONTROLLER:
                break;
            default: //this will work OK for other cases. May need modifications for logical volume commands. -TJE
                //set path to device
                memcpy(&cissCmd.LUN_info, scsiIoCtx->device->os_info.cissDeviceData->physicalLocation, LUN_ADDR_LEN);//this is 8 bytes in size maximum
                break;
            }
                                                                                                          //now setup to send a CDB
            cissCmd.Request.CDBLen = scsiIoCtx->cdbLength;
            cissCmd.Request.Type.Type = TYPE_CMD;//TYPE_MSG also available for BMIC commands, which can be things like resets
            cissCmd.Request.Type.Attribute = ATTR_SIMPLE;//Can be UNTAGGED, SIMPLE, HEADOFQUEUE, ORDERED, ACA
            switch (scsiIoCtx->direction)
            {
            case XFER_DATA_IN:
                cissCmd.Request.Type.Direction = XFER_READ;
                cissCmd.malloc_size = scsiIoCtx->dataLength;//TODO: CHeck if this is correct or a different size should be here.
                cissCmd.buf_size = scsiIoCtx->dataLength;
                cissCmd.buf = scsiIoCtx->pdata;
                break;
            case XFER_DATA_OUT:
                cissCmd.Request.Type.Direction = XFER_WRITE;
                cissCmd.malloc_size = scsiIoCtx->dataLength;//TODO: CHeck if this is correct or a different size should be here.
                cissCmd.buf_size = scsiIoCtx->dataLength;
                cissCmd.buf = scsiIoCtx->pdata;
                break;
            case XFER_NO_DATA:
                cissCmd.Request.Type.Direction = XFER_NONE;
                cissCmd.buf_size = 0;
                cissCmd.buf = NULL;
                break;
            default:
                return OS_COMMAND_NOT_AVAILABLE;
            }

            if (scsiIoCtx->timeout)
            {
                cissCmd.Request.Timeout = scsiIoCtx->timeout;
            }
            else
            {
                cissCmd.Request.Timeout = 15;
            }
            memcpy(cissCmd.Request.CDB, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

            ret = OS_PASSTHROUGH_FAILURE;//OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

            start_Timer(&commandTimer);
            ret = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CCISS_BIG_PASSTHRU, &cissCmd);
            stop_Timer(&commandTimer);

            //Copy and sense data we received, then need to check for errors
            if (scsiIoCtx->psense)
            {
                memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
                if (cissCmd.error_info.SenseLen)
                {
                    memcpy(scsiIoCtx->psense, cissCmd.error_info.SenseInfo, cissCmd.error_info.SenseLen);
                }
            }
            //set command time:
            scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

            //check for errors to set ret properly
            switch (cissCmd.error_info.CommandStatus)
            {
            case CMD_SUCCESS:
                ret = SUCCESS;
                break;
            case CMD_TARGET_STATUS:
            case CMD_DATA_UNDERRUN:
            case CMD_DATA_OVERRUN:
                printf("Data Error\n");
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            case CMD_INVALID:
                ret = OS_COMMAND_BLOCKED;
                break;
            case CMD_TIMEOUT:
                ret = OS_COMMAND_TIMEOUT;
                break;
            case CMD_PROTOCOL_ERR:
            case CMD_HARDWARE_ERR:
            case CMD_CONNECTION_LOST:
            case CMD_ABORTED:
            case CMD_ABORT_FAILED:
            case CMD_UNSOLICITED_ABORT:
            case CMD_UNABORTABLE:
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }

            //TODO: More error handling???
        }
        else
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
    }
    return ret;
}
#endif //CCISS_BIG_PASSTHRU

int issue_io_ciss_Dev(ScsiIoCtx * scsiIoCtx)
{
    if (scsiIoCtx->device->os_info.cissDeviceData)
    {
    #if defined (CCISS_BIG_PASSTHRU)
        //use big passthrough only when making a large enough data transfer to matter - TJE
        if (scsiIoCtx->dataLength > UINT16_MAX && scsiIoCtx->device->os_info.cissDeviceData->bigPassthroughAvailable)
        {
            return ciss_Big_Passthrough(scsiIoCtx, CISS_CMD_PHYSICAL_LUN);
        }
        else
    #endif //#if defined (CCISS_BIG_PASSTHRU)
        {
            return ciss_Passthrough(scsiIoCtx, CISS_CMD_PHYSICAL_LUN);
        }
    }
    else
    {
        return BAD_PARAMETER;
    }
}

int get_CISS_RAID_Device(const char *filename, tDevice *device)
{
    int ret = FAILURE;
    uint16_t driveNumber = 0;
    char osHandle[OS_CISS_HANDLE_MAX_LENGTH] = { 0 };
    char *handlePtr = &osHandle[0];//this is done to prevent warnings
    //Need to open this handle and setup some information then fill in the device information.
    if (!(validate_Device_Struct(device->sanity)))
    {
        return LIBRARY_MISMATCH;
    }
    //set the name that was provided for other display.
    memcpy(device->os_info.name, filename, strlen(filename));
    if (PARSE_COUNT_SUCCESS == parse_CISS_Handle(filename, handlePtr, &driveNumber))
    {
        device->os_info.cissDeviceData = calloc(1, sizeof(cissDeviceInfo));
        if (device->os_info.cissDeviceData)
        {
            if ((device->os_info.cissDeviceData->cissHandle = open(handlePtr, O_RDWR | O_NONBLOCK)) >= 0)
            {
                //check that CISS IOCTLs are available. If not, then we don't want to proceed. 
                //There is danger in attempting a vendor unique op code as we don't know how the target will respond to it unless we know it really is a CISS device
                if (supports_CISS_IOCTLs(device->os_info.cissDeviceData->cissHandle))
                {
                    //setup all the things we need to be able to issue commands in the code
                    device->os_info.cissDeviceData->driveNumber = driveNumber;
                    device->issue_io = C_CAST(issue_io_func, issue_io_ciss_Dev);
                    device->drive_info.drive_type = SCSI_DRIVE;
                    device->drive_info.interface_type = RAID_INTERFACE;
                    strncpy(&device->os_info.name[0], filename, M_Min(strlen(filename), 30));
                    strncpy(&device->os_info.friendlyName[0], filename, M_Min(strlen(filename), 20));
                    device->os_info.minimumAlignment = sizeof(void*);
                    device->os_info.cissDeviceData->smartpqi = is_SmartPQI_Unique_IOCTLs_Supported(device->os_info.cissDeviceData->cissHandle);
                    device->os_info.fd = device->os_info.cissDeviceData->cissHandle;//set this just to make the upper layers that validate this happy-TJE
                    device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA = true;//this may not be true for all CISS controllers, but will likely work with any of them -TJE
                    //handle opened, now get the physical device location from the C2h command
                    //This is done here to reuse lots of other code to issue commands.
                    if (SUCCESS == (ret = get_Physical_Device_Location_Data(device, device->os_info.cissDeviceData->physicalLocation)))
                    {
                        //finally call fill_Drive_Info to let opensea-transport set it's support bits and other device information
                        ret = fill_Drive_Info_Data(device);
                    }
                    else
                    {
                        //something went wrong, so clean up.
                        close(device->os_info.cissDeviceData->cissHandle);
                        device->os_info.fd = 0;
                        safe_Free(device->os_info.cissDeviceData);
                    }
                }
                else
                {
                    ret = NOT_SUPPORTED;
                    close(device->os_info.cissDeviceData->cissHandle);
                    safe_Free(device->os_info.cissDeviceData);
                }
            }
            else
            {
                ret = FILE_OPEN_ERROR;
            }
        }
    }

    return ret;
}

int close_CISS_RAID_Device(tDevice *device)
{
    if (device && device->os_info.cissDeviceData)
    {
        if (close(device->os_info.cissDeviceData->cissHandle))
        {
            device->os_info.last_error = errno;
        }
        else
        {
            device->os_info.cissDeviceData->cissHandle = -1;
            device->os_info.last_error = 0;
        }
        device->os_info.fd = -1;
        safe_Free(device->os_info.cissDeviceData);
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

static int get_CISS_Physical_LUN_Count(int fd, uint32_t *count)
{
    //This is a special function only used by get device count and get device list.
    //It creates a psuedo tDevice to issue a scsiIoCtx structure to passthrough and read this.
    //This keeps the rest of the code simpler for now
    int ret = SUCCESS;
    if(count)
    {
        tDevice pseudoDev;
        ScsiIoCtx physicalLunCMD;
        uint8_t cdb[12] = { 0 };
        uint32_t dataLength = (CISS_MAX_PHYSICAL_DRIVES * PHYSICAL_LUN_DESCRIPTOR_LENGTH) + 8;//8 byte header
        uint8_t *data = C_CAST(uint8_t*, calloc_aligned(dataLength, sizeof(uint8_t), sizeof(void*)));
        if (data)
        {
            //setup the psuedo device
            memset(&pseudoDev, 0, sizeof(tDevice));
            pseudoDev.os_info.fd = fd;
            pseudoDev.sanity.version = DEVICE_BLOCK_VERSION;
            pseudoDev.sanity.size = sizeof(tDevice);

            //setup the cdb
            cdb[OPERATION_CODE] = CISS_REPORT_PHYSICAL_LUNS_OP;
            cdb[1] = 0;//no extended data as it is not needed and this will maximize compatibility with controllers and firmwares.
            cdb[2] = RESERVED;
            cdb[3] = RESERVED;
            cdb[4] = RESERVED;
            cdb[5] = RESERVED;
            cdb[6] = M_Byte3(dataLength);
            cdb[7] = M_Byte2(dataLength);
            cdb[8] = M_Byte1(dataLength);
            cdb[9] = M_Byte0(dataLength);
            cdb[10] = RESERVED;
            cdb[11] = 0;//control byte

            //setup the scsiIoCtx
            memset(&physicalLunCMD, 0, sizeof(ScsiIoCtx));        
            physicalLunCMD.device = &pseudoDev;
            physicalLunCMD.direction = XFER_DATA_IN;
            physicalLunCMD.pdata = data;
            physicalLunCMD.psense = pseudoDev.drive_info.lastCommandSenseData;
            physicalLunCMD.senseDataSize = SPC3_SENSE_LEN;
            physicalLunCMD.timeout = 15;
            physicalLunCMD.dataLength = dataLength;
            physicalLunCMD.cdbLength = 12;
            memcpy(physicalLunCMD.cdb, cdb, 12);

            //setup the cissDeviceData struct as it is needed to issue the CMD
            pseudoDev.os_info.cissDeviceData = calloc(1, sizeof(cissDeviceInfo));
            pseudoDev.os_info.cissDeviceData->cissHandle = fd;
            pseudoDev.os_info.cissDeviceData->smartpqi = is_SmartPQI_Unique_IOCTLs_Supported(pseudoDev.os_info.cissDeviceData->cissHandle);

            //issue the passthrough command (big passthrough not used here - TJE)
            ret = ciss_Passthrough(&physicalLunCMD, CISS_CMD_CONTROLLER);

            //done with this memory now, so clean it up
            safe_Free(pseudoDev.os_info.cissDeviceData);

            //print_Data_Buffer(data, dataLength, false);

            if (ret == SUCCESS)
            {
                uint32_t lunListLength = M_BytesTo4ByteValue(data[0], data[1], data[2], data[3]);
                if (data[4] == 0)//regular report. All other values are extended reports that we don't need
                {
                    *count = lunListLength / PHYSICAL_LUN_DESCRIPTOR_LENGTH;
                }
                else
                {
                    perror("Unknown data return format when looking for CISS devices.\n");
                    *count = 0;
                }
            }

            safe_Free_aligned(data);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
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
//!   \param[in] beginningOfList = list of handles to use to check the count. This can prevent duplicate devices if we know some handles should not be looked at.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_CISS_RAID_Device_Count(uint32_t * numberOfDevices, M_ATTR_UNUSED uint64_t flags, ptrRaidHandleToScan *beginningOfList)
{
    int fd = -1;
    ptrRaidHandleToScan raidList = NULL;
    ptrRaidHandleToScan previousRaidListEntry = NULL;
    uint32_t found = 0;
    char deviceName[CISS_HANDLE_MAX_LENGTH] = { 0 };


    if (!beginningOfList || !*beginningOfList)
    {
        //don't do anything. Only scan when we get a list to use.
        //Each OS that want's to do this should generate a list of handles to look for.
        return SUCCESS;
    }

    raidList = *beginningOfList;

    while (raidList)
    {
        bool handleRemoved = false;
        if (raidList->raidHint.cissRAID || raidList->raidHint.unknownRAID)
        {
            memset(deviceName, 0, CISS_HANDLE_MAX_LENGTH);
            //check if the passed in handle contains /dev/ or not so we can open it correctly
            if (strstr(raidList->handle, "/dev/"))
            {
                snprintf(deviceName, CISS_HANDLE_MAX_LENGTH, "%s", raidList->handle);
            }
            else
            {
                snprintf(deviceName, CISS_HANDLE_MAX_LENGTH, "/dev/%s", raidList->handle);
            }
            if ((fd = open(deviceName, O_RDWR | O_NONBLOCK)) >= 0)//TODO: Verify O_NONBLOCK on FreeBSD and/or Solaris
            {
                if (supports_CISS_IOCTLs(fd))
                {
                    //responded with success to reading some basic info
                    //Need to call get_Device() and issue this SCSI CDB to read the physical location data, which will tell us how many
                    //IDs are available and we will then have a count of devices on this handle.
                    uint32_t countDevsOnThisHandle = 0;
                    //read the physical location data and count how many IDs are reported
                    if (SUCCESS == get_CISS_Physical_LUN_Count(fd, &countDevsOnThisHandle))
                    {
                        found += countDevsOnThisHandle;
                    }
                    //else something went wrong, and cannot enumerate additional devices.

                    //This was a CISS handle, remove it from the list!
                    //This will also increment us to the next handle
                    bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                    raidList = remove_RAID_Handle(raidList, previousRaidListEntry);
                    if (pointerAtBeginningOfRAIDList)
                    {
                        //if the first entry in the list was removed, we need up update the pointer before we exit so that the code that called here won't have an invalid pointer
                        *beginningOfList = raidList;
                    }
                    handleRemoved = true;
                }
                //close handle to the controller
                close(fd);
                fd = -1;
            }
            else
            {
                printf("Failed to open handle with error: ");
                print_Errno_To_Screen(errno);
                printf("\n");
            }
        }
        if (!handleRemoved)
        {
            previousRaidListEntry = raidList;//store handle we just looked at in case we need to remove one from the list
            //increment to next element in the list
            raidList = raidList->next;
        }
    }
    *numberOfDevices = found;
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
//!   \return SUCCESS - pass, WARN_NOT_ALL_DEVICES_ENUMERATED - some deviec had trouble being enumerated. 
//!                     Validate that it's drive_type is not UNKNOWN_DRIVE, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_CISS_RAID_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags, ptrRaidHandleToScan *beginningOfList)
{
    int returnValue = SUCCESS;
    if (!beginningOfList || !*beginningOfList)
    {
        //don't do anything. Only scan when we get a list to use.
        //Each OS that want's to do this should generate a list of handles to look for.
        return SUCCESS;
    }

    if (!(ptrToDeviceList) || (!sizeInBytes))
    {
        return BAD_PARAMETER;
    }
    else if ((!(validate_Device_Struct(ver))))
    {
        return LIBRARY_MISMATCH;
    }
    else
    {
        tDevice * d = ptrToDeviceList;
        ptrRaidHandleToScan raidList = *beginningOfList;
        ptrRaidHandleToScan previousRaidListEntry = NULL;
        int fd = -1;
        uint32_t numberOfDevices = sizeInBytes / sizeof(tDevice);
        uint32_t found = 0, failedGetDeviceCount = 0;
        char deviceName[CISS_HANDLE_MAX_LENGTH] = { 0 };
        while (raidList && found < numberOfDevices)
        {
            bool handleRemoved = false;
            if (raidList->raidHint.cissRAID || raidList->raidHint.unknownRAID)
            {
                memset(deviceName, 0, CISS_HANDLE_MAX_LENGTH);
                if (strstr(raidList->handle, "/dev/"))
                {
                    snprintf(deviceName, CISS_HANDLE_MAX_LENGTH, "%s", raidList->handle);
                }
                else
                {
                    snprintf(deviceName, CISS_HANDLE_MAX_LENGTH, "/dev/%s", raidList->handle);
                }
                if ((fd = open(deviceName, O_RDWR | O_NONBLOCK)) >= 0)//TODO: Verify O_NONBLOCK on FreeBSD and/or Solaris
                {
                    if (supports_CISS_IOCTLs(fd))
                    {
                        //responded with success to reading some basic info
                        //Need to call get_Device() and issue this SCSI CDB to read the physical location data, which will tell us how many
                        //IDs are available and we will then have a count of devices on this handle.
                        uint32_t countDevsOnThisHandle = 0;
                        //read the physical location data and count how many IDs are reported
                        if (SUCCESS == get_CISS_Physical_LUN_Count(fd, &countDevsOnThisHandle))
                        {
                            //from here we need to do get_CISS_Device on each of these available physical LUNs
                            for (uint32_t currentDev = 0; currentDev < countDevsOnThisHandle; ++currentDev)
                            {
                                char handle[CISS_HANDLE_MAX_LENGTH] = { 0 };
                                //handle is formatted as "ciss:os_handle:driveNumber" NOTE: osHandle is everything after /dev/
                                snprintf(handle, CISS_HANDLE_MAX_LENGTH, "ciss:%s:%" PRIu32, basename(deviceName), currentDev);
                                //get the CISS device with a get_CISS_Device function
                                memset(d, 0, sizeof(tDevice));
                                d->sanity.size = ver.size;
                                d->sanity.version = ver.version;
                                d->dFlags = flags;
                                int ret = get_CISS_RAID_Device(handle, d);
                                if (ret != SUCCESS)
                                {
                                    failedGetDeviceCount++;
                                }
                                ++d;
                                //If we were unable to open the device using get_CSMI_Device, then  we need to increment the failure counter. - TJE
                                ++found;
                            }
                        }
                        //else something went wrong, and cannot enumerate additional devices.

                        //This was a CISS handle, remove it from the list!
                        //This will also increment us to the next handle
                        bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                        raidList = remove_RAID_Handle(raidList, previousRaidListEntry);
                        if (pointerAtBeginningOfRAIDList)
                        {
                            //if the first entry in the list was removed, we need up update the pointer before we exit so that the code that called here won't have an invalid pointer
                            *beginningOfList = raidList;
                        }
                        handleRemoved = true;
                    }
                    //close handle to the controller
                    close(fd);
                    fd = -1;
                }
            }
            if (!handleRemoved)
            {
                previousRaidListEntry = raidList;//store handle we just looked at in case we need to remove one from the list
                //increment to next element in the list
                raidList = raidList->next;
            }
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (failedGetDeviceCount)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    return returnValue;
}
#endif //ENABLE_CISS
