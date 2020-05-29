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
#include "common_public.h"

#include "platform_helper.h"

#if defined (ENABLE_CSMI)
#include "csmi_helper_func.h"
#endif

int load_Bin_Buf( char *filename, void *myBuf, size_t bufSize )
{
    //int ret = UNKNOWN;
    FILE     *fp;
    uint32_t bytesRead = 0;

    //Open file

    if ((fp = fopen(filename, "rb")) == NULL)
    {
        return FILE_OPEN_ERROR;
    }

    fseek(fp, 0, SEEK_SET); //should open to start but hey

    //Read file contents into buffer
    bytesRead = (uint32_t)fread(myBuf, 1, bufSize, fp);
    fclose(fp);

    return bytesRead;
}

bool scan_Drive_Type_Filter(tDevice *device, uint32_t scanFlags)
{
    bool showDevice = false;
    //strip off all the other flags first
    scanFlags &= ALL_DRIVES;
    //if no filter flags are being used, then we need to just return true to show the device
    if (scanFlags == DEFAULT_SCAN)
    {
        showDevice = true;
    }
    else
    {
        bool showUSB = false;
        bool showATA = false;
        bool showSCSI = false;
        bool showNVMe = false;
        bool showRAID = false;
        if ((scanFlags & USB_DRIVES) > 0)
        {
            showUSB = true;
        }
        if ((scanFlags & ATA_DRIVES) > 0)
        {
            showATA = true;
        }
        if ((scanFlags & SCSI_DRIVES) > 0)
        {
            showSCSI = true;
        }
        if ((scanFlags & NVME_DRIVES) > 0)
        {
            showNVMe = true;
        }
        if ((scanFlags & RAID_DRIVES) > 0)
        {
            showRAID = true;
        }
        if (showATA)
        {
            if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type != USB_INTERFACE)
            {
                showDevice = true;
            }
        }
        if (showUSB && device->drive_info.interface_type == USB_INTERFACE)
        {
            showDevice = true;
        }
        if (showSCSI && device->drive_info.drive_type == SCSI_DRIVE)
        {
            showDevice = true;
        }
        if (showNVMe && device->drive_info.drive_type == NVME_DRIVE)
        {
            showDevice = true;
        }
        if (showRAID && device->drive_info.drive_type == RAID_DRIVE)
        {
            showDevice = true;
        }
    }
    return showDevice;
}

bool scan_Interface_Type_Filter(tDevice *device, uint32_t scanFlags)
{
    bool showInterface = false;
    //filter out other flags that don't matter here
    scanFlags &= ALL_INTERFACES;
    //if no filter flags are being used, then we need to just return true to show the device
    if (scanFlags == DEFAULT_SCAN )
    {
        showInterface = true;
    }
    else
    {
        bool showUSBInterface = false;
        bool showATAInterface = false;
        bool showSCSIInterface = false;
        bool showNVMeInterface = false;
        bool showRAIDInterface = false;
        if ((scanFlags & USB_INTERFACE_DRIVES) > 0)
        {
            showUSBInterface = true;
        }
        if ((scanFlags & IDE_INTERFACE_DRIVES) > 0)
        {
            showATAInterface = true;
        }
        if ((scanFlags & SCSI_INTERFACE_DRIVES) > 0)
        {
            showSCSIInterface = true;
        }
        if ((scanFlags & NVME_INTERFACE_DRIVES) > 0)
        {
            showNVMeInterface = true;
        }
        if ((scanFlags & RAID_INTERFACE_DRIVES) > 0)
        {
            showRAIDInterface = true;
        }
        if (showUSBInterface && device->drive_info.interface_type == USB_INTERFACE)
        {
            showInterface = true;
        }
        if (showATAInterface && device->drive_info.interface_type == IDE_INTERFACE)
        {
            showInterface = true;
        }
        if (showSCSIInterface && device->drive_info.interface_type == SCSI_INTERFACE)
        {
            showInterface = true;
        }
        if (showNVMeInterface && device->drive_info.interface_type == NVME_INTERFACE)
        {
            showInterface = true;
        }
        if (showRAIDInterface && device->drive_info.interface_type == RAID_INTERFACE)
        {
            showInterface = true;
        }
    }
    return showInterface;
}

void write_JSON_To_File(void *customData, char *message)
{
    FILE *jsonFile = (FILE*)customData;
    if (jsonFile)
    {
        //fwrite(message, 1, strlen(message), jsonFile);
        //TODO: Add exit code to this function to detect errors
        if ((fwrite(message, 1, strlen(message), jsonFile) != strlen(message)) || ferror(jsonFile))
        {
            perror("Error writing data to a file!\n");
        }
    }
}

