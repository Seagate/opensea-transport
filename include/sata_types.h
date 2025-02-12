// SPDX-License-Identifier: MPL-2.0

//! \file sata_types.h
//! \brief Defines types that are specific to SATA interface. Mostly FIS structures that may be used by some
//! passthrough interfaces. This file should only have things unique to SATA transport.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "common_types.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    typedef enum eSataFisTypeEnum
    {
        FIS_TYPE_REG_H2D   = 0x27, // Register FIS - host to device
        FIS_TYPE_REG_D2H   = 0x34, // Register FIS - device to host
        FIS_TYPE_DMA_ACT   = 0x39, // DMA activate FIS - device to host
        FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS - bidirectional
        FIS_TYPE_DATA      = 0x46, // Data FIS - bidirectional
        FIS_TYPE_BIST      = 0x58, // BIST activate FIS - bidirectional
        FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS - device to host
        FIS_TYPE_DEV_BITS  = 0xA1, // Set device bits FIS - device to host
        FIS_TYPE_RES_FUT1  = 0xA6, // Reserved
        FIS_TYPE_RES_FUT2  = 0xB8, // Reserved
        FIS_TYPE_RES_FUT3  = 0xBF, // Reserved
        FIS_TYPE_VEN_UNQ1  = 0xC7, // Vendor Specific
        FIS_TYPE_VEN_UNQ2  = 0xD4, // Vendor Specific
        FIS_TYPE_RES_FUT4  = 0xD9  // Reserved.
    } eSataFisType;

#define PM_PORT_BIT_MASK (0x0F)

// FIS Lengths in bytes
#define H2D_FIS_LENGTH             UINT8_C(20)
#define D2H_FIS_LENGTH             UINT8_C(20)
#define SET_DEVICE_BITS_FIS_LENGTH UINT8_C(8)
#define DMA_ACTIVATE_FIS_LENGTH    UINT8_C(4)
#define DMA_SETUP_FIS_LENGTH       UINT8_C(28)
#define BIST_ACTIVATE_FIS_LENGTH   UINT8_C(12)
#define PIO_SETUP_FIS_LENGTH       UINT8_C(20)
// DATA fis is variable length depending on the data transfer length
#define DATA_FIS_LENGTH_MIN UINT16_C(8)
#define DATA_FIS_LENGTH_MAX UINT16_C(8196)
#define DATA_FIS_MAX_DWORDS UINT16_C(2048)
// Unknown or vendor unique FIS's may have various lengths, but we will print out at most 20 bytes of the FIS
#define UNKNOWN_FIS_TYPE_LENGTH UINT8_C(20)

// Bit masks for H2D Fis. use these instead of the bit fields for portability. Bitfields are more informational and
// useful in debugging environments that pack them in the order they are specified below (mostly checked in MSFT
// compilers)
#define H2D_COMMAND_BIT_MASK (0x80)

    typedef struct s_sataH2DFis
    {
        // DWORD 0
        uint8_t fisType; // FIS_TYPE_REG_H2D

        union
        {
            uint8_t byte1; // Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler
                           // to handle your bit field correctly! they can order them differently! - TJE
            struct
            {
                uint8_t pmport : 4; // Port multiplier
                uint8_t rsv0 : 3;   // Reserved
                uint8_t c : 1;      // 1: Command, 0: Control
            } crrr_port;
        };

        uint8_t command; // Command register
        uint8_t feature; // Feature register, 7:0

        // DWORD 1
        uint8_t lbaLow; // LBA low register, 7:0
        uint8_t lbaMid; // LBA mid register, 15:8
        uint8_t lbaHi;  // LBA high register, 23:16
        uint8_t device; // Device register

        // DWORD 2
        uint8_t lbaLowExt;  // LBA register, 31:24
        uint8_t lbaMidExt;  // LBA register, 39:32
        uint8_t lbaHiExt;   // LBA register, 47:40
        uint8_t featureExt; // Feature register, 15:8

        // DWORD 3
        uint8_t sectorCount;    // Count register, 7:0
        uint8_t sectorCountExt; // Count register, 15:8
        uint8_t icc;            // Isochronous command completion
        uint8_t control;        // Control register

        // DWORD 4
        uint8_t aux1; // auxilary (7:0)
        uint8_t aux2; // auxilary (15:8)
        uint8_t aux3; // auxilary (23:16)
        uint8_t aux4; // auxilary (31:24)
    } sataH2DFis, *ptrSataH2DFis;

