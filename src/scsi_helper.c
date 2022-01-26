//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#include "common.h"
#include "scsi_helper_func.h"
#include "ata_helper_func.h"
#include <ctype.h>//for checking for printable characters

#if !defined (DISABLE_NVME_PASSTHROUGH)
#include "nvme_helper_func.h"
#endif

uint16_t calculate_Logical_Block_Guard(uint8_t *buffer, uint32_t userDataLength, uint32_t totalDataLength)
{
    uint16_t crc = 0;//Can also be all F's to invert it. TODO: should invert be a boolean option to this function? - TJE
    uint16_t const polynomial = 0x8BB7L;
    if (!buffer)
    {
        return 0;
    }
    for (uint32_t iter = 0; iter < userDataLength && iter < totalDataLength; iter += 2)
    {
        uint16_t x = M_BytesTo2ByteValue(buffer[iter], buffer[iter + 1]);
        for (uint32_t bitIter = 0; bitIter < 16; ++bitIter)
        {
            bool bit15Set = ((x & BIT15) == BIT15) ^ ((crc & BIT15) == BIT15);
            x <<= 1;
            crc <<= 1;
            if (bit15Set)
            {
                crc ^= polynomial;
            }
        }

    }
    return crc;
}

//this is mean to only be called by check_Sense_Key_asc_And_ascq()
void print_sense_key(const char* senseKeyToPrint, uint8_t senseKeyValue)
{
    printf("Sense Key: %"PRIX8"h = %s\n", senseKeyValue, senseKeyToPrint);
    fflush(stdout);
}
//this is meant to only be called by check_Sense_Key_asc_And_ascq()
void print_acs_ascq(const char* acsAndascqStringToPrint, uint8_t ascValue, uint8_t ascqValue)
{
    printf("ASC & ASCQ: %"PRIX8"h - %"PRIX8"h = %s\n", ascValue, ascqValue, acsAndascqStringToPrint);
    fflush(stdout);
}

//this is meant to only be called by check_Sense_Key_asc_And_ascq()
void print_Field_Replacable_Unit_Code(tDevice *device, const char *fruMessage, uint8_t fruCode)
{
    //we'll only print out a translatable string for seagate drives since fru is vendor specific
    if (is_Seagate(device, false) == true && fruMessage && device->drive_info.interface_type == SCSI_INTERFACE)
    {
        printf("FRU: %"PRIX8"h = %s\n", fruCode, fruMessage);
        fflush(stdout);
    }
    else
    {
        if (fruCode == 0)
        {
            printf("FRU: %"PRIX8"h = No Additional Information\n", fruCode);
        }
        else
        {
            printf("FRU: %"PRIX8"h = Vendor Specific\n", fruCode);
        }
        fflush(stdout);
    }
}

