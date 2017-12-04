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
// 

#include "common.h"
#include "common_public.h"
#include "ata_helper.h"
#include "ata_helper_func.h"

bool is_Buffer_Non_Zero(uint8_t* ptrData, uint32_t dataLen)
{
    bool isNonZero = false;
    for (uint32_t iter = 0; iter < dataLen; ++iter)
    {
        if (ptrData[iter] != 0)
        {
            isNonZero = true;
            break;
        }
    }
    return isNonZero;
}

int fill_In_ATA_Drive_Info(tDevice *device)
{
    int ret = UNKNOWN;
    //Both pointers pointing to the same data. 
    uint16_t *ident_word = &device->drive_info.IdentifyData.ata.Word000;
    uint8_t *identifyData = (uint8_t *)&device->drive_info.IdentifyData.ata.Word000;
#ifdef _DEBUG
    printf("%s -->\n",__FUNCTION__);
#endif

    bool retrievedIdentifyData = false;
    //try an identify command, then also try an identify packet device command. The data we care about parsing will be in the same location so everything inside this if should work as expected
    if ((SUCCESS == ata_Identify(device, (uint8_t *)ident_word, sizeof(tAtaIdentifyData)) && is_Buffer_Non_Zero((uint8_t*)ident_word, 512)) || (SUCCESS == ata_Identify_Packet_Device(device, (uint8_t *)ident_word, sizeof(tAtaIdentifyData)) && is_Buffer_Non_Zero((uint8_t*)ident_word, 512)))
    {
        retrievedIdentifyData = true;
    }
    else
    {
        //we didn't get anything...yet.
        //We are probably using 16byte cdbs already, if we aren't, then we are done, otherwise we need to try changing to 12byte CDBs for compatibility with some SATLs
        if (!device->drive_info.ata_Options.use12ByteSATCDBs && device->drive_info.ata_Options.passthroughType == ATA_PASSTHROUGH_SAT)
        {
            //we aren't trying 12 byte...we should try it...BUT if we suspect that this is an ATAPI drive, we should NOT. This is because ATAPI uses the same opcode for the "blank"
            //command. Since these are the same, the SATL may not filter it properly and we may issue this command instead. Since I don't know what this does, let's avoid that if possible. - TJE
            //Filter out ATAPI_DRIVE, TAPE_DRIVE, MEDIA_OPTICAL, & MEDIA_TAPE to be safe...this should be pretty good. -TJE
            if (!(device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.drive_type == TAPE_DRIVE
                || device->drive_info.media_type == MEDIA_OPTICAL || device->drive_info.media_type == MEDIA_TAPE))
            {
                device->drive_info.ata_Options.use12ByteSATCDBs = true;
                memset(identifyData, 0, 512);
                //send check power mode to help clear out any stale RTFRs or sense data from the drive...needed by some devices. Won't hurt other devices.
                uint8_t mode = 0;
                ata_Check_Power_Mode(device, &mode);
                if ((SUCCESS == ata_Identify(device, (uint8_t *)ident_word, sizeof(tAtaIdentifyData)) && is_Buffer_Non_Zero((uint8_t*)ident_word, 512)) || (SUCCESS == ata_Identify_Packet_Device(device, (uint8_t *)ident_word, sizeof(tAtaIdentifyData)) && is_Buffer_Non_Zero((uint8_t*)ident_word, 512)))
                {
                    retrievedIdentifyData = true;
                }

            }
        }
        /*else
        {
            printf("Already using 12byte\n");
        }*/
    }
    if (retrievedIdentifyData)
    {
        //print_Data_Buffer((uint8_t*)ident_word, 512, true);
        ret = SUCCESS;
        if (ident_word[0] & BIT15)
        {
            device->drive_info.drive_type = ATAPI_DRIVE;
            device->drive_info.media_type = MEDIA_OPTICAL;
        }
        else
        {
            device->drive_info.drive_type = ATA_DRIVE;
        }
        if (device->drive_info.IdentifyData.ata.Word217 == 0x0001) //Nominal media rotation rate.
        {
            device->drive_info.media_type = MEDIA_SSD;
        }

        //set some pointers to where we want to fill in information...we're doing this so that on USB, we can store some info about the child drive, without disrupting the standard drive_info that has already been filled in by the fill_SCSI_Info function
        char *fillModelNumber = device->drive_info.product_identification;
        char *fillSerialNumber = device->drive_info.serialNumber;
        char *fillFWRev = device->drive_info.product_revision;
        uint64_t *fillWWN = &device->drive_info.worldWideName;
        uint32_t *fillLogicalSectorSize = &device->drive_info.deviceBlockSize;
        uint32_t *fillPhysicalSectorSize = &device->drive_info.devicePhyBlockSize;
        uint16_t *fillSectorAlignment = &device->drive_info.sectorAlignment;
        uint64_t *fillMaxLba = &device->drive_info.deviceMaxLba;

        //IDE interface means we're connected to a native SATA/PATA inferface so we leave the default pointer alone and don't touch the drive info that was filled in by the scsi commands since that is how the OS talks to it for read/write and we don't want to disrupt that
        //Everything else is some sort of SAT interface (UDS, SAS, IEEE1394, etc) so we want to fill in bridge info here
        if ((device->drive_info.interface_type != IDE_INTERFACE) && (device->drive_info.interface_type != RAID_INTERFACE))
        {
            device->drive_info.bridge_info.isValid = true;
            fillModelNumber = device->drive_info.bridge_info.childDriveMN;
            fillSerialNumber = device->drive_info.bridge_info.childDriveSN;
            fillFWRev = device->drive_info.bridge_info.childDriveFW;
            fillWWN = &device->drive_info.bridge_info.childWWN;
            fillLogicalSectorSize = &device->drive_info.bridge_info.childDeviceBlockSize;
            fillPhysicalSectorSize = &device->drive_info.bridge_info.childDevicePhyBlockSize;
            fillSectorAlignment = &device->drive_info.bridge_info.childSectorAlignment;
            fillMaxLba = &device->drive_info.bridge_info.childDeviceMaxLba;
        }
        //this will catch all IDE interface devices to set a vendor identification IF one is not already set
        else if (strlen(device->drive_info.T10_vendor_ident) == 0)
        {
            //vendor ID is not set, so set it to ATA like SAT spec
            device->drive_info.T10_vendor_ident[0] = 'A';
            device->drive_info.T10_vendor_ident[1] = 'T';
            device->drive_info.T10_vendor_ident[2] = 'A';
            device->drive_info.T10_vendor_ident[3] = 0;
            device->drive_info.T10_vendor_ident[4] = 0;
            device->drive_info.T10_vendor_ident[5] = 0;
            device->drive_info.T10_vendor_ident[6] = 0;
            device->drive_info.T10_vendor_ident[7] = 0;
        }
        memcpy(fillModelNumber, &ident_word[27], MODEL_NUM_LEN);
        fillModelNumber[MODEL_NUM_LEN] = '\0';
        memcpy(fillSerialNumber, &ident_word[10], SERIAL_NUM_LEN);
        fillSerialNumber[SERIAL_NUM_LEN] = '\0';
        memcpy(fillFWRev, &ident_word[23], 8);
        fillFWRev[FW_REV_LEN] = '\0';
        //Byte swap due to endianess
        byte_Swap_String(fillModelNumber);
        byte_Swap_String(fillSerialNumber);
        byte_Swap_String(fillFWRev);
        //remove leading and trailing whitespace
        remove_Leading_And_Trailing_Whitespace(fillModelNumber);
        remove_Leading_And_Trailing_Whitespace(fillSerialNumber);
        remove_Leading_And_Trailing_Whitespace(fillFWRev);
        //get the WWN
        *fillWWN = M_WordsTo8ByteValue(device->drive_info.IdentifyData.ata.Word108,\
                                       device->drive_info.IdentifyData.ata.Word109,\
                                       device->drive_info.IdentifyData.ata.Word110,\
                                       device->drive_info.IdentifyData.ata.Word111);

        //get the sector sizes from the identify data
        if (((ident_word[106] & BIT14) == BIT14) && ((ident_word[106] & BIT15) == 0)) //making sure this word has valid data
        {
            //word 117 is only valid when word 106 bit 12 is set
            if ((ident_word[106] & BIT12) == BIT12)
            {
                *fillLogicalSectorSize = ident_word[117] | ((uint32_t)ident_word[118] << 16);
                *fillLogicalSectorSize *= 2; //convert to words to bytes
            }
            else //means that logical sector size is 512bytes
            {
                *fillLogicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
            }
            if ((ident_word[106] & BIT13) == 0)
            {
                *fillPhysicalSectorSize = device->drive_info.deviceBlockSize;
            }
            else //multiple logical sectors per physical sector
            {
                uint8_t logicalPerPhysical = 1;
                uint8_t sectorSizeExponent = 0;
                //get the number of logical blocks per physical blocks
                sectorSizeExponent = ident_word[106] & 0x000F;
                if (sectorSizeExponent != 0)
                {
                    uint8_t shiftCounter = 0;
                    while (shiftCounter < sectorSizeExponent)
                    {
                        logicalPerPhysical = logicalPerPhysical << 1; //multiply by 2
                        shiftCounter++;
                    }
                    *fillPhysicalSectorSize = *fillLogicalSectorSize * logicalPerPhysical;
                }
            }
        }
        else
        {
            *fillLogicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
            *fillPhysicalSectorSize = LEGACY_DRIVE_SEC_SIZE;
        }
        //get the sector alignment
        if (ident_word[209] & BIT14)
        {
            //bits 13:0 are valid for alignment. bit 15 will be 0 and bit 14 will be 1. remove bit 14 with an xor
            *fillSectorAlignment = ident_word[209] ^ BIT14;
        }

        //maxLBA
        if (ident_word[83] & BIT10)
        {
            //acs4 - word 69 bit3 means extended number of user addressable sectors word supported (words 230 - 233) (Use this to get the max LBA since words 100 - 103 may only contain a value of FFFF_FFFF)
            if (ident_word[69] & BIT3)
            {
                *fillMaxLba = M_BytesTo8ByteValue(identifyData[467], identifyData[466], identifyData[465], identifyData[464], identifyData[463], identifyData[462], identifyData[461], identifyData[460]);
            }
            else
            {
                *fillMaxLba = M_BytesTo8ByteValue(identifyData[207], identifyData[206], identifyData[205], identifyData[204], identifyData[203], identifyData[202], identifyData[201], identifyData[200]);
            }
        }
        else
        {
            *fillMaxLba = M_BytesTo4ByteValue(identifyData[123], identifyData[122], identifyData[121], identifyData[120]);
        }
        *fillMaxLba -= 1;

        //Now determine if the drive supports DMA and which DMA modes it supports
        if (ident_word[49] & BIT8)
        {
            device->drive_info.ata_Options.dmaSupported = true;
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
        }
        //obsolete since ATA3, holds single word DMA support
        if (ident_word[62])
        {
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
        }
        //check for multiword dma support
        if (ident_word[63] & (BIT0 | BIT1 | BIT2))
        {
            device->drive_info.ata_Options.dmaSupported = true;
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_MWDMA;
        }
        //check for UDMA support
        if (ident_word[88] & 0x007F)
        {
            device->drive_info.ata_Options.dmaSupported = true;
            device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_UDMA;
        }

        //set read/write buffer DMA
        if (ident_word[69] & BIT11)
        {
            device->drive_info.ata_Options.readBufferDMASupported = true;
        }
        if (ident_word[69] & BIT10)
        {
            device->drive_info.ata_Options.writeBufferDMASupported = true;
        }
        //set download microcode DMA support
        if (ident_word[69] & BIT8)
        {
            device->drive_info.ata_Options.downloadMicrocodeDMASupported = true;
        }
        //set zoned device type
        switch (ident_word[69] & (BIT0 | BIT1))
        {
        case 0:
            device->drive_info.zonedType = ZONED_TYPE_NOT_ZONED;
            break;
        case 1:
            device->drive_info.zonedType = ZONED_TYPE_HOST_AWARE;
            break;
        case 2:
            device->drive_info.zonedType = ZONED_TYPE_DEVICE_MANAGED;
            break;
        case 3:
            device->drive_info.zonedType = ZONED_TYPE_RESERVED;
            break;
        default:
            break;
        }
        //Determine if read/write log ext DMA commands are supported
        if (ident_word[119] & BIT3 || ident_word[120] & BIT3)
        {
            device->drive_info.ata_Options.readLogWriteLogDMASupported = true;
        }
        //set the number of logical sectors per DRQ data block (current setting)
        device->drive_info.ata_Options.logicalSectorsPerDRQDataBlock = M_Byte0(ident_word[59]);
        //check for tagged command queuing support
        if (ident_word[83] & BIT1 || ident_word[86] & BIT1)
        {
            device->drive_info.ata_Options.taggedCommandQueuingSupported = true;
        }
        //check for native command queuing support
        if (ident_word[76] & BIT8)
        {
            device->drive_info.ata_Options.nativeCommandQueuingSupported = true;
        }
        //check if the device is parallel or serial
        uint8_t transportType = (ident_word[222] & (BIT15 | BIT14 | BIT13 | BIT12)) >> 12;
        switch (transportType)
        {
        case 0x00://parallel
            device->drive_info.ata_Options.isParallelTransport = true;
            break;
        case 0x01://serial
        case 0x0E://PCIe
        default:
            break;
        }
        if (device->drive_info.IdentifyData.ata.Word076 > 0)//Only Serial ATA Devices will set the bits in words 76-79
        {
            device->drive_info.ata_Options.isParallelTransport = false;
        }
        if (ident_word[119] & BIT2 || ident_word[120] & BIT2)
        {
            device->drive_info.ata_Options.writeUncorrectableExtSupported = true;
        }
        if (ident_word[120] & BIT6)//word120 holds if this is enabled
        {
            device->drive_info.ata_Options.senseDataReportingEnabled = true;
        }
        //check that 48bit is supported
        if (ident_word[83] & BIT10)
        {
            device->drive_info.ata_Options.fourtyEightBitAddressFeatureSetSupported = true;
        }
        //GPL support
        if (ident_word[84] & BIT5 || ident_word[87] & BIT5)
        {
            device->drive_info.ata_Options.generalPurposeLoggingSupported = true;
        }

        if (device->drive_info.interface_type == SCSI_INTERFACE)
        {
            //for the SCSI interface, copy this information back to the main drive info since SCSI translated info may truncate these fields and we don't want that
            memcpy(device->drive_info.product_identification, device->drive_info.bridge_info.childDriveMN, MODEL_NUM_LEN);
            memcpy(device->drive_info.serialNumber, device->drive_info.bridge_info.childDriveSN, SERIAL_NUM_LEN);
            memcpy(device->drive_info.product_revision, device->drive_info.bridge_info.childDriveFW, FW_REV_LEN);
            device->drive_info.worldWideName = device->drive_info.bridge_info.childWWN;
        }
        //if parallel ATA, check the current mode. If not DMA, turn off ALL DMA command support since the HBA or OS or bridge may not support DMA mode and we don't want to lose communication with the host
        if (ident_word[76] == 0 || ident_word[76] == UINT16_MAX)//Check for parallel ATA.
        {
            //now check if any DMA mode is enabled...if none are enabled, then it's running in PIO mode
            if (!(M_GETBITRANGE(ident_word[62], 10, 8) != 0 //SWDMA
                || M_GETBITRANGE(ident_word[63], 10, 8) != 0 //MWDMA
                || M_GETBITRANGE(ident_word[88], 14, 8) != 0 //UDMA
                ))
            {
                //in this case, remove all the support flags for DMA versions of commands since that is the easiest way to handle the rest of the library
                device->drive_info.ata_Options.dmaSupported = false;
                device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_NO_DMA;
                device->drive_info.ata_Options.downloadMicrocodeDMASupported = false;
                device->drive_info.ata_Options.readBufferDMASupported = false;
                device->drive_info.ata_Options.readLogWriteLogDMASupported = false;
                device->drive_info.ata_Options.writeBufferDMASupported = false;
            }
        }
    }
    else
    {
        ret = FAILURE;
    }
    //This may not be required...need to do some testing to see if reading the logs below wakes a drive up - TJE
    if (M_Word0(device->dFlags) == DO_NOT_WAKE_DRIVE || M_Word0(device->dFlags) == FAST_SCAN)
    {
#ifdef _DEBUG
        printf("Quiting device discovery early for %s per DO_NOT_WAKE_DRIVE\n", device->drive_info.serialNumber);
    	printf("Drive type: %d\n",device->drive_info.drive_type);
    	printf("Interface type: %d\n",device->drive_info.interface_type);
    	printf("Media type: %d\n",device->drive_info.media_type);
        printf("SN: %s\n",device->drive_info.serialNumber);
        printf("%s <--\n",__FUNCTION__);
#endif
        return ret;
    }

    //Check if we were given any force flags regarding how we talk to ATA drives.
    if ((device->dFlags & FORCE_ATA_PIO_ONLY) != 0)
    {
        //in this case, remove all the support flags for DMA versions of commands since that is the easiest way to handle the rest of the library
        device->drive_info.ata_Options.dmaSupported = false;
        device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_NO_DMA;
        device->drive_info.ata_Options.downloadMicrocodeDMASupported = false;
        device->drive_info.ata_Options.readBufferDMASupported = false;
        device->drive_info.ata_Options.readLogWriteLogDMASupported = false;
        device->drive_info.ata_Options.writeBufferDMASupported = false;
    }
    //check if we're being asked to set the protocol to DMA for DMA commands (default behavior depends on drive support from identify)
    if ((device->dFlags & FORCE_ATA_DMA_SAT_MODE) != 0)
    {
        device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_DMA;
    }
    //check if we're being asked to set the protocol to UDMA for DMA commands (default behavior depends on drive support from identify)
    if ((device->dFlags & FORCE_ATA_UDMA_SAT_MODE) != 0)
    {
        device->drive_info.ata_Options.dmaMode = ATA_DMA_MODE_UDMA;
    }

    //device->drive_info.softSATFlags.senseDataDescriptorFormat = true;//by default software SAT will set this to descriptor format so that ATA pass-through works as expected with RTFRs.
    //only bother reading logs if GPL is supported...not going to bother with SMART even though some of the things we are looking for are in SMART - TJE
    if (retrievedIdentifyData && device->drive_info.ata_Options.generalPurposeLoggingSupported)
    {
        uint8_t logBuffer[LEGACY_DRIVE_SEC_SIZE] = { 0 };
        if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_DIRECTORY, 0, logBuffer, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
        {
            bool readIDDataLog = false;
            bool readDeviceStatisticsLog = false;
            //check for support of ID Data Log, Current Device Internal Status, Saved Device Internal Status, Device Statistics Log
            if (M_BytesTo2ByteValue(logBuffer[(ATA_LOG_DEVICE_STATISTICS * 2) + 1], logBuffer[(ATA_LOG_DEVICE_STATISTICS * 2)]) > 0)
            {
                readDeviceStatisticsLog = true;
            }
            if (M_BytesTo2ByteValue(logBuffer[(ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG * 2) + 1], logBuffer[(ATA_LOG_CURRENT_DEVICE_INTERNAL_STATUS_DATA_LOG * 2)]) > 0)
            {
                device->drive_info.softSATFlags.currentInternalStatusLogSupported = true;
            }
            if (M_BytesTo2ByteValue(logBuffer[(ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG * 2) + 1], logBuffer[(ATA_LOG_SAVED_DEVICE_INTERNAL_STATUS_DATA_LOG * 2)]) > 0)
            {
                device->drive_info.softSATFlags.savedInternalStatusLogSupported = true;
            }
            if (M_BytesTo2ByteValue(logBuffer[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2) + 1], logBuffer[(ATA_LOG_IDENTIFY_DEVICE_DATA * 2)]) > 0)
            {
                readIDDataLog = true;
            }
            //could check any log from address 80h through 9Fh...but one should be enough (used for SAT application client log page translation)
            //Using 90h since that is the first page the application client translation uses.
            if (M_BytesTo2ByteValue(logBuffer[(0x90 * 2) + 1], logBuffer[(0x90 * 2)]) > 0)
            {
                device->drive_info.softSATFlags.hostLogsSupported = true;
            }
            //now read the couple pages of logs we care about to set some more flags for software SAT
            if (readIDDataLog)
            {
                memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES, logBuffer, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                {
                    uint64_t *qwordptr = (uint64_t*)&logBuffer[0];
                    if (qwordptr[0] & BIT63 && M_Byte2(qwordptr[0]) == ATA_ID_DATA_LOG_SUPPORTED_CAPABILITIES)
                    {
                        if (qwordptr[2] & BIT63 && qwordptr[2] & BIT34)
                        {
                            device->drive_info.softSATFlags.deferredDownloadSupported = true;
                        }
                    }
                }
                memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_IDENTIFY_DEVICE_DATA, ATA_ID_DATA_LOG_ZONED_DEVICE_INFORMATION, logBuffer, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                {
                    //according to what I can find in the spec, a HOST Managed drive reports a different signature, but doens't set any identify bytes like a host aware drive.
                    //because of this and not being able to get the real signature, this check is the only way to determine we are talking to an ATA host managed drive. - TJE
                    if (device->drive_info.zonedType == ZONED_TYPE_NOT_ZONED)
                    {
                        device->drive_info.zonedType = ZONED_TYPE_HOST_MANAGED;
                    }
                }
            }
            if (readDeviceStatisticsLog)
            {
                memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
                if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_LIST, logBuffer, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                {
                    uint16_t iter = 9;
                    uint8_t numberOfEntries = logBuffer[8];
                    for (iter = 9; iter < (numberOfEntries + 9); iter++)
                    {
                        switch (logBuffer[iter])
                        {
                        case ATA_DEVICE_STATS_LOG_LIST:
                            break;
                        case ATA_DEVICE_STATS_LOG_GENERAL:
                            device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_FREE_FALL:
                            break;
                        case ATA_DEVICE_STATS_LOG_ROTATING_MEDIA:
                            device->drive_info.softSATFlags.deviceStatsPages.rotatingMediaStatisticsPageSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_GEN_ERR:
                            device->drive_info.softSATFlags.deviceStatsPages.generalErrorStatisticsSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_TEMP:
                            device->drive_info.softSATFlags.deviceStatsPages.temperatureStatisticsSupported = true;
                            break;
                        case ATA_DEVICE_STATS_LOG_TRANSPORT:
                            break;
                        case ATA_DEVICE_STATS_LOG_SSD:
                            device->drive_info.softSATFlags.deviceStatsPages.solidStateDeviceStatisticsSupported = true;
                            break;
                        default:
                            break;
                        }
                    }
                    if (device->drive_info.softSATFlags.deviceStatsPages.generalStatisitcsSupported)
                    {
                        //need to read this page and check if the data and time timestamp statistic is supported
                        memset(logBuffer, 0, LEGACY_DRIVE_SEC_SIZE);
                        if (SUCCESS == ata_Read_Log_Ext(device, ATA_LOG_DEVICE_STATISTICS, ATA_DEVICE_STATS_LOG_GENERAL, logBuffer, LEGACY_DRIVE_SEC_SIZE, device->drive_info.ata_Options.readLogWriteLogDMASupported, 0))
                        {
                            uint64_t *qwordptr = (uint64_t*)&logBuffer[0];
                            if (qwordptr[8] & BIT63)
                            {
                                device->drive_info.softSATFlags.deviceStatsPages.dateAndTimeTimestampSupported = true;
                            }
                        }
                    }
                }
            }
        }
    }
