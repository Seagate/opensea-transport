// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// *****************************************************************************
//

//! \file adapter_info.h
//! \brief Chip-level adapter profile tables (v1, ID-based lookup).
//!
//! Replaces the per-PID `case` blocks in common_public.c with:
//!  - ONE shared chip-level profile table (`tAdapterProfile[]`), and
//!  - THREE per-interface entry tables (USB / 1394 / PCI) that map an adapter
//!    identity key (from the s_adapterInfo IDs) to a profile + per-entry scalar
//!    overrides, found with `safe_bsearch_context`.
//!
//! Design rulings and the field->accessor map: docs/ADAPTER_PROFILE_SCHEMA.md
//! (locked 2026-11-03; key source + 1394 packing + layering: ruling 21,
//! 2026-11-04). Row data and provenance: docs/ADAPTER_BASE_PROFILES.md +
//! docs/ADAPTER_PROFILE_INVENTORY.md (rulings 1-21).
//!
//! The table data + lookups + applier live in src/adapter_info.c.
#pragma once

#include "common_public.h"
#include <stddef.h>
#include <stdint.h>

//! \brief One-word sort/search key for an adapter entry table. All interfaces are keyed so
//! the plain unsigned magnitude is a total order, which is what safe_bsearch_context compares.
//! The interface family is implicit in WHICH table the row lives in (three tables exist so a
//! USB ID can never collide with a PCI or 1394 ID); there is deliberately no kind bit — the
//! existing eInterfaceType guards at the lookup boundary (see adapter_Get_*_Profile).
typedef uint64_t tAdapterKey;

//! \brief 16-bit-ID key pack for the USB and PCI entry tables (ruling 21a: ids come from the
//! s_adapterInfo struct fields; the uint32 fields are 16-bit values for USB/PCI).
//! vid<<48 | pid<<32 | rev<<16; bits 15..0 reserved for a sub-channel/port flag
//! (e.g. Realtek 9220DP dual-port mode). rev=0 is the family-default row; a rev-scoped
//! row is a separate entry and stage-2 retries with rev=0 after an exact miss.
//! The source fields are unsigned uint32; only a single M_STATIC_CAST to tAdapterKey
//! is required (no intermediate cast needed — the & mask already caps the value width).
#define adapter_Make_USB_Key(vid, pid, rev) \
    ((M_STATIC_CAST(tAdapterKey, (vid)) << 48) | \
     (M_STATIC_CAST(tAdapterKey, (pid)) << 32) | \
     (M_STATIC_CAST(tAdapterKey, ((rev) & 0xFFFFu)) << 16))

//! \brief 24-bit-ID key pack for the 1394 entry table (1394 node ids are 24-bit per the
//! s_adapterInfo comment "IEEE1394 uses uint32's since most of these are 24bit numbers"):
//! vid(bits 63..40) | pid(bits 39..16) | rev(bits 15..0) = 24+24+16 = 64, fully packed.
#define adapter_Make_1394_Key(vid, pid, rev) \
    ((M_STATIC_CAST(tAdapterKey, ((vid) & 0xFFFFFFu)) << 40) | \
     (M_STATIC_CAST(tAdapterKey, ((pid) & 0xFFFFFFu)) << 16) | \
     M_STATIC_CAST(tAdapterKey, ((rev) & 0xFFFFu)))

//! \brief Which of the 8 profile/entry scalars a given row sets (no zero-sentinels: a scalar
//! is only meaningful when its Present bit is set — a value of 0 with its bit on would be
//! legal in principle; today no row needs it).
typedef enum M_FLAG_ENUM eAdapterScalPresent
{
    PHA_S_TURF                  = 1u << 0, // turfValue (u8)
    PHA_S_ATA_MAX_XFER          = 1u << 1, // ATA passthrough max transfer bytes (u32)
    PHA_S_SCSI_MAX_XFER         = 1u << 2, // SCSI max transfer bytes (u32)
    PHA_S_NVME_MAX_XFER         = 1u << 3, // NVMe max transfer bytes (u32)
    PHA_S_READ_BUFF_CMD_SIZE    = 1u << 4, // READ BUFFER command size (i8; negative = unsupported)
    PHA_S_WRITE_VERIF_CMD_SIZE  = 1u << 5, // WRITE AND VERIFY command size (i8)
    PHA_S_WRITE_BUFF_CMD_SIZE   = 1u << 6, // WRITE BUFFER command size (i8)
    PHA_S_WRITE_SAME_CMD_SIZE   = 1u << 7, // WRITE SAME command size (i8)
} eAdapterScalPresent;

