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
// \file sntl_helper.c
// \brief Defines the function headers to help with SCSI to NVMe translation

#if !defined (DISABLE_NVME_PASSTHROUGH)

#include "sntl_helper.h"
#include "scsi_helper.h"
#include "nvme_helper.h"
#include "nvme_helper_func.h"

//This file is written based on what is described in the SCSI to NVMe translation white paper (SNTL).
//Some things that are clearly wrong (incorrect bit or offset, etc) are fixed as this was written. Most of these are noted in comments.
//The plan is to expand this beyond what is provided in the white paper for other translations. These will be wrapped in a #define for SNTL_EXT
//The whitepaper was originally created to help cover transitions in some environments from SCSI to native NVMe at a driver level.
//Most operating systems quickly implemented a driver and this is not necessary.
//This was put into this code base to help adapt for places that are treating NVMe as SCSI.
//It will probably not be used much, but it is educational to see how the translation works.
//The biggest thing missing from the SNTL whitepaper is a way to issue pass-through commands. Any implementation of this should use a vendor unique opertion code
//  so as to not confuse other OS's or software into thinking it's some other type of device (such as an ATA device behind a SATL)

//Translations not yet complete (per whitepaper):
// -returning all mode pages + subpages
// -returning all sub pages of a particular mode page
// -compare and write. This translation is not possible without the ability to issue fused commands, which is not currently possible.
// -format unit translation. Needs the ability to save changes in a block descriptor. Recommend implementing a mode page 0 that is empty for this (use page format, set first 4 bytes to "SNTL" as a signature.
// -need clarification on translation of SCSI verify with bytechk set to zero

#define SNTL_EXT
//SNTL_EXT is used to enable extensions beyond the SNTL spec...which we want since it's pretty out of date and we might as well add everything we can
//Extention translations not yet complete:
// - DST
// - Sanitize
// - mode page policy VPD page
// - nvme passthrough command (needs to handle admin vs nvm, nondata, data-in, data-out, bidirectional transfers and vendor unique commands
// - read buffer command to return the NVMe telemetry log (similar to SAT translation to return current internal status log)
// - supported sector sizes log pages

void sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(uint8_t data[8], bool cd, bool bpv, uint8_t bitPointer, uint16_t fieldPointer)
{
    if (data)
    {
        data[0] = 0x02;
        data[1] = 0x06;
        data[2] = RESERVED;
        data[3] = RESERVED;
        data[4] = M_GETBITRANGE(bitPointer, 2, 0);
        data[4] |= BIT7;//if this function is being called, then this bit is assumed to be set
        if (cd)
        {
            data[4] |= BIT6;
        }
        if (bpv)
        {
            data[4] |= BIT3;
        }
        data[5] = M_Byte1(fieldPointer);
        data[6] = M_Byte0(fieldPointer);
        data[7] = RESERVED;
    }
}

void sntl_Set_Sense_Key_Specific_Descriptor_Progress_Indicator(uint8_t data[8], uint16_t progressValue)
{
    if (data)
    {
        data[0] = 0x02;
        data[1] = 0x06;
        data[2] = RESERVED;
        data[3] = RESERVED;
        data[4] = RESERVED;
        data[4] |= BIT7;//if this function is being called, then this bit is assumed to be set
        data[5] = M_Byte1(progressValue);
        data[6] = M_Byte0(progressValue);
        data[7] = RESERVED;
    }
}

void sntl_Set_Sense_Data_For_Translation(uint8_t *sensePtr, uint32_t senseDataLength, uint8_t senseKey, uint8_t asc, uint8_t ascq, bool descriptorFormat, uint8_t *descriptor, uint8_t descriptorCount /* this will probably only be 1, but up to 2 or 3 max */)
{
    uint8_t senseData[SPC3_SENSE_LEN] = { 0 };
    uint8_t additionalSenseLength = 0;
    if (descriptorFormat)
    {
        senseData[0] = 0x72;
        //sense key
        senseData[1] |= M_Nibble0(senseKey);
        //asc
        senseData[2] = asc;
        //ascq
        senseData[3] = ascq;
        if (descriptor)
        {
            //loop through descriptor copying each one to the sense data buffer
            uint8_t senseDataOffset = 8, descriptorLength = 0, counter = 0;
            uint32_t descriptorOffset = 0;
            while (counter < descriptorCount)
            {
                descriptorLength = descriptor[descriptorOffset + 1] + 1;
                memcpy(&senseData[senseDataOffset], descriptor, descriptorLength);
                additionalSenseLength += descriptorLength;
                ++counter;
                descriptorOffset += descriptorLength;
                senseDataOffset += descriptorLength;
                if (descriptor[descriptorOffset] == 9)
                {
                    descriptorOffset += 1;//adding 1 since we put the log index in one extra byte, which is non standard and not part of the actual descriptor. This handles going on to the next descriptor in the list properly. We don't return this byte in the descriptor anyways
                }
            }
        }
        //set the additional sense length
        senseData[7] = additionalSenseLength;
    }
    else
    {
        senseData[0] = 0x70;
        //sense key
        senseData[2] |= M_Nibble0(senseKey);
        //asc
        senseData[12] = asc;
        //ascq
        senseData[13] = ascq;
        //additional sense length
        additionalSenseLength = 10;
        senseData[7] = additionalSenseLength;
        if (descriptor)
        {
            uint8_t senseDataOffset = 8, descriptorLength = 0, counter = 0;
            uint32_t descriptorOffset = 0;
            while (counter < descriptorCount)
            {
                uint8_t descriptorType = descriptor[0];
                descriptorLength = descriptor[descriptorCount] + 1;
                switch (descriptorType)
                {
                case 0://information
                {
                    if (descriptor[descriptorOffset + 2] & BIT7)
                    {
                        senseData[0] |= BIT7;//set the valid bit
                    }
                    uint64_t descriptorInformation = M_BytesTo8ByteValue(descriptor[descriptorOffset + 4], descriptor[descriptorOffset + 5], descriptor[descriptorOffset + 6], descriptor[descriptorOffset + 7], descriptor[descriptorOffset + 8], descriptor[descriptorOffset + 9], descriptor[descriptorOffset + 10], descriptor[descriptorOffset + 11]);
                    if (descriptorInformation > UINT32_MAX)
                    {
                        memset(&senseData[3], UINT8_MAX, 4);
                    }
                    else
                    {
                        //copy lowest 4 bytes
                        memcpy(&senseData[3], &descriptor[descriptorOffset + 8], 4);
                    }
                }
                break;
                case 1://command specific information
                {
                    uint64_t descriptorCmdInformation = M_BytesTo8ByteValue(descriptor[descriptorOffset + 4], descriptor[descriptorOffset + 5], descriptor[descriptorOffset + 6], descriptor[descriptorOffset + 7], descriptor[descriptorOffset + 8], descriptor[descriptorOffset + 9], descriptor[descriptorOffset + 10], descriptor[descriptorOffset + 11]);
                    if (descriptorCmdInformation > UINT32_MAX)
                    {
                        memset(&senseData[8], UINT8_MAX, 4);
                    }
                    else
                    {
                        //copy lowest 4 bytes
                        memcpy(&senseData[8], &descriptor[descriptorOffset + 8], 4);
                    }
                }
                break;
                case 2://sense key specific
                    //bytes 4, 5 , and 6
                    memcpy(&senseData[15], &descriptor[descriptorOffset + 4], 3);
                    break;
                case 3://FRU
                    senseData[14] = descriptor[descriptorOffset + 3];
                    break;
                case 4://Stream Commands
                    if (descriptor[descriptorOffset + 3] & BIT7)
                    {
                        //set filemark bit
                        senseData[2] |= BIT7;
                    }
                    if (descriptor[descriptorOffset + 3] & BIT6)
                    {
                        //set end of media bit
                        senseData[2] |= BIT6;
                    }
                    if (descriptor[descriptorOffset + 3] & BIT5)
                    {
                        //set illegal length indicator bit
                        senseData[2] |= BIT5;
                    }
                    break;
                case 5://Block Commands
                    if (descriptor[descriptorOffset + 3] & BIT5)
                    {
                        //set illegal length indicator bit
                        senseData[2] |= BIT5;
                    }
                    break;
                case 6://OSD Object Identification
                case 7://OSD Response Integrity Value
                case 8://OSD Attribute identification
                    //can't handle this type
                    break;
                case 9://ATA Status return
                    senseData[0] |= BIT7;//set the valid bit since we are filling in the information field
                    //parse out the registers as best we can
                    //information offsets bytes 3-7
                    senseData[3] = descriptor[descriptorOffset + 3];//error
                    senseData[4] = descriptor[descriptorOffset + 13];//status
                    senseData[5] = descriptor[descriptorOffset + 12];//device
                    senseData[6] = descriptor[descriptorOffset + 5];//count 7:0
                    //command specific information bytes 8-11
                    if (descriptor[descriptorOffset + 2] & BIT0)
                    {
                        //extend bit set from issuing an extended command
                        senseData[8] |= BIT7;
                    }
                    if (descriptor[descriptorOffset + 10] || descriptor[descriptorOffset + 8] || descriptor[descriptorOffset + 6])
                    {
                        //set uppder LBA non-zero bit
                        senseData[8] |= BIT5;
                    }
                    if (descriptor[descriptorOffset + 4])
                    {
                        //set sector count 15:8 non-zero bit
                        senseData[8] |= BIT6;
                    }
                    senseData[8] |= M_Nibble0(descriptor[descriptorOffset + 14]);//setting the log index...this offset is nonstandard, so we will need to increment the offset one before leaving this case
                    senseData[9] = descriptor[descriptorOffset + 7];//lba
                    senseData[10] = descriptor[descriptorOffset + 9];//lba
                    senseData[11] = descriptor[descriptorOffset + 11];//lba
                    descriptorOffset += 1;//setting this since we added the log index in the last byte of this descriptor to make this easier to pass around
                    break;
                case 10://Another Progress Indication
                    //cannot handle this in fixed format
                    break;
                case 11://User Data Segment Referral
                    //cannot handle this in fixed format
                    break;
                case 12://Forwarded Sense data
                    //cannot handle this in fixed format
                    break;
                case 13://Direct Access Block Device
                    if (descriptor[descriptorOffset + 2] & BIT7)
                    {
                        senseData[0] |= BIT7;//set the valid bit
                    }
                    if (descriptor[descriptorOffset + 2] & BIT5)
                    {
                        //set illegal length indicator bit
                        senseData[2] |= BIT5;
                    }
                    //bytes 4, 5 , and 6 for sense key specific information
                    memcpy(&senseData[15], &descriptor[descriptorOffset + 4], 3);
                    //fru code
                    senseData[14] = descriptor[descriptorOffset + 7];
                    {
                        //information
                        uint64_t descriptorInformation = M_BytesTo8ByteValue(descriptor[descriptorOffset + 8], descriptor[descriptorOffset + 9], descriptor[descriptorOffset + 10], descriptor[descriptorOffset + 11], descriptor[descriptorOffset + 12], descriptor[descriptorOffset + 13], descriptor[descriptorOffset + 14], descriptor[descriptorOffset + 15]);
                        if (descriptorInformation > UINT32_MAX)
                        {
                            memset(&senseData[3], UINT8_MAX, 4);
                        }
                        else
                        {
                            //copy lowest 4 bytes
                            memcpy(&senseData[3], &descriptor[descriptorOffset + 12], 4);
                        }
                        //command specific information
                        uint64_t descriptorCmdInformation = M_BytesTo8ByteValue(descriptor[descriptorOffset + 16], descriptor[descriptorOffset + 17], descriptor[descriptorOffset + 18], descriptor[descriptorOffset + 19], descriptor[descriptorOffset + 20], descriptor[descriptorOffset + 21], descriptor[descriptorOffset + 22], descriptor[descriptorOffset + 23]);
                        if (descriptorCmdInformation > UINT32_MAX)
                        {
                            memset(&senseData[8], UINT8_MAX, 4);
                        }
                        else
                        {
                            //copy lowest 4 bytes
                            memcpy(&senseData[8], &descriptor[descriptorOffset + 20], 4);
                        }
                    }
                    break;
                default://unsupported, break
                    //can't handle this type
                    break;
                }
                ++counter;
                descriptorOffset += descriptorLength;
                senseDataOffset += descriptorLength;
            }
        }
    }
    if (sensePtr)
    {
        memcpy(sensePtr, senseData, senseDataLength);
    }
}

void set_Sense_Data_By_Generic_NVMe_Status(tDevice *device, uint8_t nvmeStatus, uint8_t *sensePtr, uint32_t senseDataLength, bool doNotRetry)
{
    //first check if sense data reporting is supported
    uint8_t senseKey = 0, asc = 0, ascq = 0;
    bool returnSenseKeySpecificInfo = false;
    uint8_t informationSenseDescriptor[12] = { 0 };

    //make sure these are cleared out still (compiler should optimize this away if this is redundant)
    senseKey = 0;
    asc = 0;
    ascq = 0;

    bool genericCatchAllSense = false;
    
    //generic translations
    switch (nvmeStatus)
    {
    case 0://success
        break;
    case 1://invalid command opcode
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //invalid command operation code
        asc = 0x20;
        ascq = 0x00;
        break;
    case 2://invalid field in command
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //invalid field in cdb
        asc = 0x24;
        ascq = 0x00;
        break;
    case 3://command ID conflict
        genericCatchAllSense = true;
        break;
    case 4://data transfer error
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //no additional sense
        asc = 0x00;
        ascq = 0x00;
        break;
    case 5://commands aborted due to power loss notification
        senseKey = SENSE_KEY_ABORTED_COMMAND;
        //warning - power loss expected
        asc = 0x0B;
        ascq = 0x08;
        break;
    case 6://internal error
        senseKey = SENSE_KEY_HARDWARE_ERROR;
        //internal target failure
        asc = 0x44;
        ascq = 0;
        break;
    case 7://command abort requested
    case 8://command aborted due to sq deletion
    case 9://command aborted due to failed fused command
    case 0xA://command aborted due to missing fused command
        senseKey = SENSE_KEY_ABORTED_COMMAND;
        //no additional sense
        asc = 0x00;
        ascq = 0x00;
        break;
    case 0xB://invalid namespace or format
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //access denied - invalid LU identifier
        asc = 0x00;
        ascq = 0x00;
        break;
    case 0xC://command sequence error
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //access denied - invalid LU identifier
        asc = 0x2C;
        ascq = 0x00;
        break;
    case 0xD://invalid SDL segment descriptor
    case 0xE://invalid number of SGL descriptors
    case 0xF://data sgl length invalid
    case 0x10://metadata SGL length invalid
    case 0x11://SGL descriptor Type invalid
    case 0x12://invalid use of controller memory buffer
    case 0x13://PRP offset invalid
    case 0x14://atomic write unit exceeded
    case 0x15://operation denied
    case 0x16://SGL offset invalid
    case 0x17://reserved
    case 0x18://host identified inconsistent format
    case 0x19://keep alive timeout expired
    case 0x1A://keep alive timeout invalid
    case 0x1B://command aborted due to preempt and abort
        genericCatchAllSense = true;
        break;
    case 0x1C://sanitize failed
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        asc = 0x31;
        ascq = 0x03;
        break;
    case 0x1D://sanitize in progress
        senseKey = SENSE_KEY_NOT_READY;
        asc = 0x04;
        ascq = 0x1B;
        break;
    case 0x1E://SGL data block granularity invalid
    case 0x1F://command not supported for queue in CMB
        //TODO: Need something to handle these....
        genericCatchAllSense = true;
        break;
        break;
        //80-BFh are I/O command set specific
    case 0x80://LBA out of range
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //logical block address out of range
        asc = 0x21;
        ascq = 0x00;
        break;
    case 0x81://capacity exceeded
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //no additional information
        asc = 0x00;
        ascq = 0x00;
        break;
    case 0x82://namespace not ready
        if (doNotRetry)
        {
            senseKey = SENSE_KEY_NOT_READY;
            //LOGICAL UNIT NOT READY, CAUSE NOT REPORTABLE
            asc = 0x04;
            ascq = 0x00;
        }
        else
        {
            senseKey = SENSE_KEY_NOT_READY;
            //LOGICAL UNIT NOT READY, BECOMING READY
            asc = 0x04;
            ascq = 0x01;
        }
        break;
    case 0x83://reservation conflict
        //spec says "N/A"
        genericCatchAllSense = true;
        break;
    case 0x84://format in progress
        //we can set this translation even though it isn't in the spec
        senseKey = SENSE_KEY_NOT_READY;
        asc = 0x04;
        ascq = 0x04;
        break;
    default:
        //20-7Fh are reserved
        //C0-FFh are vendor specific
        genericCatchAllSense = true;
        break;
    }
    if (genericCatchAllSense)
    {
        if (nvmeStatus > 0xC0)
        {
            //vendor specific
            senseKey = SENSE_KEY_VENDOR_SPECIFIC;
        }
        else
        {
            //just say aborted command
            senseKey = SENSE_KEY_ABORTED_COMMAND;
        }
        asc = 0;
        ascq = 0;
    }
    if (returnSenseKeySpecificInfo)
    {
        sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq, device->drive_info.softSATFlags.senseDataDescriptorFormat, informationSenseDescriptor, 1);
    }
    else
    {
        sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    }
    return;
}
//the completion queue will tell us if the error is specific to a command versus a generic error
void set_Sense_Data_By_Command_Specific_NVMe_Status(tDevice *device, uint8_t nvmeStatus, uint8_t *sensePtr, uint32_t senseDataLength)
{
    //first check if sense data reporting is supported
    uint8_t senseKey = 0, asc = 0, ascq = 0;
    bool returnSenseKeySpecificInfo = false;
    uint8_t informationSenseDescriptor[12] = { 0 };

    bool genericFailureSenseData = false;

    //TODO: should use enum values instead
    switch (nvmeStatus)
    {
    case 0://completion queue invalid
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //no additional sense data
        break;
    case 1://invalid queue identifier
    case 2://invalid queue size
        genericFailureSenseData = true;
        break;
    case 3://abort command limit exceeded
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //no additional sense data
        break;
    //case 4://reserved
    case 5://asynchronous event request limit exceeded
    case 6://invalid firmware slot
    case 7://invalid firmware image
    case 8://invalid interrupt vector
    case 9://invalid log page
        genericFailureSenseData = true;
        break;
    case 0xA://invalid format
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //format command failed
        asc = 0x31;
        ascq = 0x01;
        break;
    case 0x0B://Firmware Activation Requires Conventional Reset
    case 0x0C://Invalid Queue Deletion
    case 0x0D://Feature Identifier Not Savable
    case 0x0E://Feature Not Changeable
    case 0x0F://Feature Not Namespace Specific
    case 0x10://firmware Activation requires NVM subsystem reset
    case 0x11://firmware activation requires reset
    case 0x12://firmware activation requires maximum timeout violation
    case 0x13://firmware activation prohibited
    case 0x14://overlapping range
    case 0x15://namespace insufficient capacity
    case 0x16://namespace identifie3r unavailable
    case 0x17://reserved
    case 0x18://namespace already attached
    case 0x19://namespace is private
    case 0x1A://namespace not attached
    case 0x1B://thin provisioning not supported
    case 0x1C://controller list invalid
        genericFailureSenseData = true;
        break;
    case 0x1D://device self-test in progress
        senseKey = SENSE_KEY_NOT_READY;
        asc = 0x04;
        ascq = 0x09;
        break;
    case 0x1E://boot partition write protected
    case 0x1F://invalid controller identifier
    case 0x20://invalid secondary controller state
    case 0x21://invalid number of controller resources
    case 0x22://invalid resource identifier
        genericFailureSenseData = true;
        break;
    case 0x80://conflicting attributes (dataset management, read, write)
        senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        //invalid field in CDB (should be able to set param pointer for this some day)
        asc = 0x24;
        ascq = 0x00;
        break;
    case 0x81://invalid protection information (compare, read, write, write zeros)
        genericFailureSenseData = true;
        break;
    case 0x82://attempted write to read only range (dataset management, write, write uncorrectable, write zeros)
        senseKey = SENSE_KEY_DATA_PROTECT;
        asc = 0x27;
        ascq = 0x00;
        break;
        //70-7f are directive specific
        //80-BF IO command set specific
        //C0-FF vendor specific
    default:
        //set something generic
        genericFailureSenseData = true;
        break;
    }
    if (genericFailureSenseData)
    {
        if (nvmeStatus > 0xC0)
        {
            senseKey = SENSE_KEY_VENDOR_SPECIFIC;
        }
        else
        {
            senseKey = SENSE_KEY_ILLEGAL_REQUEST;
        }
    }

    if (returnSenseKeySpecificInfo)
    {
        sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq, device->drive_info.softSATFlags.senseDataDescriptorFormat, informationSenseDescriptor, 1);
    }
    else
    {
        sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    }
    return;
}

void set_Sense_Data_By_Media_Errors_NVMe_Status(tDevice *device, uint8_t nvmeStatus, uint8_t *sensePtr, uint32_t senseDataLength)
{
    //first check if sense data reporting is supported
    uint8_t senseKey = 0, asc = 0, ascq = 0;
    bool returnSenseKeySpecificInfo = false;
    uint8_t informationSenseDescriptor[12] = { 0 };
    bool genericFailureSenseData = false;

    //make sure these are cleared out still (compiler should optimize this away if this is redundant)
    senseKey = 0;
    asc = 0;
    ascq = 0;

    //TODO: Media Errors translations
    switch (nvmeStatus)
    {
        //admin are 0 - 7Fh
        //IO command set specific are 80-BFh
        //Vendor specific are 0xC0-FFh
    case 0x80://write fault
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //peripheral device write fault
        asc = 0x03;
        ascq = 0x00;
        break;
    case 0x81://unrecovered read error
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //unrecovered read error
        asc = 0x11;
        ascq = 0x00;
        break;
    case 0x82://end to end guard check failure
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //logical block guard check failed
        asc = 0x10;
        ascq = 0x01;
        break;
    case 0x83://end to end application tag check failure
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //logical block application tag check failed
        asc = 0x10;
        ascq = 0x02;
        break;
    case 0x84://end to end reference tag check error
        senseKey = SENSE_KEY_MEDIUM_ERROR;
        //logical block reference tag check failed
        asc = 0x10;
        ascq = 0x03;
        break;
    case 0x85://compare failure
        senseKey = SENSE_KEY_MISCOMPARE;
        //miscompare during verify operation
        asc = 0x1D;
        ascq = 0x00;
        break;
    case 0x86://access denied
        senseKey = SENSE_KEY_DATA_PROTECT;
        //access denied - no access rights
        asc = 0x20;
        ascq = 0x02;
        break;
        //below here are in spec but not defined
    case 0x87://deallocated or unwritten logical block
    default:
        genericFailureSenseData = true;
        break;
    }
    if (genericFailureSenseData)
    {
        if (nvmeStatus > 0xC0)
        {
            senseKey = SENSE_KEY_VENDOR_SPECIFIC;
        }
        else
        {
            senseKey = SENSE_KEY_MEDIUM_ERROR;
        }
    }
    if (returnSenseKeySpecificInfo)
    {
        sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq, device->drive_info.softSATFlags.senseDataDescriptorFormat, informationSenseDescriptor, 1);
    }
    else
    {
        sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, senseKey, asc, ascq, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    }
    return;
}

void set_Sense_Data_By_NVMe_Status(tDevice *device, uint32_t completionDWord3, uint8_t *sensePtr, uint32_t senseDataLength)
{
    uint8_t statusCodeType = M_GETBITRANGE(completionDWord3, 27, 25);
    uint8_t statusCode = M_GETBITRANGE(completionDWord3, 24, 17);
    bool doNotRetry = completionDWord3 & BIT31;

    switch (statusCodeType)
    {
    case 0://generic
        set_Sense_Data_By_Generic_NVMe_Status(device, statusCode, sensePtr, senseDataLength, doNotRetry);
        break;
    case 1://command specific
        set_Sense_Data_By_Command_Specific_NVMe_Status(device, statusCode, sensePtr, senseDataLength);
        break;
    case 2://media and data integrity errors
        set_Sense_Data_By_Media_Errors_NVMe_Status(device, statusCode, sensePtr, senseDataLength);
        break;
    default:
        //set some kind of generic sense error!
        //if status code type is 7, set a vendor unique sense data error
        //TODO: find a way to send the NVMe status back up through here
        if (statusCodeType == 7)
        {
            sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, SENSE_KEY_VENDOR_SPECIFIC, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        }
        else
        {
            sntl_Set_Sense_Data_For_Translation(sensePtr, senseDataLength, SENSE_KEY_RESERVED, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        }
        break;
    }
}

int sntl_Translate_Supported_VPD_Pages_00h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t supportedPages[LEGACY_DRIVE_SEC_SIZE] = { 0 };
    uint16_t pageOffset = 4;
    supportedPages[0] = 0;
    supportedPages[1] = 0;//page 0
    //set page 0 in here
    supportedPages[pageOffset] = SUPPORTED_VPD_PAGES;
    pageOffset++;
    //unit serial number
    supportedPages[pageOffset] = UNIT_SERIAL_NUMBER;
    pageOffset++;
    //device identification
    supportedPages[pageOffset] = DEVICE_IDENTIFICATION;
    pageOffset++;
    //extended inquiry data
    supportedPages[pageOffset] = EXTENDED_INQUIRY_DATA;
    pageOffset++;
    ////mode page policy
    //supportedPages[pageOffset] = MODE_PAGE_POLICY;
    //pageOffset++;
    //block limits
    supportedPages[pageOffset] = BLOCK_LIMITS;
    pageOffset++;
    //block device characteristics
    supportedPages[pageOffset] = BLOCK_DEVICE_CHARACTERISTICS;
    pageOffset++;
    supportedPages[pageOffset] = LOGICAL_BLOCK_PROVISIONING;
    pageOffset++;
    //set the page length last
    supportedPages[2] = M_Byte1(pageOffset - 4);
    supportedPages[3] = M_Byte0(pageOffset - 4);
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, supportedPages, M_Min(pageOffset, scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_Unit_Serial_Number_VPD_Page_80h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t unitSerialNumber[44] = { 0 };//44 is the max size of this page with the translation spec
    uint16_t pageLength = 0;
    bool eui64nonZero = false;
    bool nguidnonZero = false;
    uint8_t zeros[16] = { 0 };
    unitSerialNumber[0] = 0;
    unitSerialNumber[1] = UNIT_SERIAL_NUMBER;
    //Check EUI64 and NGUID fields to see if non-zero
    if (memcmp(device->drive_info.IdentifyData.nvme.ns.nguid, zeros, 16))
    {
        nguidnonZero = true;
    }
    if (memcmp(device->drive_info.IdentifyData.nvme.ns.eui64, zeros, 8))
    {
        eui64nonZero = true;
    }
    //based on what we found, we need to set the SN
    if (eui64nonZero && !nguidnonZero)
    {
        //EUI separated by _ every 4 characters ending with a .
        uint8_t euiOffset = 0;
        uint8_t offset = 4;
        while (offset < 23 && euiOffset < 8)//23 is the final character, which will be a period
        {
            if (euiOffset > 0 && (euiOffset * 2) % 4 == 0)
            {
                unitSerialNumber[offset] = '_';
                ++offset;
            }
            else
            {
                char shortString[3] = { 0 };
                sprintf(shortString, "%02" PRIX8, device->drive_info.IdentifyData.nvme.ns.eui64[euiOffset]);
                unitSerialNumber[offset] = shortString[0];
                unitSerialNumber[offset + 1] = shortString[1];
                offset += 2;
                ++euiOffset;
            }
        }
        unitSerialNumber[23] = '.';
        pageLength = 20;
    }
    else if ((!eui64nonZero && nguidnonZero) || (eui64nonZero && nguidnonZero))
    {
        //NGUID separated by _ every 4 spaces ending with a .
        uint8_t nguidOffset = 0;
        uint8_t offset = 4;
        while (offset < 43 && nguidOffset < 16)//43 is the final character, which will be a period
        {
            if (nguidOffset > 0 && (nguidOffset * 2) % 4 == 0)
            {
                unitSerialNumber[offset] = '_';
                ++offset;
            }
            else
            {
                char shortString[3] = { 0 };
                sprintf(shortString, "%02" PRIX8, device->drive_info.IdentifyData.nvme.ns.nguid[nguidOffset]);
                unitSerialNumber[offset] = shortString[0];
                unitSerialNumber[offset + 1] = shortString[1];
                offset += 2;
                ++nguidOffset;
            }
        }
        unitSerialNumber[43] = '.';
        pageLength = 40;
    }
    else //If both of these fields aren't set, this is an NVMe 1.0 device that needs a different thing to be returned here.
    {
        char nsidString[10] = { 0 };
        uint8_t counter = 0;
        //SN_NSID(ashex).
        uint8_t offset = 4;
        while (counter < 20)
        {
            unitSerialNumber[offset] = device->drive_info.IdentifyData.nvme.ctrl.sn[counter];
            ++offset;
            ++counter;
        }
        unitSerialNumber[offset] = '_';
        sprintf(nsidString, "%08" PRIX32, device->drive_info.namespaceID);
        counter = 0;
        while (counter < 8)
        {
            unitSerialNumber[offset] = nsidString[counter];
            ++offset;
            ++counter;
        }
        unitSerialNumber[offset] = '.';
        pageLength = 30;
    }
    unitSerialNumber[2] = M_Byte1(pageLength);
    unitSerialNumber[3] = M_Byte0(pageLength);
    //now copy all the data we set up back to the scsi io ctx
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, unitSerialNumber, M_Min((uint32_t)pageLength + UINT32_C(4), scsiIoCtx->dataLength));
    }
    return ret;
}

