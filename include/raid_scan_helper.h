// SPDX-License-Identifier: MPL-2.0

//! \file raid_scan_helper.h
//! \brief Defines the structures, types, and function to assist with scanning for devices in different RAID
//! configurations.
//! \details How this is supposed to be used:
//! The general idea is this: Each OS has some information it can use to detect what may or may not be a RAID.
//! This is handled at a low-level while scanning for devices.
//! Any device that is suspected of being part of a RAID configuration will have the OS's handle passed off to a RAID
//! implementation to use as a place to scan with RAID specific IOCTLs This code creates a linked list that should make
//! it easy to add those handles to a short list that can be passed on to the RAID implementations to use how they are
//! needed.
//! This is currently being used by CSMI in WIndows, but further support will be added as other RAID libraries are
//! supported.
//!\copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_public.h"
#include "common_types.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    /*
     */

    // using bit field here to reduce memory usage, but can be removed if this causes problems
    // This is intended to hint which RAID type a device is, if there is a way to know or guess what it is in a given
    // OS. This may not always be possible, so setting unknown will force scanning any implemented RAID setup as
    // bitfield/booleans since so that guesses can be take for multiple types of RAID in case it cannot be narrowed down
    // to one specific type NOTE: Not all RAIDs are supported when this comment was written. NOTE: It is not possible to
    // support every type of RAID. Many are not supportable since they do not provide a method to talk to individual
    // drives in a RAID configuration, only those that do will be supported.
    typedef struct s_raidTypeHint
    {
        bool unknownRAID : 1;   // Setting this means it is not a known RAID from quick OS level glance and any RAID
                                // library that sees this device should try to use it and check it.
        bool csmiRAID : 1;      // CSMI RAID IOs. Mostly Intel motherboards, but some other drivers may support it
        bool cissRAID : 1;      // HP CISS RAID controllers
        bool megaRAID : 1;      // LSI/Avago/Broadcom MegaRAID IOCTLs (Not supported yet)
        bool adaptecRAID : 1;   // Adaptec/PMC/Microsemi RAID (Not supported yet)
        bool highpointRAID : 1; // Highpoint RAID  (Not supported yet)
        bool areccaRAID : 1;    // Arecca RAID  (Not supported yet)
        bool intelVROC : 1;     // Intel VROC - NVMe (Not supported yet)
    } raidTypeHint;

// Define the max string length
#define RAID_HANDLE_STRING_MAX_LEN UINT8_C(32)

    typedef struct s_raidHandleToScan
    {
        struct s_raidHandleToScan* next; // must be declared like this to work with older GCC compilers.
        char                       handle[RAID_HANDLE_STRING_MAX_LEN];
        raidTypeHint               raidHint;
        // These pieces of info may provide additional help in case the OS is unable to classify the hint, passing this
        // along may help screen which RAID to scan this device with when this data is available.-TJE If the system has
        // this info and can pass it in, it also helps populate additional data fields when enumerating the device in
        // the RAID that may otherwise be missed since the RAID layer may not be able to figure out how to get this
        // info.
        eSCSIPeripheralDeviceType systemDeviceType; // this can be passed in to tell us if it's a block device,
                                                    // controller, etc as we are scanning.
        adapterInfo adapter_info;
        driverInfo  driver_info;
    } raidHandleToScan, *ptrRaidHandleToScan;

    // Function to make it easy to add another entry to the list
    // Returns pointer to the added entry.
    // Entry is always added in currentPtr->next
    M_NONNULL_PARAM_LIST(2)
    M_PARAM_RW(1)
    M_NULL_TERM_STRING(2)
    M_PARAM_RO(2)
    ptrRaidHandleToScan add_RAID_Handle(ptrRaidHandleToScan currentPtr,
                                        const char*         handleToScan,
                                        raidTypeHint        raidHint);

    // Same as above, but checks to make sure that the provided handle is not already part of the list to scan - helpful
    // for some configurations where a RAID also has JBOD/passthrough disks available on the same HBA If already in the
    // list, currentPtr is returned
    M_NONNULL_PARAM_LIST(3)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    M_NULL_TERM_STRING(3)
    M_PARAM_RO(3)
    ptrRaidHandleToScan add_RAID_Handle_If_Not_In_List(ptrRaidHandleToScan listBegin,
                                                       ptrRaidHandleToScan currentPtr,
                                                       const char*         handleToScan,
                                                       raidTypeHint        raidHint);

    // Make it easier to remove an item. Useful when scanning multiple RAID libs because the first RAID lib can remove
    // handles that did in fact work so that they are not scanned again by another RAID library. returns a pointer to
    // the entry after "toRemove", which can be M_NULLPTR
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_PARAM_RW(2) ptrRaidHandleToScan remove_RAID_Handle(ptrRaidHandleToScan toRemove, ptrRaidHandleToScan previous);

    // Deletes everything in the list from pointer to the beginning of the list.
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) void delete_RAID_List(ptrRaidHandleToScan listBegin);

#if defined(__cplusplus)
}
#endif