// D2H bit masks. Use these on byte1 instead of bitfields for portability
#define D2H_INTERRUPT_BITMASK (0x40)

    typedef struct s_sataD2HFis
    {
        // DWORD 0
        uint8_t fisType; // FIS_TYPE_REG_D2H = 34h

        union
        {
            uint8_t byte1; // Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler
                           // to handle your bit field correctly! they can order them differently! - TJE
            struct
            {
                uint8_t pmport : 4;   // Port multiplier
                uint8_t rsv0 : 2;     // Reserved
                uint8_t interupt : 1; // interupt bit
                uint8_t rsv1 : 1;     // Reserved
            } rirr_port;
        };

        uint8_t status; // status register
        uint8_t error;  // error register

        // DWORD 1
        uint8_t lbaLow; // LBA low register, 7:0
        uint8_t lbaMid; // LBA mid register, 15:8
        uint8_t lbaHi;  // LBA high register, 23:16
        uint8_t device; // Device register

        // DWORD 2
        uint8_t lbaLowExt; // LBA register, 31:24
        uint8_t lbaMidExt; // LBA register, 39:32
        uint8_t lbaHiExt;  // LBA register, 47:40
        uint8_t reserved0; // reserved

        // DWORD 3
        uint8_t sectorCount;    // Count register, 7:0
        uint8_t sectorCountExt; // Count register, 15:8
        uint8_t reserved1;      // reserved
        uint8_t reserved2;      // reserved

        // DWORD 4
        uint8_t reserved3; // reserved
        uint8_t reserved4; // reserved
        uint8_t reserved5; // reserved
        uint8_t reserved6; // reserved
    } sataD2HFis, *ptrSataD2HFis;

// PIO Setup bit masks
#define PIO_SETUP_DATADIR_BIT_MASK  (0x20)
#define PIO_SETUP_INTERUPT_BIT_MASK (0x40)

    typedef struct s_sataPIOSetupFis
    {
        // DWORD 0
        uint8_t fisType; // FIS_TYPE_REG_D2H = 5Fh

        union
        {
            uint8_t byte1; // Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler
                           // to handle your bit field correctly! they can order them differently! - TJE
            struct
            {
                uint8_t pmport : 4;   // Port multiplier
                uint8_t rsv0 : 1;     // Reserved
                uint8_t dataDir : 1;  // 1 = D2H, 0 = H2D
                uint8_t interupt : 1; // interupt bit
                uint8_t rsv1 : 1;     // Reserved
            } ridr_port;
        };

        uint8_t status; // status register
        uint8_t error;  // error register

        // DWORD 1
        uint8_t lbaLow; // LBA low register, 7:0
        uint8_t lbaMid; // LBA mid register, 15:8
        uint8_t lbaHi;  // LBA high register, 23:16
        uint8_t device; // Device register

        // DWORD 2
        uint8_t lbaLowExt; // LBA register, 31:24
        uint8_t lbaMidExt; // LBA register, 39:32
        uint8_t lbaHiExt;  // LBA register, 47:40
        uint8_t reserved0; // reserved

        // DWORD 3
        uint8_t sectorCount;    // Count register, 7:0
        uint8_t sectorCountExt; // Count register, 15:8
        uint8_t reserved1;      // reserved
        uint8_t eStatus;        // E_Status a.k.a. ending status register.

        // DWORD 4
        uint8_t transferCount;   // transfer count 7:0
        uint8_t transferCountHi; // transfer count 15:8
        uint8_t reserved2;       // reserved
        uint8_t reserved3;       // reserved
    } sataPIOSetupFis, *ptrSataPIOSetupFis;

#define SET_DEVICE_BITS_NOTIFICATION_BIT_MASK (0x80)

    typedef struct s_sataSetDeviceBitsFis
    {
        // DWORD 0
        uint8_t fisType; // A1h

        union
        {
            uint8_t byte1; // Bit field below for CRRR_PM_Port bits...this SHOULD be ok, but do not rely on the compiler
                           // to handle your bit field correctly! they can order them differently! - TJE
            struct
            {
                uint8_t pmport : 4;       // Port multiplier
                uint8_t rsv0 : 1;         // Reserved
                uint8_t rsv1 : 1;         // Reserved
                uint8_t notification : 1; // notification bit
            } nrr_port;
        };

        union
        {
            uint8_t status; // status register. There are some reserved bits in here, hence bitfields
            struct
            {
                uint8_t reservedStat1 : 1;
                uint8_t statusHi : 3;
                uint8_t reservedStat2 : 1;
                uint8_t statusLo : 3;
            } statusFields;
        };
        uint8_t error; // error register

        // DWORD 1
        uint32_t protocolSpecific;
    } sataSetDeviceBitsFis, *ptrSataSetDeviceBitsFis;

    typedef struct s_sataDMAActivateFis
    {
        // DWORD 0
        uint8_t fisType; // 39h
        union
        {
            uint8_t byte1;
            struct
            {
                uint8_t pmport : 4;   // Port multiplier
                uint8_t reserved : 4; // Reserved
            } reserved_port;
        };
        uint8_t reserved1;
        uint8_t reserved2;
    } sataDMAActivateFis, *ptrSataDMAActivateFis;

