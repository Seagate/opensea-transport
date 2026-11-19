// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL-2.0.
//

// The tDevice passthrough-hack accessor definition macros, included by tdevice_accessors.c.
//
// These reuse the EXACT names of the declaration macros from common_public.h (which are #undef'ed
// there after use) and are #defined to take (name, expr) [ENUM: (name, expr, typ)] so the
// declaration and the definition share one signature pattern and only the field expression comes
// from the definition side. KEEP these signatures in sync with the declaration-side macros in
// common_public.h; the header prototypes are the exported API boundary over the DLL/.so.
//
// Setters assign the field AND OR the source into the passThroughHacks.hackSources provenance word
// (latching: bits accumulate, cleared only by the tDevice zero-init at discovery; no M_INLINE:
// these are real exported symbols). Getters are plain field reads.

#include "common_public.h"

    #define TDEV_ACCESSOR_BOOL_SETTER(name, expr)                                                     \
        M_PARAM_RW(1)                                                                                  \
        OPENSEA_TRANSPORT_API void name(tDevice* M_NONNULL device, const bool val, const ePassthroughHackSource source) \
        {                                                                                              \
            expr = val;                                                                                 \
            device->drive_info.passThroughHacks.hackSources |= source;                                  \
        }

    #define TDEV_ACCESSOR_U32_SETTER(name, expr)                                                      \
        M_PARAM_RW(1)                                                                                  \
        OPENSEA_TRANSPORT_API void name(tDevice* M_NONNULL device, const uint32_t val, const ePassthroughHackSource source) \
        {                                                                                              \
            expr = val;                                                                                 \
            device->drive_info.passThroughHacks.hackSources |= source;                                  \
        }

    #define TDEV_ACCESSOR_I8_SETTER(name, expr)                                                       \
        M_PARAM_RW(1)                                                                                  \
        OPENSEA_TRANSPORT_API void name(tDevice* M_NONNULL device, const int8_t val, const ePassthroughHackSource source) \
        {                                                                                              \
            expr = val;                                                                                 \
            device->drive_info.passThroughHacks.hackSources |= source;                                  \
        }

    #define TDEV_ACCESSOR_U8_SETTER(name, expr)                                                       \
        M_PARAM_RW(1)                                                                                  \
        OPENSEA_TRANSPORT_API void name(tDevice* M_NONNULL device, const uint8_t val, const ePassthroughHackSource source) \
        {                                                                                              \
            expr = val;                                                                                 \
            device->drive_info.passThroughHacks.hackSources |= source;                                  \
        }

    #define TDEV_ACCESSOR_ENUM_SETTER(name, expr, typ)                                                \
        M_PARAM_RW(1)                                                                                  \
        OPENSEA_TRANSPORT_API void name(tDevice* M_NONNULL device, const typ val, const ePassthroughHackSource source) \
        {                                                                                              \
            expr = val;                                                                                 \
            device->drive_info.passThroughHacks.hackSources |= source;                                  \
        }

    #define TDEV_ACCESSOR_BOOL_GETTER(name, expr)                                                     \
        M_PARAM_RO(1)                                                                                  \
        OPENSEA_TRANSPORT_API M_PURE_FUNC bool name(const tDevice* M_NONNULL device)                  \
        {                                                                                              \
            return expr;                                                                                \
        }

    #define TDEV_ACCESSOR_U32_GETTER(name, expr)                                                       \
        M_PARAM_RO(1)                                                                                  \
        OPENSEA_TRANSPORT_API M_PURE_FUNC uint32_t name(const tDevice* M_NONNULL device)               \
        {                                                                                              \
            return expr;                                                                                \
        }

    #define TDEV_ACCESSOR_I8_GETTER(name, expr)                                                        \
        M_PARAM_RO(1)                                                                                  \
        OPENSEA_TRANSPORT_API M_PURE_FUNC int8_t name(const tDevice* M_NONNULL device)                 \
        {                                                                                              \
            return expr;                                                                                \
        }

    #define TDEV_ACCESSOR_U8_GETTER(name, expr)                                                        \
        M_PARAM_RO(1)                                                                                  \
        OPENSEA_TRANSPORT_API M_PURE_FUNC uint8_t name(const tDevice* M_NONNULL device)                \
        {                                                                                              \
            return expr;                                                                                \
        }

    #define TDEV_ACCESSOR_ENUM_GETTER(name, expr, typ)                                                 \
        M_PARAM_RO(1)                                                                                  \
        OPENSEA_TRANSPORT_API M_PURE_FUNC typ name(const tDevice* M_NONNULL device)                    \
        {                                                                                              \
            return expr;                                                                                \
        }
