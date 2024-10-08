// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file raid_scan_helper.c
// \brief Defines the structures, types, and function to assist with scanning for devices in different RAID configurations.

#include "common_types.h"
#include "precision_timer.h"
#include "memory_safety.h"
#include "type_conversion.h"
#include "string_utils.h"
#include "bit_manip.h"
#include "code_attributes.h"
#include "math_utils.h"
#include "io_utils.h"

#include "raid_scan_helper.h"

//Function to make it easy to add another entry to the list
    //Returns pointer to the added entry.
    //Entry is always added in currentPtr->next
ptrRaidHandleToScan add_RAID_Handle(ptrRaidHandleToScan currentPtr, char *handleToScan, raidTypeHint raidHint)
{
    //first make sure the current pointer is valid, if not it is most likely the beginning of the list, so it needs to be allocated
    if (currentPtr)
    {
        currentPtr->next = C_CAST(ptrRaidHandleToScan, safe_calloc(1, sizeof(raidHandleToScan)));
        if (!currentPtr->next)
        {
            return M_NULLPTR;
        }
        //data allocated, so update to that pointer to fill in the other data
        currentPtr = currentPtr->next;
    }
    else
    {
        //probably first entry in the list, so allocate first entry
        currentPtr = C_CAST(ptrRaidHandleToScan, safe_calloc(1, sizeof(raidHandleToScan)));
    }
    //make sure valid before filling in fields
    if (currentPtr)
    {
        currentPtr->next = M_NULLPTR;
        snprintf(currentPtr->handle, RAID_HANDLE_STRING_MAX_LEN, "%s", handleToScan);
        currentPtr->raidHint = raidHint;
    }
    else
    {
        return M_NULLPTR;
    }
    return currentPtr;
}

ptrRaidHandleToScan add_RAID_Handle_If_Not_In_List(ptrRaidHandleToScan listBegin, ptrRaidHandleToScan currentPtr, char *handleToScan, raidTypeHint raidHint)
{
    if (listBegin)
    {
        while (listBegin)
        {
            if (strcmp(listBegin->handle, handleToScan) == 0)
            {
                //already in the list, return the curent pointer
                return currentPtr;
            }
            listBegin = listBegin->next;//go to next element
        }
        //if we make it to here, the handle doesn't exist, so call the add function
        return add_RAID_Handle(currentPtr, handleToScan, raidHint);
    }
    else if (!listBegin && !currentPtr)//creating new list, so no beginning is set yet
    {
        return add_RAID_Handle(currentPtr, handleToScan, raidHint);
    }
    return M_NULLPTR;
}

static M_INLINE void free_RaidHandleToScan(ptrRaidHandleToScan *handle)
{
    safe_Free(M_REINTERPRET_CAST(void**, handle));
}

//Make it easier to remove an item. Useful when scanning multiple RAID libs because the first RAID lib can remove handles that did in fact work so that they are not scanned again by another RAID library.
//returns a pointer to the entry after "toRemove", which can be M_NULLPTR
//If previous is M_NULLPTR, then this is the beginning of the list. This is allowed.
//Previous is used to update the previous entry's next pointer to make sure the list is still functional
ptrRaidHandleToScan remove_RAID_Handle(ptrRaidHandleToScan toRemove, ptrRaidHandleToScan previous)
{
    if (toRemove)
    {
        if (toRemove->next)
        {
            ptrRaidHandleToScan returnMe = toRemove->next;
            if (previous)
            {
                //If there was a previous entry, need to update it's next pointer
                previous->next = returnMe;
            }
            free_RaidHandleToScan(&toRemove);
            return returnMe;
        }
        else
        {
            //no next available. change previous->next to M_NULLPTR
            if (previous)
            {
                previous->next = M_NULLPTR;
            }
            free_RaidHandleToScan(&toRemove);
            return M_NULLPTR;
        }
    }
    return M_NULLPTR;
}

//Deletes everything in the list from pointer to the beginning of the list.
void delete_RAID_List(ptrRaidHandleToScan listBegin)
{
    while (listBegin)
    {
        if (listBegin->next)
        {
            ptrRaidHandleToScan nextDelete = listBegin->next;
            free_RaidHandleToScan(&listBegin);
            listBegin = nextDelete;
        }
        else
        {
            free_RaidHandleToScan(&listBegin);
            break;
        }
    }
    return;
}
