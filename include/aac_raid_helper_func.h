// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file aac_raid_helper_func.h
// \brief Defines functions necessary to communicate with physical drives behind an Adaptec RAID (aac, aacraid, arcsas)

#pragma once

#if defined(ENABLE_AAC)

#    include "code_attributes.h"
#    include "common_types.h"

#    include "aac_raid_helper.h"
#    include "raid_scan_helper.h"
#    include "scsi_helper.h"

#    if defined(__cplusplus)
extern "C"
{
#    endif //__cplusplus

    bool is_Supported_aacraid_Dev(const char* devName);

    eReturnValues issue_io_aacraid_Dev(ScsiIoCtx* scsiIoCtx);

    eReturnValues get_AAC_RAID_Device(const char* filename, tDevice* device);

    eReturnValues close_AAC_RAID_Device(tDevice* device);

    eReturnValues get_AAC_RAID_Device_Count(uint32_t*              numberOfDevices,
                                            M_ATTR_UNUSED uint64_t flags,
                                            ptrRaidHandleToScan*   beginningOfList);

    eReturnValues get_AAC_RAID_Device_List(tDevice* const       ptrToDeviceList,
                                           uint32_t             sizeInBytes,
                                           versionBlock         ver,
                                           uint64_t             flags,
                                           ptrRaidHandleToScan* beginningOfList);

#    if defined(__cplusplus)
}
#    endif //__cplusplus

#endif // ENABLE_AAC
