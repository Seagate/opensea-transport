//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2020 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file intel_rst_helper.h
// \brief Defines the constants structures to help with CSMI implementation. This tries to be generic for any OS, even though Windows is the only known supported OS (pending what driver you use)

#pragma once

#if defined (ENABLE_INTEL_RST)
#include "intel_rst_defs.h"
#include "scsi_helper.h"

#if !defined (DISABLE_NVME_PASSTHROUGH)
#include "nvme_helper.h"
#endif

#if defined (__cplusplus)
extern "C"
{
#endif

#if !defined (DISABLE_NVME_PASSTHROUGH)
    //NOTE: This function will handle calling appropriate NVMe firmware update function as well
    //NOTE2: This will not issue whatever command you want. Only certain commands are supported by the driver. This function will attempt any command given in case driver updates allow other commands in the future.
    OPENSEA_TRANSPORT_API int send_Intel_NVM_Command(nvmeCmdCtx *nvmeIoCtx);

    OPENSEA_TRANSPORT_API int send_Intel_NVM_Firmware_Download(nvmeCmdCtx *nvmeIoCtx);

    OPENSEA_TRANSPORT_API int send_Intel_NVM_SCSI_Command(ScsiIoCtx *scsiIoCtx);
#endif

    //similar to Win10 function. Sends command to read firmware info and slot info to see if the API is supported or not
    OPENSEA_TRANSPORT_API bool supports_Intel_Firmware_Download(tDevice *device);

    OPENSEA_TRANSPORT_API int send_Intel_Firmware_Download(ScsiIoCtx *scsiIoCtx);

    //TODO: Define other Intel RST unique calls here based on what is in intel_rst_defs.h


#if defined (__cplusplus)
}
#endif

#endif //ENABLE_INTEL_RST