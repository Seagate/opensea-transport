// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// *****************************************************************************
//


//! \file adapter_info.c
//! \brief Chip-level adapter profile tables (v1, ID-based lookup) -- HAND-MAINTAINED.
//!
//! Layout: one shared profile table (adapter_Profiles[], tAdapterProfile) + three per-family
//! entry tables (adapter_UsbEntries / adapter_FwEntries / adapter_PciEntries, tAdapterEntry)
//! + adapter_Counts. Lookup is a safe_bsearch_context over the key (stage 1: exact key
//! (vid,pid,reported rev), stage 2: retry rev=0, stage 3: miss = probe-driven); the key
//! macros and the struct semantics are documented in adapter_info.h. A lookup is
//! deliberately NOT gated on the s_adapterInfo validity flags for USB/1394 (the table
//! mirrors the pre-migration switch behavior; absent IDs are zero bits and miss naturally);
//! the PCI lookup mirrors its legacy guard (vendorIDValid).
//!
//! Design: docs/ADAPTER_PROFILE_SCHEMA.md; row data and provenance:
//! docs/ADAPTER_BASE_PROFILES.md / docs/ADAPTER_PROFILE_INVENTORY.md.
//!
//! History: this file was migrated once (Nov 2026) from the legacy per-PID switch-case
//! blocks of common_public.c
//! (set_USB/PCI/IEEE1394_Passthrough_Hacks_By_PID_and_VID) and the duplicate case code was
//! collapsed into the shared profile blobs. That migration is complete -- this file is now
//! the source of truth and is maintained directly by hand.
//!
//! MAINTENANCE INVARIANTS (broken ones are caught by the debug-build self-check, which runs
//! lazily at first lookup; a FAILURE disables the table for the session and leaves the
//! device probe-driven, so a bad edit cannot break device access):
//!   - entries are strictly ascending by sortedKey within each family table, keys unique;
//!   - every profileIndex is in [0, profile count); the array sizes [NNN] match
//!     adapter_Counts exactly; unverified rows carry a non-empty docRef.
//!
//! HOW TO ADD A NEW VID/PID ROW:
//!   1. Find the profile to attach it to: compare the new adapter's behavior against existing
//!      profile blocks (grep by profileName/docRef). Identical behavior -> REUSE that
//!      profile index (dedup is the design goal; expect to rarely need a new profile).
//!      Different behavior -> add a profile first (procedure below).
//!   2. Pick the family table (USB / Fw = 1394 / Pci). Build the key with adapter_Make_USB_Key
//!      (USB and PCI) or adapter_Make_1394_Key (1394, 24-bit IDs). rev=0 is the family
//!      default row; a revision-scoped row is a separate entry.
//!   3. Insert the row at its sorted position inside the array initializer:
//!        { adapter_Make_USB_Key(0xNNNN, 0xNNNN, 0x0000), "docRef", profileIndex,
//!          overPresent, overrides, unverified, sat2PostFix }
//!        - docRef: short grep-able provenance string (e.g. "synth0d7d-nec"); for unverified
//!          rows encode where the behavior was researched so they can be retested later.
//!        - overPresent + overrides: per-entry scalar overrides (PHA_S_* bits; the entry
//!          always wins over the profile for the same bit). ADAPTER_PRESENT_NONE +
//!          ADAPTER_SCALARS_NONE when the row has none.
//!        - unverified: TRUE when the behavior comes from documentation or online research
//!          and no reference unit has confirmed it (runs print a one-time UNVERIFIED note
//!          with the provenance; flip to false once hardware confirms it).
//!        - sat2PostFix: always FALSE for new rows. It mirrors a legacy post-switch step
//!          (stamping FF_FMT_SAT2_FIXED_FORMAT) and only pre-migration rows ever carry it.
//!   4. Bump the family array size [NNN] and the matching adapter_Counts value.
//!   5. Verify on the real unit (or with the identity filled into a test tDevice): the -v 3
//!      run prints "adapter profile: gen_... (key 0x...h)" and the drive behaves as the
//!      profile intends.
//!
//! HOW TO ADD A NEW PROFILE:
//!   1. Append a new fill block at the end of the adapter profile initializer (the next
//!      adapter_Profiles[NNN] index is the new one).
//!   2. Set profileName (convention: gen_<fam>_<vid>_<pid>) and docRef (provenance string).
//!   3. Assign ONLY the non-default values: mediaType/hasMediaType, driveType/hasDriveType,
//!      passthroughType/hasPassthroughType, and the specific hack flags you are setting via
//!      cp->hacks.<field> = <value>. Everything left unassigned stays 0 = library default =
//!      "not set by this profile" (the table is zero-initialized static storage).
//!      Leave scalPresent at 0 -- v1 scalars always ride the entry override block.
//!   4. Do NOT set the six session-stripped hack fields on any profile (returnResponseInfo
//!      NeedsTDIR, noVPDPages, noSATVPDPage, readWrite.rw6, readWrite.rw12,
//!      testUnitReadyAfterAnyCommandFailure); the code-path defaults and the USB-miss
//!      safety net own them.
//!   5. Bump the adapter_Profiles[NNN] size and adapter_Counts.profiles, then reference the
//!      new index from the entries that share it.
//!   6. If the profile needs a hack field that adapter_Set_Hacks_By_Profile() does not yet
//!      emit, add its set_Hack_<Field>(device, p->hacks.<field>, src) line there first --
//!      the applier is the only profile -> device write path.

#include "common_public.h"
#include "adapter_info.h"
#include "sort_and_search.h"
#include "memory_safety.h"

static tAdapterProfile adapter_Profiles[102];

