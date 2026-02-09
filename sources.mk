# SPDX-License-Identifier: MPL-2.0
#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012-2025 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
#
# This software is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# ******************************************************************************************
#
# \file sources.mk
# \brief opensea-transport source file list
#
# This file defines all source files needed to build libopensea-transport.
# It consumes the PLATFORM variable from the parent makefile to select
# platform-specific sources.

#===============================================================================
# Common Transport Sources (all platforms)
#===============================================================================

TRANSPORT_SOURCES := \
    ata_cmds.c \
    ata_legacy_cmds.c \
    ata_helper.c \
    cmds.c \
    common_public.c \
    sat_helper.c \
    scsi_cmds.c \
    scsi_helper.c \
    nvme_cmds.c \
    nvme_helper.c \
    psp_legacy_helper.c \
    cypress_legacy_helper.c \
    sunplus_legacy_helper.c \
    ti_legacy_helper.c \
    nec_legacy_helper.c \
    prolific_legacy_helper.c \
    usb_hacks.c \
    sata_helper_func.c \
    raid_scan_helper.c \
    sntl_helper.c \
    jmicron_nvme_helper.c \
    jmicron_legacy_helper.c \
    asmedia_nvme_helper.c \
    realtek_nvme_helper.c \
    csmi_legacy_pt_cdb_helper.c \
    csmi_helper.c

#===============================================================================
# Platform-Specific Transport Sources
#===============================================================================

ifeq ($(PLATFORM),linux)
    # Linux: SCSI Generic (SG_IO) + CISS for HP SmartArray
    TRANSPORT_SOURCES += \
        sg_helper.c \
        ciss_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),windows)
    # Windows: SCSI_PASS_THROUGH_DIRECT + Intel RST + OpenFabrics NVMe
    TRANSPORT_SOURCES += \
        win_helper.c \
        intel_rst_helper.c \
        of_nvme_helper.c

else ifeq ($(PLATFORM),freebsd)
    # FreeBSD: CAM (Common Access Method)
    TRANSPORT_SOURCES += \
        cam_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),dragonflybsd)
    # DragonFlyBSD: CAM (Common Access Method)
    TRANSPORT_SOURCES += \
        cam_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),openbsd)
    # OpenBSD: NetBSD/OpenBSD shared helper (28-bit ATA limitation)
    # Also needs BSD ATA/SCSI passthrough helpers
    TRANSPORT_SOURCES += \
        bsd_ata_passthrough.c \
        bsd_scsi_passthrough.c \
        netbsd_openbsd_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),netbsd)
    # NetBSD: NetBSD/OpenBSD shared helper (28-bit ATA limitation)
    # Also needs BSD ATA/SCSI passthrough helpers
    TRANSPORT_SOURCES += \
        bsd_ata_passthrough.c \
        bsd_scsi_passthrough.c \
        netbsd_openbsd_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),sunos)
    # Solaris/Illumos: USCSI (User SCSI)
    TRANSPORT_SOURCES += \
        uscsi_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),aix)
    # AIX: Custom SCSI/ATA passthrough
    TRANSPORT_SOURCES += \
        aix_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),vmware)
    # VMware ESXi: Custom VM helper
    TRANSPORT_SOURCES += \
        vm_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else ifeq ($(PLATFORM),hpux)
    # HP-UX: USCSI-style interface
    TRANSPORT_SOURCES += \
        hpux_helper.c \
        posix_common_lowlevel.c \
        nix_mounts.c

else
    $(error Unsupported platform: $(PLATFORM))
endif