//translation spec says we need one of the following:
//NAA IEEE Registered extended designator
//T10 Vendor ID designator
//SCSI Name String designator
//EUI64 designator
//Spec strongly recommends at least one EUI64 designator
int sntl_Translate_Device_Identification_VPD_Page_83h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t zeros[16] = { 0 };
    //naa designator
    uint8_t naaDesignatorLength = 0;//will be set if drive supports the WWN
    uint8_t *naaDesignator = NULL;
    //scsi name string designator
    uint8_t SCSINameStringDesignatorLength = 0;
    uint8_t *SCSINameStringDesignator = NULL;
    //vars for t10 vendor id designator
    uint8_t *t10VendorIdDesignator = NULL;
    uint8_t t10VendorIdDesignatorLength = 0;
    //EUI64
    uint8_t *eui64Designator = NULL;
    uint8_t eui64DesignatorLength = 0;

    //will hold the complete data to return
    uint8_t *deviceIdentificationPage = NULL;
    bool nguidnonZero = false;
    bool eui64nonZero = false;
    //Check EUI64 and NGUID fields to see if non-zero
    if (memcmp(device->drive_info.IdentifyData.nvme.ns.nguid, zeros, 16))
    {
        nguidnonZero = true;
    }
    if (memcmp(device->drive_info.IdentifyData.nvme.ns.eui64, zeros, 8))
    {
        eui64nonZero = true;
    }


    if (eui64nonZero)//this must be non-zero to be supported.
    {
        naaDesignatorLength = 20 /*ext*/ + 12 /*locally assigned*/;
        naaDesignator = (uint8_t*)calloc(naaDesignatorLength, sizeof(uint8_t));
        //NAA extended format (6 + OUI + 64bitsEUI64 + 32bits of zeros)
        naaDesignator[0] = 1;//codes set 1
        naaDesignator[1] = 3;//designator type 3, associated with logical unit
        naaDesignator[2] = RESERVED;
        naaDesignator[3] = 16;//16 bytes for the ext designator
        naaDesignator[4] = M_NibblesTo1ByteValue(6, M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.ieee[0]));
        naaDesignator[5] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.ieee[0]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.ieee[1]));
        naaDesignator[6] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.ieee[1]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.ieee[2]));
        naaDesignator[7] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.ieee[2]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[0]));
        naaDesignator[8] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[0]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[1]));
        naaDesignator[9] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[1]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[2]));
        naaDesignator[10] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[2]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[3]));
        naaDesignator[11] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[3]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[4]));
        naaDesignator[12] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[4]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[5]));
        naaDesignator[13] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[5]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[6]));
        naaDesignator[14] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[6]), M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[7]));
        naaDesignator[15] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[7]), 0);
        naaDesignator[16] = 0;
        naaDesignator[17] = 0;
        naaDesignator[18] = 0;
        naaDesignator[19] = 0;
        //NAA locally assigned designator (3 + first 60bits of EUI64)
        naaDesignator[20] = 1;//codes set 1
        naaDesignator[21] = 3;//designator type 3, associated with logical unit
        naaDesignator[22] = RESERVED;
        naaDesignator[23] = 8;//8 bytes for the local designator
        naaDesignator[24] = M_NibblesTo1ByteValue(3, M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[0]));
        naaDesignator[25] = device->drive_info.IdentifyData.nvme.ns.eui64[1];
        naaDesignator[26] = device->drive_info.IdentifyData.nvme.ns.eui64[2];
        naaDesignator[27] = device->drive_info.IdentifyData.nvme.ns.eui64[3];
        naaDesignator[28] = device->drive_info.IdentifyData.nvme.ns.eui64[4];
        naaDesignator[29] = device->drive_info.IdentifyData.nvme.ns.eui64[5];
        naaDesignator[30] = device->drive_info.IdentifyData.nvme.ns.eui64[6];
        naaDesignator[31] = device->drive_info.IdentifyData.nvme.ns.eui64[7];
    }
    else if (!eui64nonZero && !nguidnonZero) //NVMe 1.0 devices won't support EUI or NGUID, so we should be able to detect them like this
    {
        naaDesignatorLength = 20 /*ext*/ + 12 /*locally assigned*/;
        naaDesignator = (uint8_t*)calloc(naaDesignatorLength, sizeof(uint8_t));
        //NAA extended format (6 + OUI + 64bitsEUI64 + 32bits of zeros)
        naaDesignator[0] = 1;//codes set 1
        naaDesignator[1] = 3;//designator type 3, associated with logical unit
        naaDesignator[2] = RESERVED;
        naaDesignator[3] = 16;//16 bytes following this
        naaDesignator[4] = M_NibblesTo1ByteValue(6, M_Nibble3(device->drive_info.IdentifyData.nvme.ctrl.vid));
        naaDesignator[5] = M_NibblesTo1ByteValue(M_Nibble2(device->drive_info.IdentifyData.nvme.ctrl.vid), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.vid));
        naaDesignator[6] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.vid), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[0]));
        naaDesignator[7] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[0]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[1]));
        naaDesignator[8] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[1]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[2]));
        naaDesignator[9] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[2]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[3]));
        naaDesignator[10] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[3]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[4]));
        naaDesignator[11] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[4]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[5]));
        naaDesignator[12] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[5]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[6]));
        naaDesignator[13] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[6]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[7]));
        naaDesignator[14] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[7]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[8]));
        naaDesignator[15] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[8]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[9]));
        naaDesignator[16] = M_Byte3(device->drive_info.namespaceID);
        naaDesignator[17] = M_Byte2(device->drive_info.namespaceID);
        naaDesignator[18] = M_Byte1(device->drive_info.namespaceID);
        naaDesignator[19] = M_Byte0(device->drive_info.namespaceID);
        //NAA locally assigned designator (3 + first 60bits of EUI64)
        naaDesignator[20] = 1;//codes set 1
        naaDesignator[21] = 3;//designator type 3, associated with logical unit
        naaDesignator[22] = RESERVED;
        naaDesignator[23] = 8;//8 bytes for the local designator
        naaDesignator[24] = M_NibblesTo1ByteValue(3, M_Nibble3(device->drive_info.IdentifyData.nvme.ctrl.vid));
        naaDesignator[25] = M_NibblesTo1ByteValue(M_Nibble2(device->drive_info.IdentifyData.nvme.ctrl.vid), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.vid));
        naaDesignator[26] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.vid), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[0]));
        naaDesignator[27] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[0]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[1]));
        naaDesignator[28] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[1]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[2]));
        naaDesignator[29] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[2]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[3]));
        naaDesignator[30] = M_NibblesTo1ByteValue(M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.sn[3]), M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.sn[4]));
        naaDesignator[31] = M_Byte0(device->drive_info.namespaceID);
    }

    //T10 Vendor ID descriptor (VendorID + productIdentification + (EUI64 || NGUID))
    if (eui64nonZero || nguidnonZero)
    {
        uint8_t offset = 12;
        t10VendorIdDesignatorLength = 16 + 4;//Length of truncated product ID as required by spec (4byte header)
        if (nguidnonZero)
        {
            t10VendorIdDesignatorLength += 32;//32characters to hold the NGUID as a string
        }
        else
        {
            t10VendorIdDesignatorLength += 16;//16 characters to hold the EUI64 as a string
        }
        t10VendorIdDesignator = (uint8_t*)calloc(t10VendorIdDesignatorLength, sizeof(uint8_t));
        t10VendorIdDesignator[0] = 2;//codes set 2
        t10VendorIdDesignator[1] = 1;//designator type 1, associated with logical unit
        t10VendorIdDesignator[2] = RESERVED;
        t10VendorIdDesignator[3] = t10VendorIdDesignatorLength - 4;
        //first set the t10 vendor id in the buffer
        t10VendorIdDesignator[4] = 'N';
        t10VendorIdDesignator[5] = 'V';
        t10VendorIdDesignator[6] = 'M';
        t10VendorIdDesignator[7] = 'e';
        t10VendorIdDesignator[8] = ' ';
        t10VendorIdDesignator[9] = ' ';
        t10VendorIdDesignator[10] = ' ';
        t10VendorIdDesignator[11] = ' ';
        //Need to set product ID here (16 bytes)
        for (uint8_t mnOffset = 0; mnOffset < 16; ++mnOffset, ++offset)
        {
            t10VendorIdDesignator[offset] = device->drive_info.IdentifyData.nvme.ctrl.mn[mnOffset];
        }
        //now either NGUID or EUI64
        if (nguidnonZero)
        {
            uint8_t counter = 0;
            while (counter < 16 && offset < t10VendorIdDesignatorLength)
            {
                t10VendorIdDesignator[offset] = M_Nibble1(device->drive_info.IdentifyData.nvme.ns.nguid[counter]);
                t10VendorIdDesignator[offset + 1] = M_Nibble0(device->drive_info.IdentifyData.nvme.ns.nguid[counter]);
                offset += 2;
                ++counter;
            }
        }
        else
        {
            uint8_t counter = 0;
            while (counter < 8 && offset < t10VendorIdDesignatorLength)
            {
                t10VendorIdDesignator[offset] = M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[counter]);
                t10VendorIdDesignator[offset + 1] = M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[counter]);
                offset += 2;
                ++counter;
            }
        }
    }
    else //nvme 1.0 devices: (VendorID + productIdentification + PCI VendorID + lower 52Bits of SN + NSID)
    {
        uint8_t offset = 12;
        t10VendorIdDesignatorLength = 44;
        t10VendorIdDesignator = (uint8_t*)calloc(t10VendorIdDesignatorLength, sizeof(uint8_t));
        t10VendorIdDesignator[0] = 2;//codes set 2 (ASCII)
        t10VendorIdDesignator[1] = 1;//designator type 1, associated with logical unit
        t10VendorIdDesignator[2] = RESERVED;
        t10VendorIdDesignator[3] = t10VendorIdDesignatorLength - 4;
        //first set the t10 vendor id in the buffer
        t10VendorIdDesignator[4] = 'N';
        t10VendorIdDesignator[5] = 'V';
        t10VendorIdDesignator[6] = 'M';
        t10VendorIdDesignator[7] = 'e';
        t10VendorIdDesignator[8] = ' ';
        t10VendorIdDesignator[9] = ' ';
        t10VendorIdDesignator[10] = ' ';
        t10VendorIdDesignator[11] = ' ';
        //Need to set product ID here (16 bytes)
        for (uint8_t mnOffset = 0; mnOffset < 16; ++mnOffset, ++offset)
        {
            t10VendorIdDesignator[offset] = device->drive_info.IdentifyData.nvme.ctrl.mn[mnOffset];
        }
        //now set PCI Vendor ID (as ASCII...spec is horribly written about this)
        t10VendorIdDesignator[28] = M_Nibble3(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        t10VendorIdDesignator[29] = M_Nibble2(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        t10VendorIdDesignator[30] = M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        t10VendorIdDesignator[31] = M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        //Now some SN bytes
        t10VendorIdDesignator[32] = device->drive_info.IdentifyData.nvme.ctrl.sn[0];
        t10VendorIdDesignator[33] = device->drive_info.IdentifyData.nvme.ctrl.sn[1];
        t10VendorIdDesignator[34] = device->drive_info.IdentifyData.nvme.ctrl.sn[2];
        t10VendorIdDesignator[35] = device->drive_info.IdentifyData.nvme.ctrl.sn[3];
        t10VendorIdDesignator[36] = device->drive_info.IdentifyData.nvme.ctrl.sn[4];
        t10VendorIdDesignator[37] = device->drive_info.IdentifyData.nvme.ctrl.sn[5];
        t10VendorIdDesignator[38] = device->drive_info.IdentifyData.nvme.ctrl.sn[6];
        //Finally, NSID (as ASCII)
        t10VendorIdDesignator[39] = M_Nibble7(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[40] = M_Nibble6(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[41] = M_Nibble5(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[42] = M_Nibble4(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[33] = M_Nibble3(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[44] = M_Nibble2(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[45] = M_Nibble1(device->drive_info.namespaceID) + '0';
        t10VendorIdDesignator[46] = M_Nibble0(device->drive_info.namespaceID) + '0';
    }

    //SCSI Name String (depends on NGUID and EUI64 field support...)
    if (eui64nonZero && nguidnonZero)
    {
        uint8_t counter = 0;
        uint8_t offset = 8;
        //1 descriptor for eui64 and 1 for nguid
        SCSINameStringDesignatorLength = 64;
        SCSINameStringDesignator = (uint8_t*)calloc(SCSINameStringDesignatorLength, sizeof(uint8_t));
        //NGUID first!
        SCSINameStringDesignator[0] = 3;//codes set 3 (UTF-8)
        SCSINameStringDesignator[1] = 1;//designator type 1, associated with logical unit
        SCSINameStringDesignator[2] = RESERVED;
        SCSINameStringDesignator[3] = 36;
        //set "eui."
        SCSINameStringDesignator[4] = 'e';
        SCSINameStringDesignator[5] = 'u';
        SCSINameStringDesignator[6] = 'i';
        SCSINameStringDesignator[7] = '.';
        //now nguid
        while (counter < 16 && offset < t10VendorIdDesignatorLength)
        {
            t10VendorIdDesignator[offset] = M_Nibble1(device->drive_info.IdentifyData.nvme.ns.nguid[counter]) + '0';
            t10VendorIdDesignator[offset + 1] = M_Nibble0(device->drive_info.IdentifyData.nvme.ns.nguid[counter]) + '0';
            offset += 2;
            ++counter;
        }
        //now EUI 64!
        SCSINameStringDesignator[40] = 3;//codes set 3 (UTF-8)
        SCSINameStringDesignator[41] = 1;//designator type 1, associated with logical unit
        SCSINameStringDesignator[42] = RESERVED;
        SCSINameStringDesignator[43] = 20;
        //set "eui."
        SCSINameStringDesignator[44] = 'e';
        SCSINameStringDesignator[45] = 'u';
        SCSINameStringDesignator[46] = 'i';
        SCSINameStringDesignator[47] = '.';
        //now eui64
        counter = 0;
        offset = 48;
        while (counter < 8 && offset < t10VendorIdDesignatorLength)
        {
            t10VendorIdDesignator[offset] = M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[counter]) + '0';
            t10VendorIdDesignator[offset + 1] = M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[counter]) + '0';
            offset += 2;
            ++counter;
        }
        
    }
    else if (nguidnonZero)
    {
        uint8_t counter = 0;
        uint8_t offset = 8;
        //eui. + 32 hex digits from nguid (msb to lsb) 36Bytes total length
        SCSINameStringDesignatorLength = 40;
        SCSINameStringDesignator = (uint8_t*)calloc(SCSINameStringDesignatorLength, sizeof(uint8_t));
        SCSINameStringDesignator[0] = 3;//codes set 3 (UTF-8)
        SCSINameStringDesignator[1] = 1;//designator type 1, associated with logical unit
        SCSINameStringDesignator[2] = RESERVED;
        SCSINameStringDesignator[3] = SCSINameStringDesignatorLength - 4;
        //set "eui."
        SCSINameStringDesignator[4] = 'e';
        SCSINameStringDesignator[5] = 'u';
        SCSINameStringDesignator[6] = 'i';
        SCSINameStringDesignator[7] = '.';
        //now nguid
        while (counter < 16 && offset < t10VendorIdDesignatorLength)
        {
            t10VendorIdDesignator[offset] = M_Nibble1(device->drive_info.IdentifyData.nvme.ns.nguid[counter]) + '0';
            t10VendorIdDesignator[offset + 1] = M_Nibble0(device->drive_info.IdentifyData.nvme.ns.nguid[counter]) + '0';
            offset += 2;
            ++counter;
        }
    }
    else if (eui64nonZero)
    {
        uint8_t counter = 0;
        uint8_t offset = 8;
        //eui. + 32 hex digits from nguid (msb to lsb) 36Bytes total length
        SCSINameStringDesignatorLength = 24;
        SCSINameStringDesignator = (uint8_t*)calloc(SCSINameStringDesignatorLength, sizeof(uint8_t));
        SCSINameStringDesignator[0] = 3;//codes set 3 (UTF-8)
        SCSINameStringDesignator[1] = 1;//designator type 1, associated with logical unit
        SCSINameStringDesignator[2] = RESERVED;
        SCSINameStringDesignator[3] = SCSINameStringDesignatorLength - 4;
        //set "eui."
        SCSINameStringDesignator[4] = 'e';
        SCSINameStringDesignator[5] = 'u';
        SCSINameStringDesignator[6] = 'i';
        SCSINameStringDesignator[7] = '.';
        //now eui64
        while (counter < 8 && offset < t10VendorIdDesignatorLength)
        {
            t10VendorIdDesignator[offset] = M_Nibble1(device->drive_info.IdentifyData.nvme.ns.eui64[counter]) + '0';
            t10VendorIdDesignator[offset + 1] = M_Nibble0(device->drive_info.IdentifyData.nvme.ns.eui64[counter]) + '0';
            offset += 2;
            ++counter;
        }
    }
    else //nvme 1.0 - //2bytes of PCI Vendor ID (utf8) + 40 bytes of MN + 4 bytes of NSID (utf8) + 20 bytes of SN
    {
        uint8_t offset = 8;
        SCSINameStringDesignatorLength = 72;
        SCSINameStringDesignator = (uint8_t*)calloc(SCSINameStringDesignatorLength, sizeof(uint8_t));
        SCSINameStringDesignator[0] = 3;//codes set 3 (UTF-8)
        SCSINameStringDesignator[1] = 1;//designator type 1, associated with logical unit
        SCSINameStringDesignator[2] = RESERVED;
        SCSINameStringDesignator[3] = SCSINameStringDesignatorLength - 4;
        //now set PCI Vendor ID (as UTF8)
        SCSINameStringDesignator[4] = M_Nibble3(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        SCSINameStringDesignator[5] = M_Nibble2(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        SCSINameStringDesignator[6] = M_Nibble1(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        SCSINameStringDesignator[7] = M_Nibble0(device->drive_info.IdentifyData.nvme.ctrl.vid) + '0';
        //40 MN bytes
        for (uint8_t mnCounter = 0; mnCounter < 40; ++mnCounter, ++offset)
        {
            SCSINameStringDesignator[offset] = device->drive_info.IdentifyData.nvme.ctrl.mn[mnCounter];
        }
        //NSID (as UTF-8)
        SCSINameStringDesignator[48] = M_Byte3(device->drive_info.namespaceID) + '0';
        SCSINameStringDesignator[49] = M_Byte2(device->drive_info.namespaceID) + '0';
        SCSINameStringDesignator[50] = M_Byte1(device->drive_info.namespaceID) + '0';
        SCSINameStringDesignator[51] = M_Byte0(device->drive_info.namespaceID) + '0';
        //Now 20 SN bytes
        offset = 52;
        for (uint8_t snCounter = 0; snCounter < 20; ++snCounter, ++offset)
        {
            SCSINameStringDesignator[offset] = device->drive_info.IdentifyData.nvme.ctrl.sn[snCounter];
        }
    }

    //EUI64 designator (depends on NGUID and EUI64 support...can return one for each of these)
    if (eui64nonZero && nguidnonZero)
    {
        //1 descriptor for eui64 and 1 for nguid
        uint8_t offset = 4;
        eui64DesignatorLength = 32;
        eui64Designator = (uint8_t*)calloc(eui64DesignatorLength, sizeof(uint8_t));
        //NGUID first
        eui64Designator[0] = 1;//codes set 1 (binary)
        eui64Designator[1] = 2;//designator type 2, associated with logical unit
        eui64Designator[2] = RESERVED;
        eui64Designator[3] = 16;//16 for nguid
        for (uint8_t nguidCounter = 0; nguidCounter < 16; ++nguidCounter, ++offset)
        {
            eui64Designator[offset] = device->drive_info.IdentifyData.nvme.ns.nguid[nguidCounter];
        }
        //EUI64 next
        eui64Designator[20] = 1;//codes set 1 (binary)
        eui64Designator[21] = 2;//designator type 2, associated with logical unit
        eui64Designator[22] = RESERVED;
        eui64Designator[23] = 8;//8 for eui64
        offset = 24;
        for (uint8_t euiCounter = 0; euiCounter < 8; ++euiCounter, ++offset)
        {
            eui64Designator[offset] = device->drive_info.IdentifyData.nvme.ns.eui64[euiCounter];
        }
    }
    else if (nguidnonZero)
    {
        uint8_t offset = 4;
        eui64DesignatorLength = 20;
        eui64Designator = (uint8_t*)calloc(eui64DesignatorLength, sizeof(uint8_t));
        eui64Designator[0] = 1;//codes set 1 (binary)
        eui64Designator[1] = 2;//designator type 2, associated with logical unit
        eui64Designator[2] = RESERVED;
        eui64Designator[3] = 16;//16 for nguid
        for (uint8_t nguidCounter = 0; nguidCounter < 16; ++nguidCounter, ++offset)
        {
            eui64Designator[offset] = device->drive_info.IdentifyData.nvme.ns.nguid[nguidCounter];
        }
    }
    else if (eui64nonZero)
    {
        uint8_t offset = 4;
        eui64DesignatorLength = 12;
        eui64Designator = (uint8_t*)calloc(eui64DesignatorLength, sizeof(uint8_t));
        eui64Designator[0] = 1;//codes set 1 (binary)
        eui64Designator[1] = 2;//designator type 2, associated with logical unit
        eui64Designator[2] = RESERVED;
        eui64Designator[3] = 8;//8 for eui64
        for (uint8_t euiCounter = 0; euiCounter < 8; ++euiCounter, ++offset)
        {
            eui64Designator[offset] = device->drive_info.IdentifyData.nvme.ns.eui64[euiCounter];
        }
    }
    //else NVMe 1.0 will not support this designator!
    
    //now setup the device identification page
    deviceIdentificationPage = (uint8_t*)calloc(4U + eui64DesignatorLength + t10VendorIdDesignatorLength + naaDesignatorLength + SCSINameStringDesignatorLength, sizeof(uint8_t));
    if (!deviceIdentificationPage)
    {
        return MEMORY_FAILURE;
    }
    deviceIdentificationPage[0] = 0;
    deviceIdentificationPage[1] = DEVICE_IDENTIFICATION;
    deviceIdentificationPage[2] = M_Byte1(eui64DesignatorLength + t10VendorIdDesignatorLength + naaDesignatorLength + SCSINameStringDesignatorLength);
    deviceIdentificationPage[3] = M_Byte0(eui64DesignatorLength + t10VendorIdDesignatorLength + naaDesignatorLength + SCSINameStringDesignatorLength);
    //copy naa first
    if (naaDesignatorLength > 0)
    {
        memcpy(&deviceIdentificationPage[4], naaDesignator, naaDesignatorLength);
    }
    safe_Free(naaDesignator);
    //t10 second
    if (t10VendorIdDesignatorLength > 0)
    {
        memcpy(&deviceIdentificationPage[4 + naaDesignatorLength], t10VendorIdDesignator, t10VendorIdDesignatorLength);
    }
    safe_Free(t10VendorIdDesignator);
    //scsi name string third
    if (SCSINameStringDesignatorLength > 0)
    {
        memcpy(&deviceIdentificationPage[4 + naaDesignatorLength + t10VendorIdDesignatorLength], SCSINameStringDesignator, SCSINameStringDesignatorLength);
    }
    safe_Free(SCSINameStringDesignator);
    //eui64 last
    if (eui64DesignatorLength > 0)
    {
        memcpy(&deviceIdentificationPage[4 + naaDesignatorLength + t10VendorIdDesignatorLength + SCSINameStringDesignatorLength], eui64Designator, eui64DesignatorLength);
    }
    safe_Free(eui64Designator);
    //copy the final data back for the command
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, deviceIdentificationPage, M_Min(4U + eui64DesignatorLength + t10VendorIdDesignatorLength + naaDesignatorLength + SCSINameStringDesignatorLength, scsiIoCtx->dataLength));
    }
    safe_Free(deviceIdentificationPage);
    return ret;
}

int sntl_Translate_Extended_Inquiry_Data_VPD_Page_86h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t extendedInquiry[64] = { 0 };
    extendedInquiry[0] = 0;
    extendedInquiry[1] = EXTENDED_INQUIRY_DATA;
    extendedInquiry[2] = 0x00;
    extendedInquiry[3] = 0x3C;
    //activate microcode shalll be 10b
    extendedInquiry[4] |= BIT7;//10b
    uint8_t spt = 0;
    switch (device->drive_info.IdentifyData.nvme.ns.dpc)
    {
    case 1:
        spt = 0;
        break;
    case 2:
        spt = 2;
        break;
    case 3:
        spt = 1;
        break;
    case 4:
        spt = 4;
        break;
    case 5:
        spt = 3;
        break;
    case 6:
        spt = 5;
        break;
    case 7:
        spt = 7;
        break;
    case 0:
    default:
        //undefinted...leave as zero
        break;
    }
    extendedInquiry[4] |= (spt << 3);
    if (device->drive_info.IdentifyData.nvme.ns.dps != 0)
    {
        //set grd_chk, app_chk, & ref_chk
        extendedInquiry[4] |= (BIT2 | BIT1 | BIT0);
    }
    extendedInquiry[5] |= BIT5;//set UASK_SUP
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
    {
        extendedInquiry[6] |= BIT3;//set WU_SUP since write uncorrectable command is supported
        extendedInquiry[6] |= BIT2;//set CRD_SUP since write uncorrectable command is supported
    }
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)
    {
        extendedInquiry[6] |= BIT0;
    }
    extendedInquiry[7] |= BIT0;//LUICLR is supported

    //Extended self test completion time set to zero since not supported (our extension will set this if the drive supports the DST commands from NVMe 1.3)
#if defined (SNTL_EXT)
    //if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT4)
    //{
    //    //DST command is supported! So get the time long dst will take to run and put it in here
    //    extendedInquiry[10] = M_Byte1(device->drive_info.IdentifyData.nvme.ctrl.edstt);
    //    extendedInquiry[11] = M_Byte0(device->drive_info.IdentifyData.nvme.ctrl.edstt);
    //}
    //else
#endif
    {
        extendedInquiry[10] = 0;
        extendedInquiry[11] = 0;
    }
    //max sense data length
    extendedInquiry[13] = 0;//can also set 252 since that is the spec maximum. This also means there is not a max

    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, extendedInquiry, M_Min(64, scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_Block_Limits_VPD_Page_B0h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t blockLimits[64] = { 0 };
    blockLimits[0] = 0;
    blockLimits[1] = 0xB0;
    blockLimits[2] = 0x00;
    blockLimits[3] = 0x3C;
    //wsnz bit - leave as zero since we support a value of zero to overwrite the full drive

    //max compare and write length - 0 if fused operation not supported. Otherwise less than or equal to max transfer length field
    blockLimits[5] = 0;

    //optimal transfer length granularity (unspecified) TODO: set this to something
    blockLimits[6] = 0;
    blockLimits[7] = 0;
    //maximum transfer length 
    uint32_t maxTransferLength = UINT32_MAX;
    if (device->drive_info.IdentifyData.nvme.ctrl.mdts > 0)
    {
        maxTransferLength = 1 << device->drive_info.IdentifyData.nvme.ctrl.mdts;
    }
    blockLimits[8] = M_Byte3(maxTransferLength);
    blockLimits[9] = M_Byte2(maxTransferLength);
    blockLimits[10] = M_Byte1(maxTransferLength);
    blockLimits[11] = M_Byte0(maxTransferLength);
    //optimal transfer length (unspecified....we decide)
    blockLimits[12] = M_Byte3(65536 / device->drive_info.deviceBlockSize);
    blockLimits[13] = M_Byte2(65536 / device->drive_info.deviceBlockSize);
    blockLimits[14] = M_Byte1(65536 / device->drive_info.deviceBlockSize);
    blockLimits[15] = M_Byte0(65536 / device->drive_info.deviceBlockSize);
    //maximum prefetch length (unspecified....we decide) - leave at zero since we don't support the prefetch command

    //unmap stuff
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
    {
        uint32_t unmapLBACount = UINT32_MAX;
        uint32_t unmapMaxBlockDescriptors = 256;
        //maximum unmap LBA count (unspecified....we decide)
        blockLimits[20] = M_Byte3(unmapLBACount);
        blockLimits[21] = M_Byte2(unmapLBACount);
        blockLimits[22] = M_Byte1(unmapLBACount);
        blockLimits[23] = M_Byte0(unmapLBACount);
        //maximum unmap block descriptor count (unspecified....we decide)
        blockLimits[24] = M_Byte3(unmapMaxBlockDescriptors);
        blockLimits[25] = M_Byte2(unmapMaxBlockDescriptors);
        blockLimits[26] = M_Byte1(unmapMaxBlockDescriptors);
        blockLimits[27] = M_Byte0(unmapMaxBlockDescriptors);
        //optimal unmap granularity (unspecified....we decide) - leave at zero

        //uga valid bit (unspecified....we decide) - leave at zero

        //unmap granularity alignment (unspecified....we decide) - leave at zero
    }
    //TODO: set a maximum for write same length. Currently zero since we aren't supporting the command right now
    //maximum write same length (unspecified....we decide). We will allow the full drive to be write same'd
    /*blockLimits[36] = M_Byte7(device->drive_info.deviceMaxLba);
    blockLimits[37] = M_Byte6(device->drive_info.deviceMaxLba);
    blockLimits[38] = M_Byte5(device->drive_info.deviceMaxLba);
    blockLimits[39] = M_Byte4(device->drive_info.deviceMaxLba);
    blockLimits[40] = M_Byte3(device->drive_info.deviceMaxLba);
    blockLimits[41] = M_Byte2(device->drive_info.deviceMaxLba);
    blockLimits[42] = M_Byte1(device->drive_info.deviceMaxLba);
    blockLimits[43] = M_Byte0(device->drive_info.deviceMaxLba);*/
    //maximum atomic length - leave at zero

    //atomic alignment - leave at zero

    //atomic transfer length granularity - leave at zero

    //maximum atomic transfer length with atomic boundary - leave at zero

    //maximum atomic boundary size - leave at zero

    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, blockLimits, M_Min(64, scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_Block_Device_Characteristics_VPD_Page_B1h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t blockDeviceCharacteriticsPage[64] = { 0 };
    blockDeviceCharacteriticsPage[0] = 0;
    blockDeviceCharacteriticsPage[1] = BLOCK_DEVICE_CHARACTERISTICS;
    blockDeviceCharacteriticsPage[2] = 0x00;
    blockDeviceCharacteriticsPage[3] = 0x3C;
    //rotation rate - non rotating device (SSD)
    blockDeviceCharacteriticsPage[4] = 0x00;
    blockDeviceCharacteriticsPage[5] = 0x01;
    //product type - not some kind of camera card
    blockDeviceCharacteriticsPage[6] = 0;
    //form factor - not reported
    blockDeviceCharacteriticsPage[7] = 0;
    //TODO: check if FUA in NVMe matches FUAB bit meaning and set it if it does
    //set FUAB and VBULS to 1
    //blockDeviceCharacteriticsPage[8] |= BIT1;//FUAB
    //TODO: see if this is something we can extend support for.
    //blockDeviceCharacteriticsPage[8] |= BIT0;//VBULS
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, blockDeviceCharacteriticsPage, M_Min(64, scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_Logical_Block_Provisioning_VPD_Page_B2h(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t logicalBlockProvisioning[8] = { 0 };
    logicalBlockProvisioning[0] = 0;
    logicalBlockProvisioning[1] = 0xB2;
    logicalBlockProvisioning[2] = 0x00;
    logicalBlockProvisioning[3] = 0x04;
    //threshold exponent (only non-zero if thin-provisioning is supported)
    logicalBlockProvisioning[4] = 0;
    //lbpu bit
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
    {
        logicalBlockProvisioning[5] |= BIT7;
    }
    //TODO: if we extend spec support for unmap and allow setting the unmap bit, then we should enable these next two bits
    //lbpws bit
    /*
    if (device->drive_info.IdentifyData.ata.Word169 & BIT0)
    {
    logicalBlockProvisioning[5] |= BIT6;
    }
    */
    //lbpws10 bit (set to zero since we don't support unmap during write same yet)
    /*
    if (device->drive_info.IdentifyData.ata.Word169 & BIT0)
    {
    logicalBlockProvisioning[5] |= BIT5;
    }
    */
    //lbprz
    if (M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.dlfeat, 2, 0) == 1)
    {
        logicalBlockProvisioning[5] |= BIT2;
    }
    //anc_sup //TODO: what do we do for this?
    /*if ()
    {
        logicalBlockProvisioning[5] |= BIT1;
    }*/
    //dp (set to zero since we don't support a resource descriptor)

    //provisioining type
    uint8_t provisioningType = 0;
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
    {
        if (device->drive_info.IdentifyData.nvme.ns.nsfeat & BIT0)
        {
            provisioningType = 2;//thin
        }
        else
        {
            provisioningType = 1;//resource
        }
    }
    logicalBlockProvisioning[6] = provisioningType;

    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, logicalBlockProvisioning, M_Min(8, scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_SCSI_Inquiry_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    //Check to make sure cmdDT and reserved bits aren't set
    if (scsiIoCtx->cdb[1] & 0xFE)
    {
        fieldPointer = 1;
        //One of the bits we don't support is set, so return invalid field in CDB
        uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
        uint8_t counter = 0;
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    else
    {
        //check EVPD bit
        if (scsiIoCtx->cdb[1] & BIT0)
        {
            //check the VPD page to set up that data correctly
            switch (scsiIoCtx->cdb[2])
            {
            case SUPPORTED_VPD_PAGES:
                //update this as more supported pages are added!
                ret = sntl_Translate_Supported_VPD_Pages_00h(device, scsiIoCtx);
                break;
            case UNIT_SERIAL_NUMBER:
                ret = sntl_Translate_Unit_Serial_Number_VPD_Page_80h(device, scsiIoCtx);
                break;
            case DEVICE_IDENTIFICATION:
                ret = sntl_Translate_Device_Identification_VPD_Page_83h(device, scsiIoCtx);
                break;
            case EXTENDED_INQUIRY_DATA:
                ret = sntl_Translate_Extended_Inquiry_Data_VPD_Page_86h(device, scsiIoCtx);
                break;
            //case MODE_PAGE_POLICY:
            //  ret = translate_Mode_Page_Policy_VPD_Page_87h(device, scsiIoCtx);
            //  break;
            case BLOCK_LIMITS:
                ret = sntl_Translate_Block_Limits_VPD_Page_B0h(device, scsiIoCtx);
                break;
            case BLOCK_DEVICE_CHARACTERISTICS:
                ret = sntl_Translate_Block_Device_Characteristics_VPD_Page_B1h(device, scsiIoCtx);
                break;
            case LOGICAL_BLOCK_PROVISIONING:
                ret = sntl_Translate_Logical_Block_Provisioning_VPD_Page_B2h(device, scsiIoCtx);
                break;
            default:
                ret = NOT_SUPPORTED;
                fieldPointer = 2;
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                break;
            }
        }
        else
        {
            uint8_t inquiryData[96] = { 0 };
            //standard inquiry data
            if (scsiIoCtx->cdb[2] != 0)//if page code is non-zero, we need to return an error
            {
                fieldPointer = 2;
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            //TODO: issue NVMe identify commands as we need to here.
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
            inquiryData[0] = 0;
            inquiryData[1] = 0;//not removable or a conglomerate
            //version
#if defined SNTL_EXT
            //SPC5
            inquiryData[2] = 0x07;
#else
            //SPC4
            inquiryData[2] = 0x06;
#endif
            //response format
            inquiryData[3] = 2 | BIT4;//set response format to 2 and hisup bit
            //additional length
#if defined SNTL_EXT
            inquiryData[4] = 92;
#else
            inquiryData[4] = 0x1F; 
#endif
            //check if protect bit needs to be set from namespace data
            if (device->drive_info.IdentifyData.nvme.ns.dps != 0)
            {
                inquiryData[5] = BIT0;
            }
            else
            {
                inquiryData[5] = 0;
            }
            //set cmdque bit
            inquiryData[7] = BIT1;
            //vendorID
            inquiryData[8] = 'N';
            inquiryData[9] = 'V';
            inquiryData[10] = 'M';
            inquiryData[11] = 'e';
            inquiryData[12] = ' ';
            inquiryData[13] = ' ';
            inquiryData[14] = ' ';
            inquiryData[15] = ' ';
            //Product ID (first 16bytes of the ata model number
            char nvmMN[MODEL_NUM_LEN + 1] = { 0 };
            memcpy(nvmMN, device->drive_info.IdentifyData.nvme.ctrl.mn, MODEL_NUM_LEN);
            memcpy(&inquiryData[16], nvmMN, 16);
            //product revision (truncates to 4 bytes)
            char nvmFW[FW_REV_LEN] = { 0 };
            memcpy(nvmFW, device->drive_info.IdentifyData.nvme.ctrl.fr, 8);
            remove_Leading_And_Trailing_Whitespace(nvmFW);
            if (strlen(nvmFW) > 4)
            {
                memcpy(&inquiryData[32], &nvmFW[4], 4);
            }
            else
            {
                memcpy(&inquiryData[32], &nvmFW[0], 4);
            }

            //currently this is where the translation spec ends. Anything below here is above and beyond the spec
#if defined SNTL_EXT
            //Vendor specific...we'll set the controller SN here
            char nvmSN[SERIAL_NUM_LEN + 1] = { 0 };
            memcpy(nvmSN, device->drive_info.IdentifyData.nvme.ctrl.sn, SERIAL_NUM_LEN);
            remove_Leading_And_Trailing_Whitespace(nvmSN);
            memcpy(&inquiryData[36], nvmSN, M_Min(strlen(nvmSN), 20));

            //version descriptors (bytes 58 to 73) (8 max)
            uint16_t versionOffset = 58;
            //SAM5
            inquiryData[versionOffset] = 0x00;
            inquiryData[versionOffset + 1] = 0xA0;
            versionOffset += 2;
            //SPC4
            inquiryData[versionOffset] = 0x04;
            inquiryData[versionOffset + 1] = 0x60;
            versionOffset += 2;
            //SBC3
            inquiryData[versionOffset] = 0x04;
            inquiryData[versionOffset + 1] = 0xC0;
            versionOffset += 2;
            //TODO: should we say we conform to these newer specifications?
            ////SAM6 - 
            //inquiryData[versionOffset] = 0x00;
            //inquiryData[versionOffset + 1] = 0xC0;
            //versionOffset += 2;
            ////SPC5 - 05C0h
            //inquiryData[versionOffset] = 0x05;
            //inquiryData[versionOffset + 1] = 0xC0;
            //versionOffset += 2;
            ////SBC4 - 0600h
            //inquiryData[versionOffset] = 0x06;
            //inquiryData[versionOffset + 1] = 0x00;
            //versionOffset += 2;
            //If zoned, ZBC/ZAC spec 0620h
            //Transport needs to go here...pcie?
#endif
            //now copy the data back
            if (scsiIoCtx->pdata && scsiIoCtx->dataLength > 0)
            {
                memcpy(scsiIoCtx->pdata, inquiryData, M_Min(96, scsiIoCtx->dataLength));
            }
        }
    }
    return ret;
}

int sntl_Translate_SCSI_Read_Capacity_Command(tDevice *device, bool readCapacity16, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint16_t fieldPointer = 0;
    uint8_t bitPointer = 0;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    //Check that reserved and obsolete bits aren't set
    if (readCapacity16)
    {
        //16byte field filter
        if ((fieldPointer = 1 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5) != 0)
            || (fieldPointer = 2 && scsiIoCtx->cdb[2] != 0)
            || (fieldPointer = 3 && scsiIoCtx->cdb[3] != 0)
            || (fieldPointer = 4 && scsiIoCtx->cdb[4] != 0)
            || (fieldPointer = 5 && scsiIoCtx->cdb[5] != 0)
            || (fieldPointer = 6 && scsiIoCtx->cdb[6] != 0)
            || (fieldPointer = 7 && scsiIoCtx->cdb[7] != 0)
            || (fieldPointer = 8 && scsiIoCtx->cdb[8] != 0)
            || (fieldPointer = 9 && scsiIoCtx->cdb[9] != 0)
            || (fieldPointer = 14 && scsiIoCtx->cdb[14] != 0)
            )
        {
            //invalid field in CDB
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
            return ret;
        }
    }
    else
    {
        //10 byte field filter
        if ((fieldPointer = 1 && scsiIoCtx->cdb[1] != 0)
            || (fieldPointer = 2 && scsiIoCtx->cdb[2] != 0)
            || (fieldPointer = 3 && scsiIoCtx->cdb[3] != 0)
            || (fieldPointer = 4 && scsiIoCtx->cdb[4] != 0)
            || (fieldPointer = 5 && scsiIoCtx->cdb[5] != 0)
            || (fieldPointer = 6 && scsiIoCtx->cdb[6] != 0)
            || (fieldPointer = 7 && scsiIoCtx->cdb[7] != 0)
            || (fieldPointer = 8 && scsiIoCtx->cdb[8] != 0)
            )
        {
            //invalid field in CDB
            ret = NOT_SUPPORTED;
            bitPointer = 7;
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return ret;
        }
    }
    if (scsiIoCtx->pdata)
    {
        uint64_t maxLBA = device->drive_info.IdentifyData.nvme.ns.nsze - 1;
        uint8_t flbas = M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.flbas, 3, 0);
        uint32_t logicalSectorSize = (uint32_t)power_Of_Two(device->drive_info.IdentifyData.nvme.ns.lbaf[flbas].lbaDS);
        //set the data in the buffer
        if (readCapacity16)
        {
            uint8_t readCapacityData[32] = { 0 };
            readCapacityData[0] = M_Byte7(maxLBA);
            readCapacityData[1] = M_Byte6(maxLBA);
            readCapacityData[2] = M_Byte5(maxLBA);
            readCapacityData[3] = M_Byte4(maxLBA);
            readCapacityData[4] = M_Byte3(maxLBA);
            readCapacityData[5] = M_Byte2(maxLBA);
            readCapacityData[6] = M_Byte1(maxLBA);
            readCapacityData[7] = M_Byte0(maxLBA);
            readCapacityData[8] = M_Byte3(logicalSectorSize);
            readCapacityData[9] = M_Byte2(logicalSectorSize);
            readCapacityData[10] = M_Byte1(logicalSectorSize);
            readCapacityData[11] = M_Byte0(logicalSectorSize);
            //sector size exponent
            readCapacityData[13] = 0;
            //set the alignment first
            readCapacityData[14] = 0;
            readCapacityData[15] = 0;
            //now bits related to provisioning and deallocation
            if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2 && device->drive_info.IdentifyData.nvme.ns.nsfeat & BIT0)//supports provisioning...did this like how we get provisioning type in logical block provisioining page
            {
                readCapacityData[14] |= BIT7;
            }
            if (M_GETBITRANGE(device->drive_info.IdentifyData.nvme.ns.dlfeat, 2, 0) == 1)// deallocation reads return zero
            {
                readCapacityData[14] |= BIT6;
            }
            //remaining bytes are reserved

            if (scsiIoCtx->pdata)
            {
                memcpy(scsiIoCtx->pdata, readCapacityData, M_Min(32, scsiIoCtx->dataLength));
            }
        }
        else
        {
            //data length is required by spec so return an error if we don't have at least that length
            if (scsiIoCtx->dataLength < 8 || !scsiIoCtx->pdata)
            {
                return MEMORY_FAILURE;
            }
            if (maxLBA > UINT32_MAX)
            {
                scsiIoCtx->pdata[0] = 0xFF;
                scsiIoCtx->pdata[1] = 0xFF;
                scsiIoCtx->pdata[2] = 0xFF;
                scsiIoCtx->pdata[3] = 0xFF;
            }
            else
            {
                scsiIoCtx->pdata[0] = M_Byte3(maxLBA);
                scsiIoCtx->pdata[1] = M_Byte2(maxLBA);
                scsiIoCtx->pdata[2] = M_Byte1(maxLBA);
                scsiIoCtx->pdata[3] = M_Byte0(maxLBA);
            }
            scsiIoCtx->pdata[4] = M_Byte3(logicalSectorSize);
            scsiIoCtx->pdata[5] = M_Byte2(logicalSectorSize);
            scsiIoCtx->pdata[6] = M_Byte1(logicalSectorSize);
            scsiIoCtx->pdata[7] = M_Byte0(logicalSectorSize);
        }
    }
    return ret;
}

int sntl_Translate_Supported_Log_Pages(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    bool subpageFormat = false;
    uint8_t supportedPages[LEGACY_DRIVE_SEC_SIZE] = { 0 };//this should be plenty big for now
    uint8_t offset = 4;
    uint8_t increment = 1;
    if (scsiIoCtx->cdb[3] == 0xFF)
    {
        subpageFormat = true;
        increment = 2;
    }
    if (subpageFormat)
    {
        supportedPages[0] |= BIT6;//set the subpage format bit
        supportedPages[1] = 0xFF;
    }
    else
    {
        supportedPages[0] = 0;
        supportedPages[1] = 0;
    }
    //Set supported page page
    supportedPages[offset] = LP_SUPPORTED_LOG_PAGES;
    offset += increment;
    if (subpageFormat)
    {
        //set supported pages and subpages
        supportedPages[offset] = 0;
        supportedPages[offset + 1] = 0xFF;
        offset += increment;
    }
    //temperature log
    supportedPages[offset] = LP_TEMPERATURE;
    offset += increment;
    ////If smart self test is supported, add the self test results log (10h)
    //if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT4)
    //{
    //    supportedPages[offset] = LP_SELF_TEST_RESULTS;
    //    offset += increment;
    //}
    //solid state media
    supportedPages[offset] = LP_SOLID_STATE_MEDIA;
    offset += increment;
#if defined (SNTL_EXT)
    //background scan results 
    supportedPages[offset] = LP_BACKGROUND_SCAN_RESULTS;
    offset += increment;
    //general statistics and performance
    supportedPages[offset] = LP_GENERAL_STATISTICS_AND_PERFORMANCE;
    offset += increment;
#endif
    //TODO: add logs

    //if smart is supported, add informational exceptions log page (2Fh)
    supportedPages[offset] = LP_INFORMATION_EXCEPTIONS;
    offset += increment;
    //TODO: add logs

    //set the page length (Do this last)
    supportedPages[2] = M_Byte1(offset - 4);
    supportedPages[3] = M_Byte0(offset - 4);
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, supportedPages, M_Min(scsiIoCtx->dataLength, (uint16_t)M_Min(LEGACY_DRIVE_SEC_SIZE, (uint16_t)offset)));
    }
    return ret;
}

int sntl_Translate_Temperature_Log_0x0D(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t temperatureLog[16] = { 0 };
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t offset = 4;
    uint8_t logPage[512] = { 0 };
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    nvmeGetLogPageCmdOpts getSMARTHealthData;
    memset(&getSMARTHealthData, 0, sizeof(nvmeGetLogPageCmdOpts));
    getSMARTHealthData.addr = logPage;
    getSMARTHealthData.dataLen = 512;
    getSMARTHealthData.lid = 2;//smart / health log page
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT0)
    {
        //request the page for the current namespace
        getSMARTHealthData.nsid = device->drive_info.namespaceID;
    }
    else
    {
        //request for controller wide data
        getSMARTHealthData.nsid = UINT32_MAX;//or zero?
    }
    if (parameterPointer > 1)
    {
        fieldPointer = 5;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    temperatureLog[0] = 0x0D;
    temperatureLog[1] = 0x00;
    if (parameterPointer <= 0)
    {
        if (SUCCESS != nvme_Get_Log_Page(device, &getSMARTHealthData))
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return FAILURE;
        }
        uint16_t currentTempK = M_BytesTo2ByteValue(logPage[2], logPage[1]);
        //current temp
        temperatureLog[offset + 0] = 0;
        temperatureLog[offset + 1] = 0;
        temperatureLog[offset + 2] = 0x03;//format and linking = 11b
        temperatureLog[offset + 3] = 0x02;//length
        temperatureLog[offset + 4] = RESERVED;
        temperatureLog[offset + 5] = (uint8_t)(currentTempK - 273);
        offset += 6;
    }
    if (parameterPointer <= 1)
    {
        //reference temp (get temperature threshold from get features)
        nvmeFeaturesCmdOpt getTempThresh;
        memset(&getTempThresh, 0, sizeof(nvmeFeaturesCmdOpt));
        memset(logPage, 0, 512);
        getTempThresh.fid = 0x04;//temperature threshold
        getTempThresh.prp1 = (uintptr_t)logPage;
        getTempThresh.featSetGetValue = 0;
        if(SUCCESS == nvme_Get_Features(device, &getTempThresh))
        {
            uint16_t tempThreshK = (uint16_t)getTempThresh.featSetGetValue;
            temperatureLog[offset + 0] = 0;
            temperatureLog[offset + 1] = 1;
            temperatureLog[offset + 2] = 0x03;//format and linking = 11b
            temperatureLog[offset + 3] = 0x02;
            temperatureLog[offset + 4] = RESERVED;
            temperatureLog[offset + 5] = (uint8_t)(tempThreshK - 273);
            offset += 6;
        }
        else
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return FAILURE;
        }
    }
    //set page length at the end
    temperatureLog[2] = M_Byte1(offset - 4);
    temperatureLog[3] = M_Byte0(offset - 4);
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, temperatureLog, M_Min(M_Max(16U, offset), scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_Solid_State_Media_Log_0x11(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t solidStateMediaLog[12] = { 0 };
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t offset = 4;
    uint8_t logPage[512] = { 0 };
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    nvmeGetLogPageCmdOpts getSMARTHealthData;
    memset(&getSMARTHealthData, 0, sizeof(nvmeGetLogPageCmdOpts));
    getSMARTHealthData.addr = logPage;
    getSMARTHealthData.dataLen = 512;
    getSMARTHealthData.lid = 2;//smart / health log page
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT0)
    {
        //request the page for the current namespace
        getSMARTHealthData.nsid = device->drive_info.namespaceID;
    }
    else
    {
        //request for controller wide data
        getSMARTHealthData.nsid = UINT32_MAX;//or zero?
    }
    if (parameterPointer > 1)
    {
        fieldPointer = 5;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    solidStateMediaLog[0] = 0x11;
    solidStateMediaLog[1] = 0x00;
    if (parameterPointer <= 1)
    {
        //endurance
        if (SUCCESS != nvme_Get_Log_Page(device, &getSMARTHealthData))
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return FAILURE;
        }
        solidStateMediaLog[offset + 0] = 0x00;
        solidStateMediaLog[offset + 1] = 0x01;
        solidStateMediaLog[offset + 2] = 0x03;//format and linking = 11b
        solidStateMediaLog[offset + 3] = 0x04;
        solidStateMediaLog[offset + 4] = RESERVED;
        solidStateMediaLog[offset + 5] = RESERVED;
        solidStateMediaLog[offset + 6] = RESERVED;
        solidStateMediaLog[offset + 7] = logPage[5];
        offset += 8;
    }
    //set page length at the end
    solidStateMediaLog[2] = M_Byte1(offset - 4);
    solidStateMediaLog[3] = M_Byte0(offset - 4);
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, solidStateMediaLog, M_Min(M_Min(12U, offset), scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_Informational_Exceptions_Log_Page_2F(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t informationalExceptions[11] = { 0 };
    uint8_t logPage[512] = { 0 };
    nvmeGetLogPageCmdOpts getSMARTHealthData;
    memset(&getSMARTHealthData, 0, sizeof(nvmeGetLogPageCmdOpts));
    getSMARTHealthData.addr = logPage;
    getSMARTHealthData.dataLen = 512;
    getSMARTHealthData.lid = 2;//smart / health log page
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT0)
    {
        //request the page for the current namespace
        getSMARTHealthData.nsid = device->drive_info.namespaceID;
    }
    else
    {
        //request for controller wide data
        getSMARTHealthData.nsid = UINT32_MAX;//or zero?
    }
    if (SUCCESS != nvme_Get_Log_Page(device, &getSMARTHealthData))
    {
        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    //set the header data
    informationalExceptions[0] = 0x2F;
    informationalExceptions[1] = 0x00;
    informationalExceptions[2] = 0x00;
    informationalExceptions[3] = 0x07;
    //start setting the remaining page data
    informationalExceptions[4] = 0;
    informationalExceptions[5] = 0;
    informationalExceptions[6] = 0x03 | BIT5;//sntl says to set the TSD bit to 1
    informationalExceptions[7] = 0x03;
    //TODO: if a critical warning bit is set, should we return failure???
    //if ()
    //{
    //    //fail status (Only set this when we actually know it was a failure)
    //    informationalExceptions[8] = 0x5D;
    //    informationalExceptions[9] = 0x10;
    //}
    //else
    {
        //pass status
        informationalExceptions[8] = 0x00;
        informationalExceptions[9] = 0x00;
    }
    //set temperature reading
    informationalExceptions[10] = M_BytesTo2ByteValue(logPage[2], logPage[1]) - 273;
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, informationalExceptions, M_Min(11U, scsiIoCtx->dataLength));
    }
    return ret;
}

#if defined (SNTL_EXT)
int sntl_Translate_Background_Scan_Results_Log_0x15(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t backgroundResults[20] = { 0 };
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t offset = 4;
    uint8_t logPage[512] = { 0 };
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    if (parameterPointer > 0)
    {
        fieldPointer = 5;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    nvmeGetLogPageCmdOpts readSmartLog;
    memset(&readSmartLog, 0, sizeof(nvmeGetLogPageCmdOpts));
    readSmartLog.addr = logPage;
    readSmartLog.dataLen = 512;
    readSmartLog.lid = NVME_LOG_SMART_ID;
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT0)
    {
        //request the page for the current namespace
        readSmartLog.nsid = device->drive_info.namespaceID;
    }
    else
    {
        //request for controller wide data
        readSmartLog.nsid = UINT32_MAX;//or zero?
    }
    if (SUCCESS != nvme_Get_Log_Page(device, &readSmartLog))
    {
        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    backgroundResults[0] = 0x11;
    backgroundResults[1] = 0x00;
    if (parameterPointer <= 0)
    {
        //poh
        uint64_t pohMinutes = 0;
        double nvmePOH = convert_128bit_to_double(&logPage[128]);
        if ((nvmePOH * 60.0) >= UINT64_MAX)
        {
            pohMinutes = UINT64_MAX;
        }
        else
        {
            pohMinutes = (uint64_t)(60 * nvmePOH);
        }
        backgroundResults[offset + 0] = 0x00;
        backgroundResults[offset + 1] = 0x00;
        backgroundResults[offset + 2] = 0x03;//format and linking = 11b
        backgroundResults[offset + 3] = 0x0C;
        backgroundResults[offset + 4] = M_Byte3(pohMinutes);
        backgroundResults[offset + 5] = M_Byte2(pohMinutes);
        backgroundResults[offset + 6] = M_Byte1(pohMinutes);
        backgroundResults[offset + 7] = M_Byte0(pohMinutes);
        backgroundResults[offset + 8] = RESERVED;
        backgroundResults[offset + 9] = 0;//background scan status
        backgroundResults[offset + 10] = 0;//background scans performed
        backgroundResults[offset + 11] = 0;
        backgroundResults[offset + 12] = 0;//background scan progress
        backgroundResults[offset + 13] = 0;
        backgroundResults[offset + 14] = 0;//number of background medium scans performed
        backgroundResults[offset + 15] = 0;
        offset += 8;
    }
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, backgroundResults, M_Min(20U, scsiIoCtx->dataLength));
    }
    return ret;
}

int sntl_Translate_General_Statistics_And_Performance_Log_0x19(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t generalStatisticsAndPerformance[72] = { 0 };
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t offset = 4;
    uint8_t logPage[512] = { 0 };
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    if (parameterPointer > 1)
    {
        fieldPointer = 5;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    nvmeGetLogPageCmdOpts readSmartLog;
    memset(&readSmartLog, 0, sizeof(nvmeGetLogPageCmdOpts));
    readSmartLog.addr = logPage;
    readSmartLog.dataLen = 512;
    readSmartLog.lid = NVME_LOG_SMART_ID;
    if (device->drive_info.IdentifyData.nvme.ctrl.lpa & BIT0)
    {
        //request the page for the current namespace
        readSmartLog.nsid = device->drive_info.namespaceID;
    }
    else
    {
        //request for controller wide data
        readSmartLog.nsid = UINT32_MAX;//or zero?
    }
    if (SUCCESS != nvme_Get_Log_Page(device, &readSmartLog))
    {
        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
        return FAILURE;
    }
    generalStatisticsAndPerformance[0] = 0x19;
    generalStatisticsAndPerformance[1] = 0x00;
    if (parameterPointer <= 1)
    {
        generalStatisticsAndPerformance[offset + 0] = 0x00;
        generalStatisticsAndPerformance[offset + 1] = 0x01;
        generalStatisticsAndPerformance[offset + 2] = 0x03;//format and linking = 11b
        generalStatisticsAndPerformance[offset + 3] = 0x40;
        //Number of read Commands
        {
            double nvmeReads = convert_128bit_to_double(&logPage[64]);
            uint64_t numberReads = 0;
            if (nvmeReads >= UINT64_MAX)
            {
                numberReads = UINT64_MAX;
            }
            else
            {
                numberReads = M_BytesTo8ByteValue(logPage[71], logPage[70], logPage[69], logPage[68], logPage[67], logPage[66], logPage[65], logPage[64]);
            }
            generalStatisticsAndPerformance[offset + 4] = M_Byte7(numberReads);
            generalStatisticsAndPerformance[offset + 5] = M_Byte6(numberReads);
            generalStatisticsAndPerformance[offset + 6] = M_Byte5(numberReads);
            generalStatisticsAndPerformance[offset + 7] = M_Byte4(numberReads);
            generalStatisticsAndPerformance[offset + 8] = M_Byte3(numberReads);
            generalStatisticsAndPerformance[offset + 9] = M_Byte2(numberReads);
            generalStatisticsAndPerformance[offset + 10] = M_Byte1(numberReads);
            generalStatisticsAndPerformance[offset + 11] = M_Byte0(numberReads);
        }
        //number of write commands
        {
            double nvmeWrites = convert_128bit_to_double(&logPage[80]);
            uint64_t numberWrites = 0;
            if (nvmeWrites >= UINT64_MAX)
            {
                numberWrites = UINT64_MAX;
            }
            else
            {
                numberWrites = M_BytesTo8ByteValue(logPage[87], logPage[86], logPage[85], logPage[84], logPage[83], logPage[82], logPage[81], logPage[80]);
            }
            generalStatisticsAndPerformance[offset + 12] = M_Byte7(numberWrites);
            generalStatisticsAndPerformance[offset + 13] = M_Byte6(numberWrites);
            generalStatisticsAndPerformance[offset + 14] = M_Byte5(numberWrites);
            generalStatisticsAndPerformance[offset + 15] = M_Byte4(numberWrites);
            generalStatisticsAndPerformance[offset + 16] = M_Byte3(numberWrites);
            generalStatisticsAndPerformance[offset + 17] = M_Byte2(numberWrites);
            generalStatisticsAndPerformance[offset + 18] = M_Byte1(numberWrites);
            generalStatisticsAndPerformance[offset + 19] = M_Byte0(numberWrites);
        }
        //number of logical blocks received
        {
            double nvmeWritesInLBAs = (convert_128bit_to_double(&logPage[48]) * 1000 * 512) / device->drive_info.deviceBlockSize;
            uint64_t numLogBlocksWritten = 0;
            if (nvmeWritesInLBAs >= UINT64_MAX)
            {
                numLogBlocksWritten = UINT64_MAX;
            }
            else
            {
                numLogBlocksWritten = (uint64_t)nvmeWritesInLBAs;
            }
            generalStatisticsAndPerformance[offset + 20] = M_Byte7(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 21] = M_Byte6(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 22] = M_Byte5(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 23] = M_Byte4(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 24] = M_Byte3(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 25] = M_Byte2(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 26] = M_Byte1(numLogBlocksWritten);
            generalStatisticsAndPerformance[offset + 27] = M_Byte0(numLogBlocksWritten);
        }
        //number of logical blocks transmitted
        {
            double nvmeReadsInLBAs = (convert_128bit_to_double(&logPage[32]) * 1000 * 512) / device->drive_info.deviceBlockSize;
            uint64_t numLogBlocksRead = 0;
            if (nvmeReadsInLBAs >= UINT64_MAX)
            {
                numLogBlocksRead = UINT64_MAX;
            }
            else
            {
                numLogBlocksRead = (uint64_t)nvmeReadsInLBAs;
            }
            generalStatisticsAndPerformance[offset + 28] = M_Byte7(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 29] = M_Byte6(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 30] = M_Byte5(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 31] = M_Byte4(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 32] = M_Byte3(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 33] = M_Byte2(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 34] = M_Byte1(numLogBlocksRead);
            generalStatisticsAndPerformance[offset + 35] = M_Byte0(numLogBlocksRead);
        }
        //all other fields to be set to zero since translation is unspecified - TJE
        offset += 68;
    }
    //set page length at the end
    generalStatisticsAndPerformance[2] = M_Byte1(offset - 4);
    generalStatisticsAndPerformance[3] = M_Byte0(offset - 4);
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, generalStatisticsAndPerformance, M_Min(M_Min(72U, offset), scsiIoCtx->dataLength));
    }
    return ret;
}
#endif

int sntl_Translate_SCSI_Log_Sense_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    //we ignore the sp bit since it doesn't matter to us
    uint8_t pageControl = (scsiIoCtx->cdb[2] & 0xC0) >> 6;
    uint8_t pageCode = scsiIoCtx->cdb[2] & 0x3F;
    uint8_t subpageCode = scsiIoCtx->cdb[3];
    uint16_t parameterPointer = M_BytesTo2ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6]);
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out unsupported bits
    if (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 1) != 0)
        || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    else
    {
        //uint16_t allocationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (pageControl == LPC_CUMULATIVE_VALUES)
        {
            //check the pagecode
            switch (pageCode)
            {
            case 0x00://supported pages
                switch (subpageCode)
                {
                case 0://supported pages
                case 0xFF://supported pages and subpages
                    ret = sntl_Translate_Supported_Log_Pages(device, scsiIoCtx);//Update this page as additional pages of support are added!
                    break;
                default:
                    fieldPointer = 3;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            case 0x0D://temperature
                switch (subpageCode)
                {
                case 0:
                    ret = sntl_Translate_Temperature_Log_0x0D(device, scsiIoCtx);
                    break;
                default:
                    fieldPointer = 3;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            //TODO: add in support for this page when we can also issue DST commands through send diagnostic
            //case LP_SELF_TEST_RESULTS://self test results
            //  switch (subpageCode)
            //  {
            //  case 0:
            //      if (device->drive_info.IdentifyData.ata.Word084 & BIT1 || device->drive_info.IdentifyData.ata.Word087 & BIT1)
            //      {
            //          ret = translate_Self_Test_Results_Log_0x10(device, scsiIoCtx);
            //      }
            //      else
            //      {
            //          fieldPointer = 2;
            //          bitPointer = 5;
            //          sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            //          ret = NOT_SUPPORTED;
            //          sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            //      }
            //      break;
            //  default:
            //      fieldPointer = 3;
            //      bitPointer = 7;
            //      sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            //      ret = NOT_SUPPORTED;
            //      sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            //      break;
            //  }
            //  break;
            case 0x11://solid state media
                switch (subpageCode)
                {
                case 0:
                    ret = sntl_Translate_Solid_State_Media_Log_0x11(device, scsiIoCtx);
                    break;
                default:
                    fieldPointer = 3;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            //add this page in to report POH
#if defined (SNTL_EXT)
            case 0x15://background scan (for POH)
                switch (subpageCode)
                {
                case 0:
                    ret = sntl_Translate_Background_Scan_Results_Log_0x15(device, scsiIoCtx);
                    break;
                default:
                    fieldPointer = 3;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
            //add this to report total reads/writes etc
            case 0x19://general statistics and performance
                switch (subpageCode)
                {
                case 0:
                    ret = sntl_Translate_General_Statistics_And_Performance_Log_0x19(device, scsiIoCtx);
                    break;
                default:
                    fieldPointer = 3;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
                break;
#endif
            case 0x2F://Informational Exceptions
                if (subpageCode == 0)
                {
                    if (parameterPointer == 0)
                    {
                        ret = sntl_Translate_Informational_Exceptions_Log_Page_2F(device, scsiIoCtx);
                    }
                    else
                    {
                        fieldPointer = 5;
                        bitPointer = 7;
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                        ret = NOT_SUPPORTED;
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    }
                }
                else
                {
                    fieldPointer = 3;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
                break;
            default:
                fieldPointer = 2;
                bitPointer = 5;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                break;
            }
        }
        else //page control
        {
            fieldPointer = 2;
            bitPointer = 7;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        }
    }
    return ret;
}

//mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
//dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for short, or 16 for long (when longLBABit is set to true)
int sntl_Translate_Mode_Sense_Read_Write_Error_Recovery_01h(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t pageControl, bool returnDataBlockDescriptor, bool longLBABit, uint8_t *dataBlockDescriptor, bool longHeader, uint8_t *modeParameterHeader, uint16_t allocationLength)
{
    int ret = SUCCESS;
    uint8_t *readWriteErrorRecovery = NULL;//will be allocated later
    uint16_t pageLength = 12;//add onto this depending on the length of the header and block descriptors
    uint16_t offset = 0;//used later when we start setting data in the buffer since we need to account for mode parameter header and DBDs
    uint8_t headerLength = 4;
    uint8_t blockDescLength = 0;
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    //now that we know how many bytes we need for this, allocate memory
    readWriteErrorRecovery = (uint8_t*)calloc(pageLength, sizeof(uint8_t));
    if (!readWriteErrorRecovery)
    {
        //TODO: set an error in the sense data
        return MEMORY_FAILURE;
    }
    //copy header into place
    memcpy(&readWriteErrorRecovery[0], modeParameterHeader, headerLength);
    //copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        memcpy(&readWriteErrorRecovery[headerLength], modeParameterHeader, blockDescLength);
    }
    //set the remaining part of the page up
    readWriteErrorRecovery[offset + 0] = 0x01;//page number
    readWriteErrorRecovery[offset + 1] = 0x0A;//page length
    if (pageControl != 0x1)//default, saved, and current pages.
    {
        readWriteErrorRecovery[offset + 2] = BIT7 | BIT6;//awre = 1, arre = 1, tb = 0, rc = 0, eer = 0, per = 0, dte = 0, dcr = 0
        readWriteErrorRecovery[offset + 3] = 0;//read retry count (since we only issue 1 read command)
        readWriteErrorRecovery[offset + 4] = OBSOLETE;
        readWriteErrorRecovery[offset + 5] = OBSOLETE;
        readWriteErrorRecovery[offset + 6] = OBSOLETE;
        readWriteErrorRecovery[offset + 7] = 0;//lbpre = 0
        readWriteErrorRecovery[offset + 8] = 0;//write retry count (since we only issue 1 write command)
        readWriteErrorRecovery[offset + 9] = RESERVED;
        //read the current recovery timer value
        nvmeFeaturesCmdOpt getErrRecTime;
        memset(&getErrRecTime, 0, sizeof(nvmeFeaturesCmdOpt));
        getErrRecTime.fid = 0x05;
        getErrRecTime.sel = 0;
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT4)
        {
            switch (pageControl)
            {
            case MPC_CHANGABLE_VALUES:
                getErrRecTime.sel = 3;
                break;
            case MPC_CURRENT_VALUES:
                getErrRecTime.sel = 0;
                break;
            case MPC_DEFAULT_VALUES:
                getErrRecTime.sel = 1;
                break;
            case MPC_SAVED_VALUES:
                getErrRecTime.sel = 2;
                break;
            }
        }
        if (SUCCESS == nvme_Get_Features(device, &getErrRecTime))
        {
            uint32_t recoveryTime = getErrRecTime.featSetGetValue * 100;//value is reported in 100ms units
            if (recoveryTime > UINT16_MAX)
            {
                readWriteErrorRecovery[offset + 10] = 0xFF;//recovery time limit
                readWriteErrorRecovery[offset + 11] = 0xFF;//recovery time limit
            }
            else
            {
                readWriteErrorRecovery[offset + 10] = M_Byte1(recoveryTime);//recovery time limit
                readWriteErrorRecovery[offset + 11] = M_Byte0(recoveryTime);//recovery time limit
            }
        }
        else
        {
            safe_Free(readWriteErrorRecovery);
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
    }
    else
    {
        //changable values....only allow changes to the recovery time limit
        readWriteErrorRecovery[offset + 10] = 0xFF;//recovery time limit
        readWriteErrorRecovery[offset + 11] = 0xFF;//recovery time limit
    }
    //set the mode data length
    if (longHeader)
    {
        readWriteErrorRecovery[0] = M_Byte1(pageLength - 2);
        readWriteErrorRecovery[1] = M_Byte0(pageLength - 2);
    }
    else
    {
        readWriteErrorRecovery[0] = pageLength - 1;
    }
    //now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, readWriteErrorRecovery, M_Min(pageLength, allocationLength));
    }
    safe_Free(readWriteErrorRecovery);
    return ret;
}

//mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
//dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for short, or 16 for long (when longLBABit is set to true)
int sntl_Translate_Mode_Sense_Caching_08h(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t pageControl, bool returnDataBlockDescriptor, bool longLBABit, uint8_t *dataBlockDescriptor, bool longHeader, uint8_t *modeParameterHeader, uint16_t allocationLength)
{
    int ret = SUCCESS;
    uint8_t *caching = NULL;//will be allocated later
    uint16_t pageLength = 20;//add onto this depending on the length of the header and block descriptors
    uint16_t offset = 0;//used later when we start setting data in the buffer since we need to account for mode parameter header and DBDs
    uint8_t headerLength = 4;
    uint8_t blockDescLength = 0;
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    //now that we know how many bytes we need for this, allocate memory
    caching = (uint8_t*)calloc(pageLength, sizeof(uint8_t));
    if (!caching)
    {
        //TODO: set an error in the sense data
        return MEMORY_FAILURE;
    }
    //copy header into place
    memcpy(&caching[0], modeParameterHeader, headerLength);
    //copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        memcpy(&caching[headerLength], modeParameterHeader, blockDescLength);
    }
    //set the remaining part of the page up
    //send an identify command to get up to date read/write cache info
    caching[offset + 0] = 0x08;//page number
    caching[offset + 1] = 0x12;//page length
    if (pageControl == 0x1)//changeable
    {
        //check if write cache is supported
        if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)
        {
            //TODO: if sel field of get features is supported, send that command to query if VWC is changeable?
            caching[offset + 2] = BIT2;
        }
    }
    else//saved, current, and default.
    {
        if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)
        {
            //send get features command
            nvmeFeaturesCmdOpt getVWC;
            memset(&getVWC, 0, sizeof(nvmeFeaturesCmdOpt));
            getVWC.fid = 0x06;
            getVWC.sel = 0;
            if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT4)
            {
                switch (pageControl)
                {
                case MPC_CHANGABLE_VALUES:
                    getVWC.sel = 3;
                    break;
                case MPC_CURRENT_VALUES:
                    getVWC.sel = 0;
                    break;
                case MPC_DEFAULT_VALUES:
                    getVWC.sel = 1;
                    break;
                case MPC_SAVED_VALUES:
                    getVWC.sel = 2;
                    break;
                }
            }
            if (SUCCESS == nvme_Get_Features(device, &getVWC))
            {
                if (getVWC.featSetGetValue & BIT0)
                {
                    caching[offset + 2] = BIT2;//ic = 0, abpf = 0, cap = 0, disc = 0, size = 0, wce = 1, mf = 0, rcd = 0
                }
            }
            else
            {
                //TODO: set an error...even though this shouldn't happen
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                safe_Free(caching);
                return ret;
            }
        }
        else
        {
            //wce not supported, so therefore not enabled either
            caching[offset + 2] = 0;//ic = 0, abpf = 0, cap = 0, disc = 0, size = 0, wce = 0, mf = 0, rcd = 0
        }
    }
    caching[offset + 3] = 0;//demand read/write retention priorities
    caching[offset + 4] = 0;//disable pre-fetch transfer length
    caching[offset + 5] = 0;
    caching[offset + 6] = 0;//minimum pre-fetch
    caching[offset + 7] = 0;
    caching[offset + 8] = 0;//maximum pre-fetch
    caching[offset + 9] = 0;
    caching[offset + 10] = 0;//maximum pre-fetch ceiling
    caching[offset + 11] = 0;
    caching[offset + 12] = 0;//dra not supported on nvme
    caching[offset + 13] = 0;//num cache segments
    caching[offset + 14] = 0;//cache segment size
    caching[offset + 15] = 0;//cache segment size
    caching[offset + 16] = RESERVED;
    caching[offset + 17] = OBSOLETE;
    caching[offset + 18] = OBSOLETE;
    caching[offset + 19] = OBSOLETE;
    //set the mode data length
    if (longHeader)
    {
        caching[0] = M_Byte1(pageLength - 2);
        caching[1] = M_Byte0(pageLength - 2);
    }
    else
    {
        caching[0] = pageLength - 1;
    }
    //now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, caching, M_Min(pageLength, allocationLength));
    }
    safe_Free(caching);
    return ret;
}

//mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
//dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for short, or 16 for long (when longLBABit is set to true)
int sntl_Translate_Mode_Sense_Control_0Ah(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t pageControl, bool returnDataBlockDescriptor, bool longLBABit, uint8_t *dataBlockDescriptor, bool longHeader, uint8_t *modeParameterHeader, uint16_t allocationLength)
{
    int ret = SUCCESS;
    uint8_t *controlPage = NULL;//will be allocated later
    uint16_t pageLength = 12;//add onto this depending on the length of the header and block descriptors
    uint16_t offset = 0;//used later when we start setting data in the buffer since we need to account for mode parameter header and DBDs
    uint8_t headerLength = 4;
    uint8_t blockDescLength = 0;
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    //now that we know how many bytes we need for this, allocate memory
    controlPage = (uint8_t*)calloc(pageLength, sizeof(uint8_t));
    if (!controlPage)
    {
        //TODO: set an error in the sense data
        return MEMORY_FAILURE;
    }
    //copy header into place
    memcpy(&controlPage[0], modeParameterHeader, headerLength);
    //copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        memcpy(&controlPage[headerLength], modeParameterHeader, blockDescLength);
    }
    //set the remaining part of the page up
    controlPage[offset + 0] = 0x0A;
    controlPage[offset + 1] = 0x0A;
    if (pageControl == 0x01)
    {
        //nothing is required to be changeable so don't report changes for anything
    }
    else
    {
        controlPage[offset + 2] |= BIT2 | BIT1;//TST = 0, TMF_only = 0, dpicz = 0, dsense = 1, gltsd = 1, rlec = 0
        //todo:dpicz bit set to 1???Need to figure out when we want this set
        controlPage[offset + 3] |= BIT4 | BIT1;//queued algorithm modifier = 1, nuar = 0, QErr = 01,
        controlPage[offset + 4] = 0;//rac = 0, UA_INTLCK_CTRL = 0, swp = 0
        controlPage[offset + 5] = BIT7 | BIT6;//ato = 1, tas = 1, atmpe = 0, rwwp = 0, autoload mode = 0
        controlPage[offset + 6] = OBSOLETE;
        controlPage[offset + 7] = OBSOLETE;
        controlPage[offset + 8] = 0xFF;//busy timeout period
        controlPage[offset + 9] = 0xFF;//busy timeout period
#if defined (SNTL_EXT)
        uint16_t smartSelfTestTime = 0;
        //TODO: If the drive supports DST, set the DST timeout here
        controlPage[offset + 10] = M_Byte1(smartSelfTestTime);
        controlPage[offset + 11] = M_Byte0(smartSelfTestTime);
#else
        controlPage[offset + 10] = 0;
        controlPage[offset + 11] = 0;
#endif

    }
    //set the mode data length
    if (longHeader)
    {
        controlPage[0] = M_Byte1(pageLength - 2);
        controlPage[1] = M_Byte0(pageLength - 2);
    }
    else
    {
        controlPage[0] = pageLength - 1;
    }
    //now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, controlPage, M_Min(pageLength, allocationLength));
    }
    safe_Free(controlPage);
    return ret;
}

