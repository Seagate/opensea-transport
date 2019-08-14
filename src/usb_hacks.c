//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ****************************************************************************************** *****************************************************************************

// \file usb_hacks.c
// \brief Set of functions to check or make modifications to commands to work on USB bridges that don't always follow SCSI/SAT specs.

#include "usb_hacks.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"
#include "ata_helper.h"
#include "ata_helper_func.h"
#include <ctype.h>//for checking for printable characters
#include "common.h"

//Some VID/PID info comes from these pages:
//https://usb-ids.gowdy.us/
//http://www.linux-usb.org/usb.ids

void set_ATA_Passthrough_Type(tDevice *device)
{
    //set with VID/PID since it will be fastest if we have that info to use
    if (!set_ATA_Passthrough_Type_By_PID_and_VID(device))
    {
        //set with inquiry data next
        if (!set_ATA_Passthrough_Type_By_Inquiry_Data(device))
        {
            if (device->drive_info.ata_Options.enableLegacyPassthroughDetectionThroughTrialAndError)
            {
                //if legacy passthrough flag is enabled, try using trial and error to set it.
                //We don't want to always do trial and error since the OP-codes used are vendor unique and other USB vendors may be using them for potentially dangerous commands.
                set_ATA_Passthrough_Type_By_Trial_And_Error(device);
            }
        }
    }
}

bool set_ATA_Passthrough_Type_By_Inquiry_Data(tDevice *device)
{
    bool passthroughTypeSet = false;
    if (SUCCESS == scsi_Inquiry(device, &device->drive_info.scsiVpdData.inquiryData[0], 96, 0, false, false))
    {
        char vendorID[9] = { 0 };
        char productID[17] = { 0 };
        char revision[5] = { 0 };
        uint8_t responseFormat = M_Nibble0(device->drive_info.scsiVpdData.inquiryData[3]);
        if (responseFormat == 2)
        {
            memcpy(vendorID, &device->drive_info.scsiVpdData.inquiryData[8], 8);
            memcpy(productID, &device->drive_info.scsiVpdData.inquiryData[16], 16);
            memcpy(revision, &device->drive_info.scsiVpdData.inquiryData[32], 4);
            remove_Leading_And_Trailing_Whitespace(vendorID);
            remove_Leading_And_Trailing_Whitespace(productID);
            remove_Leading_And_Trailing_Whitespace(revision);
            if (strcmp(vendorID, "ATA") == 0)
            {
                passthroughTypeSet = true;
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_SAT;
            }
            else if (strcmp(vendorID, "SMI") == 0)
            {
                if (strcmp(productID, "USB DISK") == 0)
                {
                    passthroughTypeSet = true;
                    device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                    device->drive_info.media_type = MEDIA_SSM_FLASH;
                    //this should prevent sending it bad commands!
                }
            }
            else if (strcmp(vendorID, "") == 0 && strcmp(revision, "8.07") == 0)
            {
                passthroughTypeSet = true;
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                //this should prevent sending it bad commands!
            }
            else if (strcmp(vendorID, "SEAGATE") == 0)//Newer Seagate USB's will set "Seagate" so this can help filter based on case-sensitive comparison
            {
                if (strcmp(productID, "ST650211USB") == 0 || //Rev 4.02
                    strcmp(productID, "ST660211USB") == 0 || //rev 4.06
                    strcmp(productID, "ST760211USB") == 0)   //rev 3.03
                {
                    passthroughTypeSet = true;
                    device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_NEC;
                }
            }
            else
            {
                //Don't set anything! We don't know!
            }
        }
        //else response format of 1 or 0 means we have to check all vendor unique fields on case by case basis.
        else
        {
            //This is code that works on one old drive I have. Probably needs adjustment to work on everything!
            //Returned inq example:
            /*
            00 00 00 00 1f 00 00 00 53 54 39 31 32 30 38 32  ........ST912082
            36 41 20 20 20 20 20 20 20 20 20 20 20 20 20 20  6A              
            30 30 30 30 00 00 00 00 00 00 00 00 04 00 41 41  0000..........AA
            33 41 30 35 20 20 54 53 31 39 30 32 32 38 41 36  3A05  TS190228A6
            20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20                  
            20 20 20 20 20 20 20 20 20 20 20 20 20 20 01 80                .€
            00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
            //Example 2:
            00 00 00 00 1f 00 00 00 53 65 61 67 61 74 65 20  ........Seagate
            45 78 74 65 72 6e 61 6c 20 44 72 69 76 65 00 00  External Drive..
            00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
            00 00 00 00 20 20 54 53 31 33 30 32 32 30 41 32  ....  TS130220A2
            20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20
            20 20 20 20 20 20 20 20 20 20 20 20 20 20 10 80                .€
            00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
            //Example 3:
            00 00 02 01 1f 00 00 00 53 61 6d 73 75 6e 67 20  ........Samsung
            53 32 20 50 6f 72 74 61 62 6c 65 00 08 12 00 00  S2 Portable.....
            00 00 00 00 6a 33 33 39 cd cd cd cd cd cd cd cd  ....j339ออออออออ
            cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ออออออออออออออออ
            cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ออออออออออออออออ
            cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ออออออออออออออออ
            */
            memcpy(vendorID, &device->drive_info.scsiVpdData.inquiryData[8], 8);
            remove_Leading_And_Trailing_Whitespace(vendorID);
            if (strcmp(vendorID, "Seagate") == 0)
            {
                char internalModel[41] = { 0 };//this may or may not be useful...
                memcpy(internalModel, &device->drive_info.scsiVpdData.inquiryData[54], 40);
                remove_Leading_And_Trailing_Whitespace(internalModel);
                //this looks like format 2 data, but doesn't report that way...
                memcpy(productID, &device->drive_info.scsiVpdData.inquiryData[16], 16);
                memcpy(revision, &device->drive_info.scsiVpdData.inquiryData[32], 4);
                remove_Leading_And_Trailing_Whitespace(vendorID);
                remove_Leading_And_Trailing_Whitespace(productID);
                if (strcmp(productID, "External Drive") == 0 && strlen(internalModel))//doing strlen of internal model number to catch others of this type with something set here
                {
                    passthroughTypeSet = true;
                    device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_CYPRESS;
                }
            }
            else if (strcmp(vendorID, "Samsung") == 0)
            {
                memcpy(productID, &device->drive_info.scsiVpdData.inquiryData[16], 16);
                memcpy(revision, &device->drive_info.scsiVpdData.inquiryData[36], 4);
                remove_Leading_And_Trailing_Whitespace(vendorID);
                remove_Leading_And_Trailing_Whitespace(productID);
            }
            else
            {
                memcpy(productID, &device->drive_info.scsiVpdData.inquiryData[8], 16);
                memcpy(revision, &device->drive_info.scsiVpdData.inquiryData[32], 4);
                remove_Leading_And_Trailing_Whitespace(productID);
                remove_Leading_And_Trailing_Whitespace(revision);
                if (strcmp(productID, "ST9120826A") == 0)
                {
                    memset(vendorID, 0, 8);
                    passthroughTypeSet = true;
                    device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_CYPRESS;
                }
            }
        }
    }
    return passthroughTypeSet;
}

