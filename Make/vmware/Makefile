# SPDX-License-Identifier: MPL-2.0
#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
#
# This software is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# ******************************************************************************************

# Hand Written Makefile (Edit with caution) -Debabrata
# 

NAME=opensea-transport
FILE_OUTPUT_DIR=lib
#Change the Major version when major interface changes are made. E.g. tDevice changes
MAJOR=8
#Change the Minor version when new features are added. 
MINOR=0
#Change the patch version when only bug fixes are made.
PATCH=1
VERSION=$(MAJOR).$(MINOR).$(PATCH)

DESC_ACCEPTANCE_LEVEL=certified

# When this is not empty, the infrastructure will look for the files
# $(DEV_SIGNING_PREFIX).key and $(DEV_SIGNING_PREFIX).cert.  If the
# prefix contains a slash, it is assumed to be a full path to the
# directory containing the key/cert files, and that simply appending
# ".key" and ".cert" will give the actual file names.  Otherwise, it
# looks in the vib-suite installation directory's "testcerts"
# directory for the files.
DEV_SIGNING_PREFIX=vmware

BULL_KBURL=http://kb.vmware.com/kb/example.html
BULL_PLATFORMS=bulletin-platforms.url
BULL_VENDOR_CODE=VMW

# Calculate VMKAPIDDK_ROOT, BUILD_NUMBER

HAS_NATIVEDDK := $(shell rpm -qa | grep vmware-esx-nativeddk-devtools)
ifdef HAS_NATIVEDDK
DDK=vmware-esx-nativeddk-devtools
HEXDK=vmware-esx-hexdk-devtools
else
DDK=vmware-esx-vmkapiddk-devtools
HEXDK=vmware-esx-esxcli-devtools
endif

VMKAPIDDK_ROOT = $(shell rpm -q --qf '%{INSTPREFIXES}\n' $(DDK))
BUILD_NUMBER=$(shell echo $(VMKAPIDDK_ROOT) | cut -d '-' -f3)
DESC_ESX_VERSION=$(shell echo $(VMKAPIDDK_ROOT) | cut -d '-' -f2)

# Makefile
#
# This builds the Userworld Tool binary for the example being packaged here.
#
# type:
#  make
# to build the Userworld Tool.
#
# Expected usage is that this Makefile is copied to a new source directory,
# and the variables above the "Nothing beyond this point" comment are
# adjusted appropriately.
#



################################################################
# Set build configuration options here.
#
# Variables starting with "DEV_" are reserved for communicating
# build parameters into the core makefiles (in .../samples/common).
#
# Those starting with "VMW_" are reserved for VMware use, and should
# be considered unsupported with no compatibility guarantees unless
# explicitly documented otherwise.
#
# Note that many variables do not currently follow the above
# patterns.  This is to minimize changes required in existing
# partner code.

################
# First, define the end environment(s) with the following two
# variables.  As shown here, one is defined.  If more than one is
# necessary, these values will need to be set on a per-target basis,
# or in a loop that recursively invokes make with each set of values
# provided on the command line.
#
# This parameter must be set.

# Legal values for DEV_TARGET_CONTEXT are "vmkernel" or "uw".
DEV_TARGET_CONTEXT = uw

################
# This controls which class of compiler options to use.  Allowed
# values are "beta" (for debugging) and "release".  The default is
# the type of the ESX build that produced this devkit.
DEV_OBJDIR = beta

################
# Typically, it won't be necessary to modify this setting, but the
# substitutions pattern may need to put the intermediate files in a
# more convenient location.

################
# If creating a standalone userworld application, as opposed to a
# shared object that gets loaded by some other program, uncomment
# the following assignment.  The value needs to be empty or non-
# empty.  If the latter, any content may be used.
#DEV_APP = 1
DEV_APP = 

################
# Extra compiler and preprocessor flags based on kind of build
# (release or beta).  These default to being empty.
# DEV_beta_COMPILEFLAGS = -Dmy_debug_flag
# DEV_release_COMPILEFLAGS = -Dmy_optimized_flag