//this is the "generic" scan. It uses the OS defined "get device count" and "get device list" calls to do the scan.
void scan_And_Print_Devs(unsigned int flags, OutputInfo *outputInfo, eVerbosityLevels scanVerbosity)
{
    uint32_t deviceCount = 0;
#if defined (ENABLE_CSMI)
    uint32_t csmiDeviceCount = 0;
    bool csmiDeviceCountValid = false;
#endif
#if defined (ENABLE_OFNVME)
    uint32_t ofNDeviceCount = 0;
    bool ofNDeviceCountValid = false;
#endif
    uint32_t getCountFlags = 0;
    if (flags & AGRESSIVE_SCAN)
    {
        getCountFlags |= BUS_RESCAN_ALLOWED;
    }
    if (SUCCESS == get_Device_Count(&deviceCount, getCountFlags))
    {
        if (deviceCount > 0)
        {
            tDevice * deviceList = (tDevice*)calloc_aligned(deviceCount, sizeof(tDevice), 8);
            versionBlock version;
            if (!deviceList)
            {
                char errorMessage[50] = { 0 };
                snprintf(errorMessage, 50, "calloc failure in scan to get %" PRIu32 " devices!", deviceCount);
                perror(errorMessage);
                return;
            }
            memset(&version, 0, sizeof(versionBlock));
            version.size = sizeof(tDevice);
            version.version = DEVICE_BLOCK_VERSION;
            uint64_t getDeviceflags = FAST_SCAN;

            //set the verbosity for all devices before the scan
            for (uint32_t devi = 0; devi < deviceCount; ++devi)
            {
                deviceList[devi].deviceVerbosity = scanVerbosity;
            }

#if defined (ENABLE_CSMI)
            if (flags & IGNORE_CSMI)
            {
                getDeviceflags |= GET_DEVICE_FUNCS_IGNORE_CSMI;
            }
#endif
            int ret = get_Device_List(deviceList, deviceCount * sizeof(tDevice), version, getDeviceflags);
            if (ret == SUCCESS || ret == WARN_NOT_ALL_DEVICES_ENUMERATED)
            {
                bool printToScreen = true;
                bool fileOpened = false;
                if (outputInfo)
                {
                    switch (outputInfo->outputFormat)
                    {
                    case SEAC_OUTPUT_TEXT:
                        //make sure that the file it open to write...
                        if (!outputInfo->outputFilePtr)
                        {
                            char fileNameAndPath[OPENSEA_PATH_MAX] = { 0 };
                            if (outputInfo->outputPath && *outputInfo->outputPath && strlen(*outputInfo->outputPath))
                            {
                                strcpy(fileNameAndPath, *outputInfo->outputPath);
                                strcat(fileNameAndPath, "/");
                            }
                            if (outputInfo->outputFileName && *outputInfo->outputFileName && strlen(*outputInfo->outputFileName))
                            {
                                strcat(fileNameAndPath, *outputInfo->outputFileName);
                            }
                            else
                            {
                                strcat(fileNameAndPath, "scanOutput");
                            }
                            strcat(fileNameAndPath, ".txt");
                            if (!(outputInfo->outputFilePtr = fopen(fileNameAndPath, "w+")))
                            {
                                safe_Free(deviceList);
                                perror("could not open file!");
                                return;
                            }
                        }
                        fileOpened = true;
                        break;
                    default://assume standard text output
                        break;
                    }
                }
                if (printToScreen)
                {
                    printf("%-8s %-12s %-23s %-22s %-10s\n", "Vendor", "Handle", "Model Number", "Serial Number", "FwRev");
                }
                else
                {
                    //if json, open and put header
                    switch (outputInfo->outputFormat)
                    {
                    case SEAC_OUTPUT_TEXT:
                        fprintf(outputInfo->outputFilePtr, "%-8s %-12s %-23s %-22s %-10s\n", "Vendor", "Handle", "Model Number", "Serial Number", "FwRev");
                        break;
                    default:
                        break;
                    }
                }
#if defined (ENABLE_CSMI)
                if (!(flags & IGNORE_CSMI))
                {
                    int csmiRet = get_CSMI_Device_Count(&csmiDeviceCount, flags);//get number of CSMI devices that are in the device list so we know when to start looking for duplicates in the csmi devices
                    if (csmiRet == SUCCESS && csmiDeviceCount > 0)
                    {
                        csmiDeviceCountValid = true;
                    }
                }
#endif
                for (uint32_t devIter = 0; devIter < deviceCount; ++devIter)
                {
                    if (ret == WARN_NOT_ALL_DEVICES_ENUMERATED && UNKNOWN_DRIVE == deviceList[devIter].drive_info.drive_type)
                    {
                        continue;
                    }
                    if (flags & SCAN_SEAGATE_ONLY)
                    {
                        if (is_Seagate_Family(&deviceList[devIter]) == NON_SEAGATE)
                        {
                            continue;
                        }
                    }
#if defined (ENABLE_CSMI)
                    if (csmiDeviceCountValid && devIter >= (deviceCount - csmiDeviceCount))//if the csmi device count is valid then we found some for the scan and need to see if we need to check for duplicates.
                    {
                        //check if we are being asked to show duplicates or not.
                        if (!(flags & ALLOW_DUPLICATE_DEVICE))
                        {
                            bool skipThisDevice = false;
                            for (uint32_t dupCheck = 0; dupCheck < (deviceCount - csmiDeviceCount); ++dupCheck)
                            {
                                //check if the WWN is valid (non-zero value) and then check if it matches anything else already in the list...this should be faster than the SN comparison below. - TJE
                                if (deviceList[devIter].drive_info.worldWideName != 0 && deviceList[devIter].drive_info.worldWideName == deviceList[dupCheck].drive_info.worldWideName)
                                {
                                    skipThisDevice = true;
                                    break;
                                }
                                //check if the SN is valid (non-zero length) and then check if it matches anything already seen in the list... - TJE
                                else if (strlen(deviceList[devIter].drive_info.serialNumber) && strcmp(deviceList[devIter].drive_info.serialNumber, deviceList[dupCheck].drive_info.serialNumber) == 0)
                                {
                                    skipThisDevice = true;
                                    break;
                                }
                            }
                            if (skipThisDevice)
                            {
                                continue;
                            }
                        }
                    }
#endif
                    if (scan_Drive_Type_Filter(&deviceList[devIter], flags) && scan_Interface_Type_Filter(&deviceList[devIter], flags))
                    {
                        char displayHandle[256] = { 0 };
#if defined(_WIN32)
                        strcpy(displayHandle, deviceList[devIter].os_info.friendlyName);
#else
                        strcpy(displayHandle, deviceList[devIter].os_info.name);
#endif
#if defined (__linux__) && !defined(VMK_CROSS_COMP) && !defined(UEFI_C_SOURCE)
                        if ((flags & SG_TO_SD) > 0)
                        {
                            char *genName = NULL;
                            char *blockName = NULL;
                            if (SUCCESS == map_Block_To_Generic_Handle(displayHandle, &genName, &blockName))
                            {
                                memset(displayHandle, 0, sizeof(displayHandle));
                                strcpy(displayHandle, genName);
                                strcat(displayHandle, "<->");
                                strcat(displayHandle, blockName);
                            }
                            safe_Free(genName);
                            safe_Free(blockName);
                        }
                        else if ((flags & SD_HANDLES) > 0)
                        {
                            char *genName = NULL;
                            char *blockName = NULL;
                            if (SUCCESS == map_Block_To_Generic_Handle(displayHandle, &genName, &blockName))
                            {
                                memset(displayHandle, 0, sizeof(displayHandle));
                                sprintf(displayHandle, "/dev/%s", blockName);
                            }
                            safe_Free(genName);
                            safe_Free(blockName);
                        }
#endif
                        char printable_sn[SERIAL_NUM_LEN + 1] = { 0 };
                        strcpy(printable_sn, deviceList[devIter].drive_info.serialNumber);
                        //if seagate scsi, need to truncate to 8 digits
                        if (deviceList[devIter].drive_info.drive_type == SCSI_DRIVE && is_Seagate_Family(&deviceList[devIter]) == SEAGATE)
                        {
                            memset(printable_sn, 0, SERIAL_NUM_LEN);
                            memcpy(printable_sn, deviceList[devIter].drive_info.serialNumber, 8);
                        }
                        //now show the results (or save to a file)
                        if (printToScreen)
                        {
                            printf("%-8s %-12s %-23s %-22s %-10s\n", \
                                deviceList[devIter].drive_info.T10_vendor_ident, displayHandle, \
                                deviceList[devIter].drive_info.product_identification, \
                                printable_sn, \
                                deviceList[devIter].drive_info.product_revision);
                        }
                        else
                        {
                            switch (outputInfo->outputFormat)
                            {
                            case SEAC_OUTPUT_TEXT:
                                fprintf(outputInfo->outputFilePtr, "%-8s %-12s %-23s %-22s %-10s\n", \
                                    deviceList[devIter].drive_info.T10_vendor_ident, displayHandle, \
                                    deviceList[devIter].drive_info.product_identification, \
                                    printable_sn, \
                                    deviceList[devIter].drive_info.product_revision);
                                break;
                            default://TODO: add other output format types
                                break;
                            }
                        }
                        fflush(stdout);
                    }
                }
                if (!printToScreen)
                {
                    switch (outputInfo->outputFormat)
                    {
                    case SEAC_OUTPUT_TEXT:
                        //nothing that we need to do....(maybe put some new line characters?)
                        break;
                    default:
                        break;
                    }
                    if (fileOpened)
                    {
                        if ((fflush(outputInfo->outputFilePtr) != 0) || ferror(outputInfo->outputFilePtr))
                        {
                            perror("Error flushing data!\n");
                            fclose(outputInfo->outputFilePtr);
                            return ERROR_WRITING_FILE;
                        }
                        fclose(outputInfo->outputFilePtr);
                    }
                }
            }
            safe_Free_aligned(deviceList);
        }
        else
        {
            printf("No devices found\n");
        }
    }
    else
    {
        printf("Unable to get number of devices from OS\n");
    }
    return;
}

bool validate_Device_Struct(versionBlock sanity)
{   
    size_t tdevSize = sizeof(tDevice);
    if ((sanity.size == tdevSize) && (sanity.version == DEVICE_BLOCK_VERSION))
    {
        return true;
    }
    else
    {
        return false;
    }
}

int get_Opensea_Transport_Version(apiVersionInfo * ver)
{
    if (ver)
    {
        ver->majorVersion = OPENSEA_TRANSPORT_MAJOR_VERSION;
        ver->minorVersion = OPENSEA_TRANSPORT_MINOR_VERSION;
        ver->patchVersion = OPENSEA_TRANSPORT_PATCH_VERSION;
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

int get_Version_Block(versionBlock * blk)
{
    if (blk)
    {
        blk->size = sizeof(tDevice);
        blk->version = DEVICE_BLOCK_VERSION;
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

void set_IEEE_OUI(uint32_t* ieeeOUI, tDevice *device, bool USBchildDrive)
{
    uint8_t naa = 0;
    uint64_t wwn = 0;
    if (!USBchildDrive)
    {
        wwn = device->drive_info.worldWideName;
    }
    else
    {
        wwn = device->drive_info.bridge_info.childWWN;
    }
    naa = (uint8_t)((wwn & 0xF000000000000000ULL) >> 60);
    switch (naa)
    {
    case 2://bytes 2,3,4
        *ieeeOUI = (uint32_t)((wwn & 0x0000FFFFFF000000ULL) >> 24);
        break;
    case 5://most common - ATA requires this and I think SCSI almost always matches    bytes 0 - 3 (half of 0, half of 3) see SPC4 for details
    case 6://same as NAA format 5
        *ieeeOUI = (uint32_t)((wwn & 0x0FFFFFF000000000ULL) >> 36);
        break;
    default:
        //don't do anything since we don't have a way to parse it out of here or it is a new format that wasn't defined when writing this
        break;
    }
}

bool is_Maxtor_String(char* string)
{
    bool isMaxtor = false;
    size_t maxtorLen = strlen("MAXTOR");
    size_t stringLen = strlen(string);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, string);
        localString[stringLen] = '\0';
        convert_String_To_Upper_Case(localString);
        if (strlen(localString) >= maxtorLen && strncmp(localString, "MAXTOR", maxtorLen) == 0)
        {
            isMaxtor = true;
        }
        safe_Free(localString);
    }
    return isMaxtor;
}

bool is_Maxtor(tDevice *device, bool USBchildDrive)
{
    bool isMaxtor = false;
    uint32_t ieeeOUI = 0;
    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_MAXTOR:
        isMaxtor = true;
        break;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
        {
            //on a USB drive, check the child information as well as the bridge information
            isMaxtor = is_Maxtor(device, true);
        }
        else
        {
            isMaxtor = false;
        }
        break;
    }
    if (!isMaxtor)
    {
        //we need to check the Vendor ID if SCSI or USB interface
        if (device->drive_info.interface_type == USB_INTERFACE || (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        {
            isMaxtor = is_Maxtor_String(device->drive_info.T10_vendor_ident);
        }
        //if still false (ata drive should be), then check the model number
        if (!isMaxtor)
        {
            isMaxtor = is_Maxtor_String(device->drive_info.product_identification);
            //if after a model number check, the result is still false and it's USB, we need to check the child drive information just to be certain
            if (!isMaxtor && device->drive_info.interface_type == USB_INTERFACE)
            {
                isMaxtor = is_Maxtor_String(device->drive_info.bridge_info.childDriveMN);
            }
        }
    }
    return isMaxtor;
}

bool is_Seagate_VendorID(tDevice *device)
{
    bool isSeagate = false;
    size_t seagateLen = strlen("SEAGATE");
    size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, device->drive_info.T10_vendor_ident);
        localString[stringLen] = '\0';
        convert_String_To_Upper_Case(localString);
        if (strlen(localString) >= seagateLen && strncmp(localString, "SEAGATE", seagateLen) == 0)
        {
            isSeagate = true;
        }
        safe_Free(localString);
    }
    return isSeagate;
}

bool is_Seagate_MN(char* string)
{
    bool isSeagate = false;
    size_t seagateLen = strlen("ST");
    size_t stringLen = strlen(string);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, string);
        localString[stringLen] = '\0';
        //convert_String_To_Upper_Case(localString);//Removing uppercase converstion, thus making this a case sensitive comparison to fix issues with other non-Seagate products being detected as Seagate.
        if (strlen(localString) >= seagateLen && strncmp(localString, "ST", seagateLen) == 0)
        {
            isSeagate = true;
        }
        safe_Free(localString);
    }
    return isSeagate;
}

