// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2020-2026 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//
// \file sata_helper_func.c
// \brief functions to help with SATA specific things. Printing out FIS, creating FIS, etc.

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "sata_helper_func.h"
#include "sata_types.h"

M_PARAM_RW(1)
M_PARAM_RO(2)
eReturnValues build_H2D_FIS_From_ATA_PT_Command(ptrSataH2DFis M_NONNULL h2dFis,
                                                ataTFRBlock* M_NONNULL  ataPTCmd,
                                                uint8_t                 pmPort)
{
    eReturnValues ret = SUCCESS;

    if (h2dFis == M_NULLPTR || ataPTCmd == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }

    h2dFis->fisType = FIS_TYPE_REG_H2D;
    h2dFis->byte1   = H2D_COMMAND_BIT_MASK |
                    M_Nibble0(pmPort); // set the command bit so that the drive performs the command, rather than a soft
                                       // reset. Also set port multiplier port so the command goes to the correct device
    h2dFis->command        = ataPTCmd->CommandStatus;
    h2dFis->feature        = ataPTCmd->ErrorFeature;
    h2dFis->lbaLow         = ataPTCmd->LbaLow;
    h2dFis->lbaMid         = ataPTCmd->LbaMid;
    h2dFis->lbaHi          = ataPTCmd->LbaHi;
    h2dFis->device         = ataPTCmd->DeviceHead;
    h2dFis->lbaLowExt      = ataPTCmd->LbaLow48;
    h2dFis->lbaMidExt      = ataPTCmd->LbaMid48;
    h2dFis->lbaHiExt       = ataPTCmd->LbaHi48;
    h2dFis->featureExt     = ataPTCmd->Feature48;
    h2dFis->sectorCount    = ataPTCmd->SectorCount;
    h2dFis->sectorCountExt = ataPTCmd->SectorCount48;
    h2dFis->icc            = ataPTCmd->icc;
    // do not set the control register for a command.
    h2dFis->aux1 = ataPTCmd->aux1;
    h2dFis->aux2 = ataPTCmd->aux2;
    h2dFis->aux3 = ataPTCmd->aux3;
    h2dFis->aux4 = ataPTCmd->aux4;

    return ret;
}

// NOTE: This prints some bit fields, so some architectures or compilers may not pack those in the same order. Check the
// actual byte data for real interpretation, then ifdef the structure definitions to correct it as errors are found -
// TJE

// Helper functions for printing individual FIS types
static void print_H2D_FIS(ptrSataH2DFis h2dFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < H2D_FIS_LENGTH)
    {
        print_str("Invalid H2D length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - H2D\n", h2dFis->fisType);
    printf("\tCRRR_PORT:\t%02" PRIX8 "\n", h2dFis->byte1);
    printf("\t\tCommand:\t\t%" PRIX8 "\n", h2dFis->crrr_port.c);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", h2dFis->crrr_port.rsv0);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", h2dFis->crrr_port.pmport);
    printf("\tCommand:\t\t%02" PRIX8 "\n", h2dFis->command);
    printf("\tFeature (7:0):\t\t%02" PRIX8 "\n", h2dFis->feature);
    printf("\tLBA (7:0):\t%02" PRIX8 "\n", h2dFis->lbaLow);
    printf("\tLBA (15:8):\t%02" PRIX8 "\n", h2dFis->lbaMid);
    printf("\tLBA (23:16):\t%02" PRIX8 "\n", h2dFis->lbaHi);
    printf("\tDevice:\t\t%02" PRIX8 "\n", h2dFis->device);
    printf("\tLBA (31:24):\t%02" PRIX8 "\n", h2dFis->lbaLowExt);
    printf("\tLBA (39:32):\t%02" PRIX8 "\n", h2dFis->lbaMidExt);
    printf("\tLBA (47:40):\t%02" PRIX8 "\n", h2dFis->lbaHiExt);
    printf("\tFeature (15:8):\t%02" PRIX8 "\n", h2dFis->featureExt);
    printf("\tCount (7:0):\t%02" PRIX8 "\n", h2dFis->sectorCount);
    printf("\tCount (15:8):\t%02" PRIX8 "\n", h2dFis->sectorCountExt);
    printf("\tICC:\t\t%02" PRIX8 "\n", h2dFis->icc);
    printf("\tControl:\t%02" PRIX8 "\n", h2dFis->control);
    printf("\tAux (7:0):\t%02" PRIX8 "\n", h2dFis->aux1);
    printf("\tAux (15:8):\t%02" PRIX8 "\n", h2dFis->aux2);
    printf("\tAux (23:16):\t%02" PRIX8 "\n", h2dFis->aux3);
    printf("\tAux (31:24):\t%02" PRIX8 "\n", h2dFis->aux4);
}