#ifdef _DEBUG
	printf("Drive type: %d\n",device->drive_info.drive_type);
	printf("Interface type: %d\n",device->drive_info.interface_type);
	printf("Media type: %d\n",device->drive_info.media_type);
    printf("SN: %s\n",device->drive_info.serialNumber);
    printf("%s <--\n",__FUNCTION__);
#endif
    return ret;
}

uint32_t GetRevWord(uint8_t *tempbuf, uint32_t offset)
{
    return ((tempbuf[offset + 1] << 8) + tempbuf[offset]);
}

uint16_t ata_Is_Extended_Power_Conditions_Feature_Supported(uint16_t *pIdentify)
{
    ptAtaIdentifyData pIdent = (ptAtaIdentifyData)pIdentify;
    // BIT7 according to ACS 3 rv 5 for EPC
    return (pIdent->Word119 & BIT7);
}

uint16_t ata_Is_One_Extended_Power_Conditions_Feature_Supported(uint16_t *pIdentify)
{
    ptAtaIdentifyData pIdent = (ptAtaIdentifyData)pIdentify;
    return (pIdent->Word120 & BIT7);

}

void print_Verbose_ATA_Command_Information(ataPassthroughCommand *ataCommandOptions)
{
    if (VERBOSITY_COMMAND_VERBOSE <= g_verbosity)
    {
        printf("Sending SAT ATA Pass-Through Command:\n");
        //protocol
        printf("\tProtocol: ");
        switch (ataCommandOptions->commadProtocol)
        {
        case ATA_PROTOCOL_PIO:
            printf("PIO");
            break;
        case ATA_PROTOCOL_DMA:
            printf("DMA");
            break;
        case ATA_PROTOCOL_NO_DATA:
            printf("NON-Data");
            break;
        case ATA_PROTOCOL_DEV_RESET:
            printf("Device Reset");
            break;
        case ATA_PROTOCOL_DEV_DIAG:
            printf("Device Diagnostic");
            break;
        case ATA_PROTOCOL_DMA_QUE:
            printf("DMA Queued");
            break;
        case ATA_PROTOCOL_PACKET:
        case ATA_PROTOCOL_PACKET_DMA:
            printf("Packet");
            break;
        case ATA_PROTOCOL_DMA_FPDMA:
            printf("FPDMA");
            break;
        case ATA_PROTOCOL_SOFT_RESET:
            printf("Soft Reset");
            break;
        case ATA_PROTOCOL_HARD_RESET:
            printf("Hard Reset");
            break;
        case ATA_PROTOCOL_RET_INFO:
            printf("Return Response Information");
            break;
        case ATA_PROTOCOL_UDMA:
            printf("UDMA");
            break;
        default:
            break;
        }
        printf("\n");
        printf("\tData Direction: ");
        //Data Direction:
        switch (ataCommandOptions->commandDirection)
        {
        case XFER_NO_DATA:
            printf("No Data");
            break;
        case XFER_DATA_IN:
            printf("Data In");
            break;
        case XFER_DATA_OUT:
            printf("Data Out");
            break;
        default:
            printf("Unknown");
            break;
        }
        printf("\n");
        //TFRs:
        printf("\tTask File Registers:\n");
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[FeatureExt] = %02"PRIX8"h\n", ataCommandOptions->tfr.Feature48);
        }
        printf("\t[Feature] = %02"PRIX8"h\n", ataCommandOptions->tfr.ErrorFeature);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[CountExt] = %02"PRIX8"h\n", ataCommandOptions->tfr.SectorCount48);
        }
        printf("\t[Count] = %02"PRIX8"h\n", ataCommandOptions->tfr.SectorCount);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[LBA Lo Ext] = %02"PRIX8"h\n", ataCommandOptions->tfr.LbaLow48);
        }
        printf("\t[LBA Lo] = %02"PRIX8"h\n", ataCommandOptions->tfr.LbaLow);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[LBA Mid Ext] = %02"PRIX8"h\n", ataCommandOptions->tfr.LbaMid48);
        }
        printf("\t[LBA Mid] = %02"PRIX8"h\n", ataCommandOptions->tfr.LbaMid);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[LBA Hi Ext] = %02"PRIX8"h\n", ataCommandOptions->tfr.LbaHi48);
        }
        printf("\t[LBA Hi] = %02"PRIX8"h\n", ataCommandOptions->tfr.LbaHi);
        printf("\t[DeviceHead] = %02"PRIX8"h\n", ataCommandOptions->tfr.DeviceHead);
        printf("\t[Command] = %02"PRIX8"h\n", ataCommandOptions->tfr.CommandStatus);
        //printf("\t[Device Control] = %02"PRIX8"h\n", ataCommandOptions->tfr.DeviceControl);
        printf("\n");
    }
}

