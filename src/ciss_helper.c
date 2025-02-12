// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2021-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file ciss_helper.c
// \brief Defines the constants structures to help with CISS implementation. This attempts to be generic for any
// unix-like OS. Windows support is through CSMI.

#if defined(ENABLE_CISS)

#    include "bit_manip.h"
#    include "code_attributes.h"
#    include "common_types.h"
#    include "error_translation.h"
#    include "io_utils.h"
#    include "math_utils.h"
#    include "memory_safety.h"
#    include "precision_timer.h"
#    include "string_utils.h"
#    include "type_conversion.h"

#    if defined(__unix__) // this is only done in case someone sets weird defines for Windows even though this isn't
                          // supported
#        include <ctype.h>
#        include <dirent.h>
#        include <fcntl.h>
#        include <libgen.h> //for basename function
#        include <sys/ioctl.h>
#        include <sys/stat.h>
#        include <sys/types.h>
#        include <unistd.h> // for close
#    endif                  //__unix__

// Includes vary by platform
#    if defined(__linux__)
// define this macro before these includes. Doesn't need to match exactly, just make the compiler happy.-TJE
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
// Disabling clang-tidy here since this is needed before the includes to make the compiler happy
#        ifndef __user
#            define __user
#        endif //__user
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
// usr/include/linux has the cciss_defs and cciss_ioctl files.
// Include these instead of our own copy.
#        include <linux/cciss_defs.h>
#        include <linux/cciss_ioctl.h>
#    elif defined(__FreeBSD__)
#        include "external/ciss/freebsd/cissio.h"
// https://github.com/FreeBSDDesktop/freebsd-base/blob/master/sys/dev/smartpqi/smartpqi_defines.h
//       looks compatible with ciss, but may need some changes to support.-TJE
#    elif defined(__sun)
#        include "external/ciss/solaris/cpqary3.h"
#        include "external/ciss/solaris/cpqary3_ioctl.h"
#    else // not a supported OS for CISS
#        pragma message "CISS support is not available for this OS"
#    endif // checking platform specific macros

#    include "ciss_helper.h"
#    include "ciss_helper_func.h"
#    include "scsi_helper_func.h"

extern bool validate_Device_Struct(versionBlock);

static bool is_SmartPQI_Unique_IOCTLs_Supported(int fd)
{
#    if defined(__FreeBSD__)
    pqi_pci_info_t pciInfo;
    safe_memset(&pciInfo, sizeof(pqi_pci_info_t), 0, sizeof(pqi_pci_info_t));
    DISABLE_WARNING_SIGN_CONVERSION
    if (0 == ioctl(fd, SMARTPQI_GETPCIINFO, &pciInfo))
        RESTORE_WARNING_SIGN_CONVERSION
        {
            supported = true;
        }
    else
#    endif //__FreeBSD__
    {
        M_USE_UNUSED(fd);
        return false;
    }
}

// This function helps the scan and print determine if we can send ciss IOCTLs.
// All it does is attempts an easy ioctl to get some basic info from the controller, which may vary between OSs
static bool supports_CISS_IOCTLs(int fd)
{
    bool supported = false;
#    if defined(__linux__) || defined(__FreeBSD__)
    cciss_pci_info_struct pciInfo;
    safe_memset(&pciInfo, sizeof(cciss_pci_info_struct), 0, sizeof(cciss_pci_info_struct));
    DISABLE_WARNING_SIGN_CONVERSION
    if (0 == ioctl(fd, CCISS_GETPCIINFO, &pciInfo))
    {
        supported = true;
    }
    RESTORE_WARNING_SIGN_CONVERSION
// If Linux SMARTPQI does not respond to this, maybe try some of the other non-passthrough IOCTLs to see if we get a
// valid response-TJE
#        if defined(__FreeBSD__)
    else
    {
        return is_SmartPQI_Unique_IOCTLs_Supported(fd);
    }
#        endif
#    elif defined(__sun)
    cpqary3_ctlr_info_t ctrlInfo;
    safe_memset(&ctrlInfo, sizeof(cpqary3_ctlr_info_t), 0, sizeof(cpqary3_ctlr_info_t));
    DISABLE_WARNING_SIGN_CONVERSION
    if (0 == ioctl(fd, CPQARY3_IOCTL_CTLR_INFO, &ctrlInfo))
    {
        supported = true;
    }
    RESTORE_WARNING_SIGN_CONVERSION
#    endif
    return supported;
}

#    define OS_CISS_HANDLE_MAX_LENGTH 20

// creates /dev/sg? or /dev/cciss/c?d? or /dev/ciss/?
static bool create_OS_CISS_Handle_Name(const char* input, char* osHandle)
{
    bool success = false;
    if (input != M_NULLPTR)
    {
        if (strncmp(input, "sg", 2) == 0 || strncmp(input, "ciss", 4) == 0 || strncmp(input, "smartpqi", 8) == 0)
        {
            // linux SG handle /dev/sg? or freeBSD ciss handle: /dev/ciss?
            snprintf_err_handle(osHandle, OS_CISS_HANDLE_MAX_LENGTH, "/dev/%s", input);
            success = true;
        }
        else
        {
            // check for linux cciss handle: /dev/cciss/c?d?
            // in this case, the input should only be c?d?
            uint16_t controller = UINT16_C(0);
            uint16_t device     = UINT16_C(0);
            // get handle values using strtoul
            if (input[0] == 'c')
            {
                char* endptr = M_NULLPTR;
                // need to update str pointer as we parse the handle, but not changing any data
                char*         str   = M_CONST_CAST(char*, input) + 1;
                unsigned long value = 0UL;
                if (0 != safe_strtoul(&value, str, &endptr, BASE_10_DECIMAL) ||
                    (value > UINT16_MAX)) // this should not happen for this format)
                {
                    success = false;
                }
                else
                {
                    controller = C_CAST(uint16_t, value);
                    if (endptr && endptr[0] == 'd')
                    {
                        str = endptr + 1;
                        if (0 != safe_strtoul(&value, str, &endptr, BASE_10_DECIMAL) || (value > UINT16_MAX))
                        {
                            success = false;
                        }
                        else
                        {
                            device = C_CAST(uint16_t, value);
                        }
                    }
                    else
                    {
                        success = false;
                    }
                }
            }
            else
            {
                success = false;
            }
            if (success)
            {
                snprintf_err_handle(osHandle, OS_CISS_HANDLE_MAX_LENGTH, "/dev/cciss/c%" PRIu16 "d%" PRIu16, controller,
                                    device);
            }
        }
    }
    return success;
}

#    define PARSE_COUNT_SUCCESS 3

static uint8_t parse_CISS_Handle(const char* devName, char* osHandle, uint16_t* physicalDriveNumber)
{
    uint8_t parseCount = UINT8_C(0);
    if (devName != M_NULLPTR)
    {
        // TODO: Check the format of the handle to see if it is ciss format that can be supported
        char*   dup    = M_NULLPTR;
        errno_t duperr = safe_strdup(&dup, devName);
        if (duperr == 0 && dup != M_NULLPTR && strstr(dup, CISS_HANDLE_BASE_NAME) == dup)
        {
            // starts with ciss, so now we should check to make sure we found everything else
            uint8_t counter = UINT8_C(0);
            char*   saveptr = M_NULLPTR;
            rsize_t duplen  = safe_strlen(dup);
            char*   token   = safe_String_Token(dup, &duplen, ":", &saveptr);
            while (token && counter < UINT8_C(3))
            {
                switch (counter)
                {
                case 0: // ciss - already been validated above
                    ++parseCount;
                    break;
                case 1: // partial system handle...need to append /dev/ to the front at least. ciss will need
                        // /dev/cciss/<> varies a little bit based on the format here
                    if (create_OS_CISS_Handle_Name(token, osHandle))
                    {
                        ++parseCount;
                    }
                    break;
                case 2: // physical drive number
                    if (safe_isdigit(token[0]))
                    {
                        unsigned long temp = 0UL;
                        if (0 == safe_strtoul(&temp, token, M_NULLPTR, BASE_10_DECIMAL))
                        {
                            *physicalDriveNumber = C_CAST(uint16_t, temp);
                            ++parseCount;
                        }
                    }
                    break;
                default:
                    break;
                }
                ++counter;
                token = safe_String_Token(M_NULLPTR, &duplen, ":", &saveptr);
            }
        }
        safe_free(&dup);
    }
    return parseCount;
}