//mode parameter header must be 4 bytes for short format and 8 bytes for long format (longHeader set to true)
//dataBlockDescriptor must be non-null when returnDataBlockDescriiptor is true. When non null, it must be 8 bytes for short, or 16 for long (when longLBABit is set to true)
int sntl_Translate_Mode_Sense_Power_Condition_1A(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t pageControl, bool returnDataBlockDescriptor, bool longLBABit, uint8_t *dataBlockDescriptor, bool longHeader, uint8_t *modeParameterHeader, uint16_t allocationLength)
{
    int ret = SUCCESS;
    uint8_t *powerConditionPage = NULL;//will be allocated later
    uint16_t pageLength = 40;//add onto this depending on the length of the header and block descriptors
    uint16_t offset = 0;//used later when we start setting data in the buffer since we need to account for mode parameter header and DBDs
    uint8_t headerLength = 4;
    uint8_t blockDescLength = 0;
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    //now that we know how many bytes we need for this, allocate memory
    powerConditionPage = (uint8_t*)calloc(pageLength, sizeof(uint8_t));
    if (!powerConditionPage)
    {
        //TODO: set an error in the sense data
        return MEMORY_FAILURE;
    }
    //copy header into place
    memcpy(&powerConditionPage[0], modeParameterHeader, headerLength);
    //copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        memcpy(&powerConditionPage[headerLength], modeParameterHeader, blockDescLength);
    }
    //set the remaining part of the page up
    powerConditionPage[offset + 0] = 0x1A;
    powerConditionPage[offset + 1] = 0x26;//length
    //no timers suppored in nvme, so all fields are set to zero
    //set the mode data length
    if (longHeader)
    {
        powerConditionPage[0] = M_Byte1(pageLength - 2);
        powerConditionPage[1] = M_Byte0(pageLength - 2);
    }
    else
    {
        powerConditionPage[0] = pageLength - 1;
    }
    //now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, powerConditionPage, M_Min(pageLength, allocationLength));
    }
    safe_Free(powerConditionPage);
    return ret;
}

#if defined (SNTL_EXT)
int sntl_Translate_Mode_Sense_Control_Extension_0Ah_01h(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t pageControl, bool returnDataBlockDescriptor, bool longLBABit, uint8_t *dataBlockDescriptor, bool longHeader, uint8_t *modeParameterHeader, uint16_t allocationLength)
{
    int ret = SUCCESS;
    uint8_t *controlExtPage = NULL;//will be allocated later
    uint16_t pageLength = 32;//add onto this depending on the length of the header and block descriptors
    uint16_t offset = 0;//used later when we start setting data in the buffer since we need to account for mode parameter header and DBDs
    uint8_t headerLength = 4;
    uint8_t blockDescLength = 0;
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    //now that we know how many bytes we need for this, allocate memory
    controlExtPage = (uint8_t*)calloc(pageLength, sizeof(uint8_t));
    if (!controlExtPage)
    {
        //TODO: set an error in the sense data
        return MEMORY_FAILURE;
    }
    //copy header into place
    memcpy(&controlExtPage[0], modeParameterHeader, headerLength);
    //copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        memcpy(&controlExtPage[headerLength], modeParameterHeader, blockDescLength);
    }
    //set the remaining part of the page up
    controlExtPage[offset + 0] = 0x0A;
    controlExtPage[offset + 0] |= BIT6;//set spf bit
    controlExtPage[offset + 1] = 0x01;
    controlExtPage[offset + 2] = 0x00;
    controlExtPage[offset + 3] = 0x1C;
    if (pageControl != 0x01)//default, current, & saved...nothing on this page will be changeable - TJE
    {
        controlExtPage[offset + 4] = 0;//dlc = 0, tcmos = 0, scsip = 0, ialuae = 0
        controlExtPage[offset + 5] = 0;//initial command priority = 0 (for no/vendor spcific priority)
        controlExtPage[offset + 6] = SPC3_SENSE_LEN;//252
        controlExtPage[offset + 7] = RESERVED;
        controlExtPage[offset + 8] = RESERVED;
        controlExtPage[offset + 9] = RESERVED;
        controlExtPage[offset + 10] = RESERVED;
        controlExtPage[offset + 11] = RESERVED;
        controlExtPage[offset + 12] = RESERVED;
        controlExtPage[offset + 13] = RESERVED;
        controlExtPage[offset + 14] = RESERVED;
        controlExtPage[offset + 15] = RESERVED;
        controlExtPage[offset + 16] = RESERVED;
        controlExtPage[offset + 17] = RESERVED;
        controlExtPage[offset + 18] = RESERVED;
        controlExtPage[offset + 19] = RESERVED;
        controlExtPage[offset + 20] = RESERVED;
        controlExtPage[offset + 21] = RESERVED;
        controlExtPage[offset + 22] = RESERVED;
        controlExtPage[offset + 23] = RESERVED;
        controlExtPage[offset + 24] = RESERVED;
        controlExtPage[offset + 25] = RESERVED;
        controlExtPage[offset + 26] = RESERVED;
        controlExtPage[offset + 27] = RESERVED;
        controlExtPage[offset + 28] = RESERVED;
        controlExtPage[offset + 29] = RESERVED;
        controlExtPage[offset + 30] = RESERVED;
        controlExtPage[offset + 31] = RESERVED;
    }
    //set the mode data length
    if (longHeader)
    {
        controlExtPage[0] = M_Byte1(pageLength - 2);
        controlExtPage[1] = M_Byte0(pageLength - 2);
    }
    else
    {
        controlExtPage[0] = pageLength - 1;
    }
    //now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, controlExtPage, M_Min(pageLength, allocationLength));
    }
    safe_Free(controlExtPage);
    return ret;
}

int sntl_Translate_Mode_Sense_Informational_Exceptions_Control_1Ch(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t pageControl, bool returnDataBlockDescriptor, bool longLBABit, uint8_t *dataBlockDescriptor, bool longHeader, uint8_t *modeParameterHeader, uint16_t allocationLength)
{
    int ret = SUCCESS;
    uint8_t *informationalExceptions = NULL;//will be allocated later
    uint16_t pageLength = 12;//add onto this depending on the length of the header and block descriptors
    uint16_t offset = 0;//used later when we start setting data in the buffer since we need to account for mode parameter header and DBDs
    uint8_t headerLength = 4;
    uint8_t blockDescLength = 0;
    if (!modeParameterHeader)
    {
        return BAD_PARAMETER;
    }
    if (longHeader)
    {
        pageLength += 8;
        offset += 8;
        headerLength = 8;
    }
    else
    {
        pageLength += 4;
        offset += 4;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            pageLength += 16;
            offset += 16;
            blockDescLength = 16;
        }
        else
        {
            pageLength += 8;
            offset += 8;
            blockDescLength = 8;
        }
    }
    //now that we know how many bytes we need for this, allocate memory
    informationalExceptions = (uint8_t*)calloc(pageLength, sizeof(uint8_t));
    if (!informationalExceptions)
    {
        //TODO: set an error in the sense data
        return MEMORY_FAILURE;
    }
    //copy header into place
    memcpy(&informationalExceptions[0], modeParameterHeader, headerLength);
    //copy block descriptor if it is to be returned
    if (blockDescLength > 0)
    {
        memcpy(&informationalExceptions[headerLength], modeParameterHeader, blockDescLength);
    }
    //set the remaining part of the page up
    informationalExceptions[offset + 0] = 0x1C;//page number
    informationalExceptions[offset + 1] = 0x0A;//page length
    if (pageControl != 0x1)
    {
        informationalExceptions[offset + 2] = 0;//perf = 0, reserved = 0, ebf = 0 (no background functions), EWAsc = 0 (doesn't report warnings), DExcpt = 0 (device does not disable reporting failure predictions), test = 0, ebackerr = 0, logerr = 0
        informationalExceptions[offset + 3] = 0;//MRIE = 0h //TODO: should this be different? We won't ever set a trip, but it isn't disabled either...should be either 0 or 6. 0 meaning will not trip. 6 meaning will check on request sense
        informationalExceptions[offset + 4] = 0;//interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 5] = 0;//interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 6] = 0;//interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 7] = 0;//interval timer = 0 (not used/vendor specific)
        informationalExceptions[offset + 8] = 0;//report count = 0 (no limit)
        informationalExceptions[offset + 9] = 0;//report count = 0 (no limit)
        informationalExceptions[offset + 10] = 0;//report count = 0 (no limit)
        informationalExceptions[offset + 11] = 0;//report count = 0 (no limit)
    }
    else
    {
        //DExcpt can be changed. But we won't allow it right now. - TJE
        //MRIE modes other than 6 are allowed, but unspecified. Not going to support them right now - TJE
    }
    //set the mode data length
    if (longHeader)
    {
        informationalExceptions[0] = M_Byte1(pageLength - 2);
        informationalExceptions[1] = M_Byte0(pageLength - 2);
    }
    else
    {
        informationalExceptions[0] = pageLength - 1;
    }
    //now copy the data back and return from this function
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, informationalExceptions, M_Min(pageLength, allocationLength));
    }
    safe_Free(informationalExceptions);
    return ret;
}

#endif

int sntl_Translate_SCSI_Mode_Sense_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    bool returnDataBlockDescriptor = true;//true means return a data block descriptor, false means don't return one
    bool longLBABit = false;//true = longlba format, false = standard format for block descriptor
    bool longHeader = false;//false for mode sense 6, true for mode sense 10
    uint8_t pageControl = (scsiIoCtx->cdb[2] & 0xC0) >> 6;//only current values needs to be supported...anything else is unspecified
    uint8_t pageCode = scsiIoCtx->cdb[2] & 0x3F;
    uint8_t subpageCode = scsiIoCtx->cdb[3];
    uint16_t allocationLength = 0;
    uint8_t dataBlockDescriptor[16] = { 0 };
    uint8_t modeParameterHeader[8] = { 0 };
    bool invalidField = false;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t byte1 = scsiIoCtx->cdb[1];
    if (scsiIoCtx->cdb[1] & BIT3)
    {
        returnDataBlockDescriptor = false;
    }
    if (scsiIoCtx->cdb[OPERATION_CODE] == MODE_SENSE_6_CMD)
    {
        allocationLength = scsiIoCtx->cdb[5];
        modeParameterHeader[1] = 0;//medium type
        modeParameterHeader[2] |= BIT4;//set the DPOFUA bit
        if (returnDataBlockDescriptor)
        {
            modeParameterHeader[3] = 8;//8 bytes for the short descriptor
        }
        //check for invalid fields
        byte1 &= 0xF7;//removing dbd bit since we can support that
        if (((fieldPointer = 1) != 0 && byte1 != 0))
        {
            invalidField = true;
        }
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == MODE_SENSE10)
    {
        //mode sense 10
        allocationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        longHeader = true;
        modeParameterHeader[2] = 0;//medium type
        modeParameterHeader[3] |= BIT4;//set the DPOFUA bit
        if (scsiIoCtx->cdb[1] & BIT4)
        {
            longLBABit = true;
            modeParameterHeader[4] |= BIT0;//set the longlba bit
        }
        if (returnDataBlockDescriptor)
        {
            if (longLBABit)
            {
                modeParameterHeader[6] = M_Byte1(16);
                modeParameterHeader[7] = M_Byte0(16);
            }
            else
            {
                modeParameterHeader[6] = M_Byte1(8);
                modeParameterHeader[7] = M_Byte0(8);
            }
        }
        byte1 &= 0xE7;//removing llbaa and DBD bits since we can support those
        //check for invalid fields
        if (((fieldPointer = 1) != 0 && byte1 != 0)
            || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0)
            || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0)
            || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0)
            )
        {
            invalidField = true;
        }
    }
    else
    {
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            if (fieldPointer == 1)
            {
                reservedByteVal = byte1;
            }
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (returnDataBlockDescriptor)
    {
        if (longLBABit)
        {
            //16 byte long format
            dataBlockDescriptor[0] = M_Byte7(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[1] = M_Byte6(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[2] = M_Byte5(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[3] = M_Byte4(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[4] = M_Byte3(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[5] = M_Byte2(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[6] = M_Byte1(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[7] = M_Byte0(device->drive_info.deviceMaxLba);
            dataBlockDescriptor[8] = RESERVED;
            dataBlockDescriptor[9] = RESERVED;
            dataBlockDescriptor[10] = RESERVED;
            dataBlockDescriptor[11] = RESERVED;
            dataBlockDescriptor[12] = M_Byte3(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[13] = M_Byte2(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[14] = M_Byte1(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[15] = M_Byte0(device->drive_info.deviceBlockSize);
        }
        else
        {
            //8 byte short format
            uint32_t maxLBA = (uint32_t)M_Min(UINT32_MAX, device->drive_info.deviceMaxLba);
            dataBlockDescriptor[0] = M_Byte3(maxLBA);
            dataBlockDescriptor[1] = M_Byte2(maxLBA);
            dataBlockDescriptor[2] = M_Byte1(maxLBA);
            dataBlockDescriptor[3] = M_Byte0(maxLBA);
            dataBlockDescriptor[4] = RESERVED;
            dataBlockDescriptor[5] = M_Byte2(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[6] = M_Byte1(device->drive_info.deviceBlockSize);
            dataBlockDescriptor[7] = M_Byte0(device->drive_info.deviceBlockSize);
        }
    }
    switch (pageCode)
    {
    case 0xA0://control and control extension and PATA control
        switch (subpageCode)
        {
        case 0://control
            ret = sntl_Translate_Mode_Sense_Control_0Ah(device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader, allocationLength);
            break;
#if defined (SNTL_EXT)
        case 0x01://control extension
            ret = sntl_Translate_Mode_Sense_Control_Extension_0Ah_01h(device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader, allocationLength);
            break;
#endif
        default:
            ret = NOT_SUPPORTED;
            fieldPointer = 2;
            bitPointer = 5;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    case 0x01://read-write error recovery
        switch (subpageCode)
        {
        case 0:
            ret = sntl_Translate_Mode_Sense_Read_Write_Error_Recovery_01h(device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader, allocationLength);
            break;
        default:
            ret = NOT_SUPPORTED;
            fieldPointer = 2;
            bitPointer = 5;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
        break;
    case 0x08://caching
        switch (subpageCode)
        {
        case 0:
            ret = sntl_Translate_Mode_Sense_Caching_08h(device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader, allocationLength);
            break;
        default:
            ret = NOT_SUPPORTED;
            fieldPointer = 2;
            bitPointer = 5;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
        break;
#if defined (SNTL_EXT)
    case 0x1C://informational exceptions control - add under SNTL_EXT
        switch (subpageCode)
        {
        case 0:
            ret = sntl_Translate_Mode_Sense_Informational_Exceptions_Control_1Ch(device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader, allocationLength);
            break;
        default:
            ret = NOT_SUPPORTED;
            fieldPointer = 2;
            bitPointer = 5;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
        break;
#endif
    case 0x1A://Power Condition
        switch (subpageCode)
        {
        case 0://power condition
            ret = sntl_Translate_Mode_Sense_Power_Condition_1A(device, scsiIoCtx, pageControl, returnDataBlockDescriptor, longLBABit, dataBlockDescriptor, longHeader, modeParameterHeader, allocationLength);
            break;
        default:
            ret = NOT_SUPPORTED;
            fieldPointer = 2;
            bitPointer = 5;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
        break;
        //TODO: support returning all modepages/subpages
    default:
        ret = NOT_SUPPORTED;
        fieldPointer = 2;
        bitPointer = 5;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        break;
    }
    return ret;
}

int sntl_Translate_Mode_Select_Caching_08h(tDevice *device, ScsiIoCtx *scsiIoCtx, bool parametersSaveble, uint8_t *ptrToBeginningOfModePage, uint16_t pageLength)
{
    int ret = SUCCESS;
    uint32_t dataOffset = (uint32_t)(ptrToBeginningOfModePage - scsiIoCtx->pdata);//to be used when setting which field is invalid in parameter list
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //start checking everything to make sure it looks right before we issue commands
    if (pageLength != 0x12)
    {
        fieldPointer = dataOffset + 1;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (ptrToBeginningOfModePage[2] & 0xFB)//all but WCE bit
    {
        fieldPointer = dataOffset + 2;
        uint8_t reservedByteVal = ptrToBeginningOfModePage[2] & 0xFB;
        uint8_t counter = 0;
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (((fieldPointer = 3) != 0 && ptrToBeginningOfModePage[3] != 0) ||
        ((fieldPointer = 4) != 0 && ptrToBeginningOfModePage[4] != 0) ||
        ((fieldPointer = 5) != 0 && ptrToBeginningOfModePage[5] != 0) ||
        ((fieldPointer = 6) != 0 && ptrToBeginningOfModePage[6] != 0) ||
        ((fieldPointer = 7) != 0 && ptrToBeginningOfModePage[7] != 0) ||
        ((fieldPointer = 8) != 0 && ptrToBeginningOfModePage[8] != 0) ||
        ((fieldPointer = 9) != 0 && ptrToBeginningOfModePage[9] != 0) ||
        ((fieldPointer = 10) != 0 && ptrToBeginningOfModePage[10] != 0) ||
        ((fieldPointer = 11) != 0 && ptrToBeginningOfModePage[11] != 0)
        )
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
        uint8_t counter = 0;
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        fieldPointer += dataOffset;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (((fieldPointer = 12) != 0 && ptrToBeginningOfModePage[12] != 0) ||
        ((fieldPointer = 13) != 0 && ptrToBeginningOfModePage[13] != 0) ||
        ((fieldPointer = 14) != 0 && ptrToBeginningOfModePage[14] != 0) ||
        ((fieldPointer = 15) != 0 && ptrToBeginningOfModePage[15] != 0) ||
        ((fieldPointer = 16) != 0 && ptrToBeginningOfModePage[16] != 0) ||
        ((fieldPointer = 17) != 0 && ptrToBeginningOfModePage[17] != 0) ||
        ((fieldPointer = 18) != 0 && ptrToBeginningOfModePage[18] != 0) ||
        ((fieldPointer = 19) != 0 && ptrToBeginningOfModePage[19] != 0)
        )
    {
        uint8_t reservedByteVal = ptrToBeginningOfModePage[fieldPointer];
        uint8_t counter = 0;
        while (reservedByteVal > 0 && counter < 8)
        {
            reservedByteVal >>= 1;
            ++counter;
        }
        bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        fieldPointer += dataOffset;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    int wceRet = SUCCESS;
    //WCE
    if (device->drive_info.IdentifyData.nvme.ctrl.vwc & BIT0)
    {
        nvmeFeaturesCmdOpt setWCE;
        memset(&setWCE, 0, sizeof(nvmeFeaturesCmdOpt));
        setWCE.featSetGetValue = (ptrToBeginningOfModePage[2] & BIT2) > 0 ? 1 : 0;
        setWCE.fid = 0x06;
        setWCE.sv = 0;
        wceRet = nvme_Set_Features(device, &setWCE);
        //TODO: remove this if and pass along the DWORD returned with status for translation instead.
        if (wceRet)
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            ret = FAILURE;
            return ret;
        }
        else
        {
            ret = SUCCESS;
            //set good status
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        }
    }
    else if(ptrToBeginningOfModePage[2] & BIT2)
    {
        //drive doesn't support Volatile write cache, so we need to set an error for invalid field in CDB
        bitPointer = 2;
        fieldPointer = 2;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    return ret;
}


//TODO: a way to cache changes from the incoming block descriptor to change sector size in a format command.
int sntl_Translate_SCSI_Mode_Select_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    bool pageFormat = false;
    //bool saveParameters = false;
    bool tenByteCommand = false;
    uint16_t parameterListLength = 0;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    if (scsiIoCtx->cdb[OPERATION_CODE] == 0x15 || scsiIoCtx->cdb[OPERATION_CODE] == 0x55)
    {
        if (scsiIoCtx->cdb[1] & BIT4)
        {
            pageFormat = true;
        }
        //      if (scsiIoCtx->cdb[1] & BIT0)
        //      {
        //          saveParameters = true;
        //      }
        parameterListLength = scsiIoCtx->cdb[4];
        if (scsiIoCtx->cdb[OPERATION_CODE] == 0x15)//mode select 6
        {
            uint8_t byte1 = scsiIoCtx->cdb[1] & 0x11;//removing PF and SP bits since we can handle those, but not any other bits
            if (((fieldPointer = 1) && byte1 != 0)
                || ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0)
                || ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0)
                )
            {
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                    if (fieldPointer == 1)
                    {
                        reservedByteVal = byte1;
                    }
                    uint8_t counter = 0;
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                }
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                //invalid field in cdb
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return ret;
            }
        }
        else if (scsiIoCtx->cdb[OPERATION_CODE] == 0x55)//mode select 10
        {
            tenByteCommand = true;
            parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            uint8_t byte1 = scsiIoCtx->cdb[1] & 0x11;//removing PF and SP bits since we can handle those, but not any other bits
            if (((fieldPointer = 1) && byte1 != 0)
                || ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0)
                || ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0)
                || ((fieldPointer = 4) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[4] != 0)
                || ((fieldPointer = 5) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[5] != 0)
                || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[6] != 0)
                )
            {
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                    if (fieldPointer == 1)
                    {
                        reservedByteVal = byte1;
                    }
                    uint8_t counter = 0;
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                }
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                //invalid field in cdb
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return ret;
            }
        }
    }
    else
    {
        //invalid operation code
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    if (pageFormat)
    {
        if (parameterListLength == 0)
        {
            //Spec says this is not to be considered an error. Since we didn't get any data, there is nothing to do but return good status - TJE
            return SUCCESS;
        }
        bitPointer = 0;
        //uint16_t modeDataLength = scsiIoCtx->pdata[0];
        //uint8_t deviceSpecificParameter = scsiIoCtx->pdata[2];
        //bool writeProtected = false;//Don't allow write protection. This makes no sense for this software implementation. - TJE
        //bool dpoFua = false;//TODO: allow this bit to be zero. AKA don't allow DPO or FUA in read/write
        bool longLBA = false;
        uint16_t blockDescriptorLength = scsiIoCtx->pdata[3];
        if (tenByteCommand)
        {
            //modeDataLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[0], scsiIoCtx->pdata[1]);
            if (scsiIoCtx->pdata[2] != 0)//mediumType
            {
                fieldPointer = 2;
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if ((scsiIoCtx->pdata[3] & 0x7F) != 0)//device specific parameter - WP bit is ignored in SBC. dpofua is reserved.
            {
                fieldPointer = 3;
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter = 0;
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                }
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if (scsiIoCtx->pdata[4])
            {
                longLBA = true;
            }
            blockDescriptorLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
            if (((fieldPointer = 4) != 0 && scsiIoCtx->pdata[4] & 0xFE)//reserved bits/bytes
                || ((fieldPointer = 5) != 0 && scsiIoCtx->pdata[5] != 0)//reserved bits/bytes
                )
            {
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter = 0;
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                }
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
        }
        else
        {
            if (scsiIoCtx->pdata[1] != 0)//mediumType
            {
                fieldPointer = 1;
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
            if ((scsiIoCtx->pdata[2] & 0x7F) != 0)//device specific parameter - WP bit is ignored in SBC. dpofua is reserved.
            {
                fieldPointer = 2;
                if (bitPointer == 0)
                {
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter = 0;
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                }
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return NOT_SUPPORTED;
            }
        }

        if (blockDescriptorLength > 0)
        {
            //check the block descriptor to make sure it's valid
            if (tenByteCommand)
            {
                if (longLBA)
                {
                    uint64_t numberOfLogicalBlocks = M_BytesTo8ByteValue(scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 0], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 1], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 2], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 3], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 4], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 5], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 6], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 7]);
                    uint32_t logicalBlockLength = M_BytesTo4ByteValue(scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 12], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 13], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 14], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 15]);
                    if (numberOfLogicalBlocks != device->drive_info.deviceMaxLba)
                    {
                        //TODO: handle when this is all F's? Should we allow this to change the max LBA of the drive?
                        bitPointer = 7;
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 0;
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 8) != 0 && scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 8] != 0)
                        || ((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 9) != 0 && scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 9] != 0)
                        || ((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 10) != 0 && scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 10] != 0)
                        || ((fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 11) != 0 && scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 11] != 0)
                        )
                    {
                        uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                        uint8_t counter = 0;
                        while (reservedByteVal > 0 && counter < 8)
                        {
                            reservedByteVal >>= 1;
                            ++counter;
                        }
                        bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (logicalBlockLength != device->drive_info.deviceBlockSize)
                    {
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 12;
                        bitPointer = 7;
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                }
                else
                {
                    //short block descriptor(s)...should only have 1!
                    uint32_t numberOfLogicalBlocks = M_BytesTo4ByteValue(scsiIoCtx->pdata[8], scsiIoCtx->pdata[9], scsiIoCtx->pdata[10], scsiIoCtx->pdata[11]);
                    uint32_t logicalBlockLength = M_BytesTo4ByteValue(0, scsiIoCtx->pdata[13], scsiIoCtx->pdata[14], scsiIoCtx->pdata[15]);
                    if (numberOfLogicalBlocks != device->drive_info.deviceMaxLba)
                    {
                        //TODO: handle when this is all F's? Should we allow this to change the max LBA of the drive?
                        bitPointer = 7;
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 0;
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (scsiIoCtx->pdata[MODE_PARAMETER_HEADER_10_LEN + 4] != 0)//short header + 4 bytes
                    {
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 4;
                        uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                        uint8_t counter = 0;
                        while (reservedByteVal > 0 && counter < 8)
                        {
                            reservedByteVal >>= 1;
                            ++counter;
                        }
                        bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (logicalBlockLength != device->drive_info.deviceBlockSize)
                    {
                        fieldPointer = MODE_PARAMETER_HEADER_10_LEN + 5;
                        bitPointer = 7;
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                }
            }
            else
            {
                //short block descriptor(s)...should only have 1!
                uint32_t numberOfLogicalBlocks = M_BytesTo4ByteValue(scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 0], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 1], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 2], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 3]);
                uint32_t logicalBlockLength = M_BytesTo4ByteValue(0, scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 5], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 6], scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 7]);
                if (numberOfLogicalBlocks != device->drive_info.deviceMaxLba)
                {
                    //TODO: handle when this is all F's? Should we allow this to change the max LBA of the drive?
                    bitPointer = 7;
                    fieldPointer = MODE_PARAMETER_HEADER_6_LEN + 0;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return NOT_SUPPORTED;
                }
                if (scsiIoCtx->pdata[MODE_PARAMETER_HEADER_6_LEN + 4] != 0)//short header + 4 bytes
                {
                    fieldPointer = MODE_PARAMETER_HEADER_6_LEN + 4;
                    uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                    uint8_t counter = 0;
                    while (reservedByteVal > 0 && counter < 8)
                    {
                        reservedByteVal >>= 1;
                        ++counter;
                    }
                    bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return NOT_SUPPORTED;
                }
                if (logicalBlockLength != device->drive_info.deviceBlockSize)
                {
                    fieldPointer = MODE_PARAMETER_HEADER_6_LEN + 5;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return NOT_SUPPORTED;
                }
            }
        }
        uint8_t headerLength = MODE_PARAMETER_HEADER_6_LEN;
        if (tenByteCommand)
        {
            headerLength = MODE_PARAMETER_HEADER_10_LEN;
        }
        //time to call the function that handles the changes for the mode page requested...save all this info and pass it in for convenience in that function
        uint8_t modePage = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & 0x3F;
        bool subPageFormat = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & BIT6;
        bool parametersSaveble = scsiIoCtx->pdata[headerLength + blockDescriptorLength] & BIT7;
        uint8_t subpage = 0;
        uint16_t pageLength = scsiIoCtx->pdata[headerLength + blockDescriptorLength + 1];
        if (subPageFormat)
        {
            subpage = scsiIoCtx->pdata[headerLength + blockDescriptorLength + 1];
            pageLength = M_BytesTo2ByteValue(scsiIoCtx->pdata[headerLength + blockDescriptorLength + 2], scsiIoCtx->pdata[headerLength + blockDescriptorLength + 3]);
        }
        switch (modePage)
        {
            //TODO: add more more pages...we could technically add all the pages we have in mode sense, but most of them aren't changeable so I'm not going to bother handling them here - TJE
        case 0x08:
            switch (subpage)
            {
            case 0://caching mode page
                ret = sntl_Translate_Mode_Select_Caching_08h(device, scsiIoCtx, parametersSaveble, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
                break;
            default:
                //invalid field in parameter list...we don't support this page
                fieldPointer = headerLength + blockDescriptorLength + 1;//plus one for subpage
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                break;
            }
            break;
            //while we do use these pages, nothing is changable, so just return an error for now...
        //case 0x0A:
        //    switch (subpage)
        //    {
        //    case 0://control mode page
        //        ret = sntl_Translate_Mode_Select_Control_0Ah(device, scsiIoCtx, parametersSaveble, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
        //        break;
        //    default:
        //        fieldPointer = headerLength + blockDescriptorLength + 1;//plus one for subpage
        //        bitPointer = 7;
        //        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        //        ret = NOT_SUPPORTED;
        //        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        //        break;
        //    }
        //    break;
        //case 0x1A:
        //    switch (subpage)
        //    {
        //    case 0://power conditions
        //        ret = sntl_Translate_Mode_Select_Power_Conditions_1A(device, scsiIoCtx, parametersSaveble, &scsiIoCtx->pdata[headerLength + blockDescriptorLength], pageLength);
        //        break;
        //    default:
        //        fieldPointer = headerLength + blockDescriptorLength + 1;//plus one for subpage
        //        bitPointer = 7;
        //        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
        //        ret = NOT_SUPPORTED;
        //        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        //        break;
        //    }
        //    break;
        default:
            //invalid field in parameter list...we don't support this page
            fieldPointer = headerLength + blockDescriptorLength;
            bitPointer = 5;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
    }
    else
    {
        fieldPointer = 1;
        bitPointer = 4;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    return ret;
}

int sntl_Translate_SCSI_Synchronize_Cache_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    //check the read command and get the LBA from it
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x35://synchronize cache 10
        if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0)
            || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0)
            )
        {
            //can't support these bits (including immediate)
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return SUCCESS;
        }
        break;
    case 0x91://synchronize cache 16
        if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0)
            || ((fieldPointer = 14) != 0 && scsiIoCtx->cdb[14] != 0)
            )
        {
            if (bitPointer == 0)
            {
                uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
                uint8_t counter = 0;
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
            }
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            //can't support these bits (including immediate)
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return SUCCESS;
        }
        break;
    default:
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        return BAD_PARAMETER;
    }
    ret = nvme_Flush(device);
    //now set sense data
    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