bool set_ATA_Passthrough_Type_By_Trial_And_Error(tDevice *device)
{
    bool passthroughTypeSet = false;
    if ((device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE))
    {
#if defined (_DEBUG)
        printf("\n\tAttempting to set USB passthrough type with identify commands\n");
#endif
        while (device->drive_info.ata_Options.passthroughType != ATA_PASSTHROUGH_UNKNOWN)
        {
            uint8_t identifyData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == ata_Identify(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
            {
                //command succeeded so this is most likely the correct pass-through type to use for this device
                //setting drive type while we're in here since it could help with a faster scan
                device->drive_info.drive_type = ATA_DRIVE;
                passthroughTypeSet = true;
                break;
            }
            else if (SUCCESS == ata_Identify_Packet_Device(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
            {
                //command succeeded so this is most likely the correct pass-through type to use for this device
                //setting drive type while we're in here since it could help with a faster scan
                device->drive_info.drive_type = ATAPI_DRIVE;
                passthroughTypeSet = true;
                break;
            }
            ++device->drive_info.ata_Options.passthroughType;
        }
    }
    return passthroughTypeSet;
}

bool set_ATA_Passthrough_Type_By_PID_and_VID(tDevice *device)
{
    bool passthroughTypeSet = false;
    //only change the ATA Passthrough type for USB (for legacy USB bridges)
    if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        //Most USB bridges are SAT so they'll probably fall into the default cases and issue an identify command for SAT
        switch (device->drive_info.bridge_info.vendorID)
        {
        case evVendorSeagate:
        case evVendorUSeagate:
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x0501://rev 0002
            case 0x0503:
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_CYPRESS;
                passthroughTypeSet = true;
                break;
            case 0x0502:
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_TI;
                passthroughTypeSet = true;
                break;
            case 0x0888://0BC2 VID
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_NEC;
                passthroughTypeSet = true;
                break;
            default: //unknown
                break;
            }
            break;
        case evVendorOxford:
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x0008:
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughTypeSet = true;
                break;
            default: //unknown
                break;
            }
            break;
        case evVendorJMicron:
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x2339://MiniD2
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_SAT;
                device->drive_info.ata_Options.alwaysSetCheckConditionBit = true;//This device supports the check condition bit on all commands
                passthroughTypeSet = true;
                break;
            default: //unknown
                break;
            }
            break;
        case evVendorDell:
            switch (device->drive_info.bridge_info.productID)
            {
            default: //unknown
                break;
            }
            break;
        case evVendorSamsung:
            switch (device->drive_info.bridge_info.productID)
            {
            default: //unknown
                break;
            }
            break;
        case 0x090C://Silicon Motion
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x1000://Flash Drive - Rev1100
                //Don't set a passthrough type! This is a USB flash memory, that responds to one of the legacy command requests and it will break it!
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughTypeSet = true;
                break;
            default:
                break;
            }
            break;
        case 0x058F://Alcor Micro Corp
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x1234://flash drive
            case 0x6387://flash drive - Rev 0103
            case 0x9380://flash drive
            case 0x9381://flash drive
            case 0x9382://flash drive
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughTypeSet = true;
                break;
            default:
                break;
            }
            break;
        case 0x48D://Integrated Technology Express, Inc.
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x1172://flash drive
            case 0x1176://flash drive - rev 0100
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughTypeSet = true;
                break;
            default:
                break;
            }
            break;
        default: //unknown
            break;
        }
    }
    return passthroughTypeSet;
}

