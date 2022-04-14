//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2022 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ****************************************************************************************** *****************************************************************************

// \file seagate_scsi_types.h
// \brief Defines the constants structures to help with Seagate SCSI commands/features/other definitions

#pragma once

#include "common_public.h"
#include "scsi_helper.h"
#include "vendor/seagate/seagate_common_types.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    //inquiry related definitions
    #define SEAGATE_SCSI_VENDOR_ID "SEAGATE "  //This is a case-sensitive match! Includes the space to exactly match inquiry data!
    //Drive SN is part of standard inquiry data in vendor unique area:
    #define SEAGATE_INQ_DRIVE_SN_OFFSET UINT8_C(36)
    #define SEAGATE_INQ_DRIVE_SN_LENGTH UINT8_C(8)
    //Inquiry data also contains copyright notice: (Example: "Copyright (c) XXXX Seagate All rights reserved" where XXXX indicates the year the drive's firmware code was built)
    #define SEAGATE_INQ_COPYRIGHT_OFFSET UINT8_C(96)
    #define SEAGATE_INQ_COPYRIGHT_LENGTH UINT8_C(48)

    typedef enum _eSeagateVPDPages
    {
        SEAGATE_VPD_FIRMWARE_NUMBERS    = 0xC0,
        SEAGATE_VPD_DATE_CODE           = 0xC1,
        SEAGATE_VPD_JUMPER_SETTINGS     = 0xC2,
        SEAGATE_VPD_DEVICE_BEHAVIOR     = 0xC3,
        //TODO: any other Seagate Unique VPD pages
    }eSeagateVPDPages;

    typedef enum _eSeagateModePages
    {
        SEAGATE_MP_UNIT_ATTENTION_PARAMETERS    = 0x00,
        //TODO: any other Seagate unique mode pages
    }eSeagateModePages;

    //Seagate unique protocol specific mode page sub pages
    typedef enum _eSeagateProtocolMPSP
    {
        SEAGATE_MP_SP_SAS_TRANCEIVER_CONTROL_OUT    = 0xE5, //page 19h
        SEAGATE_MP_SP_SAS_TRANCEIVER_CONTROL_IN     = 0xE6, //page 19h
        //TODO: other protocol specific mode page subpages
    }eSeagateProtocolMPSP;

    typedef enum _eSeagateLogPages //not subpages. Unique subpages should be in a different enum, similar to protocol specific MP above
    {
        SEAGATE_LP_CACHE_STATISTICS = 0x37,
        SEAGATE_LP_FACTORY_LOG      = 0x3E,
        SEAGATE_LP_FARM             = 0x3D, //must use subpages.
        //TODO: other Seagate log pages
    }eSeagateLogPages;

    typedef enum _eSeagateFARMSP
    {
        SEAGATE_FARM_SP_CURRENT = 0x03,
        SEAGATE_FARM_SP_FACTORY = 0x04,
    }eSeagateFARMSP;

    typedef enum _eSeagateDiagnosticPages
    {
        SEAGATE_DIAG_IN_DRIVE_DIAGNOSTICS = 0x98,
        SEAGATE_DIAG_POWER_MEASUREMENT    = 0x99,
        //TODO: other Seagate diagnostic pages
    }eSeagateDiagnosticPages;

    typedef enum _eSeagateErrorHistoryBuffers
    {
        SEAGATE_ERR_HIST_POWER_TELEMETRY    = 0x54,
        //TODO: other Seagate error history buffer IDs
    }eSeagateErrorHistoryBuffers;

    //TODO: Seagate unique Sense data information

#if defined(__cplusplus)
}
#endif