static void adapter_Fill_Profiles(void)
{
    (void)safe_memset(adapter_Profiles, sizeof(adapter_Profiles), 0, sizeof(adapter_Profiles));
    {
        tAdapterProfile* cp = &adapter_Profiles[0];
        cp->profileName = "gen_usb_0bc2_0888";
        cp->docRef = "usbc0bc2:0888";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_NEC;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[1];
        cp->profileName = "gen_usb_0bc2_0500";
        cp->docRef = "usbc0bc2:0500";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.scsiInq.productIDLength = 24;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 8;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[2];
        cp->profileName = "gen_usb_0bc2_0501";
        cp->docRef = "usbc0bc2:0501";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_CYPRESS;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[3];
        cp->profileName = "gen_usb_0bc2_0502";
        cp->docRef = "usbc0bc2:0502";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_TI;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.noMultipleModeCommands = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[4];
        cp->profileName = "gen_usb_0bc2_0503";
        cp->docRef = "usbc0bc2:0503";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_CYPRESS;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.scsiInq.productIDLength = 14;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 16;
        cp->hacks.scsiHacks.scsiInq.vendorIDLength = 8;
        cp->hacks.scsiHacks.scsiInq.vendorIDOffset = 8;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[5];
        cp->profileName = "gen_usb_0bc2_1000";
        cp->docRef = "usbc0bc2:1000";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[6];
        cp->profileName = "gen_usb_0bc2_200d";
        cp->docRef = "usbc0bc2:200d";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[7];
        cp->profileName = "gen_usb_0bc2_2020";
        cp->docRef = "usbc0bc2:2020";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[8];
        cp->profileName = "gen_usb_0bc2_203c";
        cp->docRef = "usbc0bc2:203c";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[9];
        cp->profileName = "gen_usb_0bc2_2030";
        cp->docRef = "usbc0bc2:2030";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[10];
        cp->profileName = "gen_usb_0bc2_2036";
        cp->docRef = "usbc0bc2:2036";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[11];
        cp->profileName = "gen_usb_0bc2_2061";
        cp->docRef = "usbc0bc2:2061";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[12];
        cp->profileName = "gen_usb_0bc2_2064";
        cp->docRef = "usbc0bc2:2064";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[13];
        cp->profileName = "gen_usb_0bc2_207b";
        cp->docRef = "usbc0bc2:207b";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[14];
        cp->profileName = "gen_usb_0bc2_207c";
        cp->docRef = "usbc0bc2:207c";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[15];
        cp->profileName = "gen_usb_0bc2_2088";
        cp->docRef = "usbc0bc2:2088";
        cp->hasPassthroughType = true;
        cp->passthroughType = NVME_PASSTHROUGH_REALTEK;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.noMultipleModeCommands = true;
        cp->hacks.ataPTHacks.possilbyEmulatedNVMe = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.nvmePTHacks.maxTransferLength = 262144;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[16];
        cp->profileName = "gen_usb_0bc2_208f";
        cp->docRef = "usbc0bc2:208f";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[17];
        cp->profileName = "gen_usb_0bc2_20b0";
        cp->docRef = "usbc0bc2:20b0";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[18];
        cp->profileName = "gen_usb_0bc2_20ad";
        cp->docRef = "usbc0bc2:20ad";
        cp->hasPassthroughType = true;
        cp->passthroughType = NVME_PASSTHROUGH_ASMEDIA;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[19];
        cp->profileName = "gen_usb_0bc2_2100";
        cp->docRef = "usbc0bc2:2100";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[20];
        cp->profileName = "gen_usb_0bc2_2101";
        cp->docRef = "usbc0bc2:2101";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[21];
        cp->profileName = "gen_usb_0bc2_2120";
        cp->docRef = "usbc0bc2:2120";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[22];
        cp->profileName = "gen_usb_0bc2_2300";
        cp->docRef = "usbc0bc2:2300";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[23];
        cp->profileName = "gen_usb_0bc2_2320";
        cp->docRef = "usbc0bc2:2320";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[24];
        cp->profileName = "gen_usb_0bc2_2321";
        cp->docRef = "usbc0bc2:2321";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[25];
        cp->profileName = "gen_usb_0bc2_2330";
        cp->docRef = "usbc0bc2:2330";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[26];
        cp->profileName = "gen_usb_0bc2_2332";
        cp->docRef = "usbc0bc2:2332";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[27];
        cp->profileName = "gen_usb_0bc2_2400";
        cp->docRef = "usbc0bc2:2400";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[28];
        cp->profileName = "gen_usb_0bc2_2700";
        cp->docRef = "usbc0bc2:2700";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[29];
        cp->profileName = "gen_usb_0bc2_3000";
        cp->docRef = "usbc0bc2:3000";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[30];
        cp->profileName = "gen_usb_0bc2_3010";
        cp->docRef = "usbc0bc2:3010";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[31];
        cp->profileName = "gen_usb_0bc2_3320";
        cp->docRef = "usbc0bc2:3320";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[32];
        cp->profileName = "gen_usb_0bc2_3330";
        cp->docRef = "usbc0bc2:3330";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[33];
        cp->profileName = "gen_usb_0bc2_3332";
        cp->docRef = "usbc0bc2:3332";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[34];
        cp->profileName = "gen_usb_0bc2_3340";
        cp->docRef = "usbc0bc2:3340";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[35];
        cp->profileName = "gen_usb_0bc2_5020";
        cp->docRef = "usbc0bc2:5020";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[36];
        cp->profileName = "gen_usb_0bc2_5030";
        cp->docRef = "usbc0bc2:5030";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[37];
        cp->profileName = "gen_usb_0bc2_5031";
        cp->docRef = "usbc0bc2:5031";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_DMA;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[38];
        cp->profileName = "gen_usb_0bc2_50a5";
        cp->docRef = "usbc0bc2:50a5";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[39];
        cp->profileName = "gen_usb_0bc2_50a7";
        cp->docRef = "usbc0bc2:50a7";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[40];
        cp->profileName = "gen_usb_0bc2_5130";
        cp->docRef = "usbc0bc2:5130";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[41];
        cp->profileName = "gen_usb_0bc2_6126";
        cp->docRef = "usbc0bc2:6126";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[42];
        cp->profileName = "gen_usb_0bc2_617a";
        cp->docRef = "usbc0bc2:617a";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[43];
        cp->profileName = "gen_usb_0bc2_61b5";
        cp->docRef = "usbc0bc2:61b5";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[44];
        cp->profileName = "gen_usb_0bc2_a003";
        cp->docRef = "usbc0bc2:a003";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[45];
        cp->profileName = "gen_usb_0bc2_a0a4";
        cp->docRef = "usbc0bc2:a0a4";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[46];
        cp->profileName = "gen_usb_0bc2_ab10";
        cp->docRef = "usbc0bc2:ab10";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[47];
        cp->profileName = "gen_usb_0bc2_ab24";
        cp->docRef = "usbc0bc2:ab24";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[48];
        cp->profileName = "gen_usb_0bc2_ab2a";
        cp->docRef = "usbc0bc2:ab2a";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[49];
        cp->profileName = "gen_usb_0bc2_aa1a";
        cp->docRef = "usbc0bc2:aa1a";
        cp->hasDriveType = true;
        cp->driveType = NVME_DRIVE;
        cp->hasPassthroughType = true;
        cp->passthroughType = NVME_PASSTHROUGH_ASMEDIA;
        cp->hacks.ataPTHacks.a1NeverSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[50];
        cp->profileName = "gen_usb_059f_0951";
        cp->docRef = "usbc059f:0951";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_JMICRON;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.noMultipleModeCommands = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 24;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[51];
        cp->profileName = "gen_usb_059f_1010";
        cp->docRef = "usbc059f:1010";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SUNPLUS;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[52];
        cp->profileName = "gen_usb_059f_1043";
        cp->docRef = "usbc059f:1043";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[53];
        cp->profileName = "gen_usb_059f_1053";
        cp->docRef = "usbc059f:1053";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[54];
        cp->profileName = "gen_usb_059f_1064";
        cp->docRef = "usbc059f:1064";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[55];
        cp->profileName = "gen_usb_059f_10cd";
        cp->docRef = "usbc059f:10cd";
        cp->hasDriveType = true;
        cp->driveType = NVME_DRIVE;
        cp->hasPassthroughType = true;
        cp->passthroughType = NVME_PASSTHROUGH_JMICRON;
        cp->hacks.ataPTHacks.a1NeverSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[56];
        cp->profileName = "gen_usb_059f_113b";
        cp->docRef = "usbc059f:113b";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.smartEnabled = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[57];
        cp->profileName = "gen_usb_059f_1105";
        cp->docRef = "usbc059f:1105";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[58];
        cp->profileName = "gen_usb_0d49_5020";
        cp->docRef = "usbc0d49:5020";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.scsiInq.productIDLength = 16;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 16;
        cp->hacks.scsiHacks.scsiInq.productRevLength = 3;
        cp->hacks.scsiHacks.scsiInq.productRevOffset = 32;
        cp->hacks.scsiHacks.scsiInq.vendorIDLength = 8;
        cp->hacks.scsiHacks.scsiInq.vendorIDOffset = 8;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[59];
        cp->profileName = "gen_usb_0d49_7310";
        cp->docRef = "usbc0d49:7310";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[60];
        cp->profileName = "gen_usb_0d49_7410";
        cp->docRef = "usbc0d49:7410";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[61];
        cp->profileName = "gen_usb_0d49_7550";
        cp->docRef = "usbc0d49:7550";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[62];
        cp->profileName = "gen_usb_0928_0008";
        cp->docRef = "usbc0928:0008";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[63];
        cp->profileName = "gen_usb_067b_2571";
        cp->docRef = "usbc067b:2571";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_PROLIFIC;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[64];
        cp->profileName = "gen_usb_067b_2507";
        cp->docRef = "usbc067b:2507";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_JMICRON_PROLIFIC;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.noMultipleModeCommands = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 24;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[65];
        cp->profileName = "gen_usb_0451_9261";
        cp->docRef = "usbc0451:9261";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.cmdDTchecked = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[66];
        cp->profileName = "gen_usb_1955_0102";
        cp->docRef = "usbc1955:0102";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[67];
        cp->profileName = "gen_usb_13fd_1340";
        cp->docRef = "usbc13fd:1340";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[68];
        cp->profileName = "gen_usb_13fd_1340";
        cp->docRef = "usbc13fd:1340";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[69];
        cp->profileName = "gen_usb_174c_2362";
        cp->docRef = "usbc174c:2362";
        cp->hasDriveType = true;
        cp->driveType = NVME_DRIVE;
        cp->hasPassthroughType = true;
        cp->passthroughType = NVME_PASSTHROUGH_ASMEDIA_BASIC;
        cp->hacks.ataPTHacks.a1NeverSupported = true;
        cp->hacks.nvmePTHacks.limitedCommandsSupported.getLogPage = true;
        cp->hacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric = true;
        cp->hacks.nvmePTHacks.limitedPassthroughCapabilities = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[70];
        cp->profileName = "gen_usb_174c_5106";
        cp->docRef = "usbc174c:5106";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_DMA;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[71];
        cp->profileName = "gen_usb_152d_0539";
        cp->docRef = "usbc152d:0539";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.retryPassthroughType = RETRY_JMICRON;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[72];
        cp->profileName = "gen_usb_152d_0551";
        cp->docRef = "usbc152d:0551";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.retryPassthroughType = RETRY_JMICRON;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[73];
        cp->profileName = "gen_usb_152d_0562";
        cp->docRef = "usbc152d:0562";
        cp->hasDriveType = true;
        cp->driveType = NVME_DRIVE;
        cp->hasPassthroughType = true;
        cp->passthroughType = NVME_PASSTHROUGH_JMICRON;
        cp->hacks.ataPTHacks.a1NeverSupported = true;
        cp->hacks.nvmePTHacks.maxTransferLength = 65536;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[74];
        cp->profileName = "gen_usb_152d_0567";
        cp->docRef = "usbc152d:0567";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.checkConditionEmpty = true;
        cp->hacks.ataPTHacks.disableCheckCondition = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[75];
        cp->profileName = "gen_usb_152d_2338";
        cp->docRef = "usbc152d:2338";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.retryPassthroughType = RETRY_JMICRON;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[76];
        cp->profileName = "gen_usb_152d_2339";
        cp->docRef = "usbc152d:2339";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.retryPassthroughType = RETRY_JMICRON;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[77];
        cp->profileName = "gen_usb_152d_2567";
        cp->docRef = "usbc152d:2567";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
        cp->hacks.scsiHacks.securityProtocolWithInc512 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[78];
        cp->profileName = "gen_usb_0bda_9210";
        cp->docRef = "usbc0bda:9210";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.knownRealtekUSB = true;
        cp->hacks.ataPTHacks.noMultipleModeCommands = true;
        cp->hacks.ataPTHacks.possilbyEmulatedNVMe = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[79];
        cp->profileName = "gen_usb_04e8_1f05";
        cp->docRef = "usbc04e8:1f05";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.scsiInq.productIDLength = 11;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 16;
        cp->hacks.scsiHacks.scsiInq.vendorIDLength = 8;
        cp->hacks.scsiHacks.scsiInq.vendorIDOffset = 8;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[80];
        cp->profileName = "gen_usb_04e8_6093";
        cp->docRef = "usbc04e8:6093";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[81];
        cp->profileName = "gen_usb_04e8_61c3";
        cp->docRef = "usbc04e8:61c3";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[82];
        cp->profileName = "gen_usb_090c_1000";
        cp->docRef = "usbc090c:1000p";
        cp->hasMediaType = true;
        cp->mediaType = MEDIA_SSM_FLASH;
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[83];
        cp->profileName = "gen_usb_1e3d_2093";
        cp->docRef = "usbc1e3d:2093p";
        cp->hasMediaType = true;
        cp->mediaType = MEDIA_SSM_FLASH;
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
        cp->hacks.scsiHacks.securityProtocolWithInc512 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[84];
        cp->profileName = "gen_usb_058f_6387";
        cp->docRef = "usbc058f:6387p";
        cp->hasMediaType = true;
        cp->mediaType = MEDIA_SSM_FLASH;
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[85];
        cp->profileName = "gen_usb_0781_5580";
        cp->docRef = "usbc0781:5580";
        cp->hasMediaType = true;
        cp->mediaType = MEDIA_SSM_FLASH;
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_DMA;
        cp->hacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
        cp->hacks.scsiHacks.reportSingleOpCodes = true;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[86];
        cp->profileName = "gen_usb_13fe_3600";
        cp->docRef = "usbc13fe:3600p";
        cp->hasMediaType = true;
        cp->mediaType = MEDIA_SSM_FLASH;
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.scsiInq.productIDLength = 16;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 16;
        cp->hacks.scsiHacks.scsiInq.productRevLength = 3;
        cp->hacks.scsiHacks.scsiInq.productRevOffset = 32;
        cp->hacks.scsiHacks.securityProtocolSupported = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[87];
        cp->profileName = "gen_usb_0d7d_1300";
        cp->docRef = "usbc0d7d:1300p";
        cp->hasMediaType = true;
        cp->mediaType = MEDIA_SSM_FLASH;
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.preSCSI2InqData = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.scsiInq.productIDLength = 16;
        cp->hacks.scsiHacks.scsiInq.productIDOffset = 16;
        cp->hacks.scsiHacks.scsiInq.productRevLength = 3;
        cp->hacks.scsiHacks.scsiInq.productRevOffset = 32;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[88];
        cp->profileName = "gen_usb_1ca1_18ae";
        cp->docRef = "usbc1ca1:18ae";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[89];
        cp->profileName = "gen_usb_2109_0715";
        cp->docRef = "usbc2109:0715";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_USE_UDMA;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportLunsMode = REPORT_LUNS_BROKEN;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[90];
        cp->profileName = "gen_usb_8888_7777";
        cp->docRef = "usbc8888:7777";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.checkConditionEmpty = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.limitedUseTPSIU = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[91];
        cp->profileName = "gen_fw_10b9_5000";
        cp->docRef = "fwc10b9:5000";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[92];
        cp->profileName = "gen_pci_1af4_0008";
        cp->docRef = "pcb1af4:0008";
        cp->hacks.ataPTHacks.ata28BitOnly = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[93];
        cp->profileName = "gen_pci_11f8_8070";
        cp->docRef = "pcb11f8:8070";
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.writeBufferNoDeferredDownload = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[94];
        cp->profileName = "gen_usb_0bc2_2310";
        cp->docRef = "rb14usbc2310";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[95];
        cp->profileName = "gen_usb_0bc2_5075";
        cp->docRef = "rb18e5075";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[96];
        cp->profileName = "gen_usb_0bc2_5033";
        cp->docRef = "rb18e5033";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.singleSectorPIOOnly = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.readWrite.rw16 = true;
        cp->hacks.scsiHacks.reportAllOpCodes = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[97];
        cp->profileName = "gen_usb_0bc2_ab34";
        cp->docRef = "rb18eab34";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[98];
        cp->profileName = "gen_usb_0bc2_ab35";
        cp->docRef = "rb18eab35";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        cp->hacks.ataPTHacks.returnResponseInfoSupported = true;
        cp->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[99];
        cp->profileName = "gen_usb_0d49_3200";
        cp->docRef = "rb18e3200";
        cp->hasPassthroughType = true;
        cp->passthroughType = PASSTHROUGH_NONE;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[100];
        cp->profileName = "gen_usb_04bb_0101";
        cp->docRef = "synth04bb-iodata";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.ata28BitOnly = true;
        cp->hacks.ataPTHacks.dmaModeSelection = ATA_DMA_SEL_NO_DMA;
        cp->hacks.ataPTHacks.dmaNotSupported = true;
        cp->hacks.ataPTHacks.retryPassthroughType = RETRY_IODATA;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noModePages = true;
        cp->hacks.scsiHacks.noReportSupportedOperations = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
        cp->hacks.scsiHacks.syncCacheMode = SYNC_CACHE_10B;
        cp->hacks.scsiHacks.unitSNAvailable = true;
    }
    {
        tAdapterProfile* cp = &adapter_Profiles[101];
        cp->profileName = "gen_usb_0409_0056";
        cp->docRef = "synth0409-nec";
        cp->hasPassthroughType = true;
        cp->passthroughType = ATA_PASSTHROUGH_SAT;
        cp->hacks.ataPTHacks.retryPassthroughType = RETRY_NEC;
        cp->hacks.scsiHacks.noLogPages = true;
        cp->hacks.scsiHacks.noLogSubPages = true;
        cp->hacks.scsiHacks.noModeSubPages = true;
        cp->hacks.scsiHacks.readWrite.available = true;
        cp->hacks.scsiHacks.readWrite.rw10 = true;
    }
}