################
# Define here any additional libraries required by uncommenting the
# appropriate variable(s) and modifying as needed.  If the library is
# not included in the devkit distribution, include a -L /some/path
# parameter before the -lname parameter so that the linker can find
# it.  The *PRE* variables are included as the first library
# directives.  The *POST* variables are included as the last library
# directives.
#
# The default values are empty.

# DEV_release_PRE_LIBS =
# DEV_beta_PRE_LIBS =

# The third assignment below is semantically equivalent to the first
# two assignments.

# DEV_release_POST_LIBS = -lm
# DEV_beta_POST_LIBS = -lm
# or
# DEV_$(DEV_OBJDIR)_POST_LIBS = -lm

################
# If the vdsPublic library needs to be included in the compilation,
# uncomment the following assignment.  Currently, that library only
# comes as part of the "kmdk-devtools-vds" devkit, so it and any
# other devkit components it depends on will need to be installed
# on the same development host as this devkit.  The value must be
# empty for not needing vdsPublic, and non-empty for needing it.
# The actual content is not examined.

# DEV_NEEDVDSLIB = 1

################
# Request that the management library be included when we link.  This
# has to be explicitly done here because we're hooking into the base
# HExDK's build system, and it doesn't do that inclusion by default.

DEV_NEED_VMKMGMT = 1

################################################################
# Nothing beyond this point should require modification.

GREP := /build/toolchain/lin32/grep-2.5.1a/bin/grep

IMPORT_DEVKIT = $(shell rpm -q --qf '%{INSTPREFIXES}\n' $(1) | $(GREP) $(BUILD_NUMBER))

HEXDK_BASE := $(call IMPORT_DEVKIT,$(HEXDK))
VMKAPIDDK_BASE := $(call IMPORT_DEVKIT,$(DDK))

ifeq ($(DESC_ESX_VERSION),7.0.0)
VMKAPI_LOCATION = $(VMKAPIDDK_BASE)/src/bora/vmkernel/include/vmkapi/v2_6_0_0
else
ifeq ($(DESC_ESX_VERSION),6.5.0)
VMKAPI_LOCATION = $(VMKAPIDDK_BASE)/src/bora/vmkernel/include/vmkapi/v2_4_0_0
else
ifeq ($(DESC_ESX_VERSION),6.0.0)
VMKAPI_LOCATION = $(VMKAPIDDK_BASE)/src/bora/vmkernel/include/vmkapi/v2_3_0_0
else
VMKAPI_LOCATION = $(VMKAPIDDK_BASE)/src/bora/vmkernel/include/vmkapi/v2_2_0_0
endif
endif
endif

VMKAPI_LOCATION = $(dir $(shell find $(VMKAPIDDK_ROOT)/src/bora -name vmkapi.h))

#
# Check paths
#

ifeq ($(wildcard $(HEXDK_BASE)/samples/common/userworld-tool.inc),)
$(error Host-Extensions devkit build $(BUILD_NUMBER) required but not installed.)
endif

ifeq ($(wildcard $(VMKAPI_LOCATION)/vmkapi.h),)
$(error VMKAPI DDK devkit build $(BUILD_NUMBER) required but not installed.)
endif

# Note that this hard-codes the prefix path at installation
# time.  This allows for having multiple different versions
# of the devkit installed and usable simultaneously.

#include $(HEXDK_BASE)/samples/common/userworld-tool.inc

VMW_VERSION := 7.0.0
VMW_BUILD   := 15843807
VMW_RELEASE := 1.0.15843807

ifeq ($(strip $(DEV_OBJDIR)),)
   # Compress all the internal build types down to the two external types.
   ifeq ($(strip release),release)
      DEV_OBJDIR = release
   else
      DEV_OBJDIR = beta
   endif
endif

# For this devkit, it's reasonable to default to userworld builds.
# So do that instead of bailing out if the user didn't tell us.
DEV_TARGET_CONTEXT ?= uw

# This will enable later generalization for supporting both userworld
# and vmkernel builds with the same make infrastructure.

DEV_TARGET_BITNESS = 64

VMW_TARGET = $(DEV_TARGET_CONTEXT)$(DEV_TARGET_BITNESS)_$(DEV_OBJDIR)


