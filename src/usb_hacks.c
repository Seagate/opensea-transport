// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// *****************************************************************************

// \file usb_hacks.c
// \brief Set of functions to check or make modifications to commands to work on USB bridges that don't always follow
// SCSI/SAT specs.

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "ata_helper.h"
#include "ata_helper_func.h"
#include "scsi_helper.h"
#include "scsi_helper_func.h"
#include "usb_hacks.h"
#include <ctype.h> //for checking for printable characters

bool set_ATA_Passthrough_Type_By_Trial_And_Error(tDevice* device)
{
    bool passthroughTypeSet = false;
    if ((device->drive_info.interface_type == USB_INTERFACE ||
         device->drive_info.interface_type == IEEE_1394_INTERFACE) &&
        device->drive_info.drive_type == SCSI_DRIVE)
    {
#if defined(_DEBUG)
        printf("\n\tAttempting to set USB passthrough type with identify commands\n");
#endif
        while (device->drive_info.passThroughHacks.passthroughType != ATA_PASSTHROUGH_UNKNOWN)
        {
            DECLARE_ZERO_INIT_ARRAY(uint8_t, identifyData, LEGACY_DRIVE_SEC_SIZE);
            if (SUCCESS == ata_Identify(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
            {
                // command succeeded so this is most likely the correct pass-through type to use for this device
                // setting drive type while we're in here since it could help with a faster scan
                device->drive_info.drive_type = ATA_DRIVE;
                passthroughTypeSet            = true;
                break;
            }
            else if (SUCCESS == ata_Identify_Packet_Device(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
            {
                // command succeeded so this is most likely the correct pass-through type to use for this device
                // setting drive type while we're in here since it could help with a faster scan
                device->drive_info.drive_type = ATAPI_DRIVE;
                passthroughTypeSet            = true;
                break;
            }
            ++device->drive_info.passThroughHacks.passthroughType;
        }
    }
    return passthroughTypeSet;
}
