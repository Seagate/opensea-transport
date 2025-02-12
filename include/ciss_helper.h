// SPDX-License-Identifier: MPL-2.0

//! \file cciss_helper.h
//! \brief Defines the constants structures to help with CCISS implementation. This attempts to be generic for any
//! unix-like OS. Windows support is through CSMI.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2021-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(ENABLE_CISS)

#    include "common_types.h"
#    include <stdint.h>

#    if defined(__cplusplus)
extern "C"
{
#    endif //__cplusplus

    // Windows: Support may be through CSMI:
    // https://listi.jpberlin.de/pipermail/smartmontools-support/2018-April/000122.html Look for this registry key:
    // [HKLM\SYSTEM\CurrentControlSet\Services\HpCISSs2\Parameters\Device] "DriverParameter" = "CSMI=None;" Change
    // CSMI=full for full support. Also can set CSMI=limited

// CISS operation codes. This comes from the open CISS spec
#    define CISS_CISS_READ               0xC0
#    define CISS_CISS_WRITE              0xC1
#    define CISS_REPORT_LOGICAL_LUNS_OP  0xC2
#    define CISS_REPORT_PHYSICAL_LUNS_OP 0xC3

#    define CISS_HANDLE_BASE_NAME        "ciss" // all cciss handles coming into the utility will look like this.

#    define CISS_HANDLE_MAX_LENGTH       40

    typedef enum eCISS_DriverEnum
    {
        CISS_DRIVER_UNKNOWN = 0, // unable to detect the driver or it is unknown type that is responding correctly.
        CISS_DRIVER_CISS,        // classic CISS/CCISS driver. Illumos/Solaris cpqqry3 will set this as well.
        CISS_DRIVER_HPSA,        // newer HPSA driver
        CISS_DRIVER_SMARTPQI,    // smartpqi driver is also compatible with some IOCTLs or needs some modifications to
                                 // work.
    } eCISS_Driver;

    typedef struct s_cissDeviceInfo
    {
        int      cissHandle;
        uint32_t driveNumber;
        uint32_t maxTransferSize;     // most likely 64k unless in Linux with big passthrough support.
        uint8_t  physicalLocation[8]; // This comes from a CISS specific CDB and is used when sending commands to
                                      // physical drive locations
        bool bigPassthroughAvailable; // Only available in Linux so far.
        bool smartpqi; // freeBSD has a slightly different set of IOCTLs for this driver, although passthrough is likely
                       // exactly the same. (all structs are marked as packed though)
    } cissDeviceInfo, *ptrCissDeviceInfo;

#    if defined(__cplusplus)
}
#    endif //__cplusplus

#endif // ENABLE_CISS
