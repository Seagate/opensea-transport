#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012 - 2019 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
MAJOR=1
#Change the Minor version when new features are added. 
MINOR=21
#Change the patch version when only bug fixes are made.
PATCH=16
VERSION=$(MAJOR).$(MINOR).$(PATCH)
SRC_DIR=../../src/
INC_DIR=-I../../include -I../../include/vendor -I../../../opensea-common/include  
CC ?= gcc
AR ?= ar

#determine the proper NVMe include file. SEA_NVME_IOCTL_H, SEA_NVME_H, or SEA_UAPI_NVME_H
NVME_IOCTL_H = /usr/include/linux/nvme_ioctl.h 
NVME_H = /usr/include/linux/nvme.h 
UAPI_NVME_H = /usr/include/uapi/nvme.h 

ifeq ($(shell test -f $(NVME_IOCTL_H) && echo -n yes),yes)
	PROJECT_DEFINES += -DSEA_NVME_IOCTL_H
else
	ifeq ($(shell test -f $(NVME_H) && echo -n yes),yes)
		PROJECT_DEFINES += -DSEA_NVME_H
	else
		ifeq ($(shell test -f $(UAPI_NVME_H) && echo -n yes),yes)
			PROJECT_DEFINES += -DSEA_UAPI_NVME_H
		endif
	endif
endif
#override CFLAGS = -Wall -c -fPIC -I. -std=gnu99
CFLAGS ?= -Wall 
#setting std=c99 instead of std-gnu99 because we want __has_include to work. This means we must add -D_GNU_SOURCE to the project defines to keep the same functionality in Linux. NOTE: This seems to work in newGCC, but if you are unfortunate enough to HAVE to use old GCC within somehting like CentOS6, you must change back to gnu99.
CFLAGS += -c -fPIC -I. -std=gnu99
LFLAGS ?= -Wall ../../../opensea-common/Make/gcc/lib/$(FILE_OUTPUT_DIR)/libopensea-common.a
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
	$(SRC_DIR)sntl_helper.c\
	$(SRC_DIR)jmicron_nvme_helper.c\
	$(SRC_DIR)asmedia_nvme_helper.c

UNAME := $(shell uname)
ifeq ($(UNAME),Linux)
	LIB_SRC_FILES += $(SRC_DIR)sg_helper.c
endif
ifeq ($(UNAME),SunOS)
    LIB_SRC_FILES += $(SRC_DIR)uscsi_helper.c
endif
ifeq ($(UNAME),FreeBSD)
    LIB_SRC_FILES += $(SRC_DIR)cam_helper.c
endif

#setting std=c99 instead of std-gnu99 because we want __has_include to work. This means we must add -D_GNU_SOURCE to the project defines to keep the same functionality in Linux
#PROJECT_DEFINES += -D_GNU_SOURCE -DDISABLE_NVME_PASSTHROUGH #-D_DEBUG
PROJECT_DEFINES += -D_GNU_SOURCE 

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
	$(CC) -shared $(LIB_OBJ_FILES) -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION)
	cd $(FILE_OUTPUT_DIR) && ln -s lib$(NAME).so* lib$(NAME).so
	
clean:
	rm -f $(FILE_OUTPUT_DIR)/lib$(NAME).a $(FILE_OUTPUT_DIR)/lib$(NAME).so* *.o ../../src/*.o
	rm -rf $(FILE_OUTPUT_DIR)

mkoutputdir:
	mkdir -p $(FILE_OUTPUT_DIR)