bool bridge_Does_Report_Unit_Serial_Number(tDevice *device)
{
    bool unitSN = false;
    bool checkInqData = true;
    //Add bridge product & vendor IDs to this switch statement.
    //ALSO add comparing for reported inquiry data as well to catch the same device on a system where we don't have this low-level USB HID information
    switch (device->drive_info.bridge_info.vendorID)
    {
    case evVendorUSeagate:
        switch (device->drive_info.bridge_info.productID)
        {
        case 0x2700:
            unitSN = true;
            checkInqData = false;
            break;
        default:
            break;
        }
    default:
        break;
    }
    if (checkInqData)
    {
        //check reported vendor ID and product ID
        if (strcmp(device->drive_info.T10_vendor_ident, "Seagate") == 0)
        {
            if (strcmp(device->drive_info.product_identification, "BlackArmorDAS25") == 0)
            {
                unitSN = true;
            }
        }
    }
    return unitSN;
}

int fill_Drive_Info_USB(tDevice *device)
{
    int           ret = FAILURE;
#ifdef _DEBUG
    printf("%s: -->\n", __FUNCTION__);
#endif
    uint8_t *inq_buf = (uint8_t*)calloc(255, sizeof(uint8_t));
    if (!inq_buf)
    {
        perror("Error allocating memory for standard inquiry data");
        return MEMORY_FAILURE;
    }
    memset(device->drive_info.serialNumber, 0, sizeof(device->drive_info.serialNumber));
    memset(device->drive_info.T10_vendor_ident, 0, sizeof(device->drive_info.T10_vendor_ident));
    memset(device->drive_info.product_identification, 0, sizeof(device->drive_info.product_identification));
    memset(device->drive_info.product_revision, 0, sizeof(device->drive_info.product_revision));
    if (device->drive_info.drive_type != ATAPI_DRIVE && device->drive_info.drive_type != LEGACY_TAPE_DRIVE)
    {
        device->drive_info.drive_type = SCSI_DRIVE;
        device->drive_info.media_type = MEDIA_HDD;
    }
    //now start getting data from the device itself
    if (SUCCESS == scsi_Inquiry(device, inq_buf, 255, 0, false, false))
    {
        bool checkForSAT = true;
        bool readCapacity = true;
        ret = SUCCESS;
        memcpy(device->drive_info.scsiVpdData.inquiryData, inq_buf, 96);//store this in the device structure to make sure it is available elsewhere in the library as well.
        copy_Inquiry_Data(inq_buf, &device->drive_info);
        uint8_t responseFormat = M_GETBITRANGE(inq_buf[3], 3, 0);
        if (responseFormat < 2)
        {
            //Need to check if vendor ID, MN, and FWRev are printable or not
            //vendor ID
            for (uint8_t iter = 0; iter < T10_VENDOR_ID_LEN; ++iter)
            {
                if (!isprint(device->drive_info.T10_vendor_ident[iter]))
                {
                    device->drive_info.T10_vendor_ident[iter] = ' ';
                }
            }
            //product ID
            for (uint8_t iter = 0; iter < MODEL_NUM_LEN && iter < 16; ++iter)//16 is max length in standardized SCSI
            {
                if (!isprint(device->drive_info.product_identification[iter]))
                {
                    device->drive_info.product_identification[iter] = ' ';
                }
            }
            //FWRev
            for (uint8_t iter = 0; iter < FW_REV_LEN && iter < 4; ++iter)//4 is max FWRev length in standardized SCSI
            {
                if (!isprint(device->drive_info.product_revision[iter]))
                {
                    device->drive_info.product_revision[iter] = ' ';
                }
            }
        }
        uint8_t version = inq_buf[2];
        switch (version) //convert some versions since old standards broke the version number into ANSI vs ECMA vs ISO standard numbers
        {
        case 0x81:
            version = SCSI_VERSION_SCSI;//changing to 1 for SCSI
            break;
        case 0x80:
        case 0x82:
            version = SCSI_VERSION_SCSI2;//changing to 2 for SCSI 2
            break;
        case 0x83:
            version = SCSI_VERSION_SPC;//changing to 3 for SPC
            break;
        case 0x84:
            version = SCSI_VERSION_SPC_2;//changing to 4 for SPC2
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
        //set the media type as best we can
        uint8_t peripheralQualifier = (inq_buf[0] & (BIT7 | BIT6 | BIT5)) >> 5;
        uint8_t peripheralDeviceType = inq_buf[0] & (BIT4 | BIT3 | BIT2 | BIT1 | BIT0);
        switch (peripheralDeviceType)
        {
        case PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE:
            device->drive_info.media_type = MEDIA_HDD;//this may not be correct because it may be SSD or USB Flash drive which use this same code
            break;
        case PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE:
            device->drive_info.media_type = MEDIA_HDD;//this may not be correct because it may be SSD or USB Flash drive which use this same code
            device->drive_info.zonedType = ZONED_TYPE_HOST_MANAGED;
            break;
        case PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE:
            device->drive_info.media_type = MEDIA_TAPE;
            checkForSAT = false;
            break;
        case PERIPHERAL_WRITE_ONCE_DEVICE:
        case PERIPHERAL_CD_DVD_DEVICE:
        case PERIPHERAL_OPTICAL_MEMORY_DEVICE:
        case PERIPHERAL_OPTICAL_CARD_READER_WRITER_DEVICE:
            device->drive_info.media_type = MEDIA_OPTICAL;
            checkForSAT = false;
            break;
        case PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE:
            device->drive_info.media_type = MEDIA_HDD;
            checkForSAT = false;
            break;
        case PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE://some USB flash drives show up as this according to the USB mass storage specification...but unfortunately all the ones I've tested show up as Direct Access Block Device just like an HDD :(
            device->drive_info.media_type = MEDIA_SSM_FLASH;
            checkForSAT = false;
            break;
        case PERIPHERAL_ENCLOSURE_SERVICES_DEVICE:
        case PERIPHERAL_BRIDGE_CONTROLLER_COMMANDS:
        case PERIPHERAL_OBJECT_BASED_STORAGE_DEVICE:
        case PERIPHERAL_PRINTER_DEVICE:
        case PERIPHERAL_PROCESSOR_DEVICE:
        case PERIPHERAL_SCANNER_DEVICE:
        case PERIPHERAL_MEDIUM_CHANGER_DEVICE:
        case PERIPHERAL_COMMUNICATIONS_DEVICE:
        case PERIPHERAL_OBSOLETE1:
        case PERIPHERAL_OBSOLETE2:
        case PERIPHERAL_AUTOMATION_DRIVE_INTERFACE:
        case PERIPHERAL_SECURITY_MANAGER_DEVICE:
        case PERIPHERAL_RESERVED3:
        case PERIPHERAL_RESERVED4:
        case PERIPHERAL_RESERVED5:
        case PERIPHERAL_RESERVED6:
        case PERIPHERAL_RESERVED7:
        case PERIPHERAL_RESERVED8:
        case PERIPHERAL_RESERVED9:
        case PERIPHERAL_RESERVED10:
        case PERIPHERAL_RESERVED11:
        case PERIPHERAL_WELL_KNOWN_LOGICAL_UNIT:
        case PERIPHERAL_UNKNOWN_OR_NO_DEVICE_TYPE:
        default:
            readCapacity = false;
            checkForSAT = false;
            device->drive_info.media_type = MEDIA_UNKNOWN;
            break;
        }
        //check for additional bits to try and filter out when to check for SAT
        if (checkForSAT)
        {
            //check that response format is 2 (or higher). SAT spec says the response format should be set to 2
            //Not checking this on USB since some adapters set this purposely to avoid certain commands, BUT DO support SAT
//          if (M_Nibble0(inq_buf[3]) < 2)
//          {
//              checkForSAT = false;
//          }
            //normaca is specified as not compatible, so if it's set, we can definitely skip the SAT check
            if (inq_buf[3] & BIT5)
            {
                checkForSAT = false;
            }
            //sat r09 says mchangr will be set to zero, so we will use this to filter out this device
            if (inq_buf[6] & BIT3)
            {
                checkForSAT = false;
            }
            //Checking to see if any old parallel scsi bits are set. Doing this because there are no known SCSI to PATA adapters that would be SAT compliant and it is unlikely these will be set otherwise
            //if less than version 6 (SPC4) some bits are marked obsolete: addr32, wbus32, ackreqq, trandis
            if (version < 6)
            {
                if (inq_buf[6] & BIT2)//ackreqq
                {
                    checkForSAT = false;
                }
                if (inq_buf[6] & BIT1)//addr32
                {
                    checkForSAT = false;
                }
                if (inq_buf[7] & BIT6)//wbus32
                {
                    checkForSAT = false;
                }
                if (inq_buf[7] & BIT2)//trandis
                {
                    checkForSAT = false;
                }
            }
            if (inq_buf[6] & BIT0)//addr16
            {
                checkForSAT = false;
            }
            if (inq_buf[7] & BIT5)//wbus16
            {
                checkForSAT = false;
            }
            if (inq_buf[7] & BIT4)//sync
            {
                checkForSAT = false;
            }
            if (inq_buf[56] & BIT0)//ius
            {
                checkForSAT = false;
            }
            if (inq_buf[56] & BIT1)//qas
            {
                checkForSAT = false;
            }
            if (M_GETBITRANGE(inq_buf[56], 3, 2) != 0)//clocking
            {
                checkForSAT = false;
            }
            //other bits we may or may not want to check for are multip, aerc, trmtsk, any vendor specific bits, sccs, protect, 3pc
            //each of these are technically not specified in SAT, but are not likely to be suppored anyways.
            //We can add these in overtime if we find them useful for the filter. Most likely, protect and 3pc will be most useful. Not sure about the others, but I doubt many controllers will set them...certainly no USB device will.
            if (inq_buf[6] & BIT5 || inq_buf[7] & BIT0)//vendor specific bits.
            {
                checkForSAT = false;
            }
            //TODO: add in additional bits to skip SAT check as we find them useful
        }

        if (strcmp(device->drive_info.T10_vendor_ident, "NVMe") == 0)
        {
            checkForSAT = false;//DO NOT try SAT passthrough if we find an NVMe device. Some USB adapters reused the opcode for vendor unique passthrough functionality and when an ATA identify is issued, can hang the bridge.
        }

        //do we want to check the version descriptors here too? There are a lot of those...I have a table that parses them to human readable, but not setting anything yet...may need to use that later

        if (M_Word0(device->dFlags) == DO_NOT_WAKE_DRIVE)
        {
#if defined (_DEBUG)
            printf("Quiting device discovery early per DO_NOT_WAKE_DRIVE\n");
#endif
            //We actually need to try issuing an ATA/ATAPI identify to the drive to set the drive type...but I'm going to try and ONLY do it for ATA drives with the if statement below...it should catch almost all cases (which is good enough for now)
            if (checkForSAT && device->drive_info.drive_type != ATAPI_DRIVE && device->drive_info.drive_type != LEGACY_TAPE_DRIVE)
            {
                ret = fill_In_ATA_Drive_Info(device);
            }
            return ret;
        }

        if (M_Word0(device->dFlags) == FAST_SCAN)
        {
            if (version >= 2 || bridge_Does_Report_Unit_Serial_Number(device))//unit serial number added in SCSI2
            {
                //I'm reading only the unit serial number page here for a quick scan and the device information page for WWN - TJE
                uint8_t unitSerialNumberPageLength = SERIAL_NUM_LEN + 4;//adding 4 bytes extra for the header
                uint8_t *unitSerialNumber = (uint8_t*)calloc(unitSerialNumberPageLength, sizeof(uint8_t));
                if (!unitSerialNumber)
                {
                    perror("Error allocating memory to read the unit serial number");
                    return MEMORY_FAILURE;
                }
                if (SUCCESS == scsi_Inquiry(device, unitSerialNumber, unitSerialNumberPageLength, UNIT_SERIAL_NUMBER, true, false))
                {
                    if (unitSerialNumber[1] == UNIT_SERIAL_NUMBER)//make sure we actually got the right page and not bogus data.
                    {
                        uint16_t serialNumberLength = M_BytesTo2ByteValue(unitSerialNumber[2], unitSerialNumber[3]);
                        if (serialNumberLength > 0)
                        {
                            memcpy(&device->drive_info.serialNumber[0], &unitSerialNumber[4], M_Min(SERIAL_NUM_LEN, serialNumberLength));
                            device->drive_info.serialNumber[M_Min(SERIAL_NUM_LEN, serialNumberLength)] = '\0';
                            remove_Leading_And_Trailing_Whitespace(device->drive_info.serialNumber);
                            for (uint8_t iter = 0; iter < SERIAL_NUM_LEN; ++iter)
                            {
                                if (!isprint(device->drive_info.serialNumber[iter]))
                                {
                                    device->drive_info.serialNumber[iter] = ' ';
                                }
                            }
                        }
                        else
                        {
                            memset(device->drive_info.serialNumber, 0, SERIAL_NUM_LEN);
                        }
                    }
                    else
                    {
                        memset(device->drive_info.serialNumber, 0, SERIAL_NUM_LEN);
                    }
                }
                safe_Free(unitSerialNumber);
            }
            else
            {
                //SN may not be available...just going to read where it may otherwise show up in inquiry data like some vendors like to put it
                memcpy(&device->drive_info.serialNumber[0], &inq_buf[36], SERIAL_NUM_LEN);
                device->drive_info.serialNumber[SERIAL_NUM_LEN] = '\0';
                //make sure the SN is printable if it's coming from here since it's non-standardized
                for (uint8_t iter = 0; iter < SERIAL_NUM_LEN; ++iter)
                {
                    if (!isprint(device->drive_info.serialNumber[iter]))
                    {
                        device->drive_info.serialNumber[iter] = ' ';
                    }
                }
            }
            if (version >= 3)//device identification added in SPC
            {
                uint8_t *deviceIdentification = (uint8_t*)calloc(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t));
                if (!deviceIdentification)
                {
                    perror("Error allocating memory to read device identification VPD page");
                    return MEMORY_FAILURE;
                }
                if (SUCCESS == scsi_Inquiry(device, deviceIdentification, INQ_RETURN_DATA_LENGTH, DEVICE_IDENTIFICATION, true, false))
                {
                    if (deviceIdentification[1] == DEVICE_IDENTIFICATION)//check the page number
                    {
                        //this SHOULD work for getting a WWN 90% of the time, but if it doesn't, then we will need to go through the descriptors from the device and set it from the correct one. See the SATChecker util code for how to do this
                        memcpy(&device->drive_info.worldWideName, &deviceIdentification[8], 8);
                        byte_Swap_64(&device->drive_info.worldWideName);
                    }
                }
                safe_Free(deviceIdentification);
            }
            //One last thing...Need to do a SAT scan...
            if (checkForSAT)
            {
                check_SAT_Compliance_And_Set_Drive_Type(device);
            }
            return ret;
        }

        bool satVPDPageRead = false;
        bool satComplianceChecked = false;
        if (version >= 2 || bridge_Does_Report_Unit_Serial_Number(device))//SCSI 2 added VPD pages...some USB drives will report this despite the version number
        {
            //from here on we need to check if a VPD page is supported and read it if there is anything in it that we care about to store info in the device struct
            memset(inq_buf, 0, INQ_RETURN_DATA_LENGTH);
            bool dummyUpVPDSupport = false;
            if (SUCCESS != scsi_Inquiry(device, inq_buf, INQ_RETURN_DATA_LENGTH, SUPPORTED_VPD_PAGES, true, false))
            {
                //for whatever reason, this device didn't return support for the list of supported pages, so set a flag telling us to dummy up a list so that we can still attempt to issue commands to pages we do need to try and get (this is a workaround for some really stupid USB bridges)
                dummyUpVPDSupport = true;
                if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
            }
            else if (inq_buf[1] != SUPPORTED_VPD_PAGES)
            {
                //did not get the list of supported pages! Checking this since occasionally we get back garbage
                memset(inq_buf, 0, INQ_RETURN_DATA_LENGTH);
            }
            if (dummyUpVPDSupport == false)
            {
                uint8_t zeroedMem[INQ_RETURN_DATA_LENGTH] = { 0 };
                if (memcmp(inq_buf, zeroedMem, INQ_RETURN_DATA_LENGTH) == 0)
                {
                    //this case means that the command was successful, but we got nothing but zeros....which happens on some craptastic USB bridges
                    dummyUpVPDSupport = true;
                }
            }
            if (dummyUpVPDSupport)
            {
                uint16_t offset = 4;//start of pages to dummy up
                                    //in here we will set up a fake supported VPD pages buffer so that we try to read the unit serial number page, the SAT page, and device identification page
                inq_buf[0] = peripheralQualifier << 5;
                inq_buf[0] |= peripheralDeviceType;
                //set page code
                inq_buf[1] = 0x00;

                //now each byte will reference a supported VPD page we want to dummy up. These should be in ascending order
                inq_buf[offset] = SUPPORTED_VPD_PAGES;
                ++offset;
                inq_buf[offset] = UNIT_SERIAL_NUMBER;
                ++offset;
                if (version >= 3)//SPC
                {
                    inq_buf[offset] = DEVICE_IDENTIFICATION;
                    ++offset;
                }
                if (checkForSAT)
                {
                    inq_buf[offset] = ATA_INFORMATION;
                    ++offset;
                }
                if (version >= 3)//SPC
                {
                    if (peripheralDeviceType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE || peripheralDeviceType == PERIPHERAL_SIMPLIFIED_DIRECT_ACCESS_DEVICE || peripheralDeviceType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE)
                    {
                        inq_buf[offset] = BLOCK_DEVICE_CHARACTERISTICS;
                        ++offset;
                    }
                }
                //TODO: Add more pages to the dummy information as we need to. This may be useful to do in the future in case a device decides not to support a MANDATORY page or another page we care about

                //set page length (n-3)
                inq_buf[2] = M_Byte1(offset - 4);//msb
                inq_buf[3] = M_Byte0(offset - 4);//lsb
            }
            //first, get the length of the supported pages
            uint16_t supportedVPDPagesLength = M_BytesTo2ByteValue(inq_buf[2], inq_buf[3]);
            uint8_t *supportedVPDPages = (uint8_t*)calloc(supportedVPDPagesLength, sizeof(uint8_t));
            if (!supportedVPDPages)
            {
                perror("Error allocating memory for supported VPD pages!\n");
                return MEMORY_FAILURE;
            }
            memcpy(supportedVPDPages, &inq_buf[4], supportedVPDPagesLength);
            //now loop through and read pages as we need to, only reading the pages that we care about
            uint16_t vpdIter = 0;
            for (vpdIter = 0; vpdIter < supportedVPDPagesLength; vpdIter++)
            {
                switch (supportedVPDPages[vpdIter])
                {
                case UNIT_SERIAL_NUMBER://Device serial number (only grab 20 characters worth since that's what we need for the device struct)
                {
                    uint8_t unitSerialNumberPageLength = SERIAL_NUM_LEN + 4;//adding 4 bytes extra for the header
                    uint8_t *unitSerialNumber = (uint8_t*)calloc(unitSerialNumberPageLength, sizeof(uint8_t));
                    if (!unitSerialNumber)
                    {
                        perror("Error allocating memory to read the unit serial number");
                        continue;//continue the loop
                    }
                    if (SUCCESS == scsi_Inquiry(device, unitSerialNumber, unitSerialNumberPageLength, supportedVPDPages[vpdIter], true, false))
                    {
                        if (unitSerialNumber[1] == UNIT_SERIAL_NUMBER)//check the page code to make sure we got the right thing
                        {
                            uint16_t serialNumberLength = M_BytesTo2ByteValue(unitSerialNumber[2], unitSerialNumber[3]);
                            if (serialNumberLength > 0)
                            {
                                memcpy(&device->drive_info.serialNumber[0], &unitSerialNumber[4], M_Min(SERIAL_NUM_LEN, serialNumberLength));
                                device->drive_info.serialNumber[M_Min(SERIAL_NUM_LEN, serialNumberLength)] = '\0';
                                remove_Leading_And_Trailing_Whitespace(device->drive_info.serialNumber);
                                for (uint8_t iter = 0; iter < SERIAL_NUM_LEN; ++iter)
                                {
                                    if (!isprint(device->drive_info.serialNumber[iter]))
                                    {
                                        device->drive_info.serialNumber[iter] = ' ';
                                    }
                                }
                            }
                            else
                            {

                            }
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(unitSerialNumber);
                    break;
                }
                case DEVICE_IDENTIFICATION://World wide name
                {
                    uint8_t *deviceIdentification = (uint8_t*)calloc(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t));
                    if (!deviceIdentification)
                    {
                        perror("Error allocating memory to read device identification VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, deviceIdentification, INQ_RETURN_DATA_LENGTH, DEVICE_IDENTIFICATION, true, false))
                    {
                        if (deviceIdentification[1] == DEVICE_IDENTIFICATION)
                        {
                            //this SHOULD work for getting a WWN 90% of the time, but if it doesn't, then we will need to go through the descriptors from the device and set it from the correct one. See the SATChecker util code for how to do this
                            memcpy(&device->drive_info.worldWideName, &deviceIdentification[8], 8);
                            byte_Swap_64(&device->drive_info.worldWideName);
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(deviceIdentification);
                    break;
                }
                case ATA_INFORMATION: //use this to determine if it's SAT compliant
                {
                    if (SUCCESS == check_SAT_Compliance_And_Set_Drive_Type(device))
                    {
                        satVPDPageRead = true;
                    }
                    else
                    {
                        //send test unit ready to get the device responding again (For better performance on some USB devices that don't support this page)
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    satComplianceChecked = true;
                    break;
                }
                case BLOCK_DEVICE_CHARACTERISTICS: //use this to determine if it's SSD or HDD and whether it's a HDD or not
                {
                    uint8_t *blockDeviceCharacteristics = (uint8_t*)calloc(VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, sizeof(uint8_t));
                    if (!blockDeviceCharacteristics)
                    {
                        perror("Error allocating memory to read block device characteistics VPD page");
                        continue;
                    }
                    if (SUCCESS == scsi_Inquiry(device, blockDeviceCharacteristics, VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, BLOCK_DEVICE_CHARACTERISTICS, true, false))
                    {
                        if (blockDeviceCharacteristics[1] == BLOCK_DEVICE_CHARACTERISTICS)
                        {
                            uint16_t mediumRotationRate = M_BytesTo2ByteValue(blockDeviceCharacteristics[4], blockDeviceCharacteristics[5]);
                            uint8_t productType = blockDeviceCharacteristics[6];
                            if (mediumRotationRate == 0x0001)
                            {
                                if (!satVPDPageRead)
                                {
                                    device->drive_info.media_type = MEDIA_SSD;
                                }
                            }
                            else if (mediumRotationRate >= 0x401 && mediumRotationRate <= 0xFFFE)
                            {
                                if (!satVPDPageRead)
                                {
                                    device->drive_info.media_type = MEDIA_HDD;
                                }
                            }
                            else
                            {
                                if (!satVPDPageRead)
                                {
                                    device->drive_info.media_type = MEDIA_UNKNOWN;
                                }
                            }
                            switch (productType)
                            {
                            case 0x01://CFAST
                            case 0x02://compact flash
                            case 0x03://Memory Stick
                            case 0x04://MultiMediaCard
                            case 0x05://SecureDigitalCard
                            case 0x06://XQD
                            case 0x07://Universal Flash Storage
                                if (!satVPDPageRead)
                                {
                                    device->drive_info.media_type = MEDIA_SSM_FLASH;
                                }
                                break;
                            default://not indicated or reserved or vendor unique so do nothing
                                break;
                            }
                            //get zoned information (as long as it isn't already set from SAT passthrough)
                            if (device->drive_info.zonedType == ZONED_TYPE_NOT_ZONED)
                            {
                                switch ((blockDeviceCharacteristics[8] & 0x30) >> 4)
                                {
                                case 0:
                                    device->drive_info.zonedType = ZONED_TYPE_NOT_ZONED;
                                    break;
                                case 1:
                                    device->drive_info.zonedType = ZONED_TYPE_HOST_AWARE;
                                    break;
                                case 2:
                                    device->drive_info.zonedType = ZONED_TYPE_DEVICE_MANAGED;
                                    break;
                                case 3:
                                    device->drive_info.zonedType = ZONED_TYPE_RESERVED;
                                    break;
                                default:
                                    break;
                                }
                            }
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                    safe_Free(blockDeviceCharacteristics);
                    break;
                }
                default:
                    //do nothing, we don't care about reading this page (at least not right now)
                    break;
                }
            }
            safe_Free(supportedVPDPages);
        }
        else
        {
            //SN may not be available...just going to read where it may otherwise show up in inquiry data like some vendors like to put it
            memcpy(&device->drive_info.serialNumber[0], &inq_buf[36], SERIAL_NUM_LEN);
            device->drive_info.serialNumber[SERIAL_NUM_LEN] = '\0';
            //make sure the SN is printable if it's coming from here since it's non-standardized
            for (uint8_t iter = 0; iter < SERIAL_NUM_LEN; ++iter)
            {
                if (!is_ASCII(device->drive_info.serialNumber[iter]) || !isprint(device->drive_info.serialNumber[iter]))
                {
                    device->drive_info.serialNumber[iter] = ' ';
                }
            }
        }

        if (readCapacity)
        {
            //if inquiry says SPC or lower (3), then only do read capacity 10
            //Anything else can have read capacity 16 command available

            //send a read capacity command to get the device's logical block size...read capacity 10 should be enough for this
            uint8_t *readCapBuf = (uint8_t*)calloc(READ_CAPACITY_10_LEN, sizeof(uint8_t));
            if (!readCapBuf)
            {
                safe_Free(inq_buf);
                return MEMORY_FAILURE;
            }
            if (SUCCESS == scsi_Read_Capacity_10(device, readCapBuf, READ_CAPACITY_10_LEN))
            {
                copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &device->drive_info.deviceMaxLba, &device->drive_info.sectorAlignment, readCapBuf, false);
                if (version > 3)//SPC2 and higher can reference SBC2 and higher which introduced read capacity 16
                {
                    //try a read capacity 16 anyways and see if the data from that was valid or not since that will give us a physical sector size whereas readcap10 data will not
                    uint8_t* temp = (uint8_t*)realloc(readCapBuf, READ_CAPACITY_16_LEN * sizeof(uint8_t));
                    if (!temp)
                    {
                        safe_Free(inq_buf);
                        return MEMORY_FAILURE;
                    }
                    readCapBuf = temp;
                    memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
                    if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
                    {
                        uint32_t logicalBlockSize = 0;
                        uint32_t physicalBlockSize = 0;
                        uint64_t maxLBA = 0;
                        uint16_t sectorAlignment = 0;
                        copy_Read_Capacity_Info(&logicalBlockSize, &physicalBlockSize, &maxLBA, &sectorAlignment, readCapBuf, true);
                        //some USB drives will return success and no data, so check if this local var is 0 or not...if not, we can use this data
                        if (maxLBA != 0)
                        {
                            device->drive_info.deviceBlockSize = logicalBlockSize;
                            device->drive_info.devicePhyBlockSize = physicalBlockSize;
                            device->drive_info.deviceMaxLba = maxLBA;
                            device->drive_info.sectorAlignment = sectorAlignment;
                        }
                        device->drive_info.currentProtectionType = 0;
                        device->drive_info.piExponent = M_GETBITRANGE(readCapBuf[13], 7, 4);
                        if (readCapBuf[12] & BIT0)
                        {
                            device->drive_info.currentProtectionType = M_GETBITRANGE(readCapBuf[12], 3, 1) + 1;
                        }
                    }
                    else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                    {
                        //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                        scsi_Test_Unit_Ready(device, NULL);
                    }
                }
            }
            else
            {
                //try read capacity 16, if that fails we are done trying
                uint8_t* temp = (uint8_t*)realloc(readCapBuf, READ_CAPACITY_16_LEN * sizeof(uint8_t));
                if (temp == NULL)
                {
                    safe_Free(inq_buf);
                    return MEMORY_FAILURE;
                }
                readCapBuf = temp;
                memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
                //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                scsi_Test_Unit_Ready(device, NULL);
                if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
                {
                    copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &device->drive_info.deviceMaxLba, &device->drive_info.sectorAlignment, readCapBuf, true);
                    device->drive_info.currentProtectionType = 0;
                    device->drive_info.piExponent = M_GETBITRANGE(readCapBuf[13], 7, 4);
                    if (readCapBuf[12] & BIT0)
                    {
                        device->drive_info.currentProtectionType = M_GETBITRANGE(readCapBuf[12], 3, 1) + 1;
                    }
                }
                else if (device->drive_info.interface_type != SCSI_INTERFACE && device->drive_info.interface_type != IDE_INTERFACE) //TODO: add other interfaces here to filter out when we send a TUR
                {
                    //Send a test unit ready to clear the error from failure to read this page. This is done mostly for USB interfaces that don't handle errors from commands well.
                    scsi_Test_Unit_Ready(device, NULL);
                }
            }
            safe_Free(readCapBuf);
            if (device->drive_info.devicePhyBlockSize == 0)
            {
                //If we did not get a physical blocksize, we need to set it to the blocksize (logical).
                //This will help with old devices or those that don't support the read capacity 16 command or return other weird invalid data.
                device->drive_info.devicePhyBlockSize = device->drive_info.deviceBlockSize;
            }
        }

        //if we haven't already, check the device for SAT support. Allow this to run on IDE interface since we'll just issue a SAT identify in here to set things up...might reduce multiple commands later
        if (checkForSAT && !satVPDPageRead && !satComplianceChecked)
        {
            check_SAT_Compliance_And_Set_Drive_Type(device);
        }
    }
    else
    {
        if (VERBOSITY_DEFAULT < device->deviceVerbosity)
        {
            printf("Getting Standard Inquiry Data Failed\n");
        }
        ret = COMMAND_FAILURE;
    }
    safe_Free(inq_buf);

#ifdef _DEBUG
    printf("\nusb hacks\n");
    printf("Drive type: %d\n", device->drive_info.drive_type);
    printf("Interface type: %d\n", device->drive_info.interface_type);
    printf("Media type: %d\n", device->drive_info.media_type);
    printf("%s: <--\n", __FUNCTION__);
#endif
    return ret;
}

bool sct_With_SMART_Commands(tDevice *device)
{
    bool sctWithSMART = false;
    if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        bool setSupported = false;
        //TODO: add in setting via VID/PID
        switch (device->drive_info.bridge_info.vendorID)
        {
        default:
            break;
        }
        if (!setSupported)
        {
            if (strcmp("S2 Portable", device->drive_info.product_identification) == 0)
            {
                //For whatever reason, reading this log on this device breaks the bridge and it has to be completely removed from the system before the system responds again.
                sctWithSMART = true;
            }
        }
    }
    return sctWithSMART;
}

bool supports_ATA_Return_SMART_Status_Command(tDevice *device)
{
    bool supported = true;
    if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        bool setSupported = false;
        //TODO: add in setting via VID/PID
        switch (device->drive_info.bridge_info.vendorID)
        {
        default:
            break;
        }
        if (!setSupported)
        {
            if (strcmp("S2 Portable", device->drive_info.product_identification) == 0)
            {
                supported = false;
            }
        }
    }
    return supported;
}