static const tAdapterEntry adapter_UsbEntries[200] =
{
    { adapter_Make_USB_Key(0x0409, 0x0056, 0x0000), "synth0409-nec", 101, PHA_S_ATA_MAX_XFER, { 0, 131072, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0451, 0x625F, 0x0000), "usbc0451:625f", 3, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0451, 0x9261, 0x0000), "usbc0451:9261", 65, PHA_S_ATA_MAX_XFER, { 0, 65536, 0, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x048D, 0x1172, 0x0000), "usbc048d:1172p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x048D, 0x1176, 0x0000), "usbc048d:1176p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x04B4, 0x6830, 0x0000), "usbc04b4:6830", 2, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04B4, 0x6831, 0x0000), "usbc04b4:6831", 2, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04BB, 0x0101, 0x0000), "synth04bb-iodata", 100, PHA_S_ATA_MAX_XFER, { 0, 512, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x04BB, 0x0201, 0x0000), "synth04bb-iodata", 100, PHA_S_ATA_MAX_XFER, { 0, 512, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x04BB, 0x0319, 0x0000), "synth04bb-iodata", 100, PHA_S_ATA_MAX_XFER, { 0, 512, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x04BB, 0x031A, 0x0000), "synth04bb-iodata", 100, PHA_S_ATA_MAX_XFER, { 0, 512, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x04BB, 0x031B, 0x0000), "synth04bb-iodata", 100, PHA_S_ATA_MAX_XFER, { 0, 512, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x04E8, 0x1F05, 0x0000), "usbc04e8:1f05", 79, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x04E8, 0x2F03, 0x0000), "usbc04e8:2f03", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04E8, 0x2F06, 0x0000), "usbc04e8:2f06", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04E8, 0x5F12, 0x0000), "usbc04e8:5f12", 77, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x04E8, 0x6032, 0x0000), "usbc04e8:6032", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04E8, 0x6093, 0x0000), "usbc04e8:6093", 80, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x04E8, 0x61C3, 0x0000), "usbc04e8:61c3", 81, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 523776, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x04FC, 0x0C05, 0x0000), "usbc04fc:0c05", 51, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04FC, 0x0C15, 0x0000), "usbc04fc:0c15", 51, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x04FC, 0x0C25, 0x0000), "usbc04fc:0c25", 51, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x058F, 0x1234, 0x0000), "usbc058f:1234p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x058F, 0x6387, 0x0000), "usbc058f:6387p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x058F, 0x9380, 0x0000), "usbc058f:9380p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x058F, 0x9381, 0x0000), "usbc058f:9381p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x058F, 0x9382, 0x0000), "usbc058f:9382p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x059F, 0x0951, 0x0000), "usbc059f:0951", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x1010, 0x0000), "usbc059f:1010", 51, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x1019, 0x0000), "usbc059f:1019", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x101D, 0x0000), "usbc059f:101d", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x1021, 0x0000), "usbc059f:1021", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x102A, 0x0000), "usbc059f:102a", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x1043, 0x0000), "usbc059f:1043", 52, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 10, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x1053, 0x0000), "usbc059f:1053", 53, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1064, 0x0000), "usbc059f:1064", 54, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 13, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x1065, 0x0000), "usbc059f:1065", 40, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1072, 0x0000), "usbc059f:1072", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1091, 0x0000), "usbc059f:1091", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x10CD, 0x0000), "usbc059f:10cd", 55, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 13, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x059F, 0x10EE, 0x0000), "usbc059f:10ee", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x10EF, 0x0000), "usbc059f:10ef", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1105, 0x0000), "usbc059f:1105", 57, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1106, 0x0000), "usbc059f:1106", 57, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1107, 0x0000), "usbc059f:1107", 57, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1120, 0x0000), "usbc059f:1120", 13, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1131, 0x0000), "usbc059f:1131", 13, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x1132, 0x0000), "usbc059f:1132", 13, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x059F, 0x113B, 0x0000), "usbc059f:113b", 56, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x067B, 0x2507, 0x0000), "usbc067b:2507", 64, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x067B, 0x2571, 0x0000), "usbc067b:2571", 63, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x067B, 0x2771, 0x0000), "usbc067b:2771", 63, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x067B, 0x2773, 0x0000), "usbc067b:2773", 63, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x067B, 0x2775, 0x0000), "usbc067b:2775", 63, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x067B, 0x3507, 0x0000), "usbc067b:3507", 64, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0781, 0x5575, 0x0000), "usbc0781:5575p", 84, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 16, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0781, 0x5580, 0x0000), "usbc0781:5580", 85, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 1048576, 1048576, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0781, 0x5583, 0x0000), "usbc0781:5583p", 84, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 14, 0, 524288, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x090C, 0x1000, 0x0000), "usbc090c:1000p", 82, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 15, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0928, 0x0008, 0x0000), "usbc0928:0008", 62, ADAPTER_PRESENT_NONE, ADAPTER_SCALARS_NONE, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x0500, 0x0000), "usbc0bc2:0500", 1, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 5, 0, 122880, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x0501, 0x0000), "usbc0bc2:0501", 2, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x0502, 0x0000), "usbc0bc2:0502", 3, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x0503, 0x0000), "usbc0bc2:0503", 4, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x0888, 0x0000), "usbc0bc2:0888", 0, ADAPTER_PRESENT_NONE, ADAPTER_SCALARS_NONE, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x1000, 0x0000), "usbc0bc2:1000", 5, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 9, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x200D, 0x0000), "usbc0bc2:200d", 6, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x200F, 0x0000), "usbc0bc2:200f", 6, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2013, 0x0000), "usbc0bc2:2013", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2020, 0x0000), "usbc0bc2:2020", 7, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2021, 0x0000), "usbc0bc2:2021", 7, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2022, 0x0000), "usbc0bc2:2022", 7, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x202D, 0x0000), "usbc0bc2:202d", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2030, 0x0000), "usbc0bc2:2030", 9, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2031, 0x0000), "usbc0bc2:2031", 9, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2036, 0x0000), "usbc0bc2:2036", 10, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2037, 0x0000), "usbc0bc2:2037", 10, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x203C, 0x0000), "usbc0bc2:203c", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x203E, 0x0000), "usbc0bc2:203e", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 7, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x204B, 0x0000), "usbc0bc2:204b", 6, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x204C, 0x0000), "usbc0bc2:204c", 6, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2060, 0x0000), "usbc0bc2:2060", 7, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2061, 0x0000), "usbc0bc2:2061", 11, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2064, 0x0000), "usbc0bc2:2064", 12, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2065, 0x0000), "usbc0bc2:2065", 12, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x207B, 0x0000), "usbc0bc2:207b", 13, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x207C, 0x0000), "usbc0bc2:207c", 14, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2088, 0x0000), "usbc0bc2:2088", 15, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 4096, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x208C, 0x0000), "usbc0bc2:208c", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x208E, 0x0000), "usbc0bc2:208e", 8, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 0, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x208F, 0x0000), "usbc0bc2:208f", 16, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2092, 0x0000), "usbc0bc2:2092", 17, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x20AD, 0x0000), "usbc0bc2:20ad", 18, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 34, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0x20AE, 0x0000), "usbc0bc2:20ae", 17, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x20AF, 0x0000), "usbc0bc2:20af", 17, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x20B0, 0x0000), "usbc0bc2:20b0", 17, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x20B1, 0x0000), "usbc0bc2:20b1", 17, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2100, 0x0000), "usbc0bc2:2100", 19, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2101, 0x0000), "usbc0bc2:2101", 20, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2120, 0x0000), "usbc0bc2:2120", 21, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2300, 0x0000), "usbc0bc2:2300", 22, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2310, 0x0000), "rb14usbc2310", 94, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 512, 524288, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0BC2, 0x2320, 0x0000), "usbc0bc2:2320", 23, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2321, 0x0000), "usbc0bc2:2321", 24, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2322, 0x0000), "usbc0bc2:2322", 24, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2330, 0x0000), "usbc0bc2:2330", 25, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2332, 0x0000), "usbc0bc2:2332", 26, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 8192, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2344, 0x0000), "usbc0bc2:2344", 10, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2400, 0x0000), "usbc0bc2:2400", 27, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x2700, 0x0000), "usbc0bc2:2700", 28, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 12, 131072, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3000, 0x0000), "usbc0bc2:3000", 29, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3001, 0x0000), "usbc0bc2:3001", 19, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3010, 0x0000), "usbc0bc2:3010", 30, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 122880, 122880, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3300, 0x0000), "usbc0bc2:3300", 20, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 17, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3320, 0x0000), "usbc0bc2:3320", 31, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3330, 0x0000), "usbc0bc2:3330", 32, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3332, 0x0000), "usbc0bc2:3332", 33, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 31, 1024, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x3340, 0x0000), "usbc0bc2:3340", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5020, 0x0000), "usbc0bc2:5020", 35, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5021, 0x0000), "usbc0bc2:5021", 35, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5030, 0x0000), "usbc0bc2:5030", 36, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5031, 0x0000), "usbc0bc2:5031", 37, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65024, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5033, 0x0000), "rb18e5033", 96, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 512, 524288, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0BC2, 0x5060, 0x0000), "usbc0bc2:5060", 35, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5070, 0x0000), "usbc0bc2:5070", 35, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5071, 0x0000), "usbc0bc2:5071", 35, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5075, 0x0000), "rb18e5075", 95, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 16, 65536, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0BC2, 0x50A2, 0x0000), "rb18e50a2", 96, (PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 0, 512, 524288, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0BC2, 0x50A5, 0x0000), "usbc0bc2:50a5", 38, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x50A7, 0x0000), "usbc0bc2:50a7", 39, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5130, 0x0000), "usbc0bc2:5130", 40, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 12, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x5170, 0x0000), "usbc0bc2:5170", 40, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x6126, 0x0000), "usbc0bc2:6126", 41, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x617A, 0x0000), "usbc0bc2:617a", 42, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 2, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0x61B5, 0x0000), "usbc0bc2:61b5", 43, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xA003, 0x0000), "usbc0bc2:a003", 44, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 12, 512, 209920, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xA013, 0x0000), "usbc0bc2:a013", 40, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xA014, 0x0000), "usbc0bc2:a014", 42, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 3, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xA0A4, 0x0000), "usbc0bc2:a0a4", 45, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xA313, 0x0000), "usbc0bc2:a313", 40, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xA314, 0x0000), "usbc0bc2:a314", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 12, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAA17, 0x0000), "usbc0bc2:aa17", 49, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 33, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0xAA1A, 0x0000), "usbc0bc2:aa1a", 49, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 33, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0BC2, 0xAB00, 0x0000), "usbc0bc2:ab00", 23, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB01, 0x0000), "usbc0bc2:ab01", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 8, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB02, 0x0000), "usbc0bc2:ab02", 23, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB10, 0x0000), "usbc0bc2:ab10", 46, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 8192, 82944, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB20, 0x0000), "usbc0bc2:ab20", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB21, 0x0000), "usbc0bc2:ab21", 46, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 512, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB24, 0x0000), "usbc0bc2:ab24", 47, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB2A, 0x0000), "usbc0bc2:ab2a", 48, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 33, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB31, 0x0000), "usbc0bc2:ab31", 46, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 8192, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB34, 0x0000), "rb18eab34", 97, PHA_S_TURF, { 5, 0, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0BC2, 0xAB35, 0x0000), "rb18eab35", 98, ADAPTER_PRESENT_NONE, ADAPTER_SCALARS_NONE, true, false },
    { adapter_Make_USB_Key(0x0BC2, 0xAB38, 0x0000), "usbc0bc2:ab38", 7, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 524288, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAB80, 0x0000), "usbc0bc2:ab80", 11, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BC2, 0xAC30, 0x0000), "usbc0bc2:ac30", 10, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0BDA, 0x9210, 0x0000), "usbc0bda:9210", 78, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 4096, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0D49, 0x3200, 0x0000), "rb18e3200", 99, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 8, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0D49, 0x5020, 0x0000), "usbc0d49:5020", 58, PHA_S_TURF, { 13, 0, 0, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x0D49, 0x7250, 0x0000), "rb18e7250", 99, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 7, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0D49, 0x7310, 0x0000), "usbc0d49:7310", 59, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0D49, 0x7410, 0x0000), "usbc0d49:7410", 60, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0D49, 0x7550, 0x0000), "usbc0d49:7550", 61, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x0D7D, 0x110E, 0x0000), "synth0d7d-nec", 101, PHA_S_ATA_MAX_XFER, { 0, 131072, 0, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x0D7D, 0x1300, 0x0000), "usbc0d7d:1300p", 87, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 33, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x13FD, 0x1340, 0x0000), "usbc13fd:1340", 67, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 12, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x13FD, 0x1340, 0x0202), "usbc13fd:1340", 68, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 12, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x13FE, 0x3600, 0x0000), "usbc13fe:3600p", 86, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 4, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x152D, 0x0539, 0x0000), "usbc152d:0539", 71, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x152D, 0x0539, 0x0100), "usbc152d:0539", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x0551, 0x0000), "usbc152d:0551", 72, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 130560, 512512, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x152D, 0x0562, 0x0000), "usbc152d:0562", 73, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 33, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x0567, 0x0000), "usbc152d:0567", 74, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 11, 130560, 1048576, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x152D, 0x0583, 0x0000), "usbc152d:0583", 73, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 33, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2329, 0x0000), "usbc152d:2329", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2336, 0x0000), "usbc152d:2336", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2337, 0x0000), "usbc152d:2337", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2338, 0x0000), "usbc152d:2338", 75, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 122880, 122880, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x152D, 0x2339, 0x0000), "usbc152d:2339", 76, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x152D, 0x2352, 0x0000), "usbc152d:2352", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2509, 0x0000), "usbc152d:2509", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2566, 0x0000), "usbc152d:2566", 50, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 13, 65024, 65535, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x152D, 0x2567, 0x0000), "usbc152d:2567", 77, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x174C, 0x2362, 0x0000), "usbc174c:2362", 69, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 33, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x174C, 0x5106, 0x0000), "usbc174c:5106", 70, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 15, 524288, 122880, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x174C, 0x55AA, 0x0000), "usbc174c:55aa", 34, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 14, 130560, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x1955, 0x0102, 0x0000), "usbc1955:0102", 66, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 17, 65536, 65536, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x1BCF, 0x0C31, 0x0000), "usbc1bcf:0c31", 51, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 6, 65536, 65536, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x1CA1, 0x18AE, 0x0000), "usbc1ca1:18ae", 88, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 17, 0, 1048576, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_USB_Key(0x1E3D, 0x1172, 0x0000), "usbc1e3d:1172p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x1176, 0x0000), "usbc1e3d:1176p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x1234, 0x0000), "usbc1e3d:1234p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x2093, 0x0000), "usbc1e3d:2093p", 83, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 10, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x6387, 0x0000), "usbc1e3d:6387p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x9380, 0x0000), "usbc1e3d:9380p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x9381, 0x0000), "usbc1e3d:9381p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x1E3D, 0x9382, 0x0000), "usbc1e3d:9382p", 84, PHA_S_SCSI_MAX_XFER, { 0, 0, 65536, 0, 0, 0, 0, 0 }, true, false },
    { adapter_Make_USB_Key(0x2109, 0x0715, 0x0000), "usbc2109:0715", 89, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 10, 292352, 524288, 0, 0, 0, 0, 0 }, false, true },
    { adapter_Make_USB_Key(0x8888, 0x7777, 0x0000), "usbc8888:7777", 90, (PHA_S_TURF | PHA_S_ATA_MAX_XFER | PHA_S_SCSI_MAX_XFER), { 34, 125440, 524288, 0, 0, 0, 0, 0 }, false, true },
};