bool is_Seagate(tDevice *device, bool USBchildDrive)
{
    bool isSeagate = false;
    uint32_t ieeeOUI = 0;

    //This check should work well enough, but we do support checking the IEEE OUI as well now. It must be set in the WWN correctly with the NAA field set to 5h or 6h - TJE
    if ((device->drive_info.interface_type == NVME_INTERFACE) &&
        (device->drive_info.adapter_info.vendorID == SEAGATE_VENDOR_ID))
    {
        return true;
    }

    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_SEAGATE1:
    case IEEE_SEAGATE2:
    case IEEE_SEAGATE3:
    case IEEE_SEAGATE4:
    case IEEE_SEAGATE5:
    case IEEE_SEAGATE6:
    case IEEE_SEAGATE7:
    case IEEE_SEAGATE8:
        isSeagate = true;
        break;
    case IEEE_SEAGATE_NVME:
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            isSeagate = true;
        }
        break;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
        {
            //on a USB drive, check the child information as well as the bridge information
            isSeagate = is_Seagate(device, true);
        }
        else
        {
            isSeagate = false;
        }
        break;
    }
    if (!isSeagate)
    {
        //we need to check the Vendor ID if SCSI or USB interface
        if (device->drive_info.interface_type == USB_INTERFACE ||
            (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        {
            isSeagate = is_Seagate_VendorID(device);
        }
        //if still false (ata drive should be), then check the model number
        if (!isSeagate)
        {
            isSeagate = is_Seagate_MN(device->drive_info.product_identification);
            //if after a model number check, the result is still false and it's USB, we need to check the child drive information just to be certain
            if (!isSeagate && device->drive_info.interface_type == USB_INTERFACE)
            {
                isSeagate = is_Seagate_MN(device->drive_info.bridge_info.childDriveMN);
            }
        }
    }
    return isSeagate;
}

bool is_Conner_Model_Number(char *mn)
{
    bool isConner = false;
    //found online. Not sure how accurate this will be
    if (strncmp(mn, "CFA", 3) == 0 ||
        strncmp(mn, "CFL", 3) == 0 ||
        strncmp(mn, "CFN", 3) == 0 ||
        strncmp(mn, "CFS", 3) == 0 ||
        strncmp(mn, "CP", 2) == 0
        )
    {
        isConner = true;
    }
    return isConner;
}

bool is_Conner_VendorID(tDevice *device)
{
    bool isConner = false;
    size_t connerLen = strlen("CONNER");
    size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, device->drive_info.T10_vendor_ident);
        localString[stringLen] = '\0';
        if (strlen(localString) >= connerLen && strncmp(localString, "CONNER", connerLen) == 0)
        {
            isConner = true;
        }
        safe_Free(localString);
    }
    return isConner;
}

bool is_Connor(tDevice *device, bool USBchildDrive)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return is_Conner_VendorID(device);
    }
    else
    {
        if (USBchildDrive)
        {
            return is_Conner_Model_Number(device->drive_info.bridge_info.childDriveMN);
        }
        else
        {
            bool result = is_Conner_Model_Number(device->drive_info.product_identification);
            if (!result && device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
            {
                return is_Conner_Model_Number(device->drive_info.bridge_info.childDriveMN);
            }
            return result;
        }
    }
}

bool is_CDC_VendorID(tDevice *device)
{
    bool isCDC = false;
    if (M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)
    {
        size_t cdcLen = strlen("CDC");
        size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char *localString = (char *)calloc(stringLen + 1, sizeof(char));
            if (localString == NULL)
            {
                perror("calloc failure");
                return false;
            }
            strcpy(localString, device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (strlen(localString) >= cdcLen && strncmp(localString, "CDC", cdcLen) == 0)
            {
                isCDC = true;
            }
            safe_Free(localString);
        }
    }
    return isCDC;
}

bool is_DEC_VendorID(tDevice *device)
{
    bool isDEC = false;
    if (M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)
    {
        size_t cdcLen = strlen("DEC");
        size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char *localString = (char *)calloc(stringLen + 1, sizeof(char));
            if (localString == NULL)
            {
                perror("calloc failure");
                return false;
            }
            strcpy(localString, device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (strlen(localString) >= cdcLen && strncmp(localString, "DEC", cdcLen) == 0)
            {
                isDEC = true;
            }
            safe_Free(localString);
        }
    }
    return isDEC;
}

bool is_MiniScribe_VendorID(tDevice *device)
{
    bool isMiniscribe = false;
    size_t miniscribeLen = strlen("MINSCRIB");
    size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, device->drive_info.T10_vendor_ident);
        localString[stringLen] = '\0';
        if (strlen(localString) >= miniscribeLen && strncmp(localString, "MINSCRIB", miniscribeLen) == 0)
        {
            isMiniscribe = true;
        }
        safe_Free(localString);
    }
    return isMiniscribe;
}

bool is_Quantum_VendorID(tDevice *device)
{
    bool isQuantum = false;
    if (M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)//must be direct access block device for HDD
    {
        size_t quantumLen = strlen("QUANTUM");
        size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char *localString = (char *)calloc(stringLen + 1, sizeof(char));
            if (localString == NULL)
            {
                perror("calloc failure");
                return false;
            }
            strcpy(localString, device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (strlen(localString) >= quantumLen && strncmp(localString, "QUANTUM", quantumLen) == 0)
            {
                isQuantum = true;
            }
            safe_Free(localString);
        }
    }
    return isQuantum;
}

bool is_Quantum_Model_Number(char* string)
{
    bool isQuantum = false;
    size_t quantumLen = strlen("Quantum");
    size_t stringLen = strlen(string);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, string);
        localString[stringLen] = '\0';
        if (strlen(localString) >= quantumLen && (strncmp(localString, "Quantum", quantumLen) == 0 || strncmp(localString, "QUANTUM", quantumLen) == 0))
        {
            isQuantum = true;
        }
        safe_Free(localString);
    }
    return isQuantum;
}

bool is_Quantum(tDevice *device, bool USBchildDrive)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return is_Quantum_VendorID(device);
    }
    else
    {
        if (USBchildDrive)
        {
            return is_Quantum_Model_Number(device->drive_info.bridge_info.childDriveMN);
        }
        else
        {
            bool result = false;
            if (M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)//must be direct access block device for HDD
            {
                result = is_Quantum_Model_Number(device->drive_info.product_identification);
            }
            if (!result && device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
            {
                return is_Quantum_Model_Number(device->drive_info.bridge_info.childDriveMN);
            }
            return result;
        }
    }
}