int check_Sense_Key_ASC_ASCQ_And_FRU(tDevice *device, uint8_t senseKey, uint8_t asc, uint8_t ascq, uint8_t fru)
{
    int ret = UNKNOWN;//if this gets returned from this function, then something is not right...
    //first check the senseKey
    senseKey = senseKey & 0x0F;//strip off bits that are not part of the sense key
    switch (senseKey)
    {
    case SENSE_KEY_NO_ERROR:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("No Error", senseKey);
        }
        ret = SUCCESS;
        break;
    case SENSE_KEY_RECOVERED_ERROR:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Recovered Error", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_NOT_READY:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Not Ready", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_MEDIUM_ERROR:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Medium Error", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_HARDWARE_ERROR:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Hardware Error", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_ILLEGAL_REQUEST:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Illegal Request", senseKey);
        }
        ret = NOT_SUPPORTED;
        break;
    case SENSE_KEY_UNIT_ATTENTION:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Unit Attention", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_DATA_PROTECT:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Data Protect", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_BLANK_CHECK:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Blank Check", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_VENDOR_SPECIFIC:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Vendor Specific", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_COPY_ABORTED:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Copy Aborted", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_ABORTED_COMMAND:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Aborted Command", senseKey);
        }
        ret = ABORTED;
        break;
    case SENSE_KEY_RESERVED:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Reserved", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_VOLUME_OVERFLOW:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Volume Overflow", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_MISCOMPARE:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Miscompare", senseKey);
        }
        ret = FAILURE;
        break;
    case SENSE_KEY_COMPLETED:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Completed", senseKey);
        }
        ret = SUCCESS;
        break;
    default:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key("Invalid sense key!", senseKey);
        }
        return BAD_PARAMETER;
    }
    //now check the asc and ascq combination...this is going to be very large set of switch cases to do this...
    //FYI there is no rhyme or reason to the order...I just went through the massive table in SPC4...and only things for direct access block devices were implemented - TJE
    switch (asc)
    {
    case 0x00:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Additional Sense Information", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Filemark Detected", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("End-Of_Partition/Medium Detected", asc, ascq);
            }
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Setmark Detected", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Beginning-Of-Partition/Medium Detected", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("End-Of-Data Detected", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("I/O Process Terminated", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Programmable Early Warning Detected", asc, ascq);
            }
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Audio Play Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Audio Play Operation Paused", asc, ascq);
            }
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Audio Play Operation Successfully Completed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Audio Play Operation Stopped Due To Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Current Audio Status To Return", asc, ascq);
            }
            break;
        case 0x16:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x17:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cleaning Requested", asc, ascq);
            }
            ret = UNKNOWN;
            break;
        case 0x18:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Erase Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x19:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Locate Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Rewind Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Set Capacity Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Verify Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ATA Passthrough Information Available", asc, ascq);
            }
            ret = UNKNOWN;
            break;
        case 0x1E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Conflicting SA Creation Request", asc, ascq);
            }
            ret = UNKNOWN;
            break;
        case 0x1F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Transitioning To Another Power Condition", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x20:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Extended Copy Information Available", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x21:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Atomic Command Aborted Due To ACA", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x01:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Index/Sector Signal", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x02:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Seek Complete", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x03:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Peripheral Device Write Fault", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Write Current", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Excessive Write Errors", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x04:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Cause Not Reported", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Is In The Process Of Becoming Ready", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Initializing Command Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Manual Intervention Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Format In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Rebuild In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Recalculation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Operation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Self-Test In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Accessible, Asymetric Access State Transition", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Accessible, Target Port In Standby State", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Accessible, Target Port in Unavailable State", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Structure Check Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Security Session In Progress", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Auxilary Memory Not Accessible", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Notify (Enable Spinup) Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, SA Creation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Space Allocation In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Robotics Disabled", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x16:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Configuration Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x17:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Calibration Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x18:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, A Door Is Open", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x19:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Operating In Sequential Mode", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Start Stop Unit Command In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Sanitize In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Additional Power Use Not Yet Granted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x1D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Configuration In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x1E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Microcode Activation Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x1F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Microcode Download Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x20:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Logical Unit Reset Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x21:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Hard Reset Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x22:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Power Cycle Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x23:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Ready, Affiliation Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x05:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Does Not Respond To Selection", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x06:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Reference Position Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x07:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Multiple Peripheral Devices Selected", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x08:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Communication Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Communication Time-Out", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Communication Parity Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Communication CRC Error (Ultra-DMA/32)", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unreachable Copy Target", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x09:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Track Following Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Tracking Servo Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Focus Servo Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Servo Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Head Select Fault", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Vibration Induced Tracking Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x0A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Log Overflow", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x0B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Specified Temperature Exceeded", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Enclosure Degraded", asc, ascq);
            }
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Background Self-Test Failed", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Background Pre-Scan Detected Medium Error", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Background Media Scan Detected Medium Error", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Non-Volitile Cache Now Volitile", asc, ascq);
            }
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Degraded Power To Non-Volitile Cache", asc, ascq);
            }
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Power Loss Expected", asc, ascq);
            }
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Device Statistics Notification Active", asc, ascq);
            }
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - High Critical Temperature Limit Exceeded", asc, ascq);
            }
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Low Critical Temperature Limit Exceeded", asc, ascq);
            }
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - High Operating Temperature Limit Exceeded", asc, ascq);
            }
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Low Operating Temperature Limit Exceeded", asc, ascq);
            }
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - High Critical Humidity Limit Exceeded", asc, ascq);
            }
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Low Critical Humidity Limit Exceeded", asc, ascq);
            }
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - High Operating Humidity Limit Exceeded", asc, ascq);
            }
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Low Operating Humidity Limit Exceeded", asc, ascq);
            }
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Microcode Security At Risk", asc, ascq);
            }
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Warning - Microcode Digital Signature Validation Failure", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x0C:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Recovered With Auto Reallocation", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Auto Reallocation Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Recommend Reassignment", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Compression Check Miscompare Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Expansion Occurred During Compression", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Block Not Compressible", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Recovery Needed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Recovery Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Loss Of Streaming", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Padding Blocks Added", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Auxiliary Memory Write Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Unexpected Unsolicited Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Not Enough Unsolicited Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Multiple Write Errors", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defects In Error Window", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incomplete Multiple Atomic Write Operations", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Recovery Scan Needed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Error - Insufficient Zone Resources", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x0D:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Detected By Third Party Temporary Initiator", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Third Party Device Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Target Device Not Reachable", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incorrect Copy Target Device Type", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Target Device Data Underrun", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Target Device Data Overrun", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x0E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Information Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Information Unit Too Short", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Information Unit Too Long", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Field In Command Information Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x0F:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x10:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ID CRC Or ECC Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Block Guard Check Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Block Application Tag Check Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Block Reference Tag Check Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Block Protection Error On Recover Buffered Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Block Protection Method Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x11:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unrecovered Read Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Retries Exhausted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Too Long To Correct", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Multiple Read Errors", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unrecovered Read Error - Auto Reallocate Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("L-EC Uncorrectable Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("CIRC Unrecovered Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Re-synchonization Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incomplete Block Read", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Gap Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Miscorrected Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unrecovered Read Error - Recommend Reassignment", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unrecovered Read Error - Recommend Rewrite The Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("De-compression CRC Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Decompress Using Declared Algorithm", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Reading UPC/EAN Number", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Reading ISRC Number", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Error - Loss Of Streaming", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Auxiliary Memory Read Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Error - Failed Retransmission Request", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Error - LBA Marked Bad By Application Client", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write After Sanitize Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x12:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Address Mark Not Found for ID Field", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x13:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Address Mark Not Found for Data Field", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x14:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recorded Entity Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Record Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Filemark Or Setmark Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("End-Of-Data Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Block Sequence Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Record Not Found - Recommend Reassignment", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Record Not Found - Data Auto-Reallocated", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Locate Operation Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x15:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Random Positioning Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Mechanical Positioning Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Positioning Error Detected By Read Of Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x16:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Synchronization Mark Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Sync Error - Data Rewritten", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Sync Error - Recommend Rewrite", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Sync Error - Data Auto-Reallocation", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Sync Error - Recommend Reassignment", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x17:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With No Error Correction Applied", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Retries", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Positive Head Offset", asc, ascq);
            }
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Negative Head Offset", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Retries And/Or CIRC Applied", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data Using Previous Sector ID", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data Without ECC - Data Auto-Reallocated", asc, ascq);
            }
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data Without ECC - Recommend Reassignment", asc, ascq);
            }
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data Without ECC - Recommend Rewrite", asc, ascq);
            }
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data Without ECC - Data Rewritten", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x18:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Error Correction Applied", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Error Correction & Retries Applied", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data - Data Auto-Reallocated", asc, ascq);
            }
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With CIRC", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With L-EC", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data - Recommend Reassignment", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data - Recommend Rewrite", asc, ascq);
            }
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With ECC - Data Rewritten", asc, ascq);
            }
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered Data With Linking", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x19:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defect List Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defect List Not Available", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defect List Error In Primary List", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defect List Error In Grown List", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x1A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Parameter List Length Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x1B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Synchronous Data Transfer Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x1C:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defect List Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Primary Defect List Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Grown Defect List Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x1D:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Miscompare During Verify Operation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Miscompare During Verify Of Unmapped LBA", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x1E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recovered ID With ECC Correction", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x1F:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Partial Defect List Transfer", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x20:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Command Operation Code", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - Initiator Pending - Enrolled", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - No Access Rights", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - Invalid Management ID Key", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Command While In Write Capable State", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Command While In Read Capable State", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Command While In Explicit Address Mode", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Command While In Implicit Address Mode", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - Enrollment Conflict", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - Invalid Logical Unit Identifier", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - Invalid Proxy Token", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Access Denied - ACL LUN Conflict", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Command When Not In Append-Only Mode", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Not An Administrative Logical Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Not A Subsidiary Logical Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Not A Conglomerate Logical Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x21:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Block Address Out Of Range", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Element Address", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Address For Write", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Write Crossing Layer Jump", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unaligned Write Command", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Boundary Violation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Attempt To Read Invalid Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Boundary Violation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Misaligned Write Command", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x22:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Function. Use 22 00, 24 00, or 26 00", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x23:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Cause Not Reportable", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Unsupported Token Type", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Remote Token Usage Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Remote ROD Token Creation Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Token Unknown", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Token Corrupt", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Token Revoked", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Token Expired", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Token Cancelled", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Token Deleted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Token Operation - Invalid Token Length", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x24:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Field In CDB", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("CDB Decryption Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid CDB Field While In Explicit Block Address Model", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid CDB Field While In Implicit Block Address Model", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Security Audit Value Frozen", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Security Working Key Frozen", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Nonce Not Unique", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Nonce Timestamp Out Of Range", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid XCDB", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Fast Format", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x25:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x26:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Field In Parameter List", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Parameter Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Parameter Value Invalid", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Threshold Parameters Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Release Of Persistent Reservation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Decryption Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Too Many Target Descriptors", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unsupported Target Descriptor Type Code", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Too Many Segment Descriptors", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unsupported Segment Descriptor Type Code", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unexpected Inexact Segment", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Inline Data Length Exceeded", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Operation For Copy Source Or Destination", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Segment Granularity Violation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Parameter While Port Is Enabled", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Data-Out Buffer Integrity Check Value", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Decryption Key Fail Limit Reached", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incomplete Key-Associated Data Set", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Vendor Specific Key Reference Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Application Tag Mode Page Is Invalid", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Tape Stream Mirroring Prevented", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Source Or Copy Destination Not Authorized", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x27:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Protected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Write Protected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Software Write Protected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Associated Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Persistent Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Permanent Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Conditional Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Space Allocation Failed Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Zone Is Read Only", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x28:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Not Ready To Ready Change, Medium May Have Changed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Import or Export Element Accessed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Format-Layer May Have Changed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Import/Export Element Accessed, Medium Changed", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x29:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power On, Reset, Or Bus Device Reset Occurred", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power On Occurred", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SCSI Bus Reset Occurred", asc, ascq);
            }
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Bus Device Reset Function Occurred", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Device Internal Reset", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Transceiver Mode Changed To Single-Ended", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Transceiver Mode Changed To LVD", asc, ascq);
            }
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("I_T Nexus Loss Occurred", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x2A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Parameters Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Mode Parameters Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Log Parameters Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Reservations Preempted", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Reservations Released", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Registrations Preempted", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Asymmetric Access State Changed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Implicit Asymetric Access State Transition Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Priority Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Capacity Data Has Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error History I_T Nexus Cleared", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error History Snapshot Released", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Recovery Attributes Have Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Encryption Capabilities Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Timestamp Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Encryption Parameters Changed By Another I_T Nexus", asc, ascq);
            }
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Encryption Parameters Changed By Vendor Specific Event", asc, ascq);
            }
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Encryption Key Instance Counter Has Changed", asc, ascq);
            }
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SA Creation Capabilities Has Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Removal Precention Preempted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x16:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Zone Reset Write Pointer Recommended", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x2B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Cannot Execute Since Host Cannot Disconnect", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x2C:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command Sequence Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Too Many Windows Specified", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Combination Of Windows Specified", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Current Program Area Is Not Empty", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Current Program Area Is Empty", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Power Condition Request", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Persistent Prevent Conflict", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Previous Busy Status", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Previous Task Set Full Status", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Previous Reservation Conflict Status", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Partition Or Collection Contains User Objects", asc, ascq);
            }
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Not Reserved", asc, ascq);
            }
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ORWrite Generation Does Not Match", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Reset Write Pointer Not Allowed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Zone Is Offline", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Stream Not Open", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unwritten Data In Zone", asc, ascq);
            }
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Descriptor Format Sense Data Required", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x2D:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Overwrite Error On Update In Place", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x2E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Time For Operation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command Timeout Before Processing", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command Timeout During Processing", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command Timeout During Processing Due To Error Recovery", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x2F:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Commands Cleared By Another Initiator", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Commands Cleared By Power Loss Notification", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Commands Cleared By Device Server", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Some Commands Cleared By Queuing Layer Event", asc, ascq);
            }
            ret = FAILURE;
            break;
        /*case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Space Allocation Failed Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;*/
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x30:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incompatible Medium Installed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Read Medium - Unknown Format", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Read Medium - Incompatible Format", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cleaning Cartridge Installed", asc, ascq);
            }
            ret = UNKNOWN;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Write Medium - Unknown Format", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Write Medium - Incompatible Format", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Format Medium - Incompatible Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cleaning Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cannot Write - Application Code Mismatch", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Current Session Not Fixated For Append", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cleaning Request Rejected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("WORM Medium - Overwrite Attempted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("WORM Medium - Integrity Check", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Not Formatted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incompatible Volume Type", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incompatible Volume Qualifier", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cleaning Volume Expired", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x31:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Format Corrupted", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Format Command Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Zoned Formatting Failed Due To Spare Linking", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Sanitize Command Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x32:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No Defect Space Location Available", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Defect List Update Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x33:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Tape Length Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x34:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Enclosure Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x35:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Enclosure Services Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unsupported Enclosure Function", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Enclosure Services Unavailable", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Enclosure Services Transfer Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Enclosure Services Transfer Refused", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Enclosure Services Checksum Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x36:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Ribbon, Ink, Or Toner Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x37:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Rounded Parameter", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x38:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Event Status Notification", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ESN - Power Management Class Event", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ESN - Media Class Event", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ESN - Device Busy Class Event", asc, ascq);
            }
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Thin Provisioning Soft Threshold Reached", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x39:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Saving Parameters Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x3A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Not Present", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Not Present - Tray Closed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Not Present - Tray Open", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Not Present - Loadable", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Not Present - Medium Auxilary Memory Accessible", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x3B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Sequential Positioning Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Tape Position Error At Beginning-Of-Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Tape Position Error At End-Of-Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Tape Or Electronic Vertical Forms Unit Not Ready", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Slew Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Paper Jam", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Failed To Sense Top-Of-Form", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Failed To Sense Bottom-Of-Form", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Reposition Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Past End Of Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Past Beginning Of Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Position Past End Of Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Position Past Beginning Of Medium", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Destination Element Full", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Source Element Empty", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("End Of Medium Reached", asc, ascq);
            }
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Magazine Not Accessible", asc, ascq);
            }
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Magazine Removed", asc, ascq);
            }
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Magazine Inserted", asc, ascq);
            }
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Magazine Locked", asc, ascq);
            }
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Magazine Unlocked", asc, ascq);
            }
            break;
        case 0x16:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Mechanical Positioning Or Changer Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x17:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Past End Of User Object", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x18:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Element Disabled", asc, ascq);
            }
            break;
        case 0x19:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Element Enabled", asc, ascq);
            }
            break;
        case 0x1A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Removed", asc, ascq);
            }
            break;
        case 0x1B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Inserted", asc, ascq);
            }
            break;
        case 0x1C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Too Many Logical Objects On Partition To Supported Operation", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x3C:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x3D:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Bits In Identify Message", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x3E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Has Not Self-Configured Yet", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Timeout On Logical Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Failed Self-Test", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Unable to Update Self-Test Log", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x3F:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Target Operating Conditions Have Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Microcode Has Been Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Changed Operation Definition", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Inquiry Data Has Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Component Device Attached", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Device Identifier Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Redundancy Group Created Or Modified", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Redundancy Group Deleted", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spare Created Or Modified", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spare Deleted", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Volume Set Created Or Modified", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Volume Set Deleted", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Volume Set Deassigned", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Volume Set Reassigned", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Reported LUNs Data Has Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Echo Buffer Overwritten", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Loadable", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Auxilary Memory Accessible", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("iSCSI IP Address Added", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("iSCSI IP Address Removed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("iSCSI IP Address Changed", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Inspect Referrals Sense Descriptors", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x16:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Microcode Has Been Changed Without Reset", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x17:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Zone Transition To Full", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x18:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Bind Completed", asc, ascq);
            }
            break;
        case 0x19:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Bind Redirected", asc, ascq);
            }
            break;
        case 0x1A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Subsidiary Binding Changed", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x40:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("RAM Failure (Should Use 40 NN) ", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (ascq >= 0x80/*  && ascq <= 0xFF */)
            {
                if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
                {
                    printf("asc & ascq: %" PRIX8 "h - %" PRIX8 "h = Diagnostic Failure On Component %02" PRIX8 "h\n", asc, ascq, ascq);
                }
                ret = FAILURE;
            }
            else
            {
                if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
                ret = UNKNOWN;
            }
            break;
        }
        break;
    case 0x41:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Path Failure (Should Use 40NN)", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x42:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power-on Or Self-Test Failure (Should use 40 NN)", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x43:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Message Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x44:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Internal Target Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Persistent Reservation Information Lost", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x71:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ATA Device Failed Set Features", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x45:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Select Or Reselect Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x46:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unsuccessful Soft Reset", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x47:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SCSI Parity Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Phase CRC Error Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SCSI Parity Error Detected During ST Data Phase", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Information Unit uiCRC Error Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Asynchronous Information Protection Error Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Protocol Service CRC Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PHY Test Function In Progress", asc, ascq);
            }
            ret = IN_PROGRESS;
            break;
        case 0x7F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Some Commands Cleared By ISCSI Protocol Event", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x48:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Initiator Detected Error Message Received", asc, ascq);
            }
            ret = SUCCESS;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x49:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Message Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x4A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command Phase Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x4B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Phase Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Target Port Transfer Tag Received", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Too Much Write Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ACK/NAK Timeout", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("NAK Received", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Offset Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Initiator Response Timeout", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Connection Lost", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data-In Buffer Overflow - Data Buffer Size", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data-In Buffer Overflow - Data Buffer Descriptor Area", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data-In Buffer Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data-Out Buffer Overflow - Data Buffer Size", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data-Out Buffer Overflow - Data Buffer Descriptor Area", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data-Out Buffer Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE Fabric Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE Completion Timeout", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE Completer Abort", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE Poisoned TLP Received", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE ECRC Check Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE Unsupported Request", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE ACS Violation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("PCIE TLP Prefix Blocked", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x4C:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Failed Self-Configuration", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x4D:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
        {
            printf("asc & ascq: %" PRIX8 "h - %" PRIX8 "h = Tagged Overlapped Commands. Task Tag = %02" PRIX8 "h\n", asc, ascq, ascq);
        }
        break;
    case 0x4E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Overlapped Commands Attempted", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x4F:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x50:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Append Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Write Append Position Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Position Error Related To Timing", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x51:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Erase Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Erase Failure - Incomplete Erase Operation Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x52:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cartridge Fault", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x53:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Media Load Or Eject Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unload Tape Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Removal Prevented", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Removal Prevented By Data Transfer Element", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Thread Or Unthread Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Volume Identifier Invalid", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Volume Identifier Missing", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Duplicate Volume Identifier", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Element Status Unknown", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Error - Load Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Error - Unload Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Error - Unload Missing", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Error - Eject Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Transfer Device Error - Library Communication Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x54:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SCSI To host System Interface Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x55:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("System Resource Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("System Buffer Full", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Reservation Resources", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Resources", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Registration Resources", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Access Control Resources", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Auxiliary Memory Out Of Space", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Quota Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Maximum Number Of Supplemental Decryption Keys Exceeded", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Medium Auxilary Memory Not Accessible", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Currently Unavailable", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Power For Operation", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Resources To Create ROD", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Resources To Create ROD Token", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Zone Resources", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Zone Resources To Complete Write", asc, ascq);
            }
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Maximum Number Of Streams Open", asc, ascq);
            }
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Resources To Bind", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x56:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x57:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unable To Recover Table-Of-Contents", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (ascq >= 0x80/*  && ascq <= 0xFF */)
            {
                print_acs_ascq("Vendor specific ascq code", asc, ascq);
            }
            else
            {
                print_acs_ascq("Unknown ascq code", asc, ascq);
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x58:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Generation Does Not Exist", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x59:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Updated Block Read", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x5A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Operator Request Or State Change Input", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Operator Medium Removal Request", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Operator Selected Write Protect", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Operator Selected Write Permit", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x5B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Log Exception", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Threshold Condition Met", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Log Counter At Maximum", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Log List Codes Exhausted", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x5C:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("RPL Status Change", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindles Synchronized", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindles Not Synchronized", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x5D:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Failure Prediction Threshold Exceeded", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Media Failure Prediction Threshold Exceeded", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Failure Prediction Threshold Exceeded", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spare Area Exhaustion Prediction Threshold Exceeded", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - General Hard Drive Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Drive Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Data Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x13:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Seek Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x14:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Too Many Block Reassigns", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x15:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Access Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x16:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Start Unit Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x17:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Channel Parametrics", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x18:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Controller Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x19:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Throughput Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x1A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Seek Time Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x1B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Spin-Up Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x1C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Drive Calibration Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x1D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Hardware Impending Failure - Power Loss Protection Circuit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x20:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - General Hard Drive Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x21:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Drive Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x22:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Data Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x23:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Seek Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x24:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Too Many Block Reassigns", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x25:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Access Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x26:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Start Unit Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x27:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Channel Parametrics", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x28:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Controller Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x29:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Throughput Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x2A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Seek Time Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x2B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Spin-Up Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x2C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Controller Impending Failure - Drive Calibration Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x30:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - General Hard Drive Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x31:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Drive Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x32:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Data Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x33:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Seek Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x34:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Too Many Block Reassigns", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x35:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Access Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x36:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Start Unit Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x37:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Channel Parametrics", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x38:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Controller Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x39:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Throughput Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x3A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Seek Time Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x3B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Spin-Up Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x3C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Channel Impending Failure - Drive Calibration Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x40:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - General Hard Drive Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x41:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Drive Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x42:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Data Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x43:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Seek Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x44:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Too Many Block Reassigns", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x45:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Access Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x46:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Start Unit Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x47:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Channel Parametrics", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x48:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Controller Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x49:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Throughput Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x4A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Seek Time Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x4B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Spin-Up Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x4C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Servo Impending Failure - Drive Calibration Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x50:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - General Hard Drive Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x51:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Drive Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x52:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Data Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x53:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Seek Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x54:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Too Many Block Reassigns", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x55:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Access Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x56:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Start Unit Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x57:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Channel Parametrics", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x58:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Controller Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x59:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Throughput Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x5A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Seek Time Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x5B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Spin-Up Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x5C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Spindle Impending Failure - Drive Calibration Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x60:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - General Hard Drive Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x61:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Drive Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x62:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Data Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x63:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Seek Error Rate Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x64:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Too Many Block Reassigns", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x65:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Access Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x66:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Start Unit Times Too High", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x67:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Channel Parametrics", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x68:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Controller Detected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x69:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Throughput Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x6A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Seek Time Performance", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x6B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Spin-Up Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x6C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Firmware Impending Failure - Drive Calibration Retry Count", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x73:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Media Impending Failure Endurance Limit Met", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0xFF:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Failure Prediction Threshold Exceeded (False)", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x5E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Low Power Condition On", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Idle Condition Activated By Timer", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Standby Condition Activated By Timer", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Idle Condition Activated By Command", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Standby Condition Activated By Command", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Idle_B Condition Activated By Timer", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Idle_B Condition Activated By Command", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Idle_C Condition Activated By Timer", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Idle_C Condition Activated By Command", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Standby_Y Condition Activated By Timer", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Standby_Y Condition Activated By Command", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x41:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power State Change To Active", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x42:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power State Change To Idle", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x43:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power State Change To Standby", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x45:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power State Change To Sleep", asc, ascq);
            }
            ret = SUCCESS;
            break;
        case 0x47:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power State Change To Device Control", asc, ascq);
            }
            ret = SUCCESS;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x5F:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x60:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Lamp Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x61:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Video ascuisition Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unable To ascuire Video", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Out Of Focus", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x62:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Scan Head Positioning Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x63:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("End Of User Area Encountered On This Track", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Packet Does Not Fit In Available Space", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x64:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Illegal Mode For This Track", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Packet Size", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x65:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Voltage Fault", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x66:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Automatic Document Feeder Cover Up", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Automatic Document Feeder Lift Up", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Document Jam In Automatic Document Feeder", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Document Miss Feed Automatic In Document Feeder", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x67:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Configuration Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Configuration Of Incapable Logical Units Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Add Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Modification Of Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Exchange Of Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Remove Of Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Attachment Of Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Creation Of Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Assign Failure Occurred", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Multiply Assigned Logical Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Set Target Port Groups Command Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("ATA Device Feature Not Enabled", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command Rejected", asc, ascq);
            }
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Explicit Bind Not Allowed", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x68:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Not Configured", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Subsidiary Logical Unit Not Configured", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x69:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Loss On Logical Unit", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Multiple Logical Unit Failures", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Parity/Data Mismatch", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x6A:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Informational, Refer To Log", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x6B:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("State Change Has Occurred", asc, ascq);
            }
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Redundancy Level Got Better", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Redundancy Level Got Worse", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x6C:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Rebuild Failure Occurred", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x6D:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Recalculate Failure Occurred", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x6E:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Command To Logical Unit Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x6F:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Protection Key Exchange Failure - Authentication Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Protection Key Exchange Failure - Key Not Present", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Copy Protection Key Exchange Failure - Key Not Established", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Read Of Scrambled Sector Without Authentication", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Media Region Code Is Mismatched To Logical Unit Region", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Drive Region Must Be Permanent/Region Reset Count Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Block Count For Binding Nonce Recording", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Conflict In Binding Nonce Recording", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Insufficient Permission", asc, ascq);
            }
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid Drive-Host Pairing Server", asc, ascq);
            }
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Drive-Host Pairing Suspended", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x70:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
        {
            printf("asc & ascq: %" PRIX8 "h - %" PRIX8 "h = Decompression Exception Short Algorithm ID of %" PRIX8 "", asc, ascq, ascq);
        }
        break;
    case 0x71:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Decompression Exception Long Algorithm ID", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x72:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Session Fixation Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Session Fixation Error Writing Lead-In", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Session Fixation Error Writing Lead-Out", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Session Fixation Error - Incomplete Track In Session", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Empty Or Partially Written Reserved Track", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No More Track Reservations Allowed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("RMZ Extension Is Not Allowed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("No More Test Zone Extensions Are Allowed", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x73:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("CD Control Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power Calibration Area Almost Full", asc, ascq);
            }
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power Calibration Area Is Full", asc, ascq);
            }
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Power Calibration Area Error", asc, ascq);
            }
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Program Memory Area Update Failuer", asc, ascq);
            }
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Program Memory Area Is Full", asc, ascq);
            }
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("RMA/PMA Is Almost Full", asc, ascq);
            }
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Current Power Calibration Area Almost Full", asc, ascq);
            }
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Current Power Calibration Area Is Full", asc, ascq);
            }
            break;
        case 0x17:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("RDZ Is Full", asc, ascq);
            }
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x74:
        switch (ascq)
        {
        case 0x00:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Security Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x01:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unable To Decrypt Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x02:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unencrypted Data Encountered While Decrypting", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x03:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incorrect Data Encryption Key", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x04:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Cryptographic Integrity Validation Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x05:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Error Decrypting Data", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x06:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unknown Signature Verification Key", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x07:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Encryption Parameters Not Useable", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x08:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Digital Signature Validation Failure", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x09:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Encryption Mode Mismatch On Read", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0A:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Encrypted Block Not Raw Read Enabled", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0B:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Incorrect Encryption Parameters", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0C:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Unable To Decrypt Parameter List", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x0D:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Encryption Algorithm Disabled", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x10:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SA Creation Parameter Value Invalid", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x11:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SA Creation Parameter Value Rejected", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x12:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Invalid SA Usage", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x21:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Data Encryption Configuration Prevented", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x30:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("SA Creation Parameter Not Supported", asc, ascq);
            }
            ret = NOT_SUPPORTED;
            break;
        case 0x40:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Authenticaion Failed", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x61:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("External Data Encryption Key Manager Access Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x62:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("External Data Encryption Key Manager Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x63:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("External Data Encryption Key Not Found", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x64:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("External Data Encryption Request Not Authorized", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x6E:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("External Data Encryption Control Timeout", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x6F:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("External Data Encryption Control Error", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x71:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Logical Unit Access Not Authorized", asc, ascq);
            }
            ret = FAILURE;
            break;
        case 0x79:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq("Security Conflict In Translated Device", asc, ascq);
            }
            ret = FAILURE;
            break;
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x75:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x76:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x77:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x78:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x79:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x7A:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x7B:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x7C:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x7D:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x7E:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    case 0x7F:
        switch (ascq)
        {
        case 0: //shutting up C4065 in VS2019 with this fallthough
            M_FALLTHROUGH
        default:
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                if (ascq >= 0x80/*  && ascq <= 0xFF */)
                {
                    print_acs_ascq("Vendor specific ascq code", asc, ascq);
                }
                else
                {
                    print_acs_ascq("Unknown ascq code", asc, ascq);
                }
            }
            ret = UNKNOWN;
            break;
        }
        break;
    default:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            if (asc >= 0x80 /* && asc <= 0xFF */)
            {
                print_acs_ascq("Vendor specific ASC & ascq code", asc, ascq);
            }
            else
            {
                print_acs_ascq("Unknown ASC & ASCQ code", asc, ascq);
            }
        }
        ret = UNKNOWN;
        break;
    }
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
    {
        print_Field_Replacable_Unit_Code(device, NULL, fru);
    }
    return ret;
}

