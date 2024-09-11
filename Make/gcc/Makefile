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

# Hand Written Makefile (Edit with caution) -Muhammad
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
SRC_DIR=../../src/
INC_DIR=-I../../include -I../../include/vendor -I../../../opensea-common/include  
CC ?= gcc
AR ?= ar

#override CFLAGS = -Wall -c -fPIC -I.
CFLAGS ?= -Wall -Wextra 
CFLAGS += -c -fPIC -I.

#NOTE -Wsign-conversion can be useful but warns way too much by default. Only enable it if debugging specific problems
COMPILER_VERSION := $(shell $(CC) --version)
ifneq (,$(findstring clang,$(COMPILER_VERSION)))
	#setup clang specific warnings/flags (centos 7's old version supports -Wno-unknown-warning-option so enabling all of them)
	CFLAGS += -Wno-unknown-warning-option -Wcast-align=strict -Wvla -Wfloat-equal -Wnull-dereference -Wunused-const-variable \
	-Wduplicated-cond -Wjump-misses-init -Wstringop-overflow -Wlogical-op -Wshift-overflow=2 -Wdouble-promotion -Wformat-security \
	-Wold-style-definition -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes
else 
	ifneq (,$(findstring GCC,$(COMPILER_VERSION)))
		#setup gcc specific warnings/flags
		GCC_VERSION_STRING = $(shell $(CC) -dumpversion)
		GCC_VER = $(subst ., ,$(GCC_VERSION_STRING))
		GCC_MAJOR = $(word 1,$(GCC_VER))
		GCC_MINOR = $(word 2,$(GCC_VER))
		GCC_SUBMINOR = $(word 3,$(GCC_VER))
		ifeq ($(GCC_MINOR),)
			GCC_MINOR = 0
		endif 
		ifeq ($(GCC_SUBMINOR),)
			GCC_SUBMINOR = 0
		endif
		#version 8.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 7; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wcast-align=strict
			else 
				CFLAGS += -Wcast-align
			endif
		else
			CFLAGS += -Wcast-align
		endif
		#version 7.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 6; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wshadow=compatible-local -Wstringop-overflow
			else 
				CFLAGS +=
			endif
		else
			CFLAGS +=
		endif
		#version 6.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 5; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wnull-dereference -Wunused-const-variable -Wduplicated-cond -Wshift-overflow=2
			else 
				CFLAGS +=
			endif
		else
			CFLAGS +=
		endif
		#version 5.5 and higher
		ifeq ($(shell test $(GCC_MAJOR) -gt 4; echo $$?),0)
			ifeq ($(shell test $(GCC_MINOR) -gt 4; echo $$?),0)
				CFLAGS += -Wlogical-not-parentheses
			endif
		else
			#GCC less that v 5.x.x needs to set gnu99 standard
			#as of 5.x.x, gnu11 is default
			#OLD NOTE: setting std=c99 instead of std-gnu99 because we want __has_include to work. This means we must add -D_GNU_SOURCE to the project defines to keep the same functionality in Linux. NOTE: This seems to work in newGCC, but if you are unfortunate enough to HAVE to use old GCC within somehting like CentOS6, you must change back to gnu99.
			CFLAGS += -std=gnu99
		endif
		
		CFLAGS += -Wvla -Wfloat-equal -Wjump-misses-init -Wlogical-op -Wdouble-promotion -Wformat-security \
			-Wold-style-definition -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes
	else
		CFLAGS += -std=gnu99
		CFLAGS += -Wvla -Wfloat-equal -Wjump-misses-init -Wlogical-op -Wdouble-promotion -Wformat-security \
				-Wold-style-definition -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes
	endif
endif
UNAME := $(shell uname -s)

LDFLAGS ?= -Wall -L../../../opensea-common/Make/gcc/$(FILE_OUTPUT_DIR)/ -lopensea-common -lm 

#AIX wants all linker libraries for the .so. Need libodm and libcfg in addition to the above to resolve all symbols
ifeq ($(UNAME),AIX)
	LDFLAGS += -lodm -lcfg
