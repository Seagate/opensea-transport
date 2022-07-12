//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2022 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
#include "vendor/seagate/seagate_common_types.h"
#include <ctype.h>//for checking for printable characters
#include <stdlib.h> // for bsearch

#include "nvme_helper_func.h"

typedef struct _senseRetDesc
{
    eReturnValues ret;
    const char * desc;
}senseRetDesc;

senseRetDesc senseKeyRetDesc[] = {
    {SUCCESS, "No Error"},
    {FAILURE, "Recovered Error"},
    {FAILURE, "Not Ready"},
    {FAILURE, "Medium Error"},
    {FAILURE, "Hardware Error"},
    {NOT_SUPPORTED, "Illegal Request"},
    {FAILURE, "Unit Attention"},
    {FAILURE, "Data Protect"},
    {FAILURE, "Blank Check"},
    {FAILURE, "Vendor Specific"},
    {FAILURE, "Copy Aborted"},
    {ABORTED, "Aborted Command"},
    {FAILURE, "Reserved"},
    {FAILURE, "Volume Overflow"},
    {FAILURE, "Miscompare"},
    {SUCCESS, "Completed"}
};

typedef struct _ascAscqRetDesc
{
    uint8_t asc;
    uint8_t ascq;
    int ret;
    const char * desc;
}ascAscqRetDesc;