static void print_D2H_FIS(ptrSataD2HFis d2hFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < D2H_FIS_LENGTH)
    {
        print_str("Invalid D2H length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - D2H\n", d2hFis->fisType);
    printf("\tRIRR_PORT:\t%02" PRIX8 "\n", d2hFis->byte1);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", d2hFis->rirr_port.rsv1);
    printf("\t\tInterupt:\t\t%" PRIX8 "\n", d2hFis->rirr_port.interupt);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", d2hFis->rirr_port.rsv0);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", d2hFis->rirr_port.pmport);
    printf("\tStatus:\t\t%02" PRIX8 "\n", d2hFis->status);
    printf("\tError:\t\t%02" PRIX8 "\n", d2hFis->error);
    printf("\tLBA (7:0):\t%02" PRIX8 "\n", d2hFis->lbaLow);
    printf("\tLBA (15:8):\t%02" PRIX8 "\n", d2hFis->lbaMid);
    printf("\tLBA (23:16):\t%02" PRIX8 "\n", d2hFis->lbaHi);
    printf("\tDevice:\t\t%02" PRIX8 "\n", d2hFis->device);
    printf("\tLBA (31:24):\t%02" PRIX8 "\n", d2hFis->lbaLowExt);
    printf("\tLBA (39:32):\t%02" PRIX8 "\n", d2hFis->lbaMidExt);
    printf("\tLBA (47:40):\t%02" PRIX8 "\n", d2hFis->lbaHiExt);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved0);
    printf("\tCount (7:0):\t%02" PRIX8 "\n", d2hFis->sectorCount);
    printf("\tCount (15:8):\t%02" PRIX8 "\n", d2hFis->sectorCountExt);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved1);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved2);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved3);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved4);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved5);
    printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved6);
}

static void print_PIO_Setup_FIS(ptrSataPIOSetupFis pioFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < PIO_SETUP_FIS_LENGTH)
    {
        print_str("Invalid PIO Setup length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - PIO Setup\n", pioFis->fisType);
    printf("\tRIDR_PORT:\t%02" PRIX8 "\n", pioFis->byte1);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", pioFis->ridr_port.rsv1);
    printf("\t\tInterupt:\t\t%" PRIX8 "\n", pioFis->ridr_port.interupt);
    printf("\t\tData Direction:\t%" PRIX8 "\n", pioFis->ridr_port.dataDir);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", pioFis->ridr_port.rsv0);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", pioFis->ridr_port.pmport);
    printf("\tStatus:\t\t%02" PRIX8 "\n", pioFis->status);
    printf("\tError:\t\t%02" PRIX8 "\n", pioFis->error);
    printf("\tLBA (7:0):\t%02" PRIX8 "\n", pioFis->lbaLow);
    printf("\tLBA (15:8):\t%02" PRIX8 "\n", pioFis->lbaMid);
    printf("\tLBA (23:16):\t%02" PRIX8 "\n", pioFis->lbaHi);
    printf("\tDevice:\t\t%02" PRIX8 "\n", pioFis->device);
    printf("\tLBA (31:24):\t%02" PRIX8 "\n", pioFis->lbaLowExt);
    printf("\tLBA (39:32):\t%02" PRIX8 "\n", pioFis->lbaMidExt);
    printf("\tLBA (47:40):\t%02" PRIX8 "\n", pioFis->lbaHiExt);
    printf("\tReserved:\t%02" PRIX8 "\n", pioFis->reserved0);
    printf("\tCount (7:0):\t%02" PRIX8 "\n", pioFis->sectorCount);
    printf("\tCount (15:8):\t%02" PRIX8 "\n", pioFis->sectorCountExt);
    printf("\tReserved:\t%02" PRIX8 "\n", pioFis->reserved1);
    printf("\tE_Status:\t%02" PRIX8 "\n", pioFis->eStatus);
    printf("\tTransfer Count (7:0):\t%02" PRIX8 "\n", pioFis->transferCount);
    printf("\tTransfer Count (15:8):\t%02" PRIX8 "\n", pioFis->transferCountHi);
    printf("\tReserved:\t%02" PRIX8 "\n", pioFis->reserved2);
    printf("\tReserved:\t%02" PRIX8 "\n", pioFis->reserved3);
}

