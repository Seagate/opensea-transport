#
# Do NOT modify or remove this copyright and license
#
# Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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
MINOR=17
#Change the patch version when only bug fixes are made.
PATCH=5
VERSION=$(MAJOR).$(MINOR).$(PATCH)
SRC_DIR=../../src/
INC_DIR=-I../../include -I../../../opensea-common/include
CC=gcc
#override CFLAGS = -Wall -c -fPIC -I. -std=gnu99
CFLAGS ?= -Wall 
CFLAGS += -c -fPIC -I. -std=gnu99
LFLAGS = -Wall ../../../opensea-common/Make/gcc/lib/$(FILE_OUTPUT_DIR)/libopensea-common.a
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
	$(SRC_DIR)prolific_legacy_helper.c

UNAME := $(shell uname)
ifeq ($(UNAME),Linux)
	LIB_SRC_FILES += $(SRC_DIR)sg_helper.c
endif
ifeq ($(UNAME),SunOS)
    LIB_SRC_FILES += $(SRC_DIR)uscsi_helper.c
endif
ifeq (($UNAME),FreeBSD)
    LIB_SRC_FILES += $(SRC_DIR)cam_helper.c
endif

PROJECT_DEFINES += #-DDISABLE_NVME_PASSTHROUGH #-D_DEBUG

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
	ar cq $(FILE_OUTPUT_DIR)/$@ $(LIB_OBJ_FILES)
	$(CC) -shared $(LIB_OBJ_FILES) -o $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION)
clean:
	rm -f $(FILE_OUTPUT_DIR)/lib$(NAME).a $(FILE_OUTPUT_DIR)/lib$(NAME).so.$(VERSION) *.o ../../src/*.o
	rm -rf $(FILE_OUTPUT_DIR)

mkoutputdir:
	mkdir -p $(FILE_OUTPUT_DIR)