void print_Verbose_ATA_Command_Result_Information(ataPassthroughCommand *ataCommandOptions)
{
    if (VERBOSITY_COMMAND_VERBOSE <= g_verbosity)
    {
        printf("\tReturn Task File Registers:\n");
        printf("\t[Error] = %02"PRIX8"h\n", ataCommandOptions->rtfr.error);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[Count Ext] = %02"PRIX8"h\n", ataCommandOptions->rtfr.secCntExt);
        }
        printf("\t[Count] = %02"PRIX8"h\n", ataCommandOptions->rtfr.secCnt);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[LBA Lo Ext] = %02"PRIX8"h\n", ataCommandOptions->rtfr.lbaLowExt);
        }
        printf("\t[LBA Lo] = %02"PRIX8"h\n", ataCommandOptions->rtfr.lbaLow);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[LBA Mid Ext] = %02"PRIX8"h\n", ataCommandOptions->rtfr.lbaMidExt);
        }
        printf("\t[LBA Mid] = %02"PRIX8"h\n", ataCommandOptions->rtfr.lbaMid);
        if (ataCommandOptions->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
        {
            printf("\t[LBA Hi Ext] = %02"PRIX8"h\n", ataCommandOptions->rtfr.lbaHiExt);
        }
        printf("\t[LBA Hi] = %02"PRIX8"h\n", ataCommandOptions->rtfr.lbaHi);
        printf("\t[Device] = %02"PRIX8"h\n", ataCommandOptions->rtfr.device);
        printf("\t[Status] = %02"PRIX8"h\n", ataCommandOptions->rtfr.status);
        printf("\n");
    }
}

