// SPDX-License-Identifier: MPL-2.0

//! \file version.h
//! \brief Defines the versioning information for opensea-transport API
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(__cplusplus)
extern "C"
{
#endif

#define COMBINE_VERSIONS_(x, y, z)      #x "_" #y "_" #z
#define COMBINE_VERSIONS(x, y, z)       COMBINE_VERSIONS_(x, y, z)

#define OPENSEA_TRANSPORT_MAJOR_VERSION 9
#define OPENSEA_TRANSPORT_MINOR_VERSION 0
#define OPENSEA_TRANSPORT_PATCH_VERSION 0

#define OPENSEA_TRANSPORT_VERSION                                                                                      \
    COMBINE_VERSIONS(OPENSEA_TRANSPORT_MAJOR_VERSION, OPENSEA_TRANSPORT_MINOR_VERSION, OPENSEA_TRANSPORT_PATCH_VERSION)

#if defined(__cplusplus)
} // extern "C"
#endif
