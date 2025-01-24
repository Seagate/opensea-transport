// SPDX-License-Identifier: MPL-2.0

//! \file sat_helper.h
//! \brief Defines the constants structures to help with SAT implementation
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(__cplusplus)
extern "C"
{
#endif

#define SAT_VPD_CMD_CODE_IDX (56)
#define SAT_VPD_PAGE_HEADER  (60)

#define SAT_DESCRIPTOR_CODE  (0x09)
#define SAT_ADDT_DESC_LEN    (0x0C)
#define SAT_DESC_LEN         (14) // 0x0E

#define SAT_DESC_EXTEND_BIT  (0x01)

#define SAT_EXTEND_BIT_SET   (0x01)
// These defines are 1 shifted for byte 1 of SAT cdb
#define SAT_ATA_HW_RESET         (0x00)
#define SAT_ATA_SW_RESET         (0x01 << 1)
#define SAT_NON_DATA             (0x03 << 1)
#define SAT_PIO_DATA_IN          (0x04 << 1)
#define SAT_PIO_DATA_OUT         (0x05 << 1)
#define SAT_DMA                  (0x06 << 1)
#define SAT_DMA_QUEUED           (0x07 << 1)
#define SAT_EXE_DEV_DIAG         (0x08 << 1)
#define SAT_NODATA_RESET         (0x09 << 1)
#define SAT_UDMA_DATA_IN         (0x0A << 1)
#define SAT_UDMA_DATA_OUT        (0x0B << 1)
#define SAT_FPDMA                (0x0C << 1)
#define SAT_RET_RESP_INFO        (0x0F << 1)

#define SAT_PROTOCOL_OFFSET      (1)
#define SAT_TRANSFER_BITS_OFFSET (2)

// SAT Spec Byte 2 specifics

// T_LENGTH Field Values based on SAT spec Table 139 - T_LENGTH field
#define SAT_T_LEN_XFER_NO_DATA (0x00) // No data is transferred
#define SAT_T_LEN_XFER_FET     (0x01) // The transfer length is an unsigned integer specified in the FEATURES (7:0) field.
#define SAT_T_LEN_XFER_SEC_CNT                                                                                         \
    (0x02) // The transfer length is an unsigned integer specified in the SECTOR_COUNT (7:0) field.
#define SAT_T_LEN_XFER_TPSIU   (0x03) // The transfer length is an unsigned integer specified in the TPSIU

#define SAT_BYTE_BLOCK_BIT_SET (0x04)
// T_DIR
#define SAT_T_DIR_DATA_OUT  (0x00)
#define SAT_T_DIR_DATA_IN   (0x08) // or (0x01 << 3) i.e. bit 3 is set)
#define SAT_T_TYPE_BIT_SET  (0x10)
#define SAT_CK_COND_BIT_SET (0x20)

// SAT VPD page 89h definitions
#define SAT_ATA_VPD_LENGTH                 UINT16_C(0x0238)
#define SAT_ATA_VPD_T10_VENDOR_OFFSET      (8)
#define SAT_ATA_VPD_T10_VENDOR_LENGTH      (8)
#define SAT_ATA_VPD_T10_PRODUCT_ID_OFFSET  (16)
#define SAT_ATA_VPD_T10_PRODUCT_ID_LENGTH  (16)
#define SAT_ATA_VPD_T10_PRODUCT_REV_OFFSET (32)
#define SAT_ATA_VPD_T10_PRODUCT_REV_LENGTH (4)
#define SAT_ATA_VPD_SIGNATURE_OFFSET       (36)
#define SAT_ATA_VPD_SIGNATURE_LENGTH       (20)
#define SAT_ATA_VPD_COMMAND_CODE_OFFSET    (56)
#define SAT_ATA_VPD_IDENTIFY_DATA_OFFSET   (60) // length is standard 512B from ATA specs

#if defined(__cplusplus)
}
#endif