//TODO: DPO bit
int sntl_Translate_SCSI_Read_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    uint64_t lba = 0;
    uint32_t transferLength = 0;
    bool fua = false;
    bool invalidField = false;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t pi = 0;//set based off of rdprotect field
    uint8_t rdprotect = 0;
    //check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x08://read 6
        lba = M_BytesTo4ByteValue(0, (scsiIoCtx->cdb[1] & 0x1F), scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
        transferLength = scsiIoCtx->cdb[4];
        if (M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5) != 0)
        {
            fieldPointer = 1;
            bitPointer = 0;
            invalidField = true;
        }
        if (transferLength == 0)
        {
            transferLength = 256;//read 6 transfer length 0 means 256 blocks
        }
        break;
    case 0x28://read 10
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        rdprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//reladr bit. Obsolete.
            || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Unspecified...will treat as error
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2)//cannot support RACR bit
            || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[6], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    case 0xA8://read 12
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        rdprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//reladr bit. Obsolete.
            || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. Unspecified...will treat as error
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2)//cannot support RACR bit
            || ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[10], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    case 0x88://read 16
        lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5], scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        transferLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        rdprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        //sbc2 fua_nv bit is unspecified
        //We don't support RARC 
        //We don't support DLD bits either
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//reladr bit. Obsolete.
            || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. 
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2)//cannot support RACR bit
            || ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[14], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (transferLength == 0)//this is allowed and it means to validate inputs and return success
    {
        return SUCCESS;
    }
    else if (transferLength > 65536)//not issuing multiple commands...
    {
        //return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x28://read 10
            fieldPointer = 7;
            break;
        case 0xA8://read 12
            fieldPointer = 6;
            break;
        case 0x88://read 16
            fieldPointer = 10;
            break;
        }
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    //TODO: we may need to add additional work to make this happen...not sure.
    if (device->drive_info.IdentifyData.nvme.ns.dps > 0 && scsiIoCtx->cdb[OPERATION_CODE] != 0x08)
    {
        switch (rdprotect)
        {
        case 0:
            pi = 0xF;
            break;
        case 1:
        case 5:
            pi = 0x7;
            break;
        case 2:
            pi = 0x3;
            break;
        case 3:
            pi = 0x0;
            break;
        case 4:
            pi = 0x4;
            break;
        default://shouldn't happen...
            return UNKNOWN;
        }
    }
    int ret = nvme_Read(device, lba, transferLength - 1, false, fua, pi, scsiIoCtx->pdata, scsiIoCtx->dataLength);
    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

//TODO: DPO bit
int sntl_Translate_SCSI_Write_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    bool fua = false;
    uint64_t lba = 0;
    uint32_t transferLength = 0;
    bool invalidField = false;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t pi = 0;//set based off of rdprotect field
    uint8_t wrprotect = 0;
    //check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x0A://write 6
        lba = M_BytesTo4ByteValue(0, (scsiIoCtx->cdb[1] & 0x1F), scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
        transferLength = scsiIoCtx->cdb[4];
        if (M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5) != 0)
        {
            bitPointer = 0;
            fieldPointer = 1;
            invalidField = true;
        }
        if (transferLength == 0)
        {
            transferLength = 256;//write 6 transfer length 0 means 256 blocks
        }
        break;
    case 0x2A://write 10
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        wrprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//reladr bit. Obsolete.
            || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. 
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2)//reserved bit
            || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[6], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    case 0xAA://write 12
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        transferLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        wrprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//reladr bit. Obsolete.
            || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. 
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2)//reserved bit
            || ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[10], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    case 0x8A://write 16
        lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5], scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        transferLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        wrprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (scsiIoCtx->cdb[1] & BIT3)
        {
            fua = true;
        }
        //sbc2 fua_nv bit can be ignored according to SAT. 
        //We don't support DLD bits either
        if (((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//reladr bit. Obsolete. also now the DLD2 bit
            || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//FUA_NV bit. 
            || ((fieldPointer = 1) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[1] & BIT2)//reserved bit
            || ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[14], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (transferLength == 0)
    {
        //a transfer length of zero means do nothing but validate inputs and is not an error
        return SUCCESS;
    }
    else if (transferLength > 65536)
    {
        //return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x2A://write 10
            fieldPointer = 7;
            break;
        case 0xAA://write 12
            fieldPointer = 6;
            break;
        case 0x8A://write 16
            fieldPointer = 10;
            break;
        }
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    //TODO: we may need to add additional work to make this happen...not sure.
    if (device->drive_info.IdentifyData.nvme.ns.dps > 0 && scsiIoCtx->cdb[OPERATION_CODE] != 0x0A)
    {
        switch (wrprotect)
        {
        case 0:
            pi = 0x8;
            break;
        case 1:
        case 5:
            pi = 0x7;
            break;
        case 2:
            pi = 0x3;
            break;
        case 3:
            pi = 0x0;
            break;
        case 4:
            pi = 0x4;
            break;
        default://shouldn't happen...
            return UNKNOWN;
        }
    }
    int ret = nvme_Write(device, lba, transferLength - 1, false, fua, pi, 0, scsiIoCtx->pdata, scsiIoCtx->dataLength);
    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

int sntl_Translate_SCSI_Verify_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t byteCheck = 0;
    uint64_t lba = 0;
    uint32_t verificationLength = 0;
    bool invalidField = false;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t pi = 0;
    uint8_t vrprotect = 0;
    //check the read command and get the LBA from it
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case 0x2F://verify 10
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        verificationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        vrprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3)
            || ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)
            || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[6], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    case 0xAF://verify 12
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
        verificationLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        vrprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3)
            || ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)
            || ((fieldPointer = 10) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[10], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    case 0x8F://verify 16
        byteCheck = (scsiIoCtx->cdb[1] >> 1) & 0x03;
        lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5], scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
        verificationLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[10], scsiIoCtx->cdb[11], scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
        vrprotect = M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5);
        if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3)
            || ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)
            || ((fieldPointer = 14) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->cdb[14], 7, 6) != 0)
            )
        {
            invalidField = true;
        }
        break;
    default:
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return BAD_PARAMETER;
    }
    if (invalidField)
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
    }
    if (verificationLength == 0)//this is allowed and it means to validate inputs and return success
    {
        return SUCCESS;
    }
    else if (verificationLength > 65536)
    {
        //return an error
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case 0x2F://verify 10
            fieldPointer = 7;
            break;
        case 0xAF://verify 12
            fieldPointer = 6;
            break;
        case 0x8F://verify 16
            fieldPointer = 10;
            break;
        }
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    switch (byteCheck)
    {
    case 0:
        //no logical block data..what do we send as the compare buffer? Or just do a read instead with FUA bit set???? or read followed by compare?
        //TODO: figure out what we actually need to do in this case
        fieldPointer = 1;
        bitPointer = 2;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
        /*if (device->drive_info.IdentifyData.nvme.ns.dps > 0)
        {
            switch (vrprotect)
            {
            case 0:
            case 1:
            case 5:
                pi = 0xF;
                break;
            case 2:
                pi = 0xB;
                break;
            case 3:
                pi = 0x8;
                break;
            case 4:
                pi = 0xC;
                break;
            }
        }*/
        break;
    case 1://compare buffer to what is on the drive medium
        if (device->drive_info.IdentifyData.nvme.ns.dps > 0)
        {
            switch (vrprotect)
            {
            case 0:
                pi = 0xF;
                break;
            default:
                pi = 0x8;
                break;
            }
        }
        ret = nvme_Compare(device, lba, verificationLength - 1, false, true, pi, scsiIoCtx->pdata, scsiIoCtx->dataLength);
        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
        break;
    case 2://not defined
        fieldPointer = 1;
        bitPointer = 2;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
        break;
    case 3://compare a single logical block of data to each LBA in the range...SNTL does not specify this mode...but we can probably implement it ourselves as an extension
        //TODO: do a loop of compare commands for the range of LBAs to compare each one, one logical block at a time
        fieldPointer = 1;
        bitPointer = 2;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return NOT_SUPPORTED;
        break;
    default:
        ret = UNKNOWN;
        break;
    }
    return ret;
}

int sntl_Translate_SCSI_Security_Protocol_In_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t securityProtocol = scsiIoCtx->cdb[1];
    uint16_t securityProtocolSpecific = M_BytesTo2ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
    uint32_t allocationLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (((fieldPointer = 4) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[4], 6, 0) != 0)
        || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0)
        || ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    if (scsiIoCtx->pdata)
    {
        memset(scsiIoCtx->pdata, 0, scsiIoCtx->dataLength);
    }
    if (scsiIoCtx->cdb[4] & BIT7)//inc512
    {
        if (allocationLength > 0xFFFF)
        {
            bitPointer = 7;
            fieldPointer = 6;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
        allocationLength *= 512;//convert to bytes for NVMe
    }
    else
    {
        if (allocationLength > 0x01FFFE00)
        {
            bitPointer = 7;
            fieldPointer = 6;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
    }
    ret = nvme_Security_Receive(device, securityProtocol, securityProtocolSpecific, 0, scsiIoCtx->pdata, allocationLength);
    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

int sntl_Translate_SCSI_Security_Protocol_Out_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t securityProtocol = scsiIoCtx->cdb[1];
    uint16_t securityProtocolSpecific = M_BytesTo2ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3]);
    uint32_t transferLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (((fieldPointer = 4) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[4], 6, 0) != 0)
        || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0)
        || ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0x00, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    if (scsiIoCtx->cdb[4] & BIT7)//inc512
    {
        if (transferLength > 0xFFFF)
        {
            bitPointer = 7;
            fieldPointer = 6;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
        transferLength *= 512;//convert to bytes for NVMe
    }
    else
    {
        if (transferLength > 0x01FFFE00)
        {
            bitPointer = 7;
            fieldPointer = 6;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return FAILURE;
        }
    }
    ret = nvme_Security_Send(device, securityProtocol, securityProtocolSpecific, 0, scsiIoCtx->pdata, transferLength);
    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
    return ret;
}

int sntl_Translate_SCSI_Report_Luns_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t *reportLunsData = NULL;
    uint32_t reportLunsDataLength = 8;//minimum data length
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint32_t allocationLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0)
        || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0)
        || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0)
        || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0)
        || ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    bool emptyData = false;
    switch (scsiIoCtx->cdb[2])
    {
    case 0x11://only report if lun 0, else return zeros...check the namespace ID
        if (device->drive_info.namespaceID != 1)//lun zero, is the same as namespace 1
        {
            emptyData = true;
            break;
        }
    case 0x00:
    case 0x02:
        //read the identify active namespace list
    {
        bool singleLun = false;
        uint8_t* activeNamespaces = (uint8_t*)calloc_aligned(4096, sizeof(uint8_t), device->os_info.minimumAlignment);
        if (activeNamespaces)
        {
            if (SUCCESS == nvme_Identify(device, activeNamespaces, 0, 2))
            {
                //allocate based on maximum number of namespaces
                reportLunsDataLength += UINT32_C(8) * device->drive_info.IdentifyData.nvme.ctrl.nn;
                reportLunsData = (uint8_t*)calloc(reportLunsDataLength, sizeof(uint8_t));
                if (reportLunsData)
                {
                    uint32_t reportLunsOffset = 8;
                    for (uint32_t offset = 0, identifier = 0; offset < 4096; offset += 4, reportLunsOffset += 8)
                    {
                        identifier = M_BytesTo4ByteValue(activeNamespaces[offset + 3], activeNamespaces[offset + 2], activeNamespaces[offset + 1], activeNamespaces[offset + 0]);
                        if (identifier == 0)
                        {
                            break;
                        }
                        reportLunsData[reportLunsOffset + 0] = 0;
                        reportLunsData[reportLunsOffset + 1] = 0;
                        reportLunsData[reportLunsOffset + 2] = 0;
                        reportLunsData[reportLunsOffset + 3] = 0;
                        reportLunsData[reportLunsOffset + 4] = M_Byte3(identifier);
                        reportLunsData[reportLunsOffset + 5] = M_Byte2(identifier);
                        reportLunsData[reportLunsOffset + 6] = M_Byte1(identifier);
                        reportLunsData[reportLunsOffset + 7] = M_Byte0(identifier);
                    }
                    //set report luns length
                    reportLunsData[0] = M_Byte3(reportLunsOffset);
                    reportLunsData[1] = M_Byte2(reportLunsOffset);
                    reportLunsData[2] = M_Byte1(reportLunsOffset);
                    reportLunsData[3] = M_Byte0(reportLunsOffset);
                }
                else
                {
                    singleLun = true;
                }
            }
            else
            {
                //dummy up single lun data
                singleLun = true;
            }
        }
        else
        {
            //dummy up a single lun
            singleLun = true;
        }
        safe_Free_aligned(activeNamespaces);
        if (singleLun)
        {
            reportLunsDataLength += 8;
            reportLunsData = (uint8_t*)calloc(reportLunsDataLength, sizeof(uint8_t));
            reportLunsData[15] = device->drive_info.namespaceID > 0 ? device->drive_info.namespaceID - 1 : 0;
            reportLunsData[0] = M_Byte3(reportLunsDataLength);
            reportLunsData[1] = M_Byte2(reportLunsDataLength);
            reportLunsData[2] = M_Byte1(reportLunsDataLength);
            reportLunsData[3] = M_Byte0(reportLunsDataLength);
        }
    }
        break;
    case 0x01:
    case 0x10:
    case 0x12:
        emptyData = true;
        break;
    default:
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x25, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        break;
    }
    if (emptyData)
    {
        //allocate zeroed data for the minimum length we need to return
        reportLunsData = (uint8_t*)calloc(reportLunsDataLength, sizeof(uint8_t));
    }
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, reportLunsData, M_Min(reportLunsDataLength, allocationLength));
    }
    safe_Free(reportLunsData);
    return ret;
}
//TODO: if any kind of "device fault" occurs, send back a sense code similar to SAT with ATA devices
int sntl_Translate_SCSI_Test_Unit_Ready_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (((fieldPointer = 1) != 0 && scsiIoCtx->cdb[1] != 0)
        || ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0)
        || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0)
        || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
#if defined (SNTL_EXT)
    //If the device supports sanitize or DST, check if either of these is in progress to report that before returing the default "ready"
    if (scsiIoCtx->device->drive_info.IdentifyData.nvme.ctrl.sanicap != 0)
    {
        //sanitize is supported. Check if sanitize is currently running or not
        uint8_t logPage[512] = { 0 };
        nvmeGetLogPageCmdOpts sanitizeLog;
        memset(&sanitizeLog, 0, sizeof(nvmeGetLogPageCmdOpts));
        sanitizeLog.addr = logPage;
        sanitizeLog.dataLen = 512;
        sanitizeLog.lid = NVME_LOG_SANITIZE_ID;
        if (SUCCESS == nvme_Get_Log_Page(device, &sanitizeLog))
        {
            uint16_t sstat = M_BytesTo2ByteValue(logPage[3], logPage[2]);
            uint8_t sanitizeStatus = M_GETBITRANGE(sstat, 2, 0);
            if(sanitizeStatus == 0x2)//sanitize in progress
            {
                //set sense data to in progress and set a progress indicator!
                sntl_Set_Sense_Key_Specific_Descriptor_Progress_Indicator(senseKeySpecificDescriptor, M_BytesTo2ByteValue(logPage[1], logPage[0]));
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NOT_READY, 0x04, 0x1B, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return SUCCESS;
            }
            else if (sanitizeStatus == 0x3)//sanitize failed
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                return SUCCESS;
            }
        }//no need for an else. We shouldn't fail just because this log read failed.
    }
#endif
    //SNTL only specifies saying if the drive is ready for commands or not...just going to say ready.
    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    return ret;
}

int sntl_Translate_SCSI_Write_Long(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_LONG_10_CMD &&
        (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 4, 0) != 0)
            || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0))
        )
    {
        //invalid field in CDB
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    else if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_LONG_16_CMD &&
        (((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0)
            || ((fieldPointer = 11) != 0 && scsiIoCtx->cdb[11] != 0)
            || ((fieldPointer = 14) != 0 && scsiIoCtx->cdb[14] != 0))
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)//check that write uncorrectable command is supported
    {
        bool correctionDisabled = false;
        bool writeUncorrectableError = false;
        bool physicalBlock = false;
        uint64_t lba = 0;
        uint16_t byteTransferLength = 0;
        switch (scsiIoCtx->cdb[OPERATION_CODE])
        {
        case WRITE_LONG_10_CMD:
            if (scsiIoCtx->cdb[1] & BIT7)
            {
                correctionDisabled = true;
            }
            if (scsiIoCtx->cdb[1] & BIT6)
            {
                writeUncorrectableError = true;
            }
            if (scsiIoCtx->cdb[1] & BIT5)
            {
                physicalBlock = true;
            }
            lba = M_BytesTo4ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            byteTransferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
            break;
        case WRITE_LONG_16_CMD://skipping checking service action as it should already be checked by now.-TJE
            if (scsiIoCtx->cdb[1] & BIT7)
            {
                correctionDisabled = true;
            }
            if (scsiIoCtx->cdb[1] & BIT6)
            {
                writeUncorrectableError = true;
            }
            if (scsiIoCtx->cdb[1] & BIT5)
            {
                physicalBlock = true;
            }
            lba = M_BytesTo8ByteValue(scsiIoCtx->cdb[2], scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5], scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
            byteTransferLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[12], scsiIoCtx->cdb[13]);
            break;
        default:
            bitPointer = 7;
            fieldPointer = 0;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return NOT_SUPPORTED;
        }
        if (byteTransferLength == 0)
        {
            if (correctionDisabled)
            {
                if (writeUncorrectableError)//NOTE! SNTL version 1.1 has this marked as must be zero, but that doesn't match later versions or what is in SBC for a non-data version of this command!
                {
                    if (physicalBlock)
                    {
                        //this bit is not allowed
                        fieldPointer = 1;
                        bitPointer = 5;
                        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        ret = NOT_SUPPORTED;
                    }
                    else
                    {
                        ret = nvme_Write_Uncorrectable(device, lba, 0);//only 1 block at a time
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                }
                else
                {
                    //the write uncorrectable bit MUST be set!
                    fieldPointer = 1;
                    bitPointer = 6;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            else
            {
                //cor_dis must be set
                fieldPointer = 1;
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
        }
        else
        {
            fieldPointer = 7;
            bitPointer = 7;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        }
    }
    else
    {
        ret = NOT_SUPPORTED;
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    return ret;
}

int sntl_Translate_SCSI_Send_Diagnostic_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t selfTestCode = (scsiIoCtx->cdb[1] >> 5) & 0x07;
    bool selfTest = false;
    uint16_t parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[3], scsiIoCtx->cdb[4]);
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    if (((fieldPointer = 1) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[1] & BIT3)//reserved
        || ((fieldPointer = 1) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->cdb[1] & BIT1)//devoffline
        || ((fieldPointer = 1) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[1] & BIT0)//unitoffline
        || ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0)//reserved
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[1] & BIT2)
    {
        selfTest = true;
    }
    if (parameterListLength == 0)
    {
        //bool nvmeSelfTestSupported = false;
        if (selfTest)
        {
            ret = SUCCESS;//nothing to do! We may be able to take hints from SAT to implement some kind of similar extension in the future.
        }
        else
        {
#if defined (SNTL_EXT)
            if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT4)//DST supported
            {
                //NOTE: doing all namespaces for now...not sure if this should be changed in the future.
                switch (selfTestCode)
                {
                case 0://default self test
                    ret = SUCCESS;
                    break;
                case 1://background self test
                    ret = nvme_Device_Self_Test(device, UINT32_MAX, 1);
                    if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                    break;
                case 2://background extended self test
                    ret = nvme_Device_Self_Test(device, UINT32_MAX, 2);
                    if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                    break;
                case 4://abort background self test
                    ret = nvme_Device_Self_Test(device, UINT32_MAX, 0xF);
                    if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                    break;
                default:
                    ret = NOT_SUPPORTED;
                    bitPointer = 7;
                    fieldPointer = 1;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    break;
                }
            }
            else
            {
#endif
                if (selfTestCode == 0)
                {
                    ret = SUCCESS;//nothing to do! Take hints from SAT to translate the self test code to a DST if DST is supported.
                }
                else
                {
                    ret = NOT_SUPPORTED;
                    bitPointer = 7;
                    fieldPointer = 1;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
#if defined (SNTL_EXT)
            }
#endif
        }
    }
    else
    {
        fieldPointer = 3;
        bitPointer = 7;
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    return ret;
}

int sntl_Translate_SCSI_Write_Buffer_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t mode = scsiIoCtx->cdb[1] & 0x1F;
    uint8_t modeSpecific = (scsiIoCtx->cdb[1] >> 5) & 0x07;
    uint8_t bufferID = scsiIoCtx->cdb[2];
    uint32_t bufferOffset = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    uint32_t parameterListLength = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //Check if download is supported...if not, then invalid operation code!
    if (!(device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT2))
    {
        fieldPointer = 0;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    switch (mode)
    {
    case 0x05://Full buffer given.
        if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0)
            /*&& ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && bufferID == 0)*///allow non-zero bufer ID for activation part of this operation
            && ((fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && bufferOffset == 0)
            && ((fieldPointer = 6) != 0 && (bitPointer = 7) != 0 && parameterListLength != 0)
            )
        {
            //send in 1 command, followed by activate. TODO: ifdef for sending in chunks? (might be needed in case system doesn't allow a transfer of that size)
            ret = nvme_Firmware_Image_Dl(device, bufferOffset, parameterListLength, scsiIoCtx->pdata);
            if (ret != SUCCESS)
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
            //send the activate command!
            nvmeFWCommitAction commitAction = NVME_CA_REPLACE_ACTIVITE_ON_RST;
            if (device->drive_info.IdentifyData.nvme.ctrl.frmw & BIT4)
            {
                commitAction = NVME_CA_ACTIVITE_IMMEDIATE;
            }
            ret = nvme_Firmware_Commit(device, commitAction, bufferID);//todo: need to catch any errors
            if (ret != SUCCESS)
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
            //need to check if the status of the commit says we need a reset too!
            int resetType = 0;//0 = no reset, 1 = reset, 2 = susb system reset
            bool dnr = false, more = false;
            uint8_t sct = 0, sc = 0;
            get_NVMe_Status_Fields_From_DWord(device->drive_info.lastNVMeResult.lastNVMeStatus, &dnr, &more, &sct, &sc);
            if (sct == NVME_SCT_COMMAND_SPECIFIC_STATUS)
            {
                switch (sc)
                {
                case NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET:
                    resetType = 2;
                    break;
                case NVME_CMD_SP_SC_FW_ACT_REQ_RESET:
                case NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET:
                    resetType = 1;
                    break;
                default:
                    resetType = 0;
                    break;
                }
            }
            if (commitAction == NVME_CA_REPLACE_ACTIVITE_ON_RST || resetType > 0)
            {
                if (resetType == 0)
                {
                    resetType = 1;
                }
                if (resetType == 1)
                {
                    //reset
                    nvme_Reset(device);
                }
                else if (resetType == 2)
                {
                    //subsystem reset
                    nvme_Subsystem_Reset(device);
                }
            }
        }
        else //these fields are reserved or vendor specific so make sure they are zeroed out
        {
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        }
        break;
#if defined SNTL_EXT
    case 0x0D://Download code. Make sure the other activate bits aren't set
        //check mode specific for PO-ACT and HR-ACT bits
        if ((modeSpecific & BIT2) == 0 || modeSpecific & BIT1)
        {
            fieldPointer = 1;
            bitPointer = 7;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            break;
        }
        //fall through to finish processing the command
#endif
    case 0x0E://Firmware image download
#if defined SNTL_EXT
            if (((mode == 0x0E && ((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0)) || mode == 0x0D) //mode specific is reserved in this mode (0x0E)
#else
            if (((mode == 0x0E && ((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0))) //mode specific is reserved in this mode (0x0E)
#endif
                && ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && bufferID == 0)//buffer ID should be zero since we don't support other buffer IDs
                )
            {
                //TODO: Check the granularity requirements from fwug in controller identify data so we can check the command properly before issuing it.
                uint32_t granularity = device->drive_info.IdentifyData.nvme.ctrl.fwug * 4096;//this is in bytes
                if (device->drive_info.IdentifyData.nvme.ctrl.fwug == UINT8_MAX)
                {
                    granularity = 1;//no restriction
                }
                else if (device->drive_info.IdentifyData.nvme.ctrl.fwug == 0)
                {
                    granularity = 4096;//error on this side for caution!
                }
                if ((bufferOffset % granularity) == 0 && (parameterListLength % granularity) == 0)//check length and offset for the same granularity requirements!
                {
                    ret = nvme_Firmware_Image_Dl(device, bufferOffset, parameterListLength, scsiIoCtx->pdata);
                    if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                        return ret;
                    }
                }
                else //invalid parameter list length! must be in 200h sizes only!
                {
                    fieldPointer = 6;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                }
            }
            else //these fields must be zeroed out
            {
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            }
        break;
    case 0x0F://Firmware image commit
            if (((fieldPointer = 1) != 0 && (bitPointer = 7) != 0 && modeSpecific == 0)
                && ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && bufferID == 0)
                && ((fieldPointer = 3) != 0 && (bitPointer = 7) != 0 && bufferOffset == 0)
                && ((fieldPointer = 6) != 0 && (bitPointer = 7) != 0 && parameterListLength == 0)
                )
            {
                //TODO: Store a way of to switch to existing firmware images? This would be better to handle when we're switching between existing images...unlikley with SCSI translation though
                nvmeFWCommitAction commitAction = NVME_CA_REPLACE_ACTIVITE_ON_RST;
                if (device->drive_info.IdentifyData.nvme.ctrl.frmw & BIT4)
                {
                    commitAction = NVME_CA_ACTIVITE_IMMEDIATE;
                }
                ret = nvme_Firmware_Commit(device, commitAction, bufferID);
                if (ret != SUCCESS)
                {
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    return ret;
                }
                //need to check if the status of the commit says we need a reset too!
                int resetType = 0;//0 = no reset, 1 = reset, 2 = susb system reset
                bool dnr = false, more = false;
                uint8_t sct = 0, sc = 0;
                get_NVMe_Status_Fields_From_DWord(device->drive_info.lastNVMeResult.lastNVMeStatus, &dnr, &more, &sct, &sc);
                if (sct == NVME_SCT_COMMAND_SPECIFIC_STATUS)
                {
                    switch (sc)
                    {
                    case NVME_CMD_SP_SC_FW_ACT_REQ_NVM_SUBSYS_RESET:
                        resetType = 2;
                        break;
                    case NVME_CMD_SP_SC_FW_ACT_REQ_RESET:
                    case NVME_CMD_SP_SC_FW_ACT_REQ_CONVENTIONAL_RESET:
                        resetType = 1;
                        break;
                    default:
                        resetType = 0;
                        break;
                    }
                }
                if (commitAction == NVME_CA_REPLACE_ACTIVITE_ON_RST || resetType > 0)
                {
                    if (resetType == 0)
                    {
                        resetType = 1;
                    }
                    if (resetType == 1)
                    {
                        //reset
                        nvme_Reset(device);
                    }
                    else if (resetType == 2)
                    {
                        //subsystem reset
                        nvme_Subsystem_Reset(device);
                    }
                }
            }
            else
            {
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            }
        break;
    case 0x07://Download code and activate on final segment...we cannot support this without some other way of notifying us that it's the final segment. Can reinvestigate it later!
    default://unknown or unsupported mode
        fieldPointer = 1;
        bitPointer = 4;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        break;
    }
    return ret;
}

int sntl_Translate_SCSI_Start_Stop_Unit_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t powerConditionModifier = M_Nibble0(scsiIoCtx->cdb[3]);
    uint8_t powerCondition = M_Nibble1(scsiIoCtx->cdb[4]);
    bool noFlush = false;
    bool start = false;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 0) != 0)//bit zero is immediate which we don't support
        || ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0)
        || ((fieldPointer = 3) != 0 && M_Nibble1(scsiIoCtx->cdb[3]) != 0)
        || ((fieldPointer = 4) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->cdb[4] & BIT3)
        || ((fieldPointer = 4) != 0 && (bitPointer = 2) != 0 && scsiIoCtx->cdb[4] & BIT2)//don't support loej bit
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[4] & BIT2)
    {
        noFlush = true;
    }
    if (scsiIoCtx->cdb[4] & BIT0)
    {
        start = true;
    }

    switch (powerCondition)
    {
    case 0x00://start valid
        if (powerConditionModifier == 0)
        {
            //process start and loej bits
            if (start)
            {
                nvmeFeaturesCmdOpt features;
                memset(&features, 0, sizeof(nvmeFeaturesCmdOpt));
                features.fid = NVME_FEAT_POWER_MGMT_;
                features.featSetGetValue = 0;//power state zero
                if (!noFlush)
                {
                    ret = nvme_Flush(device);
                    if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                        return ret;
                    }
                }
                ret = nvme_Set_Features(device, &features);
                if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                        return ret;
                    }
            }
            else
            {
                nvmeFeaturesCmdOpt features;
                memset(&features, 0, sizeof(nvmeFeaturesCmdOpt));
                features.fid = NVME_FEAT_POWER_MGMT_;
                //send lowest state, which is a higher number value for lowest power consumption (zero means max).
                features.featSetGetValue = device->drive_info.IdentifyData.nvme.ctrl.npss;
                if (!noFlush)
                {
                    ret = nvme_Flush(device);
                    if (ret != SUCCESS)
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                        return ret;
                    }
                }
                ret = nvme_Set_Features(device, &features);
                if (ret != SUCCESS)
                {
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    return ret;
                }
            }
        }
        else
        {
            //invalid power condition modifier
            fieldPointer = 3;
            bitPointer = 3;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
        break;
    case 0x01://active
        if (powerConditionModifier == 0)
        {
            nvmeFeaturesCmdOpt features;
            memset(&features, 0, sizeof(nvmeFeaturesCmdOpt));
            features.fid = NVME_FEAT_POWER_MGMT_;
            features.featSetGetValue = 0;//power state zero
            if (!noFlush)
            {
                ret = nvme_Flush(device);
                if (ret != SUCCESS)
                {
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    return ret;
                }
            }
            ret = nvme_Set_Features(device, &features);
            if (ret != SUCCESS)
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
        else
        {
            //invalid power condition modifier
            fieldPointer = 3;
            bitPointer = 3;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
        break;
    //case 0x0A://Force Idle (check power condition modifier for specific idle state)
    //    enable = true;
    //    //fall through
    case 0x02://Idle (check power condition modifier for specific idle state)
        if (powerConditionModifier <= 2)
        {
            nvmeFeaturesCmdOpt features;
            memset(&features, 0, sizeof(nvmeFeaturesCmdOpt));
            features.fid = NVME_FEAT_POWER_MGMT_;
            features.featSetGetValue = powerConditionModifier + 1;
            if (!noFlush)
            {
                ret = nvme_Flush(device);
                if (ret != SUCCESS)
                {
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    return ret;
                }
            }
            ret = nvme_Set_Features(device, &features);
            if (ret != SUCCESS)
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
        else
        {
            //invalid power condition modifier
            fieldPointer = 3;
            bitPointer = 3;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
        break;
    //case 0x0B://Force Standby (check power condition modifier for specific idle state)
    //    enable = true;
    //    //fall through
    case 0x03://standby (check power condition modifier for specific idle state)
        if (powerConditionModifier == 0)
        {
            nvmeFeaturesCmdOpt features;
            memset(&features, 0, sizeof(nvmeFeaturesCmdOpt));
            features.fid = NVME_FEAT_POWER_MGMT_;
            features.featSetGetValue = device->drive_info.IdentifyData.nvme.ctrl.npss - 2;
            if (!noFlush)
            {
                ret = nvme_Flush(device);
                if (ret != SUCCESS)
                {
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    return ret;
                }
            }
            ret = nvme_Set_Features(device, &features);
            if (ret != SUCCESS)
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
        else if (powerConditionModifier == 1)
        {
            nvmeFeaturesCmdOpt features;
            memset(&features, 0, sizeof(nvmeFeaturesCmdOpt));
            features.fid = NVME_FEAT_POWER_MGMT_;
            features.featSetGetValue = device->drive_info.IdentifyData.nvme.ctrl.npss - 1;
            if (!noFlush)
            {
                ret = nvme_Flush(device);
                if (ret != SUCCESS)
                {
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    return ret;
                }
            }
            ret = nvme_Set_Features(device, &features);
            if (ret != SUCCESS)
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
        else
        {
            //invalid power condition modifier
            fieldPointer = 3;
            bitPointer = 3;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
        break;
    //case 0x07://LU Control
    default://invalid power condition
        fieldPointer = 4;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        break;
    }
    return ret;
}

int sntl_Translate_SCSI_Unmap_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //not supporting the ancor bit
    //group number should be zero
    uint16_t parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    //filter out invalid fields
    if (((fieldPointer = 1) && scsiIoCtx->cdb[1] != 0)
        || ((fieldPointer = 2) && scsiIoCtx->cdb[2] != 0)
        || ((fieldPointer = 3) && scsiIoCtx->cdb[3] != 0)
        || ((fieldPointer = 4) && scsiIoCtx->cdb[4] != 0)
        || ((fieldPointer = 5) && scsiIoCtx->cdb[5] != 0)
        || ((fieldPointer = 6) && scsiIoCtx->cdb[6] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
            if (fieldPointer == 6 && bitPointer < 5)
            {
                bitPointer = 5;//setting group number as invalid field
            }
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (parameterListLength > 0 && parameterListLength < 8)
    {
        fieldPointer = 7;
        bitPointer = 7;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //parameter list length error
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x1A, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    else if (parameterListLength != 0 && scsiIoCtx->pdata)
    {
        //process the contents of the parameter data and send some commands to the drive
        uint16_t unmapBlockDescriptorLength = (M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]) / 16) * 16;//this can be set to zero, which is NOT an error. Also, I'm making sure this is a multiple of 16 to avoid partial block descriptors-TJE
        if (unmapBlockDescriptorLength > 0)
        {
            uint8_t *dsmBuffer = (uint8_t*)calloc_aligned(4096, sizeof(uint8_t), device->os_info.minimumAlignment);//allocate the max size the device supports...we'll fill in as much as we need to
            //need to check to make sure there weren't any truncated block descriptors before we begin
            uint16_t minBlockDescriptorLength = M_Min(unmapBlockDescriptorLength + 8, parameterListLength);
            uint16_t unmapBlockDescriptorIter = 8;
            uint64_t numberOfLBAsToDeallocate = 0;//this will be checked later to make sure it isn't greater than what we reported on the VPD pages
            uint16_t numberOfBlockDescriptors = 0;//this will be checked later to make sure it isn't greater than what we reported on the VPD pages
            uint16_t nvmeDSMOffset = 0;
            uint32_t numberOfRanges = 0;
            if (!dsmBuffer)
            {
                //lets just set this error for now...-TJE
                fieldPointer = 7;
                bitPointer = 7;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x1A, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return MEMORY_FAILURE;
            }
            //start building the buffer to transfer with data set management
            for (; unmapBlockDescriptorIter < minBlockDescriptorLength; unmapBlockDescriptorIter += 16, numberOfBlockDescriptors++)
            {
                bool exitLoop = false;
                uint64_t unmapLogicalBlockAddress = M_BytesTo8ByteValue(scsiIoCtx->pdata[unmapBlockDescriptorIter + 0], scsiIoCtx->pdata[unmapBlockDescriptorIter + 1], scsiIoCtx->pdata[unmapBlockDescriptorIter + 2], scsiIoCtx->pdata[unmapBlockDescriptorIter + 3], scsiIoCtx->pdata[unmapBlockDescriptorIter + 4], scsiIoCtx->pdata[unmapBlockDescriptorIter + 5], scsiIoCtx->pdata[unmapBlockDescriptorIter + 6], scsiIoCtx->pdata[unmapBlockDescriptorIter + 7]);
                uint32_t unmapNumberOfLogicalBlocks = M_BytesTo4ByteValue(scsiIoCtx->pdata[unmapBlockDescriptorIter + 8], scsiIoCtx->pdata[unmapBlockDescriptorIter + 9], scsiIoCtx->pdata[unmapBlockDescriptorIter + 10], scsiIoCtx->pdata[unmapBlockDescriptorIter + 11]);
                if (unmapNumberOfLogicalBlocks == 0)
                {
                    //nothing to unmap...
                    continue;
                }
                //increment this so we can check if we were given more to do than we support (to be checked after the loop)
                numberOfLBAsToDeallocate += unmapNumberOfLogicalBlocks;
                //check we aren't trying to go over the end of the drive
                if (unmapLogicalBlockAddress > device->drive_info.deviceMaxLba)
                {
                    fieldPointer = unmapBlockDescriptorIter + 0;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                    ret = FAILURE;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x21, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    exitLoop = true;
                    break;
                }
                else if (unmapLogicalBlockAddress + unmapNumberOfLogicalBlocks > device->drive_info.deviceMaxLba)
                {
                    fieldPointer = unmapBlockDescriptorIter + 8;
                    bitPointer = 7;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                    ret = FAILURE;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x21, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    exitLoop = true;
                    break;
                }
                //check that we haven't had too many block descriptors yet
                if (numberOfBlockDescriptors > (255))//max of 255 ranges
                {
                    //not setting sense key specific information because it's not clear in this condition what error we should point to
                    ret = FAILURE;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                    exitLoop = true;
                    break;
                }
                //check that we haven't been asked to TRIM more LBAs than we care to support in this code
                if (numberOfLBAsToDeallocate > (UINT64_C(255) * UINT32_MAX))//max of 255 ranges * UINT32_MAX for number LBAs in each range
                {
                    //not setting sense key specific information because it's not clear in this condition what error we should point to
                    ret = FAILURE;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                    exitLoop = true;
                    break;
                }
                //now that we've done all of our checks so far, start setting up the buffer
                //break this block descriptor into multiple ATA entries if necessary
                while (unmapNumberOfLogicalBlocks > 0)
                {
                    //range
                    dsmBuffer[nvmeDSMOffset + 4] = M_Byte3(unmapNumberOfLogicalBlocks);
                    dsmBuffer[nvmeDSMOffset + 5] = M_Byte2(unmapNumberOfLogicalBlocks);
                    dsmBuffer[nvmeDSMOffset + 6] = M_Byte1(unmapNumberOfLogicalBlocks);
                    dsmBuffer[nvmeDSMOffset + 7] = M_Byte0(unmapNumberOfLogicalBlocks);
                    ++numberOfRanges;
                    //lba
                    dsmBuffer[nvmeDSMOffset + 8] = M_Byte7(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 9] = M_Byte6(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 10] = M_Byte5(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 11] = M_Byte4(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 12] = M_Byte3(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 13] = M_Byte2(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 14] = M_Byte1(unmapLogicalBlockAddress);
                    dsmBuffer[nvmeDSMOffset + 15] = M_Byte0(unmapLogicalBlockAddress);
                    
                    //now increment the nvmeDSMOffset
                    nvmeDSMOffset += 16;
                    //check if the NVMe DSM buffer is full...if it is and there are more or potentially more block descriptors, send the command now
                    if ((nvmeDSMOffset > 4096) && ((unmapBlockDescriptorIter + 16) < minBlockDescriptorLength))
                    {
                        if(SUCCESS == nvme_Dataset_Management(device, numberOfRanges, true, false, false, dsmBuffer, 4096))
                        {
                            //clear the buffer for reuse
                            memset(dsmBuffer, 0, device->drive_info.IdentifyData.ata.Word105 * LEGACY_DRIVE_SEC_SIZE);
                            //reset the ataTrimOffset
                            nvmeDSMOffset = 0;
                            numberOfRanges = 0;
                        }
                        else
                        {
                            ret = FAILURE;
                            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                            exitLoop = true;
                            break;
                        }
                    }
                }
                if (exitLoop)
                {
                    break;
                }
            }
            if (ret == SUCCESS)
            {
                //send the data set management command with whatever is in the trim buffer at this point (all zeros is safe to send if we do get that)
                if (SUCCESS != nvme_Dataset_Management(device, numberOfRanges, true, false, false, dsmBuffer, 4096))
                {
                    ret = FAILURE;
                    set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                }
            }
            safe_Free_aligned(dsmBuffer);
        }
    }
    return ret;
}

//TODO: Add support for Format in progress
//Add support sanitize commands with our extensions
//Add detecting when background self test is in progress and reporting progress from that
//TODO: Figure out other ways to improve what happens in here to pass back out that would be useful. SNTL spec doesn't really say much about what to do here.
int sntl_Translate_SCSI_Request_Sense_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t senseData[SPC3_SENSE_LEN] = { 0 };
    bool descriptorFormat = false;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 1) != 0)
        || ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0)
        || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[1] & BIT0)
    {
        descriptorFormat = true;
    }
#if defined (SNTL_EXT)
    if (scsiIoCtx->device->drive_info.IdentifyData.nvme.ctrl.sanicap != 0)
    {
        //sanitize is supported. Check if sanitize is currently running or not
        uint8_t logPage[512] = { 0 };
        nvmeGetLogPageCmdOpts sanitizeLog;
        memset(&sanitizeLog, 0, sizeof(nvmeGetLogPageCmdOpts));
        sanitizeLog.addr = logPage;
        sanitizeLog.dataLen = 512;
        sanitizeLog.lid = NVME_LOG_SANITIZE_ID;
        if (SUCCESS == nvme_Get_Log_Page(device, &sanitizeLog))
        {
            uint16_t sstat = M_BytesTo2ByteValue(logPage[3], logPage[2]);
            uint8_t sanitizeStatus = M_GETBITRANGE(sstat, 2, 0);
            if (sanitizeStatus == 0x2)//sanitize in progress
            {
                //set sense data to in progress and set a progress indicator!
                sntl_Set_Sense_Key_Specific_Descriptor_Progress_Indicator(senseKeySpecificDescriptor, M_BytesTo2ByteValue(logPage[1], logPage[0]));
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NOT_READY, 0x04, 0x1B, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return SUCCESS;
            }
            else if (sanitizeStatus == 0x3)//sanitize failed
            {
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                return SUCCESS;
            }
        }//no need for an else. We shouldn't fail just because this log read failed.
    }
    //NOTE: DST progress should only report like this under request sense. In test unit ready, DST in progress should only happen for foreground mode (i.e. captive) which isn't supported on NVMe
    if (scsiIoCtx->device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT4)//DST is supported
    {
        uint8_t logPage[564] = { 0 };
        nvmeGetLogPageCmdOpts dstLog;
        memset(&dstLog, 0, sizeof(nvmeGetLogPageCmdOpts));
        dstLog.addr = logPage;
        dstLog.dataLen = 512;
        dstLog.lid = NVME_LOG_DEV_SELF_TEST;
        if (SUCCESS == nvme_Get_Log_Page(device, &dstLog))
        {
            uint8_t currentSelfTest = M_Nibble0(logPage[0]);
            if (currentSelfTest == 0x1 || currentSelfTest == 0x2)//DST in progress
            {
                //convert progress into a value whose divisor is 65535
                uint16_t dstProgress = 656 * M_GETBITRANGE(logPage[1], 6, 0);//This comes out very close to the actual percent...close enough anyways. There is probably a way to scale it to more even round numbers but this will do fine.
                sntl_Set_Sense_Key_Specific_Descriptor_Progress_Indicator(senseKeySpecificDescriptor, dstProgress);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NOT_READY, 0x04, 0x09, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return SUCCESS;
            }
        }//no need for else for failure...just fall through
    }
#endif
    //read the current power state
    nvmeFeaturesCmdOpt powerState;
    memset(&powerState, 0, sizeof(nvmeFeaturesCmdOpt));
    powerState.fid = 0x02;
    if (SUCCESS != nvme_Get_Features(device, &powerState))
    {
        //TODO: set error based on status code translation
        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
        return ret;
    }
    uint8_t currentPowerState = M_GETBITRANGE(powerState.featSetGetValue, 4, 0);
    if (currentPowerState == 0)
    {
        //low power condition on
        sntl_Set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0x5E, 0, descriptorFormat, NULL, 0);
    }
    else
    {
        sntl_Set_Sense_Data_For_Translation(&senseData[0], SPC3_SENSE_LEN, SENSE_KEY_NO_ERROR, 0, 0, descriptorFormat, NULL, 0);
    }
    //copy back whatever data we set
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, senseData, M_Min(scsiIoCtx->cdb[4], SPC3_SENSE_LEN));
    }
    return ret;
}

