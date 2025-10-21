// SPDX-License-Identifier: BSD-2-Clause and MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
// \file aac_ioctl.h
// \brief FreeBSD aac_ioctl userland file to issue IOCTLs to AAC and AAC raid devices.
//        Modifications by Seagate under MPL-2.0
//Modifications:
//  removed not needed kernel headers and definitions
//  converted types to work cross-platform
//  added ifdefs to switch between Windows, Linux, FreeBSD, and Solaris

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 Scott Long
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Command queue statistics
 */
#define AACQ_FREE	0
#define AACQ_BIO	1
#define AACQ_READY	2
#define AACQ_BUSY	3
#define AACQ_COUNT	4	/* total number of queues */

#include <stdlib.h>
#include <stdint.h>
#include "external/aac/aacraid_reg.h"

struct aac_qstat {
	uint32_t	q_length;
	uint32_t	q_max;
};

/*
 * Statistics request
 */
union aac_statrequest {
	uint32_t		as_item;
	struct aac_qstat	as_qstat;
};

#if !defined (_WIN32)
    #define AACIO_STATS		_IOWR('T', 101, union aac_statrequest)
#endif

#if !defined (CTL_CODE)
    /*
    * Ioctl commands likely to be submitted from a Linux management application.
    * These bit encodings are actually descended from Windows NT.  Ick.
    */

    #define CTL_CODE(devType, func, meth, acc) (((devType) << 16) | ((acc) << 14) | ((func) << 2) | (meth))
    #define METHOD_BUFFERED                 0
    #define METHOD_IN_DIRECT                1
    #define METHOD_OUT_DIRECT               2
    #define METHOD_NEITHER                  3
    #define FILE_ANY_ACCESS                 0
    #define FILE_READ_ACCESS          	( 0x0001 )
    #define FILE_WRITE_ACCESS         	( 0x0002 )
    #define FILE_DEVICE_CONTROLLER          0x00000004
#endif //CTL_CODE macro not defined (aka not Windows)