static void print_DMA_Activate_FIS(ptrSataDMAActivateFis dmaActFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < DMA_ACTIVATE_FIS_LENGTH)
    {
        print_str("Invalid DMA Activate length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - DMA Activate\n", dmaActFis->fisType);
    printf("\tR_PORT:\t%02" PRIX8 "\n", dmaActFis->byte1);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved_port.reserved);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", dmaActFis->reserved_port.pmport);
    printf("\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved1);
    printf("\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved2);
}

static void print_DMA_Setup_FIS(ptrSataDMASetupFis dmaSetupFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < DMA_SETUP_FIS_LENGTH)
    {
        print_str("Invalid DMA Setup length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - DMA Setup\n", dmaSetupFis->fisType);
    printf("\tAIDR_PORT:\t%02" PRIX8 "\n", dmaSetupFis->byte1);
    printf("\t\tReserved:\t%" PRIu8 "\n", dmaSetupFis->aidr_port.reserved);
    printf("\t\tDirection:\t%" PRIu8 "\n", dmaSetupFis->aidr_port.direction);
    printf("\t\tInterupt:\t%" PRIu8 "\n", dmaSetupFis->aidr_port.interrupt);
    printf("\t\tAuto Activate:\t%" PRIu8 "\n", dmaSetupFis->aidr_port.autoActivate);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", dmaSetupFis->aidr_port.pmport);
    printf("\tReserved:\t\t%" PRIX8 "\n", dmaSetupFis->reserved1);
    printf("\tReserved:\t\t%" PRIX8 "\n", dmaSetupFis->reserved2);
    printf("\tDMA Buffer ID Low:\t\t%" PRIX32 "\n", dmaSetupFis->dmaBufferIdentifierLow);
    printf("\tDMA Buffer ID High:\t\t%" PRIX32 "\n", dmaSetupFis->dmaBufferIdentifierHigh);
    printf("\tReserved:\t\t%" PRIX32 "\n", dmaSetupFis->reserved3);
    printf("\tDMA Buffer Offset:\t\t%" PRIX32 "\n", dmaSetupFis->dmaBufferOffset);
    printf("\tDMA Transfer Count:\t\t%" PRIX32 "\n", dmaSetupFis->dmaTransferCount);
    printf("\tReserved:\t\t%" PRIX32 "\n", dmaSetupFis->reserved6);
}

static void print_Data_FIS(ptrSataDataFis dataFis, uint32_t fisLengthBytes)
{
    printf("\tFisType:\t%02" PRIX8 " - Data\n", dataFis->fisType);
    printf("\tR_PORT:\t%02" PRIX8 "\n", dataFis->byte1);
    printf("\t\tReserved:\t%" PRIu8 "\n", dataFis->reserved_port.reserved);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", dataFis->reserved_port.pmport);
    for (uint32_t dwordIter = UINT32_C(0);
         dwordIter < DATA_FIS_MAX_DWORDS && dwordIter < (fisLengthBytes - sizeof(uint32_t)); ++dwordIter)
    {
        printf("\tDWORD %" PRIu32 ":\t%08" PRIX32 "\n", dwordIter + 1, dataFis->nDWordsData[dwordIter]);
    }
}

static void print_BIST_FIS(ptrSataBISTActivateFis bistActFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < BIST_ACTIVATE_FIS_LENGTH)
    {
        print_str("Invalid BIST Activate length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - BIST Activate\n", bistActFis->fisType);
    printf("\tR_PORT:\t%02" PRIX8 "\n", bistActFis->byte1);
    printf("\t\tReserved:\t%" PRIX8 "\n", bistActFis->reserved_port.reserved);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", bistActFis->reserved_port.pmport);
    printf("\tPattern Definition:\t%02" PRIX8 "\n", bistActFis->patternDefinition);
    printf("\tReserved:\t%02" PRIX8 "\n", bistActFis->reserved0);
    printf("\tData 1 (7:0):\t%02" PRIX8 "\n", bistActFis->data1_7_0);
    printf("\tData 1 (15:8):\t%02" PRIX8 "\n", bistActFis->data1_15_8);
    printf("\tData 1 (23:16):\t%02" PRIX8 "\n", bistActFis->data1_23_16);
    printf("\tData 1 (31:24):\t%02" PRIX8 "\n", bistActFis->data1_31_24);
    printf("\tData 2 (7:0):\t%02" PRIX8 "\n", bistActFis->data2_7_0);
    printf("\tData 2 (15:8):\t%02" PRIX8 "\n", bistActFis->data2_15_8);
    printf("\tData 2 (23:16):\t%02" PRIX8 "\n", bistActFis->data2_23_16);
    printf("\tData 2 (31:24):\t%02" PRIX8 "\n", bistActFis->data2_31_24);
}

static void print_DevBits_FIS(ptrSataSetDeviceBitsFis devBitsFis, uint32_t fisLengthBytes)
{
    if (fisLengthBytes < SET_DEVICE_BITS_FIS_LENGTH)
    {
        print_str("Invalid Set Device Bits length, cannot print.\n");
        return;
    }
    printf("\tFisType:\t%02" PRIX8 " - Set Device Bits\n", devBitsFis->fisType);
    printf("\tNRR_PORT:\t%02" PRIX8 "\n", devBitsFis->byte1);
    printf("\t\tNotification:\t\t%" PRIX8 "\n", devBitsFis->nrr_port.notification);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", devBitsFis->nrr_port.rsv1);
    printf("\t\tReserved:\t\t%" PRIX8 "\n", devBitsFis->nrr_port.rsv0);
    printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", devBitsFis->nrr_port.pmport);
    printf("\tStatus:\t%02" PRIX8 "\n", devBitsFis->status);
    printf("\tError:\t%02" PRIX8 "\n", devBitsFis->error);
    printf("\tProtocol Specific:\t%08" PRIX32 "\n", devBitsFis->protocolSpecific);
}

static void print_Unknown_FIS(uint8_t* fisPtr, uint32_t fisLengthBytes)
{
    if (fisPtr[0] == FIS_TYPE_VEN_UNQ1 || fisPtr[0] == FIS_TYPE_VEN_UNQ2)
    {
        printf("\tFisType:\t%02" PRIX8 " - Vendor Unique\n", fisPtr[0]);
    }
    else if (fisPtr[0] == FIS_TYPE_RES_FUT1 || fisPtr[0] == FIS_TYPE_RES_FUT2 || fisPtr[0] == FIS_TYPE_RES_FUT3 ||
             fisPtr[0] == FIS_TYPE_RES_FUT4)
    {
        printf("\tFisType:\t%02" PRIX8 " - Reserved\n", fisPtr[0]);
    }
    else
    {
        printf("\tFisType:\t%02" PRIX8 " - Unknown\n", fisPtr[0]);
    }
    for (uint8_t fisIter = UINT8_C(1); fisIter < UNKNOWN_FIS_TYPE_LENGTH && fisIter < fisLengthBytes; ++fisIter)
    {
        printf("\tFIS[%2" PRIu8 "]:\t%02" PRIX8 "\n", fisIter, fisPtr[fisIter]);
    }
    print_str("\n");
}
M_DEPRECATED_REASON("Use print_tDevice_Verbose_FIS instead")
void print_FIS(void* M_NONNULL fis, uint32_t fisLengthBytes)
{
    if (fis != M_NULLPTR)
    {
        uint8_t* fisPtr = fis;
        switch (fisPtr[0])
        {
        case FIS_TYPE_REG_H2D:
        {
            ptrSataH2DFis h2dFis = C_CAST(ptrSataH2DFis, fis);
            print_H2D_FIS(h2dFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_REG_D2H:
        {
            ptrSataD2HFis d2hFis = C_CAST(ptrSataD2HFis, fis);
            print_D2H_FIS(d2hFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_PIO_SETUP:
        {
            ptrSataPIOSetupFis pioFis = C_CAST(ptrSataPIOSetupFis, fis);
            print_PIO_Setup_FIS(pioFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DMA_ACT:
        {
            ptrSataDMAActivateFis dmaActFis = C_CAST(ptrSataDMAActivateFis, fis);
            print_DMA_Activate_FIS(dmaActFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DMA_SETUP:
        {
            ptrSataDMASetupFis dmaSetupFis = C_CAST(ptrSataDMASetupFis, fis);
            print_DMA_Setup_FIS(dmaSetupFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DATA:
        {
            ptrSataDataFis dataFis = C_CAST(ptrSataDataFis, fis);
            if (fisLengthBytes < DATA_FIS_LENGTH_MIN || fisLengthBytes > DATA_FIS_LENGTH_MAX)
            {
                print_str("Invalid Data Fis length, cannot print.\n");
                return;
            }
            print_Data_FIS(dataFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_BIST:
        {
            ptrSataBISTActivateFis bistActFis = C_CAST(ptrSataBISTActivateFis, fis);
            print_BIST_FIS(bistActFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DEV_BITS:
        {
            ptrSataSetDeviceBitsFis devBitsFis = C_CAST(ptrSataSetDeviceBitsFis, fis);
            print_DevBits_FIS(devBitsFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_RES_FUT1:
        case FIS_TYPE_RES_FUT2:
        case FIS_TYPE_RES_FUT3:
        case FIS_TYPE_VEN_UNQ1:
        case FIS_TYPE_VEN_UNQ2:
        case FIS_TYPE_RES_FUT4:
        default:
            print_Unknown_FIS(fisPtr, fisLengthBytes);
            break;
        }
    }
}

// Device-aware FIS printing helpers
static void print_tDevice_Verbose_H2D_FIS(const tDevice*   device,
                                          eVerbosityLevels verboseLevel,
                                          ptrSataH2DFis    h2dFis,
                                          uint32_t         fisLengthBytes)
{
    if (fisLengthBytes < H2D_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid H2D length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - H2D\n", h2dFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCRRR_PORT:\t%02" PRIX8 "\n", h2dFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tCommand:\t\t%" PRIX8 "\n", h2dFis->crrr_port.c);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           h2dFis->crrr_port.rsv0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           h2dFis->crrr_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCommand:\t\t%02" PRIX8 "\n", h2dFis->command);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFeature (7:0):\t\t%02" PRIX8 "\n", h2dFis->feature);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (7:0):\t%02" PRIX8 "\n", h2dFis->lbaLow);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (15:8):\t%02" PRIX8 "\n", h2dFis->lbaMid);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (23:16):\t%02" PRIX8 "\n", h2dFis->lbaHi);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDevice:\t\t%02" PRIX8 "\n", h2dFis->device);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (31:24):\t%02" PRIX8 "\n", h2dFis->lbaLowExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (39:32):\t%02" PRIX8 "\n", h2dFis->lbaMidExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (47:40):\t%02" PRIX8 "\n", h2dFis->lbaHiExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFeature (15:8):\t%02" PRIX8 "\n",
                                           h2dFis->featureExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCount (7:0):\t%02" PRIX8 "\n", h2dFis->sectorCount);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCount (15:8):\t%02" PRIX8 "\n",
                                           h2dFis->sectorCountExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tICC:\t\t%02" PRIX8 "\n", h2dFis->icc);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tControl:\t%02" PRIX8 "\n", h2dFis->control);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tAux (7:0):\t%02" PRIX8 "\n", h2dFis->aux1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tAux (15:8):\t%02" PRIX8 "\n", h2dFis->aux2);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tAux (23:16):\t%02" PRIX8 "\n", h2dFis->aux3);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tAux (31:24):\t%02" PRIX8 "\n", h2dFis->aux4);
}

static void print_tDevice_Verbose_D2H_FIS(const tDevice*   device,
                                          eVerbosityLevels verboseLevel,
                                          ptrSataD2HFis    d2hFis,
                                          uint32_t         fisLengthBytes)
{
    if (fisLengthBytes < D2H_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid D2H length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - D2H\n", d2hFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tRIRR_PORT:\t%02" PRIX8 "\n", d2hFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           d2hFis->rirr_port.rsv1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tInterupt:\t\t%" PRIX8 "\n",
                                           d2hFis->rirr_port.interupt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           d2hFis->rirr_port.rsv0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           d2hFis->rirr_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tStatus:\t\t%02" PRIX8 "\n", d2hFis->status);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tError:\t\t%02" PRIX8 "\n", d2hFis->error);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (7:0):\t%02" PRIX8 "\n", d2hFis->lbaLow);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (15:8):\t%02" PRIX8 "\n", d2hFis->lbaMid);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (23:16):\t%02" PRIX8 "\n", d2hFis->lbaHi);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDevice:\t\t%02" PRIX8 "\n", d2hFis->device);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (31:24):\t%02" PRIX8 "\n", d2hFis->lbaLowExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (39:32):\t%02" PRIX8 "\n", d2hFis->lbaMidExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (47:40):\t%02" PRIX8 "\n", d2hFis->lbaHiExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCount (7:0):\t%02" PRIX8 "\n", d2hFis->sectorCount);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCount (15:8):\t%02" PRIX8 "\n",
                                           d2hFis->sectorCountExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved2);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved3);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved4);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved5);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved6);
}

static void print_tDevice_Verbose_PIO_Setup_FIS(const tDevice*     device,
                                                eVerbosityLevels   verboseLevel,
                                                ptrSataPIOSetupFis pioFis,
                                                uint32_t           fisLengthBytes)
{
    if (fisLengthBytes < PIO_SETUP_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid PIO Setup length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - PIO Setup\n",
                                           pioFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tRIDR_PORT:\t%02" PRIX8 "\n", pioFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           pioFis->ridr_port.rsv1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tInterupt:\t\t%" PRIX8 "\n",
                                           pioFis->ridr_port.interupt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tData Direction:\t%" PRIX8 "\n",
                                           pioFis->ridr_port.dataDir);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           pioFis->ridr_port.rsv0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           pioFis->ridr_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tStatus:\t\t%02" PRIX8 "\n", pioFis->status);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tError:\t\t%02" PRIX8 "\n", pioFis->error);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (7:0):\t%02" PRIX8 "\n", pioFis->lbaLow);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (15:8):\t%02" PRIX8 "\n", pioFis->lbaMid);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (23:16):\t%02" PRIX8 "\n", pioFis->lbaHi);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDevice:\t\t%02" PRIX8 "\n", pioFis->device);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (31:24):\t%02" PRIX8 "\n", pioFis->lbaLowExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (39:32):\t%02" PRIX8 "\n", pioFis->lbaMidExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tLBA (47:40):\t%02" PRIX8 "\n", pioFis->lbaHiExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", pioFis->reserved0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCount (7:0):\t%02" PRIX8 "\n", pioFis->sectorCount);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tCount (15:8):\t%02" PRIX8 "\n",
                                           pioFis->sectorCountExt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", pioFis->reserved1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tE_Status:\t%02" PRIX8 "\n", pioFis->eStatus);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tTransfer Count (7:0):\t%02" PRIX8 "\n",
                                           pioFis->transferCount);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tTransfer Count (15:8):\t%02" PRIX8 "\n",
                                           pioFis->transferCountHi);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", pioFis->reserved2);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", pioFis->reserved3);
}

