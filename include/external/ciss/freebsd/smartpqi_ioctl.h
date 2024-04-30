/* SPDX-License-Identifier: BSD-2-Clause  */
/*-
 * Copyright (c) 2018 Microsemi Corporation.
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

/* $FreeBSD$ */

#ifndef	_PQI_IOCTL_H_
#define	_PQI_IOCTL_H_

//opensea-transport NOTE: Combined from FreeBSD kernel source:
//   smartpqi_defines.h
//   smartpqi_ioctl.h
//structures may have slight name changes to prevent type redefinitions and
//other errors from cissio.h which is also used in opensea-transport

/* IOCTL passthrough macros and structures */

#define SENSEINFOBYTES	32              /* note that this value may vary
 										 between host implementations */

/* transfer direction */
#define PQIIOCTL_NONE			0x00
#define PQIIOCTL_WRITE			0x01
#define PQIIOCTL_READ			0x02
#define PQIIOCTL_BIDIRECTIONAL		(PQIIOCTL_READ | PQIIOCTL_WRITE)

/* Type defs used in the following structs */
#define BYTE  	uint8_t
#define WORD  	uint16_t
#define HWORD 	uint16_t
#define DWORD 	uint32_t

#define OS_ATTRIBUTE_PACKED         __attribute__((__packed__))
#define OS_ATTRIBUTE_ALIGNED(n)     __attribute__((aligned(n)))

/* Management Interface */
#define CCISS_IOC_MAGIC		'C'
#define SMARTPQI_IOCTL_BASE     'M'
#define SMARTPQI_GETDRIVVER       _IOWR(SMARTPQI_IOCTL_BASE, 0, driver_info) //renamed to avoid colision with cissio.h
#define SMARTPQI_GETPCIINFO       _IOWR(SMARTPQI_IOCTL_BASE, 1, pqi_pci_info_t) //renamed to avoid colision with cissio.h
#define SMARTPQI_PASS_THRU     _IOWR(SMARTPQI_IOCTL_BASE, 2, pqi_IOCTL_Command_struct)
//#define CCISS_PASSTHRU         _IOWR('C', 210, IOCTL_Command_struct) //this is exactly the same as cissio.h, which is why it was commented out - TJE
//#define CCISS_REGNEWD          _IO(CCISS_IOC_MAGIC, 14)

/*IOCTL  pci_info structure */
typedef struct pqi_pci_info
{
       unsigned char   bus;
       unsigned char   dev_fn;
       unsigned short  domain;
       uint32_t        board_id;
       uint32_t        chip_id;
}pqi_pci_info_t;

typedef struct _driver_info
{
	unsigned char 	major_version;
	unsigned char 	minor_version;
	unsigned char 	release_version;
	unsigned long 	build_revision;
	unsigned long 	max_targets;
	unsigned long 	max_io;
	unsigned long 	max_transfer_length;
}driver_info, *pdriver_info;

typedef uint8_t *passthru_buf_type_t;

/* Command List Structure */
typedef union _pqi_SCSI3Addr_struct {
	  struct {
	   BYTE Dev;
	   BYTE Bus:6;
	   BYTE Mode:2; 	   /* b00 */
	 } PeripDev;
	  struct {
	   BYTE DevLSB;
	   BYTE DevMSB:6;
	   BYTE Mode:2; 	   /* b01 */
	 } LogDev;
	  struct {
	   BYTE Dev:5;
	   BYTE Bus:3;
	   BYTE Targ:6;
	   BYTE Mode:2; 	   /* b10 */
	 } LogUnit;

}OS_ATTRIBUTE_PACKED pqi_SCSI3Addr_struct;

typedef struct _pqi_PhysDevAddr_struct {
	 DWORD			   TargetId:24;
	 DWORD			   Bus:6;
	 DWORD			   Mode:2;
	 pqi_SCSI3Addr_struct  Target[2]; 	/* 2 level target device addr */
    
}OS_ATTRIBUTE_PACKED pqi_PhysDevAddr_struct;

typedef struct _pqi_LogDevAddr_struct {
	 DWORD			  VolId:30;
	 DWORD			  Mode:2;
	 BYTE			  reserved[4];
    
}OS_ATTRIBUTE_PACKED pqi_LogDevAddr_struct;

typedef union _pqi_LUNAddr_struct {
    BYTE               LunAddrBytes[8];
    pqi_SCSI3Addr_struct   SCSI3Lun[4];
    pqi_PhysDevAddr_struct PhysDev;
    pqi_LogDevAddr_struct  LogDev;

}OS_ATTRIBUTE_PACKED pqi_LUNAddr_struct;

typedef struct _pqi_RequestBlock_struct {
    BYTE  CDBLen;
    struct {
      BYTE Type:3;
      BYTE Attribute:3;
      BYTE Direction:2;
    } Type;
    HWORD  Timeout;
    BYTE   CDB[16];

}OS_ATTRIBUTE_PACKED pqi_RequestBlock_struct; 

typedef union _pqi_MoreErrInfo_struct{
   struct {
    BYTE  Reserved[3];
    BYTE  Type;
    DWORD ErrorInfo;
   } Common_Info;
   struct{
     BYTE  Reserved[2];
     BYTE  offense_size; /* size of offending entry */
     BYTE  offense_num;  /* byte # of offense 0-base */
     DWORD offense_value;
   } Invalid_Cmd;

}OS_ATTRIBUTE_PACKED pqi_MoreErrInfo_struct;

typedef struct _pqi_ErrorInfo_struct {
   BYTE               ScsiStatus;
   BYTE               SenseLen;
   HWORD              CommandStatus;
   DWORD              ResidualCnt;
   pqi_MoreErrInfo_struct MoreErrInfo;
   BYTE               SenseInfo[SENSEINFOBYTES];

}OS_ATTRIBUTE_PACKED pqi_ErrorInfo_struct;

typedef struct pqi_ioctl_passthruCmd_struct {
	pqi_LUNAddr_struct           LUN_info;
	pqi_RequestBlock_struct      Request;
	pqi_ErrorInfo_struct         error_info; 
   	WORD                     buf_size;  /* size in bytes of the buf */
	passthru_buf_type_t		buf;

}OS_ATTRIBUTE_PACKED pqi_IOCTL_Command_struct;

#endif  /* _PQI_IOCTL_H_ */
