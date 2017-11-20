//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2017 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// \file csmi_helper.h
// \brief Defines the constants structures to help with CSMI implementation. This tries to be generic for any OS, even though Windows is the only known supported OS (pending what driver you use)

#pragma once

#if defined (ENABLE_CSMI)

#include "common.h"
#include <stdint.h>
#include "csmisas.h"
#include "scsi_helper.h"

#if defined (__cplusplus)
extern "C"
{
#endif

    #define COMMAND_BIT_MASK			(0x80)
    #if defined(_WIN32)
        #define WIN_CSMI_DRIVE	"\\\\.\\SCSI"
    #elif defined (__linux__)
        #define LIN_CSMI_DRIVE	"/dev/hba"
    #else
        #message Unknown OS...may or may not need a device prefix.
    #endif

    #define CSMI_DEBUG_LEVEL 0 //set to values 1 - 4 to debug different verbosity levels...this is a define because I thought it was easier to use for the conversion than another variable..this shouldn't be on in released code anyways

    typedef enum
    {
	    FIS_TYPE_REG_H2D	= 0x27,	// Register FIS - host to device
	    FIS_TYPE_REG_D2H	= 0x34,	// Register FIS - device to host
	    FIS_TYPE_DMA_ACT	= 0x39,	// DMA activate FIS - device to host
	    FIS_TYPE_DMA_SETUP	= 0x41,	// DMA setup FIS - bidirectional
	    FIS_TYPE_DATA		= 0x46,	// Data FIS - bidirectional
	    FIS_TYPE_BIST		= 0x58,	// BIST activate FIS - bidirectional
	    FIS_TYPE_PIO_SETUP	= 0x5F,	// PIO setup FIS - device to host
	    FIS_TYPE_DEV_BITS	= 0xA1,	// Set device bits FIS - device to host
	    FIS_TYPE_RES_FUT1	= 0xA6,	// Reserved
	    FIS_TYPE_RES_FUT2	= 0xB8,	// Reserved
	    FIS_TYPE_RES_FUT3	= 0xBF,	// Reserved
	    FIS_TYPE_VEN_UNQ1	= 0xC7,	// Vendor Specific 
	    FIS_TYPE_VEN_UNQ2	= 0xD4,	// Vendor Specific 
	    FIS_TYPE_RES_FUT4	= 0xD9	// Reserved.
    } FIS_TYPE;

    #pragma pack(push, 1)
    typedef struct _FIS_REG_H2D
    {
	    // DWORD 0
	    uint8_t	fisType;            // FIS_TYPE_REG_H2D
 
        union {
            uint8_t	byte1;          //Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler to handle your bit field correctly! they can order them differently! - TJE
            struct {
                uint8_t	pmport:4;   // Port multiplier
                uint8_t	rsv0 : 3;   // Reserved
                uint8_t	c : 1;      // 1: Command, 0: Control
            }crrr_port;
        };
 
        uint8_t	command;            // Command register
        uint8_t	feature;            // Feature register, 7:0
 
	    // DWORD 1
        uint8_t	lbaLow;             // LBA low register, 7:0
        uint8_t	lbaMid;             // LBA mid register, 15:8
        uint8_t	lbaHi;              // LBA high register, 23:16
        uint8_t	device;             // Device register
 
	    // DWORD 2
        uint8_t	lbaLowExt;          // LBA register, 31:24
        uint8_t	lbaMidExt;          // LBA register, 39:32
        uint8_t	lbaHiExt;           // LBA register, 47:40
        uint8_t	featureExt;         // Feature register, 15:8
 
	    // DWORD 3
        uint8_t	sectorCount;        // Count register, 7:0
        uint8_t	sectorCountExt;     // Count register, 15:8
        uint8_t	icc;                // Isochronous command completion
        uint8_t	control;            // Control register
 
	    // DWORD 4
        uint8_t aux1;               //auxilary (7:0)
        uint8_t aux2;               //auxilary (15:8)
        uint8_t aux3;               //auxilary (23:16)
        uint8_t aux4;               //auxilary (31:24)
    } FIS_REG_H2D, *pFIS_REG_H2D;

    typedef struct _FIS_REG_D2H
    {
        // DWORD 0
        uint8_t	fisType;            // FIS_TYPE_REG_D2H = 34h

        union {
            uint8_t	byte1;          //Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler to handle your bit field correctly! they can order them differently! - TJE
            struct {
                uint8_t	pmport : 4;   // Port multiplier
                uint8_t	rsv0 : 2;   // Reserved
                uint8_t interupt : 1; //interupt bit
                uint8_t	rsv1 : 1;      // Reserved
            }rirr_port;
        };

        uint8_t	status;             // status register
        uint8_t	error;              // error register

        // DWORD 1
        uint8_t	lbaLow;             // LBA low register, 7:0
        uint8_t	lbaMid;             // LBA mid register, 15:8
        uint8_t	lbaHi;              // LBA high register, 23:16
        uint8_t	device;             // Device register

        // DWORD 2
        uint8_t	lbaLowExt;          // LBA register, 31:24
        uint8_t	lbaMidExt;          // LBA register, 39:32
        uint8_t	lbaHiExt;           // LBA register, 47:40
        uint8_t	reserved0;          // reserved

        // DWORD 3
        uint8_t	sectorCount;        // Count register, 7:0
        uint8_t	sectorCountExt;     // Count register, 15:8
        uint8_t	reserved1;          // reserved
        uint8_t	reserved2;          // reserved

        // DWORD 4
        uint8_t	reserved3;          // reserved
        uint8_t	reserved4;          // reserved
        uint8_t	reserved5;          // reserved
        uint8_t	reserved6;          // reserved
    }FIS_REG_D2H, *pFIS_REG_D2H;

    typedef struct _FIS_REG_PIO_SETUP
    {
        // DWORD 0
        uint8_t	fisType;            // FIS_TYPE_REG_D2H = 5Fh

        union {
            uint8_t	byte1;          //Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler to handle your bit field correctly! they can order them differently! - TJE
            struct {
                uint8_t	pmport : 4;   // Port multiplier
                uint8_t	rsv0 : 1;   // Reserved
                uint8_t dataDir : 1; //1 = D2H, 0 = H2D
                uint8_t interupt : 1; //interupt bit
                uint8_t	rsv1 : 1;      // Reserved
            }ridr_port;
        };

        uint8_t	status;             // status register
        uint8_t	error;              // error register

        // DWORD 1
        uint8_t	lbaLow;             // LBA low register, 7:0
        uint8_t	lbaMid;             // LBA mid register, 15:8
        uint8_t	lbaHi;              // LBA high register, 23:16
        uint8_t	device;             // Device register

        // DWORD 2
        uint8_t	lbaLowExt;          // LBA register, 31:24
        uint8_t	lbaMidExt;          // LBA register, 39:32
        uint8_t	lbaHiExt;           // LBA register, 47:40
        uint8_t	reserved0;          // reserved

        // DWORD 3
        uint8_t	sectorCount;        // Count register, 7:0
        uint8_t	sectorCountExt;     // Count register, 15:8
        uint8_t	reserved1;          // reserved
        uint8_t	eStatus;            // E_Status a.k.a. ending status register.

        // DWORD 4
        uint8_t	transferCount;      // transfer count 7:0
        uint8_t	transferCountHi;    // transfer count 15:8
        uint8_t	reserved2;          // reserved
        uint8_t	reserved3;          // reserved
    }FIS_REG_PIO_SETUP, *pFIS_REG_PIO_SETUP;
    #pragma pack(pop)

    //This is all the data, minus the IOCTL header, of the CSMI_SAS_GET_SCSI_ADDRESS_BUFFER
    //Defined here since I don't want to be responsible for adding it onto the csmisas.h file - TJE
    typedef struct _csmiSCSIAddress
    {
        uint8_t hostIndex;
        uint8_t pathId;
        uint8_t targetId;
        uint8_t lun;
    }csmiSCSIAddress, *ptrCSMISCSIAddress;

    typedef struct _csmiDevice
    {
        //NOTE: this is currently Windows only since there does not seem to be any Linux drivers that support CSMI at this time
        //CSMI specific data to use for talking to drives with CSMI supported drivers
        //This section may be expanded with other information later depending on what we find during testing.
        //use for talking to driver. Not useful for other purposes. Changing this will break communication with the driver
        uint8_t         sasAddress[8];
        uint8_t         phyIdentifier;
        uint8_t         portIdentifier;
        uint8_t         portProtocol;//get's set to what is returned by the driver. This will be stored to
        bool            useSSPInsteadOfSTP;
        bool            ataVendorUniquePT;//This will be set when STP is not available and issuing SSP w/SAT command does NOT work. Do not set this since it uses a never adopeted passthrough method
        CSMI_SAS_DRIVER_INFO driverInfo;//stored information reported by the driver to assist with driver specific details. Can be used to customize how to issue IO depending on and driver specific bugs found.
        CSMI_SAS_CNTLR_CONFIG controllerConfig;//stored information reported by the driver to assist with controller specific details. Can be used to customize how to issue IO depending on and controller specific bugs found.
        CSMI_SAS_PHY_ENTITY phyInfo;//only the PHY info for this specific device!
        bool scsiAddressValid;
        csmiSCSIAddress scsiAddress;
        bool sataSignatureValid;
        CSMI_SAS_SATA_SIGNATURE sataSignature;
        bool csmiVerbose;//set this to true to enable verbose output from all the IOCTLs sent in the CSMI code...this may help with debugging what is going on.
    }csmiDevice, *ptrCSMIDevice;

#if defined (__cplusplus)
}
#endif
#endif