static void print_tDevice_Verbose_DMA_Activate_FIS(const tDevice*        device,
                                                   eVerbosityLevels      verboseLevel,
                                                   ptrSataDMAActivateFis dmaActFis,
                                                   uint32_t              fisLengthBytes)
{
    if (fisLengthBytes < DMA_ACTIVATE_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid DMA Activate length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - DMA Activate\n",
                                           dmaActFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tR_PORT:\t%02" PRIX8 "\n", dmaActFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           dmaActFis->reserved_port.reserved);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           dmaActFis->reserved_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved2);
}

static void print_tDevice_Verbose_DMA_Setup_FIS(const tDevice*     device,
                                                eVerbosityLevels   verboseLevel,
                                                ptrSataDMASetupFis dmaSetupFis,
                                                uint32_t           fisLengthBytes)
{
    if (fisLengthBytes < DMA_SETUP_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid DMA Setup length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - DMA Setup\n",
                                           dmaSetupFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tAIDR_PORT:\t%02" PRIX8 "\n", dmaSetupFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t%" PRIu8 "\n",
                                           dmaSetupFis->aidr_port.reserved);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tDirection:\t%" PRIu8 "\n",
                                           dmaSetupFis->aidr_port.direction);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tInterupt:\t%" PRIu8 "\n",
                                           dmaSetupFis->aidr_port.interrupt);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tAuto Activate:\t%" PRIu8 "\n",
                                           dmaSetupFis->aidr_port.autoActivate);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           dmaSetupFis->aidr_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t\t%" PRIX8 "\n", dmaSetupFis->reserved1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t\t%" PRIX8 "\n", dmaSetupFis->reserved2);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDMA Buffer ID Low:\t\t%" PRIX32 "\n",
                                           dmaSetupFis->dmaBufferIdentifierLow);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDMA Buffer ID High:\t\t%" PRIX32 "\n",
                                           dmaSetupFis->dmaBufferIdentifierHigh);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t\t%" PRIX32 "\n",
                                           dmaSetupFis->reserved3);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDMA Buffer Offset:\t\t%" PRIX32 "\n",
                                           dmaSetupFis->dmaBufferOffset);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDMA Transfer Count:\t\t%" PRIX32 "\n",
                                           dmaSetupFis->dmaTransferCount);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t\t%" PRIX32 "\n",
                                           dmaSetupFis->reserved6);
}