// DO NOT break the order of ASC and ASCQ below
// for 3rd column (ret), -1 means to keep existing ret value, don't change it
ascAscqRetDesc ascAscqLookUp[] = {
    {0x00, 0x00, -1,                         "No Additional Sense Information"},
    {0x00, 0x01, -1,                         "Filemark Detected"},
    {0x00, 0x02, -1,                         "End-Of_Partition/Medium Detected"},
    {0x00, 0x03, -1,                         "Setmark Detected"},
    {0x00, 0x04, -1,                         "Beginning-Of-Partition/Medium Detected"},
    {0x00, 0x05, -1,                         "End-Of-Data Detected"},
    {0x00, 0x06, C_CAST(int, FAILURE),       "I/O Process Terminated"},
    {0x00, 0x07, -1,                         "Programmable Early Warning Detected"},
    {0x00, 0x11, C_CAST(int, IN_PROGRESS),   "Audio Play Operation In Progress"},
    {0x00, 0x12, -1,                         "Audio Play Operation Paused"},
    {0x00, 0x13, C_CAST(int, SUCCESS),       "Audio Play Operation Successfully Completed"},
    {0x00, 0x14, C_CAST(int, FAILURE),       "Audio Play Operation Stopped Due To Error"},
    {0x00, 0x15, -1,                         "No Current Audio Status To Return"},
    {0x00, 0x16, C_CAST(int, IN_PROGRESS),   "Operation In Progress"},
    {0x00, 0x17, C_CAST(int, UNKNOWN),       "Cleaning Requested"},
    {0x00, 0x18, C_CAST(int, IN_PROGRESS),   "Erase Operation In Progress"},
    {0x00, 0x19, C_CAST(int, IN_PROGRESS),   "Locate Operation In Progress"},
    {0x00, 0x1A, C_CAST(int, IN_PROGRESS),   "Rewind Operation In Progress"},
    {0x00, 0x1B, C_CAST(int, IN_PROGRESS),   "Set Capacity Operation In Progress"},
    {0x00, 0x1C, C_CAST(int, IN_PROGRESS),   "Verify Operation In Progress"},
    {0x00, 0x1D, C_CAST(int, UNKNOWN),       "ATA Passthrough Information Available"},
    {0x00, 0x1E, C_CAST(int, UNKNOWN),       "Conflicting SA Creation Request"},
    {0x00, 0x1F, C_CAST(int, SUCCESS),       "Logical Unit Transitioning To Another Power Condition"},
    {0x00, 0x20, C_CAST(int, SUCCESS),       "Extended Copy Information Available"},
    {0x00, 0x21, C_CAST(int, FAILURE),       "Atomic Command Aborted Due To ACA"},
    {0x01, 0x00, C_CAST(int, FAILURE),       "No Index/Sector Signal"},
    {0x02, 0x00, C_CAST(int, FAILURE),       "No Seek Complete"},
    {0x03, 0x00, C_CAST(int, FAILURE),       "Peripheral Device Write Fault"},
    {0x03, 0x01, C_CAST(int, FAILURE),       "No Write Current"},
    {0x03, 0x02, C_CAST(int, FAILURE),       "Excessive Write Errors"},
    {0x04, 0x00, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Cause Not Reported"},
    {0x04, 0x01, C_CAST(int, FAILURE),       "Logical Unit Is In The Process Of Becoming Ready"},
    {0x04, 0x02, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Initializing Command Required"},
    {0x04, 0x03, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Manual Intervention Required"},
    {0x04, 0x04, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Format In Progress"},
    {0x04, 0x05, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Rebuild In Progress"},
    {0x04, 0x06, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Recalculation In Progress"},
    {0x04, 0x07, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Operation In Progress"},
    {0x04, 0x09, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Self-Test In Progress"},
    {0x04, 0x0A, C_CAST(int, FAILURE),       "Logical Unit Not Accessible, Asymetric Access State Transition"},
    {0x04, 0x0B, C_CAST(int, FAILURE),       "Logical Unit Not Accessible, Target Port In Standby State"},
    {0x04, 0x0C, C_CAST(int, FAILURE),       "Logical Unit Not Accessible, Target Port in Unavailable State"},
    {0x04, 0x0D, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Structure Check Required"},
    {0x04, 0x0E, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Security Session In Progress"},
    {0x04, 0x10, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Auxilary Memory Not Accessible"},
    {0x04, 0x11, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Notify (Enable Spinup) Required"},
    {0x04, 0x13, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, SA Creation In Progress"},
    {0x04, 0x14, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Space Allocation In Progress"},
    {0x04, 0x15, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Robotics Disabled"},
    {0x04, 0x16, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Configuration Required"},
    {0x04, 0x17, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Calibration Required"},
    {0x04, 0x18, C_CAST(int, FAILURE),       "Logical Unit Not Ready, A Door Is Open"},
    {0x04, 0x19, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Operating In Sequential Mode"},
    {0x04, 0x1A, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Start Stop Unit Command In Progress"},
    {0x04, 0x1B, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Sanitize In Progress"},
    {0x04, 0x1C, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Additional Power Use Not Yet Granted"},
    {0x04, 0x1D, C_CAST(int, IN_PROGRESS),   "Logical Unit Not Ready, Configuration In Progress"},
    {0x04, 0x1E, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Microcode Activation Required"},
    {0x04, 0x1F, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Microcode Download Required"},
    {0x04, 0x20, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Logical Unit Reset Required"},
    {0x04, 0x21, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Hard Reset Required"},
    {0x04, 0x22, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Power Cycle Required"},
    {0x04, 0x23, C_CAST(int, FAILURE),       "Logical Unit Not Ready, Affiliation Required"},
    {0x05, 0x00, C_CAST(int, FAILURE),       "Logical Unit Does Not Respond To Selection"},
    {0x06, 0x00, C_CAST(int, FAILURE),       "No Reference Position Found"},
    {0x07, 0x00, C_CAST(int, FAILURE),       "Multiple Peripheral Devices Selected"},
    {0x08, 0x00, C_CAST(int, FAILURE),       "Logical Unit Communication Failure"},
    {0x08, 0x01, C_CAST(int, FAILURE),       "Logical Unit Communication Time-Out"},
    {0x08, 0x02, C_CAST(int, FAILURE),       "Logical Unit Communication Parity Error"},
    {0x08, 0x03, C_CAST(int, FAILURE),       "Logical Unit Communication CRC Error (Ultra-DMA/32)"},
    {0x08, 0x04, C_CAST(int, FAILURE),       "Unreachable Copy Target"},
    {0x09, 0x00, C_CAST(int, FAILURE),       "Track Following Error"},
    {0x09, 0x01, C_CAST(int, FAILURE),       "Tracking Servo Failure"},
    {0x09, 0x02, C_CAST(int, FAILURE),       "Focus Servo Failure"},
    {0x09, 0x03, C_CAST(int, FAILURE),       "Spindle Servo Failure"},
    {0x09, 0x04, C_CAST(int, FAILURE),       "Head Select Fault"},
    {0x09, 0x05, C_CAST(int, FAILURE),       "Vibration Induced Tracking Error"},
    {0x0A, 0x00, C_CAST(int, FAILURE),       "Error Log Overflow"},
    {0x0B, 0x00, -1,                         "Warning"},
    {0x0B, 0x01, -1,                         "Warning - Specified Temperature Exceeded"},
    {0x0B, 0x02, -1,                         "Warning - Enclosure Degraded"},
    {0x0B, 0x03, -1,                         "Warning - Background Self-Test Failed"},
    {0x0B, 0x04, -1,                         "Warning - Background Pre-Scan Detected Medium Error"},
    {0x0B, 0x05, -1,                         "Warning - Background Media Scan Detected Medium Error"},
    {0x0B, 0x06, -1,                         "Warning - Non-Volitile Cache Now Volitile"},
    {0x0B, 0x07, -1,                         "Warning - Degraded Power To Non-Volitile Cache"},
    {0x0B, 0x08, -1,                         "Warning - Power Loss Expected"},
    {0x0B, 0x09, -1,                         "Warning - Device Statistics Notification Active"},
    {0x0B, 0x0A, -1,                         "Warning - High Critical Temperature Limit Exceeded"},
    {0x0B, 0x0B, -1,                         "Warning - Low Critical Temperature Limit Exceeded"},
    {0x0B, 0x0C, -1,                         "Warning - High Operating Temperature Limit Exceeded"},
    {0x0B, 0x0D, -1,                         "Warning - Low Operating Temperature Limit Exceeded"},
    {0x0B, 0x0E, -1,                         "Warning - High Critical Humidity Limit Exceeded"},
    {0x0B, 0x0F, -1,                         "Warning - Low Critical Humidity Limit Exceeded"},
    {0x0B, 0x10, -1,                         "Warning - High Operating Humidity Limit Exceeded"},
    {0x0B, 0x11, -1,                         "Warning - Low Operating Humidity Limit Exceeded"},
    {0x0B, 0x12, -1,                         "Warning - Microcode Security At Risk"},
    {0x0B, 0x13, -1,                         "Warning - Microcode Digital Signature Validation Failure"},
    {0x0C, 0x00, C_CAST(int, FAILURE),       "Write Error"},
    {0x0C, 0x01, -1,                         "Write Error - Recovered With Auto Reallocation"},
    {0x0C, 0x02, C_CAST(int, FAILURE),       "Write Error - Auto Reallocation Failed"},
    {0x0C, 0x03, C_CAST(int, FAILURE),       "Write Error - Recommend Reassignment"},
    {0x0C, 0x04, C_CAST(int, FAILURE),       "Compression Check Miscompare Error"},
    {0x0C, 0x05, C_CAST(int, FAILURE),       "Data Expansion Occurred During Compression"},
    {0x0C, 0x06, C_CAST(int, FAILURE),       "Block Not Compressible"},
    {0x0C, 0x07, C_CAST(int, FAILURE),       "Write Error - Recovery Needed"},
    {0x0C, 0x08, C_CAST(int, FAILURE),       "Write Error - Recovery Failed"},
    {0x0C, 0x09, C_CAST(int, FAILURE),       "Write Error - Loss Of Streaming"},
    {0x0C, 0x0A, C_CAST(int, FAILURE),       "Write Error - Padding Blocks Added"},
    {0x0C, 0x0B, C_CAST(int, FAILURE),       "Auxiliary Memory Write Error"},
    {0x0C, 0x0C, C_CAST(int, FAILURE),       "Write Error - Unexpected Unsolicited Data"},
    {0x0C, 0x0D, C_CAST(int, FAILURE),       "Write Error - Not Enough Unsolicited Data"},
    {0x0C, 0x0E, C_CAST(int, FAILURE),       "Multiple Write Errors"},
    {0x0C, 0x0F, C_CAST(int, FAILURE),       "Defects In Error Window"},
    {0x0C, 0x10, C_CAST(int, FAILURE),       "Incomplete Multiple Atomic Write Operations"},
    {0x0C, 0x11, C_CAST(int, FAILURE),       "Write Error - Recovery Scan Needed"},
    {0x0C, 0x12, C_CAST(int, FAILURE),       "Write Error - Insufficient Zone Resources"},
    {0x0D, 0x00, C_CAST(int, FAILURE),       "Error Detected By Third Party Temporary Initiator"},
    {0x0D, 0x01, C_CAST(int, FAILURE),       "Third Party Device Failure"},
    {0x0D, 0x02, C_CAST(int, FAILURE),       "Copy Target Device Not Reachable"},
    {0x0D, 0x03, C_CAST(int, FAILURE),       "Incorrect Copy Target Device Type"},
    {0x0D, 0x04, C_CAST(int, FAILURE),       "Copy Target Device Data Underrun"},
    {0x0D, 0x05, C_CAST(int, FAILURE),       "Copy Target Device Data Overrun"},
    {0x0E, 0x00, C_CAST(int, FAILURE),       "Invalid Information Unit"},
    {0x0E, 0x01, C_CAST(int, FAILURE),       "Information Unit Too Short"},
    {0x0E, 0x02, C_CAST(int, FAILURE),       "Information Unit Too Long"},
    {0x0E, 0x03, C_CAST(int, FAILURE),       "Invalid Field In Command Information Unit"},
    {0x10, 0x00, C_CAST(int, FAILURE),       "ID CRC Or ECC Error"},
    {0x10, 0x01, C_CAST(int, FAILURE),       "Logical Block Guard Check Failed"},
    {0x10, 0x02, C_CAST(int, FAILURE),       "Logical Block Application Tag Check Failed"},
    {0x10, 0x03, C_CAST(int, FAILURE),       "Logical Block Reference Tag Check Failed"},
    {0x10, 0x04, C_CAST(int, FAILURE),       "Logical Block Protection Error On Recover Buffered Data"},
    {0x10, 0x05, C_CAST(int, FAILURE),       "Logical Block Protection Method Error"},
    {0x11, 0x00, C_CAST(int, FAILURE),       "Unrecovered Read Error"},
    {0x11, 0x01, C_CAST(int, FAILURE),       "Read Retries Exhausted"},
    {0x11, 0x02, C_CAST(int, FAILURE),       "Error Too Long To Correct"},
    {0x11, 0x03, C_CAST(int, FAILURE),       "Multiple Read Errors"},
    {0x11, 0x04, C_CAST(int, FAILURE),       "Unrecovered Read Error - Auto Reallocate Failed"},
    {0x11, 0x05, C_CAST(int, FAILURE),       "L-EC Uncorrectable Error"},
    {0x11, 0x06, C_CAST(int, FAILURE),       "CIRC Unrecovered Error"},
    {0x11, 0x07, C_CAST(int, FAILURE),       "Data Re-synchonization Error"},
    {0x11, 0x08, C_CAST(int, FAILURE),       "Incomplete Block Read"},
    {0x11, 0x09, C_CAST(int, FAILURE),       "No Gap Found"},
    {0x11, 0x0A, C_CAST(int, FAILURE),       "Miscorrected Error"},
    {0x11, 0x0B, C_CAST(int, FAILURE),       "Unrecovered Read Error - Recommend Reassignment"},
    {0x11, 0x0C, C_CAST(int, FAILURE),       "Unrecovered Read Error - Recommend Rewrite The Data"},
    {0x11, 0x0D, C_CAST(int, FAILURE),       "De-compression CRC Error"},
    {0x11, 0x0E, C_CAST(int, FAILURE),       "Cannot Decompress Using Declared Algorithm"},
    {0x11, 0x0F, C_CAST(int, FAILURE),       "Error Reading UPC/EAN Number"},
    {0x11, 0x10, C_CAST(int, FAILURE),       "Error Reading ISRC Number"},
    {0x11, 0x11, C_CAST(int, FAILURE),       "Read Error - Loss Of Streaming"},
    {0x11, 0x12, C_CAST(int, FAILURE),       "Auxiliary Memory Read Error"},
    {0x11, 0x13, C_CAST(int, FAILURE),       "Read Error - Failed Retransmission Request"},
    {0x11, 0x14, C_CAST(int, FAILURE),       "Read Error - LBA Marked Bad By Application Client"},
    {0x11, 0x15, C_CAST(int, FAILURE),       "Write After Sanitize Required"},
    {0x12, 0x00, C_CAST(int, FAILURE),       "Address Mark Not Found for ID Field"},
    {0x13, 0x00, C_CAST(int, FAILURE),       "Address Mark Not Found for Data Field"},
    {0x14, 0x00, C_CAST(int, FAILURE),       "Recorded Entity Not Found"},
    {0x14, 0x01, C_CAST(int, FAILURE),       "Record Not Found"},
    {0x14, 0x02, C_CAST(int, FAILURE),       "Filemark Or Setmark Not Found"},
    {0x14, 0x03, C_CAST(int, FAILURE),       "End-Of-Data Not Found"},
    {0x14, 0x04, C_CAST(int, FAILURE),       "Block Sequence Error"},
    {0x14, 0x05, C_CAST(int, FAILURE),       "Record Not Found - Recommend Reassignment"},
    {0x14, 0x06, C_CAST(int, FAILURE),       "Record Not Found - Data Auto-Reallocated"},
    {0x14, 0x07, C_CAST(int, FAILURE),       "Locate Operation Failure"},
    {0x15, 0x00, C_CAST(int, FAILURE),       "Random Positioning Error"},
    {0x15, 0x01, C_CAST(int, FAILURE),       "Mechanical Positioning Error"},
    {0x15, 0x02, C_CAST(int, FAILURE),       "Positioning Error Detected By Read Of Medium"},
    {0x16, 0x00, C_CAST(int, FAILURE),       "Data Synchronization Mark Error"},
    {0x16, 0x01, C_CAST(int, SUCCESS),       "Data Sync Error - Data Rewritten"},
    {0x16, 0x02, C_CAST(int, FAILURE),       "Data Sync Error - Recommend Rewrite"},
    {0x16, 0x03, C_CAST(int, SUCCESS),       "Data Sync Error - Data Auto-Reallocation"},
    {0x16, 0x04, C_CAST(int, FAILURE),       "Data Sync Error - Recommend Reassignment"},
    {0x17, 0x00, -1,                         "Recovered Data With No Error Correction Applied"},
    {0x17, 0x01, -1,                         "Recovered Data With Retries"},
    {0x17, 0x02, -1,                         "Recovered Data With Positive Head Offset"},
    {0x17, 0x03, -1,                         "Recovered Data With Negative Head Offset"},
    {0x17, 0x04, -1,                         "Recovered Data With Retries And/Or CIRC Applied"},
    {0x17, 0x05, -1,                         "Recovered Data Using Previous Sector ID"},
    {0x17, 0x06, -1,                         "Recovered Data Without ECC - Data Auto-Reallocated"},
    {0x17, 0x07, -1,                         "Recovered Data Without ECC - Recommend Reassignment"},
    {0x17, 0x08, -1,                         "Recovered Data Without ECC - Recommend Rewrite"},
    {0x17, 0x09, -1,                         "Recovered Data Without ECC - Data Rewritten"},
    {0x18, 0x00, -1,                         "Recovered Data With Error Correction Applied"},
    {0x18, 0x01, -1,                         "Recovered Data With Error Correction & Retries Applied"},
    {0x18, 0x02, -1,                         "Recovered Data - Data Auto-Reallocated"},
    {0x18, 0x03, -1,                         "Recovered Data With CIRC"},
    {0x18, 0x04, -1,                         "Recovered Data With L-EC"},
    {0x18, 0x05, -1,                         "Recovered Data - Recommend Reassignment"},
    {0x18, 0x06, -1,                         "Recovered Data - Recommend Rewrite"},
    {0x18, 0x07, -1,                         "Recovered Data With ECC - Data Rewritten"},
    {0x18, 0x08, C_CAST(int, FAILURE),       "Recovered Data With Linking"},
    {0x19, 0x00, C_CAST(int, FAILURE),       "Defect List Error"},
    {0x19, 0x01, C_CAST(int, FAILURE),       "Defect List Not Available"},
    {0x19, 0x02, C_CAST(int, FAILURE),       "Defect List Error In Primary List"},
    {0x19, 0x03, C_CAST(int, FAILURE),       "Defect List Error In Grown List"},
    {0x1A, 0x00, C_CAST(int, FAILURE),       "Parameter List Length Error"},
    {0x1B, 0x00, C_CAST(int, FAILURE),       "Synchronous Data Transfer Error"},
    {0x1C, 0x00, C_CAST(int, FAILURE),       "Defect List Not Found"},
    {0x1C, 0x01, C_CAST(int, FAILURE),       "Primary Defect List Not Found"},
    {0x1C, 0x02, C_CAST(int, FAILURE),       "Grown Defect List Not Found"},
    {0x1D, 0x00, C_CAST(int, FAILURE),       "Miscompare During Verify Operation"},
    {0x1D, 0x01, C_CAST(int, FAILURE),       "Miscompare During Verify Of Unmapped LBA"},
    {0x1E, 0x00, -1,                         "Recovered ID With ECC Correction"},
    {0x1F, 0x00, C_CAST(int, FAILURE),       "Partial Defect List Transfer"},
    {0x20, 0x00, C_CAST(int, NOT_SUPPORTED), "Invalid Command Operation Code"},
    {0x20, 0x01, C_CAST(int, FAILURE),       "Access Denied - Initiator Pending - Enrolled"},
    {0x20, 0x02, C_CAST(int, FAILURE),       "Access Denied - No Access Rights"},
    {0x20, 0x03, C_CAST(int, FAILURE),       "Access Denied - Invalid Management ID Key"},
    {0x20, 0x04, C_CAST(int, FAILURE),       "Illegal Command While In Write Capable State"},
    {0x20, 0x05, C_CAST(int, FAILURE),       "Illegal Command While In Read Capable State"},
    {0x20, 0x06, C_CAST(int, FAILURE),       "Illegal Command While In Explicit Address Mode"},
    {0x20, 0x07, C_CAST(int, FAILURE),       "Illegal Command While In Implicit Address Mode"},
    {0x20, 0x08, C_CAST(int, FAILURE),       "Access Denied - Enrollment Conflict"},
    {0x20, 0x09, C_CAST(int, FAILURE),       "Access Denied - Invalid Logical Unit Identifier"},
    {0x20, 0x0A, C_CAST(int, FAILURE),       "Access Denied - Invalid Proxy Token"},
    {0x20, 0x0B, C_CAST(int, FAILURE),       "Access Denied - ACL LUN Conflict"},
    {0x20, 0x0C, C_CAST(int, FAILURE),       "Illegal Command When Not In Append-Only Mode"},
    {0x20, 0x0D, C_CAST(int, FAILURE),       "Not An Administrative Logical Unit"},
    {0x20, 0x0E, C_CAST(int, FAILURE),       "Not A Subsidiary Logical Unit"},
    {0x20, 0x0F, C_CAST(int, FAILURE),       "Not A Conglomerate Logical Unit"},
    {0x21, 0x00, C_CAST(int, FAILURE),       "Logical Block Address Out Of Range"},
    {0x21, 0x01, C_CAST(int, FAILURE),       "Invalid Element Address"},
    {0x21, 0x02, C_CAST(int, FAILURE),       "Invalid Address For Write"},
    {0x21, 0x03, C_CAST(int, FAILURE),       "Invalid Write Crossing Layer Jump"},
    {0x21, 0x04, C_CAST(int, FAILURE),       "Unaligned Write Command"},
    {0x21, 0x05, C_CAST(int, FAILURE),       "Write Boundary Violation"},
    {0x21, 0x06, C_CAST(int, FAILURE),       "Attempt To Read Invalid Data"},
    {0x21, 0x07, C_CAST(int, FAILURE),       "Read Boundary Violation"},
    {0x21, 0x08, C_CAST(int, FAILURE),       "Misaligned Write Command"},
    {0x22, 0x00, C_CAST(int, NOT_SUPPORTED), "Illegal Function. Use 22 00, 24 00, or 26 00"},
    {0x23, 0x00, C_CAST(int, FAILURE),       "Invalid Token Operation - Cause Not Reportable"},
    {0x23, 0x01, C_CAST(int, FAILURE),       "Invalid Token Operation - Unsupported Token Type"},
    {0x23, 0x02, C_CAST(int, NOT_SUPPORTED), "Invalid Token Operation - Remote Token Usage Not Supported"},
    {0x23, 0x03, C_CAST(int, NOT_SUPPORTED), "Invalid Token Operation - Remote ROD Token Creation Not Supported"},
    {0x23, 0x04, C_CAST(int, FAILURE),       "Invalid Token Operation - Token Unknown"},
    {0x23, 0x05, C_CAST(int, FAILURE),       "Invalid Token Operation - Token Corrupt"},
    {0x23, 0x06, C_CAST(int, FAILURE),       "Invalid Token Operation - Token Revoked"},
    {0x23, 0x07, C_CAST(int, FAILURE),       "Invalid Token Operation - Token Expired"},
    {0x23, 0x08, C_CAST(int, FAILURE),       "Invalid Token Operation - Token Cancelled"},
    {0x23, 0x09, C_CAST(int, FAILURE),       "Invalid Token Operation - Token Deleted"},
    {0x23, 0x0A, C_CAST(int, FAILURE),       "Invalid Token Operation - Invalid Token Length"},
    {0x24, 0x00, C_CAST(int, NOT_SUPPORTED), "Invalid Field In CDB"},
    {0x24, 0x01, C_CAST(int, FAILURE),       "CDB Decryption Error"},
    {0x24, 0x02, C_CAST(int, FAILURE),       "Invalid CDB Field While In Explicit Block Address Model"},
    {0x24, 0x03, C_CAST(int, FAILURE),       "Invalid CDB Field While In Implicit Block Address Model"},
    {0x24, 0x04, C_CAST(int, FAILURE),       "Security Audit Value Frozen"},
    {0x24, 0x05, C_CAST(int, FAILURE),       "Security Working Key Frozen"},
    {0x24, 0x06, C_CAST(int, FAILURE),       "Nonce Not Unique"},
    {0x24, 0x07, C_CAST(int, FAILURE),       "Nonce Timestamp Out Of Range"},
    {0x24, 0x08, C_CAST(int, FAILURE),       "Invalid XCDB"},
    {0x24, 0x09, C_CAST(int, FAILURE),       "Invalid Fast Format"},
    {0x25, 0x00, C_CAST(int, NOT_SUPPORTED), "Logical Unit Not Supported"},
    {0x26, 0x00, C_CAST(int, FAILURE),       "Invalid Field In Parameter List"},
    {0x26, 0x01, C_CAST(int, NOT_SUPPORTED), "Parameter Not Supported"},
    {0x26, 0x02, C_CAST(int, FAILURE),       "Parameter Value Invalid"},
    {0x26, 0x03, C_CAST(int, NOT_SUPPORTED), "Threshold Parameters Not Supported"},
    {0x26, 0x04, C_CAST(int, FAILURE),       "Invalid Release Of Persistent Reservation"},
    {0x26, 0x05, C_CAST(int, FAILURE),       "Data Decryption Error"},
    {0x26, 0x06, C_CAST(int, FAILURE),       "Too Many Target Descriptors"},
    {0x26, 0x07, C_CAST(int, NOT_SUPPORTED), "Unsupported Target Descriptor Type Code"},
    {0x26, 0x08, C_CAST(int, FAILURE),       "Too Many Segment Descriptors"},
    {0x26, 0x09, C_CAST(int, FAILURE),       "Unsupported Segment Descriptor Type Code"},
    {0x26, 0x0A, C_CAST(int, FAILURE),       "Unexpected Inexact Segment"},
    {0x26, 0x0B, C_CAST(int, FAILURE),       "Inline Data Length Exceeded"},
    {0x26, 0x0C, C_CAST(int, FAILURE),       "Invalid Operation For Copy Source Or Destination"},
    {0x26, 0x0D, C_CAST(int, FAILURE),       "Copy Segment Granularity Violation"},
    {0x26, 0x0E, C_CAST(int, FAILURE),       "Invalid Parameter While Port Is Enabled"},
    {0x26, 0x0F, C_CAST(int, FAILURE),       "Invalid Data-Out Buffer Integrity Check Value"},
    {0x26, 0x10, C_CAST(int, FAILURE),       "Data Decryption Key Fail Limit Reached"},
    {0x26, 0x11, C_CAST(int, FAILURE),       "Incomplete Key-Associated Data Set"},
    {0x26, 0x12, C_CAST(int, FAILURE),       "Vendor Specific Key Reference Not Found"},
    {0x26, 0x13, C_CAST(int, FAILURE),       "Application Tag Mode Page Is Invalid"},
    {0x26, 0x14, C_CAST(int, FAILURE),       "Tape Stream Mirroring Prevented"},
    {0x26, 0x15, C_CAST(int, FAILURE),       "Copy Source Or Copy Destination Not Authorized"},
    {0x27, 0x00, C_CAST(int, FAILURE),       "Write Protected"},
    {0x27, 0x01, C_CAST(int, FAILURE),       "Hardware Write Protected"},
    {0x27, 0x02, C_CAST(int, FAILURE),       "Logical Unit Software Write Protected"},
    {0x27, 0x03, C_CAST(int, FAILURE),       "Associated Write Protect"},
    {0x27, 0x04, C_CAST(int, FAILURE),       "Persistent Write Protect"},
    {0x27, 0x05, C_CAST(int, FAILURE),       "Permanent Write Protect"},
    {0x27, 0x06, C_CAST(int, FAILURE),       "Conditional Write Protect"},
    {0x27, 0x07, C_CAST(int, FAILURE),       "Space Allocation Failed Write Protect"},
    {0x27, 0x08, C_CAST(int, FAILURE),       "Zone Is Read Only"},
    {0x28, 0x00, C_CAST(int, FAILURE),       "Not Ready To Ready Change, Medium May Have Changed"},
    {0x28, 0x01, C_CAST(int, SUCCESS),       "Import or Export Element Accessed"},
    {0x28, 0x02, C_CAST(int, FAILURE),       "Format-Layer May Have Changed"},
    {0x28, 0x03, C_CAST(int, FAILURE),       "Import/Export Element Accessed, Medium Changed"},
    {0x29, 0x00, -1,                         "Power On, Reset, Or Bus Device Reset Occurred"},
    {0x29, 0x01, -1,                         "Power On Occurred"},
    {0x29, 0x02, -1,                         "SCSI Bus Reset Occurred"},
    {0x29, 0x03, -1,                         "Bus Device Reset Function Occurred"},
    {0x29, 0x04, -1,                         "Device Internal Reset"},
    {0x29, 0x05, -1,                         "Transceiver Mode Changed To Single-Ended"},
    {0x29, 0x06, -1,                         "Transceiver Mode Changed To LVD"},
    {0x29, 0x07, C_CAST(int, FAILURE),       "I_T Nexus Loss Occurred"},
    {0x2A, 0x00, C_CAST(int, SUCCESS),       "Parameters Changed"},
    {0x2A, 0x01, C_CAST(int, SUCCESS),       "Mode Parameters Changed"},
    {0x2A, 0x02, C_CAST(int, SUCCESS),       "Log Parameters Changed"},
    {0x2A, 0x03, C_CAST(int, SUCCESS),       "Reservations Preempted"},
    {0x2A, 0x04, C_CAST(int, SUCCESS),       "Reservations Released"},
    {0x2A, 0x05, C_CAST(int, SUCCESS),       "Registrations Preempted"},
    {0x2A, 0x06, C_CAST(int, FAILURE),       "Asymmetric Access State Changed"},
    {0x2A, 0x07, C_CAST(int, FAILURE),       "Implicit Asymetric Access State Transition Failed"},
    {0x2A, 0x08, C_CAST(int, SUCCESS),       "Priority Changed"},
    {0x2A, 0x09, C_CAST(int, SUCCESS),       "Capacity Data Has Changed"},
    {0x2A, 0x0A, C_CAST(int, SUCCESS),       "Error History I_T Nexus Cleared"},
    {0x2A, 0x0B, C_CAST(int, SUCCESS),       "Error History Snapshot Released"},
    {0x2A, 0x0C, C_CAST(int, SUCCESS),       "Error Recovery Attributes Have Changed"},
    {0x2A, 0x0D, C_CAST(int, SUCCESS),       "Data Encryption Capabilities Changed"},
    {0x2A, 0x10, C_CAST(int, SUCCESS),       "Timestamp Changed"},
    {0x2A, 0x11, -1,                         "Data Encryption Parameters Changed By Another I_T Nexus"},
    {0x2A, 0x12, -1,                         "Data Encryption Parameters Changed By Vendor Specific Event"},
    {0x2A, 0x13, -1,                         "Data Encryption Key Instance Counter Has Changed"},
    {0x2A, 0x14, C_CAST(int, SUCCESS),       "SA Creation Capabilities Has Changed"},
    {0x2A, 0x15, C_CAST(int, FAILURE),       "Medium Removal Precention Preempted"},
    {0x2A, 0x16, -1,                         "Zone Reset Write Pointer Recommended"},
    {0x2B, 0x00, C_CAST(int, FAILURE),       "Copy Cannot Execute Since Host Cannot Disconnect"},
    {0x2C, 0x00, C_CAST(int, FAILURE),       "Command Sequence Error"},
    {0x2C, 0x01, C_CAST(int, FAILURE),       "Too Many Windows Specified"},
    {0x2C, 0x02, C_CAST(int, FAILURE),       "Invalid Combination Of Windows Specified"},
    {0x2C, 0x03, C_CAST(int, FAILURE),       "Current Program Area Is Not Empty"},
    {0x2C, 0x04, -1,                         "Current Program Area Is Empty"},
    {0x2C, 0x05, C_CAST(int, FAILURE),       "Illegal Power Condition Request"},
    {0x2C, 0x06, C_CAST(int, FAILURE),       "Persistent Prevent Conflict"},
    {0x2C, 0x07, C_CAST(int, FAILURE),       "Previous Busy Status"},
    {0x2C, 0x08, C_CAST(int, FAILURE),       "Previous Task Set Full Status"},
    {0x2C, 0x09, C_CAST(int, FAILURE),       "Previous Reservation Conflict Status"},
    {0x2C, 0x0A, -1,                         "Partition Or Collection Contains User Objects"},
    {0x2C, 0x0B, -1,                         "Not Reserved"},
    {0x2C, 0x0C, C_CAST(int, FAILURE),       "ORWrite Generation Does Not Match"},
    {0x2C, 0x0D, C_CAST(int, FAILURE),       "Reset Write Pointer Not Allowed"},
    {0x2C, 0x0E, C_CAST(int, FAILURE),       "Zone Is Offline"},
    {0x2C, 0x0F, C_CAST(int, FAILURE),       "Stream Not Open"},
    {0x2C, 0x10, -1,                         "Unwritten Data In Zone"},
    {0x2C, 0x11, C_CAST(int, FAILURE),       "Descriptor Format Sense Data Required"},
    {0x2D, 0x00, -1,                         "Overwrite Error On Update In Place"},
    {0x2E, 0x00, C_CAST(int, FAILURE),       "Insufficient Time For Operation"},
    {0x2E, 0x01, C_CAST(int, FAILURE),       "Command Timeout Before Processing"},
    {0x2E, 0x02, C_CAST(int, FAILURE),       "Command Timeout During Processing"},
    {0x2E, 0x03, C_CAST(int, FAILURE),       "Command Timeout During Processing Due To Error Recovery"},
    {0x2F, 0x00, C_CAST(int, FAILURE),       "Commands Cleared By Another Initiator"},
    {0x2F, 0x01, C_CAST(int, FAILURE),       "Commands Cleared By Power Loss Notification"},
    {0x2F, 0x02, C_CAST(int, FAILURE),       "Commands Cleared By Device Server"},
    {0x2F, 0x03, C_CAST(int, FAILURE),       "Some Commands Cleared By Queuing Layer Event"},
    // {0x2F, 0x07, C_CAST(int, FAILURE),       "Space Allocation Failed Write Protect"},
    {0x30, 0x00, C_CAST(int, FAILURE),       "Incompatible Medium Installed"},
    {0x30, 0x01, C_CAST(int, FAILURE),       "Cannot Read Medium - Unknown Format"},
    {0x30, 0x02, C_CAST(int, FAILURE),       "Cannot Read Medium - Incompatible Format"},
    {0x30, 0x03, C_CAST(int, UNKNOWN),       "Cleaning Cartridge Installed"},
    {0x30, 0x04, C_CAST(int, FAILURE),       "Cannot Write Medium - Unknown Format"},
    {0x30, 0x05, C_CAST(int, FAILURE),       "Cannot Write Medium - Incompatible Format"},
    {0x30, 0x06, C_CAST(int, FAILURE),       "Cannot Format Medium - Incompatible Medium"},
    {0x30, 0x07, C_CAST(int, FAILURE),       "Cleaning Failure"},
    {0x30, 0x08, C_CAST(int, FAILURE),       "Cannot Write - Application Code Mismatch"},
    {0x30, 0x09, C_CAST(int, FAILURE),       "Current Session Not Fixated For Append"},
    {0x30, 0x0A, C_CAST(int, FAILURE),       "Cleaning Request Rejected"},
    {0x30, 0x0C, C_CAST(int, FAILURE),       "WORM Medium - Overwrite Attempted"},
    {0x30, 0x0D, C_CAST(int, FAILURE),       "WORM Medium - Integrity Check"},
    {0x30, 0x10, C_CAST(int, FAILURE),       "Medium Not Formatted"},
    {0x30, 0x11, C_CAST(int, FAILURE),       "Incompatible Volume Type"},
    {0x30, 0x12, C_CAST(int, FAILURE),       "Incompatible Volume Qualifier"},
    {0x30, 0x13, C_CAST(int, FAILURE),       "Cleaning Volume Expired"},
    {0x31, 0x00, C_CAST(int, FAILURE),       "Medium Format Corrupted"},
    {0x31, 0x01, C_CAST(int, FAILURE),       "Format Command Failed"},
    {0x31, 0x02, C_CAST(int, FAILURE),       "Zoned Formatting Failed Due To Spare Linking"},
    {0x31, 0x03, C_CAST(int, FAILURE),       "Sanitize Command Failed"},
    {0x32, 0x00, C_CAST(int, FAILURE),       "No Defect Space Location Available"},
    {0x32, 0x01, C_CAST(int, FAILURE),       "Defect List Update Failure"},
    {0x33, 0x00, C_CAST(int, FAILURE),       "Tape Length Error"},
    {0x34, 0x00, C_CAST(int, FAILURE),       "Enclosure Failure"},
    {0x35, 0x00, C_CAST(int, FAILURE),       "Enclosure Services Failure"},
    {0x35, 0x01, C_CAST(int, NOT_SUPPORTED), "Unsupported Enclosure Function"},
    {0x35, 0x02, C_CAST(int, NOT_SUPPORTED), "Enclosure Services Unavailable"},
    {0x35, 0x03, C_CAST(int, FAILURE),       "Enclosure Services Transfer Failure"},
    {0x35, 0x04, C_CAST(int, FAILURE),       "Enclosure Services Transfer Refused"},
    {0x35, 0x05, C_CAST(int, FAILURE),       "Enclosure Services Checksum Failure"},
    {0x36, 0x00, C_CAST(int, FAILURE),       "Ribbon, Ink, Or Toner Failure"},
    {0x37, 0x00, -1,                         "Rounded Parameter"},
    {0x38, 0x00, -1,                         "Event Status Notification"},
    {0x38, 0x02, -1,                         "ESN - Power Management Class Event"},
    {0x38, 0x04, -1,                         "ESN - Media Class Event"},
    {0x38, 0x06, -1,                         "ESN - Device Busy Class Event"},
    {0x38, 0x07, C_CAST(int, FAILURE),       "Thin Provisioning Soft Threshold Reached"},
    {0x39, 0x00, C_CAST(int, NOT_SUPPORTED), "Saving Parameters Not Supported"},
    {0x3A, 0x00, C_CAST(int, FAILURE),       "Medium Not Present"},
    {0x3A, 0x01, C_CAST(int, FAILURE),       "Medium Not Present - Tray Closed"},
    {0x3A, 0x02, C_CAST(int, FAILURE),       "Medium Not Present - Tray Open"},
    {0x3A, 0x03, C_CAST(int, FAILURE),       "Medium Not Present - Loadable"},
    {0x3A, 0x04, C_CAST(int, FAILURE),       "Medium Not Present - Medium Auxilary Memory Accessible"},
    {0x3B, 0x00, C_CAST(int, FAILURE),       "Sequential Positioning Error"},
    {0x3B, 0x01, C_CAST(int, FAILURE),       "Tape Position Error At Beginning-Of-Medium"},
    {0x3B, 0x02, C_CAST(int, FAILURE),       "Tape Position Error At End-Of-Medium"},
    {0x3B, 0x03, C_CAST(int, FAILURE),       "Tape Or Electronic Vertical Forms Unit Not Ready"},
    {0x3B, 0x04, C_CAST(int, FAILURE),       "Slew Failure"},
    {0x3B, 0x05, C_CAST(int, FAILURE),       "Paper Jam"},
    {0x3B, 0x06, C_CAST(int, FAILURE),       "Failed To Sense Top-Of-Form"},
    {0x3B, 0x07, C_CAST(int, FAILURE),       "Failed To Sense Bottom-Of-Form"},
    {0x3B, 0x08, C_CAST(int, FAILURE),       "Reposition Error"},
    {0x3B, 0x09, C_CAST(int, FAILURE),       "Read Past End Of Medium"},
    {0x3B, 0x0A, C_CAST(int, FAILURE),       "Read Past Beginning Of Medium"},
    {0x3B, 0x0B, C_CAST(int, FAILURE),       "Position Past End Of Medium"},
    {0x3B, 0x0C, C_CAST(int, FAILURE),       "Position Past Beginning Of Medium"},
    {0x3B, 0x0D, C_CAST(int, FAILURE),       "Medium Destination Element Full"},
    {0x3B, 0x0E, C_CAST(int, FAILURE),       "Medium Source Element Empty"},
    {0x3B, 0x0F, -1,                         "End Of Medium Reached"},
    {0x3B, 0x11, -1,                         "Medium Magazine Not Accessible"},
    {0x3B, 0x12, -1,                         "Medium Magazine Removed"},
    {0x3B, 0x13, -1,                         "Medium Magazine Inserted"},
    {0x3B, 0x14, -1,                         "Medium Magazine Locked"},
    {0x3B, 0x15, -1,                         "Medium Magazine Unlocked"},
    {0x3B, 0x16, C_CAST(int, FAILURE),       "Mechanical Positioning Or Changer Error"},
    {0x3B, 0x17, C_CAST(int, FAILURE),       "Read Past End Of User Object"},
    {0x3B, 0x18, -1,                         "Element Disabled"},
    {0x3B, 0x19, -1,                         "Element Enabled"},
    {0x3B, 0x1A, -1,                         "Data Transfer Device Removed"},
    {0x3B, 0x1B, -1,                         "Data Transfer Device Inserted"},
    {0x3B, 0x1C, C_CAST(int, FAILURE),       "Too Many Logical Objects On Partition To Supported Operation"},
    {0x3D, 0x00, C_CAST(int, FAILURE),       "Invalid Bits In Identify Message"},
    {0x3E, 0x00, C_CAST(int, FAILURE),       "Logical Unit Has Not Self-Configured Yet"},
    {0x3E, 0x01, C_CAST(int, FAILURE),       "Logical Unit Failure"},
    {0x3E, 0x02, C_CAST(int, FAILURE),       "Timeout On Logical Unit"},
    {0x3E, 0x03, C_CAST(int, FAILURE),       "Logical Unit Failed Self-Test"},
    {0x3E, 0x04, C_CAST(int, FAILURE),       "Logical Unit Unable to Update Self-Test Log"},
    {0x3F, 0x00, C_CAST(int, SUCCESS),       "Target Operating Conditions Have Changed"},
    {0x3F, 0x01, C_CAST(int, SUCCESS),       "Microcode Has Been Changed"},
    {0x3F, 0x02, C_CAST(int, SUCCESS),       "Changed Operation Definition"},
    {0x3F, 0x03, C_CAST(int, SUCCESS),       "Inquiry Data Has Changed"},
    {0x3F, 0x04, C_CAST(int, SUCCESS),       "Component Device Attached"},
    {0x3F, 0x05, C_CAST(int, SUCCESS),       "Device Identifier Changed"},
    {0x3F, 0x06, C_CAST(int, SUCCESS),       "Redundancy Group Created Or Modified"},
    {0x3F, 0x07, C_CAST(int, SUCCESS),       "Redundancy Group Deleted"},
    {0x3F, 0x08, C_CAST(int, SUCCESS),       "Spare Created Or Modified"},
    {0x3F, 0x09, C_CAST(int, SUCCESS),       "Spare Deleted"},
    {0x3F, 0x0A, C_CAST(int, SUCCESS),       "Volume Set Created Or Modified"},
    {0x3F, 0x0B, C_CAST(int, SUCCESS),       "Volume Set Deleted"},
    {0x3F, 0x0C, C_CAST(int, SUCCESS),       "Volume Set Deassigned"},
    {0x3F, 0x0D, C_CAST(int, SUCCESS),       "Volume Set Reassigned"},
    {0x3F, 0x0E, C_CAST(int, SUCCESS),       "Reported LUNs Data Has Changed"},
    {0x3F, 0x0F, C_CAST(int, SUCCESS),       "Echo Buffer Overwritten"},
    {0x3F, 0x10, C_CAST(int, SUCCESS),       "Medium Loadable"},
    {0x3F, 0x11, C_CAST(int, SUCCESS),       "Medium Auxilary Memory Accessible"},
    {0x3F, 0x12, C_CAST(int, SUCCESS),       "iSCSI IP Address Added"},
    {0x3F, 0x13, C_CAST(int, SUCCESS),       "iSCSI IP Address Removed"},
    {0x3F, 0x14, C_CAST(int, SUCCESS),       "iSCSI IP Address Changed"},
    {0x3F, 0x15, C_CAST(int, SUCCESS),       "Inspect Referrals Sense Descriptors"},
    {0x3F, 0x16, C_CAST(int, SUCCESS),       "Microcode Has Been Changed Without Reset"},
    {0x3F, 0x17, C_CAST(int, SUCCESS),       "Zone Transition To Full"},
    {0x3F, 0x18, -1,                         "Bind Completed"},
    {0x3F, 0x19, -1,                         "Bind Redirected"},
    {0x3F, 0x1A, -1,                         "Subsidiary Binding Changed"},
    {0x41, 0x00, C_CAST(int, FAILURE),       "Data Path Failure (Should Use 40NN)"},
    {0x42, 0x00, C_CAST(int, FAILURE),       "Power-on Or Self-Test Failure (Should use 40 NN)"},
    {0x43, 0x00, C_CAST(int, FAILURE),       "Message Error"},
    {0x44, 0x00, C_CAST(int, FAILURE),       "Internal Target Failure"},
    {0x44, 0x01, C_CAST(int, FAILURE),       "Persistent Reservation Information Lost"},
    {0x44, 0x71, C_CAST(int, FAILURE),       "ATA Device Failed Set Features"},
    {0x45, 0x00, C_CAST(int, FAILURE),       "Select Or Reselect Failure"},
    {0x46, 0x00, C_CAST(int, FAILURE),       "Unsuccessful Soft Reset"},
    {0x47, 0x00, C_CAST(int, FAILURE),       "SCSI Parity Error"},
    {0x47, 0x01, C_CAST(int, FAILURE),       "Data Phase CRC Error Detected"},
    {0x47, 0x02, C_CAST(int, FAILURE),       "SCSI Parity Error Detected During ST Data Phase"},
    {0x47, 0x03, C_CAST(int, FAILURE),       "Information Unit uiCRC Error Detected"},
    {0x47, 0x04, C_CAST(int, FAILURE),       "Asynchronous Information Protection Error Detected"},
    {0x47, 0x05, C_CAST(int, FAILURE),       "Protocol Service CRC Error"},
    {0x47, 0x06, C_CAST(int, IN_PROGRESS),   "PHY Test Function In Progress"},
    {0x47, 0x7F, C_CAST(int, FAILURE),       "Some Commands Cleared By ISCSI Protocol Event"},
    {0x48, 0x00, C_CAST(int, SUCCESS),       "Initiator Detected Error Message Received"},
    {0x49, 0x00, C_CAST(int, FAILURE),       "Invalid Message Error"},
    {0x4A, 0x00, C_CAST(int, FAILURE),       "Command Phase Error"},
    {0x4B, 0x00, C_CAST(int, FAILURE),       "Data Phase Error"},
    {0x4B, 0x01, C_CAST(int, FAILURE),       "Invalid Target Port Transfer Tag Received"},
    {0x4B, 0x02, C_CAST(int, FAILURE),       "Too Much Write Data"},
    {0x4B, 0x03, C_CAST(int, FAILURE),       "ACK/NAK Timeout"},
    {0x4B, 0x04, C_CAST(int, SUCCESS),       "NAK Received"},
    {0x4B, 0x05, C_CAST(int, FAILURE),       "Data Offset Error"},
    {0x4B, 0x06, C_CAST(int, FAILURE),       "Initiator Response Timeout"},
    {0x4B, 0x07, C_CAST(int, FAILURE),       "Connection Lost"},
    {0x4B, 0x08, C_CAST(int, FAILURE),       "Data-In Buffer Overflow - Data Buffer Size"},
    {0x4B, 0x09, C_CAST(int, FAILURE),       "Data-In Buffer Overflow - Data Buffer Descriptor Area"},
    {0x4B, 0x0A, C_CAST(int, FAILURE),       "Data-In Buffer Error"},
    {0x4B, 0x0B, C_CAST(int, FAILURE),       "Data-Out Buffer Overflow - Data Buffer Size"},
    {0x4B, 0x0C, C_CAST(int, FAILURE),       "Data-Out Buffer Overflow - Data Buffer Descriptor Area"},
    {0x4B, 0x0D, C_CAST(int, FAILURE),       "Data-Out Buffer Error"},
    {0x4B, 0x0E, C_CAST(int, FAILURE),       "PCIE Fabric Error"},
    {0x4B, 0x0F, C_CAST(int, FAILURE),       "PCIE Completion Timeout"},
    {0x4B, 0x10, C_CAST(int, FAILURE),       "PCIE Completer Abort"},
    {0x4B, 0x11, C_CAST(int, FAILURE),       "PCIE Poisoned TLP Received"},
    {0x4B, 0x12, C_CAST(int, FAILURE),       "PCIE ECRC Check Failed"},
    {0x4B, 0x13, C_CAST(int, NOT_SUPPORTED), "PCIE Unsupported Request"},
    {0x4B, 0x14, C_CAST(int, FAILURE),       "PCIE ACS Violation"},
    {0x4B, 0x15, C_CAST(int, FAILURE),       "PCIE TLP Prefix Blocked"},
    {0x4C, 0x00, C_CAST(int, FAILURE),       "Logical Unit Failed Self-Configuration"},
    {0x4E, 0x00, C_CAST(int, FAILURE),       "Overlapped Commands Attempted"},
    {0x50, 0x00, C_CAST(int, FAILURE),       "Write Append Error"},
    {0x50, 0x01, C_CAST(int, FAILURE),       "Write Append Position Error"},
    {0x50, 0x02, C_CAST(int, FAILURE),       "Position Error Related To Timing"},
    {0x51, 0x00, C_CAST(int, FAILURE),       "Erase Failure"},
    {0x51, 0x01, C_CAST(int, FAILURE),       "Erase Failure - Incomplete Erase Operation Detected"},
    {0x52, 0x00, C_CAST(int, FAILURE),       "Cartridge Fault"},
    {0x53, 0x00, C_CAST(int, FAILURE),       "Media Load Or Eject Failed"},
    {0x53, 0x01, C_CAST(int, FAILURE),       "Unload Tape Failure"},
    {0x53, 0x02, C_CAST(int, FAILURE),       "Medium Removal Prevented"},
    {0x53, 0x03, C_CAST(int, FAILURE),       "Medium Removal Prevented By Data Transfer Element"},
    {0x53, 0x04, C_CAST(int, FAILURE),       "Medium Thread Or Unthread Failure"},
    {0x53, 0x05, C_CAST(int, FAILURE),       "Volume Identifier Invalid"},
    {0x53, 0x06, C_CAST(int, FAILURE),       "Volume Identifier Missing"},
    {0x53, 0x07, C_CAST(int, FAILURE),       "Duplicate Volume Identifier"},
    {0x53, 0x08, C_CAST(int, FAILURE),       "Element Status Unknown"},
    {0x53, 0x09, C_CAST(int, FAILURE),       "Data Transfer Device Error - Load Failed"},
    {0x53, 0x0A, C_CAST(int, FAILURE),       "Data Transfer Device Error - Unload Failed"},
    {0x53, 0x0B, C_CAST(int, FAILURE),       "Data Transfer Device Error - Unload Missing"},
    {0x53, 0x0C, C_CAST(int, FAILURE),       "Data Transfer Device Error - Eject Failed"},
    {0x53, 0x0D, C_CAST(int, FAILURE),       "Data Transfer Device Error - Library Communication Failed"},
    {0x54, 0x00, C_CAST(int, FAILURE),       "SCSI To host System Interface Failure"},
    {0x55, 0x00, C_CAST(int, FAILURE),       "System Resource Failure"},
    {0x55, 0x01, C_CAST(int, FAILURE),       "System Buffer Full"},
    {0x55, 0x02, C_CAST(int, FAILURE),       "Insufficient Reservation Resources"},
    {0x55, 0x03, C_CAST(int, FAILURE),       "Insufficient Resources"},
    {0x55, 0x04, C_CAST(int, FAILURE),       "Insufficient Registration Resources"},
    {0x55, 0x05, C_CAST(int, FAILURE),       "Insufficient Access Control Resources"},
    {0x55, 0x06, C_CAST(int, FAILURE),       "Auxiliary Memory Out Of Space"},
    {0x55, 0x07, C_CAST(int, FAILURE),       "Quota Error"},
    {0x55, 0x08, C_CAST(int, FAILURE),       "Maximum Number Of Supplemental Decryption Keys Exceeded"},
    {0x55, 0x09, C_CAST(int, FAILURE),       "Medium Auxilary Memory Not Accessible"},
    {0x55, 0x0A, C_CAST(int, FAILURE),       "Data Currently Unavailable"},
    {0x55, 0x0B, C_CAST(int, FAILURE),       "Insufficient Power For Operation"},
    {0x55, 0x0C, C_CAST(int, FAILURE),       "Insufficient Resources To Create ROD"},
    {0x55, 0x0D, C_CAST(int, FAILURE),       "Insufficient Resources To Create ROD Token"},
    {0x55, 0x0E, C_CAST(int, FAILURE),       "Insufficient Zone Resources"},
    {0x55, 0x0F, -1,                         "Insufficient Zone Resources To Complete Write"},
    {0x55, 0x10, -1,                         "Maximum Number Of Streams Open"},
    {0x55, 0x11, -1,                         "Insufficient Resources To Bind"},
    {0x57, 0x00, C_CAST(int, FAILURE),       "Unable To Recover Table-Of-Contents"},
    {0x58, 0x00, C_CAST(int, FAILURE),       "Generation Does Not Exist"},
    {0x59, 0x00, -1,                         "Updated Block Read"},
    {0x5A, 0x00, C_CAST(int, FAILURE),       "Operator Request Or State Change Input"},
    {0x5A, 0x01, C_CAST(int, FAILURE),       "Operator Medium Removal Request"},
    {0x5A, 0x02, C_CAST(int, FAILURE),       "Operator Selected Write Protect"},
    {0x5A, 0x03, C_CAST(int, FAILURE),       "Operator Selected Write Permit"},
    {0x5B, 0x00, C_CAST(int, FAILURE),       "Log Exception"},
    {0x5B, 0x01, C_CAST(int, FAILURE),       "Threshold Condition Met"},
    {0x5B, 0x02, C_CAST(int, FAILURE),       "Log Counter At Maximum"},
    {0x5B, 0x03, C_CAST(int, FAILURE),       "Log List Codes Exhausted"},
    {0x5C, 0x00, -1,                         "RPL Status Change"},
    {0x5C, 0x01, C_CAST(int, SUCCESS),       "Spindles Synchronized"},
    {0x5C, 0x02, C_CAST(int, FAILURE),       "Spindles Not Synchronized"},
    {0x5D, 0x00, C_CAST(int, FAILURE),       "Failure Prediction Threshold Exceeded"},
    {0x5D, 0x01, C_CAST(int, FAILURE),       "Media Failure Prediction Threshold Exceeded"},
    {0x5D, 0x02, C_CAST(int, FAILURE),       "Logical Unit Failure Prediction Threshold Exceeded"},
    {0x5D, 0x03, C_CAST(int, FAILURE),       "Spare Area Exhaustion Prediction Threshold Exceeded"},
    {0x5D, 0x10, C_CAST(int, FAILURE),       "Hardware Impending Failure - General Hard Drive Failure"},
    {0x5D, 0x11, C_CAST(int, FAILURE),       "Hardware Impending Failure - Drive Error Rate Too High"},
    {0x5D, 0x12, C_CAST(int, FAILURE),       "Hardware Impending Failure - Data Error Rate Too High"},
    {0x5D, 0x13, C_CAST(int, FAILURE),       "Hardware Impending Failure - Seek Error Rate Too High"},
    {0x5D, 0x14, C_CAST(int, FAILURE),       "Hardware Impending Failure - Too Many Block Reassigns"},
    {0x5D, 0x15, C_CAST(int, FAILURE),       "Hardware Impending Failure - Access Times Too High"},
    {0x5D, 0x16, C_CAST(int, FAILURE),       "Hardware Impending Failure - Start Unit Times Too High"},
    {0x5D, 0x17, C_CAST(int, FAILURE),       "Hardware Impending Failure - Channel Parametrics"},
    {0x5D, 0x18, C_CAST(int, FAILURE),       "Hardware Impending Failure - Controller Detected"},
    {0x5D, 0x19, C_CAST(int, FAILURE),       "Hardware Impending Failure - Throughput Performance"},
    {0x5D, 0x1A, C_CAST(int, FAILURE),       "Hardware Impending Failure - Seek Time Performance"},
    {0x5D, 0x1B, C_CAST(int, FAILURE),       "Hardware Impending Failure - Spin-Up Retry Count"},
    {0x5D, 0x1C, C_CAST(int, FAILURE),       "Hardware Impending Failure - Drive Calibration Retry Count"},
    {0x5D, 0x1D, C_CAST(int, FAILURE),       "Hardware Impending Failure - Power Loss Protection Circuit"},
    {0x5D, 0x20, C_CAST(int, FAILURE),       "Controller Impending Failure - General Hard Drive Failure"},
    {0x5D, 0x21, C_CAST(int, FAILURE),       "Controller Impending Failure - Drive Error Rate Too High"},
    {0x5D, 0x22, C_CAST(int, FAILURE),       "Controller Impending Failure - Data Error Rate Too High"},
    {0x5D, 0x23, C_CAST(int, FAILURE),       "Controller Impending Failure - Seek Error Rate Too High"},
    {0x5D, 0x24, C_CAST(int, FAILURE),       "Controller Impending Failure - Too Many Block Reassigns"},
    {0x5D, 0x25, C_CAST(int, FAILURE),       "Controller Impending Failure - Access Times Too High"},
    {0x5D, 0x26, C_CAST(int, FAILURE),       "Controller Impending Failure - Start Unit Times Too High"},
    {0x5D, 0x27, C_CAST(int, FAILURE),       "Controller Impending Failure - Channel Parametrics"},
    {0x5D, 0x28, C_CAST(int, FAILURE),       "Controller Impending Failure - Controller Detected"},
    {0x5D, 0x29, C_CAST(int, FAILURE),       "Controller Impending Failure - Throughput Performance"},
    {0x5D, 0x2A, C_CAST(int, FAILURE),       "Controller Impending Failure - Seek Time Performance"},
    {0x5D, 0x2B, C_CAST(int, FAILURE),       "Controller Impending Failure - Spin-Up Retry Count"},
    {0x5D, 0x2C, C_CAST(int, FAILURE),       "Controller Impending Failure - Drive Calibration Retry Count"},
    {0x5D, 0x30, C_CAST(int, FAILURE),       "Data Channel Impending Failure - General Hard Drive Failure"},
    {0x5D, 0x31, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Drive Error Rate Too High"},
    {0x5D, 0x32, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Data Error Rate Too High"},
    {0x5D, 0x33, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Seek Error Rate Too High"},
    {0x5D, 0x34, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Too Many Block Reassigns"},
    {0x5D, 0x35, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Access Times Too High"},
    {0x5D, 0x36, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Start Unit Times Too High"},
    {0x5D, 0x37, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Channel Parametrics"},
    {0x5D, 0x38, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Controller Detected"},
    {0x5D, 0x39, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Throughput Performance"},
    {0x5D, 0x3A, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Seek Time Performance"},
    {0x5D, 0x3B, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Spin-Up Retry Count"},
    {0x5D, 0x3C, C_CAST(int, FAILURE),       "Data Channel Impending Failure - Drive Calibration Retry Count"},
    {0x5D, 0x40, C_CAST(int, FAILURE),       "Servo Impending Failure - General Hard Drive Failure"},
    {0x5D, 0x41, C_CAST(int, FAILURE),       "Servo Impending Failure - Drive Error Rate Too High"},
    {0x5D, 0x42, C_CAST(int, FAILURE),       "Servo Impending Failure - Data Error Rate Too High"},
    {0x5D, 0x43, C_CAST(int, FAILURE),       "Servo Impending Failure - Seek Error Rate Too High"},
    {0x5D, 0x44, C_CAST(int, FAILURE),       "Servo Impending Failure - Too Many Block Reassigns"},
    {0x5D, 0x45, C_CAST(int, FAILURE),       "Servo Impending Failure - Access Times Too High"},
    {0x5D, 0x46, C_CAST(int, FAILURE),       "Servo Impending Failure - Start Unit Times Too High"},
    {0x5D, 0x47, C_CAST(int, FAILURE),       "Servo Impending Failure - Channel Parametrics"},
    {0x5D, 0x48, C_CAST(int, FAILURE),       "Servo Impending Failure - Controller Detected"},
    {0x5D, 0x49, C_CAST(int, FAILURE),       "Servo Impending Failure - Throughput Performance"},
    {0x5D, 0x4A, C_CAST(int, FAILURE),       "Servo Impending Failure - Seek Time Performance"},
    {0x5D, 0x4B, C_CAST(int, FAILURE),       "Servo Impending Failure - Spin-Up Retry Count"},
    {0x5D, 0x4C, C_CAST(int, FAILURE),       "Servo Impending Failure - Drive Calibration Retry Count"},
    {0x5D, 0x50, C_CAST(int, FAILURE),       "Spindle Impending Failure - General Hard Drive Failure"},
    {0x5D, 0x51, C_CAST(int, FAILURE),       "Spindle Impending Failure - Drive Error Rate Too High"},
    {0x5D, 0x52, C_CAST(int, FAILURE),       "Spindle Impending Failure - Data Error Rate Too High"},
    {0x5D, 0x53, C_CAST(int, FAILURE),       "Spindle Impending Failure - Seek Error Rate Too High"},
    {0x5D, 0x54, C_CAST(int, FAILURE),       "Spindle Impending Failure - Too Many Block Reassigns"},
    {0x5D, 0x55, C_CAST(int, FAILURE),       "Spindle Impending Failure - Access Times Too High"},
    {0x5D, 0x56, C_CAST(int, FAILURE),       "Spindle Impending Failure - Start Unit Times Too High"},
    {0x5D, 0x57, C_CAST(int, FAILURE),       "Spindle Impending Failure - Channel Parametrics"},
    {0x5D, 0x58, C_CAST(int, FAILURE),       "Spindle Impending Failure - Controller Detected"},
    {0x5D, 0x59, C_CAST(int, FAILURE),       "Spindle Impending Failure - Throughput Performance"},
    {0x5D, 0x5A, C_CAST(int, FAILURE),       "Spindle Impending Failure - Seek Time Performance"},
    {0x5D, 0x5B, C_CAST(int, FAILURE),       "Spindle Impending Failure - Spin-Up Retry Count"},
    {0x5D, 0x5C, C_CAST(int, FAILURE),       "Spindle Impending Failure - Drive Calibration Retry Count"},
    {0x5D, 0x60, C_CAST(int, FAILURE),       "Firmware Impending Failure - General Hard Drive Failure"},
    {0x5D, 0x61, C_CAST(int, FAILURE),       "Firmware Impending Failure - Drive Error Rate Too High"},
    {0x5D, 0x62, C_CAST(int, FAILURE),       "Firmware Impending Failure - Data Error Rate Too High"},
    {0x5D, 0x63, C_CAST(int, FAILURE),       "Firmware Impending Failure - Seek Error Rate Too High"},
    {0x5D, 0x64, C_CAST(int, FAILURE),       "Firmware Impending Failure - Too Many Block Reassigns"},
    {0x5D, 0x65, C_CAST(int, FAILURE),       "Firmware Impending Failure - Access Times Too High"},
    {0x5D, 0x66, C_CAST(int, FAILURE),       "Firmware Impending Failure - Start Unit Times Too High"},
    {0x5D, 0x67, C_CAST(int, FAILURE),       "Firmware Impending Failure - Channel Parametrics"},
    {0x5D, 0x68, C_CAST(int, FAILURE),       "Firmware Impending Failure - Controller Detected"},
    {0x5D, 0x69, C_CAST(int, FAILURE),       "Firmware Impending Failure - Throughput Performance"},
    {0x5D, 0x6A, C_CAST(int, FAILURE),       "Firmware Impending Failure - Seek Time Performance"},
    {0x5D, 0x6B, C_CAST(int, FAILURE),       "Firmware Impending Failure - Spin-Up Retry Count"},
    {0x5D, 0x6C, C_CAST(int, FAILURE),       "Firmware Impending Failure - Drive Calibration Retry Count"},
    {0x5D, 0x73, C_CAST(int, FAILURE),       "Media Impending Failure Endurance Limit Met"},
    {0x5D, 0xFF, C_CAST(int, FAILURE),       "Failure Prediction Threshold Exceeded (False)"},
    {0x5E, 0x00, C_CAST(int, SUCCESS),       "Low Power Condition On"},
    {0x5E, 0x01, C_CAST(int, SUCCESS),       "Idle Condition Activated By Timer"},
    {0x5E, 0x02, C_CAST(int, SUCCESS),       "Standby Condition Activated By Timer"},
    {0x5E, 0x03, C_CAST(int, SUCCESS),       "Idle Condition Activated By Command"},
    {0x5E, 0x04, C_CAST(int, SUCCESS),       "Standby Condition Activated By Command"},
    {0x5E, 0x05, C_CAST(int, SUCCESS),       "Idle_B Condition Activated By Timer"},
    {0x5E, 0x06, C_CAST(int, SUCCESS),       "Idle_B Condition Activated By Command"},
    {0x5E, 0x07, C_CAST(int, SUCCESS),       "Idle_C Condition Activated By Timer"},
    {0x5E, 0x08, C_CAST(int, SUCCESS),       "Idle_C Condition Activated By Command"},
    {0x5E, 0x09, C_CAST(int, SUCCESS),       "Standby_Y Condition Activated By Timer"},
    {0x5E, 0x0A, C_CAST(int, SUCCESS),       "Standby_Y Condition Activated By Command"},
    {0x5E, 0x41, C_CAST(int, SUCCESS),       "Power State Change To Active"},
    {0x5E, 0x42, C_CAST(int, SUCCESS),       "Power State Change To Idle"},
    {0x5E, 0x43, C_CAST(int, SUCCESS),       "Power State Change To Standby"},
    {0x5E, 0x45, C_CAST(int, SUCCESS),       "Power State Change To Sleep"},
    {0x5E, 0x47, C_CAST(int, SUCCESS),       "Power State Change To Device Control"},
    {0x60, 0x00, C_CAST(int, FAILURE),       "Lamp Failure"},
    {0x61, 0x00, C_CAST(int, FAILURE),       "Video ascuisition Error"},
    {0x61, 0x01, C_CAST(int, FAILURE),       "Unable To ascuire Video"},
    {0x61, 0x02, C_CAST(int, FAILURE),       "Out Of Focus"},
    {0x62, 0x00, C_CAST(int, FAILURE),       "Scan Head Positioning Error"},
    {0x63, 0x00, C_CAST(int, FAILURE),       "End Of User Area Encountered On This Track"},
    {0x63, 0x01, C_CAST(int, FAILURE),       "Packet Does Not Fit In Available Space"},
    {0x64, 0x00, C_CAST(int, FAILURE),       "Illegal Mode For This Track"},
    {0x64, 0x01, C_CAST(int, FAILURE),       "Invalid Packet Size"},
    {0x65, 0x00, C_CAST(int, FAILURE),       "Voltage Fault"},
    {0x66, 0x00, C_CAST(int, FAILURE),       "Automatic Document Feeder Cover Up"},
    {0x66, 0x01, C_CAST(int, FAILURE),       "Automatic Document Feeder Lift Up"},
    {0x66, 0x02, C_CAST(int, FAILURE),       "Document Jam In Automatic Document Feeder"},
    {0x66, 0x03, C_CAST(int, FAILURE),       "Document Miss Feed Automatic In Document Feeder"},
    {0x67, 0x00, C_CAST(int, FAILURE),       "Configuration Failure"},
    {0x67, 0x01, C_CAST(int, FAILURE),       "Configuration Of Incapable Logical Units Failed"},
    {0x67, 0x02, C_CAST(int, FAILURE),       "Add Logical Unit Failed"},
    {0x67, 0x03, C_CAST(int, FAILURE),       "Modification Of Logical Unit Failed"},
    {0x67, 0x04, C_CAST(int, FAILURE),       "Exchange Of Logical Unit Failed"},
    {0x67, 0x05, C_CAST(int, FAILURE),       "Remove Of Logical Unit Failed"},
    {0x67, 0x06, C_CAST(int, FAILURE),       "Attachment Of Logical Unit Failed"},
    {0x67, 0x07, C_CAST(int, FAILURE),       "Creation Of Logical Unit Failed"},
    {0x67, 0x08, C_CAST(int, FAILURE),       "Assign Failure Occurred"},
    {0x67, 0x09, C_CAST(int, FAILURE),       "Multiply Assigned Logical Unit"},
    {0x67, 0x0A, C_CAST(int, FAILURE),       "Set Target Port Groups Command Failed"},
    {0x67, 0x0B, C_CAST(int, NOT_SUPPORTED), "ATA Device Feature Not Enabled"},
    {0x67, 0x0C, -1,                         "Command Rejected"},
    {0x67, 0x0D, -1,                         "Explicit Bind Not Allowed"},
    {0x68, 0x00, C_CAST(int, FAILURE),       "Logical Unit Not Configured"},
    {0x68, 0x01, C_CAST(int, FAILURE),       "Subsidiary Logical Unit Not Configured"},
    {0x69, 0x00, C_CAST(int, FAILURE),       "Data Loss On Logical Unit"},
    {0x69, 0x01, C_CAST(int, FAILURE),       "Multiple Logical Unit Failures"},
    {0x69, 0x02, C_CAST(int, FAILURE),       "Parity/Data Mismatch"},
    {0x6A, 0x00, -1,                         "Informational, Refer To Log"},
    {0x6B, 0x00, -1,                         "State Change Has Occurred"},
    {0x6B, 0x01, -1,                         "Redundancy Level Got Better"},
    {0x6B, 0x02, -1,                         "Redundancy Level Got Worse"},
    {0x6C, 0x00, -1,                         "Rebuild Failure Occurred"},
    {0x6D, 0x00, -1,                         "Recalculate Failure Occurred"},
    {0x6E, 0x00, C_CAST(int, FAILURE),       "Command To Logical Unit Failed"},
    {0x6F, 0x00, C_CAST(int, FAILURE),       "Copy Protection Key Exchange Failure - Authentication Failure"},
    {0x6F, 0x01, C_CAST(int, FAILURE),       "Copy Protection Key Exchange Failure - Key Not Present"},
    {0x6F, 0x02, C_CAST(int, FAILURE),       "Copy Protection Key Exchange Failure - Key Not Established"},
    {0x6F, 0x03, C_CAST(int, FAILURE),       "Read Of Scrambled Sector Without Authentication"},
    {0x6F, 0x04, C_CAST(int, FAILURE),       "Media Region Code Is Mismatched To Logical Unit Region"},
    {0x6F, 0x05, C_CAST(int, FAILURE),       "Drive Region Must Be Permanent/Region Reset Count Error"},
    {0x6F, 0x06, C_CAST(int, FAILURE),       "Insufficient Block Count For Binding Nonce Recording"},
    {0x6F, 0x07, C_CAST(int, FAILURE),       "Conflict In Binding Nonce Recording"},
    {0x6F, 0x08, -1,                         "Insufficient Permission"},
    {0x6F, 0x09, -1,                         "Invalid Drive-Host Pairing Server"},
    {0x6F, 0x0A, -1,                         "Drive-Host Pairing Suspended"},
    {0x71, 0x00, -1,                         "Decompression Exception Long Algorithm ID"},
    {0x72, 0x00, C_CAST(int, FAILURE),       "Session Fixation Error"},
    {0x72, 0x01, C_CAST(int, FAILURE),       "Session Fixation Error Writing Lead-In"},
    {0x72, 0x02, C_CAST(int, FAILURE),       "Session Fixation Error Writing Lead-Out"},
    {0x72, 0x03, C_CAST(int, FAILURE),       "Session Fixation Error - Incomplete Track In Session"},
    {0x72, 0x04, C_CAST(int, FAILURE),       "Empty Or Partially Written Reserved Track"},
    {0x72, 0x05, C_CAST(int, FAILURE),       "No More Track Reservations Allowed"},
    {0x72, 0x06, C_CAST(int, FAILURE),       "RMZ Extension Is Not Allowed"},
    {0x72, 0x07, C_CAST(int, FAILURE),       "No More Test Zone Extensions Are Allowed"},
    {0x73, 0x00, C_CAST(int, FAILURE),       "CD Control Error"},
    {0x73, 0x01, -1,                         "Power Calibration Area Almost Full"},
    {0x73, 0x02, -1,                         "Power Calibration Area Is Full"},
    {0x73, 0x03, -1,                         "Power Calibration Area Error"},
    {0x73, 0x04, -1,                         "Program Memory Area Update Failuer"},
    {0x73, 0x05, -1,                         "Program Memory Area Is Full"},
    {0x73, 0x06, -1,                         "RMA/PMA Is Almost Full"},
    {0x73, 0x10, -1,                         "Current Power Calibration Area Almost Full"},
    {0x73, 0x11, -1,                         "Current Power Calibration Area Is Full"},
    {0x73, 0x17, -1,                         "RDZ Is Full"},
    {0x74, 0x00, C_CAST(int, FAILURE),       "Security Error"},
    {0x74, 0x01, C_CAST(int, FAILURE),       "Unable To Decrypt Data"},
    {0x74, 0x02, C_CAST(int, FAILURE),       "Unencrypted Data Encountered While Decrypting"},
    {0x74, 0x03, C_CAST(int, FAILURE),       "Incorrect Data Encryption Key"},
    {0x74, 0x04, C_CAST(int, FAILURE),       "Cryptographic Integrity Validation Failed"},
    {0x74, 0x05, C_CAST(int, FAILURE),       "Error Decrypting Data"},
    {0x74, 0x06, C_CAST(int, FAILURE),       "Unknown Signature Verification Key"},
    {0x74, 0x07, C_CAST(int, FAILURE),       "Encryption Parameters Not Useable"},
    {0x74, 0x08, C_CAST(int, FAILURE),       "Digital Signature Validation Failure"},
    {0x74, 0x09, C_CAST(int, FAILURE),       "Encryption Mode Mismatch On Read"},
    {0x74, 0x0A, C_CAST(int, FAILURE),       "Encrypted Block Not Raw Read Enabled"},
    {0x74, 0x0B, C_CAST(int, FAILURE),       "Incorrect Encryption Parameters"},
    {0x74, 0x0C, C_CAST(int, FAILURE),       "Unable To Decrypt Parameter List"},
    {0x74, 0x0D, C_CAST(int, FAILURE),       "Encryption Algorithm Disabled"},
    {0x74, 0x10, C_CAST(int, FAILURE),       "SA Creation Parameter Value Invalid"},
    {0x74, 0x11, C_CAST(int, FAILURE),       "SA Creation Parameter Value Rejected"},
    {0x74, 0x12, C_CAST(int, FAILURE),       "Invalid SA Usage"},
    {0x74, 0x21, C_CAST(int, FAILURE),       "Data Encryption Configuration Prevented"},
    {0x74, 0x30, C_CAST(int, NOT_SUPPORTED), "SA Creation Parameter Not Supported"},
    {0x74, 0x40, C_CAST(int, FAILURE),       "Authenticaion Failed"},
    {0x74, 0x61, C_CAST(int, FAILURE),       "External Data Encryption Key Manager Access Error"},
    {0x74, 0x62, C_CAST(int, FAILURE),       "External Data Encryption Key Manager Error"},
    {0x74, 0x63, C_CAST(int, FAILURE),       "External Data Encryption Key Not Found"},
    {0x74, 0x64, C_CAST(int, FAILURE),       "External Data Encryption Request Not Authorized"},
    {0x74, 0x6E, C_CAST(int, FAILURE),       "External Data Encryption Control Timeout"},
    {0x74, 0x6F, C_CAST(int, FAILURE),       "External Data Encryption Control Error"},
    {0x74, 0x71, C_CAST(int, FAILURE),       "Logical Unit Access Not Authorized"},
    {0x74, 0x79, C_CAST(int, FAILURE),       "Security Conflict In Translated Device"}
};

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

// Used with bsearch
static int cmp_Asc_Ascq(ascAscqRetDesc* a, ascAscqRetDesc* b)
{
    // compare ASC, if they are same, compare ASCQ
    int ret = a->asc - b->asc;
    if (ret)
    {
        return ret;
    }
    else
    {
        return (a->ascq - b->ascq);
    }
}

int check_Sense_Key_ASC_ASCQ_And_FRU(tDevice *device, uint8_t senseKey, uint8_t asc, uint8_t ascq, uint8_t fru)
{
    int ret = UNKNOWN;//if this gets returned from this function, then something is not right...
    ascAscqRetDesc* asc_ascq_result = 0;
    ascAscqRetDesc asc_ascq_key = {asc, ascq, 0, 0};
    //first check the senseKey
    senseKey = senseKey & 0x0F;//strip off bits that are not part of the sense key
    if (senseKey < sizeof(senseKeyRetDesc) / sizeof(senseKeyRetDesc[0]))
    {
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
        {
            print_sense_key(senseKeyRetDesc[senseKey].desc, senseKey);
        }
        ret = senseKeyRetDesc[senseKey].ret;
    }
    else
    {
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
    case 0x4D:
        if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
        {
            printf("asc & ascq: %" PRIX8 "h - %" PRIX8 "h = Tagged Overlapped Commands. Task Tag = %02" PRIX8 "h\n", asc, ascq, ascq);
        }
        break;
    case 0x70:
        if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
        {
            printf("asc & ascq: %" PRIX8 "h - %" PRIX8 "h = Decompression Exception Short Algorithm ID of %" PRIX8 "", asc, ascq, ascq);
        }
        break;
    default:
        asc_ascq_result = (ascAscqRetDesc*) bsearch(
            &asc_ascq_key, ascAscqLookUp,
            sizeof(ascAscqLookUp) / sizeof(ascAscqLookUp[0]), sizeof(ascAscqLookUp[0]),
            (int (*)(const void*, const void*))cmp_Asc_Ascq
        );
        if (asc_ascq_result)
        {
            if (device->deviceVerbosity >= VERBOSITY_COMMAND_VERBOSE)
            {
                print_acs_ascq(asc_ascq_result->desc, asc, ascq);
            }
            // Return code of -1 means follow return code determined by sense key, do not change
            if (asc_ascq_result->ret > -1)
            {
                ret = (eReturnValues) asc_ascq_result->ret;
            }
        }
        else
        {
            if (asc < 0x80 /* && asc >= 0 */)
            {
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
            }
            else
            {
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
            }
        }
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
    if (!device->drive_info.passThroughHacks.scsiHacks.noVPDPages && !device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage)//if this is set, then the device is known to not support VPD pages, so just skip to the SAT identify
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
                device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;
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
            else
            {
                //setup some defaults that will most likely work for most current products
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16 = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogSubPages = true;
                device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
                //TODO: since we may find SATA or NVMe adapters, we cannot set below due to a union being used. May need to remove that or find another solution
                //device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                //device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                //device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                //device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                //device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
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

bool is_LaCie_USB_Vendor_ID(const char* t10VendorIdent)
{
    if (t10VendorIdent)
    {
        if (strncmp(t10VendorIdent, "LaCie", 5) == 0)
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

bool is_Seagate_USB_Vendor_ID(const char* t10VendorIdent)
{
    if (t10VendorIdent)
    {
        if (strncmp(t10VendorIdent, "Seagate", 7) == 0)
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

bool is_Seagate_SAS_Vendor_ID(const char* t10VendorIdent)
{
    if (t10VendorIdent)
    {
        if (strncmp(t10VendorIdent, "SEAGATE", 7) == 0)
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

void seagate_Serial_Number_Cleanup(const char * t10VendorIdent, char **unitSerialNumber, size_t unitSNSize)
{
    if (t10VendorIdent && unitSerialNumber && *unitSerialNumber)
    {
        if (is_Seagate_USB_Vendor_ID(t10VendorIdent) || is_LaCie_USB_Vendor_ID(t10VendorIdent))
        {
            //sometimes these report with padded zeroes at beginning or end. Detect this and remove the extra zeroes
            //All of these SNs should be only 8 characters long.
            char zeroes[SERIAL_NUM_LEN + 1] = { 0 };//making bigger than needed for now.
            memset(zeroes, '0', SERIAL_NUM_LEN);
            if (strncmp(zeroes, *unitSerialNumber, SEAGATE_SERIAL_NUMBER_LEN) == 0)
            {
                //8 zeroes at the beginning. Strip them off
                memmove(&(*unitSerialNumber)[0], &(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], strlen(*unitSerialNumber) - SEAGATE_SERIAL_NUMBER_LEN);
                memset(&(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], 0, strlen(*unitSerialNumber) - SEAGATE_SERIAL_NUMBER_LEN);
            }
            else if (strncmp(zeroes, &(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], strlen(*unitSerialNumber) - SEAGATE_SERIAL_NUMBER_LEN) == 0)
            {
                //zeroes at the end. Write nulls over them
                //This is not correct, reverse the string as this is a product defect.
                char currentSerialNumber[SERIAL_NUM_LEN + 1] = { 0 };
                char newSerialNumber[SERIAL_NUM_LEN + 1] = { 0 };
                //backup current just in case
                memcpy(currentSerialNumber, (*unitSerialNumber), M_Min(SERIAL_NUM_LEN, unitSNSize));
                for (int8_t curSN = C_CAST(int8_t, M_Min(SERIAL_NUM_LEN, unitSNSize)), newSN = 0; curSN >= 0 && newSN < C_CAST(int8_t, M_Min(SERIAL_NUM_LEN, unitSNSize)); --curSN)
                {
                    if ((*unitSerialNumber)[curSN] != '\0')
                    {
                        newSerialNumber[newSN] = (*unitSerialNumber)[curSN];
                        ++newSN;
                    }
                }
                memcpy((*unitSerialNumber), newSerialNumber, M_Min(SERIAL_NUM_LEN, unitSNSize));
                //At this point the zeroes will now all be at the end.
                if (strncmp(zeroes, (*unitSerialNumber), SEAGATE_SERIAL_NUMBER_LEN) == 0)
                {
                    //zeroes at the beginning. Strip them off
                    memmove(&(*unitSerialNumber)[0], &(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], strlen((*unitSerialNumber)) - SEAGATE_SERIAL_NUMBER_LEN);
                    memset(&(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], 0, strlen((*unitSerialNumber)) - SEAGATE_SERIAL_NUMBER_LEN);
                }
                else
                {
                    //after string reverse, the SN still wasn't right, so go back to stripping off the zeroes from the end.
                    memcpy((*unitSerialNumber), currentSerialNumber, M_Min(SERIAL_NUM_LEN, unitSNSize));
                    memset(&(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], 0, strlen((*unitSerialNumber)) - SEAGATE_SERIAL_NUMBER_LEN);
                }
            }
            else if (strncmp(zeroes, (*unitSerialNumber), 4) == 0)
            {
                //4 zeroes at the beginning. Strip them off
                memmove(&(*unitSerialNumber)[0], &(*unitSerialNumber)[4], strlen((*unitSerialNumber)) - 4);
                memset(&(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], 0, strlen((*unitSerialNumber)) - 4);
            }
            //TODO: Add more cases if we observe other strange reporting behavior.
            //NOTE: For LaCie, it is unknown what format their SNs were before Seagate acquired them, so may need to add different cases for these older LaCie products.
        }
        else if (is_Seagate_SAS_Vendor_ID(t10VendorIdent))
        {
            //SAS Seagate drives have a maximum SN length of 8
            //Other information in here is the PCBA SN
            memset(&(*unitSerialNumber)[SEAGATE_SERIAL_NUMBER_LEN], 0, strlen((*unitSerialNumber)) - SEAGATE_SERIAL_NUMBER_LEN);
        }
    }
    return;
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
            if (device->drive_info.interface_type == SCSI_INTERFACE)
            {
                if (is_Seagate_USB_Vendor_ID(device->drive_info.T10_vendor_ident))
                {
                    device->drive_info.interface_type = USB_INTERFACE;
                }
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
            || is_Seagate_USB_Vendor_ID(device->drive_info.T10_vendor_ident) || strcmp(device->drive_info.T10_vendor_ident, "LaCie") == 0) //This is a special case to run on Seagate and LaCie USB adapters as they may use the ASmedia NVMe chips
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
                    checkForSAT = false;
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
                || is_Seagate_USB_Vendor_ID(device->drive_info.T10_vendor_ident) || strcmp(device->drive_info.T10_vendor_ident, "LaCie") == 0) //This is a special case to run on Seagate and LaCie USB adapters as they may use the Jmicron NVMe chips
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
                            remove_Leading_And_Trailing_Whitespace(device->drive_info.serialNumber);
                            char* snPtr = device->drive_info.serialNumber;
                            const char* t10VIDPtr = device->drive_info.T10_vendor_ident;
                            seagate_Serial_Number_Cleanup(t10VIDPtr, &snPtr, SERIAL_NUM_LEN + 1);
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
                    if (checkForSAT && !device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage)
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
                                remove_Leading_And_Trailing_Whitespace(device->drive_info.serialNumber);
                                char* snPtr = device->drive_info.serialNumber;
                                const char* t10VIDPtr = device->drive_info.T10_vendor_ident;
                                seagate_Serial_Number_Cleanup(t10VIDPtr, &snPtr, SERIAL_NUM_LEN + 1);
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
