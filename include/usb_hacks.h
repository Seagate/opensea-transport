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

// \file usb_hacks.h
// \brief Set of functions to check or make modifications to commands to work on USB bridges that don't always follow SCSI/SAT specs.

#pragma once

#include "common_public.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    //http://www.linux-usb.org/usb.ids
    typedef enum _eUSBVendorIDs
    {
        evVendorSeagate = 0x0477,
        evVendorOxford = 0x0928,
        evVendorJMicron = 0x152d,
        evVendorDell = 0x413C,
        evVendorUSeagate = 0x0BC2,
        evVendorSamsung = 0x04E8,
        evVendorMaxtor = 0x0D49,
        // Add new enumerations above this line!
        evVendorUnknown = 0
    } eUSBVendorIDs;

    //this function will call the others that attempt to discover passthrough capabilities below
    OPENSEA_TRANSPORT_API void set_ATA_Passthrough_Type(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Passthrough_Type_By_PID_and_VID(tDevice* device)
    //
    //! \brief   Description:  Sets the passthrough type based off theVID/PID combo. If no match is found, set_ATA_Passthrough_Type_By_Trial_And_Error(device) is called to set the passthrough type.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool set_ATA_Passthrough_Type_By_PID_and_VID(tDevice *device);

    OPENSEA_TRANSPORT_API bool set_ATA_Passthrough_Type_By_Inquiry_Data(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  set_ATA_Passthrough_Type_By_Trial_And_Error(tDevice* device)
    //
    //! \brief   Description:  Attempts to figure out the ATA passthrough method of external (USB and IEEE1394) products by issueing identify commands with different passthrough types until success is found
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool set_ATA_Passthrough_Type_By_Trial_And_Error(tDevice* device);

    /*typedef enum _eKnownUSBs
    {
        KNNOWN_USB_NOT_SET,
        KNNOWN_USB_SAMSUNG_S3_PORTABLE_USB2,
        KNNOWN_USB_SEAGATE_BLACKARMOR_DAS25_USB3,
    }eKnownUSBs;*/

    OPENSEA_TRANSPORT_API bool bridge_Does_Report_Unit_Serial_Number(tDevice *device);

    //Very similar to the fill drive info for SCSI, but this has various modifications to work on USB products differently for their quirks
    OPENSEA_TRANSPORT_API int fill_Drive_Info_USB(tDevice *device);

    OPENSEA_TRANSPORT_API bool sct_With_SMART_Commands(tDevice *device);

    //return false to NOT issue this command or it locks up the bridge and possibly the system
    //returns false if RTFRs do not come back so we cannot actually get SMART status
    OPENSEA_TRANSPORT_API bool supports_ATA_Return_SMART_Status_Command(tDevice *device);

    //TODO: Add code to enable this function to automatically set up using SAT12 byte commands whenever possible.
    //Some devices won't use the 16B command on 28bit commands, and require the 12B command...but only some devices are like this.
    //This filter will set the flag in device.drive_info.ata_options.useSat12Byte
    //void use_12_Byte_SAT_Commands(tDevice *device);

#if defined (__cplusplus)
}
#endif