static void print_tDevice_Verbose_Data_FIS(const tDevice*   device,
                                           eVerbosityLevels verboseLevel,
                                           ptrSataDataFis   dataFis,
                                           uint32_t         fisLengthBytes)
{
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - Data\n", dataFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tR_PORT:\t%02" PRIX8 "\n", dataFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t%" PRIu8 "\n",
                                           dataFis->reserved_port.reserved);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           dataFis->reserved_port.pmport);
    for (uint32_t dwordIter = UINT32_C(0);
         dwordIter < DATA_FIS_MAX_DWORDS && dwordIter < (fisLengthBytes - sizeof(uint32_t)); ++dwordIter)
    {
        print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tDWORD %" PRIu32 ":\t%08" PRIX32 "\n",
                                               dwordIter + 1, dataFis->nDWordsData[dwordIter]);
    }
}

static void print_tDevice_Verbose_BIST_FIS(const tDevice*         device,
                                           eVerbosityLevels       verboseLevel,
                                           ptrSataBISTActivateFis bistActFis,
                                           uint32_t               fisLengthBytes)
{
    if (fisLengthBytes < BIST_ACTIVATE_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid BIST Activate length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - BIST Activate\n",
                                           bistActFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tR_PORT:\t%02" PRIX8 "\n", bistActFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t%" PRIX8 "\n",
                                           bistActFis->reserved_port.reserved);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           bistActFis->reserved_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tPattern Definition:\t%02" PRIX8 "\n",
                                           bistActFis->patternDefinition);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tReserved:\t%02" PRIX8 "\n", bistActFis->reserved0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 1 (7:0):\t%02" PRIX8 "\n",
                                           bistActFis->data1_7_0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 1 (15:8):\t%02" PRIX8 "\n",
                                           bistActFis->data1_15_8);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 1 (23:16):\t%02" PRIX8 "\n",
                                           bistActFis->data1_23_16);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 1 (31:24):\t%02" PRIX8 "\n",
                                           bistActFis->data1_31_24);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 2 (7:0):\t%02" PRIX8 "\n",
                                           bistActFis->data2_7_0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 2 (15:8):\t%02" PRIX8 "\n",
                                           bistActFis->data2_15_8);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 2 (23:16):\t%02" PRIX8 "\n",
                                           bistActFis->data2_23_16);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tData 2 (31:24):\t%02" PRIX8 "\n",
                                           bistActFis->data2_31_24);
}