//! \brief The 8 scalar slots shared by the profile tail and the entry override block.
//! Same order as the PHA_S_* bits.
typedef struct tAdapterEntryScalars
{
    uint8_t  turfValue;
    uint32_t ataMaxXferLength;
    uint32_t scsiMaxXferLength;
    uint32_t nvmeMaxXferLength;
    int8_t   readBufferCmdSize;
    int8_t   writeAndVerifyCmdSize;
    int8_t   writeBufferCmdSize;
    int8_t   writeSameCmdSize;
} tAdapterEntryScalars;

//! \brief No present bits set: the row's scalars/overrides block carries nothing
//! (see eAdapterScalPresent). Row initializers use this instead of a bare 0.
#define ADAPTER_PRESENT_NONE 0

//! \brief All-default tAdapterEntryScalars initializer (used by rows whose present mask is
//! ADAPTER_PRESENT_NONE; zero here == library default == "not set by this row").
#define ADAPTER_SCALARS_NONE { 0, 0, 0, 0, 0, 0, 0, 0 }

/**
 * \brief Base chip-level passthrough profile (one per distinct flag/enum blob; multiple
 * entries may share a profile — that is the USB-flash shared profile and the chip families).
 *
 * `hacks` is a FULL EMBEDDED COPY of s_passthroughHacks (by value, minus its use at
 * table-definition time — the fill step assigns fields explicitly, never via initializer
 * lists, so C++/MSVC designated-initializer limits do not apply). Copying the struct by
 * value keeps the table in lockstep with the struct: a new hack field added to
 * s_passthroughHacks must gain a matching set_Hack_* line in
 * adapter_Set_Hacks_By_Profile() — never a silent skip. Zero == library default ==
 * "not set by the profile". The applier
 * writes each hack field present in the profile-blob union gated by its profile value
 * (`if (p->hacks.<f>) set_Hack_<F>(device, true|value, src);`), so a field absent from a
 * profile leaves whatever discovery has already set; every set_Hack_* accepts the hack
 * field's exact width(session 2026-11 rulings: TDIR, noVPDPages, noSATVPDPage, rw6, rw12,
 *neration time (session 2026-11 rulings: TDIR, noVPDPages,
 * noSATVPDPage, rw6, rw12, testUnitReadyAfterAnyCommandFailure) and are carried by the
 * code-path defaults / USB-miss safety net instead. Exception: the 12 PRESCSI2 INQ
 * offset/length u8 fields use their documented "If 0, not valid or reported" convention,
 * gated by preSCSI2InqData (ruling 19e) — 0 there means "absent", not "library default".
 */
typedef struct tAdapterProfile
{
    const char* M_NONNULL profileName;   // "prof_initio_3609" / generated family name — verbose output + test labels
    const char* M_NONNULL docRef;        // provenance pointer: which xlsx row / docx line / doc section the values came from
                                         // (printed in the UNVERIFIED note; grep-target for retests)
    eMediaType          mediaType;       // flash rows set MEDIA_SSM_FLASH explicitly
    bool                hasMediaType;    // false = leave the device media-type default alone
    eDriveType          driveType;       // USB-NVMe-bridge rows set NVME_DRIVE (legacy set_Device_DriveType)
    bool                hasDriveType;    // false = leave the device drive-type default alone
    // rows extracted from code WITHOUT a PT setter keep the caller's default PT: the table only overwrites when the old code did.
    bool                hasPassthroughType;
    ePassthroughType    passthroughType;  // exact; NEVER zero-initialized-into-SAT: SAT/NONE/legacy/NVMe are all explicit here

    passthroughHacks    hacks;           // the template: every flag/enum/inq-offset field at its profile value

    uint32_t            scalPresent;     // PHA_S_* bits: which of the 8 scalars below this profile sets (v1 tables: 0 —
                                         // all scalars ride the entry override block)
    tAdapterEntryScalars scalars;        // profile-level scalars (entry overrides win for the same bit)
} tAdapterProfile;

/**
 * \brief One adapter row: identity key -> profile + per-entry scalar overrides.
 *
 * Entries that contribute no flags beyond their profile still exist (a documented identity
 * row), which is what lets the flash alias PIDs and the rev-scoped twins be first-class
 * citizens. `docRef` is the row's provenance string (xlsx docx doc pointer + WHY for
 * UNVERIFIED rows); the verbose identity is profileName + the key's printed vid:pid hex.
 */