static const tAdapterEntry adapter_FwEntries[3] =
{
    { adapter_Make_1394_Key(0x0500, 0x0001, 0x0000), "fwc0500:0001", 91, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 4, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_1394_Key(0x0BC2, 0x0000, 0x0000), "fwc0bc2:0000", 91, (PHA_S_TURF | PHA_S_SCSI_MAX_XFER), { 5, 0, 524288, 0, 0, 0, 0, 0 }, false, false },
    { adapter_Make_1394_Key(0x10B9, 0x5000, 0x0000), "fwc10b9:5000", 91, PHA_S_TURF, { 6, 0, 0, 0, 0, 0, 0, 0 }, false, false },
};

static const tAdapterEntry adapter_PciEntries[2] =
{
    { adapter_Make_USB_Key(0x11F8, 0x8070, 0x0000), "pcb11f8:8070", 93, ADAPTER_PRESENT_NONE, ADAPTER_SCALARS_NONE, false, false },
    { adapter_Make_USB_Key(0x1AF4, 0x0008, 0x0000), "pcb1af4:0008", 92, ADAPTER_PRESENT_NONE, ADAPTER_SCALARS_NONE, false, false },
};

static const tAdapterTableCounts adapter_Counts = { 102, 200, 3, 2 };

static int adapter_Compare_EntryKey(const void* M_NONNULL keyPointer, const void* M_NONNULL entryPointer, void* M_NONNULL context)
{
    const tAdapterKey target = *M_STATIC_CAST(const tAdapterKey*, keyPointer);
    const tAdapterEntry* entry = M_STATIC_CAST(const tAdapterEntry*, entryPointer);
    (void)context;
    // safe_bsearch_context passes (key, element) and treats cmp<0 as 'key < element'.
    if (target < entry->sortedKey)
    {
        return -1;
    }
    if (target > entry->sortedKey)
    {
        return 1;
    }
    return 0;
}

