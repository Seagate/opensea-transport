// SPDX-License-Identifier: MPL-2.0

//! \file ciss_helper_func.h
//! \brief Implements low-level support for CISS/CCISS/HPSA/SmartPQI RAIDs
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2021-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(ENABLE_CISS)

#    include "ciss_helper.h"
#    include "common_types.h"
#    include "raid_scan_helper.h"
#    include "scsi_helper.h"
#    include <stdint.h>

#    if defined(__unix__) // this is only done in case someone sets weird defines for Windows even though this isn't
                          // supported
#        include <dirent.h>
#    endif //__unix__

#    if defined(__cplusplus)
extern "C"
{
#    endif //__cplusplus

#    if defined(__unix__) // this is only done in case someone sets weird defines for Windows even though this isn't
                          // supported
    // These filter functions help with scandir on /dev to find ciss compatible devices.
    // NOTE: On Linux, new devices are given /dev/sg, and those need to be tested for support in addition to these
    // filters. NOTE: smartpqi filter is only available on freeBSD. It will return 0 on all other OS's.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) int ciss_filter(const struct dirent* entry);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) int smartpqi_filter(const struct dirent* entry);
#    endif //__unix__

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) M_NULL_TERM_STRING(1) bool is_Supported_ciss_Dev(const char* devName);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) eReturnValues issue_io_ciss_Dev(ScsiIoCtx* scsiIoCtx);

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_NULL_TERM_STRING(1) M_PARAM_RW(2) eReturnValues get_CISS_RAID_Device(const char* filename, tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) eReturnValues close_CISS_RAID_Device(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_PARAM_RW(3)
    eReturnValues get_CISS_RAID_Device_Count(uint32_t*              numberOfDevices,
                                             M_ATTR_UNUSED uint64_t flags,
                                             ptrRaidHandleToScan*   beginningOfList);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_PARAM_RW(5)
    eReturnValues get_CISS_RAID_Device_List(tDevice* const       ptrToDeviceList,
                                            uint32_t             sizeInBytes,
                                            versionBlock         ver,
                                            uint64_t             flags,
                                            ptrRaidHandleToScan* beginningOfList);

#    if defined(__cplusplus)
}
#    endif //__cplusplus

#endif // ENABLE_CISS
