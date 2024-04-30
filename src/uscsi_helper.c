//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2023 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stropts.h>
#include <sys/stat.h>
#include <sys/scsi/impl/uscsi.h>
#include <sys/mount.h> //unmounting disks
#include <sys/mnttab.h> //reading mounted partition info
#include "uscsi_helper.h"
#include "cmds.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"
#include "usb_hacks.h"




extern bool validate_Device_Struct(versionBlock);

//If this returns true, a timeout can be sent with INFINITE_TIMEOUT_VALUE definition and it will be issued, otherwise you must try MAX_CMD_TIMEOUT_SECONDS instead
bool os_Is_Infinite_Timeout_Supported(void)
{
    return false;//TODO: Documentation does not state if an infinite timeout is supported. If it actually is, need to define the infinite timeout value properly, and set it to the correct value
}

/*
Return the device name without the path.
e.g. return c?t?d? from /dev/rdsk/c?t?d?
*/
static void set_Device_Name(const char* filename, char * name, int sizeOfName)
{
    char * s = strrchr(filename, '/') + 1;
    snprintf(name, sizeOfName, "%s", s);
}

//This API is very similar to the linux API.
//TODO: May need to use handle without rdsk in the name
//      We use the rdsk for issuing passthrough commands, but the other handle is probably what will really be needed to do this correctly
//      I do not have a solaris machine or VM available to test this right now, so it's written as best I could to prevent needing changes. - TJE
static int get_Partition_Count(const char* blockDeviceName)
{
    int result = 0;
    FILE* mount = fopen("/etc/mnttab", "r");//we only need to know about mounted partitions. Mounted partitions need to be known so that they can be unmounted when necessary. - TJE
    struct mnttab entry;
    memset(&entry, 0, sizeof(struct mnttab));
    if (mount)
    {
        while (0 == getmntent(mount, &entry))
        {
            if (strstr(entry.mnt_special, blockDeviceName))
            {
                //Found a match, increment result counter.
                ++result;
            }
        }
        fclose(mount);
    }
    else
    {
        result = -1;//indicate an error
    }
    return result;
}

#define PART_INFO_NAME_LENGTH (32)
#define PART_INFO_PATH_LENGTH (64)
typedef struct _spartitionInfo
{
    char fsName[PART_INFO_NAME_LENGTH];
    char mntPath[PART_INFO_PATH_LENGTH];
}spartitionInfo, * ptrsPartitionInfo;
//partitionInfoList is a pointer to the beginning of the list
//listCount is the number of these structures, which should be returned by get_Partition_Count
static int get_Partition_List(const char* blockDeviceName, ptrsPartitionInfo partitionInfoList, int listCount)
{
    int result = SUCCESS;
    int matchesFound = 0;
    if (listCount > 0)
    {
        FILE* mount = fopen("/etc/mnttab", "r");//we only need to know about mounted partitions. Mounted partitions need to be known so that they can be unmounted when necessary. - TJE
        if (mount)
        {
            struct mnttab entry;
            memset(&entry, 0, sizeof(struct mnttab));
            while (0 == getmntent(mount, &entry))
            {
                if (strstr(entry.mnt_special, blockDeviceName))
                {
                    //found a match, copy it to the list
                    if (matchesFound < listCount)
                    {
                        snprintf((partitionInfoList + matchesFound)->fsName, PART_INFO_NAME_LENGTH, "%s", entry.mnt_special);
                        snprintf((partitionInfoList + matchesFound)->mntPath, PART_INFO_PATH_LENGTH, "%s", entry.mnt_mountp);
                        ++matchesFound;
                    }
                    else
                    {
                        result = MEMORY_FAILURE;//out of memory to copy all results to the list.
                    }
                }
            }
            fclose(mount);
        }
        else
        {
            result = FAILURE;
        }
    }
    return result;
}

