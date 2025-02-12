// SPDX-License-Identifier: MPL-2.0

//! \file scsi_helper_func.h
//! \brief Defines the function headers to help with SCSI implementation
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "cmds.h"
#include "common_public.h"
#include "scsi_helper.h"

#if defined(__cplusplus)
extern "C"
{
#endif

    // This is a private function that is used by the send CDB function below in order to contain printing debug output
    // in one location instead of multiple so that the ATA layer can also call this. Do not use this directly to send a
    // CDB. Use the scsi_Send_Cdb function instead.
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_PARAM_WO(2) eReturnValues private_SCSI_Send_CDB(ScsiIoCtx* scsiIoCtx, ptrSenseDataFields pSenseFields);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Send_Cdb()
    //
    //! \brief   Description:  This is a function to allow a caller to send whatever CDB they want. It is also used by
    //! the other functions in this file to send commands
    //
    //  Entry:
    //!   \param device - pointer to the device structure containing a valid device handle
    //!   \param cdb - pointer to the array holding a CDB. MUST BE NON-M_NULLPTR
    //!   \param cdbLen - value indicating a CDB len. Must be of eCDBLen type to be valid
    //!   \param pdata - pointer to the data buffer. Can be M_NULLPTR. If set to M_NULLPTR, dataLen must be set to 0
    //!   \param dataLen - length of the data buffer to use with the specified command
    //!   \param dataDirection - the data transfer direction. Must be of type eDataTransferDirection to be valid
    //!   \param senseData - pointer to the sense data buffer to be used. This can be M_NULLPTR. If set to M_NULLPTR,
    //!   the last command sense data in the device structure will be used. \param senseDataLen - length of the sense
    //!   data buffer. If set to 0, the last command sense data in the device structure will be used. \param
    //!   timeoutSeconds - number of seconds to set for the command timeout to the OS. If this is 0, 15 seconds will be
    //!   set.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(2, 3)
    M_NONNULL_IF_NONZERO_PARAM(4, 5)
    M_PARAM_RW_SIZE(4, 5)
    M_NONNULL_IF_NONZERO_PARAM(7, 8)
    M_PARAM_WO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Send_Cdb(tDevice*               device,
                                                      uint8_t*               cdb,
                                                      eCDBLen                cdbLen,
                                                      uint8_t*               pdata,
                                                      uint32_t               dataLen,
                                                      eDataTransferDirection dataDirection,
                                                      uint8_t*               senseData,
                                                      uint32_t               senseDataLen,
                                                      uint32_t               timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  uint16_t calculate_Logical_Block_Guard(uint8_t *buffer, uint32_t userDataLength, uint32_t totalDataLength)
    //
    //! \brief   Description:  This function will perform the SBC CRC16 calculation for a logical block guard used when
    //! a drive is formatted with PI. This should be used one sector at a time.
    //
    //  Entry:
    //!   \param buffer - pointer a buffer with user data
    //!   \param userDataLength - how many bytes of user data were returned (sector size, IE 512 or 4096)
    //!   \param totalDataLength - total length of the data buffer being pointed to so we don't run over memory. Should
    //!   be sector + 8 bytes.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO_SIZE(1, 3)
    OPENSEA_TRANSPORT_API uint16_t calculate_Logical_Block_Guard(const uint8_t* buffer,
                                                                 uint32_t       userDataLength,
                                                                 uint32_t       totalDataLength);

    //-----------------------------------------------------------------------------
    //
    //  check_Sense_Key_ASC_ASCQ_And_FRU()
    //
    //! \brief   Description:  Check the Sense Key, ACQ, and ACSQ. Based on these values, return value. return value of
    //! SUCCESS, FAILURE, UNSUPPORTED, etc.
    //!                        This should be used to help judge pass/fail based off of these variables. If verbosity is
    //!                        set to VERBOSITY_COMMAND_NAMES, this function will print out what these inputs mean.
    //
    //  Entry:
    //!   \param device - pointer to the device structure. Used when deciding what to show for the FRU code (more of a
    //!   future expansion option if we start parsing to human readable form) \param senseKey - senseKey \param asc -
    //!   additional code qualifier \param ascq - additional code sense qualifier \param fru - field replaceable unit
    //!   code (if available, just used for verbose output, can always insert a 0 here)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues
    check_Sense_Key_ASC_ASCQ_And_FRU(tDevice* device, uint8_t senseKey, uint8_t asc, uint8_t ascq, uint8_t fru);

    // this is meant to only be called by check_Sense_Key_asc_And_ascq()
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_NULL_TERM_STRING(2)
    M_PARAM_RO(2)
    OPENSEA_TRANSPORT_API
    void print_Field_Replacable_Unit_Code(tDevice* device, const char* fruMessage, uint8_t fruCode);

    // this is meant to only be called by check_Sense_Key_asc_And_ascq()
    M_NONNULL_PARAM_LIST(1)
    M_NULL_TERM_STRING(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API void print_acs_ascq(const char* acsAndascqStringToPrint, uint8_t ascValue, uint8_t ascqValue);

    // this is mean to only be called by check_Sense_Key_asc_And_ascq()
    M_NONNULL_PARAM_LIST(1)
    M_NULL_TERM_STRING(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API void print_sense_key(const char* senseKeyToPrint, uint8_t senseKeyValue);

    //-----------------------------------------------------------------------------
    //
    //  get_Sense_Key_ASC_ASCQ_FRU()
    //
    //! \brief   Description:  get the sense key, additional sense code, qualifier, and field replaceable unit code from
    //! the sense buffer
    //
    //  Entry:
    //!   \param pbuf - pointer to the sense buffer to analyze
    //!   \param pbufSize - size of the sense buffer pointed to by pbuf
    //!   \param senseKey - pointer to the variable to hold the senseKey
    //!   \param asc - pointer to the variable to hold the additional code qualifier
    //!   \param ascq - pointer to the variable to hold the additional code sense qualifier
    //!   \param fru - pointer tot he variable to hold the field replaceable unit code
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3, 4, 5, 6)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    M_PARAM_WO(6)
    OPENSEA_TRANSPORT_API void get_Sense_Key_ASC_ASCQ_FRU(const uint8_t* pbuf,
                                                          uint32_t       pbufSize,
                                                          uint8_t*       senseKey,
                                                          uint8_t*       asc,
                                                          uint8_t*       ascq,
                                                          uint8_t*       fru);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    OPENSEA_TRANSPORT_API void get_Sense_Data_Fields(const uint8_t*     ptrSenseData,
                                                     uint32_t           senseDataLength,
                                                     ptrSenseDataFields senseFields);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API void print_Sense_Fields(constPtrSenseDataFields senseFields);

    //-----------------------------------------------------------------------------
    //
    //  get_Sense_Key_Specific_Information(uint8_t *ptrSenseData, uint32_t senseDataLength, ptrSenseKeySpecific sksp)
    //
    //! \brief   Description:  Will get the sense key specific information from a sense data buffer if one is available.
    //
    //  Entry:
    //!   \param ptrSenseData - pointer to the sense buffer to analyze
    //!   \param senseDataLength - size of the sense buffer pointed to by ptrSenseData
    //!   \param sksp - pointer to the structure that will hold the returned data. check the valid bit to make sure
    //!   something was filled in, use the type to parse the info out correctly
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    OPENSEA_TRANSPORT_API void get_Sense_Key_Specific_Information(const uint8_t*      ptrSenseData,
                                                                  uint32_t            senseDataLength,
                                                                  ptrSenseKeySpecific sksp);

    M_NONNULL_PARAM_LIST(1, 3, 4)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    OPENSEA_TRANSPORT_API void get_Information_From_Sense_Data(const uint8_t* ptrSenseData,
                                                               uint32_t       senseDataLength,
                                                               bool*          valid,
                                                               uint64_t*      information);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    OPENSEA_TRANSPORT_API void get_Illegal_Length_Indicator_From_Sense_Data(const uint8_t* ptrSenseData,
                                                                            uint32_t       senseDataLength,
                                                                            bool*          illegalLengthIndicator);

    M_NONNULL_PARAM_LIST(1, 3, 4, 5)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    OPENSEA_TRANSPORT_API void get_Stream_Command_Bits_From_Sense_Data(const uint8_t* ptrSenseData,
                                                                       uint32_t       senseDataLength,
                                                                       bool*          filemark,
                                                                       bool*          endOfMedia,
                                                                       bool*          illegalLengthIndicator);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    OPENSEA_TRANSPORT_API void get_Command_Specific_Information_From_Sense_Data(const uint8_t* ptrSenseData,
                                                                                uint32_t       senseDataLength,
                                                                                uint64_t* commandSpecificInformation);
    //-----------------------------------------------------------------------------
    //
    //  uint16_t get_Returned_Sense_Data_Length(uint8_t *pbuf)
    //
    //! \brief   Description:  get the number of returned sense bytes from the sense data. For no sense data, 8 bytes
    //! will be returned, for vendor specific or unknown formats, 252 bytes will be returned, otherwise, the return
    //! value is 8 + additional sense length (byte 7 in fixed and descriptor format)
    //
    //  Entry:
    //!   \param pbuf - pointer to the sense buffer to analyze
    //!
    //  Exit:
    //!   \return none
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API uint16_t get_Returned_Sense_Data_Length(const uint8_t* pbuf);

#define NO_SERVICE_ACTION UINT8_C(0)

    // length can be transfer length, parameter list length, or allocation length
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE uint8_t* set_Typical_SCSI_6B_CDB_Fields(uint8_t* cdb,
                                                            uint8_t  operationCode,
                                                            uint32_t lba, /*21 bits total*/
                                                            uint8_t  length,
                                                            uint8_t  controlByte)
    {
        cdb[OPERATION_CODE] = operationCode;
        cdb[1]              = M_Byte2(lba) & UINT8_C(0x1F);
        cdb[2]              = M_Byte1(lba);
        cdb[3]              = M_Byte0(lba);
        cdb[4]              = length;
        cdb[5]              = controlByte;
        return cdb;
    }

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE uint8_t* set_Typical_SCSI_10B_CDB_Fields(uint8_t* cdb,
                                                             uint8_t  operationCode,
                                                             uint8_t  serviceAction, /* optional */
                                                             uint32_t lba,
                                                             uint16_t length,
                                                             uint8_t  controlByte)
    {
        cdb[OPERATION_CODE] = operationCode;
        cdb[1]              = serviceAction;
        cdb[2]              = M_Byte3(lba);
        cdb[3]              = M_Byte2(lba);
        cdb[4]              = M_Byte1(lba);
        cdb[5]              = M_Byte0(lba);
        // bytes 6 is misc
        cdb[7] = M_Byte1(length);
        cdb[8] = M_Byte0(length);
        cdb[9] = controlByte;
        return cdb;
    }

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE uint8_t* set_Typical_SCSI_12B_CDB_Fields(uint8_t* cdb,
                                                             uint8_t  operationCode,
                                                             uint8_t  serviceAction, /* optional */
                                                             uint32_t lba,
                                                             uint32_t length,
                                                             uint8_t  controlByte)
    {
        cdb[OPERATION_CODE] = operationCode;
        cdb[1]              = serviceAction;
        cdb[2]              = M_Byte3(lba);
        cdb[3]              = M_Byte2(lba);
        cdb[4]              = M_Byte1(lba);
        cdb[5]              = M_Byte0(lba);
        cdb[6]              = M_Byte3(length);
        cdb[7]              = M_Byte2(length);
        cdb[8]              = M_Byte1(length);
        cdb[9]              = M_Byte0(length);
        // 10 is misc
        cdb[11] = controlByte;
        return cdb;
    }

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE uint8_t* set_Typical_SCSI_16B_CDB_Fields_32Bit_LBA(uint8_t* cdb,
                                                                       uint8_t  operationCode,
                                                                       uint8_t  serviceAction, /* optional */
                                                                       uint32_t lba,
                                                                       uint32_t length,
                                                                       uint8_t  controlByte)
    {
        cdb[OPERATION_CODE] = operationCode;
        cdb[1]              = serviceAction;
        cdb[2]              = M_Byte3(lba);
        cdb[3]              = M_Byte2(lba);
        cdb[4]              = M_Byte1(lba);
        cdb[5]              = M_Byte0(lba);
        // 6 - 9 is misc
        cdb[10] = M_Byte3(length);
        cdb[11] = M_Byte2(length);
        cdb[12] = M_Byte1(length);
        cdb[13] = M_Byte0(length);
        // 14 is misc
        cdb[15] = controlByte;
        return cdb;
    }

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE uint8_t* set_Typical_SCSI_16B_CDB_Fields_64Bit_LBA(uint8_t* cdb,
                                                                       uint8_t  operationCode,
                                                                       uint8_t  serviceAction, /* optional */
                                                                       uint64_t lba,
                                                                       uint32_t length,
                                                                       uint8_t  controlByte)
    {
        cdb[OPERATION_CODE] = operationCode;
        cdb[1]              = serviceAction;
        cdb[2]              = M_Byte7(lba);
        cdb[3]              = M_Byte6(lba);
        cdb[4]              = M_Byte5(lba);
        cdb[5]              = M_Byte4(lba);
        cdb[6]              = M_Byte3(lba);
        cdb[7]              = M_Byte2(lba);
        cdb[8]              = M_Byte1(lba);
        cdb[9]              = M_Byte0(lba);
        cdb[10]             = M_Byte3(length);
        cdb[11]             = M_Byte2(length);
        cdb[12]             = M_Byte1(length);
        cdb[13]             = M_Byte0(length);
        // 14 is misc
        cdb[15] = controlByte;
        return cdb;
    }

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE uint8_t* set_Typical_SCSI_32_CDB_Fields(uint8_t* cdb,
                                                            uint8_t  operationCode,
                                                            uint16_t serviceAction,
                                                            uint64_t lba,
                                                            uint32_t length,
                                                            uint8_t  controlByte)
    {
        cdb[OPERATION_CODE] = operationCode;
        cdb[1]              = controlByte;
        // skip misc
        cdb[7] = UINT8_C(0x18);
        cdb[8] = M_Byte1(serviceAction);
        cdb[9] = M_Byte0(serviceAction);
        // skip misc
        cdb[12] = M_Byte7(lba);
        cdb[13] = M_Byte6(lba);
        cdb[14] = M_Byte5(lba);
        cdb[15] = M_Byte4(lba);
        cdb[16] = M_Byte3(lba);
        cdb[17] = M_Byte2(lba);
        cdb[18] = M_Byte1(lba);
        cdb[19] = M_Byte0(lba);
        // skip misc
        cdb[28] = M_Byte3(length);
        cdb[29] = M_Byte2(length);
        cdb[30] = M_Byte1(length);
        cdb[31] = M_Byte0(length);
        return cdb;
    }

    static M_INLINE uint8_t get_Group_Number(uint8_t group)
    {
        return (M_STATIC_CAST(uint8_t, group & UINT8_C(0x1F)));
    }

#define GET_GROUP_CODE(groupNumber) get_Group_Number(groupNumber)

    static M_INLINE uint8_t get_Protect(uint8_t protect)
    {
        return (M_STATIC_CAST(uint8_t, (protect & 0x07) << 5));
    }

#define GET_PROTECT_VAL(protect) get_Protect(protect)

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1)
    static M_INLINE void set_SCSI_32B_PI_Fields(uint8_t* cdb,
                                                uint8_t  groupNumber,
                                                uint8_t  protect,
                                                uint32_t expectedInitialLogicalBlockRefTag,
                                                uint16_t expectedLogicalBlockAppTag,
                                                uint16_t logicalBlockAppTagMask)
    {
        cdb[6]  = GET_GROUP_CODE(groupNumber);
        cdb[10] = GET_PROTECT_VAL(protect);
        cdb[20] = M_Byte3(expectedInitialLogicalBlockRefTag);
        cdb[21] = M_Byte2(expectedInitialLogicalBlockRefTag);
        cdb[22] = M_Byte1(expectedInitialLogicalBlockRefTag);
        cdb[23] = M_Byte0(expectedInitialLogicalBlockRefTag);
        cdb[24] = M_Byte1(expectedLogicalBlockAppTag);
        cdb[25] = M_Byte0(expectedLogicalBlockAppTag);
        cdb[26] = M_Byte1(logicalBlockAppTagMask);
        cdb[27] = M_Byte0(logicalBlockAppTagMask);
    }

    //-----------------------------------------------------------------------------
    //
    //  scsi_Inquiry()
    //
    //! \brief   Description:  Function to send a SCSI Inquiry command
    //
    //  Entry:
    //!   \param[in] device - file descriptor
    //!   \param[out] pdata - pointer to the data buffer to fill in with data
    //!   \param[in] dataLength - length of data to request and length of data buffer
    //!   \param[in] pageCode - which vital product data page to request (leave at 0 for standard inquiry data)
    //!   \param[in] evpd - set to 1 to read vital product data pages, set to 0 for standard inquiry data
    //!   \param[in] cmdDt - set to 1 to get command support information. NOTE: This was declared obsolete in SPC3
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    M_NONNULL_IF_NONZERO_PARAM(2, 3)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues
    scsi_Inquiry(tDevice* device, uint8_t* pdata, uint32_t dataLength, uint8_t pageCode, bool evpd, bool cmdDt);

    //-----------------------------------------------------------------------------
    //
    //  fill_In_Device_Info()
    //
    //! \brief   Description:  Sends a SCSI Inquiry and fills in the device information
    //
    //  Entry:
    //!   \param[out] device - pointer to the device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues fill_In_Device_Info(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  copy_Inquiry_Data()
    //
    //! \brief   Description:  copy necessary data into the drive info struct
    //
    //  Entry:
    //!   \param[in] pbuf - pointer to the data buffer containing inquiry data
    //!   \param[out] info - pointer to the driveInfo structure to  fill in
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2) M_PARAM_RO(1) M_PARAM_WO(2) void copy_Inquiry_Data(uint8_t* pbuf, driveInfo* info);

    //-----------------------------------------------------------------------------
    //
    //  copy_Serial_Number()
    //
    //! \brief   Description:  copy the serial number off of VPD page 0x80
    //
    //  Entry:
    //!   \param[in] pbuf - pointer to the data buffer containing VPD page 0x80 data
    //!   \param[in] bufferlen - Length of the buffer pointed to by pbuf
    //!   \param[out] serialNumber - pointer to the string to hold the serial number
    //!   \param[in] serialNumberMemLen - Length of the buffer pointed to by serialNumber
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO_SIZE(3, 4)
    void copy_Serial_Number(uint8_t* pbuf, size_t bufferlen, char* serialNumber, size_t serialNumberMemLen);

    //-----------------------------------------------------------------------------
    //
    //  copy_Read_Capacity_Info()
    //
    //! \brief   Description:  copy the maxLBA and logical blocksize from read capacity 10 or read capacity 16
    //! information
    //
    //  Entry:
    //!   \param logicalBlockSize - pointer to the variable to set with the logical block size
    //!   \param physicalBlockSize - pointer to the variable to set with the physical block size
    //!   \param maxLBA - pointer to the variable to get set
    //!   \param sectorAlignment - pointer to the variable to set the sector alignment
    //!   \param ptrBuf - pointer to the data buffer containing VPD page 0x80 data
    //!   \param readCap16 - set to true is the data buffer is from a read capacity 16 command, otherwise set to false
    //!   for a buffer that is from read capacity 10
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2, 3, 4, 5)
    M_PARAM_WO(1)
    M_PARAM_WO(2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_RO(5)
    OPENSEA_TRANSPORT_API void copy_Read_Capacity_Info(uint32_t* logicalBlockSize,
                                                       uint32_t* physicalBlockSize,
                                                       uint64_t* maxLBA,
                                                       uint16_t* sectorAlignment,
                                                       uint8_t*  ptrBuf,
                                                       bool      readCap16);

    //-----------------------------------------------------------------------------
    //
    //  is_Device_SAT_Compliant()
    //
    //! \brief   Description:  Function to check if the device is SAT compliant
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = not an ATA device, or SAT not supported
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) eReturnValues check_SAT_Compliance_And_Set_Drive_Type(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  scsi_sanitize_cmd()
    //
    //! \brief   Description: Function to send a SCSI Sanitize command. Use the functions below this one for the
    //! features defined in SBC4 spec. Only call this if you know what you are doing.
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] sanitizeFeature - enum value specifying the sanitize service action to perform
    //!   \param[in] immediate - set to true to set the immediate bit
    //!   \param[in] znr - zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize. \param[in] ause - set to true to set the allow unrestricted sanitize exit bit
    //!   \param[in] parameterListLength - this should be 0 for all features that are not an overwrite
    //!   \param[in] ptrData - should be M_NULLPTR for all features that are not an overwrite
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_RO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Sanitize_Cmd(tDevice*             device,
                                                          eScsiSanitizeFeature sanitizeFeature,
                                                          bool                 immediate,
                                                          bool                 znr,
                                                          bool                 ause,
                                                          uint16_t             parameterListLength,
                                                          uint8_t*             ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Sanitize_Overwrite()
    //
    //! \brief   Description: Sends a SCSI sanitize overwrite erase command using scsi_Sanitize_Cmd. This command will
    //! allocate the data buffer for you with all the bits and patterns set according to the inputs
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] allowUnrestrictedSanitizeExit = set to true to set the allow unrestricted sanitize exit bit
    //!   \param[in] znr - zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize. \param[in] immediate = set to true to set the immediate bit \param[in]
    //!   invertBetweenPasses = set to true to set the invert between passes in the transferred buffer \param[in] test =
    //!   enum value specifying the test bits. Should be SANITIZE_OVERWRITE_NO_CHANGES unless you know what a vendor is
    //!   expecting \param[in] overwritePasses = set to the number of overwrite passes to perform. A value of zero is
    //!   reserved \param[in] pattern = pointer to an array containing your pattern seperated in bytes. This will be
    //!   copied for you to the transferred data buffer. may be M_NULLPTR if patternLengthBytes is set to 0 \param[in]
    //!   patternLengthBytes = set to the length of your pattern in bytes. This length must be less than the logical
    //!   block length of the device as required by SBC4
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Sanitize_Overwrite(tDevice* device,
                                                                bool     allowUnrestrictedSanitizeExit,
                                                                bool     znr,
                                                                bool     immediate,
                                                                bool     invertBetweenPasses,
                                                                eScsiSanitizeOverwriteTest test,
                                                                uint8_t                    overwritePasses,
                                                                uint8_t*                   pattern,
                                                                uint16_t                   patternLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Sanitize_Exit_Failure_Mode()
    //
    //! \brief   Description: Sends a SCSI sanitize exit failure mode command using scsi_Sanitize_Cmd
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues scsi_Sanitize_Exit_Failure_Mode(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Sanitize_Cryptographic_Erase()
    //
    //! \brief   Description: Sends a SCSI sanitize cryptographic erase command using scsi_Sanitize_Cmd
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] allowUnrestrictedSanitizeExit = set to true to set the allow unrestricted sanitize exit bit
    //!   \param[in] immediate = set to true to set the immediate bit
    //!   \param[in] znr - zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Sanitize_Cryptographic_Erase(tDevice* device,
                                                                          bool     allowUnrestrictedSanitizeExit,
                                                                          bool     immediate,
                                                                          bool     znr);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Sanitize_Block_Erase()
    //
    //! \brief   Description: Sends a SCSI sanitize block erase command using scsi_Sanitize_Cmd
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] allowUnrestrictedSanitizeExit = set to true to set the allow unrestricted sanitize exit bit
    //!   \param[in] immediate = set to true to set the immediate bit
    //!   \param[in] znr - zone no reset bit. This is used on host managed and host aware drives to not reset the zone
    //!   pointers during a sanitize.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Sanitize_Block_Erase(tDevice* device,
                                                                  bool     allowUnrestrictedSanitizeExit,
                                                                  bool     immediate,
                                                                  bool     znr);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Request_Sense_Cmd()
    //
    //! \brief   Description:  Send a SCSI Request Sense Command
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] descriptorBit - set to 1 to request descriptor format sense data, 0 for fixed format sense data
    //!   \param[out] pdata - pointer to the data buffer to fill in with sense data
    //!   \param[in] dataSize - size of the data requested. Default should be 252
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Request_Sense_Cmd(tDevice* device,
                                                               bool     descriptorBit,
                                                               uint8_t* pdata,
                                                               uint16_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Report_Supported_Operation_Codes()
    //
    //! \brief   Description:  Send a SCSI Report Supported Operation Codes Command
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[in] rctd - set to true to set the RCTD bit
    //!   \param[in] reportingOptions -
    //!   \param[in] requestedOperationCode -
    //!   \param[in] reequestedServiceAction -
    //!   \param[in] allocationLength -
    //!   \param[in] ptrData -
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 7)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Supported_Operation_Codes(tDevice* device,
                                                                              bool     rctd,
                                                                              uint8_t  reportingOptions,
                                                                              uint8_t  requestedOperationCode,
                                                                              uint16_t reequestedServiceAction,
                                                                              uint32_t allocationLength,
                                                                              uint8_t* ptrData);

    typedef enum eSCSICmdSupportEnum
    {
        SCSI_CMD_SUPPORT_UNKNOWN =
            -1, // for the case when it cannot be determined with report supported operation codes or cmddt
        SCSI_CMD_SUPPORT_UNKNOWN_RETRY =
            0, // support field = 0 in SPC in response. Possibly need to spin the drive up first
        SCSI_CMD_SUPPORT_NOT_SUPPORTED,
        SCSI_CMD_SUPPORT_RESERVED,
        SCSI_CMD_SUPPORT_SUPPORTED_TO_SCSI_STANDARD,
        SCSI_CMD_SUPPORT_RESERVED2,
        SCSI_CMD_SUPPORT_SUPPORTED_VENDOR_SPECIFIC,
        SCSI_CMD_SUPPORT_RESERVED3,
        SCSI_CMD_SUPPORT_RESERVED4
    } eSCSICmdSupport;

    typedef struct s_scsiOperationCodeInfoRequest
    {
        uint8_t  operationCode;             // Required
        bool     serviceActionValid;        // Required to know if next field contains valid data or not
        uint16_t serviceAction;             // optional. Set bool above if in use
        uint8_t  multipleLogicalUnits;      // Affect of this command on multiple logical units
        bool     requestRetriedWithoutSA;   // This is for the special cases where a retry was issued to try without the
                                            // service action (Write buffer mostly)
        uint16_t cdbUsageDataLength;        // length of following CDB usage data
        uint8_t  cdbUsageData[CDB_LEN_MAX]; // array of CDB usage data reported back up to max of 260 bytes
    } scsiOperationCodeInfoRequest, *ptrScsiOperationCodeInfoRequest;

    //-----------------------------------------------------------------------------
    //
    //  is_SCSI_Operation_Code_Supported(tDevice *device, ptrScsiOperationCodeInfoRequest request)
    //
    //! \brief   Description: Sends either report supported operation codes or inquiry cmddt to check if a operation
    //! code is supported or not.
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure
    //!   \param[inout] request - input and output structure that describes what operation code to check
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_PARAM_RW(2)
    OPENSEA_TRANSPORT_API eSCSICmdSupport is_SCSI_Operation_Code_Supported(tDevice*                        device,
                                                                           ptrScsiOperationCodeInfoRequest request);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Log_Sense_Cmd()
    //
    //! \brief   Description:  Function to Send a SCSI Log Sense Command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param saveParameters - set the saveParameters bit
    //!   \param pageControl - set the page Control field. Only bits 1:0 are valid
    //!   \param pageCode - the logpage you wish to read
    //!   \param subpageCode - the subpage you wish to read
    //!   \param paramPointer - the parameter you wish to read (0 reads everything)
    //!   \param ptrData - pointer to the data buffer to fill
    //!   \param dataSize - size of the data buffer and amount of data to be requested from the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 8)
    M_PARAM_WO_SIZE(7, 8)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Log_Sense_Cmd(tDevice* device,
                                                           bool     saveParameters,
                                                           uint8_t  pageControl,
                                                           uint8_t  pageCode,
                                                           uint8_t  subpageCode,
                                                           uint16_t paramPointer,
                                                           uint8_t* ptrData,
                                                           uint16_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_log_select_cmd()
    //
    //! \brief   Description:  Function to Send a SCSI Log Select Command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pcr - set the pcr bit (parameter code reset)
    //!   \param sp - set the sp bit (save parameters)
    //!   \param pageControl - the value of the page control field. Only bits 1:0 are valid
    //!   \param pageCode - the logpage you wish to read
    //!   \param subpageCode - the subpage you wish to read
    //!   \param parameterListLength - the parameter list length
    //!   \param ptrData - pointer to the data buffer to transfer. Must be Non-M_NULLPTR
    //!   \param dataSize - size of the data buffer
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Log_Select_Cmd(tDevice* device,
                                                            bool     pcr,
                                                            bool     sp,
                                                            uint8_t  pageControl,
                                                            uint8_t  pageCode,
                                                            uint8_t  subpageCode,
                                                            uint16_t parameterListLength,
                                                            uint8_t* ptrData,
                                                            uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Log_Sense_Cmd()
    //
    //! \brief   Description:  Function to Send a SCSI Send Diagnostic Command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param selfTestCode -
    //!   \param pageFormat -
    //!   \param selfTestBit -
    //!   \param deviceOffLIne -
    //!   \param unitOffLine -
    //!   \param parameterListLength -
    //!   \param pdata -pointer to the data buffer
    //!   \param dataSize - length of the data buffer
    //!   \param timeoutSeconds - timeout for the command. Useful for foreground tests.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Send_Diagnostic(tDevice* device,
                                                             uint8_t  selfTestCode,
                                                             uint8_t  pageFormat,
                                                             uint8_t  selfTestBit,
                                                             uint8_t  deviceOffLIne,
                                                             uint8_t  unitOffLine,
                                                             uint16_t parameterListLength,
                                                             uint8_t* pdata,
                                                             uint16_t dataSize,
                                                             uint32_t timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Read_Capacity_10()
    //
    //! \brief   Description:  Function to Send a SCSI Read Capacity 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pdata - pointer to the data buffer to be filled
    //!   \param dataSize - value describing length of the data buffer being sent in
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(2, 3)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Capacity_10(tDevice* device, uint8_t* pdata, uint16_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Read_Capacity_16()
    //
    //! \brief   Description:  Function to Send a SCSI Read Capacity 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pdata - pointer to the data buffer to be filled
    //!   \param dataSize - value describing length of the data buffer being sent in
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(2, 3)
    M_PARAM_WO_SIZE(2, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Capacity_16(tDevice* device, uint8_t* pdata, uint32_t dataSize);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    M_PARAM_WO(6)
    OPENSEA_TRANSPORT_API void get_mode_param_header_6_fields(uint8_t* ptrMP,
                                                              uint32_t mpHeaderLen,
                                                              uint8_t* modeDataLength,
                                                              uint8_t* mediumType,
                                                              uint8_t* devSpecific,
                                                              uint8_t* blockDescriptorLenth);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    M_PARAM_WO(6)
    OPENSEA_TRANSPORT_API void get_mode_param_header_10_fields(uint8_t*  ptrMP,
                                                               uint32_t  mpHeaderLen,
                                                               uint16_t* modeDataLength,
                                                               uint8_t*  mediumType,
                                                               uint8_t*  devSpecific,
                                                               bool*     longLBA,
                                                               uint16_t* blockDescriptorLenth);

    // All but direct access block devices
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    OPENSEA_TRANSPORT_API void get_mode_general_block_descriptor_fields(uint8_t*  ptrMPblkDesk,
                                                                        uint32_t  mpBlkDescLen,
                                                                        uint8_t*  densityCode,
                                                                        uint32_t* numberOfBlocks,
                                                                        uint32_t* blockLength);
    // direct access block devices
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    OPENSEA_TRANSPORT_API void get_mode_short_block_descriptor_fields(uint8_t*  ptrMPblkDesk,
                                                                      uint32_t  mpBlkDescLen,
                                                                      uint32_t* numberOfBlocks,
                                                                      uint32_t* blockLength);

    M_NONNULL_PARAM_LIST(1, 3, 4)
    M_PARAM_RO_SIZE(1, 2)
    M_PARAM_WO(3)
    M_PARAM_WO(4)
    OPENSEA_TRANSPORT_API void get_mode_long_block_descriptor_fields(uint8_t*  ptrMPblkDesk,
                                                                     uint32_t  mpBlkDescLen,
                                                                     uint64_t* numberOfBlocks,
                                                                     uint64_t* blockLength);

    M_NONNULL_PARAM_LIST(2)
    M_PARAM_RO_SIZE(2, 3)
    M_PARAM_WO(4)
    M_PARAM_WO(5)
    M_PARAM_WO(6)
    M_PARAM_WO(7)
    M_PARAM_WO(8)
    M_PARAM_WO(9)
    M_PARAM_WO(10)
    OPENSEA_TRANSPORT_API void get_SBC_Mode_Header_Blk_Desc_Fields(bool      sixByteCmd,
                                                                   uint8_t*  ptr,
                                                                   uint32_t  totalDataLen,
                                                                   uint16_t* modeDataLength,
                                                                   uint8_t*  mediumType,
                                                                   uint8_t*  devSpecific,
                                                                   bool*     longLBA,
                                                                   uint16_t* blockDescriptorLenth,
                                                                   uint64_t* numberOfBlocks,
                                                                   uint64_t* blockLength);

    //-----------------------------------------------------------------------------
    //
    //  scsi_modesense10()
    //
    //! \brief   Description:  Function to Send a SCSI Mode Sense 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pageCode - the mode page you wish to read
    //!   \param allocationLength - the length of the data to request
    //!   \param subPageCode - the subpage you wish to read
    //!   \param DBD - set the DBD bit
    //!   \param LLBAA - set the LLBAA bit
    //!   \param pageControl - value of page control field. Only bits 1:0 are valid.
    //!   \param ptrData - pointer to the data buffer to be filled
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 3)
    M_PARAM_WO_SIZE(8, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Mode_Sense_10(tDevice*             device,
                                                           uint8_t              pageCode,
                                                           uint32_t             allocationLength,
                                                           uint8_t              subPageCode,
                                                           bool                 DBD,
                                                           bool                 LLBAA,
                                                           eScsiModePageControl pageControl,
                                                           uint8_t*             ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Mode_Sense_6()
    //
    //! \brief   Description:  Function to Send a SCSI Mode Sense 6 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pageCode - the mode page you wish to read
    //!   \param allocationLength - the length of the data to request
    //!   \param subPageCode - the subpage you wish to read
    //!   \param DBD - set the DBD bit
    //!   \param pageControl - value of page control field. Only bits 1:0 are valid.
    //!   \param ptrData - pointer to the data buffer to be filled
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 3)
    M_PARAM_WO_SIZE(7, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Mode_Sense_6(tDevice*             device,
                                                          uint8_t              pageCode,
                                                          uint8_t              allocationLength,
                                                          uint8_t              subPageCode,
                                                          bool                 DBD,
                                                          eScsiModePageControl pageControl,
                                                          uint8_t*             ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Mode_Select_10()
    //
    //! \brief   Description:  Function to Send a SCSI Mode Select 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param parameterListLength -
    //!   \param pageFormat - set to false when reading page 0 if it is not formatted in page zero format.
    //!   \param savePages - set to true to save the mode page(s) to non-volatile memory. This may or may not be
    //!   supported on some drives. Some may only support saved pages \param resetToDefaults - set to true when
    //!   resetting all pages to defaults. No data shall be transferred with this bit set, or an error will occur \param
    //!   ptrData - pointer to the data buffer to send \param dataSize - value describing length of the data buffer
    //!   being passed in
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 7)
    M_PARAM_WO_SIZE(6, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Mode_Select_10(tDevice* device,
                                                            uint16_t parameterListLength,
                                                            bool     pageFormat,
                                                            bool     savePages,
                                                            bool     resetToDefaults,
                                                            uint8_t* ptrData,
                                                            uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_mode_select_6()
    //
    //! \brief   Description:  Function to Send a SCSI Mode Select 6 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param parameterListLength -
    //!   \param pageFormat - set to false when reading page 0 if it is not formatted in page zero format.
    //!   \param savePages - set to true to save the mode page(s) to non-volatile memory. This may or may not be
    //!   supported on some drives. Some may only support saved pages \param resetToDefaults - set to true when
    //!   resetting all pages to defaults. No data shall be transferred with this bit set, or an error will occur \param
    //!   ptrData - pointer to the data buffer to send \param dataSize - value describing length of the data buffer
    //!   being passed in
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 7)
    M_PARAM_WO_SIZE(6, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Mode_Select_6(tDevice* device,
                                                           uint8_t  parameterListLength,
                                                           bool     pageFormat,
                                                           bool     savePages,
                                                           bool     resetToDefaults,
                                                           uint8_t* ptrData,
                                                           uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Write_Buffer()
    //
    //! \brief   Description:  Function to Send a SCSI Write Buffer command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param mode - transfer mode
    //!   \param modeSpecific - See SPC spec for details since this depends on the contents of the mode field
    //!   \param bufferID -
    //!   \param bufferOffset -
    //!   \param parameterListLength -
    //!   \param ptrData - pointer to the data buffer to send to the tDevice
    //!   \param[in] firstSegment = Flag to help some low-level OSs know when the first segment of a firmware download
    //!   is happening...specifically Windows \param[in] lastSegment = Flag to help some low-level OSs know when the
    //!   last segment of a firmware download is happening...specifrically Windows \param[in] timeoutSeconds = set a
    //!   timeout in seconds for the command. This can be useful if some FWDL commands take longer (code activation for
    //!   example)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_RO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Buffer(tDevice*         device,
                                                          eWriteBufferMode mode,
                                                          uint8_t          modeSpecific,
                                                          uint8_t          bufferID,
                                                          uint32_t         bufferOffset,
                                                          uint32_t         parameterListLength,
                                                          uint8_t*         ptrData,
                                                          bool             firstSegment,
                                                          bool             lastSegment,
                                                          uint32_t         timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  read_media_serial_number()
    //
    //! \brief   Description:  Function to Send a SCSI Read Media Serial Number command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param ptrData - pointer to the data buffer to fill upon command completion
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 2)
    M_PARAM_WO_SIZE(3, 2)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Media_Serial_Number(tDevice* device,
                                                                      uint32_t allocationLength,
                                                                      uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_attribute()
    //
    //! \brief   Description:  Function to Send a SCSI Read Attribute command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param serviceAction -
    //!   \param restricted - see SMC-3 Spec, otherwise set to 0
    //!   \param logicalVolumeNumber -
    //!   \param partitionNumber -
    //!   \param firstAttributeIdentifier -
    //!   \param cacheBit - set the cache bit
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 7)
    M_PARAM_WO_SIZE(9, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Attribute(tDevice* device,
                                                            uint8_t  serviceAction,
                                                            uint32_t restricted,
                                                            uint8_t  logicalVolumeNumber,
                                                            uint8_t  partitionNumber,
                                                            uint16_t firstAttributeIdentifier,
                                                            uint32_t allocationLength,
                                                            bool     cacheBit,
                                                            uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Read_Buffer()
    //
    //! \brief   Description:  Function to Send a SCSI Read Buffer command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param mode -
    //!   \param bufferID -
    //!   \param bufferOffset -
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_WO_SIZE(6, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Buffer(tDevice* device,
                                                         uint8_t  mode,
                                                         uint8_t  bufferID,
                                                         uint32_t bufferOffset,
                                                         uint32_t allocationLength,
                                                         uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Read_Buffer_16(tDevice *device, uint8_t mode, uint8_t modeSpecific, uint8_t bufferID, uint64_t
    //  bufferOffset, uint32_t allocationLength, uint8_t *ptrData)
    //
    //! \brief   Description:  Function to Send a SCSI Read Buffer 16 command (SPC5)
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param mode -
    //!   \param bufferID -
    //!   \param bufferOffset -
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_WO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Buffer_16(tDevice* device,
                                                            uint8_t  mode,
                                                            uint8_t  modeSpecific,
                                                            uint8_t  bufferID,
                                                            uint64_t bufferOffset,
                                                            uint32_t allocationLength,
                                                            uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_receive_diagnostic_results()
    //
    //! \brief   Description:  Function to Send a SCSI Receive Diagnostic Result command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pcv - set the pcv bit
    //!   \param pageCode -
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion \param timeoutSeconds -
    //!   number of seconds to wait for this command (at most) to complete. If not sure, 0 will set the default of 15
    //!   seconds
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 4)
    M_PARAM_WO_SIZE(5, 4)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Receive_Diagnostic_Results(tDevice* device,
                                                                        bool     pcv,
                                                                        uint8_t  pageCode,
                                                                        uint16_t allocationLength,
                                                                        uint8_t* ptrData,
                                                                        uint32_t timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  scsi_remove_I_T_nexus()
    //
    //! \brief   Description:  Function to Send a SCSI Remove I_T Nexus command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param parameterListLength - length of the data buffer being sent to the device and length being requested
    //!   from the device \param ptrData - pointer to the data buffer to fill upon command completion \param dataSize -
    //!   size of the data buffer to use with the command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 4)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Remove_I_T_Nexus(tDevice* device,
                                                              uint32_t parameterListLength,
                                                              uint8_t* ptrData,
                                                              uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_aliases()
    //
    //! \brief   Description:  Function to Send a SCSI Report Aliases command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 2)
    M_PARAM_WO_SIZE(3, 2)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Aliases(tDevice* device,
                                                            uint32_t allocationLength,
                                                            uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_identifying_information()
    //
    //! \brief   Description:  Function to Send a SCSI Report Identifying Information command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param restricted - see SCC-2 or set to 0
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param identifyingInformationType - See SPC4. Only bits 6:0 are valid (this function will
    //!   automatically shift the bits into the proper position for the CDB) \param ptrData - pointer to the data buffer
    //!   to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 3)
    M_PARAM_WO_SIZE(5, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Identifying_Information(tDevice* device,
                                                                            uint16_t restricted,
                                                                            uint32_t allocationLength,
                                                                            uint8_t  identifyingInformationType,
                                                                            uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_luns()
    //
    //! \brief   Description:  Function to Send a SCSI Report LUNs command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param selectReport -
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 3)
    M_PARAM_WO_SIZE(4, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Luns(tDevice* device,
                                                         uint8_t  selectReport,
                                                         uint32_t allocationLength,
                                                         uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_priority()
    //
    //! \brief   Description:  Function to Send a SCSI Report Priority command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param priorityReported -
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 3)
    M_PARAM_WO_SIZE(4, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Priority(tDevice* device,
                                                             uint8_t  priorityReported,
                                                             uint32_t allocationLength,
                                                             uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_supported_task_management_functions()
    //
    //! \brief   Description:  Function to Send a SCSI Report Supported Task Management Functions command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param repd - set the repd bit
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 3)
    M_PARAM_WO_SIZE(4, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Supported_Task_Management_Functions(tDevice* device,
                                                                                        bool     repd,
                                                                                        uint32_t allocationLength,
                                                                                        uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_timestamp()
    //
    //! \brief   Description:  Function to Send a SCSI Report Timestamp command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param allocationLength - length of the data buffer being sent to the device and length being requested from
    //!   the device \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 2)
    M_PARAM_WO_SIZE(3, 2)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Timestamp(tDevice* device,
                                                              uint32_t allocationLength,
                                                              uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_SecurityProtocol_In()
    //
    //! \brief   Description:  Sends a SCSI Security Protocol In command to a device
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param securityProtocol - security protocol being used
    //!   \param securityProtocolSpecific - any specific information to the security protocol being used
    //!   \param inc512 - set the inc512 bit. This means the value in allocationLength field specifies the amount of
    //!   data to transfer is 512byte sizes \param ptrData - pointer to the data buffer that will do the transfer \param
    //!   allocationLength - size of the data to transfer
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_WO(6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_SecurityProtocol_In(tDevice* device,
                                                                 uint8_t  securityProtocol,
                                                                 uint16_t securityProtocolSpecific,
                                                                 bool     inc512,
                                                                 uint32_t allocationLength,
                                                                 uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_SecurityProtocol_Out()
    //
    //! \brief   Description:  Sends a SCSI Security Protocol Out command to a device
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param securityProtocol - security protocol being used
    //!   \param securityProtocolSpecific - any specific information to the security protocol being used
    //!   \param inc512 - set the inc512 bit. This means the value in transferLength field specifies the amount of data
    //!   to transfer is 512byte sizes \param ptrData - pointer to the data buffer that will do the transfer \param
    //!   transferLength - size of the data to transfer \param timeout - time in seconds to wait for the command. (Added
    //!   this for passing through ATA Security commands)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_RO(6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_SecurityProtocol_Out(tDevice* device,
                                                                  uint8_t  securityProtocol,
                                                                  uint16_t securityProtocolSpecific,
                                                                  bool     inc512,
                                                                  uint32_t transferLength,
                                                                  uint8_t* ptrData,
                                                                  uint32_t timeout);

    //-----------------------------------------------------------------------------
    //
    //  scsi_set_identifying_information()
    //
    //! \brief   Description:  Send a SCSI Set Identifying Information command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param restricted - see SCC-2 or set to 0
    //!   \param parameterListLength - length of the data buffer
    //!   \param identifyingInformationType - see SPC4. Only bits 6:0 are valid since this function shifts this
    //!   information into the appropriate place in the CDB \param ptrData - pointer to the data buffer to send to the
    //!   device \param dataSize - size of the data buffer being sent to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 6)
    M_PARAM_RO_SIZE(5, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Set_Identifying_Information(tDevice* device,
                                                                         uint16_t restricted,
                                                                         uint32_t parameterListLength,
                                                                         uint8_t  identifyingInformationType,
                                                                         uint8_t* ptrData,
                                                                         uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_set_priority()
    //
    //! \brief   Description:  Send a SCSI Set Priority command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param I_T_L_NexusToSet - only bits 1:0 are valid since this data gets shifted when set in the CDB
    //!   \param parameterListLength - length of the data buffer
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param dataSize - size of the data buffer being sent to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 5)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Set_Priority(tDevice* device,
                                                          uint8_t  I_T_L_NexusToSet,
                                                          uint32_t parameterListLength,
                                                          uint8_t* ptrData,
                                                          uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_set_target_port_groups()
    //
    //! \brief   Description:  Send a SCSI Set Target Port Groups command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param parameterListLength - length of the data buffer
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param dataSize - size of the data buffer being sent to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 4)
    M_PARAM_RO_SIZE(3, 4)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Set_Target_Port_Groups(tDevice* device,
                                                                    uint32_t parameterListLength,
                                                                    uint8_t* ptrData,
                                                                    uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_set_timestamp()
    //
    //! \brief   Description:  Send a SCSI Set Timestamp command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param parameterListLength - length of the data buffer
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(3, 2)
    M_PARAM_RO_SIZE(3, 2)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Set_Timestamp(tDevice* device,
                                                           uint32_t parameterListLength,
                                                           uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Test_Unit_Ready()
    //
    //! \brief   Description:  Send a SCSI Test Unit Ready command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param pReturnStatus - pointer to the SCSI status structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //!           tDevice.drive_info.lastCommandSenseData has the sense code.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_PARAM_WO(2) OPENSEA_TRANSPORT_API eReturnValues scsi_Test_Unit_Ready(tDevice* device, scsiStatus* pReturnStatus);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_attribute()
    //
    //! \brief   Description:  Send a SCSI Write Attribute command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wtc - set to true to set the wtc bit
    //!   \param restricted - see SMC3 or set to 0
    //!   \param logicalVolumeNumber -
    //!   \param partitionNumber -
    //!   \param parameterListLength - length of the data buffer
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_RO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Attribute(tDevice* device,
                                                             bool     wtc,
                                                             uint32_t restricted,
                                                             uint8_t  logicalVolumeNumber,
                                                             uint8_t  partitionNumber,
                                                             uint32_t parameterListLength,
                                                             uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_compare_and_write()
    //
    //! \brief   Description:  Send a SCSI Compare And Write command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - bits 2:0 are valid since this data gets shifted into position in this function
    //!   \param dpo - set to true to set the dpo bit
    //!   \param fua - set to true to set the fua bit
    //!   \param logicalBlockAddress -
    //!   \param numberOfLogicalBlocks -
    //!   \param groupNumber - only bits 4:0 are valid
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param transferLengthBytes - number of bytes pointed to by the ptrData
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Compare_And_Write(tDevice* device,
                                                               uint8_t  wrprotect,
                                                               bool     dpo,
                                                               bool     fua,
                                                               uint64_t logicalBlockAddress,
                                                               uint8_t  numberOfLogicalBlocks,
                                                               uint8_t  groupNumber,
                                                               uint8_t* ptrData,
                                                               uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_format_unit()
    //
    //! \brief   Description:  Send a SCSI Format Unit command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param fmtpInfo - bits 2:0 are valid since this data gets shifted into position in this function
    //!   \param longList - set to true to set the longList bit
    //!   \param fmtData - set to true to set the fmtData bit
    //!   \param cmplst - set to true to set the cmplst bit
    //!   \param defectListFormat - set to a value for the defect list format
    //!   \param vendorSpecific - set to 0 unless you know what you need here
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param dataSize - size of the data buffer to transfer
    //!   \param ffmt - set to the ffmt bit value you want. 0 - 3 are valid values. See SBC4. For default format unit,
    //!   set to 0 \param timeoutSeconds - set to how long to wait for a format in seconds. If the immediate bit is not
    //!   set in the paramter data, then this should be a long time...otherwise 15 seconds is plenty
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Format_Unit(tDevice* device,
                                                         uint8_t  fmtpInfo,
                                                         bool     longList,
                                                         bool     fmtData,
                                                         bool     cmplst,
                                                         uint8_t  defectListFormat,
                                                         uint8_t  vendorSpecific,
                                                         uint8_t* ptrData,
                                                         uint32_t dataSize,
                                                         uint8_t  ffmt,
                                                         uint32_t timeoutSeconds);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues
    scsi_Format_With_Preset(tDevice* device, bool immed, bool fmtmaxlba, uint32_t presetID, uint32_t timeoutSeconds);

    //-----------------------------------------------------------------------------
    //
    //  scsi_get_lba_status()
    //
    //! \brief   Description:  Send a SCSI Get LBA status command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param logicalBlockAddress - starting LBA
    //!   \param allocationLength - size of the data buffer to transfer
    //!   \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 3)
    M_PARAM_WO_SIZE(4, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Get_Lba_Status(tDevice* device,
                                                            uint64_t logicalBlockAddress,
                                                            uint32_t allocationLength,
                                                            uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_orwrite_16()
    //
    //! \brief   Description:  Send a SCSI ORWRITE 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param orProtect - only bits 2:0 are valid
    //!   \param dpo - if true, set the dpo bit
    //!   \param fua - if true, set the fua bit
    //!   \param logicalBlockAddress -
    //!   \param transferLengthBlocks - size of the data buffer to transfer
    //!   \param groupNumber - see SBC3. Only bits 4:0 are valid
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param transferLengthBytes - number of bytes pointed to by the ptrData
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Orwrite_16(tDevice* device,
                                                        uint8_t  orProtect,
                                                        bool     dpo,
                                                        bool     fua,
                                                        uint64_t logicalBlockAddress,
                                                        uint32_t transferLengthBlocks,
                                                        uint8_t  groupNumber,
                                                        uint8_t* ptrData,
                                                        uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_orwrite_32()
    //
    //! \brief   Description:  Send a SCSI ORWRITE 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param bmop -
    //!   \param previousGenProcessing -
    //!   \param groupNumber - see SBC3. Only bits 4:0 are valid
    //!   \param orProtect - only bits 2:0 are valid
    //!   \param dpo - if true, set the dpo bit
    //!   \param fua - if true, set the fua bit
    //!   \param logicalBlockAddress -
    //!   \param expectedORWgen -
    //!   \param newORWgen -
    //!   \param transferLengthBlocks - size of the data buffer to transfer
    //!   \param ptrData - pointer to the data buffer to send to the device
    //!   \param transferLengthBytes - number of bytes pointed to by the ptrData
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(12, 13)
    M_PARAM_RO_SIZE(12, 13)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Orwrite_32(tDevice* device,
                                                        uint8_t  bmop,
                                                        uint8_t  previousGenProcessing,
                                                        uint8_t  groupNumber,
                                                        uint8_t  orProtect,
                                                        bool     dpo,
                                                        bool     fua,
                                                        uint64_t logicalBlockAddress,
                                                        uint32_t expectedORWgen,
                                                        uint32_t newORWgen,
                                                        uint32_t transferLengthBlocks,
                                                        uint8_t* ptrData,
                                                        uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_prefetch_10()
    //
    //! \brief   Description:  Send a SCSI Pre Fetch 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param immediate - set the immediate bit
    //!   \param logicalBlockAddress -
    //!   \param groupNumber - see SBC3. Only bits 4:0 are valid
    //!   \param prefetchLength -
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Prefetch_10(tDevice* device,
                                                         bool     immediate,
                                                         uint32_t logicalBlockAddress,
                                                         uint8_t  groupNumber,
                                                         uint16_t prefetchLength);

    //-----------------------------------------------------------------------------
    //
    //  scsi_prefetch_16()
    //
    //! \brief   Description:  Send a SCSI Pre Fetch 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param immediate - set the immediate bit
    //!   \param logicalBlockAddress -
    //!   \param groupNumber - see SBC3. Only bits 4:0 are valid
    //!   \param prefetchLength -
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Prefetch_16(tDevice* device,
                                                         bool     immediate,
                                                         uint64_t logicalBlockAddress,
                                                         uint8_t  groupNumber,
                                                         uint32_t prefetchLength);

    //-----------------------------------------------------------------------------
    //
    //  scsi_prevent_allow_medium_removal()
    //
    //! \brief   Description:  Send a SCSI Prevent Allow Medium Removal command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param prevent - only bits 1:0 are valid
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Prevent_Allow_Medium_Removal(tDevice* device, uint8_t prevent);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Read_6(tDevice *device, uint32_t logicalBlockAddress, uint8_t transferLengthBlocks, uint8_t* ptrData,
    //  uint32_t transferLengthBytes);
    //
    //! \brief   Description:  Send a SCSI Read 6 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param logicalBlockAddress - LBA to read. Only 21 bits are valid
    //!   \param transferLengthBlocks - number of logical blocks to transfer
    //!   \param ptrData - pointer to the data buffer to fill. This MUST be at least (transferLength *
    //!   device.driveInfo.deviceBlockSize) in size or you will have memory errors \param transferLengthBytes - number
    //!   of bytes to transfer. This should be transferLengthBlocks * sector size. If PI, add 8.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_6(tDevice* device,
                                                    uint32_t logicalBlockAddress,
                                                    uint8_t  transferLengthBlocks,
                                                    uint8_t* ptrData,
                                                    uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_10()
    //
    //! \brief   Description:  Send a SCSI Read 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param rdProtect - only bits 2:0 are valid
    //!   \param dpo - set the dpo bit
    //!   \param fua - set the fua bit
    //!   \param rarc -set the rarc bit
    //!   \param logicalBlockAddress -
    //!   \param groupNumber -
    //!   \param transferLengthBlocks - number of logical blocks to transfer
    //!   \param ptrData - pointer to the data buffer to fill. This MUST be at least (transferLength *
    //!   device.driveInfo.deviceBlockSize) in size or you will have memory errors \param transferLengthBytes - number
    //!   of bytes to transfer. This should be transferLengthBlocks * sector size. If PI, add 8.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 10)
    M_PARAM_WO_SIZE(9, 10)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_10(tDevice* device,
                                                     uint8_t  rdProtect,
                                                     bool     dpo,
                                                     bool     fua,
                                                     bool     rarc,
                                                     uint32_t logicalBlockAddress,
                                                     uint8_t  groupNumber,
                                                     uint16_t transferLengthBlocks,
                                                     uint8_t* ptrData,
                                                     uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_12()
    //
    //! \brief   Description:  Send a SCSI Read 12 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param rdProtect - only bits 2:0 are valid
    //!   \param dpo - set the dpo bit
    //!   \param fua - set the fua bit
    //!   \param rarc -set the rarc bit
    //!   \param logicalBlockAddress -
    //!   \param groupNumber -
    //!   \param transferLengthBlocks - number of logical blocks to transfer
    //!   \param ptrData - pointer to the data buffer to fill. This MUST be at least (transferLength *
    //!   device.driveInfo.deviceBlockSize) in size or you will have memory errors \param transferLengthBytes - number
    //!   of bytes to transfer. This should be transferLengthBlocks * sector size. If PI, add 8.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 10)
    M_PARAM_WO_SIZE(9, 10)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_12(tDevice* device,
                                                     uint8_t  rdProtect,
                                                     bool     dpo,
                                                     bool     fua,
                                                     bool     rarc,
                                                     uint32_t logicalBlockAddress,
                                                     uint8_t  groupNumber,
                                                     uint32_t transferLengthBlocks,
                                                     uint8_t* ptrData,
                                                     uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_16()
    //
    //! \brief   Description:  Send a SCSI Read 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param rdProtect - only bits 2:0 are valid
    //!   \param dpo - set the dpo bit
    //!   \param fua - set the fua bit
    //!   \param rarc -set the rarc bit
    //!   \param logicalBlockAddress -
    //!   \param groupNumber -
    //!   \param transferLengthBlocks - number of logical blocks to transfer
    //!   \param ptrData - pointer to the data buffer to fill. This MUST be at least (transferLength *
    //!   device.driveInfo.deviceBlockSize) in size or you will have memory errors \param transferLengthBytes - number
    //!   of bytes to transfer. This should be transferLengthBlocks * sector size. If PI, add 8.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 10)
    M_PARAM_WO_SIZE(9, 10)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_16(tDevice* device,
                                                     uint8_t  rdProtect,
                                                     bool     dpo,
                                                     bool     fua,
                                                     bool     rarc,
                                                     uint64_t logicalBlockAddress,
                                                     uint8_t  groupNumber,
                                                     uint32_t transferLengthBlocks,
                                                     uint8_t* ptrData,
                                                     uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_32()
    //
    //! \brief   Description:  Send a SCSI Read 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param rdProtect - only bits 2:0 are valid
    //!   \param dpo - set the dpo bit
    //!   \param fua - set the fua bit
    //!   \param rarc -set the rarc bit
    //!   \param logicalBlockAddress -
    //!   \param groupNumber -
    //!   \param transferLengthBlocks - number of logical blocks to transfer
    //!   \param ptrData - pointer to the data buffer to fill. This MUST be at least (transferLengthBlocks *
    //!   device.driveInfo.deviceBlockSize) in size or you will have memory errors \param
    //!   expectedInitialLogicalBlockRefTag - \param expectedLogicalBlockAppTag - \param logicalBlockAppTagMask - \param
    //!   transferLengthBytes - number of bytes to transfer. This should be transferLengthBlocks * sector size. If PI,
    //!   add 8.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 13)
    M_PARAM_WO_SIZE(9, 13)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_32(tDevice* device,
                                                     uint8_t  rdProtect,
                                                     bool     dpo,
                                                     bool     fua,
                                                     bool     rarc,
                                                     uint64_t logicalBlockAddress,
                                                     uint8_t  groupNumber,
                                                     uint32_t transferLengthBlocks,
                                                     uint8_t* ptrData,
                                                     uint32_t expectedInitialLogicalBlockRefTag,
                                                     uint16_t expectedLogicalBlockAppTag,
                                                     uint16_t logicalBlockAppTagMask,
                                                     uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_defect_data_10()
    //
    //! \brief   Description:  Send a SCSI Read Defect Data 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param requestPList - set to true to set the request PList Bit
    //!   \param requestGList - set to true to set the request GList Bit
    //!   \param defectListFormat - format of the defect list. Only bits 2:0 are valid
    //!   \param allocationLength - size of the data buffer and the amount of data to be returned from the device
    //!   \param ptrData - pointer to the data buffer to fill. This MUST be at least (transferLength *
    //!   device.driveInfo.deviceBlockSize) in size or you will have memory errors
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_WO_SIZE(6, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Defect_Data_10(tDevice* device,
                                                                 bool     requestPList,
                                                                 bool     requestGList,
                                                                 uint8_t  defectListFormat,
                                                                 uint16_t allocationLength,
                                                                 uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_defect_data_12()
    //
    //! \brief   Description:  Send a SCSI Read Defect Data 12 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param requestPList - set to true to set the request PList Bit
    //!   \param requestGList - set to true to set the request GList Bit
    //!   \param defectListFormat - format of the defect list. Only bits 2:0 are valid
    //!   \param addressDescriptorIndex - specifies the index of the first address descriptor for the device to return
    //!   \param allocationLength - size of the data buffer and the amount of data to be returned from the device
    //!   \param ptrData - pointer to the data buffer to fill.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_WO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Defect_Data_12(tDevice* device,
                                                                 bool     requestPList,
                                                                 bool     requestGList,
                                                                 uint8_t  defectListFormat,
                                                                 uint32_t addressDescriptorIndex,
                                                                 uint32_t allocationLength,
                                                                 uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_long_10()
    //
    //! \brief   Description:  Send a SCSI Read Long 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param physicalBlock - set the physical block bit (only valid if device supports multiple logical sectors per
    //!   physical sector) \param correctBit - set to correct bit (device will do error correction to retrieve the data,
    //!   but if that fails this command will abort) \param logicalBlockAddress - The logical block address to read
    //!   \param byteTransferLength - this MUST be set to the logical block size. If the physicalBlock bit is set, this
    //!   must be set to the size of the physical block \param ptrData - pointer to the data buffer to fill.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_WO_SIZE(6, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Long_10(tDevice* device,
                                                          bool     physicalBlock,
                                                          bool     correctBit,
                                                          uint32_t logicalBlockAddress,
                                                          uint16_t byteTransferLength,
                                                          uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_read_long_16()
    //
    //! \brief   Description:  Send a SCSI Read Long 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param physicalBlock - set the physical block bit (only valid if device supports multiple logical sectors per
    //!   physical sector) \param correctBit - set to correct bit (device will do error correction to retrieve the data,
    //!   but if that fails this command will abort) \param logicalBlockAddress - The logical block address to read
    //!   \param byteTransferLength - this MUST be set to the logical block size. If the physicalBlock bit is set, this
    //!   must be set to the size of the physical block \param ptrData - pointer to the data buffer to fill.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_WO_SIZE(6, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Read_Long_16(tDevice* device,
                                                          bool     physicalBlock,
                                                          bool     correctBit,
                                                          uint64_t logicalBlockAddress,
                                                          uint16_t byteTransferLength,
                                                          uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_reassign_blocks()
    //
    //! \brief   Description:  Send a SCSI Reassign Blocks command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param longLBA -set the long LBA bit (8 byte LBA vs 4 byte)
    //!   \param longList - set the long list bit (short header or long header used in the buffer)
    //!   \param dataSize - size of the data buffer to transfer
    //!   \param ptrData - pointer to the data buffer to send to the device. This buffer must be properly filled and
    //!   formatted before issuing this command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(5, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    scsi_Reassign_Blocks(tDevice* device, bool longLBA, bool longList, uint32_t dataSize, uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_report_referrals()
    //
    //! \brief   Description:  Send a SCSI Report Referrals command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param logicalBlockAddress -LBA
    //!   \param one_seg - set the one_seg bit
    //!   \param allocationLength - size of the data buffer to transfer
    //!   \param ptrData - pointer to the data buffer to fill upon command completion
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5 -, 3)
    M_PARAM_WO_SIZE(5, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Referrals(tDevice* device,
                                                              uint64_t logicalBlockAddress,
                                                              uint32_t allocationLength,
                                                              bool     one_seg,
                                                              uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Start_Stop_Unit()
    //
    //! \brief   Description:  Send a SCSI Start Stop Unit command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param immediate - set to true to set the immediate bit
    //!   \param powerConditionModifier -
    //!   \param powerCondition -
    //!   \param noFlush -
    //!   \param loej -
    //!   \param start -
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Start_Stop_Unit(tDevice* device,
                                                             bool     immediate,
                                                             uint8_t  powerConditionModifier,
                                                             uint8_t  powerCondition,
                                                             bool     noFlush,
                                                             bool     loej,
                                                             bool     start);

    //-----------------------------------------------------------------------------
    //
    //  scsi_synchronize_cache_10()
    //
    //! \brief   Description:  Send a SCSI Synchronize Cache 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param immediate -set the immediate bit
    //!   \param logicalBlockAddress - the starting logical block address to flush
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param numberOfLogicalBlocks - number of logical blocks to flush (set to 0 to flush all logical blocks
    //!   starting with the LBA specified in the logicalBlockAddress parameter)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Synchronize_Cache_10(tDevice* device,
                                                                  bool     immediate,
                                                                  uint32_t logicalBlockAddress,
                                                                  uint8_t  groupNumber,
                                                                  uint16_t numberOfLogicalBlocks);

    //-----------------------------------------------------------------------------
    //
    //  scsi_synchronize_cache_16()
    //
    //! \brief   Description:  Send a SCSI Synchronize Cache 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param immediate -set the immediate bit
    //!   \param logicalBlockAddress - the starting logical block address to flush
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param numberOfLogicalBlocks - number of logical blocks to flush (set to 0 to flush all logical blocks
    //!   starting with the LBA specified in the logicalBlockAddress parameter)
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Synchronize_Cache_16(tDevice* device,
                                                                  bool     immediate,
                                                                  uint64_t logicalBlockAddress,
                                                                  uint8_t  groupNumber,
                                                                  uint32_t numberOfLogicalBlocks);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Unmap()
    //
    //! \brief   Description:  Send a SCSI Unmap command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param anchor -set the anchor bit
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param parameterListLength - length of the parameter list (and the data buffer being sent to the device)
    //!   \param ptrData - pointer to the data buffer to transfer to the device
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(5, 4)
    M_PARAM_WO_SIZE(5, 4)
    OPENSEA_TRANSPORT_API eReturnValues
    scsi_Unmap(tDevice* device, bool anchor, uint8_t groupNumber, uint16_t parameterListLength, uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_verify_10()
    //
    //! \brief   Description:  Send a SCSI Verify 10 command
    //
    //  Entry:
    //!   \param device - pointer to the tDevice structure
    //!   \param vrprotect - vrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param verificationLength -
    //!   \param ptrData - pointer to the data buffer to transfer to the device. may be M_NULLPTR when byteCheck is set
    //!   to 0 \param dataSize - size of the data Buffer. May be zero when byteCheck is set to 0
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Verify_10(tDevice* device,
                                                       uint8_t  vrprotect,
                                                       bool     dpo,
                                                       uint8_t  byteCheck,
                                                       uint32_t logicalBlockAddress,
                                                       uint8_t  groupNumber,
                                                       uint16_t verificationLength,
                                                       uint8_t* ptrData,
                                                       uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_verify_12()
    //
    //! \brief   Description:  Send a SCSI Verify 12 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param vrprotect - vrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param verificationLength -
    //!   \param ptrData - pointer to the data buffer to transfer to the device. may be M_NULLPTR when byteCheck is set
    //!   to 0 \param dataSize - size of the data Buffer. May be zero when byteCheck is set to 0
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Verify_12(tDevice* device,
                                                       uint8_t  vrprotect,
                                                       bool     dpo,
                                                       uint8_t  byteCheck,
                                                       uint32_t logicalBlockAddress,
                                                       uint8_t  groupNumber,
                                                       uint32_t verificationLength,
                                                       uint8_t* ptrData,
                                                       uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_verify_16()
    //
    //! \brief   Description:  Send a SCSI Verify 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param vrprotect - vrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param verificationLength -
    //!   \param ptrData - pointer to the data buffer to transfer to the device. may be M_NULLPTR when byteCheck is set
    //!   to 0 \param dataSize - size of the data Buffer. May be zero when byteCheck is set to 0
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Verify_16(tDevice* device,
                                                       uint8_t  vrprotect,
                                                       bool     dpo,
                                                       uint8_t  byteCheck,
                                                       uint64_t logicalBlockAddress,
                                                       uint8_t  groupNumber,
                                                       uint32_t verificationLength,
                                                       uint8_t* ptrData,
                                                       uint32_t dataSize);

    //-----------------------------------------------------------------------------
    //
    //  scsi_verify_32()
    //
    //! \brief   Description:  Send a SCSI Verify 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param vrprotect - vrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param verificationLength -
    //!   \param ptrData - pointer to the data buffer to transfer to the device. may be M_NULLPTR when byteCheck is set
    //!   to 0 \param dataSize - size of the data Buffer. May be zero when byteCheck is set to 0 \param
    //!   expectedInitialLogicalBlockRefTag - \param expectedLogicalBlockAppTag - \param logicalBlockAppTagMask -
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Verify_32(tDevice* device,
                                                       uint8_t  vrprotect,
                                                       bool     dpo,
                                                       uint8_t  byteCheck,
                                                       uint64_t logicalBlockAddress,
                                                       uint8_t  groupNumber,
                                                       uint32_t verificationLength,
                                                       uint8_t* ptrData,
                                                       uint32_t dataSize,
                                                       uint32_t expectedInitialLogicalBlockRefTag,
                                                       uint16_t expectedLogicalBlockAppTag,
                                                       uint16_t logicalBlockAppTagMask);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Write_6(tDevice *device, uint32_t logicalBlockAddress, uint8_t transferLengthBlocks, uint8_t* ptrData,
    //  uint32_t transferLengthBytes)
    //
    //! \brief   Description:  Send a SCSI Write 6 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param logicalBlockAddress - LBA to read (21 bits)
    //!   \param transferLengthBlocks - number of blocks to read. zero means 256.
    //!   \param ptrData - pointer to the data buffer to transfer to the device. may be M_NULLPTR when byteCheck is set
    //!   to 0 \param transferLengthBytes - size of the data Buffer in bytes
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 4)
    M_PARAM_RO(1)
    M_PARAM_RO_SIZE(4, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_6(tDevice* device,
                                                     uint32_t logicalBlockAddress,
                                                     uint8_t  transferLengthBlocks,
                                                     uint8_t* ptrData,
                                                     uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_10()
    //
    //! \brief   Description:  Send a SCSI Write 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param fua - set the fua bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write to the device in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param transferLengthBytes - length of the data to write to the device in bytes. May be larger than sectors
    //!   for PI.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_10(tDevice* device,
                                                      uint8_t  wrprotect,
                                                      bool     dpo,
                                                      bool     fua,
                                                      uint32_t logicalBlockAddress,
                                                      uint8_t  groupNumber,
                                                      uint16_t transferLengthBlocks,
                                                      uint8_t* ptrData,
                                                      uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_12()
    //
    //! \brief   Description:  Send a SCSI Write 12 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param fua - set the fua bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write to the device in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param transferLengthBytes - length of the data to write to the device in bytes. May be larger than sectors
    //!   for PI.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_12(tDevice* device,
                                                      uint8_t  wrprotect,
                                                      bool     dpo,
                                                      bool     fua,
                                                      uint32_t logicalBlockAddress,
                                                      uint8_t  groupNumber,
                                                      uint32_t transferLengthBlocks,
                                                      uint8_t* ptrData,
                                                      uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Write_16()
    //
    //! \brief   Description:  Send a SCSI Write 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param fua - set the fua bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write to the device in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param transferLengthBytes - length of the data to write to the device in bytes. May be larger than sectors
    //!   for PI.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_16(tDevice* device,
                                                      uint8_t  wrprotect,
                                                      bool     dpo,
                                                      bool     fua,
                                                      uint64_t logicalBlockAddress,
                                                      uint8_t  groupNumber,
                                                      uint32_t transferLengthBlocks,
                                                      uint8_t* ptrData,
                                                      uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_32()
    //
    //! \brief   Description:  Send a SCSI Write 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param fua - set the fua bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param expectedInitialLogicalBlockRefTag -
    //!   \param expectedLogicalBlockAppTag -
    //!   \param logicalBlockAppTagMask -
    //!   \param transferLengthBytes - length of the data to write to the device in bytes. May be larger than sectors
    //!   for PI.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 12)
    M_PARAM_RO_SIZE(8, 12)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_32(tDevice* device,
                                                      uint8_t  wrprotect,
                                                      bool     dpo,
                                                      bool     fua,
                                                      uint64_t logicalBlockAddress,
                                                      uint8_t  groupNumber,
                                                      uint32_t transferLengthBlocks,
                                                      uint8_t* ptrData,
                                                      uint32_t expectedInitialLogicalBlockRefTag,
                                                      uint16_t expectedLogicalBlockAppTag,
                                                      uint16_t logicalBlockAppTagMask,
                                                      uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_and_verify_10()
    //
    //! \brief   Description:  Send a SCSI Write and Verify 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write to the device in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param transferLengthBytes - number of bytes to transfer with the command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_And_Verify_10(tDevice* device,
                                                                 uint8_t  wrprotect,
                                                                 bool     dpo,
                                                                 uint8_t  byteCheck,
                                                                 uint32_t logicalBlockAddress,
                                                                 uint8_t  groupNumber,
                                                                 uint16_t transferLengthBlocks,
                                                                 uint8_t* ptrData,
                                                                 uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_and_verify_12()
    //
    //! \brief   Description:  Send a SCSI Write and Verify 12 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write to the device in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param transferLengthBytes - number of bytes to transfer with the command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_And_Verify_12(tDevice* device,
                                                                 uint8_t  wrprotect,
                                                                 bool     dpo,
                                                                 uint8_t  byteCheck,
                                                                 uint32_t logicalBlockAddress,
                                                                 uint8_t  groupNumber,
                                                                 uint32_t transferLengthBlocks,
                                                                 uint8_t* ptrData,
                                                                 uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_and_verify_16()
    //
    //! \brief   Description:  Send a SCSI Write and Verify 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write to the device in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param transferLengthBytes - number of bytes to transfer with the command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_And_Verify_16(tDevice* device,
                                                                 uint8_t  wrprotect,
                                                                 bool     dpo,
                                                                 uint8_t  byteCheck,
                                                                 uint64_t logicalBlockAddress,
                                                                 uint8_t  groupNumber,
                                                                 uint32_t transferLengthBlocks,
                                                                 uint8_t* ptrData,
                                                                 uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_and_verify_32()
    //
    //! \brief   Description:  Send a SCSI Write and Verify 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect - wrprotect field. Bits 2:0 are valid
    //!   \param dpo - set the DPO bit
    //!   \param byteCheck - byteCheck field. Bits 1:0 are valid
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the group number. Only bits 4:0 are valid
    //!   \param transferLengthBlocks - length of the data to write in logical blocks
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!   \param expectedInitialLogicalBlockRefTag -
    //!   \param expectedLogicalBlockAppTag -
    //!   \param logicalBlockAppTagMask -
    //!   \param transferLengthBytes - number of bytes to transfer with the command
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 12)
    M_PARAM_RO_SIZE(8, 12)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_And_Verify_32(tDevice* device,
                                                                 uint8_t  wrprotect,
                                                                 bool     dpo,
                                                                 uint8_t  byteCheck,
                                                                 uint64_t logicalBlockAddress,
                                                                 uint8_t  groupNumber,
                                                                 uint32_t transferLengthBlocks,
                                                                 uint8_t* ptrData,
                                                                 uint32_t expectedInitialLogicalBlockRefTag,
                                                                 uint16_t expectedLogicalBlockAppTag,
                                                                 uint16_t logicalBlockAppTagMask,
                                                                 uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_long_10()
    //
    //! \brief   Description:  Send a SCSI Write Long 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param correctionDisabled - set the correction disabled bit
    //!   \param writeUncorrectable - set the write uncorrectable bit
    //!   \param physicalBlock - set the physical block bit
    //!   \param logicalBlockAddress - LBA
    //!   \param byteTransferLength - length of the data to write to the device. Also the length of the ptrData
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_RO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Long_10(tDevice* device,
                                                           bool     correctionDisabled,
                                                           bool     writeUncorrectable,
                                                           bool     physicalBlock,
                                                           uint32_t logicalBlockAddress,
                                                           uint16_t byteTransferLength,
                                                           uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_long_16()
    //
    //! \brief   Description:  Send a SCSI Write Long 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param correctionDisabled - set the correction disabled bit
    //!   \param writeUncorrectable - set the write uncorrectable bit
    //!   \param physicalBlock - set the physical block bit
    //!   \param logicalBlockAddress - LBA
    //!   \param byteTransferLength - length of the data to write to the device. Also the length of the ptrData
    //!   \param ptrData - pointer to the data buffer to transfer to the device.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(7, 6)
    M_PARAM_RO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Long_16(tDevice* device,
                                                           bool     correctionDisabled,
                                                           bool     writeUncorrectable,
                                                           bool     physicalBlock,
                                                           uint64_t logicalBlockAddress,
                                                           uint16_t byteTransferLength,
                                                           uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_same_10()
    //
    //! \brief   Description:  Send a SCSI Write Same 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect -
    //!   \param anchor - set the anchor disabled bit
    //!   \param unmap - set the unmap bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber -
    //!   \param numberOfLogicalBlocks - the number of logical blocks to write data to
    //!   \param ptrData - pointer to the data buffer to transfer to the device. This buffer must be at least the
    //!   logical block size of the device! \param transferLengthBytes - number of bytes to transfer to the drive that
    //!   were allocated by ptrData
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 9)
    M_PARAM_RO_SIZE(8, 9)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Same_10(tDevice* device,
                                                           uint8_t  wrprotect,
                                                           bool     anchor,
                                                           bool     unmap,
                                                           uint32_t logicalBlockAddress,
                                                           uint8_t  groupNumber,
                                                           uint16_t numberOfLogicalBlocks,
                                                           uint8_t* ptrData,
                                                           uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_same_16()
    //
    //! \brief   Description:  Send a SCSI Write Same 16 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect -
    //!   \param anchor - set the anchor disabled bit
    //!   \param unmap - set the unmap bit
    //!   \param noDataOut - set the no data out bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber -
    //!   \param numberOfLogicalBlocks - the number of logical blocks to write data to
    //!   \param ptrData - pointer to the data buffer to transfer to the device. This buffer must be at least the
    //!   logical block size of the device! \param transferLengthBytes - number of bytes to transfer pointed to by
    //!   ptrData
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 10)
    M_PARAM_RO_SIZE(9, 10)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Same_16(tDevice* device,
                                                           uint8_t  wrprotect,
                                                           bool     anchor,
                                                           bool     unmap,
                                                           bool     noDataOut,
                                                           uint64_t logicalBlockAddress,
                                                           uint8_t  groupNumber,
                                                           uint32_t numberOfLogicalBlocks,
                                                           uint8_t* ptrData,
                                                           uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_write_same_32()
    //
    //! \brief   Description:  Send a SCSI Write Same 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param wrprotect -
    //!   \param anchor - set the anchor disabled bit
    //!   \param unmap - set the unmap bit
    //!   \param noDataOut - set the no data out bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber -
    //!   \param numberOfLogicalBlocks - the number of logical blocks to write data to
    //!   \param ptrData - pointer to the data buffer to transfer to the device. This buffer must be at least the
    //!   logical block size of the device! \param expectedInitialLogicalBlockRefTag - \param expectedLogicalBlockAppTag
    //!   - \param logicalBlockAppTagMask - \param transferLengthBytes - number of bytes to transfer pointed to by
    //!   ptrData
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(9, 13)
    M_PARAM_RO_SIZE(9, 13)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Write_Same_32(tDevice* device,
                                                           uint8_t  wrprotect,
                                                           bool     anchor,
                                                           bool     unmap,
                                                           bool     noDataOut,
                                                           uint64_t logicalBlockAddress,
                                                           uint8_t  groupNumber,
                                                           uint32_t numberOfLogicalBlocks,
                                                           uint8_t* ptrData,
                                                           uint32_t expectedInitialLogicalBlockRefTag,
                                                           uint16_t expectedLogicalBlockAppTag,
                                                           uint16_t logicalBlockAppTagMask,
                                                           uint32_t transferLengthBytes);

    //-----------------------------------------------------------------------------
    //
    //  scsi_xp_write_10()
    //
    //! \brief   Description:  Send a SCSI XP Write 10 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param dpo - set the dpo bit
    //!   \param fua - set the fua bit
    //!   \param xoprinfo - set the xorpinfo bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the groupNumber field. only bits 4:0 are valid
    //!   \param transferLength - the length of the data to read/write/transfer. Buffers must be this big
    //!   \param ptrData - pointer to the data out buffer. Must be non-M_NULLPTR
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 7)
    M_PARAM_RO_SIZE(8, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_xp_Write_10(tDevice* device,
                                                         bool     dpo,
                                                         bool     fua,
                                                         bool     xoprinfo,
                                                         uint32_t logicalBlockAddress,
                                                         uint8_t  groupNumber,
                                                         uint16_t transferLength,
                                                         uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_xp_write_32()
    //
    //! \brief   Description:  Send a SCSI XP Write 32 command
    //
    //  Entry:
    //!   \param device - pointer to the device structure
    //!   \param dpo - set the dpo bit
    //!   \param fua - set the fua bit
    //!   \param xoprinfo - set the xorpinfo bit
    //!   \param logicalBlockAddress - LBA
    //!   \param groupNumber - the groupNumber field. only bits 4:0 are valid
    //!   \param transferLength - the length of the data to read/write/transfer. Buffers must be this big
    //!   \param ptrData - pointer to the data out buffer. Must be non-M_NULLPTR
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 7)
    M_PARAM_RO_SIZE(8, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_xp_Write_32(tDevice* device,
                                                         bool     dpo,
                                                         bool     fua,
                                                         bool     xoprinfo,
                                                         uint64_t logicalBlockAddress,
                                                         uint8_t  groupNumber,
                                                         uint32_t transferLength,
                                                         uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  eReturnValues scsi_Zone_Management_In_Report(tDevice* device, eZMAction action, uint8_t actionSpecific1,
    //  uint64_t location, bool partial, uint8_t reportingOptions, uint32_t allocationLength, uint8_t* ptrData)//95h
    //
    //! \brief   Description:  Sends a zone management in command to a device for report zones, report realms, and
    //! report zone domains since they share a mostly common format. Recommend using help functions below
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] action = set this to the zone management action to perform. (enum is in common_public.h)
    //!   \param[in] actionSpecific1 = set the action specific bits in byte 1
    //!   \param[in] location = LBA, zone locator, realm locator, etc
    //!   \param[in] partial = partial bit for report zones. Reserved otherwise
    //!   \param[in] reportingOptions = reporting options. Specific to the type of report being requested.
    //!   \param[in] allocationLength = used on data transfer commands. This is how many bytes to transfer. Should be 0
    //!   for non-data commands \param[out] ptrData = pointer to the data buffer to use. Can be M_NULLPTR for non-data
    //!   actions
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 7)
    M_PARAM_WO_SIZE(8, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Zone_Management_In_Report(tDevice*  device,
                                                                       eZMAction action,
                                                                       uint8_t   actionSpecific1,
                                                                       uint64_t  location,
                                                                       bool      partial,
                                                                       uint8_t   reportingOptions,
                                                                       uint32_t  allocationLength,
                                                                       uint8_t*  ptrData); // 95h

    // Command to help with zone activate and zone query since they have mostly common formatting. Recommend using
    // helper function below
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(8, 7)
    M_PARAM_RO_SIZE(8, 7)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Zone_Management_In_ZD(tDevice*  device,
                                                                   eZMAction action,
                                                                   bool      all,
                                                                   uint64_t  zoneID,
                                                                   uint16_t  numberOfZones,
                                                                   uint8_t   otherZoneDomainID,
                                                                   uint16_t  allocationLength,
                                                                   uint8_t*  ptrData); // 95h

    //-----------------------------------------------------------------------------
    //
    //  scsi_Zone_Management_Out_Std_Format_CDB(tDevice *device, eZMAction action, uint8_t actionSpecific, uint32_t
    //  allocationLength, uint64_t actionSpecificLBA, uint8_t *ptrData)
    //
    //! \brief   Description:  Sends a zone management out command to a device that uses the common format for certain
    //! commands (recommend using helper functions below)
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] action = set this to the zone management action to perform. (enum is in common_public.h)
    //!   \param[in] zoneID = zoneID field
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZBC, use zero. On
    //!   ZBC2 and later values of 0 and 1 mean one zone. \param[in] all = action affects all zones (zone ID and count
    //!   should be zero) \param[in] commandSPecific_10_11 = command specific bytes 10 and 11 \param[in]
    //!   cmdSpecificBits1 = bits 7:5 in byte 1 of the CDB. All other bits will be stripped off. \param[in]
    //!   actionSpecific14 = set the action specific bits. (cdb byte 14) \param[in] allocationLength = used on data
    //!   transfer commands. This is how many bytes to transfer. Should be 0 for non-data commands

    //!   \param[in] ptrData = pointer to the data buffer to use. Can be M_NULLPTR for non-data actions
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Zone_Management_Out_Std_Format_CDB(tDevice*  device,
                                                                                eZMAction action,
                                                                                uint64_t  zoneID,
                                                                                uint16_t  zoneCount,
                                                                                bool      all,
                                                                                uint16_t  commandSPecific_10_11,
                                                                                uint8_t   cmdSpecificBits1,
                                                                                uint8_t   actionSpecific14); // 94h

    //-----------------------------------------------------------------------------
    //
    //  scsi_Close_Zone_Ext(tDevice *device, bool closeAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a close zone command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] closeAll = set the closeAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to close
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZBC, use zero. On
    //!   ZBC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Close_Zone(tDevice* device,
                                                        bool     closeAll,
                                                        uint64_t zoneID,
                                                        uint16_t zoneCount);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Finish_Zone_Ext(tDevice *device, bool finishAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a finish zone command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] finishAll = set the finishAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to finish
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZBC, use zero. On
    //!   ZBC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Finish_Zone(tDevice* device,
                                                         bool     finishAll,
                                                         uint64_t zoneID,
                                                         uint16_t zoneCount);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Open_Zone_Ext(tDevice *device, bool openAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a open zone command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] openAll = set the openAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to open
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZBC, use zero. On
    //!   ZBC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Open_Zone(tDevice* device,
                                                       bool     openAll,
                                                       uint64_t zoneID,
                                                       uint16_t zoneCount);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Sequentialize_Zone(tDevice* device,
                                                                bool     all,
                                                                uint64_t zoneID,
                                                                uint16_t zoneCount);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Report_Zones_Ext(tDevice *device, eZoneReportingOptions reportingOptions, uint32_t allocationLength,
    //  uint64_t zoneLocator, uint8_t *ptrData)
    //
    //! \brief   Description:  Sends a report zones ext command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] reportingOptions = set to the value for the types of zones to be reported. enum is in
    //!   common_public.h \param[in] partial = set the partial bit to 1 \param[in] allocationLength = This is a number
    //!   of bytes to transfer \param[in] zoneStartLBA = zone locator field. Set the an LBA value for the lowest
    //!   reported zone (0 for all zones) \param[out] ptrData = pointer to the data buffer to use. Must be non-M_NULLPTR
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 6)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(6, 4)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Zones(tDevice*              device,
                                                          eZoneReportingOptions reportingOptions,
                                                          bool                  partial,
                                                          uint32_t              allocationLength,
                                                          uint64_t              zoneStartLBA,
                                                          uint8_t*              ptrData);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Realms(tDevice*                device,
                                                           eRealmsReportingOptions reportingOptions,
                                                           uint32_t                allocationLength,
                                                           uint64_t                realmLocator,
                                                           uint8_t*                ptrData);

    M_NONNULL_PARAM_LIST(1, 5)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(5, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Report_Zone_Domains(tDevice*                    device,
                                                                 eZoneDomainReportingOptions reportingOptions,
                                                                 uint32_t                    allocationLength,
                                                                 uint64_t                    zoneDomainLocator,
                                                                 uint8_t*                    ptrData);

    M_NONNULL_PARAM_LIST(1, 7)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Zone_Query(tDevice* device,
                                                        bool     all,
                                                        uint64_t zoneID,
                                                        uint16_t numberOfZones,
                                                        uint8_t  otherZoneDomainID,
                                                        uint16_t allocationLength,
                                                        uint8_t* ptrData);

    M_NONNULL_PARAM_LIST(1, 7)
    M_PARAM_RO(1)
    M_PARAM_WO_SIZE(7, 6)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Zone_Activate(tDevice* device,
                                                           bool     all,
                                                           uint64_t zoneID,
                                                           uint16_t numberOfZones,
                                                           uint8_t  otherZoneDomainID,
                                                           uint16_t allocationLength,
                                                           uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Reset_Write_Pointers_Ext(tDevice *device, bool resetAll, uint64_t zoneID)
    //
    //! \brief   Description:  Sends a reset write pointers command to a device.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] resetAll = set the resetAll bit. If this is true, then the zoneID will be ignored by the device.
    //!   \param[in] zoneID = the zoneID to open
    //!   \param[in] zoneCount = zone count to apply the action to. for backwards compatibiity with ZBC, use zero. On
    //!   ZBC2 and later values of 0 and 1 mean one zone.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Reset_Write_Pointers(tDevice* device,
                                                                  bool     resetAll,
                                                                  uint64_t zoneID,
                                                                  uint16_t zoneCount);

#define MAX_VERSION_DESCRIPTOR_STRING_LENGTH 65
    //-----------------------------------------------------------------------------
    //
    //  decypher_SCSI_Version_Descriptors(uint16_t versionDescriptor, char* versionString)
    //
    //! \brief   Description:  Translates a version descriptor code into a string describing the specification it refers
    //! to. This uses versionDescriptor / 32 to find the specification support. This does not take into account
    //! revisions.
    //
    //  Entry:
    //!   \param[in] versionDescriptor = word value that is a version descriptor from the standard inquiry data.
    //!   \param[out] versionString = pointer to a char array that will hold a string describing the version. This
    //!   should be MAX_VERSION_DESCRIPTOR_STRING_LENGTH in size or larger
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(2)
    M_PARAM_WO(2)
    OPENSEA_TRANSPORT_API void decypher_SCSI_Version_Descriptors(uint16_t versionDescriptor, char* versionString);

    //-----------------------------------------------------------------------------
    //
    //  is_Standard_Supported(uint16_t versionDescriptor, eStandardCode standardCode)
    //
    //! \brief   Description:  Checks if the provided version descriptor is for the standardCode provided. This is
    //! checked by doing versionDescriptor / 32. Standard codes are from SPC specification.
    //
    //  Entry:
    //!   \param[in] versionDescriptor = word value that is a version descriptor from the standard inquiry data.
    //!   \param[in] standardCode = enum type or int type that matches a standard code from SPC spec.
    //!
    //  Exit:
    //!   \return true = version descriptor is for standard code provided, false = version descriptor does not match
    //!   standard code.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Standard_Supported(uint16_t versionDescriptor, eStandardCode standardCode);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Get_Physical_Element_Status(tDevice *device, uint32_t startingElement, uint32_t allocationLength, uint8_t
    //  filter, uint8_t reportType, uint8_t *ptrData)
    //
    //! \brief   Description:  Sends the SCSI Get Physical Element Status command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] startingElement = element to start requesting from
    //!   \param[in] allocationLength = amount of data allocated for retrieved data
    //!   \param[in] filter = filter type for command output. NOTE: Bits 0:1 are valid
    //!   \param[in] reportType = report type filter. NOTE: BITS 0:3 are valid
    //!   \param[in] ptrData = pointer to data buffer to fill with command result
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 3)
    M_PARAM_WO_SIZE(6, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Get_Physical_Element_Status(tDevice* device,
                                                                         uint32_t startingElement,
                                                                         uint32_t allocationLength,
                                                                         uint8_t  filter,
                                                                         uint8_t  reportType,
                                                                         uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Remove_And_Truncate(tDevice *device, uint64_t requestedCapacity, uint32_t elementIdentifier)
    //
    //! \brief   Description:  Sends the SCSI Remove and Truncate command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] requestedCapacity = requested new native/accessible max capacity. Can be left as zero for drive to
    //!   make this call \param[in] elementIdentifier = identifier of the element to truncate
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Remove_And_Truncate(tDevice* device,
                                                                 uint64_t requestedCapacity,
                                                                 uint32_t elementIdentifier);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Remove_Element_And_Modify_Zones(tDevice* device,
                                                                             uint32_t elementIdentifier);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Restore_Elements_And_Rebuild(tDevice *device)
    //
    //! \brief   Description:  Sends the SCSI Restore Elements and Rebuild command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues scsi_Restore_Elements_And_Rebuild(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Persistent_Reserve_In(tDevice *device, uint8_t serviceAction, uint16_t allocationLength, uint8_t *ptrData)
    //
    //! \brief   Description:  Sends the SCSI Persistent reserve in command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] serviceAction = persistent reserve action to perform
    //!   \param[in] allocationLength = length of any data associated with the service action to receive from the device
    //!   \param[in] ptrData = pointer to data buffer to issue to the device, if any.
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(4, 3)
    M_PARAM_WO_SIZE(4, 3)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Persistent_Reserve_In(tDevice* device,
                                                                   uint8_t  serviceAction,
                                                                   uint16_t allocationLength,
                                                                   uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Persistent_Reserve_Out(tDevice *device, uint8_t serviceAction, uint8_t scope, uint8_t type,  uint32_t
    //  parameterListLength, uint8_t *ptrData)
    //
    //! \brief   Description:  Sends the SCSI Persistent reserve out command
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!   \param[in] serviceAction = persistent reserve action to perform
    //!   \param[in] scope = scope of the service action
    //!   \param[in] type =
    //!   \param[in] parameterListLength = length of any data associated with the service action to send to the device
    //!   \param[in] ptrData = pointer to data buffer to issue to the device, if any.
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1)
    M_NONNULL_IF_NONZERO_PARAM(6, 5)
    M_PARAM_RO_SIZE(6, 5)
    OPENSEA_TRANSPORT_API eReturnValues scsi_Persistent_Reserve_Out(tDevice* device,
                                                                    uint8_t  serviceAction,
                                                                    uint8_t  scope,
                                                                    uint8_t  type,
                                                                    uint32_t parameterListLength,
                                                                    uint8_t* ptrData);

    //-----------------------------------------------------------------------------
    //
    //  scsi_Rezero_Unit(tDevice* device)
    //
    //! \brief   Description:  Sends the SCSI Rezero Unit command. SCSI 2 mentions that this returns the unit to a good
    //! state.
    //
    //  Entry:
    //!   \param[in] device = pointer to device structure
    //!
    //  Exit:
    //!   \return SUCCESS = pass, !SUCCESS = something when wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eReturnValues scsi_Rezero_Unit(tDevice* device);

#if defined(__cplusplus)
}
#endif