bool is_PrarieTek_VendorID(tDevice *device)
{
    bool isPrarieTek = false;
    if (M_GETBITRANGE(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)//must be direct access block device for HDD
    {
        size_t prarieTekLen = strlen("PRAIRIE");
        size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char *localString = (char *)calloc(stringLen + 1, sizeof(char));
            if (localString == NULL)
            {
                perror("calloc failure");
                return false;
            }
            strcpy(localString, device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (strlen(localString) >= prarieTekLen && strncmp(localString, "PRAIRIE", prarieTekLen) == 0)
            {
                isPrarieTek = true;
            }
            safe_Free(localString);
        }
    }
    return isPrarieTek;
}

bool is_LaCie(tDevice *device)
{
    bool isLaCie = false;
    //LaCie drives do not have a IEEE OUI, so the only way we know it's LaCie is to check the VendorID field reported by the device
    size_t lacieLen = strlen("LACIE");
    size_t stringLen = strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char *vendorID = (char *)calloc(9, sizeof(char));
        if (vendorID == NULL)
        {
            perror("calloc failure");
            return MEMORY_FAILURE;
        }
        strcpy(vendorID, device->drive_info.T10_vendor_ident);
        vendorID[8] = '\0';
        convert_String_To_Upper_Case(vendorID);
        if (strlen(vendorID) >= lacieLen && strncmp(vendorID, "LACIE", lacieLen) == 0)
        {
            isLaCie = true;
        }
        safe_Free(vendorID);
    }
    return isLaCie;
}

bool is_Samsung_String(char* string)
{
    bool isSamsung = false;
    size_t samsungLen = strlen("SAMSUNG");
    size_t stringLen = strlen(string);
    if (stringLen > 0)
    {
        char *localString = (char *)calloc(stringLen + 1, sizeof(char));
        if (localString == NULL)
        {
            perror("calloc failure");
            return false;
        }
        strcpy(localString, string);
        localString[stringLen] = '\0';
        convert_String_To_Upper_Case(localString);
        if (strlen(localString) >= samsungLen && strncmp(localString, "SAMSUNG", samsungLen) == 0)
        {
            isSamsung = true;
        }
        safe_Free(localString);
    }
    return isSamsung;
}

bool is_Samsung_HDD(tDevice *device, bool USBchildDrive)
{
    bool isSamsung = false;
    bool isSSD = false;
    uint32_t ieeeOUI = 0;
    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_SEAGATE_SAMSUNG_HDD:
    case IEEE_SAMSUNG_HDD1:
    case IEEE_SAMSUNG_HDD2:
        isSamsung = true;
        break;
    case IEEE_SAMSUNG_SSD:
        isSSD = true;//fall through
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive && !isSSD)
        {
            //on a USB drive, check the child information as well as the bridge information
            isSamsung = is_Samsung_HDD(device, true);
        }
        else
        {
            isSamsung = false;
        }
        break;
    }
    isSSD = is_SSD(device);
    if (!isSamsung && !isSSD)
    {
        //this fall back method should only be called on samsung HDD's and these should only be really old ones without a WWN which should be a minority. All drives with IEEE_SEAGATE_SAMSUNG_HDD should be caught long before this
        //we need to check the Vendor ID if SCSI or USB interface
        if (device->drive_info.interface_type == USB_INTERFACE || (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        {
            isSamsung = is_Samsung_String(device->drive_info.T10_vendor_ident);
        }
        //if still false (ata drive should be), then check the model number
        if (!isSamsung)
        {
            isSamsung = is_Samsung_String(device->drive_info.product_identification);
            //if after a model number check, the result is still false and it's USB, we need to check the child drive information just to be certain
            if (!isSamsung && device->drive_info.interface_type == USB_INTERFACE)
            {
                isSamsung = is_Samsung_String(device->drive_info.bridge_info.childDriveMN);
            }
        }
    }
    return isSamsung;
}

bool is_Seagate_Model_Vendor_A(tDevice *device)
{
    bool isSeagateVendorA = false;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        char* vendorAModel1 = "S650DC";
        char* vendorAModel2 = "S630DC";
        char* vendorAModel3 = "S610DC";
        if (strncmp(vendorAModel1, device->drive_info.product_identification, strlen(vendorAModel1)) == 0 ||
            strncmp(vendorAModel2, device->drive_info.product_identification, strlen(vendorAModel2)) == 0 ||
            strncmp(vendorAModel3, device->drive_info.product_identification, strlen(vendorAModel3)) == 0)
        {
            isSeagateVendorA = true;
        }
    }
    return isSeagateVendorA;
}

bool is_Vendor_A(tDevice *device, bool USBchildDrive)
{
    bool isVendorA = false;
    uint32_t ieeeOUI = 0;
    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_VENDOR_A:
    case IEEE_VENDOR_A_TECHNOLOGY:
        isVendorA = true;
        break;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
        {
            //on a USB drive, check the child information as well as the bridge information
            isVendorA = is_Vendor_A(device, true);
        }
        else
        {
            isVendorA = false;
        }
        break;
    }
    if (!isVendorA)
    {
        //this fall back method should only be called on samsung HDD's and these should only be really old ones without a WWN which should be a minority. All drives with IEEE_SEAGATE_SAMSUNG_HDD should be caught long before this
        //we need to check the Vendor ID if SCSI or USB interface
        //if (device->drive_info.interface_type == USB_INTERFACE || (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        //{
        //    isVendorA = is_Vendor_A_String(device->drive_info.T10_vendor_ident);
        //}
        //if still false (ata drive should be), then check the model number
        //if (!isVendorA)
        //{
        //   isVendorA = is_Vendor_A_String(device->drive_info.product_identification);
        //    //if after a model number check, the result is still false and it's USB, we need to check the child drive information just to be certain
        //    if (!isVendorA && device->drive_info.interface_type == USB_INTERFACE)
        //    {
        //        isVendorA = is_Vendor_A_String(device->drive_info.bridge_info.childDriveMN);
        //    }
        //}
    }
    return isVendorA;
}

