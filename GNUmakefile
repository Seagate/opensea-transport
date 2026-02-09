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
# \file GNUmakefile
# \brief Standalone GNU makefile for opensea-transport library
#
# This makefile can build opensea-transport independently of the main project.
# It requires opensea-common to be built first (dependency).
# Requires GNU Make - not compatible with BSD make or other make implementations.

# Directories
TRANSPORT_DIR := $(CURDIR)
COMMON_DIR ?= $(TRANSPORT_DIR)/../opensea-common
SRC_DIR := $(TRANSPORT_DIR)/src
INCLUDE_DIR := $(TRANSPORT_DIR)/include

# Include main build system modules (reuse existing infrastructure)
MAKE_DIR := $(TRANSPORT_DIR)/../../Make
include $(MAKE_DIR)/config.mk

# Override BUILD_DIR for standalone builds (use local build directory)
BUILD_DIR := $(TRANSPORT_DIR)/build
OBJ_DIR := $(BUILD_DIR)/obj
LIB_DIR := $(BUILD_DIR)/lib
include $(MAKE_DIR)/compiler-detection.mk
include $(MAKE_DIR)/compiler-flags.mk
include $(MAKE_DIR)/security-hardening.mk
include $(MAKE_DIR)/platforms/$(PLATFORM).mk

# Include source list from this subproject
include $(TRANSPORT_DIR)/sources.mk

# Set default goal (must be after all includes)
.DEFAULT_GOAL := all

# Compiler flags
CFLAGS := $(TRANSPORT_CFLAGS) $(WARNING_FLAGS) $(SECURITY_CFLAGS) \
          -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/vendor -I$(COMMON_DIR)/include \
          $(PLATFORM_DEFINES)

# Object files
OBJS := $(addprefix $(OBJ_DIR)/,$(TRANSPORT_SOURCES:.c=.o))
DEPS := $(OBJS:.o=.d)

# Library target
LIBTRANSPORT_STATIC := $(LIB_DIR)/libopensea-transport.a

# vpath for source discovery (platform-specific subdirectories)
vpath %.c $(SRC_DIR)
vpath %.c $(SRC_DIR)/linux
vpath %.c $(SRC_DIR)/windows
vpath %.c $(SRC_DIR)/freebsd
vpath %.c $(SRC_DIR)/openbsd
vpath %.c $(SRC_DIR)/netbsd
vpath %.c $(SRC_DIR)/solaris
vpath %.c $(SRC_DIR)/aix

#===============================================================================
# Targets
#===============================================================================

.PHONY: all clean distclean help

all: $(LIBTRANSPORT_STATIC)

$(LIBTRANSPORT_STATIC): $(OBJS) | $(LIB_DIR)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR) $(LIB_DIR):
	@mkdir -p $@

clean:
	@echo "Cleaning opensea-transport build artifacts..."
	@rm -rf $(BUILD_DIR)

distclean: clean
	@echo "Distclean complete for opensea-transport"

help:
	@echo "opensea-transport standalone build targets:"
	@echo "  all       - Build libopensea-transport.a (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  distclean - Remove all generated files"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_DIR  - Build output directory (default: ./build)"
	@echo "  COMMON_DIR - Path to opensea-common (default: ../opensea-common)"
	@echo "  CC         - C compiler (default: auto-detected)"
	@echo "  PLATFORM   - Target platform (default: auto-detected)"
	@echo ""
	@echo "Note: opensea-common must be built before opensea-transport"

# Include dependency files
-include $(DEPS)