CAYMAN_ESX_GDB_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_gdb-1af75dbb095cc2348c35c6708c8b326ac1661974-toolchain)
export GOBUILD_CAYMAN_ESX_GDB_ROOT = $(CAYMAN_ESX_GDB_DIR)
CAYMAN_ESX_TOOLCHAIN_GCC6_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_toolchain_gcc6-20292133d8d6444deefa46c286c3f74ec52badc8-toolchain)
export GOBUILD_CAYMAN_ESX_TOOLCHAIN_GCC6_ROOT = $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)
CAYMAN_ESX_NCURSES_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_ncurses-9bf45330b538505c6325906c08c4ecf834184b5c-toolchain)
export GOBUILD_CAYMAN_ESX_NCURSES_ROOT = $(CAYMAN_ESX_NCURSES_DIR)
CAYMAN_ESX_GLIBC_2_17_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_glibc_2_17-8da846cdfffa1ce0a450d82e5d60140c753ab937-toolchain)
export GOBUILD_CAYMAN_ESX_GLIBC_2_17_ROOT = $(CAYMAN_ESX_GLIBC_2_17_DIR)
CAYMAN_PYTHON_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_python-99788ba0a9e40ea80ae7b05efb941377abfb31e4-toolchain)
export GOBUILD_CAYMAN_PYTHON_ROOT = $(CAYMAN_PYTHON_DIR)
VMW_COMPONENTS = cayman_esx_gdb cayman_esx_toolchain_gcc6 cayman_esx_ncurses cayman_esx_glibc_2_17 cayman_python
VMW_COMPONENT_DIRS = $(CAYMAN_ESX_GDB_DIR) $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR) $(CAYMAN_ESX_NCURSES_DIR) $(CAYMAN_ESX_GLIBC_2_17_DIR) $(CAYMAN_PYTHON_DIR)

AWK = /build/toolchain/lin32/gawk-3.1.6/bin/awk
CHMOD = /build/toolchain/lin32/coreutils-5.97/bin/chmod
CP = /build/toolchain/lin32/coreutils-5.97/bin/cp
ECHO = /build/toolchain/lin32/coreutils-5.97/bin/echo
GREP = /build/toolchain/lin32/grep-2.5.1a/bin/grep
GZIP = /build/toolchain/lin32/gzip-1.3.5/bin/gzip
MAKE = /build/toolchain/lin64/make-3.81/bin/make
MKDIR = /build/toolchain/lin32/coreutils-5.97/bin/mkdir
MV = /build/toolchain/lin32/coreutils-5.97/bin/mv
RM = /build/toolchain/lin32/coreutils-5.97/bin/rm
SED = /build/toolchain/lin32/sed-4.1.5/bin/sed
TR = /build/toolchain/lin32/coreutils-5.97/bin/tr

_VMW_RPM_PREFIX	:= vmware-esx

CNS=componentNameSpec
CVS=componentVersionSpec
DESC_PAYLOAD_NAME := $(shell echo $(DESC_PAYLOAD_NAME) | $(SED) -e 's/[_-]//g' | $(TR) A-Z a-z | head -c 8)

COMPONENT_BASENAME=VMW-esx-$(DESC_ESX_VERSION)-$(BULL_CNS_NAME)-$(BULL_CVS_VERSION)

# This import capability is generic, so that user code can get a reference
# onto a companion devkit (say, a management app needs to see the headers
# for a driver).  Since we've got it anyway, might as well use it for our
# own needs as well.
#
# Note: because of the way make expands the call function, extraneous
# whitespace can cause obscure problems.  In particular, don't put any
# next to the commas.

VMW_IMPORT_DEVKIT = $(shell /bin/rpm -q --qf '%{INSTPREFIXES}\n' $(1)$(2))

VMW_KMDK_BASE := $(call VMW_IMPORT_DEVKIT,$(_VMW_RPM_PREFIX)-kmdk-devtools-vds-$(VMW_VERSION)-,$(VMW_RELEASE))
VMW_DRIVER_DEVKIT_BASE := $(call VMW_IMPORT_DEVKIT,$(_VMW_RPM_PREFIX)-nativeddk-devtools-$(VMW_VERSION)-,$(VMW_RELEASE))