static int adapter_Tables_State = 0; // 0 = uninit, 1 = ready, -1 = disabled

static int adapter_Tables_EnsureReady(void)
{
    if (adapter_Tables_State == 0)
    {
        adapter_Fill_Profiles();
#ifdef _DEBUG
        // structural validation of the shipped table (debug builds only): a failure
        // disables the table for this session so discovery stays probe-driven
        adapter_Tables_State = (adapter_Tables_Self_Check() == SUCCESS) ? 1 : -1;
#else
        adapter_Tables_State = 1;
#endif
    }
    return adapter_Tables_State;
}

static eReturnValues adapter_BSearch_Entry(const tAdapterKey key, const tAdapterEntry* M_NONNULL table, size_t count, const tAdapterEntry* M_NULLABLE* M_NONNULL out)
{
    const tAdapterEntry* hit = M_STATIC_CAST(const tAdapterEntry*, safe_bsearch_context(&key, M_STATIC_CAST(const void*, table), M_STATIC_CAST(rsize_t, count), M_STATIC_CAST(rsize_t, sizeof(table[0])), adapter_Compare_EntryKey, M_NULLPTR));
    if (hit == M_NULLPTR)
    {
        return NOT_SUPPORTED;
    }
    *out = hit;
    return SUCCESS;
}

