// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2020-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
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

eReturnValues build_H2D_FIS_From_ATA_PT_Command(ptrSataH2DFis h2dFis, ataTFRBlock* ataPTCmd, uint8_t pmPort)
{
    eReturnValues ret = SUCCESS;
    DISABLE_NONNULL_COMPARE
    if (h2dFis == M_NULLPTR || ataPTCmd == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

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
void print_FIS(void* fis, uint32_t fisLengthBytes)
{
    DISABLE_NONNULL_COMPARE
    if (fis != M_NULLPTR)
    {
        uint8_t* fisPtr =
            fis; // this is so that if using a fis structure, there aren't casting warnings calling the function
        // uint8_t pmPort = UINT8_C(0);//TODO: extract the pmPort value from byte 1 using the nibble macro
        switch (fisPtr[0])
        {
        case FIS_TYPE_REG_H2D:
        {
            ptrSataH2DFis h2dFis = C_CAST(ptrSataH2DFis, fis);
            if (fisLengthBytes < H2D_FIS_LENGTH)
            {
                printf("Invalid H2D length, cannot print.\n");
                return;
            }
            printf("\tFisType:\t%02" PRIX8 " - H2D\n", h2dFis->fisType);
            printf("\tCRRR_PORT:\t%02" PRIX8 "\n", h2dFis->byte1);
            // show the byte 1 bitfields independently
            printf("\t\tCommand:\t\t%" PRIX8 "\n", h2dFis->crrr_port.c);
            printf("\t\tReserved:\t\t%" PRIX8 "\n", h2dFis->crrr_port.rsv0);
            printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", h2dFis->crrr_port.pmport);
            //
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
        break;
        case FIS_TYPE_REG_D2H:
        {
            ptrSataD2HFis d2hFis = C_CAST(ptrSataD2HFis, fis);
            if (fisLengthBytes < D2H_FIS_LENGTH)
            {
                printf("Invalid D2H length, cannot print.\n");
                return;
            }
            printf("\tFisType:\t%02" PRIX8 " - D2H\n", d2hFis->fisType);
            printf("\tRIRR_PORT:\t%02" PRIX8 "\n", d2hFis->byte1);
            // show the byte 1 bitfields independently
            printf("\t\tReserved:\t\t%" PRIX8 "\n", d2hFis->rirr_port.rsv1);
            printf("\t\tInterupt:\t\t%" PRIX8 "\n", d2hFis->rirr_port.interupt);
            printf("\t\tReserved:\t\t%" PRIX8 "\n", d2hFis->rirr_port.rsv0);
            printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", d2hFis->rirr_port.pmport);
            //
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
        break;
        case FIS_TYPE_PIO_SETUP:
        {
            ptrSataPIOSetupFis d2hFis = C_CAST(ptrSataPIOSetupFis, fis);
            if (fisLengthBytes < PIO_SETUP_FIS_LENGTH)
            {
                printf("Invalid PIO Setup length, cannot print.\n");
                return;
            }
            printf("\tFisType:\t%02" PRIX8 " - PIO Setup\n", d2hFis->fisType);
            printf("\tRIDR_PORT:\t%02" PRIX8 "\n", d2hFis->byte1);
            // show the byte 1 bitfields independently
            printf("\t\tReserved:\t\t%" PRIX8 "\n", d2hFis->ridr_port.rsv1);
            printf("\t\tInterupt:\t\t%" PRIX8 "\n", d2hFis->ridr_port.interupt);
            printf("\t\tData Direction:\t%" PRIX8 "\n", d2hFis->ridr_port.dataDir);
            printf("\t\tReserved:\t\t%" PRIX8 "\n", d2hFis->ridr_port.rsv0);
            printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", d2hFis->ridr_port.pmport);
            //
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
            printf("\tE_Status:\t%02" PRIX8 "\n", d2hFis->eStatus);
            printf("\tTransfer Count (7:0):\t%02" PRIX8 "\n", d2hFis->transferCount);
            printf("\tTransfer Count (15:8):\t%02" PRIX8 "\n", d2hFis->transferCountHi);
            printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved2);
            printf("\tReserved:\t%02" PRIX8 "\n", d2hFis->reserved3);
        }
        break;
        case FIS_TYPE_DMA_ACT:
        {
            ptrSataDMAActivateFis dmaActFis = C_CAST(ptrSataDMAActivateFis, fis);
            if (fisLengthBytes < DMA_ACTIVATE_FIS_LENGTH)
            {
                printf("Invalid DMA Activate length, cannot print.\n");
                return;
            }
            printf("\tFisType:\t%02" PRIX8 " - DMA Activate\n", dmaActFis->fisType);
            printf("\tR_PORT:\t%02" PRIX8 "\n", dmaActFis->byte1);
            printf("\t\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved_port.reserved);
            printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", dmaActFis->reserved_port.pmport);
            printf("\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved1);
            printf("\tReserved:\t\t%" PRIX8 "\n", dmaActFis->reserved2);
        }
        break;
        case FIS_TYPE_DMA_SETUP:
        {
            ptrSataDMASetupFis dmaSetupFis = C_CAST(ptrSataDMASetupFis, fis);
            if (fisLengthBytes < DMA_SETUP_FIS_LENGTH)
            {
                printf("Invalid DMA Setup length, cannot print.\n");
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
        break;
        case FIS_TYPE_DATA:
        {
            ptrSataDataFis dataFis = C_CAST(ptrSataDataFis, fis);
            if (fisLengthBytes < DATA_FIS_LENGTH_MIN || fisLengthBytes > DATA_FIS_LENGTH_MAX)
            {
                printf("Invalid Data Fis length, cannot print.\n");
                return;
            }
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
        break;
        case FIS_TYPE_BIST:
        {
            ptrSataBISTActivateFis bistActFis = C_CAST(ptrSataBISTActivateFis, fis);
            if (fisLengthBytes < BIST_ACTIVATE_FIS_LENGTH)
            {
                printf("Invalid BIST Activate length, cannot print.\n");
                return;
            }
            printf("\tFisType:\t%02" PRIX8 " - BIST Activate\n", bistActFis->fisType);
            printf("\tR_PORT:\t%02" PRIX8 "\n", bistActFis->byte1);
            printf("\t\tReserved:\t%" PRIu8 "\n", bistActFis->reserved_port.reserved);
            printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", bistActFis->reserved_port.pmport);
            printf("\tPattern Definition:\t%02" PRIX8 "\n", bistActFis->patternDefinition);
            // TODO list out each bit individually
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
        break;
        case FIS_TYPE_DEV_BITS:
        {
            ptrSataSetDeviceBitsFis devBitsFis = C_CAST(ptrSataSetDeviceBitsFis, fis);
            if (fisLengthBytes < SET_DEVICE_BITS_FIS_LENGTH)
            {
                printf("Invalid Set Device Bits length, cannot print.\n");
                return;
            }
            printf("\tFisType:\t%02" PRIX8 " - Set Device Bits\n", devBitsFis->fisType);
            printf("\tNRR_PORT:\t%02" PRIX8 "\n", devBitsFis->byte1);
            // show the byte 1 bitfields independently
            printf("\t\tNotification:\t\t%" PRIX8 "\n", devBitsFis->nrr_port.notification);
            printf("\t\tReserved:\t\t%" PRIX8 "\n", devBitsFis->nrr_port.rsv1);
            printf("\t\tReserved:\t\t%" PRIX8 "\n", devBitsFis->nrr_port.rsv0);
            printf("\t\tPort Multiplier Port:\t%" PRIu8 "\n", devBitsFis->nrr_port.pmport);
            //
            printf("\tStatus:\t%02" PRIX8 "\n", devBitsFis->status);
            printf("\tError:\t%02" PRIX8 "\n", devBitsFis->error);
            printf("\tProtocol Specific:\t%08" PRIX32 "\n", devBitsFis->protocolSpecific);
            // TODO: break this down into the bits for each outstanding queued command
        }
        break;
        case FIS_TYPE_RES_FUT1:
        case FIS_TYPE_RES_FUT2:
        case FIS_TYPE_RES_FUT3:
        case FIS_TYPE_VEN_UNQ1:
        case FIS_TYPE_VEN_UNQ2:
        case FIS_TYPE_RES_FUT4:
        default: // unknown fis type/reserved/vendor unique
            if (fisPtr[0] == FIS_TYPE_VEN_UNQ1 || fisPtr[0] == FIS_TYPE_VEN_UNQ2)
            {
                printf("\tFisType:\t%02" PRIX8 " - Vendor Unique\n", fisPtr[0]);
            }
            else if (fisPtr[0] == FIS_TYPE_RES_FUT1 || fisPtr[0] == FIS_TYPE_RES_FUT2 ||
                     fisPtr[0] == FIS_TYPE_RES_FUT3 || fisPtr[0] == FIS_TYPE_RES_FUT4)
            {
                printf("\tFisType:\t%02" PRIX8 " - Reserved\n", fisPtr[0]);
            }
            else
            {
                printf("\tFisType:\t%02" PRIX8 " - Unknown\n", fisPtr[0]);
            }
            // start loop at 1 since we've already printed the first byte describing the type of FIS
            for (uint8_t fisIter = UINT8_C(1); fisIter < UNKNOWN_FIS_TYPE_LENGTH; ++fisIter)
            {
                printf("\tFIS[%2" PRIu8 "]:\t%02" PRIX8 "\n", fisIter, fisPtr[fisIter]);
            }
            printf("\n");
            break;
        }
    }
    RESTORE_NONNULL_COMPARE
}
