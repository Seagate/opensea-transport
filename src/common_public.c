// SPDX-License-Identifier: MPL-2.0
//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
//

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "error_translation.h"
#include "io_utils.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "secure_file.h"
#include "string_utils.h"
#include "type_conversion.h"

#include "common_public.h"
#include "csmi_helper_func.h"
#include "platform_helper.h"

void print_Low_Level_Info(tDevice* device)
{
    DISABLE_NONNULL_COMPARE
    if (device != M_NULLPTR)
    {
        int adapterIDWidthSpec;
        // Print out things useful for low-level debugging of the tDevice.
        // hacks
        // IOCTL type
        // CSMI information, if any
        // other APIs available from the OS
        // other handles that were found or open
        // Anything operating system specific would also be useful to output in here. Do this last and try to do common
        // stuff up front.
        printf("\n---Low Level tDevice information---\n");
        if (device->issue_io)
        {
            printf("\tissue io function pointer set\n");
        }
        if (device->issue_nvme_io)
        {
            printf("\tissue nvme io function pointer set\n");
        }
        if (device->dFlags & FORCE_ATA_PIO_ONLY)
        {
            printf("\tForcing ATA PIO only\n");
        }
        if (device->dFlags & FORCE_ATA_DMA_SAT_MODE)
        {
            printf("\tForcing ATA DMA mode\n");
        }
        if (device->dFlags & FORCE_ATA_UDMA_SAT_MODE)
        {
            printf("\tForcing ATA UDMA mode\n");
        }
        printf("\t---Drive Info---\n");
        // print things from drive info structure that will be useful
        printf("\t\tmedia type: ");
        switch (device->drive_info.media_type)
        {
        case MEDIA_HDD:
            printf("HDD\n");
            break;
        case MEDIA_SSD:
            printf("SDD\n");
            break;
        case MEDIA_SSM_FLASH:
            printf("SSM Flash\n");
            break;
        case MEDIA_SSHD:
            printf("SSHD\n");
            break;
        case MEDIA_OPTICAL:
            printf("Optical\n");
            break;
        case MEDIA_TAPE:
            printf("Tape\n");
            break;
        case MEDIA_NVM:
            printf("NVM\n");
            break;
        default:
            printf("unknown\n");
            break;
        }
        printf("\t\tdrive type: ");
        switch (device->drive_info.drive_type)
        {
        case ATA_DRIVE:
            printf("ATA\n");
            break;
        case SCSI_DRIVE:
            printf("SCSI\n");
            break;
        case RAID_DRIVE:
            printf("RAID\n");
            break;
        case NVME_DRIVE:
            printf("NVMe\n");
            break;
        case ATAPI_DRIVE:
            printf("ATAPI\n");
            break;
        case FLASH_DRIVE:
            printf("Flash\n");
            break;
        case LEGACY_TAPE_DRIVE:
            printf("Tape\n");
            break;
        case UNKNOWN_DRIVE:
            printf("unknown\n");
            break;
        }
        printf("\t\tinterface type: ");
        switch (device->drive_info.interface_type)
        {
        case IDE_INTERFACE:
            printf("IDE/ATA\n");
            break;
        case SCSI_INTERFACE:
            printf("SCSI/SAS/FC/etc\n");
            break;
        case RAID_INTERFACE:
            printf("RAID\n");
            break;
        case NVME_INTERFACE:
            printf("NVMe\n");
            break;
        case USB_INTERFACE:
            printf("USB\n");
            break;
        case MMC_INTERFACE:
            printf("MMC\n");
            break;
        case SD_INTERFACE:
            printf("SD\n");
            break;
        case IEEE_1394_INTERFACE:
            printf("IEEE 1394/Firewire\n");
            break;
        case UNKNOWN_INTERFACE:
            printf("unknown\n");
            break;
        }
        printf("\t\tzoned type: ");
        switch (device->drive_info.zonedType)
        {
        case ZONED_TYPE_NOT_ZONED:
            printf("not zoned\n");
            break;
        case ZONED_TYPE_HOST_AWARE:
            printf("host aware\n");
            break;
        case ZONED_TYPE_DEVICE_MANAGED:
            printf("device managed\n");
            break;
        case ZONED_TYPE_HOST_MANAGED:
            printf("host managed\n");
            break;
        case ZONED_TYPE_RESERVED:
            printf("unknown\n");
            break;
        }
        /*if (device->drive_info.bridge_info.isValid)
        {
            printf("\t\t---Bridge Info---\n");
        }*/
        printf("\t\t---adapter info---\n");
        adapterIDWidthSpec = 8;
        switch (device->drive_info.adapter_info.infoType)
        {
        case ADAPTER_INFO_USB:
            printf("\t\t\tUSB:\n");
            adapterIDWidthSpec = 4;
            break;
        case ADAPTER_INFO_IEEE1394:
            printf("\t\t\tIEEE1394:\n");
            adapterIDWidthSpec = 6; // these are 24bits
            break;
        case ADAPTER_INFO_PCI:
            printf("\t\t\tPCI/PCIe:\n");
            adapterIDWidthSpec = 4;
            break;
        case ADAPTER_INFO_UNKNOWN:
            printf("\t\t\tUnknown or no adapter info available\n");
            break;
        }
        if (device->drive_info.adapter_info.vendorIDValid)
        {
            printf("\t\t\tVendorID: %0*" PRIX32 "h\n", adapterIDWidthSpec, device->drive_info.adapter_info.vendorID);
        }
        if (device->drive_info.adapter_info.productIDValid)
        {
            printf("\t\t\tProductID: %0*" PRIX32 "h\n", adapterIDWidthSpec, device->drive_info.adapter_info.productID);
        }
        if (device->drive_info.adapter_info.revisionValid)
        {
            printf("\t\t\tRevision: %0*" PRIX32 "h\n", adapterIDWidthSpec, device->drive_info.adapter_info.revision);
        }
        if (device->drive_info.adapter_info.specifierIDValid)
        {
            printf("\t\t\tSpecifierID: %0*" PRIX32 "h\n", adapterIDWidthSpec,
                   device->drive_info.adapter_info.specifierID);
        }
        printf("\t\t---driver info---\n");
        printf("\t\t\tdriver name: %s\n", device->drive_info.driver_info.driverName);
        printf("\t\t\tdriver version string: %s\n", device->drive_info.driver_info.driverVersionString);
        if (device->drive_info.driver_info.majorVerValid)
        {
            printf("\t\t\t\tmajor ver: %" PRIu32 "\n", device->drive_info.driver_info.driverMajorVersion);
        }
        if (device->drive_info.driver_info.minorVerValid)
        {
            printf("\t\t\t\tminor ver: %" PRIu32 "\n", device->drive_info.driver_info.driverMinorVersion);
        }
        if (device->drive_info.driver_info.revisionVerValid)
        {
            printf("\t\t\t\trevision: %" PRIu32 "\n", device->drive_info.driver_info.driverRevision);
        }
        if (device->drive_info.driver_info.buildVerValid)
        {
            printf("\t\t\t\tbuild number: %" PRIu32 "\n", device->drive_info.driver_info.driverBuildNumber);
        }
        if (device->drive_info.drive_type == ATA_DRIVE)
        {
            // TODO: print ataoptions struct? This is things setup based on detection from identify and helps with how
            // some commands are built/issued
            printf("\t\t---ata flags---\n");
        }
        // TODO: Software SAT flags? These are currently always setup even when software translator is not active.
        if (device->drive_info.defaultTimeoutSeconds > 0)
        {
            printf("\t\tDefault timeout overridden as %" PRIu32 " seconds\n", device->drive_info.defaultTimeoutSeconds);
        }
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            printf("\t\tNamespace ID set as %" PRIX32 "\n", device->drive_info.namespaceID);
        }
        // TODO: Protection info?
        printf("\t\tSCSI Version: %" PRIu8 "\n", device->drive_info.scsiVersion);
        printf("\t\t---Passthrough Hacks---\n");
        if (device->drive_info.passThroughHacks.hacksSetByReportedID)
        {
            printf("\t\t\tHacks were setup from the reported adapter information\n");
        }
        if (device->drive_info.passThroughHacks.someHacksSetByOSDiscovery)
        {
            printf("\t\t\tSome hacks setup by OS discovery level\n");
        }
        printf("\t\t\tPassthrough type: ");
        switch (device->drive_info.passThroughHacks.passthroughType)
        {
        case ATA_PASSTHROUGH_SAT:
            printf("SAT/system/none\n");
            break;
        case ATA_PASSTHROUGH_CYPRESS:
            printf("ATA Cypress\n");
            break;
        case ATA_PASSTHROUGH_PROLIFIC:
            printf("ATA prolific\n");
            break;
        case ATA_PASSTHROUGH_TI:
            printf("ATA TI\n");
            break;
        case ATA_PASSTHROUGH_NEC:
            printf("ATA NEC\n");
            break;
        case ATA_PASSTHROUGH_PSP:
            printf("ATA PSP\n");
            break;
        case ATA_PASSTHROUGH_CSMI:
            printf("ATA CSMI CDBs (legacy, should be using SAT)\n");
            break;
        case ATA_PASSTHROUGH_UNKNOWN:
            printf("ATA unknown\n");
            break;
        case ATA_PASSTHROUGH_BEGIN_NON_USB:
            printf("ATA non-USB\n");
            break;
        case NVME_PASSTHROUGH_JMICRON:
            printf("NVMe JMicron\n");
            break;
        case NVME_PASSTHROUGH_ASMEDIA:
            printf("NVMe ASMedia\n");
            break;
        case NVME_PASSTHROUGH_ASMEDIA_BASIC:
            printf("NVMe ASMedia Basic\n");
            break;
        case NVME_PASSTHROUGH_REALTEK:
            printf("NVMe Realtek\n");
            break;
        case PASSTHROUGH_NONE:
            printf("None\n");
            break;
        case NVME_PASSTHROUGH_UNKNOWN:
            printf("NVMe Unknown\n");
            break;
        }
        // TODO: Test unit ready command after a failure value
        printf("\t\t\t\t---SCSI Hacks---\n");
        if (device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable)
        {
            printf("\t\t\t\t\tUNA (unit serial number available)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.readWrite.available)
        {
            if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6)
            {
                printf("\t\t\t\t\tRW6 (read/write 6 byte commands)\n");
            }
            if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10)
            {
                printf("\t\t\t\t\tRW10 (read/write 10 byte commands)\n");
            }
            if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12)
            {
                printf("\t\t\t\t\tRW12 (read/write 12 byte commands)\n");
            }
            if (device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16)
            {
                printf("\t\t\t\t\tRW16 (read/write 16 byte commands)\n");
            }
        }
        if (device->drive_info.passThroughHacks.scsiHacks.noVPDPages)
        {
            printf("\t\t\t\t\tNVPD (no VPD pages supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.noModePages)
        {
            printf("\t\t\t\t\tNMP (no Mode pages supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.noModeSubPages)
        {
            printf("\t\t\t\t\tNMSP (no Mode page subpages supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.noLogPages)
        {
            printf("\t\t\t\t\tNLP (no Log pages supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.noLogSubPages)
        {
            printf("\t\t\t\t\tNLPS (no Log page subpages supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.mode6bytes)
        {
            printf("\t\t\t\t\tMP6 (mode pages with 6byte commands only)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations)
        {
            printf("\t\t\t\t\tNRSUPOP (no report supported operation codes command)\n");
        }
        else
        {
            if (device->drive_info.passThroughHacks.scsiHacks.mode6bytes)
            {
                printf("\t\t\t\t\tSUPSOP (report single operation codes supported)\n");
            }
            if (device->drive_info.passThroughHacks.scsiHacks.mode6bytes)
            {
                printf("\t\t\t\t\tREPALLOP (report all operation codes supported)\n");
            }
        }
        if (device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported)
        {
            printf("\t\t\t\t\tSECPROT (security protocol command is supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512)
        {
            printf("\t\t\t\t\tSECPROTI512 (security protocol command with inc512 bit supported)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData)
        {
            printf("\t\t\t\t\tPRESCSI2 (inquiry data is pre-SCSI 2)\n");
        }
        if (device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload)
        {
            printf("\t\t\t\t\tWBND (HBA blocks SCSI write buffer - deferred download modes)\n");
        }
        // MXFER > 0 means we know a maximum transfer size available
        if (device->drive_info.passThroughHacks.scsiHacks.maxTransferLength > 0)
        {
            printf("\t\t\t\t\tMXFER = %" PRIu32 "B\n", device->drive_info.passThroughHacks.scsiHacks.maxTransferLength);
        }
        // list any NVMe hacks
        printf("\t\t\t\t---NVMe Hacks---\n");
        if (device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities)
        {
            printf("\t\t\t\t\tLIMPT (Limited passthrough capabilities. Only specific commands are allowed)\n");
            printf("\t\t\t\t\tAllowed commands:\n");
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric)
            {
                printf("\t\t\t\t\t\tAny identify command\n");
            }
            else
            {
                if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyController)
                {
                    printf("\t\t\t\t\t\tIdentify controller\n");
                }
                if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyNamespace)
                {
                    printf("\t\t\t\t\t\tIdentify namespace\n");
                }
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage)
            {
                printf("\t\t\t\t\t\tGet Log Page\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.format)
            {
                printf("\t\t\t\t\t\tFormat NVM\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getFeatures)
            {
                printf("\t\t\t\t\t\tGet Features\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareDownload)
            {
                printf("\t\t\t\t\t\tFirmware Download\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.firmwareCommit)
            {
                printf("\t\t\t\t\t\tFirmware Commit\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.vendorUnique)
            {
                printf("\t\t\t\t\t\tVendor Unique (Commands effects log)\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.deviceSelfTest)
            {
                printf("\t\t\t\t\t\tDevice Self Test\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.sanitize)
            {
                printf("\t\t\t\t\t\tSanitize\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.sanitizeCrypto)
            {
                printf("\t\t\t\t\t\tSanitize Crypto Erase\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.sanitizeBlock)
            {
                printf("\t\t\t\t\t\tSanitize Block Erase\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.sanitizeOverwrite)
            {
                printf("\t\t\t\t\t\tSanitize Overwrite Erase\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.namespaceManagement)
            {
                printf("\t\t\t\t\t\tNamespace Management\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.namespaceAttachment)
            {
                printf("\t\t\t\t\t\tNamespace Attachment\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.setFeatures)
            {
                printf("\t\t\t\t\t\tSet Features\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.miSend)
            {
                printf("\t\t\t\t\t\tMI Send\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.miReceive)
            {
                printf("\t\t\t\t\t\tMI Receive\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.securitySend)
            {
                printf("\t\t\t\t\t\tSecurity Send\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.securityReceive)
            {
                printf("\t\t\t\t\t\tSecurity Receive\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.formatCryptoSecureErase)
            {
                printf("\t\t\t\t\t\tFormat - SES = crypto erase\n");
            }
            if (device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.formatUserSecureErase)
            {
                printf("\t\t\t\t\t\tFormat - SES = user erase\n");
            }
        }
        if (device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength > 0)
        {
            printf("\t\t\t\t\tMPTLENGTH = %" PRIu32 "B\n",
                   device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength);
        }
        // list any ATA hacks
        printf("\t\t\t\t---ATA Hacks---\n");
        if (device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly)
        {
            printf("\t\t\t\t\tSCTSM (SCT commands only compatible with smart commands)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported)
        {
            printf("\t\t\t\t\tNA1 (A1h opcode, 12B SAT, is NOT supported by this device at all)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.a1ExtCommandWhenPossible)
        {
            printf("\t\t\t\t\tA1EXT (Some 48 bit commands must be issued with A1h opcode. Many limitations to this "
                   "device)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported)
        {
            printf(
                "\t\t\t\t\tRS (SAT return response info protocol is supported for determining command completion)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR)
        {
            printf("\t\t\t\t\tRSTD (SAT return response info TDIR bit can be set to ensure proper interpretation of "
                   "data direction)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit)
        {
            printf("\t\t\t\t\tRSIE (SAT return response info data requires ignoring the extend bit as it isn't handled "
                   "properly)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough)
        {
            printf(
                "\t\t\t\t\tTSPIU (SAT commands must use the TSPIU transfer type for all commands to work properly)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable)
        {
            printf("\t\t\t\t\tCHK (SAT check condition bit is always supported)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA)
        {
            printf("\t\t\t\t\tFDMA (SAT dma commands MUST use protocol set to DMA instead of UDMA-in/out)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported)
        {
            printf("\t\t\t\t\tNDMA (No DMA commands are supported on this adapter. Must use PIO only)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.partialRTFRs)
        {
            printf("\t\t\t\t\tPARTRTFR (Only 28bit responses are available. All extended registers are missing)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.noRTFRsPossible)
        {
            printf("\t\t\t\t\tNORTFR (It is impossible to get the drive's response. Can only rely on SAT translation "
                   "of errors if that is even available)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.multiSectorPIOWithMultipleMode)
        {
            printf("\t\t\t\t\tMMPIO (Multi-sector PIO commands are only possible if multiple mode configuration is "
                   "done first)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly)
        {
            printf("\t\t\t\t\tSPIO (Only single sector PIO commands are possible. Any attempts at multiple-sectors "
                   "will cause massive problems)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly)
        {
            printf("\t\t\t\t\tATA28 (Only 28bit ATA commands are possible on this adapter)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands)
        {
            printf("\t\t\t\t\tNOMMPIO (Do not use multiple mode read/write commands on this device. They are not "
                   "handled correctly)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength > 0)
        {
            printf("\t\t\t\t\tMPTXFER = %" PRIu32 "B\n",
                   device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength);
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU)
        {
            printf("\t\t\t\t\tTPID (TSPIU can be used on identify commands and possibly a few others, but it cannot be "
                   "used on every command)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.disableCheckCondition)
        {
            printf("\t\t\t\t\tNCHK (Do not use the check condition bit. It causes problems on this system)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty)
        {
            printf(
                "\t\t\t\t\tCHKE (Check condition bit is accepted but sense data is empty, so this bit is unusable)\n");
        }
        if (device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe)
        {
            printf("\t\t\t\t\tPEMUNV (Adapter is possibly a USB to NVMe adapter that responds to SAT ATA identify CDBs "
                   "with only MN, SN, FW)\n");
        }
        // print out the os_info unique things. This has a lot of ifdefs for the different OSs/configurations so need to
        // watch out for the differences in here
        printf("\t---OS Info---\n");
        printf("\t\thandle name: %s\n", device->os_info.name);
        printf("\t\tfriendly name: %s\n", device->os_info.friendlyName);
        printf("\t\tminimum memory alignment: %" PRIu8 "\n", device->os_info.minimumAlignment);
#if defined(UEFI_C_SOURCE)
        printf("\t\t---UEFI Unique Info---\n");
        // TODO: fd and device path
        printf("\t\t\tPassthrough type: ");
        switch (device->os_info.passthroughType)
        {
        case UEFI_PASSTHROUGH_UNKNOWN:
            printf("Unknown\n");
            break;
        case UEFI_PASSTHROUGH_SCSI:
            printf("SCSI\n");
            printf("\t\t\tSCSI Address:\n");
            printf("\t\t\t\tTarget: %" PRIu32 "\n", device->os_info.address.scsi.target);
            printf("\t\t\t\tLun: %" PRIu64 "\n", device->os_info.address.scsi.lun);
            break;
        case UEFI_PASSTHROUGH_SCSI_EXT:
            printf("SCSI Ext\n");
            printf("\t\t\t\tTarget: ");
            for (int i = 0; i < TARGET_MAX_BYTES; ++i)
            {
                printf("%02" PRIX8, device->os_info.address.scsiEx.target[i]);
            }
            printf("\n\t\t\t\tLun: %" PRIu64 "\n", device->os_info.address.scsiEx.lun);
            break;
        case UEFI_PASSTHROUGH_ATA:
            printf("ATA\n");
            printf("\t\t\t\tPort: %" PRIu16 "\n", device->os_info.address.ata.port);
            printf("\t\t\t\tPMP: %" PRIu16 "\n", device->os_info.address.ata.portMultiplierPort);
            break;
        case UEFI_PASSTHROUGH_NVME:
            printf("NVMe\n");
            printf("\t\t\t\tNamespace ID: %" PRIu32 "\n", device->os_info.address.nvme.namespaceID);
            break;
        }
        printf("\t\t\tController Number: %" PRIu16 "\n", device->os_info.controllerNum);
#elif defined(_WIN32)
        printf("\t\t---Windows Unique Info---\n");
        // show if handle and scsiSRBHandle are there
        if (device->os_info.fd != INVALID_HANDLE_VALUE && device->os_info.fd != 0)
        {
            printf("\t\t\tPrimary handle opened\n");
        }
        if (device->os_info.scsiSRBHandle != INVALID_HANDLE_VALUE && device->os_info.scsiSRBHandle != 0)
        {
            printf("\t\t\tSCSI SRB handle opened\n");
        }
        printf("\t\t\tSCSI Address:\n");
        printf("\t\t\t\tPort Number: %" PRIu8 "\n", device->os_info.scsi_addr.PortNumber);
        printf("\t\t\t\tPath ID: %" PRIu8 "\n", device->os_info.scsi_addr.PathId);
        printf("\t\t\t\tTarget ID: %" PRIu8 "\n", device->os_info.scsi_addr.TargetId);
        printf("\t\t\t\tLUN: %" PRIu8 "\n", device->os_info.scsi_addr.Lun);
        printf("\t\t\tos drive number: %" PRIu32 "\n", device->os_info.os_drive_number);
        printf("\t\t\tSRB type: %d\n", device->os_info.srbtype);
        printf("\t\t\tAlignment Mask: %lXh\n", device->os_info.alignmentMask);
        printf("\t\t\tIOCTL Type: ");
        switch (device->os_info.ioType)
        {
        case WIN_IOCTL_NOT_SET:
            printf("Not Set\n");
            break;
        case WIN_IOCTL_ATA_PASSTHROUGH:
            printf("ATA Passthrough\n");
            break;
        case WIN_IOCTL_SCSI_PASSTHROUGH:
            printf("SCSI Passthrough\n");
            break;
        case WIN_IOCTL_SCSI_PASSTHROUGH_EX:
            printf("SCSI Passthrough EX\n");
            break;
        case WIN_IOCTL_SMART_ONLY:
            printf("SMART Only\n");
            break;
        case WIN_IOCTL_IDE_PASSTHROUGH_ONLY:
            printf("IDE Passthrough Only\n");
            break;
        case WIN_IOCTL_SMART_AND_IDE:
            printf("SMART and IDE Passthrough\n");
            break;
        case WIN_IOCTL_STORAGE_PROTOCOL_COMMAND:
            printf("Storage Protocol Command\n");
            break;
        case WIN_IOCTL_BASIC:
            printf("Basic\n");
            break;
        }
        printf("\t\t\tIOCTL Method: ");
        switch (device->os_info.ioMethod)
        {
        case WIN_IOCTL_DEFAULT_METHOD:
            printf("Default\n");
            break;
        case WIN_IOCTL_FORCE_ALWAYS_DIRECT:
            printf("Force Direct\n");
            break;
        case WIN_IOCTL_FORCE_ALWAYS_DOUBLE_BUFFERED:
            printf("Force Double Buffered\n");
            break;
        case WIN_IOCTL_MAX_METHOD:
            printf("Unknown\n");
            break;
        }
        if (device->os_info.winSMARTCmdSupport.smartIOSupported)
        {
            printf("\t\t\tSMART IOCTL Support:\n");
            if (device->os_info.winSMARTCmdSupport.ataIDsupported)
            {
                printf("\t\t\t\tATA Identify supported\n");
            }
            if (device->os_info.winSMARTCmdSupport.atapiIDsupported)
            {
                printf("\t\t\t\tATAPI Identify supported\n");
            }
            if (device->os_info.winSMARTCmdSupport.smartSupported)
            {
                printf("\t\t\t\tSMART Supported\n");
            }
            printf("\t\t\t\tdevice bitmap: %" PRIX8 "h\n", device->os_info.winSMARTCmdSupport.deviceBitmap);
        }
        if (device->os_info.fwdlIOsupport.fwdlIOSupported)
        {
            printf("\t\t\tFWDL IOCTL Support:\n");
            if (device->os_info.fwdlIOsupport.allowFlexibleUseOfAPI)
            {
                printf("\t\t\t\tFlexible use flag enabled\n");
            }
            printf("\t\t\t\tPayload alignment: %" PRIu32 "B\n", device->os_info.fwdlIOsupport.payloadAlignment);
            printf("\t\t\t\tMaximum Transfer size: %" PRIu32 "B\n", device->os_info.fwdlIOsupport.maxXferSize);
        }
        printf("\t\t\tAdapter reported max transfer size: %" PRIu32 "B\n", device->os_info.adapterMaxTransferSize);
        if (device->os_info.openFabricsNVMePassthroughSupported)
        {
            printf("\t\t\tOpen Fabrics NVMe passthrough IOCTL is supported\n");
        }
        if (device->os_info.intelNVMePassthroughSupported)
        {
            printf("\t\t\tIntel NVMe Consumer passthrough is supported\n");
        }
        if (device->os_info.fwdlMiniportSupported)
        {
            printf("\t\t\tMiniport FWDL IOCTL is supported\n");
        }
        if (device->os_info.forceUnitAccessRWfd != INVALID_HANDLE_VALUE && device->os_info.forceUnitAccessRWfd != 0)
        {
            printf("\t\t\tFUA handle opened\n");
        }
        printf("\t\t\tVolume bitfield: %" PRIX32 "\n", device->os_info.volumeBitField);
        printf("\t\t\tAdapter Descriptor bustype: ");
        switch (device->os_info.adapterDescBusType)
        {
        case 0:
            printf("Unknown\n");
            break;
        case 1:
            printf("SCSI\n");
            break;
        case 2:
            printf("ATAPI\n");
            break;
        case 3:
            printf("ATA\n");
            break;
        case 4:
            printf("1394\n");
            break;
        case 5:
            printf("SSA\n");
            break;
        case 6:
            printf("Fibre\n");
            break;
        case 7:
            printf("USB\n");
            break;
        case 8:
            printf("RAID\n");
            break;
        case 9:
            printf("iSCSI\n");
            break;
        case 10:
            printf("SAS\n");
            break;
        case 11:
            printf("SATA\n");
            break;
        case 12:
            printf("SD\n");
            break;
        case 13:
            printf("MMC\n");
            break;
        case 14:
            printf("Virtual\n");
            break;
        case 15:
            printf("File Backed Virtual\n");
            break;
        case 16:
            printf("Spaces\n");
            break;
        case 17:
            printf("NVMe\n");
            break;
        case 18:
            printf("SCM\n");
            break;
        case 19:
            printf("UFS\n");
            break;
        case 0x7F:
            printf("Max reserved\n");
            break;
        default:
            printf("Unknown - %u\n", device->os_info.adapterDescBusType);
            break;
        }
        printf("\t\t\tDevice Descriptor bustype: ");
        switch (device->os_info.deviceDescBusType)
        {
        case 0:
            printf("Unknown\n");
            break;
        case 1:
            printf("SCSI\n");
            break;
        case 2:
            printf("ATAPI\n");
            break;
        case 3:
            printf("ATA\n");
            break;
        case 4:
            printf("1394\n");
            break;
        case 5:
            printf("SSA\n");
            break;
        case 6:
            printf("Fibre\n");
            break;
        case 7:
            printf("USB\n");
            break;
        case 8:
            printf("RAID\n");
            break;
        case 9:
            printf("iSCSI\n");
            break;
        case 10:
            printf("SAS\n");
            break;
        case 11:
            printf("SATA\n");
            break;
        case 12:
            printf("SD\n");
            break;
        case 13:
            printf("MMC\n");
            break;
        case 14:
            printf("Virtual\n");
            break;
        case 15:
            printf("File Backed Virtual\n");
            break;
        case 16:
            printf("Spaces\n");
            break;
        case 17:
            printf("NVMe\n");
            break;
        case 18:
            printf("SCM\n");
            break;
        case 19:
            printf("UFS\n");
            break;
        case 0x7F:
            printf("Max reserved\n");
            break;
        default:
            printf("Unknown - %u\n", device->os_info.deviceDescBusType);
            break;
        }
#elif defined(_AIX)
        printf("\t\t---AIX Unique info---\n");
        if (device->os_info.fd > 0)
        {
            printf("\t\t\trhdisk handle open and valid\n");
        }
        if (device->os_info.ctrlfdValid)
        {
            printf("\t\t\tController fd is valid\n");
        }
        if (device->os_info.diagnosticModeFlagInUse)
        {
            printf("\t\t\tHandle opened with SC_DIAGNOSTIC flag\n");
        }
        printf("\t\t\tscsiID: %" PRIX64 "h\n", device->os_info.scsiID);
        printf("\t\t\tlunID: %" PRIX64 "h\n", device->os_info.lunID);
        printf("\t\t\tPassthrough Type: ");
        switch (device->os_info.ptType)
        {
        case AIX_PASSTHROUGH_NOT_SET:
            printf("Not set\n");
            break;
        case AIX_PASSTHROUGH_SCSI:
            printf("SCSI\n");
            break;
        case AIX_PASSTHROUGH_IDE_ATA:
            printf("ATA\n");
            break;
        case AIX_PASSTHROUGH_IDE_ATAPI:
            printf("IDE ATAPI\n");
            break;
        case AIX_PASSTHROUGH_SATA:
            printf("SATA\n");
            break;
        case AIX_PASSTHROUGH_NVME:
            printf("NVMe\n");
            break;
        }
        switch (device->os_info.adapterType)
        {
        case AIX_ADAPTER_UNKNOWN:
            printf("Unknown\n");
            break;
        case AIX_ADAPTER_SCSI:
            printf("Parallel SCSI\n");
            break;
        case AIX_ADAPTER_IDE:
            printf("IDE\n");
            break;
        case AIX_ADAPTER_SAS:
            printf("SAS\n");
            break;
        case AIX_ADAPTER_SATA:
            printf("SATA\n");
            break;
        case AIX_ADAPTER_FC:
            printf("FC\n");
            break;
        case AIX_ADAPTER_USB:
            printf("USB\n");
            break;
        case AIX_ADAPTER_VSCSI:
            printf("VSCSI\n");
            break;
        case AIX_ADAPTER_ISCSI:
            printf("ISCSI\n");
            break;
        case AIX_ADAPTER_DASD:
            printf("DASD\n");
            break;
        case AIX_ADAPTER_NVME:
            printf("NVME\n");
            break;
        }
        if (device->os_info.maxXferLength > 0)
        {
            printf("\t\t\tadapter max transfer size: %" PRIu32 "B\n", device->os_info.maxXferLength);
        }
#elif defined(__linux__)
#    if defined(VMK_CROSS_COMP)
        printf("\t\t---VMWare ESXi Unique info---\n");
        if (device->os_info.fd > 0)
        {
            printf("\t\t\tFD is valid\n");
        }
        if (device->os_info.nvmeFd)
        {
            printf("\t\t\tNVME FD is valid\n");
        }
#    else  // VMK_CROSS_COMP
        printf("\t\t---Linux Unique info---\n");
        if (device->os_info.fd > 0)
        {
            printf("\t\t\tFD is valid\n");
        }
#    endif // VMK_CROSS_COMP
        if (device->os_info.scsiAddressValid)
        {
            printf("\t\t\tSCSI Address:\n");
            printf("\t\t\t\tHost: %" PRIu8 "\n", device->os_info.scsiAddress.host);
            printf("\t\t\t\tChannel: %" PRIu8 "\n", device->os_info.scsiAddress.channel);
            printf("\t\t\t\tTarget: %" PRIu8 "\n", device->os_info.scsiAddress.target);
            printf("\t\t\t\tLun: %" PRIu8 "\n", device->os_info.scsiAddress.lun);
        }
        if (device->os_info.secondHandleValid)
        {
            printf("\t\t\tSecond Handle name: %s\n", device->os_info.secondName);
            printf("\t\t\tSecond Handle friendly name: %s\n", device->os_info.secondFriendlyName);
            if (device->os_info.secondHandleOpened)
            {
                printf("\t\t\tSecond handle is open\n");
            }
        }
        if (device->os_info.sgDriverVersion.driverVersionValid)
        {
            printf("\t\t\tSG Driver Version:\n");
            printf("\t\t\t\tMajor: %" PRIu8 "\n", device->os_info.sgDriverVersion.majorVersion);
            printf("\t\t\t\tMinor: %" PRIu8 "\n", device->os_info.sgDriverVersion.minorVersion);
            printf("\t\t\t\tRevision: %" PRIu8 "\n", device->os_info.sgDriverVersion.revision);
        }
#elif defined(__FreeBSD__)
        printf("\t\t---FreeBSD Unique info---\n");
        if (device->os_info.fd > 0)
        {
            printf("\t\t\tFD is valid\n");
        }
        if (device->os_info.cam_dev)
        {
            printf("\t\t\tCam dev is valid\n");
            // TODO: Print out things from this structure???
        }
#endif // checking OS specific defines
        printf("\t\tOS read-write recommended: %s\n", device->os_info.osReadWriteRecommended ? "true" : "false");
#if defined(_WIN32)
        printf("\t\tlast recorded error: %lu\n", device->os_info.last_error);
#else
        printf("\t\tlast recorded error: %d\n", device->os_info.last_error);
#endif // _WIN32
        if (device->os_info.fileSystemInfo.fileSystemInfoValid)
        {
            printf("\t\tFile system Info:\n");
            if (device->os_info.fileSystemInfo.hasActiveFileSystem)
            {
                printf("\t\t\tActive file system detected on device\n");
            }
            else
            {
                printf("\t\t\tNo active file systems detected\n");
            }
            if (device->os_info.fileSystemInfo.isSystemDisk)
            {
                printf("\t\t\tDetected that this is the system disk\n");
            }
        }
#if defined(ENABLE_CSMI)
        // check if csmi is available
        if (device->os_info.csmiDeviceData && device->os_info.csmiDeviceData->csmiDeviceInfoValid)
        {
            print_CSMI_Device_Info(device);
        }
#endif // ENABLE_CSMI
        printf("\n");
    }
    RESTORE_NONNULL_COMPARE
}

size_t load_Bin_Buf(const char* filename, void* myBuf, size_t bufSize)
{
    secureFileInfo* fp        = secure_Open_File(filename, "rb", M_NULLPTR, M_NULLPTR, M_NULLPTR);
    size_t          bytesRead = SIZE_T_C(0);

    // Open file

    if (fp == M_NULLPTR || fp->error != SEC_FILE_SUCCESS)
    {
        free_Secure_File_Info(&fp);
        return 0;
    }

    // Read file contents into buffer
    if (SEC_FILE_SUCCESS != secure_Read_File(fp, myBuf, bufSize, sizeof(uint8_t), bufSize, &bytesRead))
    {
        printf("Error reading file into memory\n");
    }
    if (SEC_FILE_SUCCESS != secure_Close_File(fp))
    {
        printf("Error closing file after reading!\n");
    }
    free_Secure_File_Info(&fp);
    return bytesRead;
}

bool scan_Drive_Type_Filter(tDevice* device, uint32_t scanFlags)
{
    bool showDevice = false;
    // strip off all the other flags first
    scanFlags &= ALL_DRIVES;
    // if no filter flags are being used, then we need to just return true to show the device
    if (scanFlags == DEFAULT_SCAN)
    {
        showDevice = true;
    }
    else
    {
        bool showUSB  = false;
        bool showATA  = false;
        bool showSCSI = false;
        bool showNVMe = false;
        bool showRAID = false;
        if ((scanFlags & USB_DRIVES) > 0)
        {
            showUSB = true;
        }
        if ((scanFlags & ATA_DRIVES) > 0)
        {
            showATA = true;
        }
        if ((scanFlags & SCSI_DRIVES) > 0)
        {
            showSCSI = true;
        }
        if ((scanFlags & NVME_DRIVES) > 0)
        {
            showNVMe = true;
        }
        if ((scanFlags & RAID_DRIVES) > 0)
        {
            showRAID = true;
        }
        if (showATA)
        {
            if (device->drive_info.drive_type == ATA_DRIVE && device->drive_info.interface_type != USB_INTERFACE)
            {
                showDevice = true;
            }
        }
        if (showUSB && device->drive_info.interface_type == USB_INTERFACE)
        {
            showDevice = true;
        }
        if (showSCSI && device->drive_info.drive_type == SCSI_DRIVE)
        {
            showDevice = true;
        }
        if (showNVMe && device->drive_info.drive_type == NVME_DRIVE)
        {
            showDevice = true;
        }
        if (showRAID && device->drive_info.drive_type == RAID_DRIVE)
        {
            showDevice = true;
        }
    }
    return showDevice;
}

bool scan_Interface_Type_Filter(tDevice* device, uint32_t scanFlags)
{
    bool showInterface = false;
    // filter out other flags that don't matter here
    scanFlags &= ALL_INTERFACES;
    // if no filter flags are being used, then we need to just return true to show the device
    if (scanFlags == DEFAULT_SCAN)
    {
        showInterface = true;
    }
    else
    {
        bool showUSBInterface  = false;
        bool showATAInterface  = false;
        bool showSCSIInterface = false;
        bool showNVMeInterface = false;
        bool showRAIDInterface = false;
        if ((scanFlags & USB_INTERFACE_DRIVES) > 0)
        {
            showUSBInterface = true;
        }
        if ((scanFlags & IDE_INTERFACE_DRIVES) > 0)
        {
            showATAInterface = true;
        }
        if ((scanFlags & SCSI_INTERFACE_DRIVES) > 0)
        {
            showSCSIInterface = true;
        }
        if ((scanFlags & NVME_INTERFACE_DRIVES) > 0)
        {
            showNVMeInterface = true;
        }
        if ((scanFlags & RAID_INTERFACE_DRIVES) > 0)
        {
            showRAIDInterface = true;
        }
        if (showUSBInterface && device->drive_info.interface_type == USB_INTERFACE)
        {
            showInterface = true;
        }
        if (showATAInterface && device->drive_info.interface_type == IDE_INTERFACE)
        {
            showInterface = true;
        }
        if (showSCSIInterface && device->drive_info.interface_type == SCSI_INTERFACE)
        {
            showInterface = true;
        }
        if (showNVMeInterface && device->drive_info.interface_type == NVME_INTERFACE)
        {
            showInterface = true;
        }
        if (showRAIDInterface && device->drive_info.interface_type == RAID_INTERFACE)
        {
            showInterface = true;
        }
    }
    return showInterface;
}

// this is the "generic" scan. It uses the OS defined "get device count" and "get device list" calls to do the scan.
void scan_And_Print_Devs(unsigned int flags, eVerbosityLevels scanVerbosity)
{
    uint32_t deviceCount = UINT32_C(0);
#if defined(ENABLE_CSMI)
    uint32_t csmiDeviceCount      = UINT32_C(0);
    bool     csmiDeviceCountValid = false;
#endif
    uint64_t getCountFlags  = UINT64_C(0);
    uint64_t getDeviceflags = FAST_SCAN;
    if (flags & AGRESSIVE_SCAN)
    {
        getCountFlags |= BUS_RESCAN_ALLOWED;
    }
    // set the verbose flags to send onwards to the getDeviceList and getDeviceCount functions.
    switch (scanVerbosity)
    {
    case VERBOSITY_BUFFERS:
        getCountFlags |= GET_DEVICE_FUNCS_VERBOSE_BUFFERS;
        getDeviceflags |= GET_DEVICE_FUNCS_VERBOSE_BUFFERS;
        M_FALLTHROUGH;
    case VERBOSITY_COMMAND_VERBOSE:
        getCountFlags |= GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE;
        getDeviceflags |= GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE;
        M_FALLTHROUGH;
    case VERBOSITY_COMMAND_NAMES:
        getCountFlags |= GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES;
        getDeviceflags |= GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES;
        M_FALLTHROUGH;
    default:
        break;
    }
    if (SUCCESS == get_Device_Count(&deviceCount, getCountFlags))
    {
        if (deviceCount > 0)
        {
            tDevice* deviceList = M_REINTERPRET_CAST(tDevice*, safe_calloc_aligned(deviceCount, sizeof(tDevice), 8));
            versionBlock version;
            if (!deviceList)
            {
                DECLARE_ZERO_INIT_ARRAY(char, errorMessage, 50);
                snprintf_err_handle(errorMessage, 50, "calloc failure in scan to get %" PRIu32 " devices!",
                                    deviceCount);
                perror(errorMessage);
                return;
            }
            safe_memset(&version, sizeof(versionBlock), 0, sizeof(versionBlock));
            version.size    = sizeof(tDevice);
            version.version = DEVICE_BLOCK_VERSION;

            // set the verbosity for all devices before the scan
            for (uint32_t devi = UINT32_C(0); devi < deviceCount; ++devi)
            {
                deviceList[devi].deviceVerbosity = scanVerbosity;
            }

#if defined(ENABLE_CSMI)
            if (flags & IGNORE_CSMI)
            {
                getDeviceflags |= GET_DEVICE_FUNCS_IGNORE_CSMI;
            }
#endif
            eReturnValues ret = get_Device_List(deviceList, deviceCount * sizeof(tDevice), version, getDeviceflags);
            if (ret == SUCCESS || ret == WARN_NOT_ALL_DEVICES_ENUMERATED)
            {
                printf("%-8s %-12s %-23s %-22s %-10s\n", "Vendor", "Handle", "Model Number", "Serial Number", "FwRev");
                for (uint32_t devIter = UINT32_C(0); devIter < deviceCount; ++devIter)
                {
                    if (ret == WARN_NOT_ALL_DEVICES_ENUMERATED &&
                        UNKNOWN_DRIVE == deviceList[devIter].drive_info.drive_type)
                    {
                        continue;
                    }
                    if (flags & SCAN_SEAGATE_ONLY)
                    {
                        if (is_Seagate_Family(&deviceList[devIter]) == NON_SEAGATE)
                        {
                            continue;
                        }
                    }
                    if (flags & SCAN_IRONWOLF_NAS_ONLY)
                    {
                        if (is_Ironwolf_NAS_Drive(&deviceList[devIter], false) == NON_IRONWOLF_NAS_DRIVE)
                        {
                            continue;
                        }
                    }
                    if (flags & SCAN_SKYHAWK_EXOS_ONLY)
                    {
                        if (is_Skyhawk_Drive(&deviceList[devIter], false) == NON_SKYHAWK_DRIVE &&
                            !is_Exos_Drive(&deviceList[devIter], false))
                        {
                            continue;
                        }
                    }
#if defined(ENABLE_CSMI)
                    if (csmiDeviceCountValid &&
                        devIter >= (deviceCount -
                                    csmiDeviceCount)) // if the csmi device count is valid then we found some for the
                                                      // scan and need to see if we need to check for duplicates.
                    {
                        // check if we are being asked to show duplicates or not.
                        if (!(flags & ALLOW_DUPLICATE_DEVICE))
                        {
                            bool skipThisDevice = false;
                            for (uint32_t dupCheck = UINT32_C(0); dupCheck < (deviceCount - csmiDeviceCount);
                                 ++dupCheck)
                            {
                                // check if the WWN is valid (non-zero value) and then check if it matches anything else
                                // already in the list...this should be faster than the SN comparison below. - TJE
                                // check if the SN is valid (non-zero length) and then check if it matches anything
                                // already seen in the list... - TJE
                                if ((deviceList[devIter].drive_info.worldWideName != 0 &&
                                     deviceList[devIter].drive_info.worldWideName ==
                                         deviceList[dupCheck].drive_info.worldWideName) ||
                                    (safe_strlen(deviceList[devIter].drive_info.serialNumber) &&
                                     strcmp(deviceList[devIter].drive_info.serialNumber,
                                            deviceList[dupCheck].drive_info.serialNumber) == 0))
                                {
                                    skipThisDevice = true;
                                    break;
                                }
                            }
                            if (skipThisDevice)
                            {
                                continue;
                            }
                        }
                    }
#endif
                    if (scan_Drive_Type_Filter(&deviceList[devIter], flags) &&
                        scan_Interface_Type_Filter(&deviceList[devIter], flags))
                    {
                        DECLARE_ZERO_INIT_ARRAY(char, printable_sn, SERIAL_NUM_LEN + 1);
#define SCAN_DISPLAY_HANDLE_STRING_LENGTH 256
                        DECLARE_ZERO_INIT_ARRAY(char, displayHandle, SCAN_DISPLAY_HANDLE_STRING_LENGTH);
#if defined(_WIN32)
                        snprintf_err_handle(displayHandle, SCAN_DISPLAY_HANDLE_STRING_LENGTH, "%s",
                                            deviceList[devIter].os_info.friendlyName);
#else
                        snprintf_err_handle(displayHandle, SCAN_DISPLAY_HANDLE_STRING_LENGTH, "%s",
                                            deviceList[devIter].os_info.name);
#endif
#if defined(__linux__) && !defined(VMK_CROSS_COMP) && !defined(UEFI_C_SOURCE)
                        if ((flags & SG_TO_SD) > 0)
                        {
                            char* genName   = M_NULLPTR;
                            char* blockName = M_NULLPTR;
                            if (SUCCESS == map_Block_To_Generic_Handle(displayHandle, &genName, &blockName))
                            {
                                safe_memset(displayHandle, sizeof(displayHandle), 0, sizeof(displayHandle));
                                snprintf_err_handle(displayHandle, SCAN_DISPLAY_HANDLE_STRING_LENGTH, "%s<->%s",
                                                    genName, blockName);
                            }
                            safe_free(&genName);
                            safe_free(&blockName);
                        }
                        else if ((flags & SD_HANDLES) > 0)
                        {
                            char* genName   = M_NULLPTR;
                            char* blockName = M_NULLPTR;
                            if (SUCCESS == map_Block_To_Generic_Handle(displayHandle, &genName, &blockName))
                            {
                                safe_memset(displayHandle, SCAN_DISPLAY_HANDLE_STRING_LENGTH, 0,
                                            SCAN_DISPLAY_HANDLE_STRING_LENGTH);
                                snprintf_err_handle(displayHandle, SCAN_DISPLAY_HANDLE_STRING_LENGTH, "/dev/%s",
                                                    blockName);
                            }
                            safe_free(&genName);
                            safe_free(&blockName);
                        }
#endif
                        snprintf_err_handle(printable_sn, SERIAL_NUM_LEN + 1, "%s",
                                            deviceList[devIter].drive_info.serialNumber);
                        // if seagate scsi, need to truncate to 8 digits
                        if (deviceList[devIter].drive_info.drive_type == SCSI_DRIVE &&
                            is_Seagate_Family(&deviceList[devIter]) == SEAGATE)
                        {
                            safe_memset(printable_sn, SERIAL_NUM_LEN + 1, 0, SERIAL_NUM_LEN);
                            safe_memcpy(printable_sn, SERIAL_NUM_LEN + 1, deviceList[devIter].drive_info.serialNumber,
                                        8);
                        }
                        printf("%-8s %-12s %-23s %-22s %-10s\n", deviceList[devIter].drive_info.T10_vendor_ident,
                               displayHandle, deviceList[devIter].drive_info.product_identification, printable_sn,
                               deviceList[devIter].drive_info.product_revision);
                        flush_stdout();
                    }
                }
                // close all device handles
                for (uint32_t deviceIter = UINT32_C(0); deviceIter < deviceCount; ++deviceIter)
                {
                    close_Device(&deviceList[deviceIter]);
                }
            }
            safe_free_aligned_core(C_CAST(void**, &deviceList));
        }
        else
        {
            printf("No devices found\n");
        }
    }
    else
    {
        printf("Unable to get number of devices from OS\n");
    }
}

bool validate_Device_Struct(versionBlock sanity)
{
    bool   valid    = false;
    size_t tdevSize = sizeof(tDevice);
    if ((sanity.size == tdevSize) && (sanity.version == DEVICE_BLOCK_VERSION))
    {
        valid = true;
    }
    else
    {
        valid = false;
    }
    return valid;
}

eReturnValues get_Opensea_Transport_Version(apiVersionInfo* ver)
{
    DISABLE_NONNULL_COMPARE
    if (ver != M_NULLPTR)
    {
        ver->majorVersion = OPENSEA_TRANSPORT_MAJOR_VERSION;
        ver->minorVersion = OPENSEA_TRANSPORT_MINOR_VERSION;
        ver->patchVersion = OPENSEA_TRANSPORT_PATCH_VERSION;
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
    RESTORE_NONNULL_COMPARE
}

eReturnValues get_Version_Block(versionBlock* ver)
{
    DISABLE_NONNULL_COMPARE
    if (ver != M_NULLPTR)
    {
        ver->size    = sizeof(tDevice);
        ver->version = DEVICE_BLOCK_VERSION;
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
    RESTORE_NONNULL_COMPARE
}

static void set_IEEE_OUI(uint32_t* ieeeOUI, tDevice* device, bool USBchildDrive)
{
    uint8_t  naa = UINT8_C(0);
    uint64_t wwn = UINT64_C(0);
    if (!USBchildDrive)
    {
        wwn = device->drive_info.worldWideName;
    }
    else
    {
        wwn = device->drive_info.bridge_info.childWWN;
    }
    naa = C_CAST(uint8_t, (wwn & UINT64_C(0xF000000000000000)) >> 60);
    switch (naa)
    {
    case 2: // bytes 2,3,4
        *ieeeOUI = C_CAST(uint32_t, (wwn & UINT64_C(0x0000FFFFFF000000)) >> 24);
        break;
    case 5: // most common - ATA requires this and I think SCSI almost always matches    bytes 0 - 3 (half of 0, half of
            // 3) see SPC4 for details
    case 6: // same as NAA format 5
        *ieeeOUI = C_CAST(uint32_t, (wwn & UINT64_C(0x0FFFFFF000000000)) >> 36);
        break;
    default:
        // don't do anything since we don't have a way to parse it out of here or it is a new format that wasn't defined
        // when writing this
        break;
    }
}

bool is_Maxtor_String(const char* string)
{
    bool   isMaxtor  = false;
    size_t maxtorLen = safe_strlen("MAXTOR");
    size_t stringLen = safe_strlen(string);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", string);
        localString[stringLen] = '\0';
        convert_String_To_Upper_Case(localString);
        if (safe_strlen(localString) >= maxtorLen && strncmp(localString, "MAXTOR", maxtorLen) == 0)
        {
            isMaxtor = true;
        }
        safe_free(&localString);
    }
    return isMaxtor;
}

bool is_Maxtor(tDevice* device, bool USBchildDrive)
{
    bool     isMaxtor = false;
    uint32_t ieeeOUI  = UINT32_C(0);
    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_MAXTOR:
        isMaxtor = true;
        break;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
        {
            // on a USB drive, check the child information as well as the bridge information
            isMaxtor = is_Maxtor(device, true);
        }
        else
        {
            isMaxtor = false;
        }
        break;
    }
    if (!isMaxtor)
    {
        // we need to check the Vendor ID if SCSI or USB interface
        if (device->drive_info.interface_type == USB_INTERFACE ||
            (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        {
            isMaxtor = is_Maxtor_String(device->drive_info.T10_vendor_ident);
        }
        // if still false (ata drive should be), then check the model number
        if (!isMaxtor)
        {
            isMaxtor = is_Maxtor_String(device->drive_info.product_identification);
            // if after a model number check, the result is still false and it's USB, we need to check the child drive
            // information just to be certain
            if (!isMaxtor && device->drive_info.interface_type == USB_INTERFACE)
            {
                isMaxtor = is_Maxtor_String(device->drive_info.bridge_info.childDriveMN);
            }
        }
    }
    return isMaxtor;
}

bool is_Seagate_VendorID(tDevice* device)
{
    bool   isSeagate  = false;
    size_t seagateLen = safe_strlen("SEAGATE");
    size_t stringLen  = safe_strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
        localString[stringLen] = '\0';
        convert_String_To_Upper_Case(localString);
        if (safe_strlen(localString) >= seagateLen && strncmp(localString, "SEAGATE", seagateLen) == 0)
        {
            isSeagate = true;
        }
        safe_free(&localString);
    }
    return isSeagate;
}

bool is_Seagate_MN(const char* string)
{
    bool   isSeagate  = false;
    size_t seagateLen = safe_strlen("ST");
    size_t stringLen  = safe_strlen(string);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", string);
        localString[stringLen] = '\0';
        // convert_String_To_Upper_Case(localString);//Removing uppercase converstion, thus making this a case sensitive
        // comparison to fix issues with other non-Seagate products being detected as Seagate.
        if (safe_strlen(localString) >= seagateLen && strncmp(localString, "ST", seagateLen) == 0)
        {
            isSeagate = true;
        }
        safe_free(&localString);
    }
    return isSeagate;
}

bool is_Seagate(tDevice* device, bool USBchildDrive)
{
    bool     isSeagate = false;
    uint32_t ieeeOUI   = UINT32_C(0);

    // This check should work well enough, but we do support checking the IEEE OUI as well now. It must be set in the
    // WWN correctly with the NAA field set to 5h or 6h - TJE
    if ((device->drive_info.interface_type == NVME_INTERFACE) &&
        (device->drive_info.adapter_info.vendorID == SEAGATE_VENDOR_ID))
    {
        return true;
    }

    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_SEAGATE1:
    case IEEE_SEAGATE2:
    case IEEE_SEAGATE3:
    case IEEE_SEAGATE4:
    case IEEE_SEAGATE5:
    case IEEE_SEAGATE6:
    case IEEE_SEAGATE7:
    case IEEE_SEAGATE8:
        isSeagate = true;
        break;
    case IEEE_SEAGATE_NVME:
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            isSeagate = true;
        }
        break;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
        {
            // on a USB drive, check the child information as well as the bridge information
            isSeagate = is_Seagate(device, true);
        }
        else
        {
            isSeagate = false;
        }
        break;
    }
    if (!isSeagate)
    {
        // we need to check the Vendor ID if SCSI or USB interface
        if (device->drive_info.interface_type == USB_INTERFACE ||
            (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        {
            isSeagate = is_Seagate_VendorID(device);
        }
        // if still false (ata drive should be), then check the model number
        if (!isSeagate)
        {
            isSeagate = is_Seagate_MN(device->drive_info.product_identification);
            // if after a model number check, the result is still false and it's USB, we need to check the child drive
            // information just to be certain
            if (!isSeagate && device->drive_info.interface_type == USB_INTERFACE)
            {
                isSeagate = is_Seagate_MN(device->drive_info.bridge_info.childDriveMN);
            }
        }
    }
    return isSeagate;
}

bool is_Conner_Model_Number(const char* mn)
{
    bool isConner = false;
    // found online. Not sure how accurate this will be
    if (strncmp(mn, "CFA", 3) == 0 || strncmp(mn, "CFL", 3) == 0 || strncmp(mn, "CFN", 3) == 0 ||
        strncmp(mn, "CFS", 3) == 0 || strncmp(mn, "CP", 2) == 0)
    {
        isConner = true;
    }
    return isConner;
}

bool is_Conner_VendorID(tDevice* device)
{
    bool   isConner  = false;
    size_t connerLen = safe_strlen("CONNER");
    size_t stringLen = safe_strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
        localString[stringLen] = '\0';
        if (safe_strlen(localString) >= connerLen && strncmp(localString, "CONNER", connerLen) == 0)
        {
            isConner = true;
        }
        safe_free(&localString);
    }
    return isConner;
}

bool is_Connor(tDevice* device, bool USBchildDrive)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return is_Conner_VendorID(device);
    }
    else
    {
        if (USBchildDrive)
        {
            return is_Conner_Model_Number(device->drive_info.bridge_info.childDriveMN);
        }
        else
        {
            bool result = is_Conner_Model_Number(device->drive_info.product_identification);
            if (!result && device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
            {
                return is_Conner_Model_Number(device->drive_info.bridge_info.childDriveMN);
            }
            return result;
        }
    }
}

bool is_CDC_VendorID(tDevice* device)
{
    bool isCDC = false;
    if (get_bit_range_uint8(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)
    {
        size_t cdcLen    = safe_strlen("CDC");
        size_t stringLen = safe_strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
            if (localString == M_NULLPTR)
            {
                perror("calloc failure");
                return false;
            }
            snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (safe_strlen(localString) >= cdcLen && strncmp(localString, "CDC", cdcLen) == 0)
            {
                isCDC = true;
            }
            safe_free(&localString);
        }
    }
    return isCDC;
}

bool is_DEC_VendorID(tDevice* device)
{
    bool isDEC = false;
    if (get_bit_range_uint8(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) == 0)
    {
        size_t cdcLen    = safe_strlen("DEC");
        size_t stringLen = safe_strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
            if (localString == M_NULLPTR)
            {
                perror("calloc failure");
                return false;
            }
            snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (safe_strlen(localString) >= cdcLen && strncmp(localString, "DEC", cdcLen) == 0)
            {
                isDEC = true;
            }
            safe_free(&localString);
        }
    }
    return isDEC;
}

bool is_MiniScribe_VendorID(tDevice* device)
{
    bool   isMiniscribe  = false;
    size_t miniscribeLen = safe_strlen("MINSCRIB");
    size_t stringLen     = safe_strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
        localString[stringLen] = '\0';
        if (safe_strlen(localString) >= miniscribeLen && strncmp(localString, "MINSCRIB", miniscribeLen) == 0)
        {
            isMiniscribe = true;
        }
        safe_free(&localString);
    }
    return isMiniscribe;
}

bool is_Quantum_VendorID(tDevice* device)
{
    bool isQuantum = false;
    if (get_bit_range_uint8(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) ==
        0) // must be direct access block device for HDD
    {
        size_t quantumLen = safe_strlen("QUANTUM");
        size_t stringLen  = safe_strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
            if (localString == M_NULLPTR)
            {
                perror("calloc failure");
                return false;
            }
            snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (safe_strlen(localString) >= quantumLen && strncmp(localString, "QUANTUM", quantumLen) == 0)
            {
                isQuantum = true;
            }
            safe_free(&localString);
        }
    }
    return isQuantum;
}

bool is_Quantum_Model_Number(const char* string)
{
    bool   isQuantum  = false;
    size_t quantumLen = safe_strlen("Quantum");
    size_t stringLen  = safe_strlen(string);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", string);
        localString[stringLen] = '\0';
        if (safe_strlen(localString) >= quantumLen &&
            (strncmp(localString, "Quantum", quantumLen) == 0 || strncmp(localString, "QUANTUM", quantumLen) == 0))
        {
            isQuantum = true;
        }
        safe_free(&localString);
    }
    return isQuantum;
}

bool is_Quantum(tDevice* device, bool USBchildDrive)
{
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        return is_Quantum_VendorID(device);
    }
    else
    {
        if (USBchildDrive)
        {
            return is_Quantum_Model_Number(device->drive_info.bridge_info.childDriveMN);
        }
        else
        {
            bool result = false;
            if (get_bit_range_uint8(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) ==
                0) // must be direct access block device for HDD
            {
                result = is_Quantum_Model_Number(device->drive_info.product_identification);
            }
            if (!result && device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
            {
                return is_Quantum_Model_Number(device->drive_info.bridge_info.childDriveMN);
            }
            return result;
        }
    }
}

bool is_PrarieTek_VendorID(tDevice* device)
{
    bool isPrarieTek = false;
    if (get_bit_range_uint8(device->drive_info.scsiVpdData.inquiryData[0], 4, 0) ==
        0) // must be direct access block device for HDD
    {
        size_t prarieTekLen = safe_strlen("PRAIRIE");
        size_t stringLen    = safe_strlen(device->drive_info.T10_vendor_ident);
        if (stringLen > 0)
        {
            char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
            if (localString == M_NULLPTR)
            {
                perror("calloc failure");
                return false;
            }
            snprintf_err_handle(localString, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
            localString[stringLen] = '\0';
            if (safe_strlen(localString) >= prarieTekLen && strncmp(localString, "PRAIRIE", prarieTekLen) == 0)
            {
                isPrarieTek = true;
            }
            safe_free(&localString);
        }
    }
    return isPrarieTek;
}

bool is_LaCie(tDevice* device)
{
    bool isLaCie = false;
    // LaCie drives do not have a IEEE OUI, so the only way we know it's LaCie is to check the VendorID field reported
    // by the device
    size_t lacieLen  = safe_strlen("LACIE");
    size_t stringLen = safe_strlen(device->drive_info.T10_vendor_ident);
    if (stringLen > 0)
    {
        char* vendorID = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (vendorID == M_NULLPTR)
        {
            perror("calloc failure");
            return MEMORY_FAILURE;
        }
        snprintf_err_handle(vendorID, stringLen + 1, "%s", device->drive_info.T10_vendor_ident);
        vendorID[stringLen] = '\0';
        convert_String_To_Upper_Case(vendorID);
        if (safe_strlen(vendorID) >= lacieLen && strncmp(vendorID, "LACIE", lacieLen) == 0)
        {
            isLaCie = true;
        }
        safe_free(&vendorID);
    }
    return isLaCie;
}

bool is_Samsung_String(const char* string)
{
    bool   isSamsung  = false;
    size_t samsungLen = safe_strlen("SAMSUNG");
    size_t stringLen  = safe_strlen(string);
    if (stringLen > 0)
    {
        char* localString = M_REINTERPRET_CAST(char*, safe_calloc(stringLen + 1, sizeof(char)));
        if (localString == M_NULLPTR)
        {
            perror("calloc failure");
            return false;
        }
        snprintf_err_handle(localString, stringLen + 1, "%s", string);
        localString[stringLen] = '\0';
        convert_String_To_Upper_Case(localString);
        if (safe_strlen(localString) >= samsungLen && strncmp(localString, "SAMSUNG", samsungLen) == 0)
        {
            isSamsung = true;
        }
        safe_free(&localString);
    }
    return isSamsung;
}

bool is_Samsung_HDD(tDevice* device, bool USBchildDrive)
{
    bool     isSamsung = false;
    bool     isSSD     = false;
    uint32_t ieeeOUI   = UINT32_C(0);
    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_SEAGATE_SAMSUNG_HDD:
    case IEEE_SAMSUNG_HDD1:
    case IEEE_SAMSUNG_HDD2:
        isSamsung = true;
        break;
    case IEEE_SAMSUNG_SSD:
        isSSD = true;
        M_FALLTHROUGH;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive && !isSSD)
        {
            // on a USB drive, check the child information as well as the bridge information
            isSamsung = is_Samsung_HDD(device, true);
        }
        else
        {
            isSamsung = false;
        }
        break;
    }
    isSSD = is_SSD(device);
    if (!isSamsung && !isSSD)
    {
        // this fall back method should only be called on samsung HDD's and these should only be really old ones without
        // a WWN which should be a minority. All drives with IEEE_SEAGATE_SAMSUNG_HDD should be caught long before this
        // we need to check the Vendor ID if SCSI or USB interface
        if (device->drive_info.interface_type == USB_INTERFACE ||
            (device->drive_info.interface_type == SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        {
            isSamsung = is_Samsung_String(device->drive_info.T10_vendor_ident);
        }
        // if still false (ata drive should be), then check the model number
        if (!isSamsung)
        {
            isSamsung = is_Samsung_String(device->drive_info.product_identification);
            // if after a model number check, the result is still false and it's USB, we need to check the child drive
            // information just to be certain
            if (!isSamsung && device->drive_info.interface_type == USB_INTERFACE)
            {
                isSamsung = is_Samsung_String(device->drive_info.bridge_info.childDriveMN);
            }
        }
    }
    return isSamsung;
}

bool is_Seagate_Model_Vendor_A(tDevice* device)
{
    bool isSeagateVendorA = false;
    if (device->drive_info.drive_type == SCSI_DRIVE)
    {
        const char* vendorAModel1 = "S650DC";
        const char* vendorAModel2 = "S630DC";
        const char* vendorAModel3 = "S610DC";
        if (strncmp(vendorAModel1, device->drive_info.product_identification, safe_strlen(vendorAModel1)) == 0 ||
            strncmp(vendorAModel2, device->drive_info.product_identification, safe_strlen(vendorAModel2)) == 0 ||
            strncmp(vendorAModel3, device->drive_info.product_identification, safe_strlen(vendorAModel3)) == 0)
        {
            isSeagateVendorA = true;
        }
    }
    return isSeagateVendorA;
}

bool is_Vendor_A(tDevice* device, bool USBchildDrive)
{
    bool     isVendorA = false;
    uint32_t ieeeOUI   = UINT32_C(0);
    set_IEEE_OUI(&ieeeOUI, device, USBchildDrive);
    switch (ieeeOUI)
    {
    case IEEE_VENDOR_A:
    case IEEE_VENDOR_A_TECHNOLOGY:
        isVendorA = true;
        break;
    default:
        if (device->drive_info.interface_type == USB_INTERFACE && !USBchildDrive)
        {
            // on a USB drive, check the child information as well as the bridge information
            isVendorA = is_Vendor_A(device, true);
        }
        else
        {
            isVendorA = false;
        }
        break;
    }
    if (!isVendorA)
    {
        // this fall back method should only be called on samsung HDD's and these should only be really old ones without
        // a WWN which should be a minority. All drives with IEEE_SEAGATE_SAMSUNG_HDD should be caught long before this
        // we need to check the Vendor ID if SCSI or USB interface
        // if (device->drive_info.interface_type == USB_INTERFACE || (device->drive_info.interface_type ==
        // SCSI_INTERFACE && device->drive_info.drive_type != ATA_DRIVE))
        //{
        //     isVendorA = is_Vendor_A_String(device->drive_info.T10_vendor_ident);
        // }
        // if still false (ata drive should be), then check the model number
        // if (!isVendorA)
        //{
        //    isVendorA = is_Vendor_A_String(device->drive_info.product_identification);
        //     //if after a model number check, the result is still false and it's USB, we need to check the child drive
        //     information just to be certain if (!isVendorA && device->drive_info.interface_type == USB_INTERFACE)
        //     {
        //         isVendorA = is_Vendor_A_String(device->drive_info.bridge_info.childDriveMN);
        //     }
        // }
    }
    return isVendorA;
}

eIronwolf_NAS_Drive is_Ironwolf_NAS_Drive(tDevice* device, bool USBchildDrive)
{
    eIronwolf_NAS_Drive isIronWolfNASDrive = NON_IRONWOLF_NAS_DRIVE;
    char*               modelNumber        = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        modelNumber = &device->drive_info.bridge_info.childDriveMN[0];
    }

    if (safe_strlen(modelNumber))
    {
        if (wildcard_Match("ST*VN*", modelNumber) ||  // check if Ironwolf HDD
            wildcard_Match("*ZA*NM*", modelNumber) || // check if SATA Ironwolf SSD
            wildcard_Match("*ZP*NM*", modelNumber))   // check if PCIe Ironwolf SSD
        {
            isIronWolfNASDrive = IRONWOLF_NAS_DRIVE;
        }
        else if (wildcard_Match("ST*NE*", modelNumber) || // check if Ironwolf Pro HDD
                 wildcard_Match("ST*NT*", modelNumber) || // check if Ironwolf Pro HDD
                 wildcard_Match("ST*ND*", modelNumber) || // check if Ironwolf Pro HDD
                 wildcard_Match("*ZA*NX*", modelNumber))  // check if SATA Ironwolf Pro SSD
        {
            isIronWolfNASDrive = IRONWOLF_PRO_NAS_DRIVE;
        }
    }

    if (!USBchildDrive && isIronWolfNASDrive == NON_IRONWOLF_NAS_DRIVE)
    {
        return is_Ironwolf_NAS_Drive(device, true);
    }

    return isIronWolfNASDrive;
}

bool is_Firecuda_Drive(tDevice* device, bool USBchildDrive)
{
    bool  isFirecudaDrive = false;
    char* modelNumber     = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        modelNumber = &device->drive_info.bridge_info.childDriveMN[0];
    }

    if (safe_strlen(modelNumber))
    {
        if (wildcard_Match("ST*DX*", modelNumber) ||  // check if Firecuda HDD
            wildcard_Match("ST*LX*", modelNumber) ||  // check if Firecuda HDD
            wildcard_Match("*ZA*GM*", modelNumber) || // check if SATA Firecuda SSD
            wildcard_Match("*ZP*GM*", modelNumber) || // check if PCIe Firecuda SSD
            wildcard_Match("*ZP*GV*", modelNumber))   // check if PCIe Firecuda SSD
        {
            isFirecudaDrive = true;
        }
    }

    if (!USBchildDrive && !isFirecudaDrive)
    {
        return is_Firecuda_Drive(device, true);
    }

    return isFirecudaDrive;
}

eSkyhawk_Drive is_Skyhawk_Drive(tDevice* device, bool USBchildDrive)
{
    eSkyhawk_Drive isSkyhawkDrive = NON_SKYHAWK_DRIVE;
    char*          modelNumber    = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        modelNumber = &device->drive_info.bridge_info.childDriveMN[0];
    }

    if (safe_strlen(modelNumber))
    {
        if (wildcard_Match("ST*VX*", modelNumber) ||   // check if Skyhawk HDD
            wildcard_Match("ST*HKVS*", modelNumber) || // check if Skyhawk HDD
            wildcard_Match("ST*VM*", modelNumber))     // check if Skyhawk HDD
        {
            isSkyhawkDrive = SKYHAWK_DRIVE;
        }
        else if (wildcard_Match("ST*VE*", modelNumber) ||
                 wildcard_Match("ST*HKAI*", modelNumber)) // check if Skyhawk AI HDD
        {
            isSkyhawkDrive = SKYHAWK_AI_DRIVE;
        }
    }

    if (!USBchildDrive && isSkyhawkDrive == NON_SKYHAWK_DRIVE)
    {
        return is_Skyhawk_Drive(device, true);
    }
    return isSkyhawkDrive;
}

bool is_Nytro_Drive(tDevice* device, bool USBchildDrive)
{
    bool  isNytroDrive = false;
    char* modelNumber  = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        modelNumber = &device->drive_info.bridge_info.childDriveMN[0];
    }

    if (safe_strlen(modelNumber))
    {
        if (wildcard_Match("XS*SE*", modelNumber) ||  // Nytro 3332, Nytro 3331, Nytro 2332
            wildcard_Match("*XS*LE*", modelNumber) || // Nytro 3532, Nytro 3531, Nytro 2532
            wildcard_Match("*XS*ME*", modelNumber) || // Nytro 3732, Nytro 3731
            wildcard_Match("*XS*TE*", modelNumber) || // Nytro 3131
            wildcard_Match("*XA*LE*", modelNumber) || // Nytro 1351
            wildcard_Match("*XS*ME*", modelNumber) || // Nytro 1551
            wildcard_Match("*XP*LE*", modelNumber) || // Nytro 5910
            wildcard_Match("*XP*EX*", modelNumber) || // Nytro 510
            wildcard_Match("*XP*DC*", modelNumber))   // Nytro 510
        {
            isNytroDrive = true;
        }
    }

    if (!USBchildDrive && !isNytroDrive)
    {
        return is_Nytro_Drive(device, true);
    }

    return isNytroDrive;
}

bool is_Exos_Drive(tDevice* device, bool USBchildDrive)
{
    bool  isExosDrive = false;
    char* modelNumber = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        modelNumber = &device->drive_info.bridge_info.childDriveMN[0];
    }

    if (safe_strlen(modelNumber))
    {
        if (wildcard_Match("ST*NM*", modelNumber) ||  // Exos X-series
            wildcard_Match("*ST*MP*", modelNumber) || // Exos E-series
            wildcard_Match("*ST*MM*", modelNumber) || // Exos E-series
            wildcard_Match("*ST*NX*", modelNumber))   // Exos E-series
        {
            isExosDrive = true;
        }
    }

    if (!USBchildDrive && !isExosDrive)
    {
        return is_Exos_Drive(device, true);
    }

    return isExosDrive;
}

bool is_Barracuda_Drive(tDevice* device, bool USBchildDrive)
{
    bool  isBarracudaDrive = false;
    char* modelNumber      = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        modelNumber = &device->drive_info.bridge_info.childDriveMN[0];
    }

    if (safe_strlen(modelNumber))
    {
        if (wildcard_Match("ST*LM*", modelNumber) ||  // Barracuda 2.5 inhces
            wildcard_Match("ST*DM*", modelNumber) ||  // Barracuda 3.5 inhces
            wildcard_Match("*ZA*CV*", modelNumber) || // Barracuda Q1
            wildcard_Match("*ZP*CV*", modelNumber) || // Barracuda Q5
            wildcard_Match("*ZA*CM*", modelNumber) || // Barracuda 120
            wildcard_Match("*ZP*CM*", modelNumber))   // Barracuda 510
        {
            isBarracudaDrive = true;
        }
    }

    if (!USBchildDrive && !isBarracudaDrive)
    {
        return is_Barracuda_Drive(device, true);
    }

    return isBarracudaDrive;
}

bool is_Seagate_Model_Number_Vendor_B(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    // we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (strcmp(device->drive_info.bridge_info.childDriveMN, "Nytro100 ZA128CM0001") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "Nytro100 ZA256CM0001") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "Nytro100 ZA512CM0001") == 0)
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (strcmp(device->drive_info.product_identification, "Nytro100 ZA128CM0001") == 0 ||
            strcmp(device->drive_info.product_identification, "Nytro100 ZA256CM0001") == 0 ||
            strcmp(device->drive_info.product_identification, "Nytro100 ZA512CM0001") == 0)
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_B(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_C(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    // we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        // Enterprise
        if (strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A0240") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A0480") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A0960") == 0 ||
            strcmp(device->drive_info.bridge_info.childDriveMN, "XF1230-1A1920") == 0)
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        // Enterprise
        if (strcmp(device->drive_info.product_identification, "XF1230-1A0240") == 0 ||
            strcmp(device->drive_info.product_identification, "XF1230-1A0480") == 0 ||
            strcmp(device->drive_info.product_identification, "XF1230-1A0960") == 0 ||
            strcmp(device->drive_info.product_identification, "XF1230-1A1920") == 0)
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_C(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_D(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    // we need to check the model number for the ones used on the vendor products
    if (USBchildDrive)
    {
        if (strncmp(device->drive_info.bridge_info.childDriveMN, "ST500HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST500HM001", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480HM001", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240HM001", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120HM000", 10) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120HM001", 10) == 0)
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (strncmp(device->drive_info.product_identification, "ST500HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST500HM001", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480HM001", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240HM001", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120HM000", 10) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120HM001", 10) == 0)
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_D(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_E(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    // we need to check the model number for the ones used on the vendor products
    if (USBchildDrive)
    {
        // Enterprise
        if (strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FP0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FP0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FN0001", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST100FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST120FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST200FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST240FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST400FN0021", 11) == 0 ||
            strncmp(device->drive_info.bridge_info.childDriveMN, "ST480FN0021", 11) == 0)
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        // Enterprise
        if (strncmp(device->drive_info.product_identification, "ST100FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FP0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST100FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FP0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST100FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FN0001", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST100FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST120FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST200FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST240FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST400FN0021", 11) == 0 ||
            strncmp(device->drive_info.product_identification, "ST480FN0021", 11) == 0)
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_E(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_SSD_PJ(tDevice* device, bool USBchildDrive)
{
    // These are some older enterprise SSDs that had some unique capabilities.
    bool        isSeagateVendor = false;
    const char* mnPtr           = &device->drive_info.product_identification[0];
    if (USBchildDrive)
    {
        mnPtr = &device->drive_info.bridge_info.childDriveMN[0];
    }
    if (safe_strlen(mnPtr))
    {
        if (/* check P models first */
            strcmp(mnPtr, "ST400KN0001") == 0 || strcmp(mnPtr, "ST800KN0001") == 0 ||
            strcmp(mnPtr, "ST1600KN0001") == 0 || strcmp(mnPtr, "ST480KN0001") == 0 ||
            strcmp(mnPtr, "ST960KN0001") == 0 || strcmp(mnPtr, "ST1920KN0001") == 0 ||
            strcmp(mnPtr, "ST400KN0011") == 0 || strcmp(mnPtr, "ST800KN0011") == 0 ||
            strcmp(mnPtr, "ST1600KN0011") == 0 || strcmp(mnPtr, "ST480KN0011") == 0 ||
            strcmp(mnPtr, "ST960KN0011") == 0 || strcmp(mnPtr, "ST1920KN0011") == 0 ||
            strcmp(mnPtr, "ST400KN0021") == 0 || strcmp(mnPtr, "ST800KN0021") == 0 ||
            strcmp(mnPtr, "ST480KN0021") == 0 || strcmp(mnPtr, "ST960KN0021") == 0 ||
            strcmp(mnPtr, "ST400KN0031") == 0 || strcmp(mnPtr, "ST800KN0031") == 0 ||
            strcmp(mnPtr, "ST480KN0031") == 0 || strcmp(mnPtr, "ST960KN0031") == 0 ||
            /* Now check J models */
            strcmp(mnPtr, "ST1000KN0002") == 0 || strcmp(mnPtr, "ST2000KN0002") == 0 ||
            strcmp(mnPtr, "ST4000KN0002") == 0 || strcmp(mnPtr, "ST1000KN0012") == 0 ||
            strcmp(mnPtr, "ST2000KN0012") == 0)
        {
            isSeagateVendor = true;
        }
        else if (!USBchildDrive)
        {
            isSeagateVendor = is_Seagate_Model_Number_Vendor_SSD_PJ(device, true);
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_F(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;
    // we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (((strstr(device->drive_info.bridge_info.childDriveMN, "ST") != M_NULLPTR) &&
             (strstr(device->drive_info.bridge_info.childDriveMN, "401") != M_NULLPTR)) // newer models
            || ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
                (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CM") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CV") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "MC") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "GM") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "MC") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "NM10002") ==
              7)) // Vendor_F and Vendor_G has same model# except for last part, so need more chars for comparison
            || ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
                (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "NX") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "ZG") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "ZH") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZP") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CV") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "YA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CM") == 7)) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "XA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "DC") == 7)) // older models
            /*The following are unique to some USB SSD products. These seem to all report UHFS00.1 for firmware rev, but
             * trying to only match UHFS for now These drives also seem to set "Seagate SSD" for the MN in the child
             * drive info. Since they are USB, these should only be checked like this as they are not being manufactured
             * any other way for now. - TJE
             */
            || ((strstr(device->drive_info.bridge_info.childDriveMN, "Seagate SSD") != M_NULLPTR) &&
                (strstr(device->drive_info.bridge_info.childDriveFW, "UHFS") != M_NULLPTR)))
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (((strstr(device->drive_info.product_identification, "ST") != M_NULLPTR) &&
             (strstr(device->drive_info.product_identification, "401") != M_NULLPTR)) // newer models
            || ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
                (find_last_occurrence_in_string(device->drive_info.product_identification, "CM") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "CV") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "MC") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "GM") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "MC") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "NM10002") ==
              7)) // Vendor_F and Vendor_G has same model# except for last part, so need more chars for comparison
            || ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
                (find_last_occurrence_in_string(device->drive_info.product_identification, "NX") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "ZH") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "ZG") == 7)) ||
            ((strstr(device->drive_info.product_identification, "ZP") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "CV") == 7)) ||
            ((strstr(device->drive_info.product_identification, "YA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "CM") == 7)) ||
            ((strstr(device->drive_info.product_identification, "XA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "DC") == 7)) // older models
        )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_F(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_G(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;

    // we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (((strstr(device->drive_info.bridge_info.childDriveMN, "XA") != M_NULLPTR) &&
             ((find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "LE") == 7) ||
              (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "ME") == 7))) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "NM10001") ==
              7)) // Vendor_F and Vendor_G has same model# except for last part, so need more chars for comparison
        )
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (((strstr(device->drive_info.product_identification, "XA") != M_NULLPTR) &&
             ((find_last_occurrence_in_string(device->drive_info.product_identification, "LE") == 7) ||
              (find_last_occurrence_in_string(device->drive_info.product_identification, "ME") == 7))) ||
            ((strstr(device->drive_info.product_identification, "ZA") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "NM10001") ==
              7)) // Vendor_F and Vendor_G has same model# except for last part, so need more chars for comparison
        )
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_G(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Model_Number_Vendor_H(tDevice* device, bool USBchildDrive)
{
    bool isSeagateVendor = false;

    // we need to check the model number for the ones used on the Vendor products
    if (USBchildDrive)
    {
        if (((strstr(device->drive_info.bridge_info.childDriveMN, "ZP") != M_NULLPTR) &&
             ((find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "CM") == 7) ||
              (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "GM") == 7))) ||
            ((strstr(device->drive_info.bridge_info.childDriveMN, "XP") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.bridge_info.childDriveMN, "DC") == 7)))
        {
            isSeagateVendor = true;
        }
    }
    else
    {
        if (((strstr(device->drive_info.product_identification, "ZP") != M_NULLPTR) &&
             ((find_last_occurrence_in_string(device->drive_info.product_identification, "CM") == 7) ||
              (find_last_occurrence_in_string(device->drive_info.product_identification, "GM") == 7))) ||
            ((strstr(device->drive_info.product_identification, "XP") != M_NULLPTR) &&
             (find_last_occurrence_in_string(device->drive_info.product_identification, "DC") == 7)))
        {
            isSeagateVendor = true;
        }
        if (!isSeagateVendor)
        {
            return (is_Seagate_Model_Number_Vendor_H(device, true));
        }
    }
    return isSeagateVendor;
}

bool is_Seagate_Vendor_K(tDevice* device)
{
    bool isVendorK = false;
    // LaCie Vendor ID
    if (is_LaCie(device) || is_Seagate_USB_Vendor_ID(device->drive_info.T10_vendor_ident))
    {
        // PID can be set to 1120, 1131, or 1132
        if (device->drive_info.adapter_info.vendorIDValid &&
            device->drive_info.adapter_info.vendorID == USB_Vendor_LaCie)
        {
            if (device->drive_info.adapter_info.productIDValid)
            {
                if (device->drive_info.adapter_info.productID == 0x1120 ||
                    device->drive_info.adapter_info.productID == 0x1131 ||
                    device->drive_info.adapter_info.productID == 0x1132)
                {
                    isVendorK = true;
                }
            }
        }
        else if (device->drive_info.adapter_info.vendorIDValid &&
                 device->drive_info.adapter_info.vendorID == USB_Vendor_Seagate_RSS)
        {
            if (device->drive_info.adapter_info.productIDValid)
            {
                if (device->drive_info.adapter_info.productID == 0x207C)
                {
                    isVendorK = true;
                }
            }
        }
        if (!isVendorK && (strcmp(device->drive_info.product_identification, "Rugged Mini SSD") == 0 ||
                           strcmp(device->drive_info.product_identification, "Ultra Touch SSD") == 0))
        {
            if (device->drive_info.bridge_info.isValid &&
                strcmp(device->drive_info.bridge_info.childDriveMN, "Seagate SSD") == 0)
            {
                // Known FWRevs
                if (strcmp(device->drive_info.bridge_info.childDriveFW, "W0519CR0") == 0 ||
                    strcmp(device->drive_info.bridge_info.childDriveFW, "W0918AR0") == 0 ||
                    strcmp(device->drive_info.bridge_info.childDriveFW, "W0918BR0") == 0 ||
                    strcmp(device->drive_info.bridge_info.childDriveFW, "W1005AM0") == 0)
                {
                    isVendorK = true;
                }
            }
        }
    }
    return isVendorK;
}

eSeagateFamily is_Seagate_Family(tDevice* device)
{
    eSeagateFamily isSeagateFamily = NON_SEAGATE;
    uint8_t        iter            = UINT8_C(0);
    uint8_t numChecks = UINT8_C(11); // maxtor, seagate, samsung, lacie, seagate-Vendor. As the family of seagate drives
                                     // expands, we will need to increase this and add new checks
    for (iter = 0; iter < numChecks && isSeagateFamily == NON_SEAGATE; iter++)
    {
        switch (iter)
        {
        case 0: // is_Samsung_HDD
            if (is_Samsung_HDD(device, false))
            {
                // If this is an NVMe drive, we need to check if it's Seagate since both Samsung HDD's and Seagate NVMe
                // drives use the same IEEE OUI
                if (device->drive_info.drive_type == NVME_DRIVE)
                {
                    if (is_Seagate(device, false))
                    {
                        isSeagateFamily = SEAGATE;
                    }
                }
                else
                {
                    isSeagateFamily = SAMSUNG;
                }
            }
            break;
        case 1: // is_Seagate
            if (is_Seagate(device, false))
            {
                isSeagateFamily = SEAGATE;
                if (is_Seagate_Model_Vendor_A(device))
                {
                    isSeagateFamily = SEAGATE_VENDOR_A;
                }
                else if (is_Seagate_Model_Number_Vendor_C(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_C;
                }
                else if (is_Seagate_Model_Number_Vendor_B(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_B;
                }
                else if (is_Seagate_Model_Number_Vendor_E(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_E;
                }
                else if (is_Seagate_Model_Number_Vendor_D(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_D;
                }
                else if (is_Seagate_Model_Number_Vendor_F(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_F;
                }
                else if (is_Seagate_Model_Number_Vendor_G(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_G;
                }
                else if (is_Seagate_Model_Number_Vendor_H(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_H;
                }
                else if (is_Seagate_Model_Number_Vendor_SSD_PJ(device, false))
                {
                    isSeagateFamily = SEAGATE_VENDOR_SSD_PJ;
                }
                else if (is_Seagate_Vendor_K(device))
                {
                    isSeagateFamily = SEAGATE_VENDOR_K;
                }
            }
            break;
        case 2: // is_Maxtor
            if (is_Maxtor(device, false))
            {
                isSeagateFamily = MAXTOR;
            }
            break;
        case 3: // is_Vendor_A
            if (is_Vendor_A(device, false))
            {
                // we aren't done yet! Need to check the model number to make sure it's a partnership product
                if (is_Seagate_Model_Vendor_A(device))
                {
                    isSeagateFamily = SEAGATE_VENDOR_A;
                }
                else
                {
                    isSeagateFamily = NON_SEAGATE;
                }
            }
            break;
        case 4: // is_LaCie
            if (is_LaCie(device))
            {
                isSeagateFamily = LACIE;
                // Special case for some USB SSDs. These can be recognized as vendor_f
                if (is_Seagate_Model_Number_Vendor_F(device, true))
                {
                    isSeagateFamily = SEAGATE_VENDOR_F;
                }
                else if (is_Seagate_Vendor_K(device))
                {
                    isSeagateFamily = SEAGATE_VENDOR_K;
                }
            }
            break;
        case 5: // is_Quantum
            if (is_Quantum(device, false))
            {
                isSeagateFamily = SEAGATE_QUANTUM;
            }
            break;
        case 6: // is_Connor
            if (is_Connor(device, false))
            {
                isSeagateFamily = SEAGATE_CONNER;
            }
            break;
        case 7: // is_Miniscribe
            // TODO: figure out what model numbers would be reported for ATA/IDE so we can detect them
            if (is_MiniScribe_VendorID(device))
            {
                isSeagateFamily = SEAGATE_MINISCRIBE;
            }
            break;
        case 8: // is_VENDOR_F
            if (is_Seagate_Model_Number_Vendor_F(device, false))
            {
                isSeagateFamily = SEAGATE_VENDOR_F;
            }
            break;
        case 9: // is_VENDOR_G
            if (is_Seagate_Model_Number_Vendor_G(device, false))
            {
                isSeagateFamily = SEAGATE_VENDOR_G;
            }
            break;
        case 10: // is_Vendor_H - NVMe SSDs
            if (is_Seagate_Model_Number_Vendor_H(device, false))
            {
                isSeagateFamily = SEAGATE_VENDOR_H;
            }
            break;
            // TODO: Add in CDC, DEC, & PrarieTek detection. Currently not in since these drives are even more rare than
            // the Conner and Miniscribe drives...
        default:
            break;
        }
    }
    return isSeagateFamily;
}

bool is_SSD(tDevice* device)
{
    bool isSSD = false;
    if (device->drive_info.media_type == MEDIA_NVM || device->drive_info.media_type == MEDIA_SSD)
    {
        isSSD = true;
    }
    else
    {
        isSSD = false;
    }
    return isSSD;
}

bool is_SATA(tDevice* device)
{
    bool isSata = false;
    if (device->drive_info.drive_type == ATA_DRIVE)
    {
        // Word 76 will be greater than zero, and never 0xFFFF on a SATA drive (bit 0 must be cleared to zero)
        if (is_ATA_Identify_Word_Valid_SATA(le16_to_host(device->drive_info.IdentifyData.ata.Word076)))
        {
            isSata = true;
        }
    }
    return isSata;
}

bool is_Sector_Size_Emulation_Active(tDevice* device)
{
    bool emulationActive = false;
    if (device->drive_info.bridge_info.isValid)
    {
        if (device->drive_info.deviceBlockSize != device->drive_info.bridge_info.childDeviceBlockSize)
        {
            emulationActive = true;
        }
        else
        {
            emulationActive = false;
        }
    }
    else
    {
        emulationActive = false;
    }
    return emulationActive;
}

eReturnValues calculate_Checksum(uint8_t* pBuf, uint32_t blockSize)
{
    uint8_t  checksum = UINT8_C(0);
    uint32_t counter  = UINT32_C(0);

    DISABLE_NONNULL_COMPARE
    if ((blockSize > LEGACY_DRIVE_SEC_SIZE) || (blockSize == UINT32_C(0)) || (pBuf == M_NULLPTR))
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE

    printf("%s: blksize %d, pBuf %p\n", __FUNCTION__, blockSize, C_CAST(void*, pBuf));

    for (counter = UINT32_C(0); counter < UINT32_C(511); counter++)
    {
        checksum = checksum + pBuf[counter];
    }
    pBuf[511] = (~checksum + 1);

    printf("%s: counter %d\n", __FUNCTION__, counter);

    return SUCCESS;
}

#define DATA_64K 65536
#define DATA_32K 32768

uint32_t get_Sector_Count_For_Read_Write(tDevice* device)
{
    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
    case RAID_INTERFACE:
    case NVME_INTERFACE:
        // set the sector count for a 64k transfer. This is most compatible (typically 128 sectors at a time-512B sector
        // size) - TJE
        return DATA_64K / device->drive_info.deviceBlockSize;
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        // set the sector count for a 32k transfer. This is most compatible on these external interface drives since
        // they typically have RAM limitations on the bridge chip - TJE
        return DATA_32K / device->drive_info.deviceBlockSize;
    default:
        return 64; // just set something in case they try to use this value but didn't check the return code from this
                   // function - TJE
    }
}

uint32_t get_Sector_Count_For_512B_Based_XFers(tDevice* device)
{
    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
    case RAID_INTERFACE:
    case NVME_INTERFACE:
        // set the sector count for a 64k transfer.
        return 128; // DATA_64K / 512;
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        // set the sector count for a 32k transfer. This is most compatible on these external interface drives since
        // they typically have RAM limitations on the bridge chip - TJE
        // fallthrough to set 32k
    default:
        return 64; // just set something in case they try to use this value but didn't check the return code from this
                   // function - TJE
    }
}

uint32_t get_Sector_Count_For_4096B_Based_XFers(tDevice* device)
{
    switch (device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
    case RAID_INTERFACE:
    case NVME_INTERFACE:
        // set the sector count for a 64k transfer.
        return 16; // DATA_64K / 4096;
    case USB_INTERFACE:
    case MMC_INTERFACE:
    case SD_INTERFACE:
    case IEEE_1394_INTERFACE:
        // set the sector count for a 32k transfer. This is most compatible on these external interface drives since
        // they typically have RAM limitations on the bridge chip - TJE
        // fallthrough to set 32k
        // DATA_32K / 4096;
    default:
        return 8; // just set something in case they try to use this value but didn't check the return code from this
                  // function - TJE
    }
}

void print_Command_Time(uint64_t timeInNanoSeconds)
{
    double  printTime   = C_CAST(double, timeInNanoSeconds);
    uint8_t unitCounter = UINT8_C(0);
    bool    breakLoop   = false;
    while (printTime > 1 && unitCounter <= 6)
    {
        switch (unitCounter)
        {
        case 6: // shouldn't get this far...
            break;
        case 5: // h to d
            if ((printTime / 24) < 1)
            {
                breakLoop = true;
            }
            break;
        case 4: // m to h
        case 3: // s to m
            if ((printTime / 60) < 1)
            {
                breakLoop = true;
            }
            break;
        case 0: // ns to us
        case 1: // us to ms
        case 2: // ms to s
        default:
            if ((printTime / 1000) < 1)
            {
                breakLoop = true;
            }
            break;
        }
        if (breakLoop)
        {
            break;
        }
        switch (unitCounter)
        {
        case 6: // shouldn't get this far...
            break;
        case 5: // h to d
            printTime /= 24;
            break;
        case 4: // m to h
        case 3: // s to m
            printTime /= 60;
            break;
        case 0: // ns to us
        case 1: // us to ms
        case 2: // ms to s
        default:
            printTime /= 1000;
            break;
        }
        if (unitCounter == 6)
        {
            break;
        }
        ++unitCounter;
    }
    printf("Command Time (");
    switch (unitCounter)
    {
    case 6: // we shouldn't get to a days value, but room for future large drives I guess...-TJE
        printf("d): ");
        break;
    case 5:
        printf("h): ");
        break;
    case 4:
        printf("m): ");
        break;
    case 3:
        printf("s): ");
        break;
    case 2:
        printf("ms): ");
        break;
    case 1:
        printf("us): ");
        break;
    case 0:
        printf("ns): ");
        break;
    default: // couldn't get a good conversion or something weird happened so show original nanoseconds.
        printf("ns): ");
        printTime = C_CAST(double, timeInNanoSeconds);
        break;
    }
    printf("%0.02f\n\n", printTime);
}

void print_Time(uint64_t timeInNanoSeconds)
{
    double  printTime   = C_CAST(double, timeInNanoSeconds);
    uint8_t unitCounter = UINT8_C(0);
    bool    breakLoop   = false;
    while (printTime > 1.0 && unitCounter <= UINT8_C(6))
    {
        switch (unitCounter)
        {
        case 6: // shouldn't get this far...
            break;
        case 5: // h to d
            if ((printTime / 24) < 1)
            {
                breakLoop = true;
            }
            break;
        case 4: // m to h
        case 3: // s to m
            if ((printTime / 60) < 1)
            {
                breakLoop = true;
            }
            break;
        case 0: // ns to us
        case 1: // us to ms
        case 2: // ms to s
        default:
            if ((printTime / 1000) < 1)
            {
                breakLoop = true;
            }
            break;
        }
        if (breakLoop)
        {
            break;
        }
        switch (unitCounter)
        {
        case 6: // shouldn't get this far...
            break;
        case 5: // h to d
            printTime /= 24;
            break;
        case 4: // m to h
        case 3: // s to m
            printTime /= 60;
            break;
        case 0: // ns to us
        case 1: // us to ms
        case 2: // ms to s
        default:
            printTime /= 1000;
            break;
        }
        if (unitCounter == 6)
        {
            break;
        }
        ++unitCounter;
    }
    printf(" (");
    switch (unitCounter)
    {
    case 6: // we shouldn't get to a days value, but room for future large drives I guess...-TJE
        printf("d): ");
        break;
    case 5:
        printf("h): ");
        break;
    case 4:
        printf("m): ");
        break;
    case 3:
        printf("s): ");
        break;
    case 2:
        printf("ms): ");
        break;
    case 1:
        printf("us): ");
        break;
    case 0:
        printf("ns): ");
        break;
    default: // couldn't get a good conversion or something weird happened so show original nanoseconds.
        printf("ns): ");
        printTime = C_CAST(double, timeInNanoSeconds);
        break;
    }
    printf("%0.02f\n", printTime);
}

uint64_t align_LBA(tDevice* device, uint64_t LBA)
{
    uint16_t logicalPerPhysical =
        C_CAST(uint16_t, device->drive_info.devicePhyBlockSize / device->drive_info.deviceBlockSize);
    if (logicalPerPhysical > 1)
    {
        // make sure the incoming LBA is aligned to the start of the physical sector it is in
        uint64_t tempLBA = LBA / C_CAST(uint64_t, logicalPerPhysical);
        tempLBA *= C_CAST(uint64_t, logicalPerPhysical);
        LBA = tempLBA - device->drive_info.sectorAlignment;
    }
    return LBA;
}

eReturnValues remove_Duplicate_Devices(tDevice*                 deviceList,
                                       volatile uint32_t*       numberOfDevices,
                                       removeDuplicateDriveType rmvDevFlag)
{
    volatile uint32_t i        = UINT32_C(0);
    volatile uint32_t j        = UINT32_C(0);
    bool              sameSlNo = false;
    eReturnValues     ret      = UNKNOWN;

    DISABLE_NONNULL_COMPARE
    if (deviceList == M_NULLPTR || numberOfDevices == M_NULLPTR)
    {
        return BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    /*
    Go through all the devices in the list.
    */
    for (i = UINT32_C(0); i < *numberOfDevices - UINT32_C(1); i++)
    {
        /*
        Go compare it to all the rest of the drives i + 1.
        */
        for (j = i + UINT32_C(1); j < *numberOfDevices; j++)

        {
#ifdef _DEBUG
            printf("%s --> For drive i : %d and j : %d \n", __FUNCTION__, i, j);
#endif
            ret      = SUCCESS;
            sameSlNo = false;

            if ((safe_strlen((deviceList + i)->drive_info.serialNumber) > SIZE_T_C(0)) &&
                (safe_strlen((deviceList + j)->drive_info.serialNumber) > SIZE_T_C(0)))
            {
                sameSlNo =
                    (strncmp((deviceList + i)->drive_info.serialNumber, (deviceList + j)->drive_info.serialNumber,
                             safe_strlen((deviceList + i)->drive_info.serialNumber)) == SIZE_T_C(0));
            }

            if (sameSlNo)
            {
#ifdef _DEBUG
                printf("We have same serial no \n");
#endif
#if defined(_WIN32)
                /* We are supporting csmi only - for now */
                if (rmvDevFlag.csmi != UINT8_C(0))
                {
                    if (is_CSMI_Device(deviceList + i))
                    {
                        ret |= remove_Device(deviceList, i, numberOfDevices);
                        i--;
                        j--;
                    }

                    if (is_CSMI_Device(deviceList + j))
                    {
                        ret |= remove_Device(deviceList, j, numberOfDevices);
                        j--;
                    }
                }
#else //!_WIN32
                M_USE_UNUSED(rmvDevFlag);

#endif //_WIN32
            }
        }
    }
    return ret;
}

eReturnValues remove_Device(tDevice* deviceList, uint32_t driveToRemoveIdx, volatile uint32_t* numberOfDevices)
{
    uint32_t      i   = UINT32_C(0);
    eReturnValues ret = FAILURE;

#ifdef _DEBUG
    printf("Removing Drive with index : %d \n", driveToRemoveIdx);
#endif

    if (driveToRemoveIdx >= *numberOfDevices)
    {
        return ret;
    }

    /*
     *  TODO - Use close_Handle() rather than free().
     **/
    if (is_CSMI_Device(deviceList + driveToRemoveIdx))
    {
        free((deviceList + driveToRemoveIdx)->raid_device);
    }

    for (i = driveToRemoveIdx; i < *numberOfDevices - UINT32_C(1); i++)
    {
        safe_memcpy((deviceList + i), sizeof(tDevice), (deviceList + i + UINT32_C(1)), sizeof(tDevice));
    }

    safe_memset((deviceList + i), sizeof(tDevice), 0, sizeof(tDevice));
    *numberOfDevices -= UINT32_C(1);
    ret = SUCCESS;

    return ret;
}

bool is_CSMI_Device(tDevice* device)
{
    bool csmiDevice = true;

#ifdef _DEBUG
    printf("friendly name : %s interface_type : %d raid_device : %" PRIXPTR "\n", device->os_info.friendlyName,
           device->drive_info.interface_type, C_CAST(uintptr_t, device->raid_device));
#endif

    csmiDevice = csmiDevice && (strncmp(device->os_info.friendlyName, "SCSI", SIZE_T_C(4)) == 0);
    csmiDevice = csmiDevice && (device->drive_info.interface_type == RAID_INTERFACE);
    csmiDevice = csmiDevice && (device->raid_device != M_NULLPTR);

#ifdef _DEBUG
    if (csmiDevice)
    {
        printf("This is a CSMI drive \n");
    }
    else
    {
        printf("This is not a CSMI drive \n");
    }
#endif
    return csmiDevice;
}

#if defined(_DEBUG)
// This function is more for debugging than anything else!
#    include <stddef.h>
void print_tDevice_Size(void)
{
    printf("==Device struct information==\n");
    printf("--structure sizes--\n");
    printf("tDevice = %zu\n", sizeof(tDevice));
    printf("\tversionBlock = %zu\n", sizeof(versionBlock));
    printf("\tOSDriveInfo = %zu\n", sizeof(OSDriveInfo));
    printf("\tdriveInfo = %zu\n", sizeof(driveInfo));
    printf("\tvoid* raid_device = %zu\n", sizeof(void*));
    printf("\tissue_io_func = %zu\n", sizeof(issue_io_func));
    printf("\teDiscoveryOptions = %zu\n", sizeof(uint64_t));
    printf("\teVerbosityLevels = %zu\n", sizeof(eVerbosityLevels));
    printf("\n--Important offsets--\n");
    printf("tDevice = 0\n");
    printf("\tversionBlock = %zu\n", offsetof(tDevice, sanity));
    printf("\tos_info = %zu\n", offsetof(tDevice, os_info));
    printf("\tdrive_info = %zu\n", offsetof(tDevice, drive_info));
    printf("\t\tIdentifyData = %zu\n", offsetof(tDevice, drive_info.IdentifyData));
    printf("\t\tATA Identify = %zu\n", offsetof(tDevice, drive_info.IdentifyData.ata));
    printf("\t\tNVMe CTRL ID = %zu\n", offsetof(tDevice, drive_info.IdentifyData.nvme.ctrl));
    printf("\t\tNVMe Namespace ID = %zu\n", offsetof(tDevice, drive_info.IdentifyData.nvme.ns));
    printf("\t\tscsiVpdData = %zu\n", offsetof(tDevice, drive_info.scsiVpdData));
    printf("\t\tlastCommandSenseData = %zu\n", offsetof(tDevice, drive_info.lastCommandSenseData));
    printf("\traid_device = %zu\n", offsetof(tDevice, raid_device));
    printf("\tissue_io = %zu\n", offsetof(tDevice, issue_io));
    printf("\tissue_nvme_io = %zu\n", offsetof(tDevice, issue_nvme_io));
    printf("\tdFlags = %zu\n", offsetof(tDevice, dFlags));
    printf("\tdeviceVerbosity = %zu\n", offsetof(tDevice, deviceVerbosity));
    printf("\n");
}
#endif //_DEBUG

bool is_Removable_Media(tDevice* device)
{
    bool    result = false;
    uint8_t scsiDevType;

    if (device->drive_info.interface_type == IDE_INTERFACE)
    {
        if (device->drive_info.drive_type == UNKNOWN_DRIVE || device->drive_info.drive_type == FLASH_DRIVE ||
            device->drive_info.drive_type == ATAPI_DRIVE || device->drive_info.media_type == MEDIA_OPTICAL ||
            device->drive_info.media_type == MEDIA_SSM_FLASH || device->drive_info.media_type == MEDIA_TAPE ||
            device->drive_info.media_type == MEDIA_UNKNOWN ||
            (is_ATA_Identify_Word_Valid(le16_to_host(device->drive_info.IdentifyData.ata.Word000)) &&
             le16_to_host(device->drive_info.IdentifyData.ata.Word000) & BIT7))
        {
            result = true;
        }
    }
    else if (device->drive_info.interface_type == SCSI_INTERFACE)
    {
        scsiDevType = device->drive_info.scsiVpdData.inquiryData[0] & 0x1F;

        if (scsiDevType == PERIPHERAL_DIRECT_ACCESS_BLOCK_DEVICE ||
            scsiDevType == PERIPHERAL_HOST_MANAGED_ZONED_BLOCK_DEVICE ||
            scsiDevType == PERIPHERAL_SEQUENTIAL_ACCESS_BLOCK_DEVICE ||
            scsiDevType == PERIPHERAL_STORAGE_ARRAY_CONTROLLER_DEVICE)
        {
            if (device->drive_info.scsiVpdData.inquiryData[1] & BIT7)
            {
                result = true;
            }
            else
            {
                result = false;
            }
        }
        else
        {
            result = true;
        }
    }
    if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
    {
        printf("Calling from file : %s function : %s line : %li \n", __FILE__, __FUNCTION__,
               C_CAST(long int, __LINE__));
        if (result)
        {
            printf("This is a Removable Media");
        }
        else
        {
            printf("This is not a Removable Media");
        }
    }
    return result;
}

static bool set_Seagate_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet                                   = false;
    device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x0888: // 0BC2 VID
        device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_NEC;
        passthroughHacksSet                                 = true;
        break;
    case 0x0500: // ST3750640A
        device->drive_info.passThroughHacks.passthroughType                       = PASSTHROUGH_NONE;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 5;
        device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData             = true;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset     = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength     = 24;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 122880;
        break;
    case 0x0501: //
        // revision 0002h
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_CYPRESS;
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 6;
        device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData           = true;
        // device->drive_info.passThroughHacks.scsiHacks.scsiInq
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly               = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported =
            true; // TODO: Cypress passthrough has a bit for UDMA mode, but didn't appear to work in testing.
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
        break;
    case 0x0502:
        // revision 0200h
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_TI;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly               = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported            = true;
        device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands =
            true; // mutliple mode commands don't work in passthrough.
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
        break;
    case 0x0503: // Seagate External Drive
        // revision 0240h
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_CYPRESS;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 6;
        device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData             = true;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDOffset      = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDLength      = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset     = 16;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength     = 14;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly               = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported =
            true; // TODO: Cypress passthrough has a bit for UDMA mode, but didn't appear to work in testing.
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536;
        break;
    case 0x1000: // FreeAgentGoSmall
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 9;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly               = true;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x200D: // Game Drive XBox
    case 0x200F: // Game Drive XBox
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2020: // Firecuda HDD
    case 0x2021: // Firecuda HDD
    case 0x2022: // Firecuda HDD hub
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x203C: // One Touch SSD
    case 0x203E: // One Touch SSD
    case 0x2013: // Expansion SSD
    case 0x202D: // Game Drive SSD
        // NOTE: This is a weird drive.
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 7;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly      = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 0;
        break;
    case 0x2030: // Expansion HDD
    case 0x2031: // Expansion HDD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2036: // Expansion HDD
    case 0x2037: // Expansion HDD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x204B: // FireCuda HDD
    case 0x204C: // FireCuda HDDv
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34; // test one showed 3...
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2060: // Game Drive PS
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 524288;
        break;
    case 0x2061: // Game Drive PS
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 524288;
        break;
    case 0x2064: // Ultra Touch HDD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 524288;
        break;
    case 0x2065: // Ultra Touch HDD
        passthroughHacksSet                                 = true;
        device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
        // device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        // device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 524288;
        break;
    case 0x207B: // Ultra Touch SSD
        // very similar to 0x207C, but slightly different firmware capabilities
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported               = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength =
            512; // NOTE: Test failed with zero, but setting 512 for single sectors
        break;
    case 0x207C: // Ultra Touch SSD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported               = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength =
            512; // NOTE: Test failed with zero, but setting 512 for single sectors
        break;
    case 0x2088: // Firecuda eSSD
        device->drive_info.passThroughHacks.passthroughType                     = NVME_PASSTHROUGH_REALTEK;
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages =
            true; // this supports some mode pages, but unable to test for subpages, so considering them not
                  // supported at this time -TJE
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        // NOTE: Security protocol is supported according to online web page. I do not have a device supporting
        // security to test against at this time to see if INC512 is needed/required -TJE
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
        // device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        // Check condition will always return RTFRs, HOWEVER on data transfers it returns empty data. Seems like
        // a device bug. Only use check condition for non-data commands-TJE NOTE: It may be interesting to try
        // an SCT command (write log) to see how check condition works, but at least with reads, this is a no-go
        device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough =
            true; // seems to make no difference whether this is used or not. Can switch this to "limited use"
                  // if we need to
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength   = 4096;
        device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe =
            true; // no way to tell at this point. Will need to make full determination in the fill_ATA_Info
                  // function
        device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands =
            true; // probably not needed, but after what I saw testing this, it can't hurt to set this
        // even though this product is only using NVMe drives, if the bridge had a SATA drive attached it would
        // work, so leaving the above hacks in place.
        device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength =
            262144; // 256KiB according to documentation.
        break;
    case 0x208E:
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly      = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 0;
        break;
    case 0x208F: // Expansion Free
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2100: // FreeAgent Go
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x2101: // FreeAgent Go
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x2120: // FreeAgent Go
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x2300: // Portable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 11;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x2320: // Expansion
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 11;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2321: // Expansion
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2322: // Expansion (Xbox drive uses this chip)
        // NOTE: Need to rerun a full test someday as we don't have full confirmation on supported read
        // commands...just assumed same as above chip for now.
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2330: // Portable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly             = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 512;
        break;
    case 0x2332: // Portable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 6;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 8192;
        break;
    case 0x2344: // portable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x2400: // Backup
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x2700: // BlackArmorDAS25
        passthroughHacksSet = true;
        // This particular bridge has lots of limitations
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 12;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly               = true;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough               = true;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength                             = 131072;
        break;
    case 0x3000: //???
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 16;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x3001: // FreeAgent
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x3010: // FreeAgent Pro
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 16;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 122880;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 122880;
        break;
    case 0x3300: // Desktop
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 17;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x3320: // Expansion Desk
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0x3330: // External
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 11;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable           = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly             = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 512;
        break;
    case 0x3332: // External
        // rev 0016h
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 31;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 1024;
        break;
    case 0x3340: // Expansion+ Desk
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 11;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x5020: // USB 2.0 Cable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 16;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0x5021: // FreeAgent GoFlex
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x5030: //???
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 16;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0x5031: // Freeagent GoFlex.
        passthroughHacksSet = true;
        // This particular bridge has lots of limitations
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly               = true;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA       = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65024;
        break;
    case 0x5060: // FreeAgent
        // rev 155h
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x5070: // FA GoFlex Desk
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 15;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable           = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        // device->drive_info.passThroughHacks.scsiHacks.noVPDPages = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x5071: // GoFlex Desk
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 16;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x50A5: // GoFlex Desk
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported               = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 524288;
        break;
    case 0x50A7: // GoFlex Desk
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 11;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable           = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages             = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported               = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly           = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 512;
        break;
    case 0x5130: // GoFlex Cable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 12;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x5170: // USB 3.0 Cable
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0x6126: // D3 Station
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x617A: // GoFlex Slim Mac
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 2;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0x61B5: // M3 Portable
        // rev 1402h
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 11;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xA003: // GoFlex Slim
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 12;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 209920;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly             = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 512;
        break;
    case 0xA013: // Backup+ RD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536; // some testing shows 524288
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 65536; // some testing shows 524288
        break;
    case 0xA014: // Slim BK
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 3;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0xA0A4: // GoFlex Desk
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU   = true;
        break;
    case 0xA313: // Wireless
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0xA314: // Wireless Plus
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.turfValue                             = 12;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAB00: // Slim BK
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 11;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAB01: // BUP Fast SSD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 8;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAB02: // Fast
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        // Hangs if sent a SCSI read with length set to 0.
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAB10: // BUP Slim SL
        // rev 938h
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 82944;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly             = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 8192;
        break;
    case 0xAB20: // Backup+ SL
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAB21: // Backup+ BL
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly             = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 512;
        break;
    case 0xAB24: // BUP Slim
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 33;
        // Hangs if sent a SCSI read with length set to 0.
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAB2A: // Fast HDD
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        // device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;//Not sure if this
        // actually worked when the test tool pumped this result out. Off for now.
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        // This device has 2 HDDs in a RAID inside it.
        // There is no known way to address each one separately.
        // Also, for whatever reason, it is not possible to read any logs from the drive you can get identify
        // data from. This may be a pre-production unit that was tested and other shipping products may work
        // differently. - TJE
        break;
    case 0xAB31: // Backup+  Desk
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly             = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 8192;
        break;
    case 0xAB38: // Backup+ Hub BK
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 524288;
        break;
    case 0xAA1A: // Another ID for firecuda gaming SSD.
    case 0xAA17: // FireCuda Gaming SSD
        // NOTE: Recommend a retest for this device to double check the hacks. Most are setup based on other
        // ASMedia bridge chip tests.
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = NVME_PASSTHROUGH_ASMEDIA;
        device->drive_info.drive_type                                           = NVME_DRIVE;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 33;
        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported =
            true; // set this so in the case an ATA passthrough command is attempted, it won't try this opcode
                  // since it can cause performance problems or crash the bridge
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        break;
    case 0xAB80: // One Touch Hub
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0xAC30: // BUP Slim
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    default: // unknown
        // setup some defaults that will most likely work for most current products
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage                = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable =
            false; // turning this off because this causes some products to report "Invalid operation code"
                   // instead of "invalid field in CDB"
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
        break;
    }
    if (passthroughHacksSet)
    {
        device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;
    }
    return passthroughHacksSet;
}

static bool set_LaCie_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet = false;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x1043: // blade runner (product ID shows this too)
        passthroughHacksSet = true;
        // TODO: This may use some old vendor unique passthrough to get ATA drive info, but haven't figured it
        // out yet - TJE
        device->drive_info.passThroughHacks.passthroughType                       = PASSTHROUGH_NONE;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 10;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        break;
    case 0x1053: // Porsche
        // surprisingly this one can handle zero length read/write correctly.
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 524288;
        break;
    case 0x1064: // RuggedKey
        // One other note: For some reason, the MaxLBA that is reported is DIFFERENT between read capacty 10 and
        // read capacity 16
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = PASSTHROUGH_NONE;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 13;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 524288;
        break;
    case 0x1065: //
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x1072: // Fuel
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x1091: // Rugged USB-C
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 11;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x10CD: // Rugged SSD
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = NVME_PASSTHROUGH_JMICRON;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 13;
        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported =
            true; // set this so in the case an ATA passthrough command is attempted, it won't try this opcode
                  // since it can cause performance problems or crash the bridge
        device->drive_info.drive_type                                             = NVME_DRIVE;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512  = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // NOTE: Add max passthrough transfer length hack set to 65536
        break;
    case 0x10EE: // Mobile SSD
    case 0x10EF: // Mobile SSD
        // NOTE: This is a weird drive
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 7;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly      = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 0;
        break;
    case 0x1105: // Mobile Drive
    case 0x1106: // Mobile Drive
    case 0x1107: // Mobile Secure
        // oddly I cannot get a security protocol in command to work on this device (1107) despite how it is
        // marketted. May need to recheck this in the future in case this was a mislabbelled drive
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    case 0x1120:
    case 0x1131:
    case 0x1132: // LaCie Rugged Mini SSD
        // NOTE: Only ATA passthrough for Identify and SMART are available.
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        // mode pages are supported, but sometimes it returns an incorrect page
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported =
            true; // This probably has more to do with only supporting Identify and SMART commands
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength =
            512; // setting single sectors since this only does ID and SMART
        break;
    default:
        // setup some defaults that will most likely work for most current products
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage                = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = false;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    }
    return passthroughHacksSet;
}

static bool set_Maxtor_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet = false;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x5020: // 5000DV
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = PASSTHROUGH_NONE;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData             = true;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDOffset      = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDLength      = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset     = 16;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength     = 16;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevOffset    = 32;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevLength    = 3;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        // device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536; //Unable to get a good test
        // result on this, so this is currently commented out. - TJE
        break;
    case 0x7310: // OneTouch
        // rev 0122h
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 10;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable           = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages            = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x7410: // Basics Desktop
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x7550: // BlackArmor
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly           = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 65536;
        break;
    default: // unknown
        break;
    }
    return passthroughHacksSet;
}

static bool set_JMicon_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet = false;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x0551: // USB 3.0 to SATA/PATA adapter
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 512512;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 130560;
        break;
    case 0x0562: // USB to NVMe adapter
        // Rev 204h
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = NVME_PASSTHROUGH_JMICRON;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 33;
        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported =
            true; // set this so in the case an ATA passthrough command is attempted, it won't try this opcode
                  // since it can cause performance problems or crash the bridge
        device->drive_info.drive_type                                             = NVME_DRIVE;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512  = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        device->drive_info.passThroughHacks.nvmePTHacks.maxTransferLength         = 65536;
        break;
    case 0x0567: // Github user reported USB to SATA adapter
        // rev 0x05h
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 11;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages            = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true; // this may need better
                                                                                        // validation
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength              = 1048576;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        device->drive_info.passThroughHacks.ataPTHacks.disableCheckCondition =
            true; // this does not crash the bridge, just useless as it's empty...just setting this as well for
                  // consistency since return reponse info works.
        device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty             = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 130560;
        break;
    case 0x0583: // USB to NVMe adapter
        // Rev 205h
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = NVME_PASSTHROUGH_JMICRON;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 33;
        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported =
            true; // set this so in the case an ATA passthrough command is attempted, it won't try this opcode
                  // since it can cause performance problems or crash the bridge
        device->drive_info.drive_type                                             = NVME_DRIVE;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512  = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // NOTE: Add max passthrough transfer length hack set to 65536
        break;
    case 0x2338: // Sabrent USB 2.0 to SATA/PATA. Only tested SATA.
        // NOTE: Some versions of this chip will NOT do SAT passthrough.
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 122880;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 122880;
        break;
    case 0x2339: // MiniD2 - NOTE: This has custom firmware. If other things use this chip, additional product
                 // verification will be necessary.
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 13;
        device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
        break;
    case 0x2567: // USB3 to SATA adapter box
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512  = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        // device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 130560;
        break;
    default: // unknown
        break;
    }
    return passthroughHacksSet;
}

static bool set_ASMedia_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet = false;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x2362: // USB to NVMe adapter
        // Tested 3 adapters. All report the exact same USB level information, but do in fact work differently
        // (slightly) The  difference seems to be only noticable in Inquiry Data
        // 1. PID: "ASM236X NVME" could use mode sense 6 and read 6 commands
        // 2. PID: "USB3.1 TO NVME" could NOT do these commands. So setting the lowest common set of rules.
        // If there are other ASMedia chips that vary in capabilities, then may need to adjust what is done in
        // here, or add a hack to check INQ data to finish setting up remaining hacks
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.passthroughType                     = NVME_PASSTHROUGH_ASMEDIA_BASIC;
        device->drive_info.drive_type                                           = NVME_DRIVE;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 33;
        device->drive_info.passThroughHacks.ataPTHacks.a1NeverSupported =
            true; // set this so in the case an ATA passthrough command is attempted, it won't try this opcode
                  // since it can cause performance problems or crash the bridge
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available                        = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10                             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16                             = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                                = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations                = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength                          = 524288;
        device->drive_info.passThroughHacks.nvmePTHacks.limitedPassthroughCapabilities           = true;
        device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.getLogPage      = true;
        device->drive_info.passThroughHacks.nvmePTHacks.limitedCommandsSupported.identifyGeneric = true;
        break;
    case 0x5106: // Seen in ThermalTake BlackX 5G
        // Results are for revision 0001h
        // Does not seem to handle drives with a 4k logical sector size well.
        // 7/29/2021 - retested manually. TPSIU seems to work better only for identify. UDMA mode definitely
        // doesn't work.
        //             but DMA mode works fine. Remaining hacks seem appropriate overall
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 15;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages               = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
        device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength =
            122880; // This seems to vary with each time this device is tested
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU               = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly =
            true; // NOTE: Can use PIO read log ext just fine, but DMA is the problem. THis still works with
                  // SMART though, but a future hack may be needed.-TJE
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength         = 524288;
        break;
    case 0x55AA: // ASMT 2105
        device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                       = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560;
        break;
    default: // unknown
        break;
    }
    return passthroughHacksSet;
}

static bool set_Realtek_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet = false;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x9210: // USB to SATA OR USB to NVMe
        // This chip is interesting.
        // SATA rules are straight forward.
        // When using with an NVMe device, the same SCSI rules apply, however it will also respond to SAT ATA
        // identify with NVMe MN, SN, FW being returned, but nothing else is valid. So somehow this needs to set
        // a "possibly NVMe" flag somewhere. this is being setup as through it is an ATA drive for now since
        // there is not other good way to figure out what it is at this point.
        device->drive_info.passThroughHacks.passthroughType                     = ATA_PASSTHROUGH_SAT;
        passthroughHacksSet                                                     = true;
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
        device->drive_info.passThroughHacks.turfValue                           = 34;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogSubPages             = true;
        device->drive_info.passThroughHacks.scsiHacks.noModeSubPages =
            true; // this supports some mode pages, but unable to test for subpages, so considering them not
                  // supported at this time -TJE
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        // NOTE: Security protocol is supported according to online web page. I do not have a device supporting
        // security to test against at this time to see if INC512 is needed/required -TJE
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported = true;
        // device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512 = true;
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 524288;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        // Check condition will always return RTFRs, HOWEVER on data transfers it returns empty data. Seems like
        // a device bug. Only use check condition for non-data commands-TJE NOTE: It may be interesting to try
        // an SCT command (write log) to see how check condition works, but at least with reads, this is a no-go
        device->drive_info.passThroughHacks.scsiHacks.noSATVPDPage = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough =
            true; // seems to make no difference whether this is used or not. Can switch this to "limited use"
                  // if we need to
        device->drive_info.passThroughHacks.ataPTHacks.singleSectorPIOOnly = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength   = 4096;
        device->drive_info.passThroughHacks.ataPTHacks.possilbyEmulatedNVMe =
            true; // no way to tell at this point. Will need to make full determination in the fill_ATA_Info
                  // function
        device->drive_info.passThroughHacks.ataPTHacks.noMultipleModeCommands =
            true; // probably not needed, but after what I saw testing this, it can't hurt to set this
        break;
    default: // unknown;
        break;
    }
    return passthroughHacksSet;
}

static bool set_Samsung_USB_Hacks_By_PID(tDevice* device)
{
    bool passthroughHacksSet = false;
    switch (device->drive_info.adapter_info.productID)
    {
    case 0x1F05: // S2 Portable
        // based on revision 0000h
        passthroughHacksSet                                 = true;
        device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported                   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR                   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough               = true;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength                             = 65536; // Bytes
        // set SCSI hacks
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536; // bytes
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = false;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData             = true;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDOffset      = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.vendorIDLength      = 8;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset     = 16;
        device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength     = 11;
        // Serial number is not reported in inquiry data or any other known location
        break;
    case 0x5F12: // Story Station
        passthroughHacksSet = true;
        // hacks based on revision 1302h. Not sure if revision level filter is needed right now
        // Set ATA passthrough hacks
        device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 130560; // Bytes
        // set SCSI hacks
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288; // bytes
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 33;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
        device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512  = true;
        break;
    case 0x6093: // S2 portable 3
        passthroughHacksSet = true;
        // based on revision 0100h
        device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported                   = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR                   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough               = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable                 = true;
        device->drive_info.passThroughHacks.ataPTHacks.smartCommandTransportWithSMARTLogCommandsOnly = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength                             = 524288; // Bytes
        // set SCSI hacks
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288; // bytes
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 14;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
        break;
    case 0x61C3: // P3 Portable
        passthroughHacksSet = true;
        // based on revision 0E00h
        device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
        // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
        device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit   = true;
        device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
        device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 523776; // Bytes
        // set SCSI hacks
        device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288; // bytes
        device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
        device->drive_info.passThroughHacks.turfValue                             = 16;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = false;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
        device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
        device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
        device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
        break;
    default: // unknown
        break;
    }
    return passthroughHacksSet;
}

// https://usb-ids.gowdy.us/
// http://www.linux-usb.org/usb.ids
static bool set_USB_Passthrough_Hacks_By_PID_and_VID(tDevice* device)
{
    bool passthroughHacksSet = false;
    // only change the ATA Passthrough type for USB (for legacy USB bridges)
    if (device->drive_info.interface_type == USB_INTERFACE)
    {
        // Most USB bridges are SAT so they'll probably fall into the default cases and issue an identify command for
        // SAT
        switch (device->drive_info.adapter_info.vendorID)
        {
        case USB_Vendor_Seagate: // 0477
            // switch (device->drive_info.adapter_info.productID)
            //{
            // default: //unknown
            //     break;
            // }
            break;
        case USB_Vendor_Seagate_RSS: // 0BC2
            passthroughHacksSet = set_Seagate_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_LaCie: // 059F
            passthroughHacksSet = set_LaCie_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_Maxtor: // 0D49
            passthroughHacksSet = set_Maxtor_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_Oxford: // 0928
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0008: // NOTE: This should be retested for full hacks to improve performance.
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet                                 = true;
                break;
            default: // unknown
                break;
            }
            break;
        case USB_Vendor_4G_Systems_GmbH: // 1955
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0102: // Generic Frys
                passthroughHacksSet                                                       = true;
                device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
                device->drive_info.passThroughHacks.turfValue                             = 17;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
                // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
                break;
            default: // unknown
                break;
            }
            break;
        case USB_Vendor_Initio: // 13FD
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1340: // Seen in ThermalTake BlackX
                // Results are for revision 0210h
                // NOTE: This device doesn't allow for large LBAs, so it is limited on the SCSI read/write commands to a
                // 32bit LBA.
                //       Using a 12TB drive, this reports 483.72GB....so may need an additional hack at some point to
                //       force passthrough read/write commands on a device like this.
                device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
                passthroughHacksSet                                                       = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
                device->drive_info.passThroughHacks.turfValue                             = 12;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable             = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
                if (device->drive_info.adapter_info.revision == 0x0202)
                {
                    device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16               = true;
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                    device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                }
                // device->drive_info.passThroughHacks.ataPTHacks.useA1SATPassthroughWheneverPossible = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable   = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported                 = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 65536;
                break;
            default: // unknown
                break;
            }
            break;
        case USB_Vendor_ASMedia: // 174C
            passthroughHacksSet = set_ASMedia_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_JMicron: // 152D
            passthroughHacksSet = set_JMicon_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_Realtek: // 0BDAh
            passthroughHacksSet = set_Realtek_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_Samsung: // 04E8
            passthroughHacksSet = set_Samsung_USB_Hacks_By_PID(device);
            break;
        case USB_Vendor_Silicon_Motion: // 090C
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1000: // USB DISK - Rev1100
                // Don't set a passthrough type! This is a USB flash memory, that responds to one of the legacy command
                // requests and it will break it!
                device->drive_info.media_type                       = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                passthroughHacksSet                                 = true;
                // this device also supports the device identification VPD page even though the list of pages doesn't
                // work.
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                           = 15;
                device->drive_info.passThroughHacks.scsiHacks.unitSNAvailable           = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages                = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes          = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes       = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_ChipsBank: // 1E3D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x2093: // v3.1.0.4
                device->drive_info.media_type                                             = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType                       = PASSTHROUGH_NONE;
                passthroughHacksSet                                                       = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
                device->drive_info.passThroughHacks.turfValue                             = 10;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolWithInc512  = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Alcor_Micro_Corp: // 058F
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x6387: // flash drive - Rev 0103
            case 0x1234: // flash drive
            case 0x9380: // flash drive
            case 0x9381: // flash drive
            case 0x9382: // flash drive
                device->drive_info.media_type                       = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughHacksSet                                 = true;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Integrated_Techonology_Express_Inc: // 048D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1172: // flash drive
            case 0x1176: // flash drive - rev 0100
                device->drive_info.media_type                       = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType = ATA_PASSTHROUGH_UNKNOWN;
                passthroughHacksSet                                 = true;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_SanDisk_Corp: // 0781
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x5575: // Cruzer Glide
                passthroughHacksSet                                                       = true;
                device->drive_info.media_type                                             = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType                       = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
                device->drive_info.passThroughHacks.turfValue                             = 16;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
                break;
            case 0x5580: // Extreme
                passthroughHacksSet = true;
                device->drive_info.media_type =
                    MEDIA_SSM_FLASH; // Leaving this as flash since it is a flash drive/thumb drive, but this is an odd
                                     // one that seems to do SAT commands.
                device->drive_info.passThroughHacks.passthroughType                          = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure      = true;
                device->drive_info.passThroughHacks.turfValue                                = 15;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available            = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6                  = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10                 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12                 = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                     = true;
                device->drive_info.passThroughHacks.scsiHacks.reportAllOpCodes               = true;
                device->drive_info.passThroughHacks.scsiHacks.reportSingleOpCodes            = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported      = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength              = 1048576;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported   = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR   = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseIgnoreExtendBit = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseDMAInsteadOfUDMA     = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength             = 1048576;
                break;
            case 0x5583: // Ultra Fit
                passthroughHacksSet                                                       = true;
                device->drive_info.media_type                                             = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType                       = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure   = true;
                device->drive_info.passThroughHacks.turfValue                             = 14;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12              = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16              = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Kingston: // 13FE
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x3600: // Patriot Memory PMA
                passthroughHacksSet                                                     = true;
                device->drive_info.media_type                                           = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType                     = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                           = 4;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData           = true;
                // vendor ID is all spaces, so skipping it
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 16;
                // Guessing that the "PMA" value is a revision number of some kind.
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevOffset    = 32;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevLength    = 3;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.securityProtocolSupported   = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Phison: // 0D7D
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x1300: // USB DISK 2.0
                passthroughHacksSet                                                     = true;
                device->drive_info.media_type                                           = MEDIA_SSM_FLASH;
                device->drive_info.passThroughHacks.passthroughType                     = PASSTHROUGH_NONE;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                           = 33;
                device->drive_info.passThroughHacks.scsiHacks.preSCSI2InqData           = true;
                // vendor ID is all spaces, so skipping it
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDOffset = 16;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productIDLength = 16;
                // Guessing that the "PMA" value is a revision number of some kind.
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevOffset    = 32;
                device->drive_info.passThroughHacks.scsiHacks.scsiInq.productRevLength    = 3;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.noVPDPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages              = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 65536;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Symwave: // 1CA1
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x18AE: // rev 0852
                // This is a USB to SAS adapter. Not everything works as it is targetted at read/write and other basic
                // informatio only. If sent commands it doesn't understand, this struggles. Very basic information is
                // set here in order to keep this simple. The main reason is because most capabilties will depend on the
                // Attached SAS device more than the USB adapter itself.
                // The exception to that rule is transfer size. Disabling ATA passthrough test also helps
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.passthroughType =
                    PASSTHROUGH_NONE; // This disabled ATA passthrough which this devices doesn't support anyways
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 1048576;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure =
                    true; // This may not actually help this device. This hack is usually used to clear error conditions
                          // by sending a well known, easy command to clear previous SAT translation issues, but in this
                          // case, this may not be helpful
                device->drive_info.passThroughHacks.turfValue = 17;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Via_Labs:
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0715: // rev 0148
                passthroughHacksSet                                                            = true;
                device->drive_info.passThroughHacks.passthroughType                            = ATA_PASSTHROUGH_SAT;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported     = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR     = true;
                device->drive_info.passThroughHacks.ataPTHacks.alwaysUseTPSIUForSATPassthrough = true;
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength               = 292352; // Bytes
                // set SCSI hacks
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength         = 524288; // bytes
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                           = 10;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
                device->drive_info.passThroughHacks.scsiHacks.noModeSubPages            = true;
                // NOTE: Does not handle zero length read/write commands
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                break;
            default:
                break;
            }
            break;
        case USB_Vendor_Kingston_Generic:
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x7777: // USB to M.2 SATA adapter...crashes without any recovery very easily.
                // This does NOT like TPSIU for ANYTHING but identify and crashes if using UDMA mode.
                passthroughHacksSet                                                     = true;
                device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                           = 34;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available       = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6             = false;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10            = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw12            = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw16            = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                = true;
                // NOTE: Does not handle zero length read/write commands
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength           = 524288; // bytes
                device->drive_info.passThroughHacks.passthroughType                       = ATA_PASSTHROUGH_SAT;
                // A1 is always supported
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoSupported = true;
                device->drive_info.passThroughHacks.ataPTHacks.returnResponseInfoNeedsTDIR = true;
                device->drive_info.passThroughHacks.ataPTHacks.limitedUseTPSIU =
                    true; // Identify only or it crashes the adapter firmware
                device->drive_info.passThroughHacks.ataPTHacks.alwaysCheckConditionAvailable = true;
                device->drive_info.passThroughHacks.ataPTHacks.dmaNotSupported =
                    true; // Attempting any DMA passthrough fails for invalid field in CDB. UDMA mode crashes the
                          // firmware entirely.
                device->drive_info.passThroughHacks.ataPTHacks.maxTransferLength = 125440; // Bytes
                device->drive_info.passThroughHacks.ataPTHacks.checkConditionEmpty =
                    true; // returns empty sense data in return for check condition, when a result is expected to come
                          // back
                break;
            default:
                break;
            }
            break;
        default: // unknown
            break;
        }
    }
    return passthroughHacksSet;
}

// Vendor ID's, or OUI's, can be found here:
// https://regauth.standards.ieee.org/standards-ra-web/pub/view.html#registries
static bool set_IEEE1394_Passthrough_Hacks_By_PID_and_VID(tDevice* device)
{
    bool passthroughHacksSet = false;
    if (device->drive_info.interface_type == IEEE_1394_INTERFACE)
    {
        // It is unknown if any IEEE 1394 devices support any ATA passthrough.
        // Some devices had both USB and IEEE1394 interfaces and one may have passthrough while the other does not. They
        // may even be different.
        switch (device->drive_info.adapter_info.vendorID)
        {
        case IEEE1394_Vendor_Maxtor: // 0010B9
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x005000: // 5000DV
                passthroughHacksSet                                 = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                // device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                             = 6;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                // device->drive_info.passThroughHacks.scsiHacks.maxTransferLength = 65536; //Unable to get a good test
                // result on this, so this is currently commented out. - TJE
                break;
            default:
                break;
            }
            break;
        case 0x000BC2: // This vendor ID doesn't make sense for the product that was tested!!!
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x000000: // Seagate external drive with USB and 1394. USB mode has passthrough, but that doesn't work
                           // in 1394. Likely 2 different chips
                passthroughHacksSet                                 = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                // device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                             = 5;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw6               = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength =
                    524288; // Unable to get a good test result on this, so this is currently commented out. - TJE
                break;
            default:
                break;
            }
            break;
        case 0x000500: // This vendor ID doesn't make sense for the product that was tested!!!
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x000001: // Seagate external drive with USB and 1394. USB mode has passthrough, but that doesn't work
                           // in 1394. Likely 2 different chips
                passthroughHacksSet                                 = true;
                device->drive_info.passThroughHacks.passthroughType = PASSTHROUGH_NONE;
                // device->drive_info.passThroughHacks.testUnitReadyAfterAnyCommandFailure = true;
                device->drive_info.passThroughHacks.turfValue                             = 4;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.available         = true;
                device->drive_info.passThroughHacks.scsiHacks.readWrite.rw10              = true;
                device->drive_info.passThroughHacks.scsiHacks.noLogPages                  = true;
                device->drive_info.passThroughHacks.scsiHacks.noModePages                 = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations = true;
                device->drive_info.passThroughHacks.scsiHacks.maxTransferLength =
                    524288; // Unable to get a good test result on this, so this is currently commented out. - TJE
                break;
            default:
                break;
            }
            break;
        case IEEE1394_Vendor_Seagate: // 002037
        case IEEE1394_Vendor_Quantum: // 00E09E
        default:
            break;
        }
    }
    return passthroughHacksSet;
}

// possible places to lookup vendor IDs:
// https://pcisig.com/membership/member-companies?combine=&order=field_vendor_id&sort=asc
// https://www.pcilookup.com/
// https://pci-ids.ucw.cz/
static bool set_PCI_Passthrough_Hacks_By_PID_and_VID(tDevice* device)
{
    bool passthroughHacksSet = false;
    // Currently this is setting SCSI/ATA hacks as needed for the devices below.
    //       This may be ok in general, but may get confusing since this isn't necessarily the target drive having an
    //       issue, but the hardware controller that a drive is attached to having some other functionality. The hacks
    //       list started as specific to USB, but needs to handle some things for PCIe controllers too.
    if (device->drive_info.adapter_info.vendorIDValid)
    {
        switch (device->drive_info.adapter_info.vendorID)
        {
        case PCI_VENDOR_RED_HAT:
            // note: these all appear to be virtual devices from what I can find online.
            // Only handling those that this software is likely to encounter for now.
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x0008: // reported in openSeaChest#47
                // TODO: This seemed to only allow identify and smart through. May need a more thorough test for this in
                // the future, but for now listing the ata28bit only hack
                //       it is possible that even withing the A1h CDB other ATA commands will also be filtered and
                //       additional hacks will be required.
                passthroughHacksSet                                         = true;
                device->drive_info.passThroughHacks.ataPTHacks.ata28BitOnly = true;
                break;
            default:
                break;
            }
            break;
        case PCI_VENDOR_MICROCHIP: // PMC?
            switch (device->drive_info.adapter_info.productID)
            {
            case 0x8070:
                // reported from a Linux box running the following driver:
                //  driver info--
                //     driver name : pm80xx
                //     driver version string : 0.1.37 / 1.3.01 - 1 - bn_1.
                //     major ver : 0
                //     minor ver : 1
                //     revision : 37
                // Unknown if these hacks are specific to the controller or the driver, but for now applying everywhere
                // when it is detected
                passthroughHacksSet = true;
                device->drive_info.passThroughHacks.scsiHacks.noReportSupportedOperations =
                    true; // This command seems to abort or cause an internal error for some reason, so turning it off.
                // none of the report supported operation codes modes work on this controller.
                // I do not have one of these to do a full test so this workaround list is definitely incomplete.
                device->drive_info.passThroughHacks.scsiHacks.writeBufferNoDeferredDownload = true;
                // This controller/driver absolutely monitors the mode field in write buffer and blocks the deferred
                // download modes, but regular old segmented works fine.-TJE
                break;
            default:
                break;
            }
            break;
        // case PCI_VENDOR_ADAPTEC_2:
        //     switch (device->drive_info.adapter_info.productID)
        //     {
        //     case 0x028B://6-series SAS
        //     case 0x028C://7-series SAS
        //     case 0x028D://8-series SAS
        //         //Adaptec controllers in 8 series definitely require ATA passthrough commands to use DMA mode instead
        //         of UDMA mode. Have seen this on the ASR8405
        //         //Right now this is handled with retries in other parts of the code
        //         //We should run a more thorough test before turning on hacks here to make sure it covers all
        //         capabilities.-TJE
        //         //Another known SAT thing is that soft-reset works, but hard reset does not.
        //         break;
        //     }
        //     break;
        default:
            // unknown vendor ID or nothing necessary
            break;
        }
    }
    return passthroughHacksSet;
}

bool setup_Passthrough_Hacks_By_ID(tDevice* device)
{
    bool success = false;
    // need to do things different for USB vs PCI vs IEEE1394, etc
    switch (device->drive_info.adapter_info.infoType)
    {
    case ADAPTER_INFO_USB:
        success = set_USB_Passthrough_Hacks_By_PID_and_VID(device);
        break;
    case ADAPTER_INFO_PCI:
        success = set_PCI_Passthrough_Hacks_By_PID_and_VID(device);
        break;
    case ADAPTER_INFO_IEEE1394:
        success = set_IEEE1394_Passthrough_Hacks_By_PID_and_VID(device);
        break;
    default:
        break;
    }
    if (success)
    {
        device->drive_info.passThroughHacks.hacksSetByReportedID = true;
    }
    else
    {
        // The OS didn't report the vendor and product identifiers needed to set hacks this way. Anything else will be
        // done based on known Product matches or trial and error
    }
    return success;
}