int sntl_Translate_Persistent_Reserve_In(tDevice * device, ScsiIoCtx * scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t *persistentReserveData = NULL;
    uint32_t persistentReserveDataLength = 8;//start with this...it could change.
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t serviceAction = M_GETBITRANGE(scsiIoCtx->cdb[1], 4, 0);
    uint16_t allocationLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    //filter out invalid fields
    if (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5) != 0)
        || ((fieldPointer = 2) != 0 && scsiIoCtx->cdb[2] != 0)
        || ((fieldPointer = 3) != 0 && scsiIoCtx->cdb[3] != 0)
        || ((fieldPointer = 4) != 0 && scsiIoCtx->cdb[4] != 0)
        || ((fieldPointer = 5) != 0 && scsiIoCtx->cdb[5] != 0)
        || ((fieldPointer = 6) != 0 && scsiIoCtx->cdb[6] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            if (fieldPointer == 1)
            {
                reservedByteVal &= 0xE0;//strip off the service action bits since those are usable.
            }
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    //process based on service action
    switch (serviceAction)
    {
    case 0://read keys
    {
        uint8_t nvmeReportKeys[4096] = { 0 };//I hope this is big enough...may need to redo this!
        if (SUCCESS != nvme_Reservation_Report(device, false, nvmeReportKeys, 4096))
        {
            //Set error based on the status the controller replied with!!!
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
        uint16_t numberOfRegisteredControllers = M_BytesTo2ByteValue(nvmeReportKeys[5], nvmeReportKeys[6]);
        persistentReserveDataLength = (numberOfRegisteredControllers * 8) + 8;
        //allocate the memory we need.
        persistentReserveData = (uint8_t*)calloc(persistentReserveDataLength, sizeof(uint8_t));
        //set PRGeneration (remember, the endianness is different!)
        persistentReserveData[0] = nvmeReportKeys[3];
        persistentReserveData[1] = nvmeReportKeys[2];
        persistentReserveData[2] = nvmeReportKeys[1];
        persistentReserveData[3] = nvmeReportKeys[0];
        //set the additional length
        persistentReserveData[4] = M_Byte3(persistentReserveDataLength - 8);
        persistentReserveData[5] = M_Byte2(persistentReserveDataLength - 8);
        persistentReserveData[6] = M_Byte1(persistentReserveDataLength - 8);
        persistentReserveData[7] = M_Byte0(persistentReserveDataLength - 8);
        //now set the keys in the list.
        uint32_t persistentReseverOffset = 8;//each key is 8 bytes and starts at this offset
        uint32_t nvmeReportOffset = 24;//increment by 24 for each key due to extra data NVMe returns
        for (; persistentReseverOffset < persistentReserveDataLength && nvmeReportOffset < 4096; persistentReseverOffset += 8, nvmeReportOffset += 24)
        {
            persistentReserveData[persistentReseverOffset + 0] = nvmeReportKeys[nvmeReportOffset + 23];
            persistentReserveData[persistentReseverOffset + 1] = nvmeReportKeys[nvmeReportOffset + 22];
            persistentReserveData[persistentReseverOffset + 2] = nvmeReportKeys[nvmeReportOffset + 21];
            persistentReserveData[persistentReseverOffset + 3] = nvmeReportKeys[nvmeReportOffset + 20];
            persistentReserveData[persistentReseverOffset + 4] = nvmeReportKeys[nvmeReportOffset + 19];
            persistentReserveData[persistentReseverOffset + 5] = nvmeReportKeys[nvmeReportOffset + 18];
            persistentReserveData[persistentReseverOffset + 6] = nvmeReportKeys[nvmeReportOffset + 17];
            persistentReserveData[persistentReseverOffset + 7] = nvmeReportKeys[nvmeReportOffset + 16];
        }
    }
        break;
    case 1://read reservation
    {
        uint8_t nvmeReport[4096] = { 0 };//I hope this is big enough...may need to redo this!
        if (SUCCESS != nvme_Reservation_Report(device, false, nvmeReport, 4096))
        {
            //Set error based on the status the controller replied with!!!
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
        //uint16_t numberOfRegisteredControllers = M_BytesTo2ByteValue(nvmeReport[5], nvmeReport[6]);
        //figure out what, if any controller is holding a reservation
        bool foundReservationActive = false;
        uint32_t nvmeReportOffset = 24;//increment by 24 for each key due to extra data NVMe returns
        for (; nvmeReportOffset < 4096; nvmeReportOffset += 24)
        {
            if (nvmeReport[nvmeReportOffset + 2] & BIT2)
            {
                foundReservationActive = true;
                break;
            }
        }
        persistentReserveDataLength = 8;
        if (foundReservationActive)
        {
            persistentReserveDataLength += 16;
        }
        //allocate the memory we need.
        persistentReserveData = (uint8_t*)calloc(persistentReserveDataLength, sizeof(uint8_t));
        //set PRGeneration (remember, the endianness is different!)
        persistentReserveData[0] = nvmeReport[3];
        persistentReserveData[1] = nvmeReport[2];
        persistentReserveData[2] = nvmeReport[1];
        persistentReserveData[3] = nvmeReport[0];
        //set the additional length
        persistentReserveData[4] = M_Byte3(persistentReserveDataLength - 8);
        persistentReserveData[5] = M_Byte2(persistentReserveDataLength - 8);
        persistentReserveData[6] = M_Byte1(persistentReserveDataLength - 8);
        persistentReserveData[7] = M_Byte0(persistentReserveDataLength - 8);
        if (foundReservationActive)
        {
            //set the key from controller holding reservation
            persistentReserveData[8] = nvmeReport[nvmeReportOffset + 23];
            persistentReserveData[9] = nvmeReport[nvmeReportOffset + 22];
            persistentReserveData[10] = nvmeReport[nvmeReportOffset + 21];
            persistentReserveData[11] = nvmeReport[nvmeReportOffset + 20];
            persistentReserveData[12] = nvmeReport[nvmeReportOffset + 19];
            persistentReserveData[13] = nvmeReport[nvmeReportOffset + 18];
            persistentReserveData[14] = nvmeReport[nvmeReportOffset + 17];
            persistentReserveData[15] = nvmeReport[nvmeReportOffset + 16];
            //set scope (0) and type (R-Type - translate to SCSI)
            switch (nvmeReport[4])
            {
            case 0:
                persistentReserveData[21] = 0;
                break;
            case 1:
                persistentReserveData[21] = 1;
                break;
            case 2:
                persistentReserveData[21] = 3;
                break;
            case 3:
                persistentReserveData[21] = 5;
                break;
            case 4:
                persistentReserveData[21] = 6;
                break;
            case 5:
                persistentReserveData[21] = 7;
                break;
            case 6:
                persistentReserveData[21] = 8;
                break;
            default:
                persistentReserveData[21] = 0x0F;//set to something invalid..we should be able to get this right
                break;
            }
        }
    }
        break;
    case 2://report capabilities
    {
        //send NVMe identify command, CNS set to 0, current namespace being queried.
        if (SUCCESS != nvme_Identify(device, (uint8_t*)&device->drive_info.IdentifyData.nvme.ns, device->drive_info.namespaceID, 0))
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
        //then do a get features with FID set to 83h (reservation persistence)
        nvmeFeaturesCmdOpt getReservationPersistence;
        memset(&getReservationPersistence, 0, sizeof(nvmeFeaturesCmdOpt));
        getReservationPersistence.fid = 0x83;
        if (SUCCESS != nvme_Get_Features(device, &getReservationPersistence))
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
        //Both commands must complete before translating!
        persistentReserveDataLength = 8;
        persistentReserveData = (uint8_t*)calloc(persistentReserveDataLength, sizeof(uint8_t));
        //length
        persistentReserveData[0] = 0;
        persistentReserveData[1] = 0x08;
        //set ATP_C bit
        persistentReserveData[2] |= BIT2;
        //set PTPL_C bit
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT0)
        {
            persistentReserveData[2] |= BIT0;
        }
        //TMV set to 1
        persistentReserveData[3] |= BIT7;
        //allowed commands set to zero
        //set PTL_A
        if (getReservationPersistence.featSetGetValue & BIT0)
        {
            persistentReserveData[3] |= BIT0;
        }
        //set the type mask
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT1)
        {
            //wr_ex
            persistentReserveData[4] |= BIT1;
        }
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT2)
        {
            //ex_ac
            persistentReserveData[4] |= BIT3;
        }
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT3)
        {
            //wr_ex_ro
            persistentReserveData[4] |= BIT5;
        }
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT4)
        {
            //ex_ac_ro
            persistentReserveData[4] |= BIT6;
        }
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT5)
        {
            //wr_ex_ar
            persistentReserveData[4] |= BIT7;
        }
        if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT6)
        {
            //ex_ac_ar
            persistentReserveData[5] |= BIT0;
        }
    }
        break;
    case 3://read full status
    {
        uint8_t nvmeReport[4096] = { 0 };//I hope this is big enough...may need to redo this!
        if (SUCCESS != nvme_Reservation_Report(device, false, nvmeReport, 4096))
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
        uint16_t numberOfRegisteredControllers = M_BytesTo2ByteValue(nvmeReport[5], nvmeReport[6]);
        persistentReserveDataLength = (numberOfRegisteredControllers * 32) + 8;//data structure size for full status is 32 bytes
        //allocate the memory we need.
        persistentReserveData = (uint8_t*)calloc(persistentReserveDataLength, sizeof(uint8_t));
        //set PRGeneration (remember, the endianness is different!)
        persistentReserveData[0] = nvmeReport[3];
        persistentReserveData[1] = nvmeReport[2];
        persistentReserveData[2] = nvmeReport[1];
        persistentReserveData[3] = nvmeReport[0];
        //set the additional length
        persistentReserveData[4] = M_Byte3(persistentReserveDataLength - 8);
        persistentReserveData[5] = M_Byte2(persistentReserveDataLength - 8);
        persistentReserveData[6] = M_Byte1(persistentReserveDataLength - 8);
        persistentReserveData[7] = M_Byte0(persistentReserveDataLength - 8);
        //now set the keys in the list.
        uint32_t persistentReseverOffset = 8;//each key is 32 bytes and starts at this offset
        uint32_t nvmeReportOffset = 24;//nvme structures start here. each is 24 bytes in size
        for (; persistentReseverOffset < persistentReserveDataLength && nvmeReportOffset < 4096; persistentReseverOffset += 32, nvmeReportOffset += 24)
        {
            //set reservation key
            persistentReserveData[persistentReseverOffset + 0] = nvmeReport[nvmeReportOffset + 23];
            persistentReserveData[persistentReseverOffset + 1] = nvmeReport[nvmeReportOffset + 22];
            persistentReserveData[persistentReseverOffset + 2] = nvmeReport[nvmeReportOffset + 21];
            persistentReserveData[persistentReseverOffset + 3] = nvmeReport[nvmeReportOffset + 20];
            persistentReserveData[persistentReseverOffset + 4] = nvmeReport[nvmeReportOffset + 19];
            persistentReserveData[persistentReseverOffset + 5] = nvmeReport[nvmeReportOffset + 18];
            persistentReserveData[persistentReseverOffset + 6] = nvmeReport[nvmeReportOffset + 17];
            persistentReserveData[persistentReseverOffset + 7] = nvmeReport[nvmeReportOffset + 16];
            //bytes 8 - 11 are reserved
            //set all_tg_pt to 1
            persistentReserveData[persistentReseverOffset + 12] |= BIT1;
            //set r_holder
            if (nvmeReport[nvmeReportOffset + 2] & BIT0)//SNTL says bit 1, but that is not correct as that bit is still reserved...
            {
                persistentReserveData[persistentReseverOffset + 12] |= BIT0;
            }
            //scope = 0, type is translated
            //set scope (0) and type (R-Type - translate to SCSI)
            switch (nvmeReport[4])
            {
            case 0:
                persistentReserveData[persistentReseverOffset + 13] = 0;
                break;
            case 1:
                persistentReserveData[persistentReseverOffset + 13] = 1;
                break;
            case 2:
                persistentReserveData[persistentReseverOffset + 13] = 3;
                break;
            case 3:
                persistentReserveData[persistentReseverOffset + 13] = 5;
                break;
            case 4:
                persistentReserveData[persistentReseverOffset + 13] = 6;
                break;
            case 5:
                persistentReserveData[persistentReseverOffset + 13] = 7;
                break;
            case 6:
                persistentReserveData[persistentReseverOffset + 13] = 8;
                break;
            default:
                persistentReserveData[persistentReseverOffset + 13] = 0x0F;//set to something invalid..we should be able to get this right
                break;
            }
            //set relative target port identifier to nvme host identifier (swap endianness)
            persistentReserveData[persistentReseverOffset + 18] = nvmeReport[nvmeReportOffset + 1];
            persistentReserveData[persistentReseverOffset + 19] = nvmeReport[nvmeReportOffset + 0];
            //set additional descriptor length to 8 for transport ID
            persistentReserveData[persistentReseverOffset + 20] = 0;
            persistentReserveData[persistentReseverOffset + 21] = 0;
            persistentReserveData[persistentReseverOffset + 22] = 0;
            persistentReserveData[persistentReseverOffset + 23] = 0x08;
            //set transport ID to nvme host idnetifier (remember diffferent endianness...)
            persistentReserveData[persistentReseverOffset + 24] = nvmeReport[nvmeReportOffset + 15];
            persistentReserveData[persistentReseverOffset + 25] = nvmeReport[nvmeReportOffset + 14];
            persistentReserveData[persistentReseverOffset + 26] = nvmeReport[nvmeReportOffset + 13];
            persistentReserveData[persistentReseverOffset + 27] = nvmeReport[nvmeReportOffset + 12];
            persistentReserveData[persistentReseverOffset + 28] = nvmeReport[nvmeReportOffset + 11];
            persistentReserveData[persistentReseverOffset + 29] = nvmeReport[nvmeReportOffset + 10];
            persistentReserveData[persistentReseverOffset + 30] = nvmeReport[nvmeReportOffset + 9];
            persistentReserveData[persistentReseverOffset + 31] = nvmeReport[nvmeReportOffset + 8];
        }
    }
        break;
    default:
        //invalid field in cdb (service action is not valid)
        bitPointer = 4;
        fieldPointer = 1;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, persistentReserveData, M_Min(persistentReserveDataLength, allocationLength));
    }
    safe_Free(persistentReserveData);
    return ret;
}