VMW_VIB_BASE := $(shell $(HEXDK_BASE)/tools/find_rpm_install_dir.sh $(_VMW_RPM_PREFIX)-vib-suite)

ifeq ($(strip $(VMW_VIB_BASE)),)
   $(error Could not locate vib-suite.)
endif

# This is for backwards compatibility.  The VIBPATH variable is
# mostly being deprecated.

VIBPATH := $(VMW_VIB_BASE)

VMW_uw64_beta_CCBIN = $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/bin/x86_64-vmk-linux-gnu-gcc
VMW_uw64_beta_OBJCOPYBIN = $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/bin/x86_64-vmk-linux-gnu-objcopy
#VMW_uw64_beta_COMPILEFLAGS = -fno-working-directory -fno-strict-aliasing -funwind-tables -fasynchronous-unwind-tables -Wall -Werror -O2 -fno-omit-frame-pointer -pthread -fwrapv -pipe -fno-strict-aliasing -Wno-unused-but-set-variable -g -fPIC -fstack-protector -fstack-protector-all -DUSE_GOBUILD_TOOLS_VERSION -std=gnu90 -Wno-unused-value -Wdeclaration-after-statement -Wno-pointer-sign -Wno-strict-prototypes -Wno-enum-compare --sysroot=$(CAYMAN_ESX_GLIBC_2_17_DIR)/sysroot -m64 -isystem $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/lib/gcc/x86_64-vmk-linux-gnu/6.4.0/include -D_LARGEFILE64_SOURCE -D_XOPEN_SOURCE=600 -D_SVID_SOURCE -DVMX86_DEBUG -D_FORTIFY_SOURCE=2 -D_FILE_OFFSET_BITS=64 -D_GLIBCXX_USE_CXX11_ABI=0 -D_BSD_SOURCE -Dlinux -DVMX86_BETA
VMW_uw64_beta_COMPILEFLAGS = -fno-working-directory -fno-strict-aliasing -funwind-tables -fasynchronous-unwind-tables -Wall -O2 -fno-omit-frame-pointer -pthread -fwrapv -pipe -fno-strict-aliasing -Wno-unused-but-set-variable -g -fPIC -fstack-protector -fstack-protector-all -DUSE_GOBUILD_TOOLS_VERSION -std=gnu99 -Wno-unused-value -Wno-pointer-sign -Wno-strict-prototypes -Wno-enum-compare --sysroot=$(CAYMAN_ESX_GLIBC_2_17_DIR)/sysroot -m64 -isystem $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/lib/gcc/x86_64-vmk-linux-gnu/6.4.0/include -D_LARGEFILE64_SOURCE -D_XOPEN_SOURCE=600 -D_SVID_SOURCE -DVMX86_DEBUG -D_FORTIFY_SOURCE=2 -D_FILE_OFFSET_BITS=64 -D_GLIBCXX_USE_CXX11_ABI=0 -D_BSD_SOURCE -Dlinux -DVMX86_BETA
VMW_uw64_beta_LINKFLAGS =  -pthread --sysroot=$(CAYMAN_ESX_GLIBC_2_17_DIR)/sysroot -Wl,-z,relro -Wl,-z,noexecstack -Wl,-z,now -Wl,-z,defs -Wl,--hash-style=gnu

