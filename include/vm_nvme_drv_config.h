// SPDX-License-Identifier: MPL-2.0

//! \file vm_nvme_drv_config.h
//! \brief Defines the constants structures specific to VMWare Cross compiler for ESXi
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2018-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#ifndef EXC_HANDLER
#    define EXC_HANDLER 1
#endif

#ifndef USE_TIMER
#    define USE_TIMER (1 && EXC_HANDLER)
#endif

#ifndef ENABLE_REISSUE
#    define ENABLE_REISSUE 1
#endif

#define CONG_QUEUE           (1 && EXC_HANDLER)
#define ASYNC_EVENTS_ENABLED (1 && EXC_HANDLER)

// The NVME_ENABLE_STATISTICS macro used to enable the statistics logging
#define NVME_ENABLE_STATISTICS 0

#define NVME_ENABLE_IO_STATS   (1 & NVME_ENABLE_STATISTICS) // Enable or disable the IO stastistics paramenters
#define NVME_ENABLE_IO_STATS_ADDITIONAL                                                                                \
    (1 & NVME_ENABLE_IO_STATS) // Enable or disable the Additional IO statistics parameters
#define NVME_ENABLE_EXCEPTION_STATS                                                                                    \
    (1 & NVME_ENABLE_STATISTICS) // Enable or disable the Exception statistics parameters
