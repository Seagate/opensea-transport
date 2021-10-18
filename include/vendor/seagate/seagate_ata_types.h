//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2021 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ****************************************************************************************** *****************************************************************************

// \file seagate_ata_types.h
// \brief Defines the constants structures to help with Seagate ATA commands/features/other definitions

#pragma once

#include "common_public.h"
#include "ata_helper.h"
#include "vendor/seagate/seagate_common_types.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    typedef enum _eSeagateSCTBISTFuncCodes {
        BIST_SET_SATA_PHY_SPEED     = 0x0003,
        BIST_CHECK_SATA_PHY_SPEED   = 0x0004
    }eSeagateSCTBISTFuncCodes;

    #define SCT_SEAGATE_SPEED_CONTROL SCT_RESERVED_FOR_SATA

    typedef enum _eSSCFeatureState
    {
        SSC_DEFAULT = 0x0000,
        SSC_ENABLED = 0x0001,
        SSC_DISABLED = 0x0002
    }eSSCFeatureState;

    typedef enum _eSeagateSCTFeatureCodes
    {
        SEAGATE_SCT_FEATURE_CONTROL_LOW_CURRENT_SPINUP      = 0xD001,
        SEAGATE_SCT_FEATURE_CONTROL_SPEAD_SPECTRUM_CLOCKING = 0xD002,
    }eSeagateSCTFeatureCodes;

    typedef enum _eSeagateLCSpinLevel
    {
        SEAGATE_LOW_CURRENT_SPINUP_STATE_LOW        = 0x0001,
        SEAGATE_LOW_CURRENT_SPINUP_STATE_DEFAULT    = 0x0002,
        SEAGATE_LOW_CURRENT_SPINUP_STATE_ULTRA_LOW  = 0x0003  //requires firmware that supports this. Only newer products
    }eSeagateLCSpinLevel;

    typedef enum _eSeagateSetFeaturesSubcommands
    {
        SEAGATE_SF_LOW_CURRENT_SPINUP = 0x5B,
        SEAGATE_FEATURE_POWER_BALANCE = 0x5C, //TODO: definitions of enable/disable specific to this feature
        //TODO: other Seagate unique set features codes.
    }eSeagateSetFeaturesSubcommands;

    #define LOW_CURRENT_SPINUP_LBA_MID_SIG  (0xED)
    #define LOW_CURRENT_SPINUP_LBA_HI_SIG   (0xB5)

    #define POWER_BALANCE_LBA_LOW_ENABLE UINT8_C(0x01)
    #define POWER_BALANCE_LBA_LOW_DISABLE UINT8_C(0x02)

    typedef enum _eSeagateLowCurrentSpinupSetFeaturesValues
    {
        SEAGATE_SF_LCS_DISABLE = 0x00,
        SEAGATE_SF_LCS_ENABLE = 0x01,
    }eSeagateLowCurrentSpinupSetFeaturesValues;

    typedef enum _eSeagateLogs
    {
        SEAGATE_ATA_LOG_FIELD_ACCESSIBLE_RELIABILITY_METRICS    = 0xA6,//a.k.a. FARM
        SEAGATE_ATA_LOG_POWER_TELEMETRY                         = 0xB4,
        SEAGATE_ATA_LOG_FARM_TIME_SERIES = 0xC6,
        //Define other Seagate log pages here as necessary
    }eSeagateLogs;

    //sublogs are the features register in this case
    typedef enum _eSeagateFARMSublogs
    {
        SEAGATE_FARM_CURRENT                = 0x00,
        SEAGATE_FARM_GENERATE_NEW_AND_SAVE  = 0x01,
        SEAGATE_FARM_REPORT_SAVED           = 0x02,
        SEAGATE_FARM_REPORT_FACTORY_DATA    = 0x03,
    }eSeagateFARMSublogs;

    //sublogs are the feature registers in this case
    typedef enum _eSeagateFARMTimeSeriesSublogs
    {
        SEAGATE_FARM_TIME_SERIES_DISC  = 0x00,
        SEAGATE_FARM_TIME_SERIES_FLASH = 0x01,
        SEAGATE_FARM_TIME_SERIES_WLTR  = 0x02,
    }eSeagateFARMTimeSeriesSublogs;

    typedef enum _eSeagateSelfTests
    {
        SEAGATE_ST_IDD_SHORT_OFFLINE   = 0x70,
        SEAGATE_ST_IDD_LONG_OFFLINE    = 0x71,
        SEAGATE_ST_IDD_SHORT_CAPTIVE   = 0xD0,
        SEAGATE_ST_IDD_LONG_CAPTIVE    = 0xD1,
    }eSeagateSelfTests;

    //TODO: Any other SMART read data fields that are parsed for certain bits

    //TODO: Any Seagate unique identify data bits that are parsed for certain bits

#if defined(__cplusplus)
}
#endif
