//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2020 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ****************************************************************************************** *****************************************************************************

// \file seagate_common_types.h
// \brief Defines the constants structures to help with Seagate common commands/features/other definitions that work on ATA & SCSI

#pragma once

#include "common_public.h"

#if defined(__cplusplus)
extern "C"
{
#endif
    
    //Seagate IDD
    #define SEAGATE_IDD_TIMEOUT UINT16_C(300) //this is set this long to help with drives coming back to ready to receive commands after the first part of IDD

    //Seagate Power Telemetry
    //must measure one of these, otherwise default mode 0 is used and no error reported.
    typedef enum _ePowerTelemetryMeasurementOptions
    {
        PWR_TEL_MEASURE_5V_AND_12V  = 0,
        PWR_TEL_MEASURE_5V          = 5,
        PWR_TEL_MEASURE_12V         = 12
    }ePowerTelemetryMeasurementOptions;

    #define POWER_TELEMETRY_DATA_SIGNATURE "POWERTEL"

    #define MINIMUM_POWER_MEASUREMENT_TIME_SECONDS  UINT16_C(22)    //if less than this, 22 is used and no errors reported.
    #define MAXIMUM_POWER_MEASUREMENT_TIME_SECONDS  UINT16_C(65535) //18.2 hours
    #define POWER_TELEMETRY_REQUEST_MEASUREMENT_VERSION    UINT8_C(1)
    #define POWER_TELEMETRY_MAXIMUM_MEASUREMENTS    UINT16_C(1024)

    //TODO: Add any other definitions to this file that are the same drive interface types

#if defined(__cplusplus)
}
#endif
