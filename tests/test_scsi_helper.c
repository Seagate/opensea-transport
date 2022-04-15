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
#include "scsi_helper_func.h"

int _check_Sense_Key_ASC_ASCQ_And_FRU(tDevice *device, uint8_t senseKey, uint8_t asc, uint8_t ascq, uint8_t fru)
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

int32_t main(int argc, char *argv[])
{
    int i, j, k;
    tDevice dev = {};

    dev.deviceVerbosity = VERBOSITY_BUFFERS;
    for (i = 0; i <= 15; i++)
    {
        for (j = 0; j <= 255; j++)
        {
            for (k = 0; k <= 255; k++)
            {
                printf("%2X %2X %2X\n", i, j, k);
                if (check_Sense_Key_ASC_ASCQ_And_FRU(&dev, i, j, k, 0) != _check_Sense_Key_ASC_ASCQ_And_FRU(&dev, i, j, k, 0))
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}