static int set_Device_Partition_Info(tDevice* device)
{
    int ret = SUCCESS;
    int partitionCount = 0;
    char blockHandle[OS_HANDLE_NAME_MAX_LENGTH] = {0};
    snprintf(blockHandle, OS_HANDLE_NAME_MAX_LENGTH, "/dev/");
    set_Device_Name(device->os_info.name, &blockHandle[strlen("/dev/")], OS_HANDLE_NAME_MAX_LENGTH - strlen("/dev/"));
    //note: this mess above is to get rid of /rdsk/ in the file handle as that raw disk handle won't be part of the information in the mount tab file.
    partitionCount = get_Partition_Count(blockHandle);
#if defined (_DEBUG)
    printf("Partition count for %s = %d\n", blockHandle, partitionCount);
#endif
    if (partitionCount > 0)
    {
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;
        device->os_info.fileSystemInfo.hasActiveFileSystem = false;
        device->os_info.fileSystemInfo.isSystemDisk = false;
        ptrsPartitionInfo parts = C_CAST(ptrsPartitionInfo, calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts)
        {
            if (SUCCESS == get_Partition_List(blockHandle, parts, partitionCount))
            {
                int iter = 0;
                for (; iter < partitionCount; ++iter)
                {
                    //since we found a partition, set the "has file system" bool to true
                    device->os_info.fileSystemInfo.hasActiveFileSystem = true;
#if defined (_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    //check if one of the partitions is /boot and mark the system disk when this is found
                    //TODO: Should / be treated as a system disk too?
                    if (strncmp((parts + iter)->mntPath, "/boot", 5) == 0)
                    {
                        device->os_info.fileSystemInfo.isSystemDisk = true;
#if defined (_DEBUG)
                        printf("found system disk\n");
#endif
                    }
                }
            }
            safe_Free(parts);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
    {
        device->os_info.fileSystemInfo.fileSystemInfoValid = true;
        device->os_info.fileSystemInfo.hasActiveFileSystem = false;
        device->os_info.fileSystemInfo.isSystemDisk = false;
    }
    return ret;
}

int get_Device(const char *filename, tDevice *device)
{
    int ret = SUCCESS;

    if ((device->os_info.fd = open(filename, O_RDWR | O_NONBLOCK)) < 0)
    {
        perror("open");
        device->os_info.last_error = C_CAST(unsigned int, errno);
        printf("open failure\n");
        ret = FAILURE;
    }

    device->os_info.osType = OS_SOLARIS;
    device->os_info.minimumAlignment = sizeof(void *);//setting to be compatible with certain aligned memory allocation functions.

    //Adding support for different device discovery options. 
    if (device->dFlags == OPEN_HANDLE_ONLY)
    {
        return ret;
    }

    if ((device->os_info.fd >= 0) && (ret == SUCCESS))
    {
        //set the name
        snprintf(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
        set_Device_Partition_Info(device);
        //set the friendly name
        set_Device_Name(filename, device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH);

        //set the OS Type
        device->os_info.osType = OS_SOLARIS;

        //uscsi documentation: http://docs.oracle.com/cd/E23824_01/html/821-1475/uscsi-7i.html
        device->drive_info.interface_type = SCSI_INTERFACE;
        device->drive_info.drive_type = SCSI_DRIVE;
        if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
        {
            //TODO: Actually get the VID and PID set before calling this.
            setup_Passthrough_Hacks_By_ID(device);
        }
        //fill in the device info
        ret = fill_Drive_Info_Data(device);
        
        //set the drive type now
        switch (device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
        case USB_INTERFACE:
            device->drive_info.drive_type = ATA_DRIVE;
            break;
        case NVME_INTERFACE:
            device->drive_info.drive_type = NVME_DRIVE;
            break;
        case SCSI_INTERFACE:
            if (0 == strncmp(device->drive_info.T10_vendor_ident, "ATA", 3))
            {
                device->drive_info.drive_type = ATA_DRIVE;
            }
            else
            {
                device->drive_info.drive_type = SCSI_DRIVE;
            }
            break;
        default:
            device->drive_info.drive_type = UNKNOWN_DRIVE;
            break;
        }
    }

    return ret;
}

static int uscsi_Reset(int fd, int resetFlag)
{
    struct uscsi_cmd uscsi_io;
    int ret = SUCCESS;

    memset(&uscsi_io, 0, sizeof(uscsi_io));

    uscsi_io.uscsi_flags |= resetFlag;
    ret = ioctl(fd, USCSICMD, &uscsi_io);
    if (ret < 0)
    {
        //TODO: check errno to figure out failure versus not supported???
        ret = OS_COMMAND_NOT_AVAILABLE;
    }
    else
    {
        ret = SUCCESS;
    }
    return ret;
}

int os_Device_Reset(tDevice *device)
{
    //NOTE: USCSI_RESET is the same thing, but for legacy versions
    //TODO: is USCSI_RESET_LUN better???
    return uscsi_Reset(device->os_info.fd, USCSI_RESET_TARGET);
}
    
int os_Bus_Reset(tDevice *device)
{
    //USCSI_RESET_ALL seems to imply a bus reset
    return uscsi_Reset(device->os_info.fd, USCSI_RESET_ALL);
}

int os_Controller_Reset(M_ATTR_UNUSED tDevice *device)
{
    return OS_COMMAND_NOT_AVAILABLE;
}

int send_IO (ScsiIoCtx *scsiIoCtx)
{
    int ret = FAILURE;
    switch (scsiIoCtx->device->drive_info.interface_type)
    {
    case SCSI_INTERFACE:
    case IDE_INTERFACE:
    case USB_INTERFACE:
        ret = send_uscsi_io(scsiIoCtx);
        break;
    case RAID_INTERFACE:
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
        break;
    case NVME_INTERFACE:
        //haven't defined a way to send NVME commands yet. Need to add this in later...-TJE
        ret = send_uscsi_io(scsiIoCtx);
        break;
    default:
        if(VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("Target Device does not have a valid interface %d\n", scsiIoCtx->device->drive_info.interface_type);
        }
    }

    if (scsiIoCtx->device->delay_io)
    {
        delay_Milliseconds(scsiIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= scsiIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", scsiIoCtx->device->delay_io);
        }
    }

    return ret;
}

int send_uscsi_io(ScsiIoCtx *scsiIoCtx)
{
    //http://docs.oracle.com/cd/E23824_01/html/821-1475/uscsi-7i.html
    struct uscsi_cmd uscsi_io;
    int ret = SUCCESS;

    memset(&uscsi_io, 0, sizeof(uscsi_io));
    if(VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
    {
        printf("Sending command with send_IO\n");
    }

    if (scsiIoCtx->timeout > USCSI_MAX_CMD_TIMEOUT_SECONDS || scsiIoCtx->device->drive_info.defaultTimeoutSeconds > USCSI_MAX_CMD_TIMEOUT_SECONDS)
    {
        return OS_TIMEOUT_TOO_LARGE;
    }

    uscsi_io.uscsi_timeout = scsiIoCtx->timeout;
    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        uscsi_io.uscsi_timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            uscsi_io.uscsi_timeout = scsiIoCtx->timeout;
        }
        else
        {
            uscsi_io.uscsi_timeout = 15;//default to 15 second timeout
        }
    }
    uscsi_io.uscsi_cdb = C_CAST(caddr_t, scsiIoCtx->cdb);
    uscsi_io.uscsi_cdblen = scsiIoCtx->cdbLength;
    uscsi_io.uscsi_rqbuf = C_CAST(caddr_t, scsiIoCtx->psense);
    uscsi_io.uscsi_rqlen = scsiIoCtx->senseDataSize;
    uscsi_io.uscsi_bufaddr = C_CAST(caddr_t, scsiIoCtx->pdata);
    uscsi_io.uscsi_buflen = scsiIoCtx->dataLength;

    //set the uscsi flags for the command
    uscsi_io.uscsi_flags = USCSI_ISOLATE | USCSI_RQENABLE;//base flags
    switch (scsiIoCtx->direction)
    {
    case XFER_NO_DATA:
        break;
    case XFER_DATA_IN:
        uscsi_io.uscsi_flags |= USCSI_READ;
        break;
    case XFER_DATA_OUT:
        uscsi_io.uscsi_flags |= USCSI_WRITE;
        break;
    default:
        if(VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
        {
            printf("%s Didn't understand direction\n",__FUNCTION__);
        }
        return BAD_PARAMETER;
    }

    // \revisit: should this be FF or something invalid than 0?
    scsiIoCtx->returnStatus.format = 0xFF;
    scsiIoCtx->returnStatus.senseKey = 0;
    scsiIoCtx->returnStatus.asc = 0;
    scsiIoCtx->returnStatus.ascq = 0;

    seatimer_t commandTimer;
    memset(&commandTimer, 0, sizeof(seatimer_t));

    //issue the io
    start_Timer(&commandTimer);
    ret = ioctl(scsiIoCtx->device->os_info.fd, USCSICMD, &uscsi_io);
    stop_Timer(&commandTimer);
    if( ret < 0)
    {
        ret = FAILURE;
        if(VERBOSITY_BUFFERS <= scsiIoCtx->device->deviceVerbosity)
        {
            perror("send_IO");
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

static int uscsi_filter( const struct dirent *entry )
{
    //in this folder everything will start with a c.
    int uscsiHandle = strncmp("c",entry->d_name,1);
    if(uscsiHandle != 0)
    {
        return !uscsiHandle;
    }
    //now, we need to filter out the device names that have "p"s for the partitions and "s"s for the slices
    char *partitionOrSlice = strpbrk(entry->d_name, "pPsS");
    if(partitionOrSlice != NULL)
    {
        return 0;
    }
    else
    {
        return !uscsiHandle;
    }
}

int close_Device(tDevice *device)
{
    int retValue = 0;
    if(device)
    {
        retValue = close(device->os_info.fd);
        device->os_info.last_error = C_CAST(unsigned int, errno);
        if(retValue == 0)
        {
            device->os_info.fd = -1;
            return SUCCESS;
        }
        else
        {
            return FAILURE;
        }
    }
    else
    {
        return MEMORY_FAILURE;
    }
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
    int  num_devs = 0;

    struct dirent **namelist;
    num_devs = scandir("/dev/rdsk", &namelist, uscsi_filter, alphasort);
    for (int iter = 0; iter < num_devs; ++iter)
    {
        safe_Free(namelist[iter])
    }
    safe_Free(namelist)
    if (num_devs >= 0)
    {
        *numberOfDevices = C_CAST(uint32_t, num_devs);
    }
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
int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, M_ATTR_UNUSED uint64_t flags)
{
    int returnValue = SUCCESS;
    uint32_t numberOfDevices = 0;
    uint32_t num_rdsk = 0;
    uint32_t driveNumber = 0, found = 0, failedGetDeviceCount = 0, permissionDeniedCount = 0;
    char name[80] = { 0 }; //Because get device needs char
    int fd;
    tDevice * d = NULL;
    
    struct dirent **namelist;
    int scandirres = scandir("/dev/rdsk", &namelist, uscsi_filter, alphasort);
    if (scandirres > 0)
    {
        num_rdsk = C_CAST(uint32_t, scandirres);
    }
    
    char **devs = C_CAST(char **, calloc(num_rdsk + 1, sizeof(char *)));
    uint32_t i = 0;
    for(; i < num_rdsk; i++)
    {
        size_t handleSize = (strlen("/dev/rdsk/") + strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i] = C_CAST(char *, malloc(handleSize));
        snprintf(devs[i], handleSize, "/dev/rdsk/%s", namelist[i]->d_name);
        safe_Free(namelist[i])
    }
    devs[i] = NULL;
    safe_Free(namelist)

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
        for (driveNumber = 0; ((driveNumber >= 0 && driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < num_rdsk) && (found < numberOfDevices)); ++driveNumber)
        {
            if (!devs[driveNumber] || strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            memset(name, 0, sizeof(name));//clear name before reusing it
            snprintf(name, sizeof(name), "%s", devs[driveNumber]);
            fd = -1;
            //lets try to open the device.      
            fd = open(name, O_RDWR | O_NONBLOCK);
            if (fd >= 0)
            {
                close(fd);
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
            safe_Free(devs[driveNumber])
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if(permissionDeniedCount == (num_devs))
        {
            returnValue = PERMISSION_DENIED;
        }
	    else if (failedGetDeviceCount && returnValue != PERMISSION_DENIED)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    safe_Free(devs)
    return returnValue;
}

int os_Read(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool forceUnitAccess, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

int os_Write(M_ATTR_UNUSED tDevice *device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED bool forceUnitAccess, M_ATTR_UNUSED uint8_t *ptrData, M_ATTR_UNUSED uint32_t dataSize)
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

int send_NVMe_IO(M_ATTR_UNUSED nvmeCmdCtx *nvmeIoCtx)
{
    return NOT_SUPPORTED;
}

int pci_Read_Bar_Reg(M_ATTR_UNUSED tDevice * device, M_ATTR_UNUSED uint8_t * pData, M_ATTR_UNUSED uint32_t dataSize)
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

int os_Lock_Device(tDevice *device)
{
    int ret = SUCCESS;
    //Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    //disable O_NONBLOCK
    flags &= ~O_NONBLOCK;
    //Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

int os_Unlock_Device(tDevice *device)
{
    int ret = SUCCESS;
    //Get flags
    int flags = fcntl(device->os_info.fd, F_GETFL);
    //enable O_NONBLOCK
    flags |= O_NONBLOCK;
    //Set Flags
    fcntl(device->os_info.fd, F_SETFL, flags);
    return ret;
}

int os_Update_File_System_Cache(M_ATTR_UNUSED tDevice* device)
{
    //TODO: Complete this stub when this is figured out - TJE
    return NOT_SUPPORTED;
}

int os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

int os_Unmount_File_Systems_On_Device(tDevice *device)
{
    int ret = SUCCESS;
    int partitionCount = 0;
    char blockHandle[OS_HANDLE_NAME_MAX_LENGTH] = {0};
    snprintf(blockHandle, OS_HANDLE_NAME_MAX_LENGTH, "/dev/");
    set_Device_Name(device->os_info.name, &blockHandle[strlen("/dev/")], OS_HANDLE_NAME_MAX_LENGTH - strlen("/dev/"));
    //note: this mess above is to get rid of /rdsk/ in the file handle as that raw disk handle won't be part of the information in the mount tab file.
    partitionCount = get_Partition_Count(blockHandle);
#if defined (_DEBUG)
    printf("Partition count for %s = %d\n", blockHandle, partitionCount);
#endif
    if (partitionCount > 0)
    {
        ptrsPartitionInfo parts = C_CAST(ptrsPartitionInfo, calloc(int_to_sizet(partitionCount), sizeof(spartitionInfo)));
        if (parts)
        {
            if (SUCCESS == get_Partition_List(blockHandle, parts, partitionCount))
            {
                int iter = 0;
                for (; iter < partitionCount; ++iter)
                {
#if defined (_DEBUG)
                    printf("Found mounted file system: %s - %s\n", (parts + iter)->fsName, (parts + iter)->mntPath);
#endif
                    int umountResult = 0;
                    if (0 > (umountResult = umount2((parts + iter)->mntPath, MS_FORCE)))
                    {
                        if (errno == ENOTSUP)
                        {
                            //try again without the force flag since it may not be supported.
                            umountResult = umount2((parts + iter)->mntPath, 0);
                        }
                        if (0 > umountResult)
                        {
                            ret = FAILURE;
                            device->os_info.last_error = C_CAST(unsigned int, errno);
                            if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
                            {
                                printf("Unable to unmount %s: \n", (parts + iter)->mntPath);
                                print_Errno_To_Screen(errno);
                                printf("\n");
                            }
                        }
                    }
                }
            }
            safe_Free(parts);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    return ret;
}