void set_ATA_Passthrough_Type_By_Trial_And_Error(tDevice *device)
{
    if ((device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE) && device->drive_info.ata_Options.enableLegacyPassthroughDetectionThroughTrialAndError)
    {
#if defined (_DEBUG)
        printf("\n\tAttempting to set USB passthrough type with identify commands\n");
#endif
        while (device->drive_info.ata_Options.passthroughType != ATA_PASSTHROUGH_UNKNOWN)
        {
            uint8_t identifyData[LEGACY_DRIVE_SEC_SIZE] = { 0 };
            if (SUCCESS == ata_Identify(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
            {
                //command succeeded so this is most likely the correct pass-through type to use for this device
                //setting drive type while we're in here since it could help with a faster scan
                device->drive_info.drive_type = ATA_DRIVE;
                break;
            }
            else if (SUCCESS == ata_Identify_Packet_Device(device, identifyData, LEGACY_DRIVE_SEC_SIZE))
            {
                //command succeeded so this is most likely the correct pass-through type to use for this device
                //setting drive type while we're in here since it could help with a faster scan
                device->drive_info.drive_type = ATAPI_DRIVE;
                break;
            }
            ++device->drive_info.ata_Options.passthroughType;
        }
    }
}

typedef enum _eUSBVendorIDs
{
    evVendorSeagate = 0x0477,
    evVendorOxford = 0x0928,
    evVendorJMicron = 0x152d,
    evVendorDell = 0x413C,
    evVendorUSeagate = 0x0BC2,
    evVendorSamsung = 0x04E8,
    // Add new enumerations above this line!
    evVendorUnknown = 0
} eUSBVendorIDs;

void set_ATA_Passthrough_Type_By_PID_and_VID(tDevice *device)
{
    //only change the ATA Passthrough type for USB (for legacy USB bridges)
    if (device->drive_info.interface_type == USB_INTERFACE || device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        //Most USB bridges are SAT so they'll probably fall into the default cases and issue an identify command for SAT
        switch (device->drive_info.bridge_info.vendorID)
        {
        case evVendorUSeagate:
        case evVendorSeagate:
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x0501:
            case 0x0503:
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_CYPRESS;
                break;
            case 0x0502:
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_TI;
                break;
                
            default: //unknown
                set_ATA_Passthrough_Type_By_Trial_And_Error(device);
                break;
            }
            break;
        case evVendorOxford:
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x0008:
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_SAT;
                break;
            default: //unknown
                set_ATA_Passthrough_Type_By_Trial_And_Error(device);
                break;
            }
            break;
        case evVendorJMicron:
            switch (device->drive_info.bridge_info.productID)
            {
            case 0x2339://MiniD2
                device->drive_info.ata_Options.passthroughType = ATA_PASSTHROUGH_SAT;
                break;
            default: //unknown
                set_ATA_Passthrough_Type_By_Trial_And_Error(device);
                break;
            }
            break;
        case evVendorDell:
            switch (device->drive_info.bridge_info.productID)
            {
            default: //unknown
                set_ATA_Passthrough_Type_By_Trial_And_Error(device);
                break;
            }
            break;
        case evVendorSamsung:
            switch (device->drive_info.bridge_info.productID)
            {
            default: //unknown
                set_ATA_Passthrough_Type_By_Trial_And_Error(device);
                break;
            }
            break;
        default: //unknown
            set_ATA_Passthrough_Type_By_Trial_And_Error(device);
            break;
        }
    }
}