eReturnValues adapter_Get_USB_Profile(const tDevice* M_NONNULL device, const tAdapterEntry* M_NULLABLE* M_NONNULL entryOut)
{
    const struct s_adapterInfo* ai = &device->drive_info.adapter_info;
    tAdapterKey key;
    if (adapter_Tables_EnsureReady() != 1)
    {
        return NOT_SUPPORTED;
    }
    if (ai->infoType != ADAPTER_INFO_USB || get_Device_InterfaceType(device) != USB_INTERFACE)
    {
        return NOT_SUPPORTED;
    }

    key = adapter_Make_USB_Key(ai->vendorID, ai->productID, ai->revision);
    if (adapter_BSearch_Entry(key, adapter_UsbEntries, sizeof(adapter_UsbEntries)/(sizeof(adapter_UsbEntries[0])), entryOut) == SUCCESS)
    {
        return SUCCESS;
    }
    if (ai->revision != 0)
    {
        key = adapter_Make_USB_Key(ai->vendorID, ai->productID, 0);
        if (adapter_BSearch_Entry(key, adapter_UsbEntries, sizeof(adapter_UsbEntries)/(sizeof(adapter_UsbEntries[0])), entryOut) == SUCCESS)
        {
            return SUCCESS;
        }
    }
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_VERBOSE, "adapter profile miss: vid %04Xh pid %04Xh rev %04Xh - probe-driven discovery applies\n", ai->vendorID, ai->productID, ai->revision);
    return NOT_SUPPORTED;
}

eReturnValues adapter_Get_FireWire_Profile(const tDevice* M_NONNULL device, const tAdapterEntry* M_NULLABLE* M_NONNULL entryOut)
{
    const struct s_adapterInfo* ai = &device->drive_info.adapter_info;
    tAdapterKey key;
    if (adapter_Tables_EnsureReady() != 1)
    {
        return NOT_SUPPORTED;
    }
    if (ai->infoType != ADAPTER_INFO_IEEE1394 || get_Device_InterfaceType(device) != IEEE_1394_INTERFACE)
    {
        return NOT_SUPPORTED;
    }

    key = adapter_Make_1394_Key(ai->vendorID, ai->productID, ai->revision);
    if (adapter_BSearch_Entry(key, adapter_FwEntries, sizeof(adapter_FwEntries)/(sizeof(adapter_FwEntries[0])), entryOut) == SUCCESS)
    {
        return SUCCESS;
    }
    if (ai->revision != 0)
    {
        key = adapter_Make_1394_Key(ai->vendorID, ai->productID, 0);
        if (adapter_BSearch_Entry(key, adapter_FwEntries, sizeof(adapter_FwEntries)/(sizeof(adapter_FwEntries[0])), entryOut) == SUCCESS)
        {
            return SUCCESS;
        }
    }
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_VERBOSE, "adapter profile miss: vid %04Xh pid %04Xh rev %04Xh - probe-driven discovery applies\n", ai->vendorID, ai->productID, ai->revision);
    return NOT_SUPPORTED;
}

eReturnValues adapter_Get_PCI_Profile(const tDevice* M_NONNULL device, const tAdapterEntry* M_NULLABLE* M_NONNULL entryOut)
{
    const struct s_adapterInfo* ai = &device->drive_info.adapter_info;
    tAdapterKey key;
    if (adapter_Tables_EnsureReady() != 1)
    {
        return NOT_SUPPORTED;
    }
    if (ai->infoType != ADAPTER_INFO_PCI || !ai->vendorIDValid)
    {
        return NOT_SUPPORTED;
    }

    key = adapter_Make_USB_Key(ai->vendorID, ai->productID, ai->revision);
    if (adapter_BSearch_Entry(key, adapter_PciEntries, sizeof(adapter_PciEntries)/(sizeof(adapter_PciEntries[0])), entryOut) == SUCCESS)
    {
        return SUCCESS;
    }
    if (ai->revision != 0)
    {
        key = adapter_Make_USB_Key(ai->vendorID, ai->productID, 0);
        if (adapter_BSearch_Entry(key, adapter_PciEntries, sizeof(adapter_PciEntries)/(sizeof(adapter_PciEntries[0])), entryOut) == SUCCESS)
        {
            return SUCCESS;
        }
    }
    print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_VERBOSE, "adapter profile miss: vid %04Xh pid %04Xh rev %04Xh - probe-driven discovery applies\n", ai->vendorID, ai->productID, ai->revision);
    return NOT_SUPPORTED;
}