bool is_Seagate_Model_Number_Vendor_B(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    //we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (strcmp(device->drive_info.bridge_info.childDriveMN, "Nytro100 ZA128CM0001") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "Nytro100 ZA256CM0001") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "Nytro100 ZA512CM0001") == 0
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (strcmp(device->drive_info.product_identification, "Nytro100 ZA128CM0001") == 0 ||
            strcmp(device->drive_info.product_identification, "Nytro100 ZA256CM0001") == 0 ||
            strcmp(device->drive_info.product_identification, "Nytro100 ZA512CM0001") == 0
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_B(device, true));
        }
    }
    return isSeagateVendor;
}
bool is_Seagate_Model_Number_Vendor_C(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    //we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        //Enterprise
        if (strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A0240") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A0480") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A0960") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A1920") == 0
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        //Enterprise
        if (strcmp(device->drive_info.product_identification, "XF1230-1A0240") == 0 ||
            strcmp(device->drive_info.product_identification, "XF1230-1A0480") == 0 ||
            strcmp(device->drive_info.product_identification, "XF1230-1A0960") == 0 ||
            strcmp(device->drive_info.product_identification, "XF1230-1A1920") == 0
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_C(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_D(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    //we need to check the model number for the ones used on the vendor products
    if (USBchildDrive)
    {
        if (strncmp(device->drive_info.bridge_info.childDriveMN, "ST500HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST500HM001", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480HM001", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240HM001", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120HM001", 10) == 0
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (strncmp(device->drive_info.product_identification, "ST500HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST500HM001", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480HM001", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240HM001", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120HM001", 10) == 0
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_D(device, true));
        }
    }
    return isSeagateVendor;
}
bool is_Seagate_Model_Number_Vendor_E(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    //we need to check the model number for the ones used on the vendor products
    if (USBchildDrive)
    {
        //Enterprise
        if (
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FN0021", 11) == 0
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        //Enterprise
        if (
            strncmp(device->drive_info.product_identification, "ST100FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST100FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST100FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST100FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FN0021", 11) == 0
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_E(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_F(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;

    //we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ST") != NULL)
                && (strstr(device->drive_info.bridge_info.childDriveMN, "401") != NULL))                                                                                    //newer models
            ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CM") == 7))
            ||
			((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != NULL)
				&& (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "GM") == 7))
			||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "YA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CM") == 7))
            ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "XA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "DC") == 7))         //older models
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (
            ((strstr(device->drive_info.product_identification, "ST") != NULL)
                && (strstr(device->drive_info.product_identification, "401") != NULL))                                                                                  //newer models
            ||
            ((strstr(device->drive_info.product_identification, "ZA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.product_identification, "CM") == 7))
            ||
			((strstr(device->drive_info.product_identification, "ZA") != NULL)
				&& (find_last_occurrence_in_string(device->drive_info.product_identification, "GM") == 7))
			||
            ((strstr(device->drive_info.product_identification, "YA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.product_identification, "CM") == 7))
            ||
            ((strstr(device->drive_info.product_identification, "XA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.product_identification, "DC") == 7))       //older models
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_F(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_G(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;

    //we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (((strstr(device->drive_info.bridge_info.childDriveMN, "XA") != NULL)
            && ((find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "LE") == 7)
                || (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "ME") == 7)))
            ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "NM") == 7))
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (((strstr(device->drive_info.product_identification, "XA") != NULL)
            && ((find_last_occurrence_in_string(device->drive_info.product_identification, "LE") == 7)
                || (find_last_occurrence_in_string(device->drive_info.product_identification, "ME") == 7)))
            ||
            ((strstr(device->drive_info.product_identification, "ZA") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.product_identification, "NM") == 7))
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_G(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_H(tDevice *device, bool USBchildDrive)
{
    bool isSeagateVendor = false;

    //we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (((strstr(device->drive_info.bridge_info.childDriveMN, "ZP") != NULL)
            && ((find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CM") == 7)
                || (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "GM") == 7)))
            ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "XP") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "DC") == 7))
            )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (((strstr(device->drive_info.product_identification, "ZP") != NULL)
            && ((find_last_occurrence_in_string(device->drive_info.product_identification, "CM") == 7)
                || (find_last_occurrence_in_string(device->drive_info.product_identification, "GM") == 7)))
            ||
            ((strstr(device->drive_info.product_identification, "XP") != NULL)
                && (find_last_occurrence_in_string(device->drive_info.product_identification, "DC") == 7))
            )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_H(device, true));
        }
    }
    return isSeagateVendor;
}

eSeagateFamily is_Seagate_Family(tDevice *device)
{
    eSeagateFamily isSeagateFamily = NON_SEAGATE;
    uint8_t iter = 0;
    uint8_t numChecks = 11;//maxtor, seagate, samsung, lacie, seagate-Vendor. As the family of seagate drives expands, we will need to increase this and add new checks
    for (iter = 0; iter < numChecks && isSeagateFamily == NON_SEAGATE; iter++)
    {
        switch (iter)
        {
        case 0://is_Samsung_HDD
            if (is_Samsung_HDD(device, false))
            {
                //If this is an NVMe drive, we need to check if it's Seagate since both Samsung HDD's and Seagate NVMe drives use the same IEEE OUI
                if(device->drive_info.drive_type == NVME_DRIVE)
                {
                    if (is_Seagate(device, false))
                    {
                        isSeagateFamily = SEAGATE;
                    }
                }
                else
                {
                    isSeagateFamily = SAMSUNG;
                }
            }
            break;
        case 1://is_Seagate
            if (is_Seagate(device, false))
            {
                isSeagateFamily = SEAGATE;
                if (is_Seagate_Model_Vendor_A(device))
                {
                    isSeagateFamily = SEAGATE_VENDOR_A;
                }
                else if (is_Seagate_Model_Number_Vendor_C(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_C;
                }
                else if (is_Seagate_Model_Number_Vendor_B(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_B;
                }
                else if (is_Seagate_Model_Number_Vendor_E(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_E;
                }
                else if (is_Seagate_Model_Number_Vendor_D(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_D;
                }
                else if (is_Seagate_Model_Number_Vendor_F(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_F;
                }
                else if (is_Seagate_Model_Number_Vendor_G(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_G;
                }
                else if (is_Seagate_Model_Number_Vendor_H(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_H;
                }
            }
            break;
        case 2://is_Maxtor
            if (is_Maxtor(device, false))
            {
                isSeagateFamily = MAXTOR;
            }
            break;
        case 3://is_Vendor_A
            if (is_Vendor_A(device, false))
            {
                //we aren't done yet! Need to check the model number to make sure it's a partnership product
                if (is_Seagate_Model_Vendor_A(device))
                {
                    isSeagateFamily = SEAGATE_VENDOR_A;
                }
                else
                {
                    isSeagateFamily = NON_SEAGATE;
                }
            }
            break;
        case 4://is_LaCie
            if (is_LaCie(device))
            {
                isSeagateFamily = LACIE;
            }
            break;
        case 5://is_Quantum
            if (is_Quantum(device, false))
            {
                isSeagateFamily = SEAGATE_QUANTUM;
            }
            break;
        case 6://is_Connor
            if (is_Connor(device, false))
            {
                isSeagateFamily = SEAGATE_CONNER;
            }
            break;
        case 7://is_Miniscribe
            //TODO: figure out what model numbers would be reported for ATA/IDE so we can detect them
            if (is_MiniScribe_VendorID(device))
            {
                isSeagateFamily = SEAGATE_MINISCRIBE;
            }
            break;
        case 8://is_VENDOR_F
            if (is_Seagate_Model_Number_Vendor_F(device, false))
            {
                isSeagateFamily = SEAGATE_VENDOR_F;
            }
            break;
        case 9://is_VENDOR_G
            if (is_Seagate_Model_Number_Vendor_G(device, false))
            {
                isSeagateFamily = SEAGATE_VENDOR_G;
            }
            break;
        case 10://is_Vendor_H - NVMe SSDs
            if (is_Seagate_Model_Number_Vendor_H(device, false))
            {
                isSeagateFamily = SEAGATE_VENDOR_H;
            }
            break;
            //TODO: Add in CDC, DEC, & PrarieTek detection. Currently not in since these drives are even more rare than the Conner and Miniscribe drives...
        default:
            break;
        }
    }
    return isSeagateFamily;
}
bool is_SSD(tDevice *device)
{
    if (device->drive_info.media_type == MEDIA_NVM || device->drive_info.media_type == MEDIA_SSD)
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool is_SATA(tDevice *device)
{
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        //Word 76 will be greater than zero, and never 0xFFFF on a SATA drive (bit 0 must be cleared to zero)
        if (device->drive_info.IdentifyData.ata.Word076 > 0 && device->drive_info.IdentifyData.ata.Word076 != 0xFFFF)
        {
            return true;
        }
    }
    return false;
}

bool is_Sector_Size_Emulation_Active(tDevice *device)
{
    if (device->drive_info.bridge_info.isValid)
    {
        if (device->drive_info.deviceBlockSize != device->drive_info.bridge_info.childDeviceBlockSize)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

int calculate_Checksum(uint8_t *pBuf, uint32_t blockSize)
{
    if (
        (blockSize > LEGACY_DRIVE_SEC_SIZE) 
        || (blockSize == 0)
        || (pBuf == NULL)
        )
    {
        return BAD_PARAMETER;
    }
    
    printf("%s: blksize %d, pBuf %p\n",__FUNCTION__, blockSize,pBuf);
    
    uint8_t checksum = 0; 
    uint32_t counter = 0;
    for (counter = 0; counter < 511; counter++)
    {
        checksum = checksum + pBuf[counter];
    }
    pBuf[511] = (~checksum + 1);

    printf("%s: counter %d\n",__FUNCTION__, counter);

    return SUCCESS;
}

#define DATA_64K 65536
#define DATA_32K 32768

uint32_t get_Sector_Count_For_Read_Write(tDevice *device)
{
    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
    case RAID_INTERFACE:
    case NVME_INTERFACE:
        //set the sector count for a 64k transfer. This is most compatible (typically 128 sectors at a time-512B sector size) - TJE
        return DATA_64K / device->drive_info.deviceBlockSize;
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        //set the sector count for a 32k transfer. This is most compatible on these external interface drives since they typically have RAM limitations on the bridge chip - TJE
        return DATA_32K / device->drive_info.deviceBlockSize;
    default:
        return 64;//just set something in case they try to use this value but didn't check the return code from this function - TJE
    }
    return 0;
}

uint32_t get_Sector_Count_For_512B_Based_XFers(tDevice *device)
{
    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
    case RAID_INTERFACE:
    case NVME_INTERFACE:
        //set the sector count for a 64k transfer.
        return 128;//DATA_64K / 512;
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        //set the sector count for a 32k transfer. This is most compatible on these external interface drives since they typically have RAM limitations on the bridge chip - TJE
        return 64;//DATA_32K / 512;
    default:
        return 64;//just set something in case they try to use this value but didn't check the return code from this function - TJE
    }
    return 0;
}

uint32_t get_Sector_Count_For_4096B_Based_XFers(tDevice *device)
{
    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
    case RAID_INTERFACE:
    case NVME_INTERFACE:
        //set the sector count for a 64k transfer. 
        return 16;//DATA_64K / 4096;
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        //set the sector count for a 32k transfer. This is most compatible on these external interface drives since they typically have RAM limitations on the bridge chip - TJE
        return 8;//DATA_32K / 4096;
    default:
        return 8;//just set something in case they try to use this value but didn't check the return code from this function - TJE
    }
    return 0;
}

void print_Command_Time(uint64_t timeInNanoSeconds)
{
    double printTime = (double)timeInNanoSeconds;
    uint8_t unitCounter = 0;
    bool breakLoop = false;;
    while (printTime > 1 && unitCounter <= 6)
    {
        switch (unitCounter)
        {
        case 6://shouldn't get this far...
            break;
        case 5://h to d
            if ((printTime / 24) < 1)
            {
                breakLoop = true;
            }
            break;
            break;
        case 4://m to h
        case 3://s to m
            if ((printTime / 60) < 1)
            {
                breakLoop = true;
            }
            break;
        case 0://ns to us
        case 1://us to ms
        case 2://ms to s
        default:
            if ((printTime / 1000) < 1)
            {
                breakLoop = true;
            }
            break;
        }
        if (breakLoop)
        {
            break;
        }
        switch (unitCounter)
        {
        case 6://shouldn't get this far...
            break;
        case 5://h to d
            printTime /= 24;
            break;
        case 4://m to h
        case 3://s to m
            printTime /= 60;
            break;
        case 0://ns to us
        case 1://us to ms
        case 2://ms to s
        default:
            printTime /= 1000;
            break;
        }
        if (unitCounter == 6)
        {
            break;
        }
        ++unitCounter;
    }
    printf("Command Time (");
    switch (unitCounter)
    {
    case 6://we shouldn't get to a days value, but room for future large drives I guess...-TJE
        printf("d): ");
        break;
    case 5:
        printf("h): ");
        break;
    case 4:
        printf("m): ");
        break;
    case 3:
        printf("s): ");
        break;
    case 2:
        printf("ms): ");
        break;
    case 1:
        printf("us): ");
        break;
    case 0:
        printf("ns): ");
        break;
    default://couldn't get a good conversion or something weird happened so show original nanoseconds.
        printf("ns): ");
        printTime = (double)timeInNanoSeconds;
        break;
    }
    printf("%0.02f\n\n", printTime);
}

void print_Time(uint64_t timeInNanoSeconds)
{
    double printTime = (double)timeInNanoSeconds;
    uint8_t unitCounter = 0;
    bool breakLoop = false;;
    while (printTime > 1 && unitCounter <= 6)
    {
        switch (unitCounter)
        {
        case 6://shouldn't get this far...
            break;
        case 5://h to d
            if ((printTime / 24) < 1)
            {
                breakLoop = true;
            }
            break;
            break;
        case 4://m to h
        case 3://s to m
            if ((printTime / 60) < 1)
            {
                breakLoop = true;
            }
            break;
        case 0://ns to us
        case 1://us to ms
        case 2://ms to s
        default:
            if ((printTime / 1000) < 1)
            {
                breakLoop = true;
            }
            break;
        }
        if (breakLoop)
        {
            break;
        }
        switch (unitCounter)
        {
        case 6://shouldn't get this far...
            break;
        case 5://h to d
            printTime /= 24;
            break;
        case 4://m to h
        case 3://s to m
            printTime /= 60;
            break;
        case 0://ns to us
        case 1://us to ms
        case 2://ms to s
        default:
            printTime /= 1000;
            break;
        }
        if (unitCounter == 6)
        {
            break;
        }
        ++unitCounter;
    }
    printf(" (");
    switch (unitCounter)
    {
    case 6://we shouldn't get to a days value, but room for future large drives I guess...-TJE
        printf("d): ");
        break;
    case 5:
        printf("h): ");
        break;
    case 4:
        printf("m): ");
        break;
    case 3:
        printf("s): ");
        break;
    case 2:
        printf("ms): ");
        break;
    case 1:
        printf("us): ");
        break;
    case 0:
        printf("ns): ");
        break;
    default://couldn't get a good conversion or something weird happened so show original nanoseconds.
        printf("ns): ");
        printTime = (double)timeInNanoSeconds;
        break;
    }
    printf("%0.02f\n", printTime);
}


uint64_t align_LBA(tDevice *device, uint64_t LBA)
{
    uint16_t logicalPerPhysical = device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize;
    if (logicalPerPhysical > 1)
    {
        //make sure the incoming LBA is aligned to the start of the physical sector it is in
        uint64_t tempLBA = LBA / (uint64_t)logicalPerPhysical;
        tempLBA *= (uint64_t)logicalPerPhysical;
        LBA = tempLBA - device->drive_info.sectorAlignment;
    }
    return LBA;
}


int remove_Duplicate_Devices(tDevice *deviceList, volatile uint32_t * numberOfDevices, removeDuplicateDriveType rmvDevFlag)
{
    volatile uint32_t i, j;
    bool sameSlNo = false;
    int ret = UNKNOWN;


    /*
    Go through all the devices in the list. 
    */
    for (i = 0; i < *numberOfDevices - 1; i++)
    {
        /*
        Go compare it to all the rest of the drives i + 1. 
        */
        for (j = i + 1; j < *numberOfDevices; j++)

        {
#ifdef _DEBUG
            printf("%s --> For drive i : %d and j : %d \n", __FUNCTION__, i, j);
#endif
            ret = SUCCESS;
            sameSlNo = false;

            if ( ((deviceList + i) && strlen((deviceList + i)->drive_info.serialNumber) > 0) &&
                 ((deviceList + j) && strlen((deviceList + j)->drive_info.serialNumber) > 0) )
            {
                 sameSlNo = (strncmp((deviceList + i)->drive_info.serialNumber,
                     (deviceList + j)->drive_info.serialNumber,
                     strlen((deviceList + i)->drive_info.serialNumber)) == 0);
            }

            if (sameSlNo)
            {
#ifdef _DEBUG
                printf("We have same serial no \n");
#endif
#if defined (_WIN32)
                /* We are supporting csmi only - for now */
                if (rmvDevFlag.csmi != 0)
                {
                    if (is_CSMI_Device(deviceList + i))
                    {
                        ret |= remove_Device(deviceList, i, numberOfDevices);
                        i--;
                        j--;
                    }

                    if (is_CSMI_Device(deviceList + j))
                    {
                        ret |= remove_Device(deviceList, j, numberOfDevices);
                        j--;
                    }
                }

#endif
            }
        }
    }
    return ret;
}

int remove_Device(tDevice *deviceList, uint32_t driveToRemoveIdx, volatile uint32_t * numberOfDevices)
{
    uint32_t i;
    int ret = FAILURE;

#ifdef _DEBUG
    printf("Removing Drive with index : %d \n", driveToRemoveIdx);
#endif

    if (driveToRemoveIdx >= *numberOfDevices)
    {
        return ret;
    }

    /*
     *  TODO - Use close_Handle() rather than free().
     **/
    if (is_CSMI_Device(deviceList + driveToRemoveIdx))
    {
        free((deviceList + driveToRemoveIdx)->raid_device);
    }

    for (i = driveToRemoveIdx; i < *numberOfDevices - 1; i++)
    {
        memcpy((deviceList + i), (deviceList + i + 1), sizeof(tDevice));
    }

    memset((deviceList + i), 0, sizeof(tDevice));
    *numberOfDevices -= 1;
    ret = SUCCESS;

    return ret;
}

bool is_CSMI_Device(tDevice *device)
{
    bool csmiDevice = true;

#ifdef _DEBUG
    printf("friendly name : %s interface_type : %d raid_device : %" PRIXPTR "\n",
        device->os_info.friendlyName, device->drive_info.interface_type, (uintptr_t)device->raid_device);
#endif

    csmiDevice = csmiDevice && (strncmp(device->os_info.friendlyName, "SCSI", 4) == 0);
    csmiDevice = csmiDevice && (device->drive_info.interface_type == RAID_INTERFACE);
    csmiDevice = csmiDevice && (device->raid_device != NULL);

#ifdef _DEBUG
    if (csmiDevice)
    {
        printf("This is a CSMI drive \n");
    }
    else
    {
        printf("This is not a CSMI drive \n");
    }
#endif
    return csmiDevice;
}

#if defined (_DEBUG)
//This function is more for debugging than anything else!
#include <stddef.h>
void print_tDevice_Size()
{
    printf("==Device struct information==\n");
    printf("--structure sizes--\n");
    printf("tDevice = %zu\n", sizeof(tDevice));
    printf("\tversionBlock = %zu\n", sizeof(versionBlock));
    printf("\tOSDriveInfo = %zu\n", sizeof(OSDriveInfo));
    printf("\tdriveInfo = %zu\n", sizeof(driveInfo));
    printf("\tvoid* raid_device = %zu\n", sizeof(void*));
    printf("\tissue_io_func = %zu\n", sizeof(issue_io_func));
    printf("\teDiscoveryOptions = %zu\n", sizeof(eDiscoveryOptions));
    printf("\teVerbosityLevels = %zu\n", sizeof(eVerbosityLevels));
    printf("\n--Important offsets--\n");
    printf("tDevice = 0\n");
    printf("\tversionBlock = %zu\n", offsetof(tDevice, sanity));
    printf("\tos_info = %zu\n", offsetof(tDevice, os_info));
    printf("\tdrive_info = %zu\n", offsetof(tDevice, drive_info));
    printf("\t\tIdentifyData = %zu\n", offsetof(tDevice, drive_info.IdentifyData));
    printf("\t\tATA Identify = %zu\n", offsetof(tDevice, drive_info.IdentifyData.ata));
    #if !defined (DISABLE_NVME_PASSTHROUGH)
    printf("\t\tNVMe CTRL ID = %zu\n", offsetof(tDevice, drive_info.IdentifyData.nvme.ctrl));
    printf("\t\tNVMe Namespace ID = %zu\n", offsetof(tDevice, drive_info.IdentifyData.nvme.ns));
    #endif
    printf("\t\tscsiVpdData = %zu\n", offsetof(tDevice, drive_info.scsiVpdData));
    printf("\t\tlastCommandSenseData = %zu\n", offsetof(tDevice, drive_info.lastCommandSenseData));
    printf("\traid_device = %zu\n", offsetof(tDevice, raid_device));
    printf("\tissue_io = %zu\n", offsetof(tDevice, issue_io));
    printf("\tissue_nvme_io = %zu\n", offsetof(tDevice, issue_nvme_io));
    printf("\tdFlags = %zu\n", offsetof(tDevice, dFlags));
    printf("\tdeviceVerbosity = %zu\n", offsetof(tDevice, deviceVerbosity));
    printf("\n");
}
#endif //_DEBUG

bool is_Removable_Media(tDevice *device)
{
    bool result = false;
    uint8_t scsiDevType;

    if(device->drive_info.interface_type == IDE_INTERFACE) 
    {
        if(device->drive_info.drive_type == UNKNOWN_DRIVE || 
           device->drive_info.drive_type == FLASH_DRIVE ||
           device->drive_info.drive_type == ATAPI_DRIVE || 
           device->drive_info.media_type == MEDIA_OPTICAL || 
           device->drive_info.media_type == MEDIA_SSM_FLASH || 
           device->drive_info.media_type == MEDIA_TAPE || 
           device->drive_info.media_type == MEDIA_UNKNOWN ||
           (device->drive_info.IdentifyData.ata.Word000 & BIT7) )
        {
            result = true;
        }
    }
    else if(device->drive_info.interface_type == SCSI_INTERFACE) 
    {
        scsiDevType = device->drive_info.scsiVpdData.inquiryData[0] & 0x1F;

        if (scsiDevType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE ||
            scsiDevType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE ||
            scsiDevType == PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE ||
            scsiDevType == PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE)
        {
            if (device->drive_info.scsiVpdData.inquiryData[1] & BIT7)
            {
                result = true;
            }
            else
            {
                result = false;
            }
        }
        else
        {
            result = true;
        }
        

    }
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES )
    {
        printf("Calling from file : %s function : %s line : %d \n", __FILE__, __FUNCTION__, __LINE__);
        if (result)
        {
            printf("This is a Removable Media");
        }
        else
        {
            printf("This is not a Removable Media");
        }
    }
    return result;
}
//https://usb-ids.gowdy.us/
//http://www.linux-usb.org/usb.ids
bool set_USB_Passthrough_Hacks_By_PID_and_VID(tDevice *device)
{
    bool passthroughHacksSet = false;
    //only change the ATA Passthrough type for USB (for legacy USB bridges)
    if (device->drive_info.interface_type == USB_INTERFACE)
    {
        //Most USB bridges are SAT so they'll probably fall into the default cases and issue an identify command for SAT
        switch (device->drive_info.adapter_info.vendorID)
        {
        case USB_Vendor_Seagate://0477
            switch (device->drive_info.adapter_info.productID)
            {
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_Seagate_RSS://0BC2
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0888://0BC2 VID
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_NEC;
                passthroughHacksSet = true;
                break;
            case 0x0500://ST3750640A
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 5;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 24;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 122880;
                break;
            case 0x0501://
                //revision 0002h
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_CYPRESS;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 6;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                //device->drive_info.passThroughHacks.scsiHacks.scsiInq
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;//TODO: Cypress passthrough has a bit for UDMA mode, but didn't appear to work in testing.
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x0502:
                //revision 0200h
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_TI;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands = true;//mutliple mode commands don't work in passthrough.
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x0503://Seagate External Drive
                //revision 0240h
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_CYPRESS;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 6;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDOffset = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDLength = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 14;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;//TODO: Cypress passthrough has a bit for UDMA mode, but didn't appear to work in testing.
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x1000://FreeAgentGoSmall
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 9;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x2100://FreeAgent Go
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x2101://FreeAgent Go
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x2120://FreeAgent Go
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x2300://Portable
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x2320://Expansion
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x2321://Expansion
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x2322://Expansion (Xbox drive uses this chip)
                //NOTE: Need to rerun a full test someday as we don't have full confirmation on supported read commands...just assumed same as above chip for now.
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x2330://Portable
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 512;
                break;
            case 0x2332://Portable
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 6;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 8192;
                break;
            case 0x2400://Backup
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x2700://BlackArmorDAS25
                passthroughHacksSet = true;
                //This particular bridge has lots of limitations
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 12;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 131072;
                break;
            case 0x3000://???
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x3001://FreeAgent
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x3010://FreeAgent Pro
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 122880;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 122880;
                break;
            case 0x3300://Desktop
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 17;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x3320://Expansion Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0x3330://External
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 512;
                break;
            case 0x3332://External
                //rev 0016h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 31;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 1024;
                break;
            case 0x3340://Expansion+ Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x5020://USB 2.0 Cable
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0x5021://FreeAgent GoFlex
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x5030://???
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0x5031://Freeagent GoFlex. 
                passthroughHacksSet = true;
                //This particular bridge has lots of limitations
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65024;
                break;
            case 0x5060://FreeAgent
                //rev 155h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x5070://FA GoFlex Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                //device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x5071://GoFlex Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x50A5://GoFlex Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0x50A7://GoFlex Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 512;
                break;
            case 0x5130://GoFlex Cable
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 12;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x5170://USB 3.0 Cable
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0x6126://D3 Station
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x617A: //GoFlex Slim Mac
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 2;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0x61B5://M3 Portable
                //rev 1402h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xA003://GoFlex Slim
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 12;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 209920;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 512;
                break;
            case 0xA013://Backup+ RD
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;//some testing shows 524288
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;//some testing shows 524288
                break;
            case 0xA014://Slim BK
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 3;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0xA0A4://GoFlex Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                //NOTE: TPSIU worked for identify but not other commands when tested.
                break;
            case 0xA313://Wireless
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;
                break;
            case 0xA314://Wireless Plus
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.turfValue = 12;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xAB00://Slim BK
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xAB01://BUP Fast SSD
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 8;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xAB02://Fast
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                //Hangs if sent a SCSI read with length set to 0.
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xAB10://BUP Slim SL
            //rev 938h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 82944;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 8192;
                break;
            case 0xAB20://Backup+ SL
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xAB21://Backup+ BL
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 512;
                break;
            case 0xAB24://BUP Slim
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                //Hangs if sent a SCSI read with length set to 0.
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0xAB2A://Fast HDD
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                //device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;//Not sure if this actually worked when the test tool pumped this result out. Off for now.
                //device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;//This only worked for identify
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                //This device has 2 HDDs in a RAID inside it.
                //There is no known way to address each one separately.
                //Also, for whatever reason, it is not possible to read any logs from the drive you can get identify data from.
                //This may be a pre-production unit that was tested and other shipping products may work differently. - TJE
                break;
            case 0xAB31://Backup+  Desk
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 8192;
                break;

            case 0xAA17://FireCuda Gaming SSD
                //NOTE: Recommend a retest for this device to double check the hacks. Most are setup based on other ASMedia bridge chip tests.
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_ASMEDIA;
                device->drive_info.drive_type = NVME_DRIVE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in the case an ATA passthrough command is attempted, it won't try this opcode since it can cause performance problems or crash the bridge
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_LaCie://059F
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1064://RuggedKey
                //One other note: For some reason, the MaxLBA that is reported is DIFFERENT between read capacty 10 and read capacity 16
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                break;
            case 0x1065://
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x1072://Fuel
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x1091://Rugged USB-C
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 11;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x10CD://Rugged SSD
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in the case an ATA passthrough command is attempted, it won't try this opcode since it can cause performance problems or crash the bridge
                device->drive_info.drive_type = NVME_DRIVE;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //NOTE: Add max passthrough transfer length hack set to 65536
                break;
            default:
                break;
            }
        case USB_Vendor_Maxtor://0D49
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x5020://5000DV
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDOffset = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDLength = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevOffset = 32;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevLength = 3;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                //device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536; //Unable to get a good test result on this, so this is currently commented out. - TJE
                break;
            case 0x7310://OneTouch
                //rev 0122h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x7410://Basics Desktop
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            case 0x7550://BlackArmor
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_Oxford://0928
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0008://NOTE: This should be retested for full hacks to improve performance.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_4G_Systems_GmbH://1955
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0102://Generic Frys
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 17;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_Initio://13FD
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1340://Seen in ThermalTake BlackX
                //Results are for revision 0210h
                //NOTE: This device doesn't allow for large LBAs, so it is limited on the SCSI read/write commands to a 32bit LBA.
                //      Using a 12TB drive, this reports 483.72GB....so may need an additional hack at some point to force passthrough read/write commands on a device like this.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 12;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                if (device->drive_info.adapter_info.revision == 0x0202)
                {
                    device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                }
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_ASMedia://174C
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x2362://USB to NVMe adapter
                //Tested 3 adapters. All report the exact same USB level information, but do in fact work differently (slightly)
                //The  difference seems to be only noticable in Inquiry Data
                //1. PID: "ASM236X NVME" could use mode sense 6 and read 6 commands
                //2. PID: "USB3.1 TO NVME" could NOT do these commands. So setting the lowest common set of rules.
                //If there are other ASMedia chips that vary in capabilities, then may need to adjust what is done in here, or add a hack to check INQ data to finish setting up remaining hacks
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_ASMEDIA_BASIC;
                device->drive_info.drive_type = NVME_DRIVE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in the case an ATA passthrough command is attempted, it won't try this opcode since it can cause performance problems or crash the bridge
                //device->drive_info.drive_type = NVME_DRIVE; //Uncomment this line when it is possible to issue NVMe passthrough commands behind ASMedia chips.
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities = true;
                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage = true;
                device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric = true;
                break;
            case 0x5106://Seen in ThermalTake BlackX 5G
                //Results are for revision 0001h
                //Does not seem to handle drives with a 4k logical sector size well.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 122880;//This seems to vary with each time this device is tested
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 36864;//This seems to vary with each time this device is tested.
                break;
            case 0x55AA://ASMT 2105
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_JMicron://152D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0551://USB 3.0 to SATA/PATA adapter
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 512512;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
                break;
            case 0x0562://USB to NVMe adapter
                //Rev 204h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in the case an ATA passthrough command is attempted, it won't try this opcode since it can cause performance problems or crash the bridge
                device->drive_info.drive_type = NVME_DRIVE;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //NOTE: Add max passthrough transfer length hack set to 65536
                break;
            case 0x0583://USB to NVMe adapter
                //Rev 205h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in the case an ATA passthrough command is attempted, it won't try this opcode since it can cause performance problems or crash the bridge
                device->drive_info.drive_type = NVME_DRIVE;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //NOTE: Add max passthrough transfer length hack set to 65536
                break;
            case 0x2338://Sabrent USB 2.0 to SATA/PATA. Only tested SATA.
                //NOTE: Some versions of this chip will NOT do SAT passthrough.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 122880;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 122880;
                break;
            case 0x2339://MiniD2 - NOTE: This has custom firmware. If other things use this chip, additional product verification will be necessary.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_Samsung://04E8
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1F05://S2 Portable
                //based on revision 0000h
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;//Bytes
                //set SCSI hacks
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;//bytes
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = false;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDOffset = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDLength = 8;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 11;
                //Serial number is not reported in inquiry data or any other known location
                break;
            case 0x5F12://Story Station
                passthroughHacksSet = true;
                //hacks based on revision 1302h. Not sure if revision level filter is needed right now
                //Set ATA passthrough hacks
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;//Bytes
                //set SCSI hacks
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;//bytes
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = true;
                break;
            case 0x6093://S2 portable 3
                passthroughHacksSet = true;
                //based on revision 0100h
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 524288;//Bytes
                //set SCSI hacks
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;//bytes
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                break;
            case 0x61C3://P3 Portable
                passthroughHacksSet = true;
                //based on revision 0E00h
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 523776;//Bytes
                //set SCSI hacks
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;//bytes
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                break;
            default: //unknown
                break;
            }
            break;
        case USB_Vendor_Silicon_Motion://090C
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1000://USB DISK - Rev1100
                //Don't set a passthrough type! This is a USB flash memory, that responds to one of the legacy command requests and it will break it!
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                passthroughHacksSet = true;
                //this device also supports the device identification VPD page even though the list of pages doesn't work.
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_ChipsBank://1E3D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x2093://v3.1.0.4
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 10;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Alcor_Micro_Corp://058F
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x6387://flash drive - Rev 0103
            case 0x1234://flash drive
            case 0x9380://flash drive
            case 0x9381://flash drive
            case 0x9382://flash drive
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughHacksSet = true;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Integrated_Techonology_Express_Inc://048D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1172://flash drive
            case 0x1176://flash drive - rev 0100
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughHacksSet = true;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_SanDisk_Corp://0781
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x5575://Cruzer Glide
                passthroughHacksSet = true;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 16;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                break;
            case 0x5580://Extreme
                passthroughHacksSet = true;
                device->drive_info.media_type = MEDIA_SSM_FLASH;//Leaving this as flash since it is a flash drive/thumb drive, but this is an odd one that seems to do SAT commands.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 15;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 1048576;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 1048576;
                break;
            case 0x5583://Ultra Fit
                passthroughHacksSet = true;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Kingston://13FE
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x3600://Patriot Memory PMA
                passthroughHacksSet = true;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 4;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                //vendor ID is all spaces, so skipping it
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 16;
                //Guessing that the "PMA" value is a revision number of some kind. 
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevOffset = 32;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevLength = 3;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Phison://0D7D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1300://USB DISK 2.0
                passthroughHacksSet = true;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 33;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData = true;
                //vendor ID is all spaces, so skipping it
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 16;
                //Guessing that the "PMA" value is a revision number of some kind. 
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevOffset = 32;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevLength = 3;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Symwave://1CA1
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x18AE://rev 0852
                //This is a USB to SAS adapter. Not everything works as it is targetted at read/write and other basic informatio only.
                //If sent commands it doesn't understand, this struggles.
                //Very basic information is set here in order to keep this simple. The main reason is because most capabilties will depend on the
                //Attached SAS device more than the USB adapter itself.
                //The exception to that rule is transfer size. Disabling ATA passthrough test also helps
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;//This disabled ATA passthrough which this devices doesn't support anyways
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 1048576;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;//This may not actually help this device. This hack is usually used to clear error conditions by sending a well known, easy command to clear previous SAT translation issues, but in this case, this may not be helpful
                device->drive_info.passThroughHacks.turfValue = 17;
                break;
            default:
                break;
            }
            break;
        default: //unknown
            break;
        }
    }
    return passthroughHacksSet;
}

//Vendor ID's, or OUI's, can be found here: https://regauth.standards.ieee.org/standards-ra-web/pub/view.html#registries
bool set_IEEE1394_Passthrough_Hacks_By_PID_and_VID(tDevice *device)
{
    bool passthroughHacksSet = false;
    if (device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        //It is unknown if any IEEE 1394 devices support any ATA passthrough.
        //Some devices had both USB and IEEE1394 interfaces and one may have passthrough while the other does not. They may even be different.
        switch (device->drive_info.adapter_info.vendorID)
        {
        case IEEE1394_Vendor_Maxtor://0010B9
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x005000://5000DV
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                //device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 6;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                //device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536; //Unable to get a good test result on this, so this is currently commented out. - TJE
                break;
            default:
                break;
            }
            break;
        case IEEE1394_Vendor_Seagate://002037
            switch (device->drive_info.adapter_info.productID)
            {
            default:
                break;
            }
            break;
        case IEEE1394_Vendor_Quantum://00E09E
            switch (device->drive_info.adapter_info.productID)
            {
            default:
                break;
            }
            break;
        case 0x000BC2://This vendor ID doesn't make sense for the product that was tested!!!
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x000000://Seagate external drive with USB and 1394. USB mode has passthrough, but that doesn't work in 1394. Likely 2 different chips
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                //device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 5;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288; //Unable to get a good test result on this, so this is currently commented out. - TJE
                break;
            default:
                break;
            }
            break;
        case 0x000500://This vendor ID doesn't make sense for the product that was tested!!!
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x000001://Seagate external drive with USB and 1394. USB mode has passthrough, but that doesn't work in 1394. Likely 2 different chips
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                //device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 4;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288; //Unable to get a good test result on this, so this is currently commented out. - TJE
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    return passthroughHacksSet;
}

bool setup_Passthrough_Hacks_By_ID(tDevice *device)
{
    bool success = false;
    //need to do things different for USB vs PCI vs IEEE1394, etc
    switch (device->drive_info.adapter_info.infoType)
    {
    case ADAPTER_INFO_USB:
        success = set_USB_Passthrough_Hacks_By_PID_and_VID(device);
        break;
    case ADAPTER_INFO_PCI://TODO: PCI device hacks based on known controllers with workarounds or other changes we can make.
        break;
    case ADAPTER_INFO_IEEE1394:
        success = set_IEEE1394_Passthrough_Hacks_By_PID_and_VID(device);
        break;
    default:
        break;
    }
    if (success)
    {
        device->drive_info.passThroughHacks.hacksSetByReportedID = true;
    }
    else
    {
        //The OS didn't report the vendor and product identifiers needed to set hacks this way. Anything else will be done based on known Product matches or trial and error
    }
    return success;
}