bool is_Supported_ciss_Dev(const char* devName)
{
    bool     supported   = false;
    uint16_t driveNumber = UINT16_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, osHandle, OS_CISS_HANDLE_MAX_LENGTH);
    char* handlePtr = &osHandle[0]; // this is done to prevent warnings
    if (PARSE_COUNT_SUCCESS == parse_CISS_Handle(devName, handlePtr, &driveNumber))
    {
        supported = true;
    }
    return supported;
}

#    if defined(__linux__)
// linux /dev/sg?
//  int sg_filter(const struct dirent *entry)
//  {
//      return !strncmp("sg", entry->d_name, 2);
//  }
// linux /dev/cciss/c?d?
int ciss_filter(const struct dirent* entry)
{
    return !strncmp("cciss/c", entry->d_name, 7);
}

int smartpqi_filter(const struct dirent* entry)
{
    M_USE_UNUSED(entry);
    return 0;
}
#    elif defined(__FreeBSD__)
// freeBSD /dev/ciss?
int ciss_filter(const struct dirent* entry)
{
    return !strncmp("ciss", entry->d_name, 4);
}

// freeBSD /dev/smartpqi?
int smartpqi_filter(const struct dirent* entry)
{
    return !strncmp("smartpqi", entry->d_name, 8);
}

#    elif defined(__sun)
int ciss_filter(const struct dirent* entry)
{
    // TODO: Figure out exactly what this handle would look like in solaris
    return !strncmp("ciss", entry->d_name, 4);
}

int smartpqi_filter(const struct dirent* entry)
{
    M_USE_UNUSED(entry);
    return 0;
}
#    else
#        error "Define an OS specific device handle filter here"
#    endif // checking for OS macros

#    define LUN_ADDR_LEN                              8 /*for the physical location data needed to issue commands*/

#    define CISS_REPORT_LOGICAL_LUNS_NO_EXTENDED_DATA 0
#    define CISS_REPORT_LOGICAL_LUNS_EXTENDED_DATA    1

#    define LOGICAL_LUN_DESCRIPTOR_LENGTH             8
#    define LOGICAL_LUN_EXTENDED_DESCRIPTOR_LENGTH    24
// NOTE: "Other physical device info" does not indicate a length that I can see different from the 16B node data, but
// may be different. -TJE

/*
static eReturnValues ciss_Scsi_Report_Logical_LUNs(tDevice *device, uint8_t extendedDataType, uint8_t* ptrData, uint32_t
dataLength)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
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
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, ptrData, dataLength, XFER_DATA_IN,
device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, M_NULLPTR, 0, XFER_NO_DATA,
device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    return ret;
}
*/

// TODO: Use what is in the ciss headers, but for now, this is ok.
#    define CISS_MAX_PHYSICAL_DRIVES                           1024

#    define CISS_REPORT_PHYSICAL_LUNS_NO_EXTENDED_DATA         0
#    define CISS_REPORT_PHYSICAL_LUNS_EXTENDED_WITH_NODES      1
#    define CISS_REPORT_PHYSICAL_LUNS_EXTENDED_WITH_OTHER_INFO 2

#    define PHYSICAL_LUN_DESCRIPTOR_LENGTH                     8
#    define PHYSICAL_LUN_EXTENDED_NODE_DESCRIPTOR_LENGTH       16
// NOTE: "Other physical device info" does not indicate a length that I can see different from the 16B node data, but
// may be different. -TJE

static eReturnValues ciss_Scsi_Report_Physical_LUNs(tDevice* device,
                                                    uint8_t  extendedDataType,
                                                    uint8_t* ptrData,
                                                    uint32_t dataLength)
{
    eReturnValues ret = SUCCESS;
    DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, CDB_LEN_12);
    cdb[OPERATION_CODE] = CISS_REPORT_PHYSICAL_LUNS_OP;
    cdb[1]  = extendedDataType; // can set to receive extended information, but we don't care about this right now...
    cdb[2]  = RESERVED;
    cdb[3]  = RESERVED;
    cdb[4]  = RESERVED;
    cdb[5]  = RESERVED;
    cdb[6]  = M_Byte3(dataLength);
    cdb[7]  = M_Byte2(dataLength);
    cdb[8]  = M_Byte1(dataLength);
    cdb[9]  = M_Byte0(dataLength);
    cdb[10] = RESERVED;
    cdb[11] = 0; // control byte

    // CDB is created...let's send it!
    if (ptrData && dataLength > 0)
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, ptrData, dataLength, XFER_DATA_IN,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    else
    {
        ret = scsi_Send_Cdb(device, cdb, CDB_LEN_12, M_NULLPTR, 0, XFER_NO_DATA,
                            device->drive_info.lastCommandSenseData, SPC3_SENSE_LEN, 15);
    }
    return ret;
}

