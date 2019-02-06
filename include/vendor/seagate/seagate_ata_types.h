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

// \file seagate_ata_types.h
// \brief Defines the constants structures to help with Seagate ATA commands/features/other definitions

#pragma once

#include "common_public.h"
#include "ata_helper.h"

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
    }eSeagateSetFeaturesSubcommands;

    #define LOW_CURRENT_SPINUP_LBA_MID_SIG  (0xED)
    #define LOW_CURRENT_SPINUP_LBA_HI_SIG   (0xB5)

    typedef enum _eSeagateLowCurrentSpinupSetFeaturesValues
    {
        SEAGATE_SF_LCS_DISABLE = 0x00,
        SEAGATE_SF_LCS_ENABLE = 0x01,
    }eSeagateLowCurrentSpinupSetFeaturesValues;

#if defined(__cplusplus)
}
#endif