static void print_tDevice_Verbose_DevBits_FIS(const tDevice*          device,
                                              eVerbosityLevels        verboseLevel,
                                              ptrSataSetDeviceBitsFis devBitsFis,
                                              uint32_t                fisLengthBytes)
{
    if (fisLengthBytes < SET_DEVICE_BITS_FIS_LENGTH)
    {
        print_tDevice_Verbose_String(device, verboseLevel, "Invalid Set Device Bits length, cannot print.\n");
        return;
    }
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - Set Device Bits\n",
                                           devBitsFis->fisType);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tNRR_PORT:\t%02" PRIX8 "\n", devBitsFis->byte1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tNotification:\t\t%" PRIX8 "\n",
                                           devBitsFis->nrr_port.notification);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           devBitsFis->nrr_port.rsv1);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tReserved:\t\t%" PRIX8 "\n",
                                           devBitsFis->nrr_port.rsv0);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\t\tPort Multiplier Port:\t%" PRIu8 "\n",
                                           devBitsFis->nrr_port.pmport);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tStatus:\t%02" PRIX8 "\n", devBitsFis->status);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tError:\t%02" PRIX8 "\n", devBitsFis->error);
    print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tProtocol Specific:\t%08" PRIX32 "\n",
                                           devBitsFis->protocolSpecific);
}