static void adapter_Set_Hacks_By_Profile(tDevice* M_NONNULL device, int32_t profileIndex, const ePassthroughHackSource src)
{
    const tAdapterProfile* p = &adapter_Profiles[profileIndex];
    set_Hack_ATA_A1_Never_Supported(device, p->hacks.ataPTHacks.a1NeverSupported, src);
    set_Hack_ATA_Always_Check_Condition_Available(device, p->hacks.ataPTHacks.alwaysCheckConditionAvailable, src);
    set_Hack_ATA_Always_Use_TPSIU_For_SAT_Passthrough(device, p->hacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough, src);
    set_Hack_ATA_28_Bit_Only(device, p->hacks.ataPTHacks.ata28BitOnly, src);
    set_Hack_ATA_Check_Condition_Empty(device, p->hacks.ataPTHacks.checkConditionEmpty, src);
    set_Hack_ATA_Disable_Check_Condition(device, p->hacks.ataPTHacks.disableCheckCondition, src);
    set_Hack_ATA_DMA_Mode_Selection(device, p->hacks.ataPTHacks.dmaModeSelection, src);
    set_Hack_ATA_DMA_Not_Supported(device, p->hacks.ataPTHacks.dmaNotSupported, src);
    set_Hack_ATA_Known_Realtek_USB(device, p->hacks.ataPTHacks.knownRealtekUSB, src);
    set_Hack_ATA_Limited_Use_TPSIU(device, p->hacks.ataPTHacks.limitedUseTPSIU, src);
    set_Hack_ATA_No_Multiple_Mode_Commands(device, p->hacks.ataPTHacks.noMultipleModeCommands, src);
    set_Hack_ATA_Possibly_Emulated_NVMe(device, p->hacks.ataPTHacks.possilbyEmulatedNVMe, src);
    set_Hack_ATA_Retry_Passthrough_Type(device, p->hacks.ataPTHacks.retryPassthroughType, src);
    set_Hack_ATA_Return_Response_Ignore_Extend_Bit(device, p->hacks.ataPTHacks.returnResponseIgnoreExtendBit, src);
    set_Hack_ATA_Return_Response_Info_Supported(device, p->hacks.ataPTHacks.returnResponseInfoSupported, src);
    set_Hack_ATA_Single_Sector_PIO_Only(device, p->hacks.ataPTHacks.singleSectorPIOOnly, src);
    set_Hack_ATA_SCT_Smart_Logs_Only(device, p->hacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly, src);
    set_Hack_ATA_SMART_Enabled(device, p->hacks.ataPTHacks.smartEnabled, src);
    set_Hack_NVMe_Limited_Get_Log_Page(device, p->hacks.nvmePTHacks.limitedCommandsSupported.getLogPage, src);
    set_Hack_NVMe_Limited_ID_Gen(device, p->hacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric, src);
    set_Hack_NVMe_Limited_Passthrough_Capabilities(device, p->hacks.nvmePTHacks.limitedPassthroughCapabilities, src);
    set_Hack_NVMe_Max_Xfer_Length(device, p->hacks.nvmePTHacks.maxTransferLength, src);
    set_Hack_SCSI_CmdDT_Checked(device, p->hacks.scsiHacks.cmdDTchecked, src);
    set_Hack_SCSI_No_Log_Pages(device, p->hacks.scsiHacks.noLogPages, src);
    set_Hack_SCSI_No_Log_Sub_Pages(device, p->hacks.scsiHacks.noLogSubPages, src);
    set_Hack_SCSI_No_Mode_Pages(device, p->hacks.scsiHacks.noModePages, src);
    set_Hack_SCSI_No_Mode_Sub_Pages(device, p->hacks.scsiHacks.noModeSubPages, src);
    set_Hack_SCSI_No_Report_Supported_Ops(device, p->hacks.scsiHacks.noReportSupportedOperations, src);
    set_Hack_SCSI_Pre_SCSI2_Inq_Data(device, p->hacks.scsiHacks.preSCSI2InqData, src);
    set_Hack_SCSI_RW_Available(device, p->hacks.scsiHacks.readWrite.available, src);
    set_Hack_SCSI_RW10(device, p->hacks.scsiHacks.readWrite.rw10, src);
    set_Hack_SCSI_RW16(device, p->hacks.scsiHacks.readWrite.rw16, src);
    set_Hack_SCSI_Report_All_Op_Codes(device, p->hacks.scsiHacks.reportAllOpCodes, src);
    set_Hack_SCSI_Report_Luns_Mode(device, p->hacks.scsiHacks.reportLunsMode, src);
    set_Hack_SCSI_Report_Single_Op_Codes(device, p->hacks.scsiHacks.reportSingleOpCodes, src);
    set_Hack_SCSI_INQ_ProductID_Length(device, p->hacks.scsiHacks.scsiInq.productIDLength, src);
    set_Hack_SCSI_INQ_ProductID_Offset(device, p->hacks.scsiHacks.scsiInq.productIDOffset, src);
    set_Hack_SCSI_INQ_ProductRev_Length(device, p->hacks.scsiHacks.scsiInq.productRevLength, src);
    set_Hack_SCSI_INQ_ProductRev_Offset(device, p->hacks.scsiHacks.scsiInq.productRevOffset, src);
    set_Hack_SCSI_INQ_VendorID_Length(device, p->hacks.scsiHacks.scsiInq.vendorIDLength, src);
    set_Hack_SCSI_INQ_VendorID_Offset(device, p->hacks.scsiHacks.scsiInq.vendorIDOffset, src);
    set_Hack_SCSI_Security_Protocol_Supported(device, p->hacks.scsiHacks.securityProtocolSupported, src);
    set_Hack_SCSI_Security_Protocol_Inc512(device, p->hacks.scsiHacks.securityProtocolWithInc512, src);
    set_Hack_SCSI_Sync_Cache_Mode(device, p->hacks.scsiHacks.syncCacheMode, src);
    set_Hack_SCSI_Unit_SN_Available(device, p->hacks.scsiHacks.unitSNAvailable, src);
    set_Hack_SCSI_WriteBuffer_No_Deferred_Download(device, p->hacks.scsiHacks.writeBufferNoDeferredDownload, src);
}

static void adapter_Apply_Profile_Scalars(tDevice* M_NONNULL device, const tAdapterProfile* M_NONNULL profile, const ePassthroughHackSource src)
{
    if (profile->scalPresent & PHA_S_TURF)
    {
        set_Hack_Generic_TURF_Value(device, profile->scalars.turfValue, src);
    }
    if (profile->scalPresent & PHA_S_ATA_MAX_XFER)
    {
        set_Hack_ATA_Max_Xfer_Length(device, profile->scalars.ataMaxXferLength, src);
    }
    if (profile->scalPresent & PHA_S_SCSI_MAX_XFER)
    {
        set_Hack_SCSI_Max_Xfer_Length(device, profile->scalars.scsiMaxXferLength, src);
    }
    if (profile->scalPresent & PHA_S_NVME_MAX_XFER)
    {
        set_Hack_NVMe_Max_Xfer_Length(device, profile->scalars.nvmeMaxXferLength, src);
    }
    if (profile->scalPresent & PHA_S_READ_BUFF_CMD_SIZE)
    {
        set_Hack_SCSI_Read_Buffer_Cmd_Size(device, profile->scalars.readBufferCmdSize, src);
    }
    if (profile->scalPresent & PHA_S_WRITE_VERIF_CMD_SIZE)
    {
        set_Hack_SCSI_Write_And_Verify_Cmd_Size(device, profile->scalars.writeAndVerifyCmdSize, src);
    }
    if (profile->scalPresent & PHA_S_WRITE_BUFF_CMD_SIZE)
    {
        set_Hack_SCSI_Write_Buffer_Cmd_Size(device, profile->scalars.writeBufferCmdSize, src);
    }
    if (profile->scalPresent & PHA_S_WRITE_SAME_CMD_SIZE)
    {
        set_Hack_SCSI_Write_Same_Cmd_Size(device, profile->scalars.writeSameCmdSize, src);
    }
}

static void adapter_Apply_Entry_Overrides(tDevice* M_NONNULL device, const tAdapterEntry* M_NONNULL entry, const ePassthroughHackSource src)
{
    if (entry->overPresent & PHA_S_TURF)
    {
        set_Hack_Generic_TURF_Value(device, entry->overrides.turfValue, src);
    }
    if (entry->overPresent & PHA_S_ATA_MAX_XFER)
    {
        set_Hack_ATA_Max_Xfer_Length(device, entry->overrides.ataMaxXferLength, src);
    }
    if (entry->overPresent & PHA_S_SCSI_MAX_XFER)
    {
        set_Hack_SCSI_Max_Xfer_Length(device, entry->overrides.scsiMaxXferLength, src);
    }
    if (entry->overPresent & PHA_S_NVME_MAX_XFER)
    {
        set_Hack_NVMe_Max_Xfer_Length(device, entry->overrides.nvmeMaxXferLength, src);
    }
    if (entry->overPresent & PHA_S_READ_BUFF_CMD_SIZE)
    {
        set_Hack_SCSI_Read_Buffer_Cmd_Size(device, entry->overrides.readBufferCmdSize, src);
    }
    if (entry->overPresent & PHA_S_WRITE_VERIF_CMD_SIZE)
    {
        set_Hack_SCSI_Write_And_Verify_Cmd_Size(device, entry->overrides.writeAndVerifyCmdSize, src);
    }
    if (entry->overPresent & PHA_S_WRITE_BUFF_CMD_SIZE)
    {
        set_Hack_SCSI_Write_Buffer_Cmd_Size(device, entry->overrides.writeBufferCmdSize, src);
    }
    if (entry->overPresent & PHA_S_WRITE_SAME_CMD_SIZE)
    {
        set_Hack_SCSI_Write_Same_Cmd_Size(device, entry->overrides.writeSameCmdSize, src);
    }
}