#define DMA_SETUP_DIRECTION_BIT_MASK     (0x40)
#define DMA_SETUP_INTERUPT_BIT_MASK      (0x20)
#define DMA_SETUP_AUTO_ACTIVATE_BIT_MASK (0x10)

    typedef struct s_sataDMASetupFis
    {
        // DWORD 0
        uint8_t fisType; // 41h
        union
        {
            uint8_t byte1;
            struct
            {
                uint8_t pmport : 4; // Port multiplier
                uint8_t autoActivate : 1;
                uint8_t interrupt : 1;
                uint8_t direction : 1;
                uint8_t reserved : 1;
            } aidr_port;
        };
        uint8_t reserved1;
        uint8_t reserved2;

        // DWORD 1
        uint32_t dmaBufferIdentifierLow;
        // DWORD 2
        uint32_t dmaBufferIdentifierHigh;
        // DWORD 3
        uint32_t reserved3;
        // DWORD 4
        uint32_t dmaBufferOffset;
        // DWORD 5
        uint32_t dmaTransferCount;
        // DWORD 6
        uint32_t reserved6;
    } sataDMASetupFis, *ptrSataDMASetupFis;

#define BIST_ACTIVATE_PATTERN_T_BITMASK (0x80)
#define BIST_ACTIVATE_PATTERN_A_BITMASK (0x40)
#define BIST_ACTIVATE_PATTERN_S_BITMASK (0x20)
#define BIST_ACTIVATE_PATTERN_L_BITMASK (0x10)
#define BIST_ACTIVATE_PATTERN_F_BITMASK (0x08)
#define BIST_ACTIVATE_PATTERN_P_BITMASK (0x04)
#define BIST_ACTIVATE_PATTERN_R_BITMASK (0x02)
#define BIST_ACTIVATE_PATTERN_V_BITMASK (0x01)
    typedef struct s_sataBISTActivateFis
    {
        // DWORD 0
        uint8_t fisType; // 58h
        union
        {
            uint8_t byte1;
            struct
            {
                uint8_t pmport : 4; // Port multiplier
                uint8_t reserved : 4;
            } reserved_port;
        };
        union
        {
            uint8_t patternDefinition;
            struct
            {
                uint8_t farEndTranmitOnly : 1;
                uint8_t alignBypass : 1;
                uint8_t bypassScrambling : 1;
                uint8_t farEndRetimedLoopback : 1;
                uint8_t farEndAnalogLoopback : 1;
                uint8_t primitive : 1;
                uint8_t reserved : 1;
                uint8_t vendorSpecific : 1;
            } patternBits;
        };
        uint8_t reserved0;

        // DWORD 1
        uint8_t data1_7_0;
        uint8_t data1_15_8;
        uint8_t data1_23_16;
        uint8_t data1_31_24;

        // DWORD 2
        uint8_t data2_7_0;
        uint8_t data2_15_8;
        uint8_t data2_23_16;
        uint8_t data2_31_24;
    } sataBISTActivateFis, *ptrSataBISTActivateFis;

    typedef struct s_sataDataFis
    {
        // DWORD 0
        uint8_t fisType; // 46h
        union
        {
            uint8_t byte1;
            struct
            {
                uint8_t pmport : 4; // Port multiplier
                uint8_t reserved : 4;
            } reserved_port;
        };
        uint8_t reserved0;
        uint8_t reserved1;

        // DWORD 1
        uint32_t nDWordsData[1]; // This is variable length. up to 2048 DWords, so this struct should be variable size
                                 // depending on the transfer. Because of this, we represent it like this.
    } sataDataFis, *ptrSataDataFis;

#if defined(__cplusplus)
}
#endif