static void print_tDevice_Verbose_Unknown_FIS(const tDevice*   device,
                                              eVerbosityLevels verboseLevel,
                                              uint8_t*         fisPtr,
                                              uint32_t         fisLengthBytes)
{
    if (fisPtr[0] == FIS_TYPE_VEN_UNQ1 || fisPtr[0] == FIS_TYPE_VEN_UNQ2)
    {
        print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - Vendor Unique\n",
                                               fisPtr[0]);
    }
    else if (fisPtr[0] == FIS_TYPE_RES_FUT1 || fisPtr[0] == FIS_TYPE_RES_FUT2 || fisPtr[0] == FIS_TYPE_RES_FUT3 ||
             fisPtr[0] == FIS_TYPE_RES_FUT4)
    {
        print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - Reserved\n",
                                               fisPtr[0]);
    }
    else
    {
        print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFisType:\t%02" PRIX8 " - Unknown\n", fisPtr[0]);
    }
    for (uint8_t fisIter = UINT8_C(1); fisIter < UNKNOWN_FIS_TYPE_LENGTH && fisIter < fisLengthBytes; ++fisIter)
    {
        print_tDevice_Verbose_Formatted_String(device, verboseLevel, "\tFIS[%2" PRIu8 "]:\t%02" PRIX8 "\n", fisIter,
                                               fisPtr[fisIter]);
    }
    print_tDevice_Verbose_String(device, verboseLevel, "\n");
}