//! \fn void adapter_Apply_USB_Defaults(tDevice* M_NONNULL device)
//! \brief Safety-net defaults for USB-attached devices NOT covered by a
//! profile-table row (applied on lookup miss only): the SAT VPD page generally
//! does not exist on USB bridges, and a Test Unit Ready resync after a command
//! failure (still gated by the existing turfValue >= TURF limit check) is the
//! norm for USB-attached media. Deliberately NOT applied on a hit: known
//! profiles carry exactly what the legacy per-PID data said. noVPDPages is
//! never defaulted — some USB bridges DO expose VPD pages. If a future
//! exception to these defaults is found, add a new opposite (inhibit) hack
//! field rather than re-adding per-PID rows.
//! \param device
static void adapter_Apply_USB_Defaults(tDevice* M_NONNULL device)
{
    set_Hack_SCSI_No_SAT_VPD_Page(device, true, PH_BY_ADAPTER_ID);
    set_Hack_Generic_Test_Unit_Ready_After_Failure(device, true, PH_BY_ADAPTER_ID);
}

static void adapter_Apply_Entry(tDevice* M_NONNULL device, const tAdapterEntry* M_NONNULL entry)
{
    // generated-table integrity guard: an out-of-range index would be UB on the
    // profile array; the index is compile-time constant data, so this should be
    // unreachable, but fail soft (skip the profile) instead of reading past it
    if (entry->profileIndex < 0 || (size_t)entry->profileIndex >= adapter_Counts.profiles)
    {
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_VERBOSE, "adapter table integrity error: profileIndex %d out of range (profiles %u) - skipping profile\n", entry->profileIndex, M_STATIC_CAST(unsigned, adapter_Counts.profiles));
        return;
    }
    const tAdapterProfile* p = &adapter_Profiles[entry->profileIndex];
    const ePassthroughHackSource src = entry->unverified ? M_STATIC_CAST(ePassthroughHackSource, PH_BY_ADAPTER_ID | PH_DATA_UNVERIFIED) : PH_BY_ADAPTER_ID;
    if (device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
    {
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "adapter profile: %s (key 0x%016llXh)\n", p->profileName, M_STATIC_CAST(unsigned long long, entry->sortedKey));
    }
    if (p->hasPassthroughType)
    {
        set_Hack_Generic_Passthrough_Type(device, p->passthroughType, src);
    }
    if (p->hasMediaType)
    {
        set_Device_MediaType(device, p->mediaType);
    }
    if (p->hasDriveType)
    {
        set_Device_DriveType(device, p->driveType);
    }
    adapter_Set_Hacks_By_Profile(device, entry->profileIndex, src);
    adapter_Apply_Profile_Scalars(device, p, src);
    adapter_Apply_Entry_Overrides(device, entry, src);

    if (entry->sat2PostFix &&
        get_Hack_Generic_Passthrough_Type(device) == ATA_PASSTHROUGH_SAT &&
        get_Hack_ATA_Fixed_Sense_Format(device) == FF_FMT_TEST_WITH_NOP)
    {
        set_Hack_ATA_Fixed_Sense_Format(device, FF_FMT_SAT2_FIXED_FORMAT, src);
    }
    if (entry->unverified && device->deviceVerbosity >= VERBOSITY_COMMAND_NAMES)
    {
        print_tDevice_Verbose_Formatted_String(device, VERBOSITY_COMMAND_NAMES, "adapter profile %s is UNVERIFIED (provenance: %s)\n", p->profileName, entry->docRef);
    }
    if (device->verboseOutputStream != M_NULLPTR)
    {
        flush_tDevice_Verbose_Stream(device);
    }
}

void adapter_Apply_Profile(tDevice* M_NONNULL device)
{
    const tAdapterEntry* entry = M_NULLPTR;
    eReturnValues rc;
    if (device == M_NULLPTR)
    {
        return;
    }
    switch (device->drive_info.adapter_info.infoType)
    {
    case ADAPTER_INFO_USB:
        rc = adapter_Get_USB_Profile(device, &entry);
        break;
    case ADAPTER_INFO_PCI:
        rc = adapter_Get_PCI_Profile(device, &entry);
        break;
    case ADAPTER_INFO_IEEE1394:
        rc = adapter_Get_FireWire_Profile(device, &entry);
        break;
    default:
        return;
    }
    if (rc != SUCCESS || entry == M_NULLPTR)
    {
        if (device->drive_info.adapter_info.infoType == ADAPTER_INFO_USB)
        {
            adapter_Apply_USB_Defaults(device);
        }
        return;
    }
    adapter_Apply_Entry(device, entry);
}

#ifdef _DEBUG
eReturnValues adapter_Tables_Self_Check(void)
{
    size_t i;
    if (adapter_Counts.usbEntries == 0 || adapter_Counts.firewireEntries == 0 || adapter_Counts.pciEntries == 0 || adapter_Counts.profiles == 0)
    {
        return NOT_SUPPORTED;
    }
    for (i = 1; i < adapter_Counts.usbEntries; ++i)
    {
        if (adapter_UsbEntries[i-1].sortedKey >= adapter_UsbEntries[i].sortedKey)
        {
            return FAILURE;
        }
    }
    for (i = 0; i < adapter_Counts.usbEntries; ++i)
    {
        if (adapter_UsbEntries[i].profileIndex < 0 || M_STATIC_CAST(size_t, adapter_UsbEntries[i].profileIndex) >= adapter_Counts.profiles || adapter_UsbEntries[i].docRef == M_NULLPTR)
        {
            return FAILURE;
        }
    }
    for (i = 1; i < adapter_Counts.firewireEntries; ++i)
    {
        if (adapter_FwEntries[i-1].sortedKey >= adapter_FwEntries[i].sortedKey)
        {
            return FAILURE;
        }
    }
    for (i = 0; i < adapter_Counts.firewireEntries; ++i)
    {
        if (adapter_FwEntries[i].profileIndex < 0 || M_STATIC_CAST(size_t, adapter_FwEntries[i].profileIndex) >= adapter_Counts.profiles || adapter_FwEntries[i].docRef == M_NULLPTR)
        {
            return FAILURE;
        }
    }
    for (i = 1; i < adapter_Counts.pciEntries; ++i)
    {
        if (adapter_PciEntries[i-1].sortedKey >= adapter_PciEntries[i].sortedKey)
        {
            return FAILURE;
        }
    }
    for (i = 0; i < adapter_Counts.pciEntries; ++i)
    {
        if (adapter_PciEntries[i].profileIndex < 0 || M_STATIC_CAST(size_t, adapter_PciEntries[i].profileIndex) >= adapter_Counts.profiles || adapter_PciEntries[i].docRef == M_NULLPTR)
        {
            return FAILURE;
        }
    }
    return SUCCESS;
}
#endif // _DEBUG

const tAdapterTableCounts* M_NONNULL adapter_Get_Table_Counts(void)
{
    return &adapter_Counts;
}

const tAdapterEntry* M_NULLABLE adapter_Get_Entry_Array(eAdapterInfoType family)
{
    switch (family)
    {
    case ADAPTER_INFO_USB:
        return adapter_UsbEntries;
    case ADAPTER_INFO_IEEE1394:
        return adapter_FwEntries;
    case ADAPTER_INFO_PCI:
        return adapter_PciEntries;
    default:
        return M_NULLPTR;
    }
}