void get_Sense_Key_ASC_ASCQ_FRU(uint8_t *pbuf, uint32_t pbufSize, uint8_t *senseKey, uint8_t *asc, uint8_t *ascq, uint8_t *fru)
{
    uint8_t format = pbuf[0] & 0x7F; //Stripping the last bit.
    uint8_t additionalSenseLength = pbuf[7];//total sense data length
    uint32_t iter = 8;//set to beginning of the descriptors
    //clear everything to zero first
    *senseKey = 0;
    *asc = 0;
    *ascq = 0;
    *fru = 0;

    switch (format)
    {
    case SCSI_SENSE_NO_SENSE_DATA:
        break;
    case SCSI_SENSE_CUR_INFO_FIXED:
    case SCSI_SENSE_DEFER_ERR_FIXED:
        *senseKey = pbuf[2] & 0x0F;
        *asc = pbuf[12];
        *ascq = pbuf[13];
        *fru = pbuf[14];
        break;
    case SCSI_SENSE_CUR_INFO_DESC:
    case SCSI_SENSE_DEFER_ERR_DESC:
        *senseKey = pbuf[1] & 0x0F;
        *asc = pbuf[2];
        *ascq = pbuf[3];
        //for descriptor format we have to loop through the buffer until we find the FRU descriptor (if available)
        while (iter < SPC3_SENSE_LEN && iter < pbufSize && iter < (C_CAST(uint32_t, additionalSenseLength) + UINT16_C(8)))
        {
            bool gotFRU = false;
            uint8_t descriptorType = pbuf[iter];
            uint8_t additionalLength = pbuf[iter + 1];//descriptor length
            switch (descriptorType)
            {
            case SENSE_DESCRIPTOR_FIELD_REPLACEABLE_UNIT:
                *fru = pbuf[iter + 3];
                gotFRU = true;
                break;
            case SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE:
                *fru = pbuf[iter + 7];
                gotFRU = true;
                break;
            default:
                break;
            }
            if (gotFRU)
            {
                break;
            }
            iter += additionalLength + 2;//the 2 is the number of bytes for the descriptor header
        }
        break;
    case SCSI_SENSE_VENDOR_SPECIFIC://vendor specific sense data format.
        break;
    default://unknown sense data format.
        break;
    }
}