// Main device-aware FIS printing function
M_PARAM_RO(1)
M_PARAM_RO_SIZE(3, 4)
void print_tDevice_Verbose_FIS(const tDevice* M_NONNULL device,
                               eVerbosityLevels         verboseLevel,
                               void* M_NONNULL          fis,
                               uint32_t                 fisLengthBytes)
{
    if (fis != M_NULLPTR)
    {
        uint8_t* fisPtr = fis;
        switch (fisPtr[0])
        {
        case FIS_TYPE_REG_H2D:
        {
            ptrSataH2DFis h2dFis = C_CAST(ptrSataH2DFis, fis);
            print_tDevice_Verbose_H2D_FIS(device, verboseLevel, h2dFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_REG_D2H:
        {
            ptrSataD2HFis d2hFis = C_CAST(ptrSataD2HFis, fis);
            print_tDevice_Verbose_D2H_FIS(device, verboseLevel, d2hFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_PIO_SETUP:
        {
            ptrSataPIOSetupFis pioFis = C_CAST(ptrSataPIOSetupFis, fis);
            print_tDevice_Verbose_PIO_Setup_FIS(device, verboseLevel, pioFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DMA_ACT:
        {
            ptrSataDMAActivateFis dmaActFis = C_CAST(ptrSataDMAActivateFis, fis);
            print_tDevice_Verbose_DMA_Activate_FIS(device, verboseLevel, dmaActFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DMA_SETUP:
        {
            ptrSataDMASetupFis dmaSetupFis = C_CAST(ptrSataDMASetupFis, fis);
            print_tDevice_Verbose_DMA_Setup_FIS(device, verboseLevel, dmaSetupFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DATA:
        {
            ptrSataDataFis dataFis = C_CAST(ptrSataDataFis, fis);
            if (fisLengthBytes < DATA_FIS_LENGTH_MIN || fisLengthBytes > DATA_FIS_LENGTH_MAX)
            {
                print_tDevice_Verbose_String(device, verboseLevel, "Invalid Data Fis length, cannot print.\n");
                return;
            }
            print_tDevice_Verbose_Data_FIS(device, verboseLevel, dataFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_BIST:
        {
            ptrSataBISTActivateFis bistActFis = C_CAST(ptrSataBISTActivateFis, fis);
            print_tDevice_Verbose_BIST_FIS(device, verboseLevel, bistActFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_DEV_BITS:
        {
            ptrSataSetDeviceBitsFis devBitsFis = C_CAST(ptrSataSetDeviceBitsFis, fis);
            print_tDevice_Verbose_DevBits_FIS(device, verboseLevel, devBitsFis, fisLengthBytes);
        }
        break;
        case FIS_TYPE_RES_FUT1:
        case FIS_TYPE_RES_FUT2:
        case FIS_TYPE_RES_FUT3:
        case FIS_TYPE_VEN_UNQ1:
        case FIS_TYPE_VEN_UNQ2:
        case FIS_TYPE_RES_FUT4:
        default:
            print_tDevice_Verbose_Unknown_FIS(device, verboseLevel, fisPtr, fisLengthBytes);
            break;
        }
    }
    flush_tDevice_Verbose_Stream(device);
}