endif

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
	$(SRC_DIR)csmi_helper.c\
	$(SRC_DIR)ciss_helper.c

UNAME := $(shell uname)

ifeq ($(UNAME),Linux)
	LIB_SRC_FILES += $(SRC_DIR)sg_helper.c
	#determine the proper NVMe include file. SEA_NVME_IOCTL_H, SEA_NVME_H, or SEA_UAPI_NVME_H
	NVME_IOCTL_H = /usr/include/linux/nvme_ioctl.h 
	NVME_H = /usr/include/linux/nvme.h 
	UAPI_NVME_H = /usr/include/uapi/nvme.h 

	ifeq ($(shell test -f $(NVME_IOCTL_H) && printf "yes"),yes)
		PROJECT_DEFINES += -DSEA_NVME_IOCTL_H
	else
		ifeq ($(shell test -f $(NVME_H) && printf "yes"),yes)
			PROJECT_DEFINES += -DSEA_NVME_H
		else
			ifeq ($(shell test -f $(UAPI_NVME_H) && printf "yes"),yes)
				PROJECT_DEFINES += -DSEA_UAPI_NVME_H
			else
				PROJECT_DEFINES += -DDISABLE_NVME_PASSTHROUGH
			endif
		endif
	endif
endif

ifeq ($(UNAME),SunOS)
    LIB_SRC_FILES += $(SRC_DIR)uscsi_helper.c
	PROJECT_DEFINES += -DDISABLE_NVME_PASSTHROUGH
endif

ifeq ($(UNAME),FreeBSD)
    LIB_SRC_FILES += $(SRC_DIR)cam_helper.c
	FREEBSD_NVME_H = /usr/include/dev/nvme/nvme.h
	ifneq ($(shell test -f $(FREEBSD_NVME_H) && printf "yes"),yes)
		PROJECT_DEFINES += -DDISABLE_NVME_PASSTHROUGH
	endif
endif

ifeq ($(UNAME),AIX)
	LIB_SRC_FILES += $(SRC_DIR)aix_helper.c
	AIX_NVME_H = /usr/include/sys/nvme.h
	ifneq ($(shell test -f $(AIX_NVME_H) && printf "yes"),yes)
		PROJECT_DEFINES += -DDISABLE_NVME_PASSTHROUGH
	endif
endif

#setting std=c99 instead of std-gnu99 because we want __has_include to work. This means we must add -D_GNU_SOURCE to the project defines to keep the same functionality in Linux
#PROJECT_DEFINES += -D_GNU_SOURCE -DDISABLE_NVME_PASSTHROUGH #-D_DEBUG
PROJECT_DEFINES += -D_GNU_SOURCE -DENABLE_CISS -DENABLE_CSMI

#All of the source files have associated object files
LIB_OBJ_FILES = $(LIB_SRC_FILES:.c=.o)
LIBS = lib$(NAME).a
#DEPFILES = $(LIB_SRC_FILES:.c=.d)

#-include $(DEPFILES)

.PHONY: all 

all: clean mkoutputdir $(LIBS)

opensea-libs:
	$(MAKE) -C ../../../opensea-common/Make/gcc

%.o: %.c
	$(CC) $(CFLAGS) $(PROJECT_DEFINES) $(INC_DIR) $< -o $@

$(LIBS): $(LIB_OBJ_FILES) opensea-libs
	rm -f $(FILE_OUTPUT_DIR)/$@
	$(AR) cq $(FILE_OUTPUT_DIR)/$@ $(LIB_OBJ_FILES)
	$(CC) -shared $(LIB_OBJ_FILES) -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION) $(LDFLAGS)
	cd $(FILE_OUTPUT_DIR) && ln -sf lib$(NAME).so.$(VERSION) lib$(NAME).so
	
clean:
	rm -f $(FILE_OUTPUT_DIR)/lib$(NAME).a $(FILE_OUTPUT_DIR)/lib$(NAME).so* *.o ../../src/*.o
	rm -rf $(FILE_OUTPUT_DIR)

mkoutputdir:
	mkdir -p $(FILE_OUTPUT_DIR)