#define FSACTL_SENDFIB		CTL_CODE(FILE_DEVICE_CONTROLLER, 2050, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_SEND_RAW_SRB		CTL_CODE(FILE_DEVICE_CONTROLLER, 2067, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
//NOTE: Not in illumos driver - FSACTL_GET_COMM_PERF_DATA
#define FSACTL_GET_COMM_PERF_DATA	CTL_CODE(FILE_DEVICE_CONTROLLER, 2084, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
//NOTE: Not in illumos driver - FSACTL_OPENCLS_COMM_PERF_DATA
#define FSACTL_OPENCLS_COMM_PERF_DATA CTL_CODE(FILE_DEVICE_CONTROLLER, \
					2085, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_OPEN_GET_ADAPTER_FIB	CTL_CODE(FILE_DEVICE_CONTROLLER, 2100, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_GET_NEXT_ADAPTER_FIB	CTL_CODE(FILE_DEVICE_CONTROLLER, 2101, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_CLOSE_GET_ADAPTER_FIB CTL_CODE(FILE_DEVICE_CONTROLLER, \
					2102, METHOD_BUFFERED, FILE_ANY_ACCESS)
//NOTE: Not in illumos driver - FSACTL_CLOSE_ADAPTER_CONFIG
#define FSACTL_CLOSE_ADAPTER_CONFIG	CTL_CODE(FILE_DEVICE_CONTROLLER, 2104, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
//NOTE: Not in illumos driver - FSACTL_OPEN_ADAPTER_CONFIG
#define FSACTL_OPEN_ADAPTER_CONFIG	CTL_CODE(FILE_DEVICE_CONTROLLER, 2105, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_MINIPORT_REV_CHECK	CTL_CODE(FILE_DEVICE_CONTROLLER, 2107, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
//NOTE: Not in illumos driver - FSACTL_QUERY_ADAPTER_CONFIG
#define FSACTL_QUERY_ADAPTER_CONFIG	CTL_CODE(FILE_DEVICE_CONTROLLER, 2113, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_GET_PCI_INFO		CTL_CODE(FILE_DEVICE_CONTROLLER, 2119, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_FORCE_DELETE_DISK	CTL_CODE(FILE_DEVICE_CONTROLLER, 2120, \
					METHOD_NEITHER, FILE_ANY_ACCESS)
//NOTE: Not in illumos driver - FSACTL_AIF_THREAD
#define FSACTL_AIF_THREAD		CTL_CODE(FILE_DEVICE_CONTROLLER, 2127, \
					METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSACTL_SEND_LARGE_FIB	CTL_CODE(FILE_DEVICE_CONTROLLER, 2138, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	FSACTL_GET_FEATURES		CTL_CODE(FILE_DEVICE_CONTROLLER, 2139, \
					METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Why these don't follow the previous convention, I don't know */
// #define FSACTL_LNX_NULL_IO_TEST		0x43
// #define FSACTL_LNX_SIM_IO_TEST		0x53
// #define FSACTL_LNX_DOWNLOAD		0x83
// #define FSACTL_LNX_GET_VAR		0x93
// #define FSACTL_LNX_SET_VAR		0xa3
// #define FSACTL_LNX_GET_FIBTIMES		0xb3
// #define FSACTL_LNX_ZERO_FIBTIMES	0xc3
// #define FSACTL_LNX_DELETE_DISK		0x163
// #define FSACTL_LNX_QUERY_DISK		0x173

/* Ok, here it gets really lame */
//Seagate NOTE: This IOCTL code is defined like this on solaris and linux. In Windows it probably needs the CTL_CODE macro with this value
//#define FSACTL_LNX_PROBE_CONTAINERS	2131	/* Just guessing */

//Seagate Note: Using Linux/Windows codes since FreeBSD has compatibility with them.
//              While the native codes may be better, it is not clear when this was modified
//              so we will use the old ones to be sure we are compatible with both the built-in
//              driver and the Adaptec/PMC/Microchip distributed driver
//              Codes are left below. We can switch to them in the future as we learn more -TJE

/* Do the native version of the ioctls.  Since the BSD encoding scheme
 * conflicts with the 'standard' AAC encoding scheme, the resulting numbers
 * will be different.  The '8' comes from the fact that the previous scheme
 * used 12 bits for the number, with the 12th bit being the only set
 * bit above bit 8.  Thus the value of 8, with the lower 8 bits holding the
 * command number.  9 is used for the odd overflow case.
 */
// #define FSACTL_SENDFIB			_IO('8', 2)
// #define FSACTL_SEND_RAW_SRB		_IO('8', 19)
// #define FSACTL_GET_COMM_PERF_DATA	_IO('8', 36)
// #define FSACTL_OPENCLS_COMM_PERF_DATA	_IO('8', 37)
// #define FSACTL_OPEN_GET_ADAPTER_FIB	_IO('8', 52)
// #define FSACTL_GET_NEXT_ADAPTER_FIB	_IO('8', 53)
// #define FSACTL_CLOSE_GET_ADAPTER_FIB	_IO('8', 54)
// #define FSACTL_CLOSE_ADAPTER_CONFIG	_IO('8', 56)
// #define FSACTL_OPEN_ADAPTER_CONFIG	_IO('8', 57)
// #define FSACTL_MINIPORT_REV_CHECK	_IO('8', 59)
// #define FSACTL_QUERY_ADAPTER_CONFIG	_IO('8', 65)
// #define FSACTL_GET_PCI_INFO		_IO('8', 71)
// #define FSACTL_FORCE_DELETE_DISK	_IO('8', 72)
// #define FSACTL_AIF_THREAD		_IO('8', 79)
// #define FSACTL_SEND_LARGE_FIB		_IO('8', 90)
// #define	FSACTL_GET_FEATURES		_IO('8', 91)

// #define FSACTL_NULL_IO_TEST		_IO('8', 67)
// #define FSACTL_SIM_IO_TEST		_IO('8', 83)
// #define FSACTL_DOWNLOAD			_IO('8', 131)
// #define FSACTL_GET_VAR			_IO('8', 147)
// #define FSACTL_SET_VAR			_IO('8', 163)
// #define FSACTL_GET_FIBTIMES		_IO('8', 179)
// #define FSACTL_ZERO_FIBTIMES		_IO('8', 195)
// #define FSACTL_DELETE_DISK		_IO('8', 99)
// #define FSACTL_QUERY_DISK		_IO('9', 115)

// #define FSACTL_PROBE_CONTAINERS		_IO('9', 83)	/* Just guessing */

/*
 * Support for faking the "miniport" version.
 */
struct aac_rev_check {
	RevComponent		callingComponent;
	struct FsaRevision	callingRevision;
};

struct aac_rev_check_resp {
	int			possiblyCompatible;
	struct FsaRevision	adapterSWRevision;
};

/*
 * Context passed in by a consumer looking to collect an AIF.
 */
struct get_adapter_fib_ioctl {
	uint32_t	AdapterFibContext;
	int	  	Wait;
	char*		AifFib;//Seagate note: Was type caddr_t which is just char*
};

#ifdef _KERNEL
struct get_adapter_fib_ioctl32 {
	uint32_t	AdapterFibContext;
	int	  	Wait;
	uint32_t	AifFib;
};
#endif

struct aac_query_disk {
	int32_t		ContainerNumber;
	int32_t		Bus;
	int32_t		Target;
	int32_t		Lun;
	uint32_t	Valid;
	uint32_t	Locked;
	uint32_t	Deleted;
	int32_t		Instance;
	char		diskDeviceName[10];
	uint32_t	UnMapped;
};

/* Features, asked from the tools to know if the driver
 * supports drives >2TB
 */
typedef union {
	struct {
		uint32_t largeLBA  : 1;	/* disk support greater 2TB */
		uint32_t IoctlBuf  : 1;	/* ARCIOCTL call support */
		uint32_t AIFSupport: 1;	/* AIF support */
		uint32_t JBODSupport:1;	/* fw + driver support JBOD */
		uint32_t fReserved : 28;
	} fBits;
	uint32_t fValue;
} featuresState;

M_AAC_PACKED_STRUCT(aac_features,
	featuresState feat;
	uint32_t data[31];
	uint32_t reserved[32];
);