int sntl_Translate_Persistent_Reserve_Out(tDevice * device, ScsiIoCtx * scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t *persistentReserveData = NULL;
    uint32_t persistentReserveDataLength = 8;//start with this...it could change.
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    uint8_t serviceAction = M_GETBITRANGE(scsiIoCtx->cdb[1], 4, 0);
    uint32_t parameterListLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[5], scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
    uint8_t scope = M_Nibble1(scsiIoCtx->cdb[2]);//must be set to zero
    uint8_t type = M_Nibble0(scsiIoCtx->cdb[2]);//used for some actions, ignored for others
    //filter out invalid fields
    if (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5) != 0)
        || ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && scope != 0)
        || ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0)
        || ((fieldPointer = 4) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[4] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            if (fieldPointer == 1)
            {
                reservedByteVal &= 0xE0;//strip off the service action bits since those are usable.
            }
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    //process based on service action
    //check that parameter length is at least 24 bytes...
    if (parameterListLength < 24)
    {
        //invalid field in cdb (parameter list length)
        bitPointer = 7;
        fieldPointer = 5;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (serviceAction == 7)
    {
        //different parameter data format...handle it separate of other translations.
        //check if any reserved fields are set.
        if (((fieldPointer = 16) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->pdata[16] != 0) //reserved
            || ((fieldPointer = 17) != 0 && (bitPointer = 0) == 0 && M_GETBITRANGE(scsiIoCtx->pdata[17], 7, 1) != 0)//reserved and unreg bit...not supporting unreg since it isn't mentioned in SNTL
            )
        {
            if (bitPointer == 0)
            {
                uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                uint8_t counter = 0;
                if (fieldPointer == 17)
                {
                    reservedByteVal = scsiIoCtx->pdata[fieldPointer] & 0xFE;//remove lower bits since they may be valid
                }
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
            }
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
            //invalid field in CDB
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return ret;
        }
        //TODO: the SNTL doesn't mention what, if anything, to do with the transport ID that can be provided here...ignoring it for now since I don't see a way to send that to the nvme drive.
        //NOTE: NVMe spec doesn't mention the unreg bit or the relative target port identifier...should it be an error?
        //now translate to the register command with the correct inputs for the NVMe data
        //iekey = 0, rrega = 010 (replace)
        uint8_t buffer[16] = { 0 };
        uint8_t changeThroughPowerLoss = 0;//no change
        //set the reservation key
        buffer[0] = scsiIoCtx->pdata[7];
        buffer[1] = scsiIoCtx->pdata[6];
        buffer[2] = scsiIoCtx->pdata[5];
        buffer[3] = scsiIoCtx->pdata[4];
        buffer[4] = scsiIoCtx->pdata[3];
        buffer[5] = scsiIoCtx->pdata[2];
        buffer[6] = scsiIoCtx->pdata[1];
        buffer[7] = scsiIoCtx->pdata[0];
        //set the service action reservation key to new reservation key field
        buffer[8] = scsiIoCtx->pdata[15];
        buffer[9] = scsiIoCtx->pdata[14];
        buffer[10] = scsiIoCtx->pdata[13];
        buffer[11] = scsiIoCtx->pdata[12];
        buffer[12] = scsiIoCtx->pdata[11];
        buffer[13] = scsiIoCtx->pdata[10];
        buffer[14] = scsiIoCtx->pdata[9];
        buffer[15] = scsiIoCtx->pdata[8];
        //aptpl is unused in this translation
        if (SUCCESS != nvme_Reservation_Register(device, changeThroughPowerLoss, false, 0x02, buffer, 16))
        {
            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
            return ret;
        }
    }
    else
    {
        //check if bytes 16 though 19 are set...they are obsolete and not supported.
        if (((fieldPointer = 16) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->pdata[16] != 0) //obsolete (scope specific address)
            || ((fieldPointer = 16) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->pdata[17] != 0)//obsolete (scope specific address)
            || ((fieldPointer = 16) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->pdata[18] != 0)//obsolete (scope specific address)
            || ((fieldPointer = 16) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->pdata[19] != 0)//obsolete (scope specific address)
            || ((fieldPointer = 20) != 0 && (bitPointer = 3) != 0 && M_GETBITRANGE(scsiIoCtx->pdata[20],7, 4) != 0)
            || ((fieldPointer = 20) != 0 && (bitPointer = 3) != 0 && scsiIoCtx->pdata[20] & BIT3)//SPEC_I_PT bit
            || ((fieldPointer = 20) != 0 && (bitPointer = 1) != 0 && scsiIoCtx->pdata[20] & BIT1)//reserved bit
            || ((fieldPointer = 21) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->pdata[21])//reserved
            || ((fieldPointer = 22) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->pdata[22])//obsolete (extent length)
            || ((fieldPointer = 22) != 0 && (bitPointer = 7) != 0 && scsiIoCtx->pdata[23])//obsolete (extent length)
            )
        {
            if (bitPointer == 0)
            {
                uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                uint8_t counter = 0;
                if (fieldPointer == 20)
                {
                    reservedByteVal = scsiIoCtx->pdata[fieldPointer] & 0xF0;//remove lower bits since they may be valid
                }
                while (reservedByteVal > 0 && counter < 8)
                {
                    reservedByteVal >>= 1;
                    ++counter;
                }
                bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
            }
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
            //invalid field in CDB
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return ret;
        }
        bool aptpl = scsiIoCtx->pdata[20] & BIT0;//used on register and register and ignore existing key actions. ignored otherwise
        //NOTE: parameter data is DIFFERENT for register and move. Everything else uses the same format
        switch (serviceAction)
        {
        case 0://register
        case 6://register and ignore existing key
        {
            uint8_t buffer[16] = { 0 };
            uint8_t zeros[8] = { 0 };
            //iekey = 0. (register), iekey = 1 register and ignore existing key
            bool iekey = false;
            uint8_t changeThroughPowerLoss = 2;//10b
            uint8_t rrega = 0;
            if (serviceAction == 6)
            {
                iekey = true;
            }
            //setup the buffer.
            //rrega is zero OR 1 depending on service action key.
            //if service action key is zero, set rrega to 001 (unregister) and set CRKEY to the reservation key.
            //else reservation key is ignored. CRKEY is as though it is reserved. RREGA set to zero
            if (memcmp(zeros, &scsiIoCtx->pdata[8], 8) == 0)
            {
                //service action reservation key is zero
                //crkey = reservation key
                rrega = 1;//unregister
                //set the reservation key
                buffer[0] = scsiIoCtx->pdata[7];
                buffer[1] = scsiIoCtx->pdata[6];
                buffer[2] = scsiIoCtx->pdata[5];
                buffer[3] = scsiIoCtx->pdata[4];
                buffer[4] = scsiIoCtx->pdata[3];
                buffer[5] = scsiIoCtx->pdata[2];
                buffer[6] = scsiIoCtx->pdata[1];
                buffer[7] = scsiIoCtx->pdata[0];
                //set the service action reservation key to new reservation key field (can safely do this as it's just setting zeros)
                buffer[8] = scsiIoCtx->pdata[15];
                buffer[9] = scsiIoCtx->pdata[14];
                buffer[10] = scsiIoCtx->pdata[13];
                buffer[11] = scsiIoCtx->pdata[12];
                buffer[12] = scsiIoCtx->pdata[11];
                buffer[13] = scsiIoCtx->pdata[10];
                buffer[14] = scsiIoCtx->pdata[9];
                buffer[15] = scsiIoCtx->pdata[8];
            }
            else
            {
                //service action reservation key is non-zero
                //crkey is reserved (leave set to zero, ignoring any provided reservation key)
                rrega = 0;//register
                //NRKEY - service action reservation key
                //set the service action reservation key to new reservation key field
                buffer[8] = scsiIoCtx->pdata[15];
                buffer[9] = scsiIoCtx->pdata[14];
                buffer[10] = scsiIoCtx->pdata[13];
                buffer[11] = scsiIoCtx->pdata[12];
                buffer[12] = scsiIoCtx->pdata[11];
                buffer[13] = scsiIoCtx->pdata[10];
                buffer[14] = scsiIoCtx->pdata[9];
                buffer[15] = scsiIoCtx->pdata[8];
            }
            if (aptpl)//aptpl
            {
                //=1
                changeThroughPowerLoss = 3;//11b
                if (device->drive_info.IdentifyData.nvme.ns.rescap & BIT0)
                {
                    //send a set features for reservation persistence with PTPL set to 1 before sending the reservation register command
                    nvmeFeaturesCmdOpt setPTPL;
                    memset(&setPTPL, 0, sizeof(nvmeFeaturesCmdOpt));
                    setPTPL.fid = 0x83;
                    setPTPL.featSetGetValue = BIT0;
                    if (SUCCESS != nvme_Set_Features(device, &setPTPL))
                    {
                        set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                        return ret;
                    }
                }
                else
                {
                    //ERROR! Invalid field in parameter list since the drive doesn't support this mode!
                    bitPointer = 0;
                    fieldPointer = 17;
                    sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                    //invalid field in CDB
                    ret = NOT_SUPPORTED;
                    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    return ret;
                }
            }
            //send the reservation register command
            if (SUCCESS != nvme_Reservation_Register(device, changeThroughPowerLoss, iekey, rrega, buffer, 16))
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
            break;
        case 1://reserve
        case 4://preempt
        case 5://preempt and abort
        {
            uint8_t buffer[16] = { 0 };
            //translate type field
            uint8_t rtype = 0;
            uint8_t racqa = 0;
            switch (type)
            {
            case 0:
                rtype = 0;
                break;
            case 1:
                rtype = 1;
                break;
            case 3:
                rtype = 2;
                break;
            case 5:
                rtype = 3;
                break;
            case 6:
                rtype = 4;
                break;
            case 7:
                rtype = 5;
                break;
            case 8:
                rtype = 6;
                break;
            default:
                //Invalid field in parameter cdb!
                bitPointer = 3;
                fieldPointer = 2;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                //invalid field in CDB
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return ret;
                break;
            }
            switch (serviceAction)
            {
            case 1://reserve
                racqa = 0;
                break;
            case 4://preempt
                racqa = 1;
                break;
            case 5://preempt and abort
                racqa = 2;
                break;
            }
            //set the reservation key
            buffer[0] = scsiIoCtx->pdata[7];
            buffer[1] = scsiIoCtx->pdata[6];
            buffer[2] = scsiIoCtx->pdata[5];
            buffer[3] = scsiIoCtx->pdata[4];
            buffer[4] = scsiIoCtx->pdata[3];
            buffer[5] = scsiIoCtx->pdata[2];
            buffer[6] = scsiIoCtx->pdata[1];
            buffer[7] = scsiIoCtx->pdata[0];
            if (serviceAction != 1)
            {
                //set the PRKEY to service action reservation key field
                buffer[8] = scsiIoCtx->pdata[15];
                buffer[9] = scsiIoCtx->pdata[14];
                buffer[10] = scsiIoCtx->pdata[13];
                buffer[11] = scsiIoCtx->pdata[12];
                buffer[12] = scsiIoCtx->pdata[11];
                buffer[13] = scsiIoCtx->pdata[10];
                buffer[14] = scsiIoCtx->pdata[9];
                buffer[15] = scsiIoCtx->pdata[8];
            }
            if (SUCCESS != nvme_Reservation_Acquire(device, rtype, false, racqa, buffer, 16))
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
            break;
        case 2://release
        case 3://clear
        {
            //reservation release IEKEY = 0, RRELA = 0 (release)
            //reservation release IEKEY = 0, RRELA = 1 (clear)
            uint8_t buffer[8] = { 0 };
            uint8_t rrela = 0;
            uint8_t rtype = 0;
            //translate type field
            if (serviceAction != 2)
            {
                rrela = 1;//clear
            }
            switch (type)
            {
            case 0:
                rtype = 0;
                break;
            case 1:
                rtype = 1;
                break;
            case 3:
                rtype = 2;
                break;
            case 5:
                rtype = 3;
                break;
            case 6:
                rtype = 4;
                break;
            case 7:
                rtype = 5;
                break;
            case 8:
                rtype = 6;
                break;
            default:
                //Invalid field in parameter cdb!
                bitPointer = 3;
                fieldPointer = 2;
                sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                //invalid field in CDB
                ret = NOT_SUPPORTED;
                sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                return ret;
                break;
            }
            //set the reservation key
            buffer[0] = scsiIoCtx->pdata[7];
            buffer[1] = scsiIoCtx->pdata[6];
            buffer[2] = scsiIoCtx->pdata[5];
            buffer[3] = scsiIoCtx->pdata[4];
            buffer[4] = scsiIoCtx->pdata[3];
            buffer[5] = scsiIoCtx->pdata[2];
            buffer[6] = scsiIoCtx->pdata[1];
            buffer[7] = scsiIoCtx->pdata[0];
            if (SUCCESS != nvme_Reservation_Release(device, rtype, false, rrela, buffer, 8))
            {
                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                return ret;
            }
        }
        break;
        //case 7://register and move <- handled above in if since parameter data is different
        case 8://replace lost reservation (no translation available)
        default:
            //invalid field in cdb (service action is not valid)
            bitPointer = 4;
            fieldPointer = 1;
            sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            //invalid field in CDB
            ret = NOT_SUPPORTED;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            return ret;
        }
    }
    if (scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, persistentReserveData, M_Min(persistentReserveDataLength, parameterListLength));
    }
    safe_Free(persistentReserveData);
    return ret;
}

#if defined (SNTL_EXT)
int sntl_Translate_SCSI_Sanitize_Command(tDevice * device, ScsiIoCtx * scsiIoCtx)
{
    int ret = SUCCESS;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (
        ((fieldPointer = 1) != 0 && (bitPointer = 6) != 0 && scsiIoCtx->cdb[1] & BIT6) || //ZNR bit
        ((fieldPointer = 2) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[2] != 0)
        || ((fieldPointer = 3) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[3] != 0)
        || ((fieldPointer = 4) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[4] != 0)
        || ((fieldPointer = 5) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[5] != 0)
        || ((fieldPointer = 6) != 0 && (bitPointer = 0) == 0 && scsiIoCtx->cdb[6] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (device->drive_info.IdentifyData.nvme.ctrl.sanicap > 0)
    {
        uint8_t serviceAction = 0x1F & scsiIoCtx->cdb[1];
        bool immediate = false;//this is ignored for now since there is no way to handle this without multi-threading
        //bool znr = false;
        bool ause = false;
        uint16_t parameterListLength = M_BytesTo2ByteValue(scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        if (scsiIoCtx->cdb[1] & BIT7)
        {
            immediate = true;
        }
        /*if (scsiIoCtx->cdb[1] & BIT6)
        {
            znr = true;
        }*/
        if (scsiIoCtx->cdb[1] & BIT5)
        {
            ause = true;
        }
        //begin validating the parameters
        switch (serviceAction)
        {
        case 0x01://overwrite
            if (parameterListLength != 0x0008)
            {
                fieldPointer = 7;
                bitPointer = 7;
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else if (!scsiIoCtx->pdata)//if this pointer is invalid set sense data saying the cdb list is invalid...which shouldn't ever happen, but just in case...
            {
                fieldPointer = 7;
                bitPointer = 7;
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                ret = BAD_PARAMETER;
            }
            else
            {
                if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT2)
                {
                    //check the parameter data
                    bool invert = false;
                    uint8_t numberOfPasses = scsiIoCtx->pdata[0] & 0x1F;
                    uint32_t pattern = M_BytesTo4ByteValue(scsiIoCtx->pdata[4], scsiIoCtx->pdata[5], scsiIoCtx->pdata[6], scsiIoCtx->pdata[7]);
                    if (scsiIoCtx->pdata[0] & BIT7)
                    {
                        invert = true;
                    }
                    //validate the parameter data for the number of passes and pattern length according to SAT
                    if (
                        ((fieldPointer = 0) == 0 && (bitPointer = 6) != 0 && scsiIoCtx->pdata[0] & BIT6)
                        || ((fieldPointer = 0) == 0 && (bitPointer = 5) != 0 && scsiIoCtx->pdata[0] & BIT5)
                        || ((fieldPointer = 1) != 0 && (bitPointer = 0) != 0 && scsiIoCtx->pdata[1] != 0)
                        || ((fieldPointer = 0) != 0 && (bitPointer = 4) != 0 && (numberOfPasses == 0 || numberOfPasses > 0x10))
                        || ((fieldPointer = 2) != 0 && (bitPointer = 7) != 0 && 0x0004 != M_BytesTo2ByteValue(scsiIoCtx->pdata[2], scsiIoCtx->pdata[3]))
                        )
                    {
                        if (bitPointer == 0)
                        {
                            uint8_t reservedByteVal = scsiIoCtx->pdata[fieldPointer];
                            uint8_t counter = 0;
                            while (reservedByteVal > 0 && counter < 8)
                            {
                                reservedByteVal >>= 1;
                                ++counter;
                            }
                            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
                        }
                        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, false, true, bitPointer, fieldPointer);
                        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x26, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                        return NOT_SUPPORTED;
                    }
                    if (numberOfPasses == 0x10)
                    {
                        numberOfPasses = 0;//this needs to be set to zero to specify 16 passes as this is how ATA does it.
                    }
                    if (SUCCESS != nvme_Sanitize(device, false, invert, numberOfPasses, ause, SANITIZE_NVM_OVERWRITE, pattern) && !immediate)
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                    else if (!immediate)
                    {
                        //poll until there is no longer a sanitize command in progress
                        uint8_t logPage[512] = { 0 };
                        uint8_t sanitizeStatus = 0x02;//start as in progress
                        nvmeGetLogPageCmdOpts sanitizeLog;
                        memset(&sanitizeLog, 0, sizeof(nvmeGetLogPageCmdOpts));
                        sanitizeLog.addr = logPage;
                        sanitizeLog.dataLen = 512;
                        sanitizeLog.lid = NVME_LOG_SANITIZE_ID;
                        while (sanitizeStatus == 0x2)
                        {
                            delay_Seconds(5);
                            if (SUCCESS == nvme_Get_Log_Page(device, &sanitizeLog))
                            {
                                uint16_t sstat = M_BytesTo2ByteValue(logPage[3], logPage[2]);
                                uint8_t sanitizeStatus = M_GETBITRANGE(sstat, 2, 0);
                                if (sanitizeStatus == 0x3)//sanitize failed
                                {
                                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                                    break;
                                }
                            }
                            else
                            {
                                //set failure for command failing to work
                                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                                break;
                            }
                        }
                        if (sanitizeStatus == 0x03)
                        {
                            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                        }
                    }
                }
                else
                {
                    fieldPointer = 1;
                    bitPointer = 4;
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            break;
        case 0x02://block erase
            if (parameterListLength != 0)
            {
                fieldPointer = 7;
                bitPointer = 7;
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else
            {
                if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT1)
                {
                    if (SUCCESS != nvme_Sanitize(device, false, false, 0, ause, SANITIZE_NVM_BLOCK_ERASE, 0) && !immediate)
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                    else if (!immediate)
                    {
                        //poll until there is no longer a sanitize command in progress
                        uint8_t logPage[512] = { 0 };
                        uint8_t sanitizeStatus = 0x02;//start as in progress
                        nvmeGetLogPageCmdOpts sanitizeLog;
                        memset(&sanitizeLog, 0, sizeof(nvmeGetLogPageCmdOpts));
                        sanitizeLog.addr = logPage;
                        sanitizeLog.dataLen = 512;
                        sanitizeLog.lid = NVME_LOG_SANITIZE_ID;
                        while (sanitizeStatus == 0x2)
                        {
                            delay_Seconds(5);
                            if (SUCCESS == nvme_Get_Log_Page(device, &sanitizeLog))
                            {
                                uint16_t sstat = M_BytesTo2ByteValue(logPage[3], logPage[2]);
                                uint8_t sanitizeStatus = M_GETBITRANGE(sstat, 2, 0);
                                if (sanitizeStatus == 0x3)//sanitize failed
                                {
                                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                                    break;
                                }
                            }
                            else
                            {
                                //set failure for command failing to work
                                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                                break;
                            }
                        }
                        if (sanitizeStatus == 0x03)
                        {
                            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                        }
                    }
                }
                else
                {
                    fieldPointer = 1;
                    bitPointer = 4;
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            break;
        case 0x03://cryptographic erase
            if (parameterListLength != 0)
            {
                fieldPointer = 7;
                bitPointer = 7;
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else
            {
                if (device->drive_info.IdentifyData.nvme.ctrl.sanicap & BIT0)
                {
                    if (SUCCESS != nvme_Sanitize(device, false, false, 0, ause, SANITIZE_NVM_CRYPTO, 0) && !immediate)
                    {
                        ret = FAILURE;
                        set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                    }
                    else if (!immediate)
                    {
                        //poll until there is no longer a sanitize command in progress
                        uint8_t logPage[512] = { 0 };
                        uint8_t sanitizeStatus = 0x02;//start as in progress
                        nvmeGetLogPageCmdOpts sanitizeLog;
                        memset(&sanitizeLog, 0, sizeof(nvmeGetLogPageCmdOpts));
                        sanitizeLog.addr = logPage;
                        sanitizeLog.dataLen = 512;
                        sanitizeLog.lid = NVME_LOG_SANITIZE_ID;
                        while (sanitizeStatus == 0x2)
                        {
                            delay_Seconds(5);
                            if (SUCCESS == nvme_Get_Log_Page(device, &sanitizeLog))
                            {
                                uint16_t sstat = M_BytesTo2ByteValue(logPage[3], logPage[2]);
                                uint8_t sanitizeStatus = M_GETBITRANGE(sstat, 2, 0);
                                if (sanitizeStatus == 0x3)//sanitize failed
                                {
                                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                                    break;
                                }
                            }
                            else
                            {
                                //set failure for command failing to work
                                set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                                break;
                            }
                        }
                        if (sanitizeStatus == 0x03)
                        {
                            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                        }
                    }
                }
                else
                {
                    fieldPointer = 1;
                    bitPointer = 4;
                    set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                    set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                    ret = NOT_SUPPORTED;
                }
            }
            break;
        case 0x1F://exit failure mode
            if (parameterListLength != 0)
            {
                fieldPointer = 7;
                bitPointer = 7;
                set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
                ret = NOT_SUPPORTED;
            }
            else
            {
                if (SUCCESS != nvme_Sanitize(device, false, false, 0, ause, SANITIZE_NVM_EXIT_FAILURE_MODE, 0) && !immediate)
                {
                    ret = FAILURE;
                    set_Sense_Data_By_RTFRs(device, &device->drive_info.lastCommandRTFRs, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                }
                else if (!immediate)
                {
                    //poll until there is no longer a sanitize command in progress
                    uint8_t logPage[512] = { 0 };
                    uint8_t sanitizeStatus = 0x02;//start as in progress
                    nvmeGetLogPageCmdOpts sanitizeLog;
                    memset(&sanitizeLog, 0, sizeof(nvmeGetLogPageCmdOpts));
                    sanitizeLog.addr = logPage;
                    sanitizeLog.dataLen = 512;
                    sanitizeLog.lid = NVME_LOG_SANITIZE_ID;
                    while (sanitizeStatus == 0x2)
                    {
                        delay_Seconds(5);
                        if (SUCCESS == nvme_Get_Log_Page(device, &sanitizeLog))
                        {
                            uint16_t sstat = M_BytesTo2ByteValue(logPage[3], logPage[2]);
                            uint8_t sanitizeStatus = M_GETBITRANGE(sstat, 2, 0);
                            if (sanitizeStatus == 0x3)//sanitize failed
                            {
                                set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                                break;
                            }
                        }
                        else
                        {
                            //set failure for command failing to work
                            set_Sense_Data_By_NVMe_Status(device, device->drive_info.lastNVMeResult.lastNVMeStatus, scsiIoCtx->psense, scsiIoCtx->senseDataSize);
                            break;
                        }
                    }
                    if (sanitizeStatus == 0x03)
                    {
                        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_MEDIUM_ERROR, 0x31, 0x03, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
                    }
                }
            }
            break;
        default:
            fieldPointer = 1;
            bitPointer = 4;
            set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
            set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
            ret = NOT_SUPPORTED;
        }
    }
    else //sanitize feature not supported.
    {
        fieldPointer = 0;
        bitPointer = 7;
        set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
    }
    return ret;
}
#endif

void sntl_Set_Command_Timeouts_Descriptor(uint32_t nominalCommandProcessingTimeout, uint32_t recommendedCommandProcessingTimeout, uint8_t *pdata, uint32_t *offset)
{
    pdata[*offset + 0] = 0x00;
    pdata[*offset + 1] = 0x0A;
    pdata[*offset + 2] = RESERVED;
    //command specfic
    pdata[*offset + 3] = 0x00;
    //nominal command processing timeout
    pdata[*offset + 4] = M_Byte3(nominalCommandProcessingTimeout);
    pdata[*offset + 5] = M_Byte2(nominalCommandProcessingTimeout);
    pdata[*offset + 6] = M_Byte1(nominalCommandProcessingTimeout);
    pdata[*offset + 7] = M_Byte0(nominalCommandProcessingTimeout);
    //recommended command timeout
    pdata[*offset + 8] = M_Byte3(recommendedCommandProcessingTimeout);
    pdata[*offset + 9] = M_Byte2(recommendedCommandProcessingTimeout);
    pdata[*offset + 10] = M_Byte1(recommendedCommandProcessingTimeout);
    pdata[*offset + 11] = M_Byte0(recommendedCommandProcessingTimeout);
    //increment the offset
    *offset += 12;
}

//TODO: add in support info for immediate bits (requires command support via threading)
int sntl_Check_Operation_Code(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t operationCode, bool rctd, uint8_t **pdata, uint32_t *dataLength)
{
    int ret = SUCCESS;
    *dataLength = 4;//add onto this for each of the different commands below, then allocate memory accordingly
    uint32_t offset = 4;//use to keep track and setup the buffer
    uint16_t cdbLength = 1;//set to 1 for the default case
    uint8_t controlByte = 0;
    bool commandSupported = true;
    if (rctd)
    {
        //add 12 bytes for room for the command timeouts descriptor to be setup
        dataLength += 12;
    }
    switch (operationCode)
    {
    case INQUIRY_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT0;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case READ_CAPACITY_10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;
        pdata[0][offset + 2] = OBSOLETE;
        pdata[0][offset + 3] = OBSOLETE;
        pdata[0][offset + 4] = OBSOLETE;
        pdata[0][offset + 5] = OBSOLETE;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = RESERVED;
        pdata[0][offset + 8] = 0;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    
    case LOG_SENSE_CMD:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;//leave at zero since sp bit is ignored in translation
        pdata[0][offset + 2] = 0x7F;//PC only supports 01h, hence 7F instead of FF
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case MODE_SENSE_6_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;
        pdata[0][offset + 2] = 0x3F;//PC only valid for 00 (current mode page)
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case MODE_SENSE10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3 | BIT4;
        pdata[0][offset + 2] = 0x3F;//PC only valid for 00 (current mode page)
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = RESERVED;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case MODE_SELECT_6_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT4 | BIT0;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case MODE_SELECT10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT4 | BIT0;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = RESERVED;
        pdata[0][offset + 6] = RESERVED;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case READ6:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0x1F;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = controlByte;
        break;
    case READ10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;//RARC bit support to be added later, dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0;//group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case READ12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;//RARC bit support to be added later, dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0;//group number should be zero
        pdata[0][offset + 11] = controlByte;//control byte
        break;
    case READ16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;//RARC bit support to be added later, dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;//group number should be zero
        pdata[0][offset + 15] = controlByte;//control byte
        break;
    case REPORT_LUNS_CMD:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = BIT4 | BIT0 | BIT1;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = RESERVED;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = RESERVED;
        pdata[0][offset + 11] = controlByte;//control byte
        break;
    case REQUEST_SENSE_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT1;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case SECURITY_PROTOCOL_IN://fallthrough
    case SECURITY_PROTOCOL_OUT:
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT0)
        {
            cdbLength = 12;
            *dataLength += cdbLength;
            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = 0xFF;//security protocol
            pdata[0][offset + 2] = 0xFF;//security protocol specific
            pdata[0][offset + 3] = 0xFF;//security protocol specific
            pdata[0][offset + 4] = BIT7;//inc512 bit
            pdata[0][offset + 5] = RESERVED;
            pdata[0][offset + 6] = 0xFF;
            pdata[0][offset + 7] = 0xFF;
            pdata[0][offset + 8] = 0xFF;
            pdata[0][offset + 9] = 0xFF;
            pdata[0][offset + 10] = RESERVED;
            pdata[0][offset + 11] = controlByte;//control byte
        }
        else
        {
            commandSupported = false;
        }
        break;
    case SEND_DIAGNOSTIC_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT7 | BIT6 | BIT5 | BIT2;//self test bit and self test code field
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = 0;
        pdata[0][offset + 4] = 0;
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case START_STOP_UNIT_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;//TODO: add immediate bit support in here....for now returning that it's not supported
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = 0x0F;//power condition modifier supported
        pdata[0][offset + 4] = 0xF7;//power condition and no_flush and loej and start supported
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case SYNCHRONIZE_CACHE_10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;//TODO: add Immediate bit support
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0;//group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case SYNCHRONIZE_CACHE_16_CMD:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0;//TODO: add Immediate bit support
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;//group number should be zero
        pdata[0][offset + 15] = controlByte;//control byte
        break;
    case TEST_UNIT_READY_CMD:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = RESERVED;
        pdata[0][offset + 3] = RESERVED;
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = controlByte;//control byte
        break;
    case UNMAP_CMD:
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
        {
            cdbLength = 10;
            *dataLength += cdbLength;
            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = 0;//anchor bit ignored
            pdata[0][offset + 2] = RESERVED;
            pdata[0][offset + 3] = RESERVED;
            pdata[0][offset + 4] = RESERVED;
            pdata[0][offset + 5] = RESERVED;
            pdata[0][offset + 6] = 0;//group number should be zero
            pdata[0][offset + 7] = 0xFF;
            pdata[0][offset + 8] = 0xFF;
            pdata[0][offset + 9] = controlByte;//control byte
        }
        else //not supported
        {
            commandSupported = false;
        }
        break;
    case VERIFY10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT2 | BIT1;//bytecheck 11 supported
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0;//group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case VERIFY12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT2 | BIT1;//bytecheck 11 supported
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0;//group number should be zero
        pdata[0][offset + 11] = controlByte;//control byte
        break;
    case VERIFY16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT2 | BIT1;//bytecheck 11 supported
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;//group number should be zero
        pdata[0][offset + 15] = controlByte;//control byte
        break;
    case WRITE6:
        cdbLength = 6;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = 0x1F;
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = controlByte;
        break;
    case WRITE10:
        cdbLength = 10;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;//dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0;//group number should be zero
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = controlByte;//control byte
        break;
    case WRITE12:
        cdbLength = 12;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;//dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0;//group number should be zero
        pdata[0][offset + 11] = controlByte;//control byte
        break;
    case WRITE16:
        cdbLength = 16;
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset + 0] = operationCode;
        pdata[0][offset + 1] = BIT3;//dpo ignored
        pdata[0][offset + 2] = 0xFF;
        pdata[0][offset + 3] = 0xFF;
        pdata[0][offset + 4] = 0xFF;
        pdata[0][offset + 5] = 0xFF;
        pdata[0][offset + 6] = 0xFF;
        pdata[0][offset + 7] = 0xFF;
        pdata[0][offset + 8] = 0xFF;
        pdata[0][offset + 9] = 0xFF;
        pdata[0][offset + 10] = 0xFF;
        pdata[0][offset + 11] = 0xFF;
        pdata[0][offset + 12] = 0xFF;
        pdata[0][offset + 13] = 0xFF;
        pdata[0][offset + 14] = 0;//group number should be zero
        pdata[0][offset + 15] = controlByte;//control byte
        break;
    //case WRITE_AND_VERIFY_10:
    //    cdbLength = 10;
    //    *dataLength += cdbLength;
    //    *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //    if (!*pdata)
    //    {
    //        return MEMORY_FAILURE;
    //    }
    //    pdata[0][offset + 0] = operationCode;
    //    pdata[0][offset + 1] = BIT2 | BIT1;//bytecheck 11 supported
    //    pdata[0][offset + 2] = 0xFF;
    //    pdata[0][offset + 3] = 0xFF;
    //    pdata[0][offset + 4] = 0xFF;
    //    pdata[0][offset + 5] = 0xFF;
    //    pdata[0][offset + 6] = 0;//group number should be zero
    //    pdata[0][offset + 7] = 0xFF;
    //    pdata[0][offset + 8] = 0xFF;
    //    pdata[0][offset + 9] = controlByte;//control byte
    //    break;
    //case WRITE_AND_VERIFY_12:
    //    cdbLength = 12;
    //    *dataLength += cdbLength;
    //    *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //    if (!*pdata)
    //    {
    //        return MEMORY_FAILURE;
    //    }
    //    pdata[0][offset + 0] = operationCode;
    //    pdata[0][offset + 1] = BIT2 | BIT1;//bytecheck 11 supported
    //    pdata[0][offset + 2] = 0xFF;
    //    pdata[0][offset + 3] = 0xFF;
    //    pdata[0][offset + 4] = 0xFF;
    //    pdata[0][offset + 5] = 0xFF;
    //    pdata[0][offset + 6] = 0xFF;
    //    pdata[0][offset + 7] = 0xFF;
    //    pdata[0][offset + 8] = 0xFF;
    //    pdata[0][offset + 9] = 0xFF;
    //    pdata[0][offset + 10] = 0;//group number should be zero
    //    pdata[0][offset + 11] = controlByte;//control byte
    //    break;
    //case WRITE_AND_VERIFY_16:
    //    cdbLength = 16;
    //    *dataLength += cdbLength;
    //    *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //    if (!*pdata)
    //    {
    //        return MEMORY_FAILURE;
    //    }
    //    pdata[0][offset + 0] = operationCode;
    //    pdata[0][offset + 1] = BIT2 | BIT1;//bytecheck 11 supported
    //    pdata[0][offset + 2] = 0xFF;
    //    pdata[0][offset + 3] = 0xFF;
    //    pdata[0][offset + 4] = 0xFF;
    //    pdata[0][offset + 5] = 0xFF;
    //    pdata[0][offset + 6] = 0xFF;
    //    pdata[0][offset + 7] = 0xFF;
    //    pdata[0][offset + 8] = 0xFF;
    //    pdata[0][offset + 9] = 0xFF;
    //    pdata[0][offset + 10] = 0xFF;
    //    pdata[0][offset + 11] = 0xFF;
    //    pdata[0][offset + 12] = 0xFF;
    //    pdata[0][offset + 13] = 0xFF;
    //    pdata[0][offset + 14] = 0;//group number should be zero
    //    pdata[0][offset + 15] = controlByte;//control byte
    //    break;
    case WRITE_LONG_10_CMD:
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
        {
            cdbLength = 10;
            *dataLength += cdbLength;
            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = BIT7 | BIT6;
            pdata[0][offset + 2] = 0xFF;
            pdata[0][offset + 3] = 0xFF;
            pdata[0][offset + 4] = 0xFF;
            pdata[0][offset + 5] = 0xFF;
            pdata[0][offset + 6] = 0;//group number should be zero
            pdata[0][offset + 7] = 0;
            pdata[0][offset + 8] = 0;
            pdata[0][offset + 9] = controlByte;//control byte
        }
        else
        {
            commandSupported = false;
        }
        break;
    //case SCSI_FORMAT_UNIT_CMD:
    //    cdbLength = 6;
    //    *dataLength += cdbLength;
    //    *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //    if (!*pdata)
    //    {
    //        return MEMORY_FAILURE;
    //    }
    //    pdata[0][offset + 0] = operationCode;
    //    pdata[0][offset + 1] = 0x37;//protection information is not supported, complete list is not supported, but we are supposed to be able to take 2 different defect list formats even though we don't use them - TJE
    //    pdata[0][offset + 2] = RESERVED;
    //    pdata[0][offset + 3] = RESERVED;
    //    pdata[0][offset + 4] = RESERVED;
    //    pdata[0][offset + 5] = controlByte;//control byte
    //    break;
    //case WRITE_SAME_10_CMD://TODO: add in once the command is supported
    //case WRITE_SAME_16_CMD://TODO: add in once the command is supported
    default:
        commandSupported = false;
        break;
    }
    if (!commandSupported)
    {
        //allocate memory
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset] = operationCode;
    }
    //set up the common data
    pdata[0][0] = RESERVED;
    if (commandSupported)
    {
        pdata[0][1] |= BIT0 | BIT1;//command supported by standard
    }
    else
    {
        pdata[0][1] |= BIT0;//command not supported
    }
    pdata[0][2] = M_Byte1(cdbLength);
    pdata[0][3] = M_Byte0(cdbLength);
    //increment the offset by the cdb length
    offset += cdbLength;
    if (rctd && ret == SUCCESS)
    {
        //set CTDP to 1
        pdata[0][1] |= BIT7;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, *pdata, &offset);
    }
    return ret;
}

//TODO: add in support info for immediate bits (requires command support via threading)
int sntl_Check_Operation_Code_and_Service_Action(tDevice *device, ScsiIoCtx *scsiIoCtx, uint8_t operationCode, uint16_t serviceAction, bool rctd, uint8_t **pdata, uint32_t *dataLength)
{
    int ret = SUCCESS;
    *dataLength = 4;//add onto this for each of the different commands below, then allocate memory accordingly
    uint32_t offset = 4;//use to keep track and setup the buffer
    uint16_t cdbLength = 1;//set to 1 for the default case
    uint8_t controlByte = 0;
    bool commandSupported = true;
    if (rctd)
    {
        //add 12 bytes for room for the command timeouts descriptor to be setup
        dataLength += 12;
    }
    switch (operationCode)
    {
    case 0x9E:
        switch (serviceAction)
        {
        case 0x10://read capacity 16
            cdbLength = 16;
            *dataLength += cdbLength;
            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = serviceAction & 0x001F;
            pdata[0][offset + 2] = OBSOLETE;
            pdata[0][offset + 3] = OBSOLETE;
            pdata[0][offset + 4] = OBSOLETE;
            pdata[0][offset + 5] = OBSOLETE;
            pdata[0][offset + 6] = OBSOLETE;
            pdata[0][offset + 7] = OBSOLETE;
            pdata[0][offset + 8] = OBSOLETE;
            pdata[0][offset + 9] = OBSOLETE;
            pdata[0][offset + 10] = 0xFF;
            pdata[0][offset + 11] = 0xFF;
            pdata[0][offset + 12] = 0xFF;
            pdata[0][offset + 13] = 0xFF;
            pdata[0][offset + 14] = 0;//bit 0 is obsolete
            pdata[0][offset + 15] = controlByte;//control byte
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
    case 0xA3:
        switch (serviceAction)
        {
        case 0x0C://report supported op codes
            cdbLength = 12;
            *dataLength += cdbLength;
            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
            if (!*pdata)
            {
                return MEMORY_FAILURE;
            }
            pdata[0][offset + 0] = operationCode;
            pdata[0][offset + 1] = serviceAction & 0x001F;
            pdata[0][offset + 2] = 0x87;//bits 0-2 & bit7
            pdata[0][offset + 3] = 0xFF;
            pdata[0][offset + 4] = 0xFF;
            pdata[0][offset + 5] = 0xFF;
            pdata[0][offset + 6] = 0xFF;
            pdata[0][offset + 7] = 0xFF;
            pdata[0][offset + 8] = 0xFF;
            pdata[0][offset + 9] = 0xFF;
            pdata[0][offset + 10] = RESERVED;
            pdata[0][offset + 11] = controlByte;//control byte
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
        //TODO: add in sanitize support when we add this extention
#if defined (SNTL_EXT)
    //case SANITIZE_CMD:
    //    if (device->drive_info.IdentifyData.ata.Word059 & BIT12)
    //    {
    //        switch (serviceAction)
    //        {
    //        case 1://overwrite
    //            cdbLength = 10;
    //            *dataLength += cdbLength;
    //            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //            if (!*pdata)
    //            {
    //                return MEMORY_FAILURE;
    //            }
    //            pdata[0][offset + 0] = operationCode;
    //            pdata[0][offset + 1] = (serviceAction & 0x001F) | BIT5;//TODO: add immediate and znr bit support
    //            pdata[0][offset + 2] = RESERVED;
    //            pdata[0][offset + 3] = RESERVED;
    //            pdata[0][offset + 4] = RESERVED;
    //            pdata[0][offset + 5] = RESERVED;
    //            pdata[0][offset + 6] = RESERVED;
    //            pdata[0][offset + 7] = 0xFF;
    //            pdata[0][offset + 8] = 0xFF;
    //            pdata[0][offset + 9] = controlByte;//control byte
    //            break;
    //        case 2://block erase
    //            //fallthrough
    //        case 3://cryptographic erase
    //            //fallthrough
    //        case 0x1F://exit failure mode
    //            cdbLength = 10;
    //            *dataLength += cdbLength;
    //            *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //            if (!*pdata)
    //            {
    //                return MEMORY_FAILURE;
    //            }
    //            pdata[0][offset + 0] = operationCode;
    //            pdata[0][offset + 1] = (serviceAction & 0x001F) | BIT5;//TODO: add immediate and znr bit support
    //            pdata[0][offset + 2] = RESERVED;
    //            pdata[0][offset + 3] = RESERVED;
    //            pdata[0][offset + 4] = RESERVED;
    //            pdata[0][offset + 5] = RESERVED;
    //            pdata[0][offset + 6] = RESERVED;
    //            pdata[0][offset + 7] = 0;
    //            pdata[0][offset + 8] = 0;
    //            pdata[0][offset + 9] = controlByte;//control byte
    //            break;
    //        default:
    //            commandSupported = false;
    //            break;
    //        }
    //    }
    //    else
    //    {
    //        commandSupported = false;
    //    }
    //    break;
#endif
    case WRITE_BUFFER_CMD:
    {
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT2)
        {
            switch (serviceAction)
            {
            case 0x05://download
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0;
                pdata[0][offset + 4] = 0;
                pdata[0][offset + 5] = 0;
                pdata[0][offset + 6] = 0xFF;
                pdata[0][offset + 7] = 0xFF;
                pdata[0][offset + 8] = 0xFF;
                pdata[0][offset + 9] = controlByte;//control byte
                break;
                //Mode 7 not supported since we don't always know when we are at the final download segment.
                //case 0x07://download offsets
                //        cdbLength = 10;
                //        *dataLength += cdbLength;
                //        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
                //        if (!*pdata)
                //        {
                //            return MEMORY_FAILURE;
                //        }
                //        pdata[0][offset + 0] = operationCode;
                //        pdata[0][offset + 1] = serviceAction & 0x001F;
                //        pdata[0][offset + 2] = 0;
                //        pdata[0][offset + 3] = 0;
                //        pdata[0][offset + 4] = 0;
                //        pdata[0][offset + 5] = 0;
                //        pdata[0][offset + 6] = 0x3F;
                //        pdata[0][offset + 7] = 0xFE;
                //        pdata[0][offset + 8] = 0x00;
                //        pdata[0][offset + 9] = controlByte;//control byte
                //    break;
#if defined (SNTL_EXT)
            case 0x0D://download offsets defer
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = (serviceAction & 0x001F) | BIT7;//service action plus mode specific set for power cycle activation
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0x3F;
                pdata[0][offset + 4] = 0xFE;
                pdata[0][offset + 5] = 0x00;
                pdata[0][offset + 6] = 0x3F;
                pdata[0][offset + 7] = 0xFE;
                pdata[0][offset + 8] = 0x00;
                pdata[0][offset + 9] = controlByte;//control byte
                break;
#endif
            case 0x0E://download offsets defer
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0x3F;
                pdata[0][offset + 4] = 0xFE;
                pdata[0][offset + 5] = 0x00;
                pdata[0][offset + 6] = 0x3F;
                pdata[0][offset + 7] = 0xFE;
                pdata[0][offset + 8] = 0x00;
                pdata[0][offset + 9] = controlByte;//control byte
                break;
            case 0x0F://activate deferred code
                cdbLength = 10;
                *dataLength += cdbLength;
                *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = serviceAction & 0x001F;
                pdata[0][offset + 2] = 0;
                pdata[0][offset + 3] = 0x3F;
                pdata[0][offset + 4] = 0xFE;
                pdata[0][offset + 5] = 0x00;
                pdata[0][offset + 6] = 0x3F;
                pdata[0][offset + 7] = 0xFE;
                pdata[0][offset + 8] = 0x00;
                pdata[0][offset + 9] = controlByte;//control byte
                break;
            default:
                commandSupported = false;
                break;
            }
        }
        else
        {
            commandSupported = false;
        }
    }
    break;//Write buffer cmd
#if defined SNTL_EXT
    //case READ_BUFFER_CMD:
    //    switch (serviceAction)
    //    {
    //    case 0x02://read buffer command
    //        //fall through
    //    case 0x03://descriptor
    //        cdbLength = 10;
    //        *dataLength += cdbLength;
    //        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
    //        if (!*pdata)
    //        {
    //            return MEMORY_FAILURE;
    //        }
    //        pdata[0][offset + 0] = operationCode;
    //        pdata[0][offset + 1] = serviceAction & 0x001F;
    //        pdata[0][offset + 2] = 0;
    //        pdata[0][offset + 3] = 0;
    //        pdata[0][offset + 4] = 0;
    //        pdata[0][offset + 5] = 0;
    //        pdata[0][offset + 6] = 0;
    //        pdata[0][offset + 7] = 0x03;
    //        pdata[0][offset + 8] = 0xFF;
    //        pdata[0][offset + 9] = controlByte;//control byte
    //        break;
        //case 0x1C://TODO: only show this when ISL log is supported...for now this should be ok
        //    if (device->drive_info.softSATFlags.currentInternalStatusLogSupported || device->drive_info.softSATFlags.savedInternalStatusLogSupported)
        //    {
        //        cdbLength = 10;
        //        *dataLength += cdbLength;
        //        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        //        if (!*pdata)
        //        {
        //            return MEMORY_FAILURE;
        //        }
        //        pdata[0][offset + 0] = operationCode;
        //        pdata[0][offset + 1] = serviceAction & 0x001F;
        //        pdata[0][offset + 2] = 0;
        //        pdata[0][offset + 3] = 0;
        //        pdata[0][offset + 4] = 0;
        //        pdata[0][offset + 5] = 0;
        //        pdata[0][offset + 6] = 0;
        //        pdata[0][offset + 7] = 0x03;
        //        pdata[0][offset + 8] = 0xFF;
        //        pdata[0][offset + 9] = controlByte;//control byte
        //    }
        //    else
        //    {
        //        commandSupported = false;
        //    }
        //    break;
        /*default:
            commandSupported = false;
            break;
        }
        break;*/
#endif
    case 0x9F:
        switch (serviceAction)
        {
        case 0x11://write long 16
            if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
            {
                cdbLength = 16;
                *dataLength += cdbLength;
                *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
                if (!*pdata)
                {
                    return MEMORY_FAILURE;
                }
                pdata[0][offset + 0] = operationCode;
                pdata[0][offset + 1] = (serviceAction & 0x001F) | BIT7 | BIT6;
                pdata[0][offset + 2] = 0xFF;
                pdata[0][offset + 3] = 0xFF;
                pdata[0][offset + 4] = 0xFF;
                pdata[0][offset + 5] = 0xFF;
                pdata[0][offset + 6] = 0xFF;
                pdata[0][offset + 7] = 0xFF;
                pdata[0][offset + 8] = 0xFF;
                pdata[0][offset + 9] = 0xFF;
                pdata[0][offset + 10] = RESERVED;
                pdata[0][offset + 11] = RESERVED;
                pdata[0][offset + 12] = 0;
                pdata[0][offset + 13] = 0;
                pdata[0][offset + 14] = RESERVED;
                pdata[0][offset + 15] = controlByte;//control byte
            }
            else
            {
                commandSupported = false;
            }
            break;
        default:
            commandSupported = false;
            break;
        }
        break;
    default:
        commandSupported = false;
        break;
    }
    if (!commandSupported)
    {
        //allocate memory
        *dataLength += cdbLength;
        *pdata = (uint8_t*)calloc(*dataLength, sizeof(uint8_t));
        if (!*pdata)
        {
            return MEMORY_FAILURE;
        }
        pdata[0][offset] = operationCode;
    }
    //set up the common data
    pdata[0][0] = RESERVED;
    if (commandSupported)
    {
        pdata[0][1] |= BIT0 | BIT1;//command supported by standard
    }
    else
    {
        pdata[0][1] |= BIT0;//command not supported
    }
    pdata[0][2] = M_Byte1(cdbLength);
    pdata[0][3] = M_Byte0(cdbLength);
    //increment the offset by the cdb length
    offset += cdbLength;
    if (rctd && ret == SUCCESS)
    {
        //set CTDP to 1
        pdata[0][1] |= BIT7;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, *pdata, &offset);
    }
    return ret;
}

int sntl_Create_All_Supported_Op_Codes_Buffer(tDevice *device, bool rctd, uint8_t **pdata, uint32_t *dataLength)
{
    int ret = SUCCESS;
    uint32_t reportAllMaxLength = 4 * LEGACY_DRIVE_SEC_SIZE;
    uint32_t offset = 4;
    *pdata = (uint8_t*)calloc(reportAllMaxLength, sizeof(uint8_t));
    if (!*pdata)
    {
        return MEMORY_FAILURE;
    }
    //go through supported op codes &| service actions in order
    //TEST_UNIT_READY_CMD = 0x00
    pdata[0][offset + 0] = TEST_UNIT_READY_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //REQUEST_SENSE_CMD = 0x03
    pdata[0][offset + 0] = REQUEST_SENSE_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //SCSI_FORMAT_UNIT_CMD = 0x04
    //pdata[0][offset + 0] = SCSI_FORMAT_UNIT_CMD;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    ////skipping offset 5 for this
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //READ6 = 0x08
    pdata[0][offset + 0] = READ6;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE6 = 0x0A
    pdata[0][offset + 0] = WRITE6;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //INQUIRY_CMD = 0x12
    pdata[0][offset + 0] = INQUIRY_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //MODE_SELECT_6_CMD = 0x15
    pdata[0][offset + 0] = MODE_SELECT_6_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //MODE_SENSE_6_CMD = 0x1A
    pdata[0][offset + 0] = MODE_SENSE_6_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //START_STOP_UNIT_CMD = 0x1B
    pdata[0][offset + 0] = START_STOP_UNIT_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //SEND_DIAGNOSTIC_CMD = 0x1D
    pdata[0][offset + 0] = SEND_DIAGNOSTIC_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_6);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_6);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //READ_CAPACITY_10 = 0x25
    pdata[0][offset + 0] = READ_CAPACITY_10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //READ10 = 0x28
    pdata[0][offset + 0] = READ10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE10 = 0x2A
    pdata[0][offset + 0] = WRITE10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE_AND_VERIFY_10 = 0x2E
    //pdata[0][offset + 0] = WRITE_AND_VERIFY_10;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    ////skipping offset 5 for this
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //VERIFY10 = 0x2F
    pdata[0][offset + 0] = VERIFY10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //SYNCHRONIZE_CACHE_10 = 0x35
    pdata[0][offset + 0] = SYNCHRONIZE_CACHE_10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE_BUFFER_CMD = 0x3B + modes
    if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT2)
    {
        pdata[0][offset + 0] = WRITE_BUFFER_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x05);//service action msb
        pdata[0][offset + 3] = M_Byte0(0x05);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        //Not supported since we don't know when we get the final segment
        //if (downloadMode3Supported)
        //{
        //    pdata[0][offset + 0] = WRITE_BUFFER_CMD;
        //    pdata[0][offset + 1] = RESERVED;
        //    pdata[0][offset + 2] = M_Byte1(0x07);//service action msb
        //    pdata[0][offset + 3] = M_Byte0(0x07);//service action lsb if non zero set byte 5, bit0
        //    pdata[0][offset + 4] = RESERVED;
        //    pdata[0][offset + 5] = BIT0;
        //    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        //    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        //    offset += 8;
        //    if (rctd)
        //    {
        //        //set CTPD to 1
        //        pdata[0][offset - 8 + 5] |= BIT1;
        //        //set up timeouts descriptor
        //        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        //    }
        //}
#if defined (SNTL_EXT)
        pdata[0][offset + 0] = WRITE_BUFFER_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x0D);//service action msb
        pdata[0][offset + 3] = M_Byte0(0x0D);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
#endif
        pdata[0][offset + 0] = WRITE_BUFFER_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x0E);//service action msb
        pdata[0][offset + 3] = M_Byte0(0x0E);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
        pdata[0][offset + 0] = WRITE_BUFFER_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x0F);//service action msb
        pdata[0][offset + 3] = M_Byte0(0x0F);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
#if defined (SNTL_EXT)
    //READ_BUFFER_CMD = 0x3C + modes
    //not able to support normal read buffer since there is no translation...but maybe the descriptor since it could tell us offset requirements for downloads???
    //pdata[0][offset + 0] = READ_BUFFER_CMD;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0x02);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0x02);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    //pdata[0][offset + 5] = BIT0;
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //pdata[0][offset + 0] = READ_BUFFER_CMD;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0x03);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0x03);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    //pdata[0][offset + 5] = BIT0;
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //TODO: add in support for error history sntl extention
//#if SAT_4_ERROR_HISTORY_FEATURE
//    if (device->drive_info.softSATFlags.currentInternalStatusLogSupported || device->drive_info.softSATFlags.savedInternalStatusLogSupported)
//    {
//        pdata[0][offset + 0] = READ_BUFFER_CMD;
//        pdata[0][offset + 1] = RESERVED;
//        pdata[0][offset + 2] = M_Byte1(0x1C);//service action msb
//        pdata[0][offset + 3] = M_Byte0(0x1C);//service action lsb if non zero set byte 5, bit0
//        pdata[0][offset + 4] = RESERVED;
//        pdata[0][offset + 5] = BIT0;
//        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
//        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
//        offset += 8;
//        if (rctd)
//        {
//            //set CTPD to 1
//            pdata[0][offset - 8 + 5] |= BIT1;
//            //set up timeouts descriptor
//            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
//        }
//    }
//#endif
#endif
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
    {
        //WRITE_LONG_10_CMD = 0x3F
        pdata[0][offset + 0] = WRITE_LONG_10_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0);//service action msb
        pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        //skipping offset 5 for this
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    //TODO: write same support
    ////WRITE_SAME_10_CMD = 0x41
    //pdata[0][offset + 0] = WRITE_SAME_10_CMD;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    ////skipping offset 5 for this
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //UNMAP_CMD = 0x42
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
    {
        pdata[0][offset + 0] = UNMAP_CMD;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0);//service action msb
        pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        //skipping offset 5 for this
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
#if defined (SNTL_EXT) //TODO: SNTL sanitize extension
    ////SANITIZE_CMD = 0x48//4 possible service actions
    //if (device->drive_info.IdentifyData.ata.Word059 & BIT12)
    //{
    //    //check overwrite
    //    if (device->drive_info.IdentifyData.ata.Word059 & BIT14)
    //    {
    //        pdata[0][offset + 0] = SANITIZE_CMD;
    //        pdata[0][offset + 1] = RESERVED;
    //        pdata[0][offset + 2] = M_Byte1(0x01);//service action msb
    //        pdata[0][offset + 3] = M_Byte0(0x01);//service action lsb if non zero set byte 5, bit0
    //        pdata[0][offset + 4] = RESERVED;
    //        pdata[0][offset + 5] = BIT0;
    //        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    //        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    //        offset += 8;
    //        if (rctd)
    //        {
    //            //set CTPD to 1
    //            pdata[0][offset - 8 + 5] |= BIT1;
    //            //set up timeouts descriptor
    //            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //        }
    //    }
    //    //check block erase
    //    if (device->drive_info.IdentifyData.ata.Word059 & BIT15)
    //    {
    //        pdata[0][offset + 0] = SANITIZE_CMD;
    //        pdata[0][offset + 1] = RESERVED;
    //        pdata[0][offset + 2] = M_Byte1(0x02);//service action msb
    //        pdata[0][offset + 3] = M_Byte0(0x02);//service action lsb if non zero set byte 5, bit0
    //        pdata[0][offset + 4] = RESERVED;
    //        pdata[0][offset + 5] = BIT0;
    //        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    //        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    //        offset += 8;
    //        if (rctd)
    //        {
    //            //set CTPD to 1
    //            pdata[0][offset - 8 + 5] |= BIT1;
    //            //set up timeouts descriptor
    //            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //        }
    //    }
    //    //check crypto erase
    //    if (device->drive_info.IdentifyData.ata.Word059 & BIT13)
    //    {
    //        pdata[0][offset + 0] = SANITIZE_CMD;
    //        pdata[0][offset + 1] = RESERVED;
    //        pdata[0][offset + 2] = M_Byte1(0x03);//service action msb
    //        pdata[0][offset + 3] = M_Byte0(0x03);//service action lsb if non zero set byte 5, bit0
    //        pdata[0][offset + 4] = RESERVED;
    //        pdata[0][offset + 5] = BIT0;
    //        pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    //        pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    //        offset += 8;
    //        if (rctd)
    //        {
    //            //set CTPD to 1
    //            pdata[0][offset - 8 + 5] |= BIT1;
    //            //set up timeouts descriptor
    //            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //        }
    //    }
    //    //set exit failure mode since it's always available
    //    pdata[0][offset + 0] = SANITIZE_CMD;
    //    pdata[0][offset + 1] = RESERVED;
    //    pdata[0][offset + 2] = M_Byte1(0x1F);//service action msb
    //    pdata[0][offset + 3] = M_Byte0(0x1F);//service action lsb if non zero set byte 5, bit0
    //    pdata[0][offset + 4] = RESERVED;
    //    pdata[0][offset + 5] = BIT0;
    //    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    //    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    //    offset += 8;
    //    if (rctd)
    //    {
    //        //set CTPD to 1
    //        pdata[0][offset - 8 + 5] |= BIT1;
    //        //set up timeouts descriptor
    //        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //    }
    //}
#endif
    //LOG_SENSE_CMD = 0x4D
    pdata[0][offset + 0] = LOG_SENSE_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //MODE_SELECT10 = 0x55
    pdata[0][offset + 0] = MODE_SELECT10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //MODE_SENSE10 = 0x5A
    pdata[0][offset + 0] = MODE_SENSE10;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_10);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_10);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //READ16 = 0x88
    pdata[0][offset + 0] = READ16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE16 = 0x8A
    pdata[0][offset + 0] = WRITE16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    ////WRITE_AND_VERIFY_16 = 0x8E
    //pdata[0][offset + 0] = WRITE_AND_VERIFY_16;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    ////skipping offset 5 for this
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //VERIFY16 = 0x8F
    pdata[0][offset + 0] = VERIFY16;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //SYNCHRONIZE_CACHE_16_CMD = 0x91
    pdata[0][offset + 0] = SYNCHRONIZE_CACHE_16_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    //skipping offset 5 for this
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE_SAME_16_CMD = 0x93
    //pdata[0][offset + 0] = WRITE_SAME_16_CMD;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    ////skipping offset 5 for this
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //0x9E / 0x10//read capacity 16                 = 0x9E
    pdata[0][offset + 0] = 0x9E;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x10);//service action msb
    pdata[0][offset + 3] = M_Byte0(0x10);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
    {
        //0x9F / 0x11//write long 16                    = 0x9F
        pdata[0][offset + 0] = 0x9F;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0x11);//service action msb
        pdata[0][offset + 3] = M_Byte0(0x11);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = BIT0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_16);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_16);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    //REPORT_LUNS_CMD = 0xA0
    pdata[0][offset + 0] = REPORT_LUNS_CMD;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT0)
    {
        //SECURITY_PROTOCOL_IN = 0xA2
        pdata[0][offset + 0] = SECURITY_PROTOCOL_IN;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0);//service action msb
        pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = 0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    //0xA3 / 0x0C//report supported op codes        = 0xA3
    pdata[0][offset + 0] = 0xA3;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0x0C);//service action msb
    pdata[0][offset + 3] = M_Byte0(0x0C);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = BIT0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //READ12 = 0xA8
    pdata[0][offset + 0] = READ12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE12 = 0xAA
    pdata[0][offset + 0] = WRITE12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    //WRITE_AND_VERIFY_12 = 0xAE
    //pdata[0][offset + 0] = WRITE_AND_VERIFY_12;
    //pdata[0][offset + 1] = RESERVED;
    //pdata[0][offset + 2] = M_Byte1(0);//service action msb
    //pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    //pdata[0][offset + 4] = RESERVED;
    //pdata[0][offset + 5] = 0;
    //pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    //pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    //offset += 8;
    //if (rctd)
    //{
    //    //set CTPD to 1
    //    pdata[0][offset - 8 + 5] |= BIT1;
    //    //set up timeouts descriptor
    //    sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    //}
    //VERIFY12 = 0xAF
    pdata[0][offset + 0] = VERIFY12;
    pdata[0][offset + 1] = RESERVED;
    pdata[0][offset + 2] = M_Byte1(0);//service action msb
    pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
    pdata[0][offset + 4] = RESERVED;
    pdata[0][offset + 5] = 0;
    pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
    pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
    offset += 8;
    if (rctd)
    {
        //set CTPD to 1
        pdata[0][offset - 8 + 5] |= BIT1;
        //set up timeouts descriptor
        sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
    }
    if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT0)
    {
        //SECURITY_PROTOCOL_OUT = 0xB5
        pdata[0][offset + 0] = SECURITY_PROTOCOL_OUT;
        pdata[0][offset + 1] = RESERVED;
        pdata[0][offset + 2] = M_Byte1(0);//service action msb
        pdata[0][offset + 3] = M_Byte0(0);//service action lsb if non zero set byte 5, bit0
        pdata[0][offset + 4] = RESERVED;
        pdata[0][offset + 5] = 0;
        pdata[0][offset + 6] = M_Byte1(CDB_LEN_12);
        pdata[0][offset + 7] = M_Byte0(CDB_LEN_12);
        offset += 8;
        if (rctd)
        {
            //set CTPD to 1
            pdata[0][offset - 8 + 5] |= BIT1;
            //set up timeouts descriptor
            sntl_Set_Command_Timeouts_Descriptor(0, 0, pdata[0], &offset);
        }
    }
    //now that we are here, we need to set the data length
    pdata[0][0] = M_Byte3(offset - 4);
    pdata[0][1] = M_Byte2(offset - 4);
    pdata[0][2] = M_Byte1(offset - 4);
    pdata[0][3] = M_Byte0(offset - 4);
    *dataLength = offset;
    return ret;
}