VMW_uw64_release_CCBIN = $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/bin/x86_64-vmk-linux-gnu-gcc
VMW_uw64_release_OBJCOPYBIN = $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/bin/x86_64-vmk-linux-gnu-objcopy
#VMW_uw64_release_COMPILEFLAGS = -fno-working-directory -fno-strict-aliasing -funwind-tables -fasynchronous-unwind-tables -Wall -Werror -DNDEBUG -O2 -fno-omit-frame-pointer -pthread -fwrapv -pipe -fno-strict-aliasing -Wno-unused-but-set-variable -g -fPIC -fstack-protector -DUSE_GOBUILD_TOOLS_VERSION -std=gnu90 -Wno-unused-value -Wdeclaration-after-statement -Wno-pointer-sign -Wno-strict-prototypes -Wno-enum-compare --sysroot=$(CAYMAN_ESX_GLIBC_2_17_DIR)/sysroot -m64 -isystem $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/lib/gcc/x86_64-vmk-linux-gnu/6.4.0/include -D_LARGEFILE64_SOURCE -D_XOPEN_SOURCE=600 -D_SVID_SOURCE -DVMX86_RELEASE -D_FORTIFY_SOURCE=2 -D_FILE_OFFSET_BITS=64 -D_GLIBCXX_USE_CXX11_ABI=0 -D_BSD_SOURCE -Dlinux
VMW_uw64_release_COMPILEFLAGS = -fno-working-directory -fno-strict-aliasing -funwind-tables -fasynchronous-unwind-tables -Wall -DNDEBUG -O2 -fno-omit-frame-pointer -pthread -fwrapv -pipe -fno-strict-aliasing -Wno-unused-but-set-variable -g -fPIC -fstack-protector -DUSE_GOBUILD_TOOLS_VERSION -std=gnu99 -Wno-unused-value -Wno-pointer-sign -Wno-strict-prototypes -Wno-enum-compare --sysroot=$(CAYMAN_ESX_GLIBC_2_17_DIR)/sysroot -m64 -isystem $(CAYMAN_ESX_TOOLCHAIN_GCC6_DIR)/usr/lib/gcc/x86_64-vmk-linux-gnu/6.4.0/include -D_LARGEFILE64_SOURCE -D_XOPEN_SOURCE=600 -D_SVID_SOURCE -DVMX86_RELEASE -D_FORTIFY_SOURCE=2 -D_FILE_OFFSET_BITS=64 -D_GLIBCXX_USE_CXX11_ABI=0 -D_BSD_SOURCE -Dlinux
VMW_uw64_release_LINKFLAGS =  -pthread --sysroot=$(CAYMAN_ESX_GLIBC_2_17_DIR)/sysroot -Wl,-z,relro -Wl,-z,noexecstack -Wl,-z,now -Wl,-z,defs -Wl,--hash-style=gnu

# In theory, the compiler already knows what version of the basic libraries
# it should be using, so we don't need to specify any of them here.  Any
# path modifications were done when the devkit was put together.

VMW_EXTRA_DEFS := -DBUILT_BY_$(USER) $(DEV_$(DEV_OBJDIR)_COMPILEFLAGS)

# Need to set variable type so that it's always fully expanded upon
# reference.
VMW_LINK_FLAGS = $(VMW_$(DEV_OBJDIR)_LINKFLAGS)

ifneq ($(strip $(DEV_APP)),)
   # Userworld apps need to be position-independent executables, so
   # tell gcc about it.
   VMW_LINK_FLAGS += -pie
else
   VMW_LINK_FLAGS += -shared
endif

VMW_LIB_DIRS = -L $(HEXDK_BASE)/lib64
# If the user requested that the vdsPublic library be used, and it's
# available on the system, add the bits the compiler/linker need
# for finding it.

# xxx should rename to DEV_NEED_VDSLIB.
ifneq ($(strip $(DEV_NEEDVDSLIB)),)
   ifneq ($(strip $(VMW_KMDK_BASE)),)
      VMW_LIB_DIRS += -L $(VMW_KMDK_BASE)/uw-vds/lib/$(DEV_OBJDIR)
      VMW_LIBS += -lvdsPublic
      VMW_EXTRA_DEFS += -I$(VMW_KMDK_BASE)/uw-vds/include
   else
      $(error VDS library requested, but kmdk-devtools-vds devkit not found.)
   endif
endif

VMW_LIB_BITNESS = 64

# Similarly, if we have a request for the VMKernel Management library,
# find it and update variables accordingly.

