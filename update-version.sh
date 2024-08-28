#!/bin/bash
: '
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file update-version.h
// \brief Update the version number & check in code.
//
'
VERSION_FILE=include/version.h
BASE=`awk '$2 == "OPENSEA_TRANSPORT_PATCH_VERSION" {print $1 "  " $2}' "$VERSION_FILE"`
CURRENT_VERSION=`awk '$2 == "OPENSEA_TRANSPORT_PATCH_VERSION" {print $3}' "$VERSION_FILE"`
NEXT_VER=$((CURRENT_VERSION + 1))
echo $BASE $CURRENT_VERSION
echo $BASE $NEXT_VER
sed -i "/._PATCH_VERSION\s*/s/$CURRENT_VERSION/$NEXT_VER/" "$VERSION_FILE"

MAKE_FILE=Make/gcc/Makefile
sed -i "/PATCH=*/s/$CURRENT_VERSION/$NEXT_VER/" "$MAKE_FILE"