int sntl_Translate_SCSI_Report_Supported_Operation_Codes_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    int ret = SUCCESS;
    bool rctd = false;
    uint8_t reportingOptions = scsiIoCtx->cdb[2] & 0x07;
    uint8_t requestedOperationCode = scsiIoCtx->cdb[3];
    uint16_t requestedServiceAction = M_BytesTo2ByteValue(scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
    uint32_t allocationLength = M_BytesTo4ByteValue(scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8], scsiIoCtx->cdb[9]);
    uint8_t *supportedOpData = NULL;
    uint32_t supportedOpDataLength = 0;
    uint8_t senseKeySpecificDescriptor[8] = { 0 };
    uint8_t bitPointer = 0;
    uint16_t fieldPointer = 0;
    //filter out invalid fields
    if (((fieldPointer = 1) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[1], 7, 5) != 0)
        || ((fieldPointer = 2) != 0 && M_GETBITRANGE(scsiIoCtx->cdb[2], 6, 3) != 0)
        || ((fieldPointer = 10) != 0 && scsiIoCtx->cdb[10] != 0)
        )
    {
        if (bitPointer == 0)
        {
            uint8_t reservedByteVal = scsiIoCtx->cdb[fieldPointer];
            uint8_t counter = 0;
            if (fieldPointer == 2)
            {
                reservedByteVal &= 0x7F;//strip off rctd bit
            }
            while (reservedByteVal > 0 && counter < 8)
            {
                reservedByteVal >>= 1;
                ++counter;
            }
            bitPointer = counter - 1;//because we should always get a count of at least 1 if here and bits are zero indexed
        }
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //invalid field in CDB
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return ret;
    }
    if (scsiIoCtx->cdb[2] & BIT7)
    {
        rctd = true;
    }
    switch (reportingOptions)
    {
    case 0://return all op codes (return not supported for now until we get the other methods working...)
        ret = sntl_Create_All_Supported_Op_Codes_Buffer(device, rctd, &supportedOpData, &supportedOpDataLength);
        break;
    case 1://check operation code, service action ignored
        //check op code func
        ret = sntl_Check_Operation_Code(device, scsiIoCtx, requestedOperationCode, rctd, &supportedOpData, &supportedOpDataLength);
        break;
    case 2://check operation code and service action (error on commands that don't have service actions)
        //check opcode and service action func
        ret = sntl_Check_Operation_Code_and_Service_Action(device, scsiIoCtx, requestedOperationCode, requestedServiceAction, rctd, &supportedOpData, &supportedOpDataLength);
        break;
    case 3://case 1 or case 2 (SPC4+)
        if (SUCCESS == sntl_Check_Operation_Code(device, scsiIoCtx, requestedOperationCode, rctd, &supportedOpData, &supportedOpDataLength))
        {
            ret = SUCCESS;
        }
        else
        {
            //free this memory since the last function allocated it, but failed, then check if the op/sa combination is supported
            safe_Free(supportedOpData);
            supportedOpDataLength = 0;
            if (sntl_Check_Operation_Code_and_Service_Action(device, scsiIoCtx, requestedOperationCode, requestedServiceAction, rctd, &supportedOpData, &supportedOpDataLength))
            {
                ret = SUCCESS;
            }
            else
            {
                ret = NOT_SUPPORTED;
            }
        }
        break;
    default:
        bitPointer = 2;
        fieldPointer = 2;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        ret = NOT_SUPPORTED;
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        break;
    }
    if (supportedOpData && scsiIoCtx->pdata)
    {
        memcpy(scsiIoCtx->pdata, supportedOpData, M_Min(supportedOpDataLength, allocationLength));
    }
    safe_Free(supportedOpData);
    return ret;
}

//always sets Descriptor type sense data
int sntl_Translate_SCSI_Command(tDevice *device, ScsiIoCtx *scsiIoCtx)
{
    static bool deviceInfoAvailable = false;
    int ret = UNKNOWN;
    bool invalidFieldInCDB = false;
    bool invalidOperationCode = false;
    uint16_t fieldPointer = 0;
    uint8_t bitPointer = 0;
    //if we weren't given a sense data pointer, use the sense data in the device structure
    if (!scsiIoCtx->psense)
    {
        scsiIoCtx->psense = device->drive_info.lastCommandSenseData;
        scsiIoCtx->senseDataSize = SPC3_SENSE_LEN;
    }
    memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
    uint8_t controlByteOffset = scsiIoCtx->cdbLength - 1;
    if (scsiIoCtx->cdb[OPERATION_CODE] == 0x7E || scsiIoCtx->cdb[OPERATION_CODE] == 0x7F)
    {
        //variable length and 32byte CDBs have the control byte at offset 1
        controlByteOffset = 1;
    }
    //check for bits in the control byte that are set that aren't supported
    if ((bitPointer = 7 && scsiIoCtx->cdb[controlByteOffset] & BIT7) //vendor specific
        || (bitPointer = 6 && scsiIoCtx->cdb[controlByteOffset] & BIT6) //vendor specific
        || (bitPointer = 5 && scsiIoCtx->cdb[controlByteOffset] & BIT5) //reserved
        || (bitPointer = 4 && scsiIoCtx->cdb[controlByteOffset] & BIT4) //reserved
        || (bitPointer = 3 && scsiIoCtx->cdb[controlByteOffset] & BIT3) //reserved
        || (bitPointer = 2 && scsiIoCtx->cdb[controlByteOffset] & BIT2) //naca
        || (bitPointer = 1 && scsiIoCtx->cdb[controlByteOffset] & BIT1) //flag (obsolete in SAM2)
        || (!(bitPointer = 0) && scsiIoCtx->cdb[controlByteOffset] & BIT0) //link (obsolete in SAM4)
        )
    {
        uint8_t senseKeySpecificDescriptor[8] = { 0 };
        fieldPointer = controlByteOffset;
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        //set up a sense key specific information descriptor to say that this bit is not valid
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0x00, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        return SUCCESS;
    }
    sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
    //if the ataIdentify data is zero, send an identify at least once so we aren't sending that every time we do a read or write command...inquiry, read capacity will always do one though to get the most recent data
    if (!deviceInfoAvailable)
    {
        uint8_t zeroData[NVME_IDENTIFY_DATA_LEN] = { 0 };
        if (memcmp(&device->drive_info.IdentifyData.nvme.ctrl, zeroData, LEGACY_DRIVE_SEC_SIZE) == 0)
        {
            //call fill ata drive info to set up vars inside the device struct which the other commands will use.
            if (SUCCESS != fill_In_NVMe_Device_Info(device))
            {
                return FAILURE;
            }
            deviceInfoAvailable = true;
            sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_NO_ERROR, 0, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, NULL, 0);
        }
        else
        {
            deviceInfoAvailable = true;
        }
    }
    //start checking the scsi command and call the function to translate it
    //All functions within this switch-case should dummy up their own sense data specific to the translation!
    switch (scsiIoCtx->cdb[OPERATION_CODE])
    {
    case INQUIRY_CMD://mostly identify information, but some log info for some pages.
        ret = sntl_Translate_SCSI_Inquiry_Command(device, scsiIoCtx);
        break;
    case READ_CAPACITY_10://identify
        ret = sntl_Translate_SCSI_Read_Capacity_Command(device, false, scsiIoCtx);
        break;
    case 0x9E:
        //check the service action
        switch (scsiIoCtx->cdb[1] & 0x1F)
        {
        case 0x10://Read Capacity 16
            ret = sntl_Translate_SCSI_Read_Capacity_Command(device, true, scsiIoCtx);
            break;
        default:
            fieldPointer = 1;
            bitPointer = 4;
            invalidFieldInCDB = true;
            break;
        }
        break;
    //To Support format, we need to store the last block descriptor so we format with the correct block size when running format. We also need to send back "format corrupt" until the format has been done.
    //case SCSI_FORMAT_UNIT_CMD:
        //ret = sntl_Translate_SCSI_Format_Unit_Command(device, scsiIoCtx);
        //break;
    case LOG_SENSE_CMD:
        ret = sntl_Translate_SCSI_Log_Sense_Command(device, scsiIoCtx);
        break;
    case MODE_SELECT_6_CMD:
    case MODE_SELECT10:
        sntl_Translate_SCSI_Mode_Select_Command(device, scsiIoCtx);
        break;
    case MODE_SENSE_6_CMD:
    case MODE_SENSE10:
        ret = sntl_Translate_SCSI_Mode_Sense_Command(device, scsiIoCtx);
        break;
    case READ6:
    case READ10:
    case READ12:
    case READ16://read commands
        ret = sntl_Translate_SCSI_Read_Command(device, scsiIoCtx);
        break;
    //SNTL spec doesn't define it, but we should add a way to read the telemetry log through here similar to SAT's translation for SATA Internal Status log
#if defined (SNTL_EXT)
    //case READ_BUFFER_CMD:
    //  ret = sntl_Translate_SCSI_Read_Buffer_Command(device, scsiIoCtx);
    //  break;
#endif
    case REPORT_LUNS_CMD:
        ret = sntl_Translate_SCSI_Report_Luns_Command(device, scsiIoCtx);
        break;
    case 0xA3://check the service action for this one!
        switch (scsiIoCtx->cdb[1] & 0x1F)
        {
        case 0x0C://report supported op codes <- this is essentially returning either a massive table of supported commands, or it is sending back data or an error based off a switch statement
            //update this as more supported op codes are added
            ret = sntl_Translate_SCSI_Report_Supported_Operation_Codes_Command(device, scsiIoCtx);
            break;
        default:
            fieldPointer = 1;
            bitPointer = 4;
            invalidFieldInCDB = true;
            break;
        }
        break;
    case REQUEST_SENSE_CMD://bunch of different commands...or read the "last command sense data" and change it from fixed to descriptor, or the other way around
        ret = sntl_Translate_SCSI_Request_Sense_Command(device, scsiIoCtx);
        break;
    //SNTL doesn't have this, but we should add it similar to SAT
#if defined (SNTL_EXT)
    case SANITIZE_CMD://NVMe Sanitize
        if (device->drive_info.IdentifyData.nvme.ctrl.sanicap != 0)
        {
            ret = sntl_Translate_SCSI_Sanitize_Command(device, scsiIoCtx);
        }
      break;
#endif
    case SECURITY_PROTOCOL_IN:
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT0)
        {
            ret = sntl_Translate_SCSI_Security_Protocol_In_Command(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    case SECURITY_PROTOCOL_OUT:
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT0)
        {
            ret = sntl_Translate_SCSI_Security_Protocol_Out_Command(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    case SEND_DIAGNOSTIC_CMD://SNTL's translation is lacking. Should add DST support similar to SAT if the drive supports DST
        ret = sntl_Translate_SCSI_Send_Diagnostic_Command(device, scsiIoCtx);
        break;
    case START_STOP_UNIT_CMD://Varies for EPC and NON-EPC drives
        ret = sntl_Translate_SCSI_Start_Stop_Unit_Command(device, scsiIoCtx);
        break;
    case SYNCHRONIZE_CACHE_10:
    case SYNCHRONIZE_CACHE_16_CMD:
        ret = sntl_Translate_SCSI_Synchronize_Cache_Command(device, scsiIoCtx);//ATA Flush cache command
        break;
    case TEST_UNIT_READY_CMD:
        ret = sntl_Translate_SCSI_Test_Unit_Ready_Command(device, scsiIoCtx);
        break;
    case UNMAP_CMD://Data Set management-TRIM
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT2)
        {
            ret = sntl_Translate_SCSI_Unmap_Command(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    case VERIFY10:
    case VERIFY12:
    case VERIFY16:
        ret = sntl_Translate_SCSI_Verify_Command(device, scsiIoCtx);//compare command
        break;
    case WRITE6:
    case WRITE10:
    case WRITE12:
    case WRITE16:
        ret = sntl_Translate_SCSI_Write_Command(device, scsiIoCtx);//write command
        break;
#if defined (SNTL_EXT)
    //These are not part of SNTL. We could add support IF the bytecheck field is specified the same as verify translation requires.
    //The similar command (but reverse order of operations) is below (compare and write)
    //case WRITE_AND_VERIFY_10:
    //case WRITE_AND_VERIFY_12:
    //case WRITE_AND_VERIFY_16:
    //  ret = sntl_Translate_SCSI_Write_And_Verify_Command(device, scsiIoCtx);//ATA Write, then read-verify commands
    //  break;
#endif
    //case COMPARE_AND_WRITE://can only do this if fused commands are supported
        //ret = sntl_Translate_SCSI_Compare_And_Write_Command(device, scsiIoCtx);
        //break;
    case WRITE_BUFFER_CMD://Firmware Download
        if (device->drive_info.IdentifyData.nvme.ctrl.oacs & BIT2)
        {
            ret = sntl_Translate_SCSI_Write_Buffer_Command(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    case WRITE_LONG_10_CMD://Write Uncorrectable
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
        {
            ret = sntl_Translate_SCSI_Write_Long(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    case 0x9F://write uncorrectable ext-check service action for 11h
        switch (scsiIoCtx->cdb[1] & 0x1F)
        {
        case 0x11://write uncorrectable
            if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT1)
            {
                ret = sntl_Translate_SCSI_Write_Long(device, scsiIoCtx);
            }
            else
            {
                invalidOperationCode = true;
            }
            break;
        default:
            fieldPointer = 1;
            bitPointer = 4;
            invalidFieldInCDB = true;
            break;
        }
        break;
#if defined (SNTL_EXT)
    //SNTL doesn't describe these, but they could be added similar to SAT's specification
    //case WRITE_SAME_10_CMD://Sequential write commands
    //case WRITE_SAME_16_CMD://Sequential write commands
    //  ret = sntl_Translate_SCSI_Write_Same_Command(device, scsiIoCtx);
    //  break;
#endif
    case PERSISTENT_RESERVE_IN_CMD:
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT5)
        {
            //reservations supported
            ret = sntl_Translate_Persistent_Reserve_In(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    case PERSISTENT_RESERVE_OUT_CMD:
        if (device->drive_info.IdentifyData.nvme.ctrl.oncs & BIT5)
        {
            //reservations supported
            ret = sntl_Translate_Persistent_Reserve_Out(device, scsiIoCtx);
        }
        else
        {
            invalidOperationCode = true;
        }
        break;
    default:
        invalidOperationCode = true;
        break;
    }
    if (invalidFieldInCDB)
    {
        uint8_t senseKeySpecificDescriptor[8] = { 0 };
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x24, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        ret = NOT_SUPPORTED;
    }
    if (invalidOperationCode)
    {
        uint8_t senseKeySpecificDescriptor[8] = { 0 };
        bitPointer = 7;
        fieldPointer = 0;//operation code is not right
        sntl_Set_Sense_Key_Specific_Descriptor_Invalid_Field(senseKeySpecificDescriptor, true, true, bitPointer, fieldPointer);
        sntl_Set_Sense_Data_For_Translation(scsiIoCtx->psense, scsiIoCtx->senseDataSize, SENSE_KEY_ILLEGAL_REQUEST, 0x20, 0, device->drive_info.softSATFlags.senseDataDescriptorFormat, senseKeySpecificDescriptor, 1);
        ret = NOT_SUPPORTED;
    }
    return ret;
}

#endif // (DISABLE_NVME_PASSTHROUGH)