uint8_t calculate_ATA_Checksum(uint8_t *ptrData)
{
    uint8_t checksum = 0;
    uint32_t counter = 0;
    if (!ptrData)
    {
        return BAD_PARAMETER;
    }  
    for (counter = 0; counter < 511; ++counter)
    {
        checksum = checksum + ptrData[counter];
    }
    return checksum; // (~checksum + 1);//return this? or just the checksum?
}

bool is_Checksum_Valid(uint8_t *ptrData, uint32_t dataSize, uint32_t *firstInvalidSector)
{
    bool isValid = false;
    if (!ptrData || !firstInvalidSector)
    {
        return false;
    }
    uint8_t checksumCalc = 0;
    for (uint32_t blockIter = 0; blockIter < (dataSize / LEGACY_DRIVE_SEC_SIZE); ++blockIter)
    {
        for (uint32_t counter = 0; counter <= 511; ++counter)
        {
            checksumCalc = checksumCalc + ptrData[counter];
        }
        if (checksumCalc == 0)
        {
            isValid = true;
        }
        else
        {
            *firstInvalidSector = blockIter;
            isValid = false;
            break;
        }
    }
    return isValid;
}

int set_ATA_Checksum_Into_Data_Buffer(uint8_t *ptrData, uint32_t dataSize)
{
    int ret = SUCCESS;
    if (!ptrData)
    {
        return BAD_PARAMETER;
    }
    uint8_t checksum = 0;
    for (uint32_t blockIter = 0; blockIter < (dataSize / LEGACY_DRIVE_SEC_SIZE); ++blockIter)
    {
        checksum = calculate_ATA_Checksum(&ptrData[blockIter]);
        ptrData[blockIter + 511] = (~checksum + 1);
    }
    return ret;
}