void get_Information_From_Sense_Data(uint8_t *ptrSenseData, uint32_t senseDataLength, bool *valid, uint64_t *information)
{
    if (ptrSenseData && valid && senseDataLength > 0 && information)
    {
        *valid = false;
        *information = 0;
        uint8_t format = ptrSenseData[0] & 0x7F; //Stripping the last bit so we just get the format
        uint8_t descriptorLength = 0;//for descriptor format sense data
        uint16_t returnedLength = 8;//assume length returned is at least 8 bytes
        switch (format)
        {
        case SCSI_SENSE_NO_SENSE_DATA:
            break;
        case SCSI_SENSE_CUR_INFO_FIXED:
        case SCSI_SENSE_DEFER_ERR_FIXED:
            *valid = ptrSenseData[0] & BIT7;
            *information = M_BytesTo4ByteValue(ptrSenseData[3], ptrSenseData[4], ptrSenseData[5], ptrSenseData[6]);
            break;
        case SCSI_SENSE_CUR_INFO_DESC:
        case SCSI_SENSE_DEFER_ERR_DESC:
            returnedLength += ptrSenseData[SCSI_SENSE_ADDT_LEN_INDEX];
            //loop through the descriptors to see if a sense key specific descriptor was provided
            for (uint32_t offset = SCSI_DESC_FORMAT_DESC_INDEX; offset < SPC3_SENSE_LEN && offset < returnedLength && offset < senseDataLength; offset += descriptorLength + 2)
            {
                bool gotInformation = false;
                uint8_t descriptorType = ptrSenseData[offset];
                descriptorLength = ptrSenseData[offset + 1];
                switch (descriptorType)
                {
                case SENSE_DESCRIPTOR_INFORMATION:
                    *valid = ptrSenseData[offset + 2] & BIT7;
                    *information = M_BytesTo8ByteValue(ptrSenseData[offset + 4], ptrSenseData[offset + 5], ptrSenseData[offset + 6], ptrSenseData[offset + 7], ptrSenseData[offset + 8], ptrSenseData[offset + 9], ptrSenseData[offset + 10], ptrSenseData[offset + 11]);
                    gotInformation = true;
                    break;
                case SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE:
                    *valid = ptrSenseData[offset + 2] & BIT7;
                    *information = M_BytesTo8ByteValue(ptrSenseData[offset + 8], ptrSenseData[offset + 9], ptrSenseData[offset + 10], ptrSenseData[offset + 11], ptrSenseData[offset + 12], ptrSenseData[offset + 13], ptrSenseData[offset + 14], ptrSenseData[offset + 15]);
                    gotInformation = true;
                    break;
                default: //not a descriptor we care about, so skip it
                    break;
                }
                if (gotInformation || descriptorLength == 0)
                {
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
}

void get_Illegal_Length_Indicator_From_Sense_Data(uint8_t *ptrSenseData, uint32_t senseDataLength, bool *illegalLengthIndicator)
{
    if (ptrSenseData && senseDataLength > 0 && illegalLengthIndicator)
    {
        *illegalLengthIndicator = false;
        uint8_t format = ptrSenseData[0] & 0x7F; //Stripping the last bit so we just get the format
        uint8_t descriptorLength = 0;//for descriptor format sense data
        uint16_t returnedLength = 8 + ptrSenseData[SCSI_SENSE_ADDT_LEN_INDEX];
        switch (format)
        {
        case SCSI_SENSE_NO_SENSE_DATA:
            break;
        case SCSI_SENSE_CUR_INFO_FIXED:
        case SCSI_SENSE_DEFER_ERR_FIXED:
            *illegalLengthIndicator = ptrSenseData[2] & BIT5;
            break;
        case SCSI_SENSE_CUR_INFO_DESC:
        case SCSI_SENSE_DEFER_ERR_DESC:
            //loop through the descriptors to see if a sense key specific descriptor was provided
            for (uint32_t offset = SCSI_DESC_FORMAT_DESC_INDEX; offset < SPC3_SENSE_LEN && offset < returnedLength && offset < senseDataLength; offset += descriptorLength + 2)
            {
                bool gotILI = false;
                uint8_t descriptorType = ptrSenseData[offset];
                descriptorLength = ptrSenseData[offset + 1];
                switch (descriptorType)
                {
                case SENSE_DESCRIPTOR_BLOCK_COMMANDS://SBC
                    *illegalLengthIndicator = ptrSenseData[offset + 3] & BIT5;
                    gotILI = true;
                    break;
                case SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE://SBC
                    *illegalLengthIndicator = ptrSenseData[offset + 2] & BIT5;
                    gotILI = true;
                    break;
                case SENSE_DESCRIPTOR_STREAM_COMMANDS://SSC
                    *illegalLengthIndicator = ptrSenseData[offset + 3] & BIT5;
                    gotILI = true;
                    break;
                default: //not a descriptor we care about, so skip it
                    break;
                }
                if (gotILI || descriptorLength == 0)
                {
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
}

void get_Stream_Command_Bits_From_Sense_Data(uint8_t *ptrSenseData, uint32_t senseDataLength, bool *filemark, bool *endOfMedia, bool *illegalLengthIndicator)
{
    if (ptrSenseData && senseDataLength > 0 && illegalLengthIndicator && filemark && endOfMedia)
    {
        *illegalLengthIndicator = false;
        uint8_t format = ptrSenseData[0] & 0x7F; //Stripping the last bit so we just get the format
        uint8_t descriptorLength = 0;//for descriptor format sense data
        uint16_t returnedLength = 8 + ptrSenseData[SCSI_SENSE_ADDT_LEN_INDEX];
        switch (format)
        {
        case SCSI_SENSE_NO_SENSE_DATA:
            break;
        case SCSI_SENSE_CUR_INFO_FIXED:
        case SCSI_SENSE_DEFER_ERR_FIXED:
            *illegalLengthIndicator = ptrSenseData[2] & BIT5;
            *endOfMedia = ptrSenseData[2] & BIT6;
            *filemark = ptrSenseData[2] & BIT7;
            break;
        case SCSI_SENSE_CUR_INFO_DESC:
        case SCSI_SENSE_DEFER_ERR_DESC:
            //loop through the descriptors to see if a sense key specific descriptor was provided
            for (uint32_t offset = SCSI_DESC_FORMAT_DESC_INDEX; offset < SPC3_SENSE_LEN && offset < returnedLength && offset < senseDataLength; offset += descriptorLength + 2)
            {
                bool gotbits = false;
                uint8_t descriptorType = ptrSenseData[offset];
                descriptorLength = ptrSenseData[offset + 1];
                switch (descriptorType)
                {
                case SENSE_DESCRIPTOR_STREAM_COMMANDS://SSC
                    *illegalLengthIndicator = ptrSenseData[offset + 3] & BIT5;
                    *endOfMedia = ptrSenseData[offset + 3] & BIT6;
                    *filemark = ptrSenseData[offset + 3] & BIT7;
                    gotbits = true;
                    break;
                default: //not a descriptor we care about, so skip it
                    break;
                }
                if (gotbits || descriptorLength == 0)
                {
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
}

void get_Command_Specific_Information_From_Sense_Data(uint8_t *ptrSenseData, uint32_t senseDataLength, uint64_t *commandSpecificInformation)
{
    if (ptrSenseData && senseDataLength > 0 && commandSpecificInformation)
    {
        *commandSpecificInformation = 0;
        uint8_t format = ptrSenseData[0] & 0x7F; //Stripping the last bit so we just get the format
        uint8_t descriptorLength = 0;//for descriptor format sense data
        uint16_t returnedLength = 8 + ptrSenseData[SCSI_SENSE_ADDT_LEN_INDEX];
        switch (format)
        {
        case SCSI_SENSE_NO_SENSE_DATA:
            break;
        case SCSI_SENSE_CUR_INFO_FIXED:
        case SCSI_SENSE_DEFER_ERR_FIXED:
            if (returnedLength >= 12)
            {
                *commandSpecificInformation = M_BytesTo4ByteValue(ptrSenseData[8], ptrSenseData[9], ptrSenseData[10], ptrSenseData[11]);
            }
            break;
        case SCSI_SENSE_CUR_INFO_DESC:
        case SCSI_SENSE_DEFER_ERR_DESC:
            //loop through the descriptors to see if a sense key specific descriptor was provided
            for (uint32_t offset = SCSI_DESC_FORMAT_DESC_INDEX; offset < SPC3_SENSE_LEN && offset < returnedLength && offset < senseDataLength; offset += descriptorLength + 2)
            {
                bool gotCommandInformation = false;
                uint8_t descriptorType = ptrSenseData[offset];
                descriptorLength = ptrSenseData[offset + 1];
                switch (descriptorType)
                {
                case SENSE_DESCRIPTOR_COMMAND_SPECIFIC_INFORMATION:
                    *commandSpecificInformation = M_BytesTo8ByteValue(ptrSenseData[offset + 4], ptrSenseData[offset + 5], ptrSenseData[offset + 6], ptrSenseData[offset + 7], ptrSenseData[offset + 8], ptrSenseData[offset + 9], ptrSenseData[offset + 10], ptrSenseData[offset + 11]);
                    gotCommandInformation = true;
                    break;
                case SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE:
                    *commandSpecificInformation = M_BytesTo8ByteValue(ptrSenseData[offset + 16], ptrSenseData[offset + 17], ptrSenseData[offset + 18], ptrSenseData[offset + 19], ptrSenseData[offset + 20], ptrSenseData[offset + 21], ptrSenseData[offset + 22], ptrSenseData[offset + 23]);
                    gotCommandInformation = true;
                    break;
                default: //not a descriptor we care about, so skip it
                    break;
                }
                if (gotCommandInformation || descriptorLength == 0)
                {
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
}

void get_Sense_Key_Specific_Information(uint8_t *ptrSenseData, uint32_t senseDataLength, ptrSenseKeySpecific sksp)
{
    if (ptrSenseData && sksp && senseDataLength > 0)
    {
        uint8_t senseKey = 0;
        uint8_t format = ptrSenseData[0] & 0x7F; //Stripping the last bit so we just get the format
        uint16_t returnedLength = 8;//assume length returned is at least 8 bytes
        bool sksv = false;
        uint8_t senseKeySpecificOffset = 0;
        uint8_t descriptorLength = 0;//for descriptor format sense data
        switch (format)
        {
        case SCSI_SENSE_NO_SENSE_DATA:
            break;
        case SCSI_SENSE_CUR_INFO_FIXED:
        case SCSI_SENSE_DEFER_ERR_FIXED:
            senseKey = M_Nibble0(ptrSenseData[2]);
            returnedLength += ptrSenseData[SCSI_SENSE_ADDT_LEN_INDEX];
            senseKeySpecificOffset = 15;
            sksv = ptrSenseData[senseKeySpecificOffset] & BIT7;
            break;
        case SCSI_SENSE_CUR_INFO_DESC:
        case SCSI_SENSE_DEFER_ERR_DESC:
            returnedLength += ptrSenseData[SCSI_SENSE_ADDT_LEN_INDEX];
            senseKey = M_Nibble0(ptrSenseData[2]);
            //loop through the descriptors to see if a sense key specific descriptor was provided
            for (uint32_t offset = SCSI_DESC_FORMAT_DESC_INDEX; offset < SPC3_SENSE_LEN && offset < returnedLength && offset < senseDataLength; offset += descriptorLength + 2)
            {
                bool senseKeySpecificFound = false;
                uint8_t descriptorType = ptrSenseData[offset];
                descriptorLength = ptrSenseData[offset + 1];
                switch (descriptorType)
                {
                case SENSE_DESCRIPTOR_SENSE_KEY_SPECIFIC:
                    senseKeySpecificOffset = C_CAST(uint8_t, offset + 4);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    senseKeySpecificFound = true;
                    break;
                case SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE:
                    senseKeySpecificOffset = C_CAST(uint8_t, offset + 4);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    senseKeySpecificFound = true;
                    break;
                default: //not a descriptor we care about, so skip it
                    break;
                }
                if (senseKeySpecificFound || descriptorLength == 0)
                {
                    break;
                }
            }
            break;
        default:
            returnedLength = SPC3_SENSE_LEN;
            break;
        }
        if (senseKeySpecificOffset > 0U && sksv && returnedLength >= (senseKeySpecificOffset + 2U) && senseDataLength >= (senseKeySpecificOffset + 2U))
        {
            sksp->senseKeySpecificValid = sksv;
            //Need at least 17 bytes to read this field
            switch (senseKey)
            {
            case SENSE_KEY_NO_ERROR:
            case SENSE_KEY_NOT_READY:
                sksp->type = SENSE_KEY_SPECIFIC_PROGRESS_INDICATION;
                sksp->progress.progressIndication = M_BytesTo2ByteValue(ptrSenseData[senseKeySpecificOffset + 1], ptrSenseData[senseKeySpecificOffset + 2]);
                break;
            case SENSE_KEY_ILLEGAL_REQUEST:
                sksp->type = SENSE_KEY_SPECIFIC_FIELD_POINTER;
                sksp->field.cdbOrData = ptrSenseData[senseKeySpecificOffset] & BIT6;
                sksp->field.bitPointerValid = ptrSenseData[senseKeySpecificOffset] & BIT3;
                sksp->field.bitPointer = M_GETBITRANGE(ptrSenseData[senseKeySpecificOffset], 2, 0);
                sksp->field.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[senseKeySpecificOffset + 1], ptrSenseData[senseKeySpecificOffset + 2]);
                break;
            case SENSE_KEY_HARDWARE_ERROR:
            case SENSE_KEY_RECOVERED_ERROR:
            case SENSE_KEY_MEDIUM_ERROR:
                sksp->type = SENSE_KEY_SPECIFIC_ACTUAL_RETRY_COUNT;
                sksp->retryCount.actualRetryCount = M_BytesTo2ByteValue(ptrSenseData[senseKeySpecificOffset + 1], ptrSenseData[senseKeySpecificOffset + 2]);
                break;
            case SENSE_KEY_COPY_ABORTED:
                sksp->type = SENSE_KEY_SPECIFIC_SEGMENT_POINTER;
                sksp->segment.segmentDescriptor = ptrSenseData[senseKeySpecificOffset] & BIT5;
                sksp->segment.bitPointerValid = ptrSenseData[senseKeySpecificOffset] & BIT3;
                sksp->segment.bitPointer = M_GETBITRANGE(ptrSenseData[senseKeySpecificOffset], 2, 0);
                sksp->segment.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[senseKeySpecificOffset + 1], ptrSenseData[senseKeySpecificOffset + 2]);
                break;
            case SENSE_KEY_UNIT_ATTENTION:
                sksp->type = SENSE_KEY_SPECIFIC_UNIT_ATTENTION_CONDITION_QUEUE_OVERFLOW;
                sksp->unitAttention.overflow = ptrSenseData[senseKeySpecificOffset] & BIT0;
                break;
            default:
                sksp->type = SENSE_KEY_SPECIFIC_UNKNOWN;
                memcpy(&sksp->unknownDataType, &ptrSenseData[senseKeySpecificOffset], 3);
                break;
            }
        }
    }
}

void get_Sense_Data_Fields(uint8_t *ptrSenseData, uint32_t senseDataLength, ptrSenseDataFields senseFields)
{
    if (ptrSenseData && senseDataLength > 0 && senseFields)
    {
        uint8_t format = ptrSenseData[0] & 0x7F; //Stripping the last bit so we just get the format
        uint16_t returnedLength = ptrSenseData[7] + 8;//offset 7 has additional length. +8 is number of bytes to get to a total length
        uint8_t descriptorLength = 0;//for descriptor format sense data
        uint8_t numOfProgressIndications = 0;
        uint8_t numOfForwardedSenseData = 0;
        memset(senseFields, 0, sizeof(senseDataFields));
        switch (format)
        {
        case SCSI_SENSE_NO_SENSE_DATA:
            senseFields->validStructure = true;
            break;
        case SCSI_SENSE_DEFER_ERR_FIXED:
            senseFields->deferredError = true;
            M_FALLTHROUGH
        case SCSI_SENSE_CUR_INFO_FIXED:
            senseFields->fixedFormat = true;
            senseFields->validStructure = true;
            senseFields->valid = ptrSenseData[0] & BIT7;
            senseFields->filemark = ptrSenseData[2] & BIT7;
            senseFields->endOfMedia = ptrSenseData[2] & BIT6;
            senseFields->illegalLengthIndication = ptrSenseData[2] & BIT5;
            senseFields->senseDataOverflow = ptrSenseData[2] & BIT4;
            senseFields->scsiStatusCodes.format = format;
            senseFields->scsiStatusCodes.senseKey = M_Nibble0(ptrSenseData[2]);
            if (senseFields->valid)
            {
                senseFields->fixedInformation = M_BytesTo4ByteValue(ptrSenseData[3], ptrSenseData[4], ptrSenseData[5], ptrSenseData[6]);
            }
            if (returnedLength > 8)
            {
                //todo: better handling of if returned length for each field in here...
                if (returnedLength >= 11)
                {
                    senseFields->fixedCommandSpecificInformation = M_BytesTo4ByteValue(ptrSenseData[8], ptrSenseData[9], ptrSenseData[10], ptrSenseData[11]);
                }
                senseFields->scsiStatusCodes.asc = ptrSenseData[12];
                senseFields->scsiStatusCodes.ascq = ptrSenseData[13];
                senseFields->scsiStatusCodes.fru = ptrSenseData[14];
                if (returnedLength >= 18)
                {
                    //sense key specific information
                    senseFields->senseKeySpecificInformation.senseKeySpecificValid = ptrSenseData[15] & BIT7;
                    if (senseFields->senseKeySpecificInformation.senseKeySpecificValid)
                    {
                        switch (senseFields->scsiStatusCodes.senseKey)
                        {
                        case SENSE_KEY_NO_ERROR:
                        case SENSE_KEY_NOT_READY:
                            senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_PROGRESS_INDICATION;
                            senseFields->senseKeySpecificInformation.progress.progressIndication = M_BytesTo2ByteValue(ptrSenseData[16], ptrSenseData[17]);
                            break;
                        case SENSE_KEY_ILLEGAL_REQUEST:
                            senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_FIELD_POINTER;
                            senseFields->senseKeySpecificInformation.field.cdbOrData = ptrSenseData[15] & BIT6;
                            senseFields->senseKeySpecificInformation.field.bitPointerValid = ptrSenseData[15] & BIT3;
                            senseFields->senseKeySpecificInformation.field.bitPointer = M_GETBITRANGE(ptrSenseData[15], 2, 0);
                            senseFields->senseKeySpecificInformation.field.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[16], ptrSenseData[17]);
                            break;
                        case SENSE_KEY_HARDWARE_ERROR:
                        case SENSE_KEY_RECOVERED_ERROR:
                        case SENSE_KEY_MEDIUM_ERROR:
                            senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_ACTUAL_RETRY_COUNT;
                            senseFields->senseKeySpecificInformation.retryCount.actualRetryCount = M_BytesTo2ByteValue(ptrSenseData[16], ptrSenseData[17]);
                            break;
                        case SENSE_KEY_COPY_ABORTED:
                            senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_SEGMENT_POINTER;
                            senseFields->senseKeySpecificInformation.segment.segmentDescriptor = ptrSenseData[15] & BIT5;
                            senseFields->senseKeySpecificInformation.segment.bitPointerValid = ptrSenseData[15] & BIT3;
                            senseFields->senseKeySpecificInformation.segment.bitPointer = M_GETBITRANGE(ptrSenseData[15], 2, 0);
                            senseFields->senseKeySpecificInformation.segment.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[16], ptrSenseData[17]);
                            break;
                        case SENSE_KEY_UNIT_ATTENTION:
                            senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_UNIT_ATTENTION_CONDITION_QUEUE_OVERFLOW;
                            senseFields->senseKeySpecificInformation.unitAttention.overflow = ptrSenseData[15] & BIT0;
                            break;
                        default:
                            senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_UNKNOWN;
                            memcpy(&senseFields->senseKeySpecificInformation.unknownDataType, &ptrSenseData[15], 3);
                            break;
                        }
                    }
                }
                if (returnedLength > 18)
                {
                    senseFields->additionalDataAvailable = true;
                    senseFields->additionalDataOffset = UINT8_C(18);
                }
            }
            break;
        case SCSI_SENSE_DEFER_ERR_DESC:
            senseFields->deferredError = true;
            M_FALLTHROUGH
        case SCSI_SENSE_CUR_INFO_DESC:
            senseFields->fixedFormat = false;
            senseFields->validStructure = true;
            senseFields->scsiStatusCodes.format = format;
            senseFields->scsiStatusCodes.senseKey = M_Nibble0(ptrSenseData[1]);
            senseFields->scsiStatusCodes.asc = ptrSenseData[2];
            senseFields->scsiStatusCodes.ascq = ptrSenseData[3];
            senseFields->senseDataOverflow = ptrSenseData[4] & BIT7;
            //now we need to loop through the returned descriptors
            for (uint32_t offset = SCSI_DESC_FORMAT_DESC_INDEX; offset < SPC3_SENSE_LEN && offset < returnedLength && offset < senseDataLength; offset += descriptorLength + 2)
            {
                uint8_t descriptorType = ptrSenseData[offset];
                descriptorLength = ptrSenseData[offset + 1];
                switch (descriptorType)
                {
                case SENSE_DESCRIPTOR_INFORMATION:
                    senseFields->valid = ptrSenseData[offset + 2] & BIT7;
                    senseFields->descriptorInformation = M_BytesTo8ByteValue(ptrSenseData[offset + 4], ptrSenseData[offset + 5], ptrSenseData[offset + 6], ptrSenseData[offset + 7], ptrSenseData[offset + 8], ptrSenseData[offset + 9], ptrSenseData[offset + 10], ptrSenseData[offset + 11]);
                    break;
                case SENSE_DESCRIPTOR_COMMAND_SPECIFIC_INFORMATION:
                    senseFields->descriptorCommandSpecificInformation = M_BytesTo8ByteValue(ptrSenseData[offset + 4], ptrSenseData[offset + 5], ptrSenseData[offset + 6], ptrSenseData[offset + 7], ptrSenseData[offset + 8], ptrSenseData[offset + 9], ptrSenseData[offset + 10], ptrSenseData[offset + 11]);
                    break;
                case SENSE_DESCRIPTOR_SENSE_KEY_SPECIFIC:
                    senseFields->senseKeySpecificInformation.senseKeySpecificValid = ptrSenseData[offset + 4] & BIT7;
                    //Need at least 17 bytes to read this field
                    switch (senseFields->scsiStatusCodes.senseKey)
                    {
                    case SENSE_KEY_NO_ERROR:
                    case SENSE_KEY_NOT_READY:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_PROGRESS_INDICATION;
                        senseFields->senseKeySpecificInformation.progress.progressIndication = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_ILLEGAL_REQUEST:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_FIELD_POINTER;
                        senseFields->senseKeySpecificInformation.field.cdbOrData = ptrSenseData[offset + 4] & BIT6;
                        senseFields->senseKeySpecificInformation.field.bitPointerValid = ptrSenseData[offset + 4] & BIT3;
                        senseFields->senseKeySpecificInformation.field.bitPointer = M_GETBITRANGE(ptrSenseData[offset + 4], 2, 0);
                        senseFields->senseKeySpecificInformation.field.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_HARDWARE_ERROR:
                    case SENSE_KEY_RECOVERED_ERROR:
                    case SENSE_KEY_MEDIUM_ERROR:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_ACTUAL_RETRY_COUNT;
                        senseFields->senseKeySpecificInformation.retryCount.actualRetryCount = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_COPY_ABORTED:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_SEGMENT_POINTER;
                        senseFields->senseKeySpecificInformation.segment.segmentDescriptor = ptrSenseData[offset + 4] & BIT5;
                        senseFields->senseKeySpecificInformation.segment.bitPointerValid = ptrSenseData[offset + 4] & BIT3;
                        senseFields->senseKeySpecificInformation.segment.bitPointer = M_GETBITRANGE(ptrSenseData[offset + 4], 2, 0);
                        senseFields->senseKeySpecificInformation.segment.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_UNIT_ATTENTION:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_UNIT_ATTENTION_CONDITION_QUEUE_OVERFLOW;
                        senseFields->senseKeySpecificInformation.unitAttention.overflow = ptrSenseData[offset + 4] & BIT0;
                        break;
                    default:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_UNKNOWN;
                        memcpy(&senseFields->senseKeySpecificInformation.unknownDataType, &ptrSenseData[offset + 4], 3);
                        break;
                    }
                    break;
                case SENSE_DESCRIPTOR_FIELD_REPLACEABLE_UNIT:
                    senseFields->scsiStatusCodes.fru = ptrSenseData[offset + 3];
                    break;
                case SENSE_DESCRIPTOR_STREAM_COMMANDS:
                    senseFields->filemark = ptrSenseData[offset + 3] & BIT7;
                    senseFields->endOfMedia = ptrSenseData[offset + 3] & BIT6;
                    senseFields->illegalLengthIndication = ptrSenseData[offset + 3] & BIT5;
                    break;
                case SENSE_DESCRIPTOR_BLOCK_COMMANDS:
                    senseFields->illegalLengthIndication = ptrSenseData[offset + 3] & BIT5;
                    break;
                case SENSE_DESCRIPTOR_OSD_OBJECT_IDENTIFICATION:
                    senseFields->osdObjectIdentificationDescriptorOffset = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    break;
                case SENSE_DESCRIPTOR_OSD_RESPONSE_INTEGRITY_CHECK_VALUE:
                    senseFields->osdResponseIntegrityCheckValueDescriptorOffset = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    break;
                case SENSE_DESCRIPTOR_OSD_ATTRIBUTE_IDENTIFICATION:
                    senseFields->osdAttributeIdentificationDescriptorOffset = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    break;
                case SENSE_DESCRIPTOR_ATA_STATUS_RETURN:
                    senseFields->ataStatusReturnDescriptor.valid = true;
                    senseFields->ataStatusReturnDescriptor.extend = ptrSenseData[offset + 2] & BIT0;
                    senseFields->ataStatusReturnDescriptor.error = ptrSenseData[offset + 3];
                    senseFields->ataStatusReturnDescriptor.sectorCountExt = ptrSenseData[offset + 4];
                    senseFields->ataStatusReturnDescriptor.sectorCount = ptrSenseData[offset + 5];
                    senseFields->ataStatusReturnDescriptor.lbaLowExt = ptrSenseData[offset + 6];
                    senseFields->ataStatusReturnDescriptor.lbaLow = ptrSenseData[offset + 7];
                    senseFields->ataStatusReturnDescriptor.lbaMidExt = ptrSenseData[offset + 8];
                    senseFields->ataStatusReturnDescriptor.lbaMid = ptrSenseData[offset + 9];
                    senseFields->ataStatusReturnDescriptor.lbaHiExt = ptrSenseData[offset + 10];
                    senseFields->ataStatusReturnDescriptor.lbaHi = ptrSenseData[offset + 11];
                    senseFields->ataStatusReturnDescriptor.device = ptrSenseData[offset + 12];
                    senseFields->ataStatusReturnDescriptor.status = ptrSenseData[offset + 13];
                    break;
                case SENSE_DESCRIPTOR_ANOTHER_PROGRESS_INDICATION:
                    if (numOfProgressIndications < MAX_PROGRESS_INDICATION_DESCRIPTORS)
                    {
                        senseFields->anotherProgressIndicationDescriptorOffset[numOfProgressIndications] = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.;
                        ++numOfProgressIndications;
                    }
                    break;
                case SENSE_DESCRIPTOR_USER_DATA_SEGMENT_REFERRAL:
                    senseFields->userDataSegmentReferralDescriptorOffset = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    break;
                case SENSE_DESCRIPTOR_FORWAREDED_SENSE_DATA:
                    if (numOfForwardedSenseData < MAX_FORWARDED_SENSE_DATA_DESCRIPTORS)
                    {
                        senseFields->forwardedSenseDataDescriptorOffset[numOfForwardedSenseData] = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.;
                        ++numOfForwardedSenseData;
                    }
                    break;
                case SENSE_DESCRIPTOR_DIRECT_ACCESS_BLOCK_DEVICE:
                    senseFields->valid = ptrSenseData[offset + 2] & BIT7;
                    senseFields->illegalLengthIndication = ptrSenseData[offset + 2] & BIT5;
                    senseFields->senseKeySpecificInformation.senseKeySpecificValid = ptrSenseData[offset + 4] & BIT7;
                    //Need at least 17 bytes to read this field
                    switch (senseFields->scsiStatusCodes.senseKey)
                    {
                    case SENSE_KEY_NO_ERROR:
                    case SENSE_KEY_NOT_READY:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_PROGRESS_INDICATION;
                        senseFields->senseKeySpecificInformation.progress.progressIndication = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_ILLEGAL_REQUEST:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_FIELD_POINTER;
                        senseFields->senseKeySpecificInformation.field.cdbOrData = ptrSenseData[offset + 4] & BIT6;
                        senseFields->senseKeySpecificInformation.field.bitPointerValid = ptrSenseData[offset + 4] & BIT3;
                        senseFields->senseKeySpecificInformation.field.bitPointer = M_GETBITRANGE(ptrSenseData[offset + 4], 2, 0);
                        senseFields->senseKeySpecificInformation.field.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_HARDWARE_ERROR:
                    case SENSE_KEY_RECOVERED_ERROR:
                    case SENSE_KEY_MEDIUM_ERROR:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_ACTUAL_RETRY_COUNT;
                        senseFields->senseKeySpecificInformation.retryCount.actualRetryCount = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_COPY_ABORTED:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_SEGMENT_POINTER;
                        senseFields->senseKeySpecificInformation.segment.segmentDescriptor = ptrSenseData[offset + 4] & BIT5;
                        senseFields->senseKeySpecificInformation.segment.bitPointerValid = ptrSenseData[offset + 4] & BIT3;
                        senseFields->senseKeySpecificInformation.segment.bitPointer = M_GETBITRANGE(ptrSenseData[offset + 4], 2, 0);
                        senseFields->senseKeySpecificInformation.segment.fieldPointer = M_BytesTo2ByteValue(ptrSenseData[offset + 5], ptrSenseData[offset + 6]);
                        break;
                    case SENSE_KEY_UNIT_ATTENTION:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_UNIT_ATTENTION_CONDITION_QUEUE_OVERFLOW;
                        senseFields->senseKeySpecificInformation.unitAttention.overflow = ptrSenseData[offset + 4] & BIT0;
                        break;
                    default:
                        senseFields->senseKeySpecificInformation.type = SENSE_KEY_SPECIFIC_UNKNOWN;
                        memcpy(&senseFields->senseKeySpecificInformation.unknownDataType, &ptrSenseData[offset + 4], 3);
                        break;
                    }
                    senseFields->scsiStatusCodes.fru = ptrSenseData[offset + 7];
                    senseFields->descriptorInformation = M_BytesTo8ByteValue(ptrSenseData[offset + 8], ptrSenseData[offset + 9], ptrSenseData[offset + 10], ptrSenseData[offset + 11], ptrSenseData[offset + 12], ptrSenseData[offset + 13], ptrSenseData[offset + 14], ptrSenseData[offset + 15]);
                    senseFields->descriptorCommandSpecificInformation = M_BytesTo8ByteValue(ptrSenseData[offset + 16], ptrSenseData[offset + 17], ptrSenseData[offset + 18], ptrSenseData[offset + 19], ptrSenseData[offset + 20], ptrSenseData[offset + 21], ptrSenseData[offset + 22], ptrSenseData[offset + 23]);
                    break;
                case SENSE_DESCRIPTOR_DEVICE_DESIGNATION:
                    senseFields->deviceDesignationDescriptorOffset = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    break;
                case SENSE_DESCRIPTOR_MICROCODE_ACTIVATION:
                    senseFields->microCodeActivation.valid = true;
                    senseFields->microCodeActivation.microcodeActivationTimeSeconds = M_BytesTo2ByteValue(ptrSenseData[offset + 6], ptrSenseData[offset + 7]);
                    break;
                default: //not a known descriptor
                    if (!senseFields->additionalDataAvailable)
                    {
                        senseFields->additionalDataOffset = C_CAST(uint8_t, offset);//This shouldn't be a problem as offset should never be larger than 252 in the first place.
                    }
                    senseFields->additionalDataAvailable = true;
                    break;
                }
                if (descriptorLength == 0)
                {
                    break;
                }
            }
            break;
        default:
            //unknown sense data format! Can't do anything
            break;
        }
    }
    return;
}

void print_Sense_Fields(ptrSenseDataFields senseFields)
{
    if (senseFields && senseFields->validStructure)
    {
        //This function assumes that the "check_Sense_Key_ASC_ASCQ_FRU" function was called before hand to print out its fields
        if (senseFields->deferredError)
        {
            printf("Deferred error found.\n");
        }
        if (senseFields->senseDataOverflow)
        {
            printf("Sense Data Overflow detected! Request sense command is recommended to retrieve full sense data!\n");
        }
        if (senseFields->filemark)
        {
            printf("Filemark detected\n");
        }
        if (senseFields->endOfMedia)
        {
            printf("End of media detected\n");
        }
        if (senseFields->illegalLengthIndication)
        {
            printf("Illegal Length detected\n");
        }
        printf("Information");
        if (senseFields->valid)
        {
            printf(" (Valid): ");
        }
        else
        {
            printf(": ");
        }
        if (senseFields->fixedFormat)
        {
            printf("%08" PRIX32 "h\n", senseFields->fixedInformation);
        }
        else
        {
            printf("%016" PRIX64 "h\n", senseFields->descriptorInformation);
        }
        printf("Command Specific Information: ");
        if (senseFields->fixedFormat)
        {
            printf("%08" PRIX32 "h\n", senseFields->fixedCommandSpecificInformation);
        }
        else
        {
            printf("%016" PRIX64 "h\n", senseFields->descriptorCommandSpecificInformation);
        }
        if (senseFields->senseKeySpecificInformation.senseKeySpecificValid)
        {
            printf("Sense Key Specific Information:\n\t");
            switch (senseFields->senseKeySpecificInformation.type)
            {
            case SENSE_KEY_SPECIFIC_FIELD_POINTER:
                if (senseFields->senseKeySpecificInformation.field.cdbOrData)
                {
                    if (senseFields->senseKeySpecificInformation.field.bitPointerValid)
                    {
                        printf("Invalid field in CDB byte %" PRIu16 " bit %" PRIu8"\n", senseFields->senseKeySpecificInformation.field.fieldPointer, senseFields->senseKeySpecificInformation.field.bitPointer);
                    }
                    else
                    {
                        printf("Invalid field in CDB byte %" PRIu16 "\n", senseFields->senseKeySpecificInformation.field.fieldPointer);
                    }
                }
                else
                {
                    if (senseFields->senseKeySpecificInformation.field.bitPointerValid)
                    {
                        printf("Invalid field in Parameter byte %" PRIu16 " bit %" PRIu8"\n", senseFields->senseKeySpecificInformation.field.fieldPointer, senseFields->senseKeySpecificInformation.field.bitPointer);
                    }
                    else
                    {
                        printf("Invalid field in Parameter byte %" PRIu16 "\n", senseFields->senseKeySpecificInformation.field.fieldPointer);
                    }
                }
                break;
            case SENSE_KEY_SPECIFIC_ACTUAL_RETRY_COUNT:
                printf("Actual Retry Count: %" PRIu16 "\n", senseFields->senseKeySpecificInformation.retryCount.actualRetryCount);
                break;
            case SENSE_KEY_SPECIFIC_PROGRESS_INDICATION:
                printf("Progress: %0.02f%%\n", C_CAST(double, senseFields->senseKeySpecificInformation.progress.progressIndication) / 65536.0);
                break;
            case SENSE_KEY_SPECIFIC_SEGMENT_POINTER:
                if (senseFields->senseKeySpecificInformation.segment.segmentDescriptor)
                {
                    if (senseFields->senseKeySpecificInformation.field.bitPointerValid)
                    {
                        printf("Invalid field in Segment Descriptor byte %" PRIu16 " bit %" PRIu8"\n", senseFields->senseKeySpecificInformation.field.fieldPointer, senseFields->senseKeySpecificInformation.field.bitPointer);
                    }
                    else
                    {
                        printf("Invalid field in Segment Descriptor byte %" PRIu16 "\n", senseFields->senseKeySpecificInformation.field.fieldPointer);
                    }
                }
                else
                {
                    if (senseFields->senseKeySpecificInformation.field.bitPointerValid)
                    {
                        printf("Invalid field in Parameter byte %" PRIu16 " bit %" PRIu8"\n", senseFields->senseKeySpecificInformation.field.fieldPointer, senseFields->senseKeySpecificInformation.field.bitPointer);
                    }
                    else
                    {
                        printf("Invalid field in Parameter byte %" PRIu16 "\n", senseFields->senseKeySpecificInformation.field.fieldPointer);
                    }
                }
                break;
            case SENSE_KEY_SPECIFIC_UNIT_ATTENTION_CONDITION_QUEUE_OVERFLOW:
                if (senseFields->senseKeySpecificInformation.unitAttention.overflow)
                {
                    printf("Unit attention condition is due to Queue Overflow\n");
                }
                else
                {
                    printf("Unit attention condition is not due to a queue overflow\n");
                }
                break;
            case SENSE_KEY_SPECIFIC_UNKNOWN:
            default:
                printf("Unknown sense key specific data: %" PRIX8 "h %" PRIX8 "h %" PRIX8 "h\n", senseFields->senseKeySpecificInformation.unknownDataType[0], senseFields->senseKeySpecificInformation.unknownDataType[1], senseFields->senseKeySpecificInformation.unknownDataType[2]);
                break;
            }
        }
        if (!senseFields->fixedFormat)
        {
            //look for other descriptor format data that we saved and can easily parse here
            if (senseFields->ataStatusReturnDescriptor.valid)
            {
                printf("ATA Return Status:\n");
                printf("\tExtend: ");
                if (senseFields->ataStatusReturnDescriptor.extend)
                {
                    printf("true\n");
                }
                else
                {
                    printf("false\n");
                }
                printf("\tError:            %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.error);
                printf("\tSector Count Ext: %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.sectorCountExt);
                printf("\tSector Count:     %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.sectorCount);
                printf("\tLBA Low Ext:      %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.lbaLowExt);
                printf("\tLBA Low:          %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.lbaLow);
                printf("\tLBA Mid Ext:      %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.lbaMidExt);
                printf("\tLBA Mid:          %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.lbaMid);
                printf("\tLBA Hi Ext:       %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.lbaHiExt);
                printf("\tLBA Hi:           %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.lbaHi);
                printf("\tDevice:           %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.device);
                printf("\tStatus:           %" PRIX8 "h\n", senseFields->ataStatusReturnDescriptor.status);
            }
            //TODO: go through the other progress indications?
            if (senseFields->microCodeActivation.valid)
            {
                printf("Microcode Activation Time:");
                if (senseFields->microCodeActivation.microcodeActivationTimeSeconds > 0)
                {
                    uint8_t hours = 0, minutes = 0, seconds = 0;
                    convert_Seconds_To_Displayable_Time(senseFields->microCodeActivation.microcodeActivationTimeSeconds, NULL, NULL, &hours, &minutes, &seconds);
                    print_Time_To_Screen(NULL, NULL, &hours, &minutes, &seconds);
                    printf("\n");
                }
                else
                {
                    printf(" Unknown\n");
                }
            }
        }
    }
}

uint16_t get_Returned_Sense_Data_Length(uint8_t *pbuf)
{
    uint16_t length = 8;
    if (!pbuf)
    {
        return 0;
    }
    uint8_t format = pbuf[0] & 0x7F; //Stripping the last bit so we just get the format
        
    switch (format)
    {
    case SCSI_SENSE_NO_SENSE_DATA:
        break;
    case SCSI_SENSE_CUR_INFO_FIXED:
    case SCSI_SENSE_DEFER_ERR_FIXED:
    case SCSI_SENSE_CUR_INFO_DESC:
    case SCSI_SENSE_DEFER_ERR_DESC:
        length += pbuf[7];
        break;
    default:
        length = SPC3_SENSE_LEN;
        break;
    }
    return length;
}

// \fn copy_Inquiry_Data(unsigned char * pbuf, driveInfo * info)
// \brief copy in the necessary data to our struct from INQ data.
void copy_Inquiry_Data( uint8_t *pbuf, driveInfo *info )
{
    // \todo: Create a macro to get various stuff out of the inq buffer
    memcpy(info->T10_vendor_ident, &pbuf[8], INQ_DATA_T10_VENDOR_ID_LEN);
    info->T10_vendor_ident[INQ_DATA_T10_VENDOR_ID_LEN] = '\0';
    memcpy(info->product_identification, &pbuf[16], INQ_DATA_PRODUCT_ID_LEN);
    info->product_identification[INQ_DATA_PRODUCT_ID_LEN] = '\0';
    memcpy(info->product_revision, &pbuf[32], INQ_DATA_PRODUCT_REV_LEN);
    info->product_revision[INQ_DATA_PRODUCT_REV_LEN] = '\0';
    remove_Leading_And_Trailing_Whitespace(info->product_identification);
    remove_Leading_And_Trailing_Whitespace(info->product_revision);
    remove_Leading_And_Trailing_Whitespace(info->T10_vendor_ident);
}

// \brief copy the serial number off of 0x80 VPD page data.
void copy_Serial_Number( uint8_t *pbuf, char *serialNumber )
{
    uint16_t snLen = M_BytesTo2ByteValue(pbuf[2], pbuf[3]);
    memcpy(serialNumber, &pbuf[4], M_Min(snLen,SERIAL_NUM_LEN));
    serialNumber[M_Min(snLen,SERIAL_NUM_LEN)]='\0';
    remove_Leading_Whitespace(serialNumber);
    remove_Trailing_Whitespace(serialNumber);
}

void copy_Read_Capacity_Info(uint32_t *logicalBlockSize, uint32_t *physicalBlockSize, uint64_t *maxLBA, uint16_t *sectorAlignment, uint8_t *ptrBuf, bool readCap16)
{
    if (readCap16)
    {
        uint8_t sectorSizeExponent = 0;
        //get the max LBA
        *maxLBA = M_BytesTo8ByteValue(ptrBuf[0], ptrBuf[1], ptrBuf[2], ptrBuf[3], ptrBuf[4], ptrBuf[5], ptrBuf[6], ptrBuf[7]);
        //get the logical sector size
        *logicalBlockSize = M_BytesTo4ByteValue(ptrBuf[8], ptrBuf[9], ptrBuf[10], ptrBuf[11]);
        //get the physical sector size
        sectorSizeExponent = ptrBuf[13] & 0x0F;
        *physicalBlockSize = C_CAST(uint32_t, *logicalBlockSize * power_Of_Two(sectorSizeExponent));
        //set the sector alignment info
        *sectorAlignment = M_GETBITRANGE(M_BytesTo2ByteValue(ptrBuf[14], ptrBuf[15]), 13, 0);
    }
    else
    {
        //get the maxLBA
        *maxLBA = M_BytesTo4ByteValue(ptrBuf[0], ptrBuf[1], ptrBuf[2], ptrBuf[3]);
        //get the logical sector size
        *logicalBlockSize = M_BytesTo4ByteValue(ptrBuf[4], ptrBuf[5], ptrBuf[6], ptrBuf[7]);
        *physicalBlockSize = *logicalBlockSize;
        *sectorAlignment = 0;
    }
}

int check_SAT_Compliance_And_Set_Drive_Type( tDevice *device )
{
    int ret = FAILURE;
    bool issueSATIdentify = true;//default to ALWAYS reading this unless something else says not to. - TJE
    if (device->drive_info.interface_type == IDE_INTERFACE || device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        //always do this on IDE_INTERFACE since we know it will work here. Doesn't matter if the VPD page read fails or not
        issueSATIdentify = true;
    }
    if (device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.drive_type == LEGACY_TAPE_DRIVE)
    {
        //DO NOT try a SAT identify on these devices if we already know what they are. These should be treated as SCSI since they are either SCSI or ATA packet devices
        return NOT_SUPPORTED;
    }
    if (!device->drive_info.passThroughHacks.scsiHacks.noVPDPages)//if this is set, then the device is known to not support VPD pages, so just skip to the SAT identify
    {
        uint8_t *ataInformation = C_CAST(uint8_t *, calloc_aligned(VPD_ATA_INFORMATION_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (!ataInformation)
        {
            perror("Error allocating memory to read the ATA Information VPD page");
            return MEMORY_FAILURE;
        }
        if (SUCCESS == scsi_Inquiry(device, ataInformation, VPD_ATA_INFORMATION_LEN, ATA_INFORMATION, true, false))
        {
            if (ataInformation[1] == ATA_INFORMATION)
            {
                //set some of the bridge info in the device structure
                memcpy(&device->drive_info.bridge_info.t10SATvendorID[0], &ataInformation[8], 8);
                memcpy(&device->drive_info.bridge_info.SATproductID[0], &ataInformation[16], 16);
                memcpy(&device->drive_info.bridge_info.SATfwRev[0], &ataInformation[32], 4);

                if (ataInformation[36] == 0) //checking for PATA drive
                {
                    if (ataInformation[43] & DEVICE_SELECT_BIT)//ATA signature device register is here. Checking for the device select bit being set to know it's device 1 (Not that we really need it)
                    {
                        device->drive_info.ata_Options.isDevice1 = true;
                    }
                }

                if (ataInformation[56] == ATA_IDENTIFY || ataInformation[56] == ATA_READ_LOG_EXT || ataInformation[56] == ATA_READ_LOG_EXT_DMA)//Added read log commands here since they are in SAT4. Only HDD/SSD should use these.
                {
                    issueSATIdentify = true;
                    device->drive_info.media_type = MEDIA_HDD;
                    device->drive_info.drive_type = ATA_DRIVE;
                }
                else if (ataInformation[56] == ATAPI_IDENTIFY)
                {
                    issueSATIdentify = false;//Do not read it since we want to treat ATAPI as SCSI/with SCSI commands (at least for now)-TJE
                    device->drive_info.media_type = MEDIA_OPTICAL;
                    device->drive_info.drive_type = ATAPI_DRIVE;
                }
                else
                {
                    issueSATIdentify = true;
                }
                ret = SUCCESS;
            }
            else
            {
                issueSATIdentify = true;
            }
        }
        else if (device->drive_info.interface_type == MMC_INTERFACE || device->drive_info.interface_type == NVME_INTERFACE || device->drive_info.interface_type == SD_INTERFACE)
        {
            return NOT_SUPPORTED;
        }
        safe_Free_aligned(ataInformation)
    }
    if (issueSATIdentify)
    {
        if (SUCCESS == fill_In_ATA_Drive_Info(device))
        {
            ret = SUCCESS;
        }
        else
        {
            // It's most likely SCSI/non-SAT compliant translator
            device->drive_info.drive_type = SCSI_DRIVE;
            ret = FAILURE;
        }
    }
    return ret;
}

static bool set_Passthrough_Hacks_By_Inquiry_Data(tDevice *device)
{
    bool passthroughTypeSet = false;
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
            device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
        }
        else if (strcmp(vendorID, "SMI") == 0)
        {
            if (strcmp(productID, "USB DISK") == 0)
            {
                passthroughTypeSet = true;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                //this should prevent sending it bad commands!
            }
        }
        else if (strcmp(vendorID, "") == 0 && strcmp(revision, "8.07") == 0)
        {
            passthroughTypeSet = true;
            device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
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
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_NEC;
            }
        }
        else if (strcmp(vendorID, "Seagate") == 0)
        {
            //Current Seagate USBs will report the vendor ID like this, so this will match ALL of them.
            //If we are in this function, then the low-level was unable to get PID/VID, so we need to set some generic hacks to make sure things work, then do device specific things.
            device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
            device->drive_info.passThroughHacks.turfValue = TURF_LIMIT + 1;//Doing this generically here for now to force this!

            //known device specific hacks
            if (strcmp(productID, "BlackArmorDAS25") == 0)
            {
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
            }
            else if (strcmp(productID, "S2 Portable") == 0)
            {
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                //TODO: this device previously had a hack that SMART check isn't supported, so need to migrate that too.
            }
        }
        else if (strcmp(vendorID, "Samsung") == 0)
        {
            device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
            device->drive_info.passThroughHacks.turfValue = TURF_LIMIT + 1;//Doing this generically here for now to force this!
            if (strcmp(productID, "S2 Portable") == 0)
            {
                device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
                //TODO: this device previously had a hack that SMART check isn't supported, so need to migrate that too.
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
        20 20 20 20 20 20 20 20 20 20 20 20 20 20 01 80                .�
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
        //Example 2:
        00 00 00 00 1f 00 00 00 53 65 61 67 61 74 65 20  ........Seagate
        45 78 74 65 72 6e 61 6c 20 44 72 69 76 65 00 00  External Drive..
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
        00 00 00 00 20 20 54 53 31 33 30 32 32 30 41 32  ....  TS130220A2
        20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20
        20 20 20 20 20 20 20 20 20 20 20 20 20 20 10 80                .�
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
        //Example 3:
        00 00 02 01 1f 00 00 00 53 61 6d 73 75 6e 67 20  ........Samsung
        53 32 20 50 6f 72 74 61 62 6c 65 00 08 12 00 00  S2 Portable.....
        00 00 00 00 6a 33 33 39 cd cd cd cd cd cd cd cd  ....j339��������
        cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ����������������
        cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ����������������
        cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ����������������
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
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_CYPRESS;
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
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_CYPRESS;
            }
        }
    }
    return passthroughTypeSet;
}

bool is_Seagate_USB_Vendor_ID(tDevice* device)
{
    if (strcmp(device->drive_info.T10_vendor_ident, "Seagate") == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

// \fn fill_In_Device_Info(device device)
// \brief Sends a set of INQUIRY commands & fills in the device information
// \param device device struture
// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int fill_In_Device_Info(tDevice *device)
{
    int           ret      = FAILURE;
    #ifdef _DEBUG
    printf("%s: -->\n",__FUNCTION__);
    #endif

    bool mediumNotPresent = false;//assume medium is available until we find out otherwise.
    scsiStatus turStatus;
    memset(&turStatus, 0, sizeof(scsiStatus));
    scsi_Test_Unit_Ready(device, &turStatus);
    if (turStatus.senseKey != SENSE_KEY_NO_ERROR)
    {
        if (turStatus.senseKey == SENSE_KEY_NOT_READY)
        {
            if (turStatus.asc == 0x3A)//NOTE: 3A seems to be all the "medium not present" status's, so not currently checking for ascq - TJE
            {
                mediumNotPresent = true;
            }
        }
    }

    uint8_t *inq_buf = C_CAST(uint8_t*, calloc_aligned(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t), device->os_info.minimumAlignment));
    if (!inq_buf)
    {
        perror("Error allocating memory for standard inquiry data (scsi)");
        return MEMORY_FAILURE;
    }
    memset(device->drive_info.serialNumber, 0, sizeof(device->drive_info.serialNumber));
    memset(device->drive_info.T10_vendor_ident, 0, sizeof(device->drive_info.T10_vendor_ident));
    memset(device->drive_info.product_identification, 0, sizeof(device->drive_info.product_identification));
    memset(device->drive_info.product_revision, 0, sizeof(device->drive_info.product_revision));
    //By default, we need to set up some information about drive type based on what is known so far from the OS level code telling us the interface the device is attached on. We can change it later if need be
    //Remember, these are assumptions ONLY based off what interface the OS level code is reporting. It should default to something, then get changed later when we know more about it.
//  switch (device->drive_info.interface_type)
//  {
//  case IDE_INTERFACE:
//      if (device->drive_info.drive_type != ATAPI_DRIVE && device->drive_info.drive_type != LEGACY_TAPE_DRIVE)
//      {
//          device->drive_info.drive_type = ATA_DRIVE;
//          device->drive_info.media_type = MEDIA_HDD;
//      }
//      break;
//  case RAID_INTERFACE:
//      //This has already been set by the RAID level, don't change it.
//      if(device->drive_info.drive_type == UNKNOWN_DRIVE)
//      {
//          device->drive_info.drive_type = RAID_DRIVE;
//      }
//      if(device->drive_info.media_type == MEDIA_UNKNOWN)
//      {
//          device->drive_info.media_type = MEDIA_HDD;
//      }
//      break;
//  case NVME_INTERFACE:
//      device->drive_info.drive_type = NVME_DRIVE;
//      device->drive_info.media_type = MEDIA_NVM;
//      break;
//  case SCSI_INTERFACE:
//  case USB_INTERFACE:
//  case IEEE_1394_INTERFACE:
//      if (device->drive_info.drive_type != ATAPI_DRIVE && device->drive_info.drive_type != LEGACY_TAPE_DRIVE)
//      {
//          device->drive_info.drive_type = SCSI_DRIVE;
//          device->drive_info.media_type = MEDIA_HDD;
//      }
//      break;
//  case MMC_INTERFACE:
//  case SD_INTERFACE:
//      device->drive_info.drive_type = FLASH_DRIVE;
//      device->drive_info.media_type = MEDIA_SSM_FLASH;
//      break;
//  case UNKNOWN_INTERFACE:
//  default:
//      device->drive_info.media_type = MEDIA_UNKNOWN;
//      break;
//  }
    //now start getting data from the device itself
    if (SUCCESS == scsi_Inquiry(device, inq_buf, INQ_RETURN_DATA_LENGTH, 0, false, false))
    {
        bool checkForSAT = true;
        bool readCapacity = true;
        ret = SUCCESS;
        memcpy(device->drive_info.scsiVpdData.inquiryData, inq_buf, 96);//store this in the device structure to make sure it is available elsewhere in the library as well.
        copy_Inquiry_Data(inq_buf, &device->drive_info);

        if (!device->drive_info.passThroughHacks.hacksSetByReportedID)
        {
            //This function will check known inquiry data to set passthrough hacks for devices that are known to report a certain way.
            //TODO: can running this be used to prevent checking for SAT if this is already set? This could help with certain scenarios
            set_Passthrough_Hacks_By_Inquiry_Data(device);
        }

        uint8_t responseFormat = M_GETBITRANGE(inq_buf[3], 3, 0);
        if (responseFormat < 2)
        {
            //Need to check if vendor ID, MN, and FWRev are printable or not
            //vendor ID
            for (uint8_t iter = 0; iter < T10_VENDOR_ID_LEN; ++iter)
            {
                if (!is_ASCII(device->drive_info.T10_vendor_ident[iter]) || !isprint(device->drive_info.T10_vendor_ident[iter]))
                {
                    device->drive_info.T10_vendor_ident[iter] = ' ';
                }
            }
            //product ID
            for (uint8_t iter = 0; iter < MODEL_NUM_LEN && iter < INQ_DATA_PRODUCT_ID_LEN; ++iter)
            {
                if (!is_ASCII(device->drive_info.product_identification[iter]) ||!isprint(device->drive_info.product_identification[iter]))
                {
                    device->drive_info.product_identification[iter] = ' ';
                }
            }
            //FWRev
            for (uint8_t iter = 0; iter < FW_REV_LEN && iter < INQ_DATA_PRODUCT_REV_LEN; ++iter)
            {
                if (!is_ASCII(device->drive_info.product_revision[iter]) ||!isprint(device->drive_info.product_revision[iter]))
                {
                    device->drive_info.product_revision[iter] = ' ';
                }
            }
        }
        uint8_t version = inq_buf[2];
        switch (version) //convert some versions since old standards broke the version number into ANSI vs ECMA vs ISO standard numbers
        {
        case 0:
            version = SCSI_VERSION_NO_STANDARD;
            if (device->drive_info.interface_type != USB_INTERFACE && !device->drive_info.passThroughHacks.hacksSetByReportedID)
            {
                checkForSAT = false; //NOTE: some cheap USB to SATA/PATA adapters will set this version or no version. The only way to work around this, is to make sure the low level for the OS detects it on USB interface and it can be run through the usb_hacks file instead.
            }
            break;
        case 0x81:
            version = SCSI_VERSION_SCSI;//changing to 1 for SCSI
            if (device->drive_info.interface_type != USB_INTERFACE && !device->drive_info.passThroughHacks.hacksSetByReportedID)
            {
                checkForSAT = false;//NOTE: some cheap USB to SATA/PATA adapters will set this version or no version. The only way to work around this, is to make sure the low level for the OS detects it on USB interface and it can be run through the usb_hacks file instead.
            }
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
            if (!device->drive_info.passThroughHacks.hacksSetByReportedID)
            {
                checkForSAT = false;
            }
            break;
        case PERIPHERAL_ENCLOSURE_SERVICES_DEVICE:
        case PERIPHERAL_BRIDGE_CONTROLLER_COMMANDS:
        case PERIPHERAL_OBJECT_BASED_STORAGE_DEVICE:
        case PERIPHERAL_PRINTER_DEVICE:
        case PERIPHERAL_PROCESSOR_DEVICE:
        case PERIPHERAL_SCANNER_DEVICE:
        case PERIPHERAL_MEDIUM_CHANGER_DEVICE:
        case PERIPHERAL_COMMUNICATIONS_DEVICE:
        case PERIPHERAL_ASC_IT8_1:
        case PERIPHERAL_ASC_IT8_2:
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

        bool foundUSBStandardDescriptor = false;
        bool foundSATStandardDescriptor = false;
        bool foundATAStandardDescriptor = false;

        //check version descriptors first (If returned sense data is long enough and reports all of this)
        //This can help improve SAT detection and other passthrough quirks
        for (uint16_t versionIter = 0, offset = 58; versionIter < 7 && offset < (inq_buf[4] + 4); ++versionIter, offset += 2)
        {
            uint16_t versionDescriptor = M_BytesTo2ByteValue(device->drive_info.scsiVpdData.inquiryData[offset + 0], device->drive_info.scsiVpdData.inquiryData[offset + 1]);
            if (!foundUSBStandardDescriptor && (is_Standard_Supported(versionDescriptor, STANDARD_CODE_USB)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_UAS)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_UAS2)))
            {
                foundUSBStandardDescriptor = true;
            }
            else if (!foundSATStandardDescriptor && (is_Standard_Supported(versionDescriptor, STANDARD_CODE_SAT)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_SAT2)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_SAT3)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_SAT4)))
            {
                foundSATStandardDescriptor = true;
            }
            else if (!foundATAStandardDescriptor && (is_Standard_Supported(versionDescriptor, STANDARD_CODE_ATA_ATAPI6)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_ATA_ATAPI7)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_ATA_ATAPI8)
                || is_Standard_Supported(versionDescriptor, STANDARD_CODE_ACSx)))
            {
                foundATAStandardDescriptor = true;
            }
        }

        //special USB detection case. If not already USB interface, do a few more checks to get the interface correct
        if (device->drive_info.interface_type == SCSI_INTERFACE)
        {
            if (foundUSBStandardDescriptor)
            {
                device->drive_info.interface_type = USB_INTERFACE;
            }
            
            //Only rely on this as a last resort. Try using version descriptors when possible
            //NOTE: This is different from SAS where the ID is in all CAPS, which makes this identification possible.
            //TODO: LaCie? Need to make sure this only catches USB and not something else like thunderbolt
            if (device->drive_info.interface_type == SCSI_INTERFACE && is_Seagate_USB_Vendor_ID(device))
            {
                device->drive_info.interface_type = USB_INTERFACE;
            }
        }
        bool hisup = M_ToBool(inq_buf[3] & BIT4);//historical support...this is set to 1 for nvme (SNTL) and not specified in SAT...may be useful later - TJE
        bool rmb = M_ToBool(inq_buf[1] & BIT1);//removable medium. This should be zero for all modern HDDs and any SSD, even over USB, but USB sometimes plays by it's own rules. - TJE
        bool cmdQueue = M_ToBool(inq_buf[7] & BIT1);//set to 1 for nvme, unspecified for SAT
        //check for additional bits to try and filter out when to check for SAT
        if (checkForSAT && !device->drive_info.passThroughHacks.hacksSetByReportedID)
        {
            //check that response format is 2 (or higher). SAT spec says the response format should be set to 2
            //Not checking this on USB since some adapters set this purposely to avoid certain commands, BUT DO support SAT
            if (M_Nibble0(inq_buf[3]) < 2 && device->drive_info.interface_type != USB_INTERFACE)
            {
                checkForSAT = false;
            }
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
            if ((inq_buf[6] & BIT5 || inq_buf[7] & BIT0) && (device->drive_info.interface_type != USB_INTERFACE))//vendor specific bits. Ignore USB Interface
            {
                checkForSAT = false;
            }
            //TODO: add in additional bits to skip SAT check as we find them useful
        }
        //do we want to check the version descriptors here too? There are a lot of those...I have a table that parses them to human readable, but not setting anything yet...may need to use that later

        //As per NVM Express SCSI Translation Reference. 
        //NOTE: Setting this type here allows us to skip sending some extra commands. (e.g. SAT compliant)
        if (memcmp(device->drive_info.T10_vendor_ident, "NVMe", 4) == 0 )
        {
            //DO NOT set the drive type to NVMe here. We need to treat it as a SCSI device since we can only issue SCSI translatable commands!!!
            //device->drive_info.drive_type  = NVME_DRIVE;
            device->drive_info.media_type = MEDIA_NVM;
            checkForSAT = false;
        }
        else if (device->drive_info.passThroughHacks.passthroughType >= NVME_PASSTHROUGH_JMICRON && device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_UNKNOWN)
        {
            device->drive_info.media_type = MEDIA_NVM;
            checkForSAT = false;
        }
        else if (device->drive_info.passThroughHacks.hacksSetByReportedID && device->drive_info.passThroughHacks.passthroughType == PASSTHROUGH_NONE)
        {
            //Disable checking for SAT when the low-level device information says it is not available.
            //This prevents unnecessary discovery and slow-down on devices that are already confirmed to not support SAT or other ATA passthrough
            checkForSAT = false;
        }

        bool knownMemoryStickID = false;
        //Checking the product identification for "Generic-" device to see if they are MMC, SD, etc type devices
        if (strcmp(device->drive_info.T10_vendor_ident, "Generic-") == 0)
        {
            if (strcmp(device->drive_info.product_identification, "MS/MS-PRO") == 0 ||
                strcmp(device->drive_info.product_identification, "MS/MS-Pro") == 0 ||
                strcmp(device->drive_info.product_identification, "xD-Picture") == 0 ||
                strcmp(device->drive_info.product_identification, "SD/MMC") == 0 ||
                strcmp(device->drive_info.product_identification, "SD/MemoryStick") == 0 ||
                strcmp(device->drive_info.product_identification, "SM/xD-Picture") == 0 ||
                strcmp(device->drive_info.product_identification, "Compact Flash") == 0 //TODO: Keep this here? This can be an ATA device, but that may depend on the interface - TJE
                )
            {
                //TODO: We have "FLASH_DRIVE" as a type, but it won't ba handled well in the rest of the library.
                //      Either need to start using it, or make more changes to handle it better -TJE
                //device->drive_info.drive_type = FLASH_DRIVE;
                device->drive_info.media_type = MEDIA_SSM_FLASH;
                if (strcmp(device->drive_info.product_identification, "Compact Flash") != 0 || mediumNotPresent)
                {
                    //Only check for SAT on compact flash since it uses ATA commands. May need another case for CFast as well.
                    checkForSAT = false;
                }
                knownMemoryStickID = true;
            }
        }

        //If this is a suspected NVMe device, specifically ASMedia 236X chip, need to do an inquiry with EXACTLY 38bytes to check for a specific signature
        //This will check for some known outputs to know when to do the additional inquiry command for ASMedia detection. This may not catch everything. - TJE
        if (!knownMemoryStickID && !device->drive_info.passThroughHacks.hacksSetByReportedID  && !(device->drive_info.passThroughHacks.passthroughType >= NVME_PASSTHROUGH_JMICRON && device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_UNKNOWN)
            &&
            (strncmp(device->drive_info.T10_vendor_ident, "ASMT", 4) == 0 || strncmp(device->drive_info.T10_vendor_ident, "ASMedia", 7) == 0 
            || strstr(device->drive_info.product_identification, "ASM236X") || strstr(device->drive_info.product_identification, "NVME")
            || is_Seagate_USB_Vendor_ID(device) || strcmp(device->drive_info.T10_vendor_ident, "LaCie") == 0) //This is a special case to run on Seagate and LaCie USB adapters as they may use the ASmedia NVMe chips
            //TODO: Check when FWRev is set to 2364? At least one device I have does this, but not sure this is a good thing to add in here or not -TJE
            && !hisup && !rmb //hisup shoiuld be 1 and rmb should be zero...on the asmedia chips I have tested, hisup is zero
            && responseFormat >= 2 //filter out any weird old drives with bizarre responses
            && inq_buf[4] == 0x47 //SNTL says 1F, but a couple of adapter I have sets 47h...using this for now to help filter the list
            && cmdQueue //should be set for all NVMe SNTL translators
            )
        {
            //This is likely a ASMedia 236X device. Need to do another inquiry command in order to confirm.
            uint8_t asmtInq[38] = { 0 };
            if (SUCCESS == scsi_Inquiry(device, asmtInq, 38, 0, false, false))
            {
                if (asmtInq[36] == 0x60 && asmtInq[37] == 0x23)//todo: add checking length ahead of this for improved backwards compatibility with SCSI 2 devices.
                {
                    //This is an ASMedia device with the 236X chip which supports USB to NVMe passthrough
                    //will attempt to check for full passthrough support first
                    uint8_t* nvmeIdentify = C_CAST(uint8_t*, calloc_aligned(NVME_IDENTIFY_DATA_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
                    bool fullCmdSupport = false;
                    //setup hacks/flags common for both types of passthrough
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
                    if (nvmeIdentify)
                    {
                        device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_ASMEDIA;
                        //attempt the full passthrough
                        if (SUCCESS == nvme_Identify(device, nvmeIdentify, 0, NVME_IDENTIFY_CTRL))
                        {
                            fullCmdSupport = true;
                        }
                        safe_Free_aligned(nvmeIdentify);
                    }
                    //This code will setup known hacks for these devices since it wasn't already detected by lower layers based on VID/PID reported over the USB interface
                    checkForSAT = false;
                    if (!fullCmdSupport)
                    {
                        //if the full cmd above failed, but we are here, that means it supports the basic passthrough
                        //change to this passthrough and set the limited capabilities flags
                        device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_ASMEDIA_BASIC;
                        device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities = true;
                        device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage = true;
                        device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric = true;
                    }
                }
            }
        }

        //Need to check version descriptors here since they may be useful below, but also because it can be used to help rule-out some USB to NVMe devices.
        bool satVersionDescriptorFound = false;
        if(strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) == 0 || strstr(device->drive_info.product_identification, "NVME") || strstr(device->drive_info.product_identification, "NVMe"))
        {
            //This means we most likely have some sort of NVMe device, so SAT (ATA passthrough) makes no sense to check for.
            checkForSAT = false;
        }
        else
        {
            if (foundSATStandardDescriptor || foundATAStandardDescriptor)
            {
                if (strncmp(device->drive_info.T10_vendor_ident, "NVMe", 4) != 0 || strstr(device->drive_info.product_identification, "NVME") == NULL || strstr(device->drive_info.product_identification, "NVMe") == NULL)
                {
                    satVersionDescriptorFound = true;
                }
                else
                {
                    checkForSAT = false;
                }
            }
        }

        bool checkJMicronNVMe = false;
        if (!device->drive_info.passThroughHacks.hacksSetByReportedID && checkForSAT && !(device->drive_info.passThroughHacks.passthroughType >= NVME_PASSTHROUGH_JMICRON && device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_UNKNOWN)
            && (
                strncmp(device->drive_info.T10_vendor_ident, "JMicron", 4) == 0 || //check anything coming up as Jmicron
                (strncmp(device->drive_info.T10_vendor_ident, "JMicron", 4) == 0 && strncmp(device->drive_info.product_identification, "Tech", 4) == 0 && (strncmp(device->drive_info.product_revision, "0204", 4) == 0 || strncmp(device->drive_info.product_revision, "0205", 4) == 0)) //this is specific to known Jmicron-nvme adapters
                || is_Seagate_USB_Vendor_ID(device) || strcmp(device->drive_info.T10_vendor_ident, "LaCie") == 0) //This is a special case to run on Seagate and LaCie USB adapters as they may use the Jmicron NVMe chips
            && foundSATStandardDescriptor && !foundATAStandardDescriptor //these chips report SAT, but not an ATA standard...might reduce how often this check is performed - TJE
            && hisup && !rmb //hisup shoiuld be 1 and rmb should be zero...this should filter SOME, but not all USB adapters that are actually SATA drives - TJE
            && responseFormat >= 2 //filter out any weird old drives with bizarre responses
            //&& inq_buf[4] == 0x5b //SNTL says 1F, but one adapter I have sets 5B...need to make sure other adapters do the same before we enforce this check - TJE
            && cmdQueue //should be set for all NVMe SNTL translators
            )
        {
            //this is some additional checks for JMicron NVMe passthrough
            checkJMicronNVMe = true;
            //Do not turn off SAT checks because there is not enough information to filter down into a known NVMe vs known SATA device in this "auto-detect" case
            //checking for this will come, most likely, after SAT check.
            //TODO: Further improvement: Detect A1h command supported, but 85h is NOT. A1 is the opcode supported by this device's passthrough and anything supporting 85h will have to be SAT, not this unique NVMe passthrough - TJE
            //      note: there is not currently a good way to track the results from these commands in fill_ATA_Drive_Info...that is something we can work on going forward.
            //      A1 SAT identify should return "Invalid field in CDB" and 85h should return "Invalid operation code". While SOME SAT device may do this too, this will reduce commanmds sent to genuine SAT devices.
        }

        if (M_Word0(device->dFlags) == DO_NOT_WAKE_DRIVE)
        {
#if defined (_DEBUG)
            printf("Quiting device discovery early per DO_NOT_WAKE_DRIVE\n");
#endif
            //We actually need to try issuing an ATA/ATAPI identify to the drive to set the drive type...but I'm going to try and ONLY do it for ATA drives with the if statement below...it should catch almost all cases (which is good enough for now)
            if (checkForSAT && device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_JMICRON && (satVersionDescriptorFound || strncmp(device->drive_info.T10_vendor_ident, "ATA", 3) == 0 || device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE || device->drive_info.interface_type == IDE_INTERFACE)
                &&
                (device->drive_info.drive_type != ATAPI_DRIVE && device->drive_info.drive_type != LEGACY_TAPE_DRIVE)
               )
            {
                ret = fill_In_ATA_Drive_Info(device);
                if (ret != SUCCESS && checkJMicronNVMe)
                {
                    device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
                    ret = fill_In_NVMe_Device_Info(device);
                    if (ret == SUCCESS)
                    {
                        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                        device->drive_info.passThroughHacks.turfValue = 13;
                        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in 
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
                        device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength = UINT16_MAX;
                    }
                    else
                    {
                        device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                        ret = SUCCESS;//do not fail here since this should otherwise be treated as a SCSI drive
                    }
                }
            }
            safe_Free_aligned(inq_buf)
            return ret;
        }

        if (M_Word0(device->dFlags) == FAST_SCAN)
        {
            if ((!device->drive_info.passThroughHacks.scsiHacks.noVPDPages || device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable) && (version >= 2 || device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable))//unit serial number added in SCSI2
            {
                //I'm reading only the unit serial number page here for a quick scan and the device information page for WWN - TJE
                uint8_t unitSerialNumberPageLength = SERIAL_NUM_LEN + 4;//adding 4 bytes extra for the header
                uint8_t *unitSerialNumber = C_CAST(uint8_t*, calloc_aligned(unitSerialNumberPageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                safe_Free_aligned(unitSerialNumber)
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
            if (version >= 3 && !device->drive_info.passThroughHacks.scsiHacks.noVPDPages)//device identification added in SPC
            {
                uint8_t *deviceIdentification = C_CAST(uint8_t*, calloc_aligned(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t), device->os_info.minimumAlignment));
                if (!deviceIdentification)
                {
                    perror("Error allocating memory to read device identification VPD page");
                    safe_Free_aligned(inq_buf)
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
                safe_Free_aligned(deviceIdentification)
            }
            //One last thing...Need to do a SAT scan...
            if (checkForSAT)
            {
                if (SUCCESS != check_SAT_Compliance_And_Set_Drive_Type(device) && checkJMicronNVMe)
                {
                    device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
                    ret = fill_In_NVMe_Device_Info(device);
                    if (ret == SUCCESS)
                    {
                        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                        device->drive_info.passThroughHacks.turfValue = 13;
                        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in 
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
                        device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength = UINT16_MAX;
                    }
                    else
                    {
                        device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                        ret = SUCCESS;//do not fail here since this should otherwise be treated as a SCSI drive
                    }
                }
            }
            safe_Free_aligned(inq_buf)
            return ret;
        }

        if (device->drive_info.interface_type != USB_INTERFACE && device->drive_info.interface_type != IEEE_1394_INTERFACE)
        {
            //Issue report LUNs to figure out how many logical units are present.
            uint8_t reportLuns[8] = { 0 };//only really need first 4 bytes, but this will make sure we get the length, hopefully without error
            if (SUCCESS == scsi_Report_Luns(device, 0, 8, reportLuns))
            {
                uint32_t lunListLength = M_BytesTo4ByteValue(reportLuns[0], reportLuns[1], reportLuns[2], reportLuns[3]);
                device->drive_info.numberOfLUs = lunListLength / 8;//each LUN is 8 bytes long
            }
            else
            {
                //some other crappy device that doesn't respond properly
                device->drive_info.numberOfLUs = 1;
            }
        }
        else
        {
            device->drive_info.numberOfLUs = 1;
        }

        bool satVPDPageRead = false;
        bool satComplianceChecked = false;
        if ((!device->drive_info.passThroughHacks.scsiHacks.noVPDPages || device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable) && (version >= 2 || device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)) //SCSI 2 added VPD pages
        {
            //from here on we need to check if a VPD page is supported and read it if there is anything in it that we care about to store info in the device struct
            memset(inq_buf, 0, INQ_RETURN_DATA_LENGTH);
            bool dummyUpVPDSupport = false;
            if (!device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable && SUCCESS != scsi_Inquiry(device, inq_buf, INQ_RETURN_DATA_LENGTH, SUPPORTED_VPD_PAGES, true, false))
            {
                //for whatever reason, this device didn't return support for the list of supported pages, so set a flag telling us to dummy up a list so that we can still attempt to issue commands to pages we do need to try and get (this is a workaround for some really stupid USB bridges)
                dummyUpVPDSupport = true;
            }
            else if (device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)
            {
                dummyUpVPDSupport = true;
            }
            else if (inq_buf[1] != SUPPORTED_VPD_PAGES)
            {
                //did not get the list of supported pages! Checking this since occasionally we get back garbage
                memset(inq_buf, 0, INQ_RETURN_DATA_LENGTH);
                dummyUpVPDSupport = true;
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
                if (device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)
                {
                    //If this is set, then this means that the device ONLY supports the unit SN page, but not other. Only add unit serial number to this dummied data.
                    //This is a workaround for some USB devices.
                    //TODO: if these devices support a limited number of other pages, we will need to change this hack a little bit to work with them better.
                    //      Some of the devices only support the unit serial number page and the device identification page.
                    inq_buf[offset] = UNIT_SERIAL_NUMBER;
                    ++offset;
                }
                else
                {
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

                }
                //set page length (n-3)
                inq_buf[2] = M_Byte1(offset - 4);//msb
                inq_buf[3] = M_Byte0(offset - 4);//lsb
            }
            //first, get the length of the supported pages
            uint16_t supportedVPDPagesLength = M_BytesTo2ByteValue(inq_buf[2], inq_buf[3]);
            uint8_t *supportedVPDPages = C_CAST(uint8_t*, calloc(supportedVPDPagesLength, sizeof(uint8_t)));
            if (!supportedVPDPages)
            {
                perror("Error allocating memory for supported VPD pages!\n");
                safe_Free_aligned(inq_buf)
                return MEMORY_FAILURE;
            }
            memcpy(supportedVPDPages, &inq_buf[4], supportedVPDPagesLength);
            //now loop through and read pages as we need to, only reading the pages that we care about
            uint16_t vpdIter = 0;
            for (vpdIter = 0; vpdIter < supportedVPDPagesLength && vpdIter < INQ_RETURN_DATA_LENGTH; vpdIter++)
            {
                switch (supportedVPDPages[vpdIter])
                {
                case UNIT_SERIAL_NUMBER://Device serial number (only grab 20 characters worth since that's what we need for the device struct)
                {
                    uint8_t unitSerialNumberPageLength = SERIAL_NUM_LEN + 4;//adding 4 bytes extra for the header
                    uint8_t *unitSerialNumber = C_CAST(uint8_t*, calloc_aligned(unitSerialNumberPageLength, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                                for (size_t iter = 0; iter < SERIAL_NUM_LEN && iter < strlen(device->drive_info.serialNumber); ++iter)
                                {
                                    if (!isprint(device->drive_info.serialNumber[iter]))
                                    {
                                        device->drive_info.serialNumber[iter] = ' ';
                                    }
                                }
                            }
                        }
                    }
                    safe_Free_aligned(unitSerialNumber)
                    break;
                }
                case DEVICE_IDENTIFICATION://World wide name
                {
                    uint8_t *deviceIdentification = C_CAST(uint8_t*, calloc_aligned(INQ_RETURN_DATA_LENGTH, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                    safe_Free_aligned(deviceIdentification)
                    break;
                }
                case ATA_INFORMATION: //use this to determine if it's SAT compliant
                {
                    if(device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_JMICRON)
                    {
                        //printf("VPD pages, check SAT info\n");
                        //do not check the checkForSAT bool here. If we get here, then the device most likely reported support for it so it should be readable.
                        if (SUCCESS == check_SAT_Compliance_And_Set_Drive_Type(device))
                        {
                            satVPDPageRead = true;
                        }
                        else
                        {
                            //send test unit ready to get the device responding again (For better performance on some USB devices that don't support this page)
                            scsi_Test_Unit_Ready(device, NULL);
                            //TODO: Check jmicron here???
                            if (checkJMicronNVMe)
                            {
                                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
                                ret = fill_In_NVMe_Device_Info(device);
                                if (ret == SUCCESS)
                                {
                                    device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                                    device->drive_info.passThroughHacks.turfValue = 13;
                                    device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in 
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
                                    device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength = UINT16_MAX;
                                }
                                else
                                {
                                    device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                                    ret = SUCCESS;//do not fail here since this should otherwise be treated as a SCSI drive
                                }
                            }
                        }
                        satComplianceChecked = true;
                    }
                    break;
                }
                case BLOCK_DEVICE_CHARACTERISTICS: //use this to determine if it's SSD or HDD and whether it's a HDD or not
                {
                    uint8_t *blockDeviceCharacteristics = C_CAST(uint8_t*, calloc_aligned(VPD_BLOCK_DEVICE_CHARACTERISTICS_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
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
                            if (device->drive_info.media_type != MEDIA_SSM_FLASH)//if this is already set, we don't want to change it because this is a helpful filter for some card-reader type devices.
                            {
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
                                    if (checkJMicronNVMe)
                                    {
                                        //The logic here is that there are no NVMe HDDs that will use this bridge, so do not do a SAT check, and instead check only for JMicron NVMe adapter - TJE
                                        checkForSAT = false;
                                    }
                                }
                                else
                                {
                                    if (!satVPDPageRead)
                                    {
                                        device->drive_info.media_type = MEDIA_UNKNOWN;
                                    }
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
                    safe_Free_aligned(blockDeviceCharacteristics)
                    break;
                }
                default:
                    //do nothing, we don't care about reading this page (at least not right now)
                    break;
                }
            }
            safe_Free(supportedVPDPages)
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

        if (readCapacity && !mediumNotPresent)
        {
            //if inquiry says SPC or lower (3), then only do read capacity 10
            //Anything else can have read capacity 16 command available

            //send a read capacity command to get the device's logical block size...read capacity 10 should be enough for this
            uint8_t *readCapBuf = C_CAST(uint8_t*, calloc_aligned(READ_CAPACITY_10_LEN, sizeof(uint8_t), device->os_info.minimumAlignment));
            if (!readCapBuf)
            {
                safe_Free_aligned(inq_buf)
                return MEMORY_FAILURE;
            }
            if (SUCCESS == scsi_Read_Capacity_10(device, readCapBuf, READ_CAPACITY_10_LEN))
            {
                copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &device->drive_info.deviceMaxLba, &device->drive_info.sectorAlignment, readCapBuf, false);
                if (version > 3)//SPC2 and higher can reference SBC2 and higher which introduced read capacity 16
                {
                    //try a read capacity 16 anyways and see if the data from that was valid or not since that will give us a physical sector size whereas readcap10 data will not
                    uint8_t* temp = C_CAST(uint8_t*, realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN, device->os_info.minimumAlignment));
                    if (!temp)
                    {
                        safe_Free_aligned(readCapBuf)
                        safe_Free_aligned(inq_buf)
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
                            checkForSAT = false;
                        }
                    }
                }
            }
            else
            {
                //try read capacity 16, if that fails we are done trying
                uint8_t* temp = C_CAST(uint8_t*, realloc_aligned(readCapBuf, READ_CAPACITY_10_LEN, READ_CAPACITY_16_LEN, device->os_info.minimumAlignment));
                if (temp == NULL)
                {
                    safe_Free_aligned(readCapBuf)
                    safe_Free_aligned(inq_buf)
                    return MEMORY_FAILURE;
                }
                readCapBuf = temp;
                memset(readCapBuf, 0, READ_CAPACITY_16_LEN);
                if (SUCCESS == scsi_Read_Capacity_16(device, readCapBuf, READ_CAPACITY_16_LEN))
                {
                    copy_Read_Capacity_Info(&device->drive_info.deviceBlockSize, &device->drive_info.devicePhyBlockSize, &device->drive_info.deviceMaxLba, &device->drive_info.sectorAlignment, readCapBuf, true);
                    device->drive_info.currentProtectionType = 0;
                    device->drive_info.piExponent = M_GETBITRANGE(readCapBuf[13], 7, 4);
                    if (readCapBuf[12] & BIT0)
                    {
                        device->drive_info.currentProtectionType = M_GETBITRANGE(readCapBuf[12], 3, 1) + 1;
                        checkForSAT = false;
                    }
                }
            }
            safe_Free_aligned(readCapBuf)
            if (device->drive_info.devicePhyBlockSize == 0)
            {
                //If we did not get a physical blocksize, we need to set it to the blocksize (logical).
                //This will help with old devices or those that don't support the read capacity 16 command or return other weird invalid data.
                device->drive_info.devicePhyBlockSize = device->drive_info.deviceBlockSize;
            }
        }

        //NOTE: You would think that checking if physical and logical block sizes don't match you can filter NVMe (they are supposed to be the same in translation),
        //      but this DOES NOT WORK. For whatever reason, some report 512B logical, 4k physical....for no apparent reason. - TJE

        //printf("passthrough type set to %d\n", device->drive_info.passThroughHacks.passthroughType);
        int satCheck = FAILURE;
        //if we haven't already, check the device for SAT support. Allow this to run on IDE interface since we'll just issue a SAT identify in here to set things up...might reduce multiple commands later
        if (checkForSAT && !satVPDPageRead && !satComplianceChecked && (device->drive_info.drive_type != RAID_DRIVE) && (device->drive_info.drive_type != NVME_DRIVE) 
            && device->drive_info.media_type != MEDIA_UNKNOWN && device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_JMICRON)
        {
            satCheck = check_SAT_Compliance_And_Set_Drive_Type(device);
        }

#if !defined (DISABLE_NVME_PASSTHROUGH)
        //Because we may find an NVMe over USB device, if we find one of these, perform a little more discovery...
        if ((device->drive_info.passThroughHacks.passthroughType >= NVME_PASSTHROUGH_JMICRON && device->drive_info.passThroughHacks.passthroughType < NVME_PASSTHROUGH_UNKNOWN)
            ||
            (satCheck != SUCCESS && checkJMicronNVMe)
            )
        {
            int scsiRet = ret;
            if (checkJMicronNVMe)
            {
                device->drive_info.passThroughHacks.passthroughType = NVME_PASSTHROUGH_JMICRON;
            }
            //NOTE: It is OK if this fails since it will fall back to treating as SCSI
            ret = fill_In_NVMe_Device_Info(device);
            if (ret == SUCCESS && checkJMicronNVMe)
            {
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue = 13;
                device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported = true;//set this so in 
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
                device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength = UINT16_MAX;
            }
            else if(checkJMicronNVMe)
            {
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                ret = scsiRet;//do not fail here since this should otherwise be treated as a SCSI drive
            }
            else
            {
                ret = scsiRet;//do not fail here since this should otherwise be treated as a SCSI drive
            }
        }
#endif
    }
    else
    {
        if (VERBOSITY_DEFAULT < device->deviceVerbosity)
        {
            printf("Getting Standard Inquiry Data Failed\n");
        }
        ret = COMMAND_FAILURE;
    }
    safe_Free_aligned(inq_buf)

    #ifdef _DEBUG
    printf("\nscsi helper\n");
    printf("Drive type: %d\n",device->drive_info.drive_type);
    printf("Interface type: %d\n",device->drive_info.interface_type);
    printf("Media type: %d\n",device->drive_info.media_type);
    printf("%s: <--\n",__FUNCTION__);
    #endif
    return ret;
}

//this is a look up that makes use of the formula in SPC spec for how versions are set. Formula is ((standard x 32) + revision)
//The simplest thing to do is take the version descriptor and divide it by 32. Using iteger division we can check if that matches the standard we're looking for. - TJE
bool is_Standard_Supported(uint16_t versionDescriptor, eStandardCode standardCode)
{
    if ((eStandardCode)(versionDescriptor / 32) == standardCode)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void decypher_SCSI_Version_Descriptors(uint16_t versionDescriptor, char* versionString)
{
    switch (versionDescriptor / 32)
    {
            //1 - 8 architecture model
    case STANDARD_CODE_SAM:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAM");
        break;
    case STANDARD_CODE_SAM2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAM-2");
        break;
    case STANDARD_CODE_SAM3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAM-3");
        break;
    case STANDARD_CODE_SAM4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAM-4");
        break;
    case STANDARD_CODE_SAM5:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAM-5");
        break;
    case STANDARD_CODE_SAM6:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAM-6");
        break;
            //9-64 Command Set
    case STANDARD_CODE_SPC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPC");
        break;
    case STANDARD_CODE_MMC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "MMC");
        break;
    case STANDARD_CODE_SCC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SCC");
        break;
    case STANDARD_CODE_SBC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SBC");
        break;
    case STANDARD_CODE_SMC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SMC");
        break;
    case STANDARD_CODE_SES:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SES");
        break;
    case STANDARD_CODE_SCC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SCC-2");
        break;
    case STANDARD_CODE_SSC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSC");
        break;
    case STANDARD_CODE_RBC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "RBC");
        break;
    case STANDARD_CODE_MMC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "MMC-2");
        break;
    case STANDARD_CODE_SPC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPC-2");
        break;
    case STANDARD_CODE_OCRW:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "OCRW");
        break;
    case STANDARD_CODE_MMC3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "MMC-3");
        break;
    case STANDARD_CODE_RMC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "RMC");
        break;
    case STANDARD_CODE_SMC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SMC-2");
        break;
    case STANDARD_CODE_SPC3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPC-3");
        break;
    case STANDARD_CODE_SBC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SBC-2");
        break;
    case STANDARD_CODE_OSD:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "OSD");
        break;
    case STANDARD_CODE_SSC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSC-2");
        break;
    case STANDARD_CODE_BCC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "BCC");
        break;
    case STANDARD_CODE_MMC4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "MMC-4");
        break;
    case STANDARD_CODE_ADC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADC");
        break;
    case STANDARD_CODE_SES2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SES-2");
        break;
    case STANDARD_CODE_SSC3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSC-3");
        break;
    case STANDARD_CODE_MMC5:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "MMC-5");
        break;
    case STANDARD_CODE_OSD2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "OSD-2");
        break;
    case STANDARD_CODE_SPC4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPC-4");
        break;
    case STANDARD_CODE_SMC3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SMC-3");
        break;
    case STANDARD_CODE_ADC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADC-2");
        break;
    case STANDARD_CODE_SBC3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SBC-3");
        break;
    case STANDARD_CODE_MMC6:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "MMC-6");
        break;
    case STANDARD_CODE_ADC3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADC-3");
        break;
    case STANDARD_CODE_SSC4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSC-4");
        break;
    case STANDARD_CODE_OSD3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "OSD-3");
        break;
    case STANDARD_CODE_SES3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SES-3");
        break;
    case STANDARD_CODE_SSC5:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSC-5");
        break;
    case STANDARD_CODE_SPC5:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPC-5");
        break;
    case STANDARD_CODE_SFSC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SFSC");
        break;
    case STANDARD_CODE_SBC4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SBC-4");
        break;
    case STANDARD_CODE_ZBC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ZBC");
        break;
    case STANDARD_CODE_ADC4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADC-4");
        break;
    case STANDARD_CODE_ZBC2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ZBC-2");
        break;
    case STANDARD_CODE_SES4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SES-4");
        break;
            //65 - 84 Physical Mapping protocol
    case STANDARD_CODE_SSA_TL2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSA-TL2");
        break;
    case STANDARD_CODE_SSA_TL1:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSA-TL1");
        break;
    case STANDARD_CODE_SSA_S3P:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSA-S3P");
        break;
    case STANDARD_CODE_SSA_S2P:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSA-S2P");
        break;
    case STANDARD_CODE_SIP:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SIP");
        break;
    case STANDARD_CODE_FCP:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FCP");
        break;
    case STANDARD_CODE_SBP2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SBP-2");
        break;
    case STANDARD_CODE_FCP2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FCP-2");
        break;
    case STANDARD_CODE_SST:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SST");
        break;
    case STANDARD_CODE_SRP:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SRP");
        break;
    case STANDARD_CODE_iSCSI:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "iSCSI");
        break;
    case STANDARD_CODE_SBP3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SBP-3");
        break;
    case STANDARD_CODE_SRP2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SRP-2");
        break;
    case STANDARD_CODE_ADP:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADP");
        break;
    case STANDARD_CODE_ADT:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADT");
        break;
    case STANDARD_CODE_FCP3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FCP-3");
        break;
    case STANDARD_CODE_ADT2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADT-2");
        break;
    case STANDARD_CODE_FCP4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FCP-4");
        break;
    case STANDARD_CODE_ADT3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ADT-3");
        break;
            //85 - 94 Parallel SCSI Physical
    case STANDARD_CODE_SPI:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPI");
        break;
    case STANDARD_CODE_FAST20:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "Fast-20");
        break;
    case STANDARD_CODE_SPI2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPI-2");
        break;
    case STANDARD_CODE_SPI3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPI-3");
        break;
    case STANDARD_CODE_EPI:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "EPI");
        break;
    case STANDARD_CODE_SPI4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPI-4");
        break;
    case STANDARD_CODE_SPI5:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPI-5");
        break;
            //95 - 104 Serial Attached SCSI
    case STANDARD_CODE_SAS:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAS");
        break;
    case STANDARD_CODE_SAS1_1:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAS-1.1");
        break;
    case STANDARD_CODE_SAS2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAS-2");
        break;
    case STANDARD_CODE_SAS2_1:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAS-2.1");
        break;
    case STANDARD_CODE_SAS3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAS-3");
        break;
    case STANDARD_CODE_SAS4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAS-4");
        break;
            //105 - 154 Fibre Channel
    case STANDARD_CODE_FC_PH:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PH");
        break;
    case STANDARD_CODE_FC_AL:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-AL");
        break;
    case STANDARD_CODE_FC_AL2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-AL-2");
        break;
    case STANDARD_CODE_AC_PH3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PH-3");
        break;
    case STANDARD_CODE_FC_FS:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-FS");
        break;
    case STANDARD_CODE_FC_PI:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PI");
        break;
    case STANDARD_CODE_FC_PI2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PI-2");
        break;
    case STANDARD_CODE_FC_FS2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-FS-2");
        break;
    case STANDARD_CODE_FC_LS:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-LS");
        break;
    case STANDARD_CODE_FC_SP:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-SP");
        break;
    case STANDARD_CODE_FC_PI3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PI-3");
        break;
    case STANDARD_CODE_FC_PI4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PI-4");
        break;
    case STANDARD_CODE_FC_10GFC:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC 10GFC");
        break;
    case STANDARD_CODE_FC_SP2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-SP-2");
        break;
    case STANDARD_CODE_FC_FS3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-FS-3");
        break;
    case STANDARD_CODE_FC_LS2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-LS-2");
        break;
    case STANDARD_CODE_FC_PI5:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PI-5");
        break;
    case STANDARD_CODE_FC_PI6:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PI-6");
        break;
    case STANDARD_CODE_FC_FS4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-FS-4");
        break;
    case STANDARD_CODE_FC_LS3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-LS-3");
        break;
    case STANDARD_CODE_FC_SCM:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-SCM");
        break;
    case STANDARD_CODE_FC_DA2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-DA-2");
        break;
    case STANDARD_CODE_FC_DA:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-DA");
        break;
    case STANDARD_CODE_FC_TAPE:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-Tape");
        break;
    case STANDARD_CODE_FC_FLA:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-FLA");
        break;
    case STANDARD_CODE_FC_PLDA:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "FC-PLDA");
        break;
            //155 - 164 SSA
    case STANDARD_CODE_SSA_PH2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSA-PH2");
        break;
    case STANDARD_CODE_SSA_PH3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SSA-PH3");
        break;
            //165 - 174 IEEE 1394
    case STANDARD_CODE_IEEE_1394_1995:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "IEEE-1394-1995");
        break;
    case STANDARD_CODE_IEEE_1394a:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "IEEE-1394a");
        break;
    case STANDARD_CODE_IEEE_1394b:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "IEEE-1394b");
        break;
            //175 - 200 ATAPI & USB
    case STANDARD_CODE_ATA_ATAPI6:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ATA/ATAPI-6");
        break;
    case STANDARD_CODE_ATA_ATAPI7:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ATA/ATAPI-7");
        break;
    case STANDARD_CODE_ATA_ATAPI8:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ATA8-ACS");
        break;
    case STANDARD_CODE_USB:
        switch (versionDescriptor)
        {
        case 0x1728: //USB 1.1
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "USB-1.1");
            break;
        case 0x1729: //USB 2.0
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "USB-2.0");
            break;
        case 0x1730: //USB BOT
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "USB-BOT");
            break;
        default:
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "USB");
            break;
        }
        break;
    case STANDARD_CODE_UAS:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "UAS");
        break;
    case STANDARD_CODE_ACSx:
        //special case, look at the version descriptor to set an exact version number
        switch (versionDescriptor)
        {
        case 0x1761: //ACS-2
        case 0x1762:
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ACS-2");
            break;
        case 0x1765: //ACS-3
        case 0x1766: //ACS-3 INCITS 522-2014
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ACS-3");
            break;
        case 0x1767: //ACS-4 INCITS 529-2018
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ACS-4");
            break;
        default:
            snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "ACS-x");
            break;
        }
        break;
    case STANDARD_CODE_UAS2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "UAS-2");
        break;
            //201 - 224 Networking
            //225 - 244 ATM
            //245 - 260 Translators
    case STANDARD_CODE_SAT:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAT");
        break;
    case STANDARD_CODE_SAT2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAT-2");
        break;
    case STANDARD_CODE_SAT3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAT-3");
        break;
    case STANDARD_CODE_SAT4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SAT-4");
        break;
            //261 - 270 SAS Transport Protocols
    case STANDARD_CODE_SPL:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPL");
        break;
    case STANDARD_CODE_SPL2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPL-2");
        break;
    case STANDARD_CODE_SPL3:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPL-3");
        break;
    case STANDARD_CODE_SPL4:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SPL-4");
        break;
            //271 - 290 SCSI over PCI Extress Transport Protocols
    case STANDARD_CODE_SOP:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SOP");
        break;
    case STANDARD_CODE_PQI:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "PQI");
        break;
    case STANDARD_CODE_SOP2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "SOP-2");
        break;
    case STANDARD_CODE_PQI2:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "PQI-2");
        break;
            //291 - 2045 Reserved for Expansion
    case STANDARD_CODE_IEEE_1667:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "IEEE-1667");
        break;
    case STANDARD_CODE_RESERVED:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "RESERVED");
        break;
    case STANDARD_CODE_NOT_SUPPORTED:
    default:
        snprintf(versionString, MAX_VERSION_DESCRIPTOR_STRING_LENGTH,  "----");
        break;
    }
    return;
}