ifneq ($(strip $(DEV_NEED_VMKMGMT)$(DEV_NEED_IMA)),)
   ifneq ($(strip $(VMW_DRIVER_DEVKIT_BASE)),)
      VMW_EXTRA_DEFS += \
         -I$(VMW_DRIVER_DEVKIT_BASE)/src/bora/public \
         -I$(VMW_DRIVER_DEVKIT_BASE)/src/bora/vmkernel/include/vmkapi/v2_6_0_0
      VMW_LIB_DIRS += \
         -L $(VMW_DRIVER_DEVKIT_BASE)/lib$(VMW_LIB_BITNESS) \
         -L $(HEXDK_BASE)/debug \
         -L $(CAYMAN_ZLIB_DIR)/lin$(DEV_TARGET_BITNESS)/lib$(VMW_LIB_BITNESS) \
         -L $(CAYMAN_OPENSSL_DIR)/lin$(DEV_TARGET_BITNESS)/lib$(VMW_LIB_BITNESS)
      VMW_LIB_DIRS += -L $(CAYMAN_ZLIB_DIR)/lin$(DEV_TARGET_BITNESS)/lib

      ifneq ($(strip $(DEV_NEED_VMKMGMT)),)
         VMW_LIBS += -lvmkmgmt
      endif
      ifneq ($(strip $(DEV_NEED_IMA)),)
         VMW_LIBS += -lvmkima -lvmkuser -lvmlibs -lcrypto -lssl -lz
         VMW_EXTRA_DEFS += \
            -I$(VMW_DRIVER_DEVKIT_BASE)/include -I$(CAYMAN_ZLIB_DIR)/include
      endif
   else
      $(error VMKernel Management or IMA library requested, but build $(VMW_BUILD) of nativeddk-devtools rpm not found.)
   endif
endif

ifneq ($(strip $(DEV_NEED_NICMGMT)),)
   VMW_LIBS += -lnicmgmt -lvmkuser
   VMW_EXTRA_DEFS += -I $(HEXDK_BASE)/include
endif

VMW_LINK_FLAGS += \
	$(DEV_$(DEV_OBJDIR)_PRE_LIBS) \
	$(VMW_LIB_DIRS) $(VMW_LIBS) \
	$(DEV_$(DEV_OBJDIR)_POST_LIBS)

# Usage:
# objects: sources
#	$(CC_CMD)
# The $* and $@ references take care of the grungy details as long as
# the objects/sources mapping is valid.

CC_CMD = $(VMW_$(VMW_TARGET)_CCBIN) -c -o $@ \
	$(VMW_EXTRA_DEFS) \
	$(VMW_$(VMW_TARGET)_COMPILEFLAGS) \
	$<

# Usage:
# deliverable: objects
#	$(LINK_CMD)
# This assumes that $(DEV_$(DEV_OBJDIR)_OBJECTS) lists all the object
# files that are to be linked to produce "deliverable".  That is, in
# fact, the semantic definition of that variable.

LINK_CMD = $(VMW_$(VMW_TARGET)_CCBIN) -o $@ \
	$(VMW_$(VMW_TARGET)_COMPILEFLAGS) \
	$(DEV_$(DEV_OBJDIR)_OBJECTS) \
	$(VMW_LINK_FLAGS)

VMW_EXTRA_DEFS += -I$(VMKAPI_LOCATION) -I$(VMKAPI_LOCATION)lib \
	-DVMK_DEVKIT_HAS_API_VMKAPI_BASE \
	-DVMK_DEVKIT_USES_PUBLIC_APIS \
	-DVMK_DEVKIT_USES_BINARY_COMPATIBLE_APIS

.PHONY: clean

#all-workbench: binaries xml-checks package



#.PHONY: path_chk
#path_chk:
#	PWD := $(shell pwd)
#	echo "Current Path"
#	echo $(CURRENT_DIR)
#	echo $(PWD)

STRIP = strip