typedef struct tAdapterEntry
{
    tAdapterKey           sortedKey;     // adapter_Make_USB_Key / adapter_Make_1394_Key — tables are strictly ascending
    const char* M_NONNULL docRef;        // e.g. "BASE_PROFILES §6, xlsx R40 / crossref2 2310"
    int32_t               profileIndex;  // index into the shared profile table (always >= 0; no no-profile pseudo-index)
    uint32_t              overPresent;   // same PHA_S_* bits; sets the override scalar for this entry only
    tAdapterEntryScalars  overrides;     // per-entry scalars (entry always wins over the profile for the same bit)
    bool                  unverified;    // true -> applier stamps PH_DATA_UNVERIFIED + emits the UNVERIFIED one-liner
    bool                  sat2PostFix;   // legacy post-switch step: the old USB switch stamped FF_FMT_SAT2_FIXED_FORMAT onto
                                          // rows where passthroughHacksSet && live PT==SAT && live fixed-sense==FF_FMT_TEST_WITH_NOP.
                                          // Pre-migration rows mirror that behavior; new hand-added rows always use false.
                                          // Only ever set for USB-family rows (1394/PCI switch functions have no such post-step).
} tAdapterEntry;

//! \brief USB-table lookup. Identity key (vid/pid/rev + validity flags) comes from device->drive_info.adapter_info
//! (ruling 21a: keys are the s_adapterInfo IDs; a NULL/absent VID is a designed miss, not an error). Tests build a
//! zero-filled tDevice with the identity populated. The entry family is guarded by adapter_info.infoType.
//! Stage 1: exact key (vid,pid,reported rev) -> stage 2: retry rev=0 -> stage 3: miss = probe-driven (NOT_SUPPORTED).
//! \return SUCCESS (found; *entryOut set) / NOT_SUPPORTED (miss or inapplicable) / FAILURE (guard trip — impossible if wired right).
M_NODISCARD
eReturnValues adapter_Get_USB_Profile(const tDevice* M_NONNULL device, const tAdapterEntry* M_NULLABLE* M_NONNULL entryOut);

//! \brief 1394-table lookup (guard: IEEE_1394_INTERFACE). Same staged semantics as the USB lookup.
M_NODISCARD
eReturnValues adapter_Get_FireWire_Profile(const tDevice* M_NONNULL device, const tAdapterEntry* M_NULLABLE* M_NONNULL entryOut);

//! \brief PCI-table lookup. Called from the HBA discovery path (SCSI/RAID-interface devices): PCI has no
//! eInterfaceType value today, so the table boundary — not an enum value — is the guard.
M_NODISCARD
eReturnValues adapter_Get_PCI_Profile(const tDevice* M_NONNULL device, const tAdapterEntry* M_NULLABLE* M_NONNULL entryOut);

//! \brief Apply the adapter row + profile for device->drive_info identity to the device hacks: profile
//! media/drive/passthrough types, then profile hack fields (non-default only, via the matching
//! set_Hack_* with the row's provenance), profile scalars iff scalPresent, then entry
//! scalars iff overPresent (entry wins). Runs at discovery, after identity is known and BEFORE any probe.
//! On a USB lookup miss ONLY, the USB safety-net defaults apply (noSATVPDPage +
//! testUnitReadyAfterAnyCommandFailure); a PCI/1394 miss applies nothing (probe-driven
//! "just work"). Idempotent for a given identity.
void adapter_Apply_Profile(tDevice* M_NONNULL device);

//! Debug-build table integrity self-check per family: strictly-increasing keys, unique
//! (vid,pid,rev), profileIndex in range, unverified-row docRef non-empty. Called once lazily at
//! first lookup (no init hook) and by the unit test; the shipped table is static const data,
//! so release builds trust it and skip the check. Returns SUCCESS / NOT_SUPPORTED (a table is
//! empty/missing — legal for v1 PCI/1394 edge cases) / FAILURE; on FAILURE the table is disabled
//! for the session (probe-driven fallback — a corrupt table must never break device access).
#ifdef _DEBUG
M_NODISCARD
eReturnValues adapter_Tables_Self_Check(void);
#endif // _DEBUG

//! \brief Row counts per family (unit tests + verbose logging). size_t (not int32_t):
//! the only comparisons are against size_t loop indexes (M_C4388/-Wsign-compare clean).
typedef struct tAdapterTableCounts
{
    size_t profiles;      // shared profile table size
    size_t usbEntries;
    size_t firewireEntries;
    size_t pciEntries;
} tAdapterTableCounts;

M_NODISCARD M_PURE_FUNC
const tAdapterTableCounts* M_NONNULL adapter_Get_Table_Counts(void);

//! \brief Entry-array access for the given adapter-info family (differential tooling + unit tests):
//! returns the row array and its length is the matching tAdapterTableCounts field. M_NULLPTR for
//! an unknown family. Rows are strictly ascending by sortedKey (self-checked at first lookup).
M_NODISCARD M_PURE_FUNC
const tAdapterEntry* M_NULLABLE adapter_Get_Entry_Array(eAdapterInfoType family);