// This is a reworked old function from first internal code to support CISS...is this still needed like this?-TJE
static eReturnValues get_Physical_Device_Location_Data(tDevice* device,
                                                       uint8_t* physicalLocationData,
                                                       uint32_t physicalLocationDataLength)
{
    eReturnValues ret            = UNKNOWN;
    uint32_t      dataLength     = UINT32_C(8) + (PHYSICAL_LUN_DESCRIPTOR_LENGTH * CISS_MAX_PHYSICAL_DRIVES);
    uint8_t*      physicalDrives = M_REINTERPRET_CAST(
        uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (physicalDrives != M_NULLPTR)
    {
        ret = ciss_Scsi_Report_Physical_LUNs(device, CISS_REPORT_PHYSICAL_LUNS_NO_EXTENDED_DATA, physicalDrives,
                                             dataLength);
        if (ret == SUCCESS)
        {
            uint32_t lunListLength =
                M_BytesTo4ByteValue(physicalDrives[0], physicalDrives[1], physicalDrives[2], physicalDrives[3]);
            if (physicalDrives[4] == 0) // regular report. All other values are extended reports that we don't need
            {
                if (device->os_info.cissDeviceData->driveNumber <= (lunListLength / PHYSICAL_LUN_DESCRIPTOR_LENGTH))
                {
                    // should be able to find the device in the data
                    safe_memcpy(
                        physicalLocationData, physicalLocationDataLength,
                        &physicalDrives[uint32_to_sizet(device->os_info.cissDeviceData->driveNumber + UINT32_C(1)) *
                                        PHYSICAL_LUN_DESCRIPTOR_LENGTH],
                        LUN_ADDR_LEN); //+1 to get past the header
                    ret = SUCCESS;
                }
                else
                {
                    // invalid physical drive number
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
        }
        safe_free_aligned(&physicalDrives);
    }
    else
    {
        ret = MEMORY_FAILURE;
    }
    return ret;
}

typedef enum eCISSptCmdTypeEnum
{
    CISS_CMD_CONTROLLER,
    CISS_CMD_LOGICAL_LUN, // not currently used or supported in this code, but may be added in the future if needed.
                          // -TJE
    CISS_CMD_PHYSICAL_LUN
} eCISSptCmdType;

// Standard passthrough
// physicalDriveCmd - true for most things. Only set to false when trying to issue a command directly to the controller.
//                    Only using this flag as part of device discovery. - TJE
static eReturnValues ciss_Passthrough(ScsiIoCtx* scsiIoCtx, eCISSptCmdType cmdType)
{
    eReturnValues ret      = BAD_PARAMETER;
    int           ioctlRet = 0;
    if (scsiIoCtx != M_NULLPTR)
    {
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            printf("Sending CISS Passthrough command\n");
        }
        if (scsiIoCtx->cdbLength <= 16 && scsiIoCtx->dataLength <= UINT16_MAX)
        {
#    if defined(__linux__) || defined(__FreeBSD__) /*interface is the same on Linux and FreeBSD*/
            if (scsiIoCtx->device->os_info.cissDeviceData->smartpqi)
            {
#        if defined(__FreeBSD__)
                // if the smartpqi bool is set, use the structures from that driver to issue the command to ensure
                // that the packing matches exactly to minimize compatibility problems.
                pqi_IOCTL_Command_struct pqiCmd;
                DECLARE_SEATIMER(commandTimer);
                safe_memset(&pqiCmd, sizeof(pqi_IOCTL_Command_struct), 0, sizeof(pqi_IOCTL_Command_struct));

                switch (cmdType)
                {
                case CISS_CMD_CONTROLLER:
                    break;
                default: // this will work OK for other cases. May need modifications for logical volume commands. -TJE
                    // set path to device
                    safe_memcpy(&pqiCmd.LUN_info, sizeof(pqi_LUNAddr_struct),
                                scsiIoCtx->device->os_info.cissDeviceData->physicalLocation,
                                LUN_ADDR_LEN); // this is 8 bytes in size maximum
                    break;
                }
                // now setup to send a CDB
                pqiCmd.Request.CDBLen = scsiIoCtx->cdbLength;
                pqiCmd.Request.Type.Type =
                    TYPE_CMD; // TYPE_MSG also available for BMIC commands, which can be things like resets
                pqiCmd.Request.Type.Attribute = ATTR_SIMPLE; // Can be UNTAGGED, SIMPLE, HEADOFQUEUE, ORDERED, ACA
                switch (scsiIoCtx->direction)
                {
                case XFER_DATA_IN:
                    pqiCmd.Request.Type.Direction = XFER_READ;
                    pqiCmd.buf_size               = C_CAST(uint16_t, scsiIoCtx->dataLength);
                    pqiCmd.buf                    = scsiIoCtx->pdata;
                    break;
                case XFER_DATA_OUT:
                    pqiCmd.Request.Type.Direction = XFER_WRITE;
                    pqiCmd.buf_size               = C_CAST(uint16_t, scsiIoCtx->dataLength);
                    pqiCmd.buf                    = scsiIoCtx->pdata;
                    break;
                case XFER_NO_DATA:
                    pqiCmd.Request.Type.Direction = XFER_NONE;
                    pqiCmd.buf_size               = 0;
                    pqiCmd.buf                    = M_NULLPTR;
                    break;
                default:
                    return OS_COMMAND_NOT_AVAILABLE;
                }

                if (scsiIoCtx->timeout)
                {
                    if (scsiIoCtx->timeout > UINT16_MAX)
                    {
                        pqiCmd.Request.Timeout = UINT16_MAX;
                    }
                    else
                    {
                        pqiCmd.Request.Timeout = C_CAST(uint16_t, scsiIoCtx->timeout);
                    }
                }
                else
                {
                    pqiCmd.Request.Timeout = 15;
                }
                safe_memcpy(pqiCmd.Request.CDB, 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

                ret = OS_PASSTHROUGH_FAILURE; // OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

                start_Timer(&commandTimer);
                DISABLE_WARNING_SIGN_CONVERSION
                ioctlRet = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CCISS_PASSTHRU, &pqiCmd);
                RESTORE_WARNING_SIGN_CONVERSION
                stop_Timer(&commandTimer);
                if (ioctlRet < 0)
                {
                    ret                                   = OS_PASSTHROUGH_FAILURE;
                    scsiIoCtx->device->os_info.last_error = errno;
                    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                    {
                        print_Errno_To_Screen(errno);
                    }
                }

                // Copy and sense data we received, then need to check for errors
                if (scsiIoCtx->psense)
                {
                    safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
                    if (pqiCmd.error_info.SenseLen)
                    {
                        safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, pqiCmd.error_info.SenseInfo,
                                    M_Min(pqiCmd.error_info.SenseLen, scsiIoCtx->senseDataSize));
                    }
                }
                // set command time:
                scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

                if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                {
                    switch (pqiCmd.error_info.CommandStatus)
                    {
                    case CMD_SUCCESS:
                        printf("CISS Success\n");
                        break;
                    case CMD_TARGET_STATUS:
                        printf("CISS Target Status: ");
                        switch (pqiCmd.error_info.ScsiStatus)
                        {
                        case SAM_STATUS_GOOD:
                            printf("Good\n");
                            break;
                        case SAM_STATUS_CHECK_CONDITION:
                            printf("Check Condition\n");
                            break;
                        case SAM_STATUS_CONDITION_MET:
                            printf("Condition Met\n");
                            break;
                        case SAM_STATUS_INTERMEDIATE:
                            printf("Intermediate\n");
                            break;
                        case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                            printf("Intermediate Condition Met\n");
                            break;
                        case SAM_STATUS_COMMAND_TERMINATED:
                            printf("Command Terminated\n");
                            break;
                        case SAM_STATUS_BUSY:
                            printf("Busy\n");
                            break;
                        case SAM_STATUS_RESERVATION_CONFLICT:
                            printf("Reservation Conflict\n");
                            break;
                        case SAM_STATUS_TASK_SET_FULL:
                            printf("Task Set Full\n");
                            break;
                        case SAM_STATUS_ACA_ACTIVE:
                            printf("ACA Active\n");
                            break;
                        case SAM_STATUS_TASK_ABORTED:
                            printf("Task Aborted\n");
                            break;
                        default:
                            printf("Unknown: %02X\n", pqiCmd.error_info.ScsiStatus);
                            break;
                        }
                        break;
                    case CMD_DATA_UNDERRUN:
                        printf("CISS Data Underrun\n");
                        break;
                    case CMD_DATA_OVERRUN:
                        printf("CISS Data Overrun\n");
                        break;
                    case CMD_INVALID:
                        printf("CISS Invalid\n");
                        // print out additional invalid command info
                        printf("\toffense_size  = %" PRIu8 "\n",
                               pqiCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_size);
                        printf("\toffense_num   = %" PRIu8 "\n", pqiCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_num);
                        printf("\toffense_value = %" PRIu32 "\n",
                               pqiCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_value);
                        break;
                    case CMD_TIMEOUT:
                        printf("CISS Timeout\n");
                        break;
                    case CMD_PROTOCOL_ERR:
                        printf("CISS Protocol Error\n");
                        break;
                    case CMD_HARDWARE_ERR:
                        printf("CISS Hardware Error\n");
                        break;
                    case CMD_CONNECTION_LOST:
                        printf("CISS Connection Lost\n");
                        break;
                    case CMD_ABORTED:
                        printf("CISS Command Aborted\n");
                        break;
                    case CMD_ABORT_FAILED:
                        printf("CISS Abort Failed\n");
                        break;
                    case CMD_UNSOLICITED_ABORT:
                        printf("CISS Unsolicited Abort\n");
                        break;
                    case CMD_UNABORTABLE:
                        printf("CISS Unabortable\n");
                        break;
                    default:
                        printf("CISS unknown error: %u\n", pqiCmd.error_info.CommandStatus);
                        break;
                    }
                }

                // check for errors to set ret properly
                switch (pqiCmd.error_info.CommandStatus)
                {
                case CMD_SUCCESS:
                case CMD_DATA_UNDERRUN:
                case CMD_DATA_OVERRUN:
                    ret = SUCCESS;
                    break;
                case CMD_TARGET_STATUS:
                    switch (pqiCmd.error_info.ScsiStatus)
                    {
                    case SAM_STATUS_GOOD:
                    case SAM_STATUS_CHECK_CONDITION:
                    case SAM_STATUS_CONDITION_MET:
                    case SAM_STATUS_INTERMEDIATE:
                    case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                    case SAM_STATUS_COMMAND_TERMINATED:
                        ret = SUCCESS; // let upper layer parse sense data
                        break;
                    case SAM_STATUS_BUSY:
                    case SAM_STATUS_RESERVATION_CONFLICT:
                    case SAM_STATUS_TASK_SET_FULL:
                    case SAM_STATUS_ACA_ACTIVE:
                    case SAM_STATUS_TASK_ABORTED:
                    default:
                        ret = OS_PASSTHROUGH_FAILURE;
                        break;
                    }
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
#        else
                ret = BAD_PARAMETER;
#        endif
            }
            else
            {
                IOCTL_Command_struct cissCmd;
                DECLARE_SEATIMER(commandTimer);
                safe_memset(&cissCmd, sizeof(IOCTL_Command_struct), 0, sizeof(IOCTL_Command_struct));

                switch (cmdType)
                {
                case CISS_CMD_CONTROLLER:
                    break;
                default: // this will work OK for other cases. May need modifications for logical volume commands. -TJE
                    // set path to device
                    safe_memcpy(&cissCmd.LUN_info, sizeof(LUNAddr_struct),
                                scsiIoCtx->device->os_info.cissDeviceData->physicalLocation,
                                LUN_ADDR_LEN); // this is 8 bytes in size maximum
                    break;
                }
                // now setup to send a CDB
                cissCmd.Request.CDBLen = scsiIoCtx->cdbLength;
                cissCmd.Request.Type.Type =
                    TYPE_CMD; // TYPE_MSG also available for BMIC commands, which can be things like resets
                cissCmd.Request.Type.Attribute = ATTR_SIMPLE; // Can be UNTAGGED, SIMPLE, HEADOFQUEUE, ORDERED, ACA
                switch (scsiIoCtx->direction)
                {
                case XFER_DATA_IN:
                    cissCmd.Request.Type.Direction = XFER_READ;
                    cissCmd.buf_size               = C_CAST(uint16_t, scsiIoCtx->dataLength);
                    cissCmd.buf                    = scsiIoCtx->pdata;
                    break;
                case XFER_DATA_OUT:
                    cissCmd.Request.Type.Direction = XFER_WRITE;
                    cissCmd.buf_size               = C_CAST(uint16_t, scsiIoCtx->dataLength);
                    cissCmd.buf                    = scsiIoCtx->pdata;
                    break;
                case XFER_NO_DATA:
                    cissCmd.Request.Type.Direction = XFER_NONE;
                    cissCmd.buf_size               = 0;
                    cissCmd.buf                    = M_NULLPTR;
                    break;
                default:
                    return OS_COMMAND_NOT_AVAILABLE;
                }

                if (scsiIoCtx->timeout)
                {
                    if (scsiIoCtx->timeout > UINT16_MAX)
                    {
                        cissCmd.Request.Timeout = UINT16_MAX;
                    }
                    else
                    {
                        cissCmd.Request.Timeout = C_CAST(uint16_t, scsiIoCtx->timeout);
                    }
                }
                else
                {
                    cissCmd.Request.Timeout = 15;
                }
                safe_memcpy(cissCmd.Request.CDB, 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

                ret = OS_PASSTHROUGH_FAILURE; // OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

                start_Timer(&commandTimer);
                DISABLE_WARNING_SIGN_CONVERSION
                ioctlRet = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CCISS_PASSTHRU, &cissCmd);
                RESTORE_WARNING_SIGN_CONVERSION
                stop_Timer(&commandTimer);
                if (ioctlRet < 0)
                {
                    ret                                   = OS_PASSTHROUGH_FAILURE;
                    scsiIoCtx->device->os_info.last_error = errno;
                    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                    {
                        print_Errno_To_Screen(errno);
                    }
                }

                // Copy and sense data we received, then need to check for errors
                if (scsiIoCtx->psense)
                {
                    safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
                    if (cissCmd.error_info.SenseLen)
                    {
                        safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, cissCmd.error_info.SenseInfo,
                                    M_Min(cissCmd.error_info.SenseLen, scsiIoCtx->senseDataSize));
                    }
                }
                // set command time:
                scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

                if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                {
                    switch (cissCmd.error_info.CommandStatus)
                    {
                    case CMD_SUCCESS:
                        printf("CISS Success\n");
                        break;
                    case CMD_TARGET_STATUS:
                        printf("CISS Target Status: ");
                        switch (cissCmd.error_info.ScsiStatus)
                        {
                        case SAM_STATUS_GOOD:
                            printf("Good\n");
                            break;
                        case SAM_STATUS_CHECK_CONDITION:
                            printf("Check Condition\n");
                            break;
                        case SAM_STATUS_CONDITION_MET:
                            printf("Condition Met\n");
                            break;
                        case SAM_STATUS_INTERMEDIATE:
                            printf("Intermediate\n");
                            break;
                        case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                            printf("Intermediate Condition Met\n");
                            break;
                        case SAM_STATUS_COMMAND_TERMINATED:
                            printf("Command Terminated\n");
                            break;
                        case SAM_STATUS_BUSY:
                            printf("Busy\n");
                            break;
                        case SAM_STATUS_RESERVATION_CONFLICT:
                            printf("Reservation Conflict\n");
                            break;
                        case SAM_STATUS_TASK_SET_FULL:
                            printf("Task Set Full\n");
                            break;
                        case SAM_STATUS_ACA_ACTIVE:
                            printf("ACA Active\n");
                            break;
                        case SAM_STATUS_TASK_ABORTED:
                            printf("Task Aborted\n");
                            break;
                        default:
                            printf("Unknown: %02X\n", cissCmd.error_info.ScsiStatus);
                            break;
                        }
                        break;
                    case CMD_DATA_UNDERRUN:
                        printf("CISS Data Underrun\n");
                        break;
                    case CMD_DATA_OVERRUN:
                        printf("CISS Data Overrun\n");
                        break;
                    case CMD_INVALID:
                        printf("CISS Invalid\n");
                        // print out additional invalid command info
                        printf("\toffense_size  = %" PRIu8 "\n",
                               cissCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_size);
                        printf("\toffense_num   = %" PRIu8 "\n",
                               cissCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_num);
                        printf("\toffense_value = %" PRIu32 "\n",
                               cissCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_value);
                        break;
                    case CMD_TIMEOUT:
                        printf("CISS Timeout\n");
                        break;
                    case CMD_PROTOCOL_ERR:
                        printf("CISS Protocol Error\n");
                        break;
                    case CMD_HARDWARE_ERR:
                        printf("CISS Hardware Error\n");
                        break;
                    case CMD_CONNECTION_LOST:
                        printf("CISS Connection Lost\n");
                        break;
                    case CMD_ABORTED:
                        printf("CISS Command Aborted\n");
                        break;
                    case CMD_ABORT_FAILED:
                        printf("CISS Abort Failed\n");
                        break;
                    case CMD_UNSOLICITED_ABORT:
                        printf("CISS Unsolicited Abort\n");
                        break;
                    case CMD_UNABORTABLE:
                        printf("CISS Unabortable\n");
                        break;
                    default:
                        printf("CISS unknown error: %u\n", cissCmd.error_info.CommandStatus);
                        break;
                    }
                }

                // check for errors to set ret properly
                switch (cissCmd.error_info.CommandStatus)
                {
                case CMD_SUCCESS:
                case CMD_DATA_UNDERRUN:
                case CMD_DATA_OVERRUN:
                    ret = SUCCESS;
                    break;
                case CMD_TARGET_STATUS:
                    switch (cissCmd.error_info.ScsiStatus)
                    {
                    case SAM_STATUS_GOOD:
                    case SAM_STATUS_CHECK_CONDITION:
                    case SAM_STATUS_CONDITION_MET:
                    case SAM_STATUS_INTERMEDIATE:
                    case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                    case SAM_STATUS_COMMAND_TERMINATED:
                        ret = SUCCESS; // let upper layer parse sense data
                        break;
                    case SAM_STATUS_BUSY:
                    case SAM_STATUS_RESERVATION_CONFLICT:
                    case SAM_STATUS_TASK_SET_FULL:
                    case SAM_STATUS_ACA_ACTIVE:
                    case SAM_STATUS_TASK_ABORTED:
                    default:
                        ret = OS_PASSTHROUGH_FAILURE;
                        break;
                    }
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
#    elif defined(__sun)
            cpqary3_scsi_pass_t cissCmd;
            DECLARE_SEATIMER(commandTimer);
            safe_memset(&cissCmd, sizeof(cpqary3_scsi_pass_t), 0, sizeof(cpqary3_scsi_pass_t));

            switch (cmdType)
            {
            case CISS_CMD_CONTROLLER:
                break;
            default: // this will work OK for other cases. May need modifications for logical volume commands. -TJE
                // set path to device
                safe_memcpy(&cissCmd.lun_addr, 8, scsiIoCtx->device->os_info.cissDeviceData->physicalLocation,
                            LUN_ADDR_LEN); // this is 8 bytes in size maximum
                break;
            }

            // now setup to send a CDB
            cissCmd.cdb_len = scsiIoCtx->cdbLength;
            // NOTE: The .buf is a uint64. Not sure if this should be a pointer value, or overallocated for buffer space
            //       currently code assumes that it should be the pointer value.
            switch (scsiIoCtx->direction)
            {
            case XFER_DATA_IN:
                cissCmd.io_direction = CPQARY3_SCSI_IN;
                cissCmd.buf_len      = C_CAST(uint16_t, scsiIoCtx->dataLength);
                cissCmd.buf          = C_CAST(uintptr_t, scsiIoCtx->pdata);
                break;
            case XFER_DATA_OUT:
                cissCmd.io_direction = CPQARY3_SCSI_OUT;
                cissCmd.buf_len      = C_CAST(uint16_t, scsiIoCtx->dataLength);
                cissCmd.buf          = C_CAST(uintptr_t, scsiIoCtx->pdata);
                break;
            case XFER_NO_DATA:
                cissCmd.Request.Type.Direction = CPQARY3_NODATA_XFER;
                cissCmd.buf_len                = 0;
                cissCmd.buf                    = C_CAST(uintptr_t, M_NULLPTR);
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
            safe_memcpy(cissCmd.cdb, 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

            ret = OS_PASSTHROUGH_FAILURE; // OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

            start_Timer(&commandTimer);
            DISABLE_WARNING_SIGN_CONVERSION
            ioctlRet = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CPQARY3_IOCTL_SCSI_PASS, &cissCmd);
            RESTORE_WARNING_SIGN_CONVERSION
            stop_Timer(&commandTimer);
            if (ioctlRet < 0)
            {
                ret                                   = OS_PASSTHROUGH_FAILURE;
                scsiIoCtx->device->os_info.last_error = errno;
                if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
                {
                    print_Errno_To_Screen(errno);
                }
            }

            // Copy and sense data we received, then need to check for errors
            if (scsiIoCtx->psense)
            {
                safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
                if (cissCmd.err_info.SenseLen)
                {
                    safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, cissCmd.err_info.SenseInfo,
                                M_Min(cissCmd.err_info.SenseLen, scsiIoCtx->senseDataSize));
                }
            }
            // set command time:
            scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                switch (cissCmd.err_info.CommandStatus)
                {
                case CMD_SUCCESS:
                    printf("CISS Success\n");
                    break;
                case CMD_TARGET_STATUS:
                    printf("CISS Target Status: ");
                    switch (cissCmd.err_info.ScsiStatus)
                    {
                    case SAM_STATUS_GOOD:
                        printf("Good\n");
                        break;
                    case SAM_STATUS_CHECK_CONDITION:
                        printf("Check Condition\n");
                        break;
                    case SAM_STATUS_CONDITION_MET:
                        printf("Condition Met\n");
                        break;
                    case SAM_STATUS_INTERMEDIATE:
                        printf("Intermediate\n");
                        break;
                    case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                        printf("Intermediate Condition Met\n");
                        break;
                    case SAM_STATUS_COMMAND_TERMINATED:
                        printf("Command Terminated\n");
                        break;
                    case SAM_STATUS_BUSY:
                        printf("Busy\n");
                        break;
                    case SAM_STATUS_RESERVATION_CONFLICT:
                        printf("Reservation Conflict\n");
                        break;
                    case SAM_STATUS_TASK_SET_FULL:
                        printf("Task Set Full\n");
                        break;
                    case SAM_STATUS_ACA_ACTIVE:
                        printf("ACA Active\n");
                        break;
                    case SAM_STATUS_TASK_ABORTED:
                        printf("Task Aborted\n");
                        break;
                    default:
                        printf("Unknown: %02X\n", cissCmd.err_info.ScsiStatus);
                        break;
                    }
                    break;
                case CMD_DATA_UNDERRUN:
                    printf("CISS Data Underrun\n");
                    break;
                case CMD_DATA_OVERRUN:
                    printf("CISS Data Overrun\n");
                    break;
                case CMD_INVALID:
                    printf("CISS Invalid\n");
                    // print out additional invalid command info
                    printf("\toffense_size  = %" PRIu8 "\n", cissCmd.err_info.MoreErrInfo.Invalid_Cmd.offense_size);
                    printf("\toffense_num   = %" PRIu8 "\n", cissCmd.err_info.MoreErrInfo.Invalid_Cmd.offense_num);
                    printf("\toffense_value = %" PRIu32 "\n", cissCmd.err_info.MoreErrInfo.Invalid_Cmd.offense_value);
                    break;
                case CMD_TIMEOUT:
                    printf("CISS Timeout\n");
                    break;
                case CMD_PROTOCOL_ERR:
                    printf("CISS Protocol Error\n");
                    break;
                case CMD_HARDWARE_ERR:
                    printf("CISS Hardware Error\n");
                    break;
                case CMD_CONNECTION_LOST:
                    printf("CISS Connection Lost\n");
                    break;
                case CMD_ABORTED:
                    printf("CISS Command Aborted\n");
                    break;
                case CMD_ABORT_FAILED:
                    printf("CISS Abort Failed\n");
                    break;
                case CMD_UNSOLICITED_ABORT:
                    printf("CISS Unsolicited Abort\n");
                    break;
                case CMD_UNABORTABLE:
                    printf("CISS Unabortable\n");
                    break;
                default:
                    printf("CISS unknown error: %u\n", cissCmd.err_info.CommandStatus);
                    break;
                }
            }

            // check for errors to set ret properly
            switch (cissCmd.err_info.CommandStatus)
            {
            case CMD_SUCCESS:
            case CMD_DATA_UNDERRUN:
            case CMD_DATA_OVERRUN:
                ret = SUCCESS;
                break;
            case CMD_TARGET_STATUS:
                switch (cissCmd.err_info.ScsiStatus)
                {
                case SAM_STATUS_GOOD:
                case SAM_STATUS_CHECK_CONDITION:
                case SAM_STATUS_CONDITION_MET:
                case SAM_STATUS_INTERMEDIATE:
                case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                case SAM_STATUS_COMMAND_TERMINATED:
                    ret = SUCCESS; // let upper layer parse sense data
                    break;
                case SAM_STATUS_BUSY:
                case SAM_STATUS_RESERVATION_CONFLICT:
                case SAM_STATUS_TASK_SET_FULL:
                case SAM_STATUS_ACA_ACTIVE:
                case SAM_STATUS_TASK_ABORTED:
                default:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }
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

#    else
            ret = OS_COMMAND_NOT_AVAILABLE;
#    endif // checking __linux__, __FreeBSD__, __sun
        }
        else
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
    }
    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
    {
        print_Return_Enum("Ciss Passthrough", ret);
    }
    return ret;
}

// Support big passthrough for Linux. Currently not found in other header files.
#    if defined(CCISS_BIG_PASSTHRU)
static eReturnValues ciss_Big_Passthrough(ScsiIoCtx* scsiIoCtx, eCISSptCmdType cmdType)
{
    eReturnValues ret = BAD_PARAMETER;
    if (scsiIoCtx != M_NULLPTR)
    {
        if (scsiIoCtx->cdbLength <= 16 && scsiIoCtx->dataLength <= MAX_KMALLOC_SIZE)
        {
            BIG_IOCTL_Command_struct cissCmd;
            DECLARE_SEATIMER(commandTimer);
            safe_memset(&cissCmd, sizeof(BIG_IOCTL_Command_struct), 0, sizeof(BIG_IOCTL_Command_struct));

            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                printf("Sending CISS Big Passthrough\n");
            }

            switch (cmdType)
            {
            case CISS_CMD_CONTROLLER:
                break;
            default: // this will work OK for other cases. May need modifications for logical volume commands. -TJE
                // set path to device
                safe_memcpy(&cissCmd.LUN_info, sizeof(LUNAddr_struct),
                            scsiIoCtx->device->os_info.cissDeviceData->physicalLocation,
                            LUN_ADDR_LEN); // this is 8 bytes in size maximum
                break;
            }
            // now setup to send a CDB
            cissCmd.Request.CDBLen = scsiIoCtx->cdbLength;
            cissCmd.Request.Type.Type =
                TYPE_CMD; // TYPE_MSG also available for BMIC commands, which can be things like resets
            cissCmd.Request.Type.Attribute = ATTR_SIMPLE; // Can be UNTAGGED, SIMPLE, HEADOFQUEUE, ORDERED, ACA
            switch (scsiIoCtx->direction)
            {
            case XFER_DATA_IN:
                cissCmd.Request.Type.Direction = XFER_READ;
                cissCmd.malloc_size            = scsiIoCtx->dataLength;
                cissCmd.buf_size               = scsiIoCtx->dataLength;
                cissCmd.buf                    = scsiIoCtx->pdata;
                break;
            case XFER_DATA_OUT:
                cissCmd.Request.Type.Direction = XFER_WRITE;
                cissCmd.malloc_size            = scsiIoCtx->dataLength;
                cissCmd.buf_size               = scsiIoCtx->dataLength;
                cissCmd.buf                    = scsiIoCtx->pdata;
                break;
            case XFER_NO_DATA:
                cissCmd.Request.Type.Direction = XFER_NONE;
                cissCmd.buf_size               = 0;
                cissCmd.buf                    = M_NULLPTR;
                break;
            default:
                return OS_COMMAND_NOT_AVAILABLE;
            }

            if (scsiIoCtx->timeout)
            {
                if (scsiIoCtx->timeout > UINT16_MAX)
                {
                    cissCmd.Request.Timeout = UINT16_MAX;
                }
                else
                {
                    cissCmd.Request.Timeout = C_CAST(uint16_t, scsiIoCtx->timeout);
                }
            }
            else
            {
                cissCmd.Request.Timeout = 15;
            }
            safe_memcpy(cissCmd.Request.CDB, 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

            ret = OS_PASSTHROUGH_FAILURE; // OS_COMMAND_NOT_AVAILABLE, OS_COMMAND_BLOCKED

            start_Timer(&commandTimer);
            DISABLE_WARNING_SIGN_CONVERSION
            ret = ioctl(scsiIoCtx->device->os_info.cissDeviceData->cissHandle, CCISS_BIG_PASSTHRU, &cissCmd);
            RESTORE_WARNING_SIGN_CONVERSION
            stop_Timer(&commandTimer);

            // Copy and sense data we received, then need to check for errors
            if (scsiIoCtx->psense)
            {
                safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
                if (cissCmd.error_info.SenseLen)
                {
                    safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, cissCmd.error_info.SenseInfo,
                                M_Min(cissCmd.error_info.SenseLen, scsiIoCtx->senseDataSize));
                }
            }
            // set command time:
            scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);

            if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
            {
                switch (cissCmd.error_info.CommandStatus)
                {
                case CMD_SUCCESS:
                    printf("CISS Success\n");
                    break;
                case CMD_TARGET_STATUS:
                    printf("CISS Target Status: ");
                    switch (cissCmd.error_info.ScsiStatus)
                    {
                    case SAM_STATUS_GOOD:
                        printf("Good\n");
                        break;
                    case SAM_STATUS_CHECK_CONDITION:
                        printf("Check Condition\n");
                        break;
                    case SAM_STATUS_CONDITION_MET:
                        printf("Condition Met\n");
                        break;
                    case SAM_STATUS_INTERMEDIATE:
                        printf("Intermediate\n");
                        break;
                    case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                        printf("Intermediate Condition Met\n");
                        break;
                    case SAM_STATUS_COMMAND_TERMINATED:
                        printf("Command Terminated\n");
                        break;
                    case SAM_STATUS_BUSY:
                        printf("Busy\n");
                        break;
                    case SAM_STATUS_RESERVATION_CONFLICT:
                        printf("Reservation Conflict\n");
                        break;
                    case SAM_STATUS_TASK_SET_FULL:
                        printf("Task Set Full\n");
                        break;
                    case SAM_STATUS_ACA_ACTIVE:
                        printf("ACA Active\n");
                        break;
                    case SAM_STATUS_TASK_ABORTED:
                        printf("Task Aborted\n");
                        break;
                    default:
                        printf("Unknown: %02X\n", cissCmd.error_info.ScsiStatus);
                        break;
                    }
                    break;
                case CMD_DATA_UNDERRUN:
                    printf("CISS Data Underrun\n");
                    break;
                case CMD_DATA_OVERRUN:
                    printf("CISS Data Overrun\n");
                    break;
                case CMD_INVALID:
                    printf("CISS Invalid\n");
                    // print out additional invalid command info
                    printf("\toffense_size  = %" PRIu8 "\n", cissCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_size);
                    printf("\toffense_num   = %" PRIu8 "\n", cissCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_num);
                    printf("\toffense_value = %" PRIu32 "\n", cissCmd.error_info.MoreErrInfo.Invalid_Cmd.offense_value);
                    break;
                case CMD_TIMEOUT:
                    printf("CISS Timeout\n");
                    break;
                case CMD_PROTOCOL_ERR:
                    printf("CISS Protocol Error\n");
                    break;
                case CMD_HARDWARE_ERR:
                    printf("CISS Hardware Error\n");
                    break;
                case CMD_CONNECTION_LOST:
                    printf("CISS Connection Lost\n");
                    break;
                case CMD_ABORTED:
                    printf("CISS Command Aborted\n");
                    break;
                case CMD_ABORT_FAILED:
                    printf("CISS Abort Failed\n");
                    break;
                case CMD_UNSOLICITED_ABORT:
                    printf("CISS Unsolicited Abort\n");
                    break;
                case CMD_UNABORTABLE:
                    printf("CISS Unabortable\n");
                    break;
                default:
                    printf("CISS unknown error: %u\n", cissCmd.error_info.CommandStatus);
                    break;
                }
            }

            // check for errors to set ret properly
            switch (cissCmd.error_info.CommandStatus)
            {
            case CMD_SUCCESS:
            case CMD_DATA_UNDERRUN:
            case CMD_DATA_OVERRUN:
                ret = SUCCESS;
                break;
            case CMD_TARGET_STATUS:
                switch (cissCmd.error_info.ScsiStatus)
                {
                case SAM_STATUS_GOOD:
                case SAM_STATUS_CHECK_CONDITION:
                case SAM_STATUS_CONDITION_MET:
                case SAM_STATUS_INTERMEDIATE:
                case SAM_STATUS_INTERMEDIATE_CONDITION_MET:
                case SAM_STATUS_COMMAND_TERMINATED:
                    ret = SUCCESS; // let upper layer parse sense data
                    break;
                case SAM_STATUS_BUSY:
                case SAM_STATUS_RESERVATION_CONFLICT:
                case SAM_STATUS_TASK_SET_FULL:
                case SAM_STATUS_ACA_ACTIVE:
                case SAM_STATUS_TASK_ABORTED:
                default:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }
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
        else
        {
            ret = OS_COMMAND_NOT_AVAILABLE;
        }
    }
    if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
    {
        print_Return_Enum("Ciss Big Passthrough", ret);
    }
    return ret;
}
#    endif // CCISS_BIG_PASSTHRU

eReturnValues issue_io_ciss_Dev(ScsiIoCtx* scsiIoCtx)
{
    if (scsiIoCtx->device->os_info.cissDeviceData)
    {
#    if defined(CCISS_BIG_PASSTHRU)
        // use big passthrough only when making a large enough data transfer to matter - TJE
        if (scsiIoCtx->dataLength > UINT16_MAX && scsiIoCtx->device->os_info.cissDeviceData->bigPassthroughAvailable)
        {
            return ciss_Big_Passthrough(scsiIoCtx, CISS_CMD_PHYSICAL_LUN);
        }
        else
#    endif // #if defined (CCISS_BIG_PASSTHRU)
        {
            return ciss_Passthrough(scsiIoCtx, CISS_CMD_PHYSICAL_LUN);
        }
    }
    else
    {
        return BAD_PARAMETER;
    }
}

eReturnValues get_CISS_RAID_Device(const char* filename, tDevice* device)
{
    eReturnValues ret         = FAILURE;
    uint16_t      driveNumber = UINT16_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, osHandle, OS_CISS_HANDLE_MAX_LENGTH);
    char* handlePtr = &osHandle[0]; // this is done to prevent warnings
    // Need to open this handle and setup some information then fill in the device information.
    if (!(validate_Device_Struct(device->sanity)))
    {
        return LIBRARY_MISMATCH;
    }
    // set the name that was provided for other display.
    safe_memcpy(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, filename, safe_strlen(filename));
    if (PARSE_COUNT_SUCCESS == parse_CISS_Handle(filename, handlePtr, &driveNumber))
    {
        device->os_info.cissDeviceData = safe_calloc(1, sizeof(cissDeviceInfo));
        if (device->os_info.cissDeviceData)
        {
            if ((device->os_info.cissDeviceData->cissHandle = open(handlePtr, O_RDWR | O_NONBLOCK)) >= 0)
            {
                // check that CISS IOCTLs are available. If not, then we don't want to proceed.
                // There is danger in attempting a vendor unique op code as we don't know how the target will respond to
                // it unless we know it really is a CISS device
                if (supports_CISS_IOCTLs(device->os_info.cissDeviceData->cissHandle))
                {
                    // setup all the things we need to be able to issue commands in the code
                    device->os_info.cissDeviceData->driveNumber = driveNumber;
                    device->issue_io                            = C_CAST(issue_io_func, issue_io_ciss_Dev);
                    device->drive_info.drive_type               = SCSI_DRIVE;
                    device->drive_info.interface_type           = RAID_INTERFACE;
                    snprintf_err_handle(&device->os_info.name[0], OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
                    snprintf_err_handle(&device->os_info.friendlyName[0], OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s",
                                        filename);
                    device->os_info.minimumAlignment = sizeof(void*);
                    device->os_info.cissDeviceData->smartpqi =
                        is_SmartPQI_Unique_IOCTLs_Supported(device->os_info.cissDeviceData->cissHandle);
                    device->os_info.fd =
                        device->os_info.cissDeviceData
                            ->cissHandle; // set this just to make the upper layers that validate this happy-TJE
                    device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA =
                        true; // this may not be true for all CISS controllers, but will likely work with any of them
                              // -TJE
                    // handle opened, now get the physical device location from the C2h command
                    // This is done here to reuse lots of other code to issue commands.
                    if (SUCCESS == (ret = get_Physical_Device_Location_Data(
                                        device, device->os_info.cissDeviceData->physicalLocation, 8)))
                    {
                        // finally call fill_Drive_Info to let opensea-transport set it's support bits and other device
                        // information
                        ret = fill_Drive_Info_Data(device);
                    }
                    else
                    {
                        // something went wrong, so clean up.
                        close(device->os_info.cissDeviceData->cissHandle);
                        device->os_info.fd = 0;
                        safe_free_ciss_dev_info(&device->os_info.cissDeviceData);
                    }
                }
                else
                {
                    ret = NOT_SUPPORTED;
                    close(device->os_info.cissDeviceData->cissHandle);
                    safe_free_ciss_dev_info(&device->os_info.cissDeviceData);
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

eReturnValues close_CISS_RAID_Device(tDevice* device)
{
    DISABLE_NONNULL_COMPARE
    if (device != M_NULLPTR && device->os_info.cissDeviceData)
    {
        if (close(device->os_info.cissDeviceData->cissHandle))
        {
            device->os_info.last_error = errno;
        }
        else
        {
            device->os_info.cissDeviceData->cissHandle = -1;
            device->os_info.last_error                 = 0;
        }
        device->os_info.fd = -1;
        safe_free_ciss_dev_info(&device->os_info.cissDeviceData);
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
    RESTORE_NONNULL_COMPARE
}

static eReturnValues get_CISS_Physical_LUN_Count(int fd, uint32_t* count)
{
    // This is a special function only used by get device count and get device list.
    // It creates a psuedo tDevice to issue a scsiIoCtx structure to passthrough and read this.
    // This keeps the rest of the code simpler for now
    eReturnValues ret = SUCCESS;
    if (count != M_NULLPTR)
    {
        tDevice   pseudoDev;
        ScsiIoCtx physicalLunCMD;
        DECLARE_ZERO_INIT_ARRAY(uint8_t, cdb, 12);
        uint32_t dataLength = (CISS_MAX_PHYSICAL_DRIVES * PHYSICAL_LUN_DESCRIPTOR_LENGTH) + 8; // 8 byte header
        uint8_t* data = M_REINTERPRET_CAST(uint8_t*, safe_calloc_aligned(dataLength, sizeof(uint8_t), sizeof(void*)));
        if (data != M_NULLPTR)
        {
            // setup the psuedo device
            safe_memset(&pseudoDev, sizeof(tDevice), 0, sizeof(tDevice));
            pseudoDev.os_info.fd     = fd;
            pseudoDev.sanity.version = DEVICE_BLOCK_VERSION;
            pseudoDev.sanity.size    = sizeof(tDevice);

            // setup the cdb
            cdb[OPERATION_CODE] = CISS_REPORT_PHYSICAL_LUNS_OP;
            cdb[1] = 0; // no extended data as it is not needed and this will maximize compatibility with controllers
                        // and firmwares.
            cdb[2]  = RESERVED;
            cdb[3]  = RESERVED;
            cdb[4]  = RESERVED;
            cdb[5]  = RESERVED;
            cdb[6]  = M_Byte3(dataLength);
            cdb[7]  = M_Byte2(dataLength);
            cdb[8]  = M_Byte1(dataLength);
            cdb[9]  = M_Byte0(dataLength);
            cdb[10] = RESERVED;
            cdb[11] = 0; // control byte

            // setup the scsiIoCtx
            safe_memset(&physicalLunCMD, sizeof(ScsiIoCtx), 0, sizeof(ScsiIoCtx));
            physicalLunCMD.device        = &pseudoDev;
            physicalLunCMD.direction     = XFER_DATA_IN;
            physicalLunCMD.pdata         = data;
            physicalLunCMD.psense        = pseudoDev.drive_info.lastCommandSenseData;
            physicalLunCMD.senseDataSize = SPC3_SENSE_LEN;
            physicalLunCMD.timeout       = 15;
            physicalLunCMD.dataLength    = dataLength;
            physicalLunCMD.cdbLength     = 12;
            safe_memcpy(physicalLunCMD.cdb, SCSI_IO_CTX_MAX_CDB_LEN, cdb, 12);

            // setup the cissDeviceData struct as it is needed to issue the CMD
            pseudoDev.os_info.cissDeviceData             = safe_calloc(1, sizeof(cissDeviceInfo));
            pseudoDev.os_info.cissDeviceData->cissHandle = fd;
            pseudoDev.os_info.cissDeviceData->smartpqi =
                is_SmartPQI_Unique_IOCTLs_Supported(pseudoDev.os_info.cissDeviceData->cissHandle);

            // issue the passthrough command (big passthrough not used here - TJE)
            ret = ciss_Passthrough(&physicalLunCMD, CISS_CMD_CONTROLLER);

            // done with this memory now, so clean it up
            safe_free_ciss_dev_info(&pseudoDev.os_info.cissDeviceData);

            // print_Data_Buffer(data, dataLength, false);

            if (ret == SUCCESS)
            {
                uint32_t lunListLength = M_BytesTo4ByteValue(data[0], data[1], data[2], data[3]);
                if (data[4] == 0) // regular report. All other values are extended reports that we don't need
                {
                    *count = lunListLength / PHYSICAL_LUN_DESCRIPTOR_LENGTH;
                }
                else
                {
                    perror("Unknown data return format when looking for CISS devices.\n");
                    *count = 0;
                }
            }

            safe_free_aligned(&data);
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
//!   \param[in] beginningOfList = list of handles to use to check the count. This can prevent duplicate devices if we
//!   know some handles should not be looked at.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
eReturnValues get_CISS_RAID_Device_Count(uint32_t*              numberOfDevices,
                                         M_ATTR_UNUSED uint64_t flags,
                                         ptrRaidHandleToScan*   beginningOfList)
{
    int                 fd                    = -1;
    ptrRaidHandleToScan raidList              = M_NULLPTR;
    ptrRaidHandleToScan previousRaidListEntry = M_NULLPTR;
    uint32_t            found                 = UINT32_C(0);
    DECLARE_ZERO_INIT_ARRAY(char, deviceName, CISS_HANDLE_MAX_LENGTH);

    if (beginningOfList == M_NULLPTR || *beginningOfList == M_NULLPTR)
    {
        // don't do anything. Only scan when we get a list to use.
        // Each OS that want's to do this should generate a list of handles to look for.
        return SUCCESS;
    }

    raidList = *beginningOfList;

    while (raidList)
    {
        bool handleRemoved = false;
        if (raidList->raidHint.cissRAID || raidList->raidHint.unknownRAID)
        {
            safe_memset(deviceName, CISS_HANDLE_MAX_LENGTH, 0, CISS_HANDLE_MAX_LENGTH);
            // check if the passed in handle contains /dev/ or not so we can open it correctly
            if (strstr(raidList->handle, "/dev/"))
            {
                snprintf_err_handle(deviceName, CISS_HANDLE_MAX_LENGTH, "%s", raidList->handle);
            }
            else
            {
                snprintf_err_handle(deviceName, CISS_HANDLE_MAX_LENGTH, "/dev/%s", raidList->handle);
            }
            if ((fd = open(deviceName, O_RDWR | O_NONBLOCK)) >= 0) // TODO: Verify O_NONBLOCK on FreeBSD and/or Solaris
            {
                if (supports_CISS_IOCTLs(fd))
                {
                    // responded with success to reading some basic info
                    // Need to call get_Device() and issue this SCSI CDB to read the physical location data, which will
                    // tell us how many IDs are available and we will then have a count of devices on this handle.
                    uint32_t countDevsOnThisHandle = UINT32_C(0);
                    // read the physical location data and count how many IDs are reported
                    if (SUCCESS == get_CISS_Physical_LUN_Count(fd, &countDevsOnThisHandle))
                    {
                        found += countDevsOnThisHandle;
                    }
                    // else something went wrong, and cannot enumerate additional devices.

                    // This was a CISS handle, remove it from the list!
                    // This will also increment us to the next handle
                    bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                    raidList                          = remove_RAID_Handle(raidList, previousRaidListEntry);
                    if (pointerAtBeginningOfRAIDList)
                    {
                        // if the first entry in the list was removed, we need up update the pointer before we exit so
                        // that the code that called here won't have an invalid pointer
                        *beginningOfList = raidList;
                    }
                    handleRemoved = true;
                }
                // close handle to the controller
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
            previousRaidListEntry =
                raidList; // store handle we just looked at in case we need to remove one from the list
            // increment to next element in the list
            raidList = raidList->next;
        }
    }
    DISABLE_NONNULL_COMPARE
    if (numberOfDevices != M_NULLPTR)
    {
        *numberOfDevices = found;
    }
    RESTORE_NONNULL_COMPARE
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
eReturnValues get_CISS_RAID_Device_List(tDevice* const       ptrToDeviceList,
                                        uint32_t             sizeInBytes,
                                        versionBlock         ver,
                                        uint64_t             flags,
                                        ptrRaidHandleToScan* beginningOfList)
{
    eReturnValues returnValue = SUCCESS;
    if (beginningOfList == M_NULLPTR || *beginningOfList == M_NULLPTR)
    {
        // don't do anything. Only scan when we get a list to use.
        // Each OS that want's to do this should generate a list of handles to look for.
        return SUCCESS;
    }

    DISABLE_NONNULL_COMPARE
    if (ptrToDeviceList == M_NULLPTR || sizeInBytes == UINT32_C(0))
    {
        return BAD_PARAMETER;
    }
    else if ((!(validate_Device_Struct(ver))))
    {
        return LIBRARY_MISMATCH;
    }
    else
    {
        tDevice*            d                     = ptrToDeviceList;
        ptrRaidHandleToScan raidList              = *beginningOfList;
        ptrRaidHandleToScan previousRaidListEntry = M_NULLPTR;
        int                 fd                    = -1;
        uint32_t            numberOfDevices       = sizeInBytes / sizeof(tDevice);
        uint32_t            found                 = UINT32_C(0);
        uint32_t            failedGetDeviceCount  = UINT32_C(0);
        DECLARE_ZERO_INIT_ARRAY(char, deviceName, CISS_HANDLE_MAX_LENGTH);
        while (raidList && found < numberOfDevices)
        {
            bool handleRemoved = false;
            if (raidList->raidHint.cissRAID || raidList->raidHint.unknownRAID)
            {
                safe_memset(deviceName, CISS_HANDLE_MAX_LENGTH, 0, CISS_HANDLE_MAX_LENGTH);
                if (strstr(raidList->handle, "/dev/"))
                {
                    snprintf_err_handle(deviceName, CISS_HANDLE_MAX_LENGTH, "%s", raidList->handle);
                }
                else
                {
                    snprintf_err_handle(deviceName, CISS_HANDLE_MAX_LENGTH, "/dev/%s", raidList->handle);
                }
                if ((fd = open(deviceName, O_RDWR | O_NONBLOCK)) >=
                    0) // TODO: Verify O_NONBLOCK on FreeBSD and/or Solaris
                {
                    if (supports_CISS_IOCTLs(fd))
                    {
                        // responded with success to reading some basic info
                        // Need to call get_Device() and issue this SCSI CDB to read the physical location data, which
                        // will tell us how many IDs are available and we will then have a count of devices on this
                        // handle.
                        uint32_t countDevsOnThisHandle = UINT32_C(0);
                        // read the physical location data and count how many IDs are reported
                        if (SUCCESS == get_CISS_Physical_LUN_Count(fd, &countDevsOnThisHandle))
                        {
                            // from here we need to do get_CISS_Device on each of these available physical LUNs
                            for (uint32_t currentDev = UINT32_C(0); currentDev < countDevsOnThisHandle; ++currentDev)
                            {
                                DECLARE_ZERO_INIT_ARRAY(char, handle, CISS_HANDLE_MAX_LENGTH);
                                // handle is formatted as "ciss:os_handle:driveNumber" NOTE: osHandle is everything
                                // after /dev/
                                snprintf_err_handle(handle, CISS_HANDLE_MAX_LENGTH, "ciss:%s:%" PRIu32,
                                                    basename(deviceName), currentDev);
                                // get the CISS device with a get_CISS_Device function
                                safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                                d->sanity.size    = ver.size;
                                d->sanity.version = ver.version;
                                d->dFlags         = flags;
                                eReturnValues ret = get_CISS_RAID_Device(handle, d);
                                if (ret != SUCCESS)
                                {
                                    failedGetDeviceCount++;
                                }
                                ++d;
                                // If we were unable to open the device using get_CSMI_Device, then  we need to
                                // increment the failure counter. - TJE
                                ++found;
                            }
                        }
                        // else something went wrong, and cannot enumerate additional devices.

                        // This was a CISS handle, remove it from the list!
                        // This will also increment us to the next handle
                        bool pointerAtBeginningOfRAIDList = raidList == *beginningOfList ? true : false;
                        raidList                          = remove_RAID_Handle(raidList, previousRaidListEntry);
                        if (pointerAtBeginningOfRAIDList)
                        {
                            // if the first entry in the list was removed, we need up update the pointer before we exit
                            // so that the code that called here won't have an invalid pointer
                            *beginningOfList = raidList;
                        }
                        handleRemoved = true;
                    }
                    // close handle to the controller
                    close(fd);
                    fd = -1;
                }
            }
            if (!handleRemoved)
            {
                previousRaidListEntry =
                    raidList; // store handle we just looked at in case we need to remove one from the list
                // increment to next element in the list
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
    RESTORE_NONNULL_COMPARE
    return returnValue;
}
#endif // ENABLE_CISS