SRC_DIR=../../src/
INC_DIR=-I../../include -I../../../opensea-common/include
CC=$(VMW_$(VMW_TARGET)_CCBIN)
AR=ar
#override CFLAGS = -Wall -c -fPIC -I. -std=gnu99
CFLAGS ?= -Wall 
CFLAGS += -c -fPIC -I. $(VMW_$(VMW_TARGET)_COMPILEFLAGS) -std=gnu99
CFLAGS += -DVMK_CROSS_COMP
LFLAGS = -Wall ../../../opensea-common/Make/vmware/$(FILE_OUTPUT_DIR)/libopensea-common.a $(VMW_LINK_FLAGS)
LIB_SRC_FILES = \
	$(SRC_DIR)ata_cmds.c\
	$(SRC_DIR)ata_legacy_cmds.c\
	$(SRC_DIR)ata_helper.c\
	$(SRC_DIR)cmds.c\
	$(SRC_DIR)common_public.c\
	$(SRC_DIR)sat_helper.c\
	$(SRC_DIR)scsi_cmds.c\
	$(SRC_DIR)scsi_helper.c\
	$(SRC_DIR)nvme_cmds.c\
	$(SRC_DIR)nvme_helper.c\
	$(SRC_DIR)psp_legacy_helper.c\
	$(SRC_DIR)cypress_legacy_helper.c\
	$(SRC_DIR)ti_legacy_helper.c\
	$(SRC_DIR)nec_legacy_helper.c\
	$(SRC_DIR)prolific_legacy_helper.c\
	$(SRC_DIR)usb_hacks.c\
	$(SRC_DIR)sata_helper_func.c\
	$(SRC_DIR)raid_scan_helper.c\
	$(SRC_DIR)sntl_helper.c\
	$(SRC_DIR)jmicron_nvme_helper.c\
	$(SRC_DIR)asmedia_nvme_helper.c\
	$(SRC_DIR)realtek_nvme_helper.c\
	$(SRC_DIR)csmi_legacy_pt_cdb_helper.c\
	$(SRC_DIR)csmi_helper.c

UNAME := $(shell uname)
ifeq ($(UNAME),Linux)
	LIB_SRC_FILES += $(SRC_DIR)vm_helper.c $(SRC_DIR)vm_nvme_lib.c $(SRC_DIR)vm_nvme_mgmt_common.c
endif
ifeq ($(UNAME),SunOS)
    LIB_SRC_FILES += $(SRC_DIR)uscsi_helper.c
endif
ifeq (($UNAME),FreeBSD)
    LIB_SRC_FILES += $(SRC_DIR)cam_helper.c
endif

PROJECT_DEFINES += $(VMW_EXTRA_DEFS) #-DDISABLE_NVME_PASSTHROUGH #-D_DEBUG
#PROJECT_DEFINES += -DDISABLE_NVME_PASSTHROUGH #-D_DEBUG

#All of the source files have associated object files
LIB_OBJ_FILES = $(LIB_SRC_FILES:.c=.o)
LIBS = lib$(NAME).a
#DEPFILES = $(LIB_SRC_FILES:.c=.d)

#-include $(DEPFILES)

.PHONY: all 

all: clean mkoutputdir $(LIBS)

opensea-libs:
	$(MAKE) -C ../../../opensea-common/Make/vmware

%.o: %.c
	$(CC) $(CFLAGS) $(PROJECT_DEFINES) $(DEV_$(DEV_OBJDIR)_OBJECTS) $(INC_DIR) $< -o $@

$(LIBS): $(LIB_OBJ_FILES) opensea-libs
	rm -f $(FILE_OUTPUT_DIR)/$@
	$(AR) cq $(FILE_OUTPUT_DIR)/$@ $(LIB_OBJ_FILES)
	#$(CC) -shared -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION) $(LIB_OBJ_FILES) $(LFLAGS) $(DEV_$(DEV_OBJDIR)_OBJECTS)
	$(CC) $(VMW_$(VMW_TARGET)_COMPILEFLAGS) -shared -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION) $(LIB_OBJ_FILES) $(LFLAGS) $(DEV_$(DEV_OBJDIR)_OBJECTS)
	cd $(FILE_OUTPUT_DIR) && ln -s lib$(NAME).so* lib$(NAME).so
	
clean:
	rm -f $(FILE_OUTPUT_DIR)/lib$(NAME).a $(FILE_OUTPUT_DIR)/lib$(NAME).so* *.o ../../src/*.o
	rm -rf $(FILE_OUTPUT_DIR)

mkoutputdir:
	mkdir -p $(FILE_OUTPUT_DIR)

