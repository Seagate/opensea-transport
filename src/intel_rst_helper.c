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
#if defined(ENABLE_INTEL_RST)
#    include "bit_manip.h"
#    include "code_attributes.h"
#    include "common_types.h"
#    include "error_translation.h"
#    include "io_utils.h"
#    include "math_utils.h"
#    include "memory_safety.h"
#    include "precision_timer.h"
#    include "string_utils.h"
#    include "type_conversion.h"

#    include "intel_rst_defs.h"
#    include "intel_rst_helper.h"
#    include "sntl_helper.h"
#    include <stddef.h>
#    if defined(ENABLE_CSMI)
#        include "csmi_helper.h"
#    endif

#    if defined(_DEBUG) && !defined(INTRST_DEBUG)
#        define INTRST_DEBUG
#    endif //_DEBUG && !INTRST_DEBUG

static void print_Intel_SRB_Status(uint32_t srbStatus)
{
    printf("SRB Status: ");
    switch (srbStatus)
    {
    case INTEL_SRB_STATUS_PENDING:
        printf("Pending\n");
        break;
    case INTEL_SRB_STATUS_SUCCESS:
        printf("Success\n");
        break;
    case INTEL_SRB_STATUS_ABORTED:
        printf("Aborted\n");
        break;
    case INTEL_SRB_STATUS_ABORT_FAILED:
        printf("Abort Failed\n");
        break;
    case INTEL_SRB_STATUS_ERROR:
        printf("Error\n");
        break;
    case INTEL_SRB_STATUS_BUSY:
        printf("Busy\n");
        break;
    case INTEL_SRB_STATUS_INVALID_REQUEST:
        printf("Invalid Request\n");
        break;
    case INTEL_SRB_STATUS_INVALID_PATH_ID:
        printf("Invalid Path ID\n");
        break;
    case INTEL_SRB_STATUS_NO_DEVICE:
        printf("No Device\n");
        break;
    case INTEL_SRB_STATUS_TIMEOUT:
        printf("Timeout\n");
        break;
    case INTEL_SRB_STATUS_SELECTION_TIMEOUT:
        printf("Selection Timeout\n");
        break;
    case INTEL_SRB_STATUS_COMMAND_TIMEOUT:
        printf("Command Timeout\n");
        break;
    case INTEL_SRB_STATUS_MESSAGE_REJECTED:
        printf("Message Rejected\n");
        break;
    case INTEL_SRB_STATUS_BUS_RESET:
        printf("Bus Reset\n");
        break;
    case INTEL_SRB_STATUS_PARITY_ERROR:
        printf("Parity Error\n");
        break;
    case INTEL_SRB_STATUS_REQUEST_SENSE_FAILED:
        printf("Request Sense Failed\n");
        break;
    case INTEL_SRB_STATUS_NO_HBA:
        printf("No HBA\n");
        break;
    case INTEL_SRB_STATUS_DATA_OVERRUN:
        printf("Data Overrun\n");
        break;
    case INTEL_SRB_STATUS_UNEXPECTED_BUS_FREE:
        printf("Unexpected Bus Free\n");
        break;
    case INTEL_SRB_STATUS_PHASE_SEQUENCE_FAILURE:
        printf("Phase Sequence Failure\n");
        break;
    case INTEL_SRB_STATUS_BAD_SRB_BLOCK_LENGTH:
        printf("Bad SRB Block Length\n");
        break;
    case INTEL_SRB_STATUS_REQUEST_FLUSHED:
        printf("Request Flushed\n");
        break;
    case INTEL_SRB_STATUS_INVALID_LUN:
        printf("Invalid LUN\n");
        break;
    case INTEL_SRB_STATUS_INVALID_TARGET_ID:
        printf("Invalid Target ID\n");
        break;
    case INTEL_SRB_STATUS_BAD_FUNCTION:
        printf("Bad Function\n");
        break;
    case INTEL_SRB_STATUS_ERROR_RECOVERY:
        printf("Error Recovery\n");
        break;
    case INTEL_SRB_STATUS_NOT_POWERED:
        printf("Not Powered\n");
        break;
    case INTEL_SRB_STATUS_LINK_DOWN:
        printf("Link Down\n");
        break;
    case INTEL_SRB_STATUS_INSUFFICIENT_RESOURCES:
        printf("Insufficient Resources\n");
        break;
    case INTEL_SRB_STATUS_THROTTLED_REQUEST:
        printf("Throttled Request\n");
        break;
    case INTEL_SRB_STATUS_INVALID_PARAMETER:
        printf("Invalid Parameter\n");
        break;
    default:
        printf("Unknown SRB Status - %" PRIX32 "\n", srbStatus);
        break;
    }
}

static void printf_Intel_Firmware_SRB_Status(uint32_t srbStatus)
{
    switch (srbStatus)
    {
    case INTEL_FIRMWARE_STATUS_SUCCESS:
        printf("Success\n");
        break;
    case INTEL_FIRMWARE_STATUS_ERROR:
        printf("Error\n");
        break;
    case INTEL_FIRMWARE_STATUS_ILLEGAL_REQUEST:
        printf("Illegal Request\n");
        break;
    case INTEL_FIRMWARE_STATUS_INVALID_PARAMETER:
        printf("Invalid Parameter\n");
        break;
    case INTEL_FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
        printf("Input Buffer Too Big\n");
        break;
    case INTEL_FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
        printf("Output Buffer Too Small\n");
        break;
    case INTEL_FIRMWARE_STATUS_INVALID_SLOT:
        printf("Invalid Slot\n");
        break;
    case INTEL_FIRMWARE_STATUS_INVALID_IMAGE:
        printf("Invalid Image\n");
        break;
    case INTEL_FIRMWARE_STATUS_CONTROLLER_ERROR:
        printf("Controller Error\n");
        break;
    case INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
        printf("Power Cycle Required\n");
        break;
    case INTEL_FIRMWARE_STATUS_DEVICE_ERROR:
        printf("Device Error\n");
        break;
    case INTEL_FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
        printf("Interface CRC Error\n");
        break;
    case INTEL_FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
        printf("Uncorrectable Data Error\n");
        break;
    case INTEL_FIRMWARE_STATUS_MEDIA_CHANGE:
        printf("Media Change\n");
        break;
    case INTEL_FIRMWARE_STATUS_ID_NOT_FOUND:
        printf("ID Not Found\n");
        break;
    case INTEL_FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
        printf("Media Change Request\n");
        break;
    case INTEL_FIRMWARE_STATUS_COMMAND_ABORT:
        printf("Command Abort\n");
        break;
    case INTEL_FIRMWARE_STATUS_END_OF_MEDIA:
        printf("End of Media\n");
        break;
    case INTEL_FIRMWARE_STATUS_ILLEGAL_LENGTH:
        printf("Illegal Length\n");
        break;
    default:
        printf("Unknown SRB Status - %" PRIX32 "\n", srbStatus);
        break;
    }
}

static M_INLINE void safe_free_irst_raid_fw_buffer(IOCTL_RAID_FIRMWARE_BUFFER** buf)
{
    safe_free_aligned_core(M_REINTERPRET_CAST(void**, buf));
}

// generic function to handle taking in the various RAID FW Requests to keep code from being dumplicated
static eReturnValues intel_RAID_FW_Request(tDevice*  device,
                                           void*     ptrDataRequest,
                                           uint32_t  dataRequestLength,
                                           uint32_t  timeoutSeconds,
                                           uint32_t  intelFirmwareFunction,
                                           uint32_t  intelFirmwareFlags,
                                           bool      readFirmwareInfo,
                                           uint32_t* returnCode)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (device)
    {
        size_t                      allocationSize = sizeof(IOCTL_RAID_FIRMWARE_BUFFER) + dataRequestLength;
        IOCTL_RAID_FIRMWARE_BUFFER* raidFirmwareRequest =
            C_CAST(IOCTL_RAID_FIRMWARE_BUFFER*,
                   safe_calloc_aligned(allocationSize, sizeof(uint8_t), device->os_info.minimumAlignment));
        if (raidFirmwareRequest)
        {
            DECLARE_SEATIMER(commandTimer);
            HANDLE handleToUse = device->os_info.fd; // start with this in case of CSMI RAID
            // fill in SRB_IO_HEADER first
            raidFirmwareRequest->Header.HeaderLength = sizeof(SRB_IO_CONTROL);
            safe_memcpy(raidFirmwareRequest->Header.Signature, 8, INTEL_RAID_FW_SIGNATURE, 8);
            raidFirmwareRequest->Header.Timeout = timeoutSeconds;
            if (device->drive_info.defaultTimeoutSeconds > 0 &&
                device->drive_info.defaultTimeoutSeconds > timeoutSeconds)
            {
                raidFirmwareRequest->Header.Timeout = device->drive_info.defaultTimeoutSeconds;
            }
            else
            {
                if (timeoutSeconds != 0)
                {
                    raidFirmwareRequest->Header.Timeout = timeoutSeconds;
                }
                else
                {
                    raidFirmwareRequest->Header.Timeout = 15;
                }
            }
            raidFirmwareRequest->Header.ControlCode = C_CAST(ULONG, IOCTL_RAID_FIRMWARE);
            raidFirmwareRequest->Header.Length      = C_CAST(ULONG, allocationSize - sizeof(SRB_IO_CONTROL));
            // Next fill in IOCTL_RAID_FIRMWARE_BUFFER, then work down from there and memcpy the input data to this
            // function since it will have the specific download function data in it
            raidFirmwareRequest->Request.Version  = RAID_FIRMWARE_REQUEST_BLOCK_VERSION;
            raidFirmwareRequest->Request.TargetId = RESERVED;
            raidFirmwareRequest->Request.Lun      = RESERVED;
            if (device->os_info.csmiDeviceData && device->os_info.csmiDeviceData->csmiDeviceInfoValid)
            {
                if (device->os_info.csmiDeviceData->scsiAddressValid)
                {
                    raidFirmwareRequest->Request.PathId   = device->os_info.csmiDeviceData->scsiAddress.pathId;
                    raidFirmwareRequest->Request.TargetId = device->os_info.csmiDeviceData->scsiAddress.targetId;
                    raidFirmwareRequest->Request.Lun      = device->os_info.csmiDeviceData->scsiAddress.lun;
                }
                else
                {
                    raidFirmwareRequest->Request.PathId = device->os_info.csmiDeviceData->portIdentifier;
                }
            }
            else
            {
                handleToUse = device->os_info.scsiSRBHandle;
                // use Windows pathId
                raidFirmwareRequest->Request.PathId = device->os_info.scsi_addr.PathId;
                // may need to add in remaining scsi address in the future, but for now these other fields are reserved
            }
            // setup the firmware request
            raidFirmwareRequest->Request.FwRequestBlock.Version  = INTEL_FIRMWARE_REQUEST_BLOCK_STRUCTURE_VERSION;
            raidFirmwareRequest->Request.FwRequestBlock.Size     = sizeof(INTEL_FIRMWARE_REQUEST_BLOCK);
            raidFirmwareRequest->Request.FwRequestBlock.Function = intelFirmwareFunction;
            raidFirmwareRequest->Request.FwRequestBlock.Flags    = intelFirmwareFlags;
            if (ptrDataRequest)
            {
                // NOTE: The offset should be a multiple of sizeof(void), which reading the structure types indicated
                // that this should be the case for 64bit and 32bit builds. Anything else will need additional byte
                // padding.
                raidFirmwareRequest->Request.FwRequestBlock.DataBufferOffset =
                    sizeof(SRB_IO_CONTROL) + sizeof(RAID_FIRMWARE_REQUEST_BLOCK);
                raidFirmwareRequest->Request.FwRequestBlock.DataBufferLength = dataRequestLength;
                safe_memcpy(&raidFirmwareRequest->ioctlBuffer,
                            allocationSize - sizeof(SRB_IO_CONTROL) - sizeof(RAID_FIRMWARE_REQUEST_BLOCK),
                            ptrDataRequest, dataRequestLength);
            }

            if (VERBOSITY_COMMAND_NAMES <= device->deviceVerbosity)
            {
                printf("\n====Sending Intel Raid Firmware Request====\n");
            }

            // send the command
            DWORD      bytesReturned = DWORD_C(0);
            OVERLAPPED overlappedStruct;
            safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
            overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
            if (overlappedStruct.hEvent == M_NULLPTR)
            {
                safe_free_irst_raid_fw_buffer(&raidFirmwareRequest);
                return OS_PASSTHROUGH_FAILURE;
            }
            start_Timer(&commandTimer);
            BOOL success =
                DeviceIoControl(handleToUse, IOCTL_SCSI_MINIPORT, raidFirmwareRequest, C_CAST(DWORD, allocationSize),
                                raidFirmwareRequest, C_CAST(DWORD, allocationSize), &bytesReturned, &overlappedStruct);
            device->os_info.last_error = GetLastError();
            if (ERROR_IO_PENDING ==
                device->os_info
                    .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                 // overlapped flag, everything will work like old synchronous code.-TJE
            {
                success = GetOverlappedResult(handleToUse, &overlappedStruct, &bytesReturned, TRUE);
            }
            else if (device->os_info.last_error != ERROR_SUCCESS)
            {
                ret = OS_PASSTHROUGH_FAILURE;
            }
            stop_Timer(&commandTimer);
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
            if (VERBOSITY_COMMAND_VERBOSE <= device->deviceVerbosity)
            {
                printf("Windows Error: ");
                print_Windows_Error_To_Screen(device->os_info.last_error);
                printf("Intel RAID Firmware: ");
                printf_Intel_Firmware_SRB_Status(raidFirmwareRequest->Header.ReturnCode);
            }
            if (MSFT_BOOL_FALSE(success))
            {
                ret = FAILURE;
            }
            else
            {
                if (returnCode != M_NULLPTR)
                {
                    *returnCode = raidFirmwareRequest->Header.ReturnCode;
                }
                ret = SUCCESS; // IO sent successfully in the system...BUT we need to check the SRB return code to
                               // determine if the command went through to the device
                switch (raidFirmwareRequest->Header.ReturnCode)
                {
                case INTEL_FIRMWARE_STATUS_SUCCESS:
                    // should have completion data here
                    if (readFirmwareInfo && ptrDataRequest)
                    {
                        safe_memcpy(ptrDataRequest, dataRequestLength,
                                    C_CAST(uint8_t*, raidFirmwareRequest) +
                                        raidFirmwareRequest->Request.FwRequestBlock.DataBufferOffset,
                                    dataRequestLength);
                    }
                    break;
                case INTEL_FIRMWARE_STATUS_ERROR:
                case INTEL_FIRMWARE_STATUS_ILLEGAL_REQUEST:
                case INTEL_FIRMWARE_STATUS_INVALID_PARAMETER:
                case INTEL_FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
                case INTEL_FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
                case INTEL_FIRMWARE_STATUS_INVALID_SLOT:
                case INTEL_FIRMWARE_STATUS_INVALID_IMAGE:
                case INTEL_FIRMWARE_STATUS_CONTROLLER_ERROR:
                case INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
                case INTEL_FIRMWARE_STATUS_DEVICE_ERROR:
                case INTEL_FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
                case INTEL_FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
                case INTEL_FIRMWARE_STATUS_MEDIA_CHANGE:
                case INTEL_FIRMWARE_STATUS_ID_NOT_FOUND:
                case INTEL_FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
                case INTEL_FIRMWARE_STATUS_COMMAND_ABORT:
                case INTEL_FIRMWARE_STATUS_END_OF_MEDIA:
                case INTEL_FIRMWARE_STATUS_ILLEGAL_LENGTH:
                    ret = FAILURE;
                    break;
                default:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }
            }
            safe_free_irst_raid_fw_buffer(&raidFirmwareRequest);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static M_INLINE void safe_free_irst_fw_info(INTEL_STORAGE_FIRMWARE_INFO_V2** info)
{
    safe_free_core(M_REINTERPRET_CAST(void**, info));
}

bool supports_Intel_Firmware_Download(tDevice* device)
{
    bool supported = false;
#    if defined(INTRST_DEBUG)
    printf("Intel: Checking FWDL IOCTL support\n");
#    endif // INTRST_DEBUG
    uint32_t allocationSize =
        sizeof(INTEL_STORAGE_FIRMWARE_INFO_V2) + (sizeof(INTEL_STORAGE_FIRMWARE_SLOT_INFO_V2) * 7); // max of 7 slots
    PINTEL_STORAGE_FIRMWARE_INFO_V2 firmwareInfo =
        C_CAST(PINTEL_STORAGE_FIRMWARE_INFO_V2,
               safe_calloc(allocationSize, sizeof(uint8_t))); // alignment not needed since this is passed to another
                                                              // function where it will be copied as needed
    if (firmwareInfo)
    {
        uint32_t flags      = UINT32_C(0);
        uint32_t returnCode = UINT32_C(0);
        if (device->drive_info.drive_type == NVME_DRIVE)
        {
            flags |= INTEL_FIRMWARE_REQUEST_FLAG_CONTROLLER;
        }
        // do some setup first, then call intel_RAID_FW_Request to issue the command
        firmwareInfo->Version = INTEL_STORAGE_FIRMWARE_INFO_STRUCTURE_VERSION_V2;
        firmwareInfo->Size    = sizeof(INTEL_STORAGE_FIRMWARE_INFO_V2);
        // nothing else needs setup, just issue the command
#    if defined(INTRST_DEBUG)
        printf("Attempting to get Intel FWDL support\n");
#    endif // INTRST_DEBUG
        if (SUCCESS == intel_RAID_FW_Request(device, firmwareInfo, allocationSize, 15, INTEL_FIRMWARE_FUNCTION_GET_INFO,
                                             flags, true, &returnCode))
        {
            supported = firmwareInfo->UpgradeSupport;
            if (device->os_info.csmiDeviceData)
            {
                device->os_info.csmiDeviceData->intelRSTSupport.intelRSTSupported = true;
                device->os_info.csmiDeviceData->intelRSTSupport.fwdlIOSupported   = firmwareInfo->UpgradeSupport;
                device->os_info.csmiDeviceData->intelRSTSupport.maxXferSize       = firmwareInfo->ImagePayloadMaxSize;
                device->os_info.csmiDeviceData->intelRSTSupport.payloadAlignment  = firmwareInfo->ImagePayloadAlignment;
            }
#    if defined(INTRST_DEBUG)
            printf("Got Intel FWDL Info\n");
            printf("\tSupported: %d\n", firmwareInfo->UpgradeSupport);
            printf("\tPayload Alignment: %ld\n", firmwareInfo->ImagePayloadAlignment);
            printf("\tmaxXferSize: %ld\n", firmwareInfo->ImagePayloadMaxSize);
            printf("\tPendingActivate: %d\n", firmwareInfo->PendingActivateSlot);
            printf("\tActiveSlot: %d\n", firmwareInfo->ActiveSlot);
            printf("\tSlot Count: %d\n", firmwareInfo->SlotCount);
            printf("\tFirmware Shared: %d\n", firmwareInfo->FirmwareShared);
            // print out what's in the slots!
            for (uint8_t iter = UINT8_C(0); iter < firmwareInfo->SlotCount && iter < 7; ++iter)
            {
                printf("\t    Firmware Slot %d:\n", firmwareInfo->Slot[iter].SlotNumber);
                printf("\t\tRead Only: %d\n", firmwareInfo->Slot[iter].ReadOnly);
                printf("\t\tRevision: %s\n", firmwareInfo->Slot[iter].Revision);
            }
#    endif // INTRST_DEBUG
        }
        safe_free_irst_fw_info(&firmwareInfo);
    }
#    if defined(INTRST_DEBUG)
    printf("Intel: FWDL IOCTL = %u\n", supported);
#    endif // INTRST_DEBUG
    return supported;
}

static M_INLINE void safe_free_irst_fwdl(INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2** fwdl)
{
    safe_free_core(M_REINTERPRET_CAST(void**, fwdl));
}

// The idea with this function is that it can handle NVMe or SCSI with generic inputs that will work to reduce code
static eReturnValues internal_Intel_FWDL_Function_Download(tDevice*  device,
                                                           uint32_t  flags,
                                                           uint32_t* returnCode,
                                                           uint8_t*  imagePtr,
                                                           uint32_t  imageDataLength,
                                                           uint32_t  imageOffset,
                                                           uint8_t   firmwareSlot,
                                                           uint32_t  timeoutSeconds)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    if (device && imagePtr)
    {
        uint32_t allocationSize                      = sizeof(INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2) + imageDataLength;
        PINTEL_STORAGE_FIRMWARE_DOWNLOAD_V2 download = C_CAST(
            PINTEL_STORAGE_FIRMWARE_DOWNLOAD_V2,
            safe_calloc(allocationSize,
                        sizeof(uint8_t))); // alignment not needed since this will get copied to an aligned location
        if (download)
        {
            download->Version    = INTEL_STORAGE_FIRMWARE_DOWNLOAD_STRUCTURE_VERSION_V2;
            download->Size       = sizeof(INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2);
            download->Offset     = imageOffset;
            download->BufferSize = imageDataLength;
            download->Slot       = firmwareSlot;
            download->ImageSize  = imageDataLength; // TODO: Not sure if this is supposed to be the same or different
                                                    // from the buffersize listed above
            safe_memcpy(download->ImageBuffer, allocationSize - sizeof(INTEL_STORAGE_FIRMWARE_DOWNLOAD_V2), imagePtr,
                        imageDataLength);
            ret = intel_RAID_FW_Request(device, download, allocationSize, timeoutSeconds,
                                        INTEL_FIRMWARE_FUNCTION_DOWNLOAD, flags, false, returnCode);
            safe_free_irst_fwdl(&download);
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

static M_INLINE void safe_free_irst_fw_activate(INTEL_STORAGE_FIRMWARE_ACTIVATE** activate)
{
    safe_free_core(M_REINTERPRET_CAST(void**, activate));
}

static eReturnValues internal_Intel_FWDL_Function_Activate(tDevice*  device,
                                                           uint32_t  flags,
                                                           uint32_t* returnCode,
                                                           uint8_t   firmwareSlot,
                                                           uint32_t  timeoutSeconds)
{
    eReturnValues ret = OS_COMMAND_NOT_AVAILABLE;
    if (device)
    {
        uint32_t                         allocationSize = sizeof(INTEL_STORAGE_FIRMWARE_ACTIVATE);
        PINTEL_STORAGE_FIRMWARE_ACTIVATE activate       = C_CAST(
            PINTEL_STORAGE_FIRMWARE_ACTIVATE,
            safe_calloc(allocationSize,
                              sizeof(uint8_t))); // alignment not needed since this will get copied to an aligned location
        if (activate)
        {
            activate->Version        = INTEL_STORAGE_FIRMWARE_ACTIVATE_STRUCTURE_VERSION;
            activate->Size           = sizeof(INTEL_STORAGE_FIRMWARE_ACTIVATE);
            activate->SlotToActivate = firmwareSlot;
            ret                      = intel_RAID_FW_Request(device, activate, allocationSize, timeoutSeconds,
                                                             INTEL_FIRMWARE_FUNCTION_ACTIVATE, flags, false, returnCode);
            safe_free_irst_fw_activate(&activate);
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

// This must also check to ensure that the alignment requirements and transfer length requirements are also met
// This is really only needed on SCSI or ATA since NVMe will only take this API to update firmware
static bool is_Compatible_SCSI_FWDL_IO(ScsiIoCtx* scsiIoCtx, bool* isActivate)
{
    bool     compatible          = false;
    uint32_t transferLengthBytes = UINT32_C(0);
    // check if this is a SCSI Write buffer command or ATA download microcode. Can only support deferred and activate
    // subcommands.
    if (scsiIoCtx->cdb[OPERATION_CODE] == WRITE_BUFFER_CMD)
    {
        uint8_t wbMode = get_bit_range_uint8(scsiIoCtx->cdb[1], 4, 0);
        if (wbMode == SCSI_WB_DL_MICROCODE_OFFSETS_SAVE_DEFER)
        {
            compatible          = true;
            transferLengthBytes = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[6], scsiIoCtx->cdb[7], scsiIoCtx->cdb[8]);
        }
        else if (wbMode == SCSI_WB_ACTIVATE_DEFERRED_MICROCODE)
        {
            compatible = true;
            if (isActivate)
            {
                *isActivate = true;
            }
        }
    }
    else if (scsiIoCtx->pAtaCmdOpts && (scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_CMD ||
                                        scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus == ATA_DOWNLOAD_MICROCODE_DMA))
    {
        if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0E)
        {
            compatible = true;
            transferLengthBytes =
                M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaLow, scsiIoCtx->pAtaCmdOpts->tfr.SectorCount) *
                LEGACY_DRIVE_SEC_SIZE;
        }
        else if (scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature == 0x0F)
        {
            compatible = true;
            if (isActivate)
            {
                *isActivate = true;
            }
        }
    }
    if (compatible)
    {
        // before we call it a supported command, we need to validate if this meets the alignment requirements, etc
        if (isActivate && !(*isActivate))
        {
            if (!(transferLengthBytes < scsiIoCtx->device->os_info.fwdlIOsupport.maxXferSize &&
                  (transferLengthBytes % scsiIoCtx->device->os_info.fwdlIOsupport.payloadAlignment == 0)))
            {
                compatible = false;
            }
        }
    }
    return compatible;
}

eReturnValues send_Intel_Firmware_Download(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret        = OS_COMMAND_NOT_AVAILABLE;
    bool          isActivate = false;
    if (is_Compatible_SCSI_FWDL_IO(scsiIoCtx, &isActivate))
    {
        uint32_t returnCode = UINT32_C(0); // will most likely help with debugging, but we should check this to dummy up
                                           // sense data or rtfrs as necessary.
        uint32_t flags   = UINT32_C(0);    // flags aren't required
        uint32_t timeout = scsiIoCtx->timeout;
        uint8_t  firmwareSlot =
            UINT8_C(0); // NOTE: Some intel documentation suggests that this should be
                        // INTEL_STORAGE_FIRMWARE_INFO_INVALID_SLOT, but we should test before using that
        // special case, if running in SCSI translation mode for NVMe, we should set the controller flag
        if (strcmp(scsiIoCtx->device->drive_info.T10_vendor_ident, "NVMe") == 0)
        {
            flags |= INTEL_FIRMWARE_REQUEST_FLAG_CONTROLLER;
        }
        if (isActivate)
        {
            // send activate command
            // set up the things we need to input to the activate from SCSI or ATA info as appropriate
            if (scsiIoCtx->pAtaCmdOpts)
            {
                timeout = scsiIoCtx->pAtaCmdOpts->timeout;
            }
            else
            {
                // assume SCSI write buffer if we made it this far. The is_Compatible_SCSI_FWDL_IO will filter out other
                // commands since the opcode won't match
                firmwareSlot = scsiIoCtx->cdb[2]; // firmware slot or buffer ID are "the same" in SNTL
            }
            ret = internal_Intel_FWDL_Function_Activate(scsiIoCtx->device, flags, &returnCode, firmwareSlot, timeout);
            // TODO: Dummy up sense data!
        }
        else
        {
            uint8_t* imagePtr        = scsiIoCtx->pdata;
            uint32_t imageDataLength = scsiIoCtx->dataLength;
            uint32_t imageOffset     = UINT32_C(0);
            // send download command
            if (scsiIoCtx->fwdlFirstSegment)
            {
                flags |= INTEL_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
            }
            if (scsiIoCtx->fwdlLastSegment)
            {
                flags |= INTEL_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
            }
            // set up the things we need to input to the activate from SCSI or ATA info as appropriate
            if (scsiIoCtx->pAtaCmdOpts)
            {
                timeout         = scsiIoCtx->pAtaCmdOpts->timeout;
                imagePtr        = scsiIoCtx->pAtaCmdOpts->ptrData;
                imageDataLength = scsiIoCtx->pAtaCmdOpts->dataSize;
                imageOffset =
                    M_BytesTo2ByteValue(scsiIoCtx->pAtaCmdOpts->tfr.LbaHi, scsiIoCtx->pAtaCmdOpts->tfr.LbaMid) *
                    LEGACY_DRIVE_SEC_SIZE;
            }
            else
            {
                // assume SCSI write buffer if we made it this far. The is_Compatible_SCSI_FWDL_IO will filter out other
                // commands since the opcode won't match
                firmwareSlot = scsiIoCtx->cdb[2]; // firmware slot or buffer ID are "the same" in SNTL
                imageOffset  = M_BytesTo4ByteValue(0, scsiIoCtx->cdb[3], scsiIoCtx->cdb[4], scsiIoCtx->cdb[5]);
            }
            ret = internal_Intel_FWDL_Function_Download(scsiIoCtx->device, flags, &returnCode, imagePtr,
                                                        imageDataLength, imageOffset, firmwareSlot, timeout);
            // TODO: Dummy up sense data!
        }
    }
    return ret;
}

static M_INLINE void safe_free_irst_nvme_passthrough(NVME_IOCTL_PASS_THROUGH** nvmcmd)
{
    safe_free_aligned_core(M_REINTERPRET_CAST(void**, nvmcmd));
}

// NOTE: This function will handle calling appropriate NVMe firmware update function as well
// NOTE2: This will not issue whatever command you want. Only certain commands are supported by the driver. This
// function will attempt any command given in case driver updates allow other commands in the future.
static eReturnValues send_Intel_NVM_Passthrough_Command(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    if (nvmeIoCtx)
    {
        DECLARE_SEATIMER(commandTimer);
        NVME_IOCTL_PASS_THROUGH* nvmPassthroughCommand = M_NULLPTR;
        HANDLE                   handleToUse           = nvmeIoCtx->device->os_info.fd;
        size_t                   allocationSize        = sizeof(NVME_IOCTL_PASS_THROUGH) + nvmeIoCtx->dataSize;
        nvmPassthroughCommand =
            C_CAST(NVME_IOCTL_PASS_THROUGH*,
                   safe_calloc_aligned(allocationSize, sizeof(uint8_t), nvmeIoCtx->device->os_info.minimumAlignment));
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("\n====Sending Intel RST NVMe Command====\n");
        }
        if (nvmPassthroughCommand)
        {
            // setup the header (SRB_IO_CONTROL) first
            nvmPassthroughCommand->Header.HeaderLength = sizeof(SRB_IO_CONTROL);
            safe_memcpy(nvmPassthroughCommand->Header.Signature, 8, INTELNVM_SIGNATURE, 8);
            nvmPassthroughCommand->Header.Timeout = nvmeIoCtx->timeout;
            if (nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
                nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > nvmeIoCtx->timeout)
            {
                nvmPassthroughCommand->Header.Timeout = nvmeIoCtx->device->drive_info.defaultTimeoutSeconds;
            }
            else
            {
                if (nvmeIoCtx->timeout != 0)
                {
                    nvmPassthroughCommand->Header.Timeout = nvmeIoCtx->timeout;
                }
                else
                {
                    nvmPassthroughCommand->Header.Timeout = 15;
                }
            }
            nvmPassthroughCommand->Header.ControlCode = C_CAST(ULONG, IOCTL_NVME_PASSTHROUGH);
            nvmPassthroughCommand->Header.Length      = C_CAST(ULONG, allocationSize - sizeof(SRB_IO_CONTROL));
            // srb_io_control has been setup, now to main struct fields (minus data which is set when configuring the
            // NVMe command)
            nvmPassthroughCommand->Version = NVME_PASS_THROUGH_VERSION;
            // setup the SCSI address, pathID is only part needed at time of writing, the rest is currently reserved, so
            // leave as zeros pathId must be from the SCSI address in non-raid mode, or CSMI port number, so this is
            // where it begins to get a little messy
            nvmPassthroughCommand->TargetId = RESERVED;
            nvmPassthroughCommand->Lun      = RESERVED;
            if (nvmeIoCtx->device->os_info.csmiDeviceData &&
                nvmeIoCtx->device->os_info.csmiDeviceData->csmiDeviceInfoValid)
            {
                if (nvmeIoCtx->device->os_info.csmiDeviceData->scsiAddressValid)
                {
                    nvmPassthroughCommand->PathId   = nvmeIoCtx->device->os_info.csmiDeviceData->scsiAddress.pathId;
                    nvmPassthroughCommand->TargetId = nvmeIoCtx->device->os_info.csmiDeviceData->scsiAddress.targetId;
                    nvmPassthroughCommand->Lun      = nvmeIoCtx->device->os_info.csmiDeviceData->scsiAddress.lun;
                }
                else
                {
                    nvmPassthroughCommand->PathId = nvmeIoCtx->device->os_info.csmiDeviceData->portIdentifier;
                }
            }
            else
            {
                handleToUse = nvmeIoCtx->device->os_info.scsiSRBHandle;
                // use Windows pathId
                nvmPassthroughCommand->PathId = nvmeIoCtx->device->os_info.scsi_addr.PathId;
                // may need to add in remaining scsi address in the future, but for now these other fields are reserved
            }
            // time to start setting up the command!
            nvmPassthroughCommand->Parameters.Command.DWord0  = nvmeIoCtx->cmd.dwords.cdw0;
            nvmPassthroughCommand->Parameters.Command.DWord1  = nvmeIoCtx->cmd.dwords.cdw1;
            nvmPassthroughCommand->Parameters.Command.DWord2  = nvmeIoCtx->cmd.dwords.cdw2;
            nvmPassthroughCommand->Parameters.Command.DWord3  = nvmeIoCtx->cmd.dwords.cdw3;
            nvmPassthroughCommand->Parameters.Command.DWord4  = nvmeIoCtx->cmd.dwords.cdw4;
            nvmPassthroughCommand->Parameters.Command.DWord5  = nvmeIoCtx->cmd.dwords.cdw5;
            nvmPassthroughCommand->Parameters.Command.DWord6  = nvmeIoCtx->cmd.dwords.cdw6;
            nvmPassthroughCommand->Parameters.Command.DWord7  = nvmeIoCtx->cmd.dwords.cdw7;
            nvmPassthroughCommand->Parameters.Command.DWord8  = nvmeIoCtx->cmd.dwords.cdw8;
            nvmPassthroughCommand->Parameters.Command.DWord9  = nvmeIoCtx->cmd.dwords.cdw9;
            nvmPassthroughCommand->Parameters.Command.DWord10 = nvmeIoCtx->cmd.dwords.cdw10;
            nvmPassthroughCommand->Parameters.Command.DWord11 = nvmeIoCtx->cmd.dwords.cdw11;
            nvmPassthroughCommand->Parameters.Command.DWord12 = nvmeIoCtx->cmd.dwords.cdw12;
            nvmPassthroughCommand->Parameters.Command.DWord13 = nvmeIoCtx->cmd.dwords.cdw13;
            nvmPassthroughCommand->Parameters.Command.DWord14 = nvmeIoCtx->cmd.dwords.cdw14;
            nvmPassthroughCommand->Parameters.Command.DWord15 = nvmeIoCtx->cmd.dwords.cdw15;
            if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
            {
                nvmPassthroughCommand->Parameters.IsIOCommandSet = FALSE;
            }
            else
            {
                nvmPassthroughCommand->Parameters.IsIOCommandSet = TRUE;
            }
            switch (nvmeIoCtx->commandDirection)
            {
            case XFER_DATA_OUT:
                safe_memcpy(nvmPassthroughCommand->data, nvmeIoCtx->dataSize, nvmeIoCtx->ptrData, nvmeIoCtx->dataSize);
                M_FALLTHROUGH;
            case XFER_DATA_IN:
                // set the data length and offset
                nvmPassthroughCommand->Parameters.DataBufferLength = nvmeIoCtx->dataSize;
                nvmPassthroughCommand->Parameters.DataBufferOffset = offsetof(
                    NVME_IOCTL_PASS_THROUGH, data); // NOTE: This must be DWORD aligned. The structure being used SHOULD
                                                    // be, but if things aren't working, this could be the problem
                break;
            case XFER_NO_DATA:
                nvmPassthroughCommand->Parameters.DataBufferLength = 0;
                nvmPassthroughCommand->Parameters.DataBufferOffset =
                    0; // this should be ok since we aren't doing a transfer
                break;
            default:
                safe_free_irst_nvme_passthrough(&nvmPassthroughCommand);
                return OS_COMMAND_NOT_AVAILABLE;
            }
            DWORD      bytesReturned = DWORD_C(0);
            OVERLAPPED overlappedStruct;
            safe_memset(&overlappedStruct, sizeof(OVERLAPPED), 0, sizeof(OVERLAPPED));
            overlappedStruct.hEvent = CreateEvent(M_NULLPTR, TRUE, FALSE, M_NULLPTR);
            if (overlappedStruct.hEvent == M_NULLPTR)
            {
                safe_free_irst_nvme_passthrough(&nvmPassthroughCommand);
                return OS_PASSTHROUGH_FAILURE;
            }
            SetLastError(ERROR_SUCCESS); // clear out any errors before we begin
            start_Timer(&commandTimer);
            BOOL success = DeviceIoControl(handleToUse, IOCTL_SCSI_MINIPORT, nvmPassthroughCommand,
                                           C_CAST(DWORD, allocationSize), nvmPassthroughCommand,
                                           C_CAST(DWORD, allocationSize), &bytesReturned, &overlappedStruct);
            nvmeIoCtx->device->os_info.last_error = GetLastError();
            if (ERROR_IO_PENDING ==
                nvmeIoCtx->device->os_info
                    .last_error) // This will only happen for overlapped commands. If the drive is opened without the
                                 // overlapped flag, everything will work like old synchronous code.-TJE
            {
                success = GetOverlappedResult(handleToUse, &overlappedStruct, &bytesReturned, TRUE);
            }
            else if (nvmeIoCtx->device->os_info.last_error != ERROR_SUCCESS)
            {
                ret = OS_PASSTHROUGH_FAILURE;
            }
            stop_Timer(&commandTimer);
            CloseHandle(overlappedStruct.hEvent); // close the overlapped handle since it isn't needed any more...-TJE
            overlappedStruct.hEvent = M_NULLPTR;
            if (MSFT_BOOL_FALSE(success))
            {
                ret = OS_PASSTHROUGH_FAILURE;
            }
            else
            {
                ret = SUCCESS;
                switch (nvmPassthroughCommand->Header.ReturnCode)
                {
                case INTEL_SRB_STATUS_SUCCESS:
                    // TODO: Handle unsupported commands error codes and others that don't fill in the completion data
                    //       This may not come into this SUCCESS case and instead may need handling elsewhere.
                    // if(nvmPassthroughCommand->Header.ReturnCode)
                    if (nvmeIoCtx->commandDirection == XFER_DATA_IN && nvmeIoCtx->ptrData)
                    {
                        safe_memcpy(nvmeIoCtx->ptrData, nvmeIoCtx->dataSize, nvmPassthroughCommand->data,
                                    nvmeIoCtx->dataSize);
                    }
                    // copy completion data
                    nvmeIoCtx->commandCompletionData.dw0Valid = true;
                    nvmeIoCtx->commandCompletionData.dw1Valid = true;
                    nvmeIoCtx->commandCompletionData.dw2Valid = true;
                    nvmeIoCtx->commandCompletionData.dw3Valid = true;
                    nvmeIoCtx->commandCompletionData.commandSpecific =
                        nvmPassthroughCommand->Parameters.Completion.completion0;
                    nvmeIoCtx->commandCompletionData.dw1Reserved =
                        nvmPassthroughCommand->Parameters.Completion.completion1;
                    nvmeIoCtx->commandCompletionData.sqIDandHeadPtr =
                        nvmPassthroughCommand->Parameters.Completion.completion2;
                    nvmeIoCtx->commandCompletionData.statusAndCID =
                        nvmPassthroughCommand->Parameters.Completion.completion3;
                    break;
                default:
                    ret = OS_PASSTHROUGH_FAILURE;
                    break;
                }
            }
            if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
            {
                print_Windows_Error_To_Screen(nvmeIoCtx->device->os_info.last_error);
                print_Intel_SRB_Status(nvmPassthroughCommand->Header.ReturnCode);
            }

            // set command time
            nvmeIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
            safe_free_irst_nvme_passthrough(&nvmPassthroughCommand);
        }
        else
        {
            ret = MEMORY_FAILURE;
        }
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            print_Return_Enum("Intel RST NVMe Cmd", ret);
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    return ret;
}

#    define DUMMY_NVME_STATUS(sct, sc) C_CAST(uint32_t, (sct << 25) | (sc << 17))

// This may need adjusting with bus trace help in the future, but for now this is a best guess from what info we
// have.-TJE
static void dummy_Up_NVM_Status_FWDL(nvmeCmdCtx* nvmeIoCtx, uint32_t returnCode)
{
    switch (returnCode)
    {
    case INTEL_FIRMWARE_STATUS_SUCCESS:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 0);
        break;
    case INTEL_FIRMWARE_STATUS_ERROR:
        break;
    case INTEL_FIRMWARE_STATUS_ILLEGAL_REQUEST: // overlapping range?
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 1);
        break;
    case INTEL_FIRMWARE_STATUS_INVALID_PARAMETER: // overlapping range?
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 2);
        break;
    case INTEL_FIRMWARE_STATUS_INPUT_BUFFER_TOO_BIG:
        break;
    case INTEL_FIRMWARE_STATUS_OUTPUT_BUFFER_TOO_SMALL:
        break;
    case INTEL_FIRMWARE_STATUS_INVALID_SLOT:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, 6);
        break;
    case INTEL_FIRMWARE_STATUS_INVALID_IMAGE:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, 7);
        break;
    case INTEL_FIRMWARE_STATUS_ILLEGAL_LENGTH: // overlapping range???
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3      = DUMMY_NVME_STATUS(NVME_SCT_COMMAND_SPECIFIC_STATUS, 14);
        break;
    case INTEL_FIRMWARE_STATUS_DEVICE_ERROR:
    case INTEL_FIRMWARE_STATUS_CONTROLLER_ERROR:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 = DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 6); // internal error
        break;
    case INTEL_FIRMWARE_STATUS_COMMAND_ABORT:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 7); // command abort requested
        break;
    case INTEL_FIRMWARE_STATUS_ID_NOT_FOUND:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 0x80); // lba out of range
        break;
    case INTEL_FIRMWARE_STATUS_INTERFACE_CRC_ERROR:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            DUMMY_NVME_STATUS(NVME_SCT_GENERIC_COMMAND_STATUS, 4); // data transfer error
        break;
    case INTEL_FIRMWARE_STATUS_UNCORRECTABLE_DATA_ERROR:
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw3 =
            DUMMY_NVME_STATUS(NVME_SCT_MEDIA_AND_DATA_INTEGRITY_ERRORS,
                              0x81); // unrecovered read error...not sure what else to set if this happens-TJE
        break;
    case INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED:
    case INTEL_FIRMWARE_STATUS_MEDIA_CHANGE:
    case INTEL_FIRMWARE_STATUS_MEDIA_CHANGE_REQUEST:
    case INTEL_FIRMWARE_STATUS_END_OF_MEDIA:
    default:
        break;
    }
}

eReturnValues send_Intel_NVM_Firmware_Download(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
    DISABLE_NONNULL_COMPARE
    if (nvmeIoCtx != M_NULLPTR)
    {
        if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
        {
            uint32_t flags      = INTEL_FIRMWARE_REQUEST_FLAG_CONTROLLER; // always set this for NVMe
            uint32_t returnCode = UINT32_C(0);
            uint8_t  firmwareSlot =
                INTEL_STORAGE_FIRMWARE_INFO_INVALID_SLOT; // Starting with this...we may need to change this later!!!
            // check if sending download or if sending activate
            if (nvmeIoCtx->cmd.adminCmd.opcode == NVME_ADMIN_CMD_DOWNLOAD_FW)
            {
                if (nvmeIoCtx->fwdlFirstSegment)
                {
                    flags |= INTEL_FIRMWARE_REQUEST_FLAG_FIRST_SEGMENT;
                }
                if (nvmeIoCtx->fwdlLastSegment)
                {
                    flags |= INTEL_FIRMWARE_REQUEST_FLAG_LAST_SEGMENT;
                }
                // send download command API
                // NOTE: CDW10 and 11 are numbers of dwords! So shift by 2 (mul by 4) is required as this function is
                // expecting a byte offset
                ret = internal_Intel_FWDL_Function_Download(
                    nvmeIoCtx->device, flags, &returnCode, nvmeIoCtx->ptrData, (nvmeIoCtx->cmd.adminCmd.cdw10 + 1) << 2,
                    nvmeIoCtx->cmd.adminCmd.cdw11 << 2, firmwareSlot, nvmeIoCtx->timeout);
                // Dummy up output data based on return code.
                dummy_Up_NVM_Status_FWDL(nvmeIoCtx, returnCode);
                if (returnCode == INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED)
                {
                    ret = POWER_CYCLE_REQUIRED;
                }
            }
            else if (nvmeIoCtx->cmd.adminCmd.opcode == NVME_ADMIN_CMD_ACTIVATE_FW)
            {
                uint8_t activateAction = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 5, 3);
                if (activateAction == NVME_CA_ACTIVITE_ON_RST ||
                    activateAction == NVME_CA_ACTIVITE_IMMEDIATE) // check the activate action
                {
                    // Activate actions 2, & 3 sound like the closest match to this flag. Each of these requests
                    // switching to the a firmware already on the drive. Activate action 0 & 1 say to replace a firmware
                    // image in a specified slot (and to or not to activate).
                    flags |= INTEL_FIRMWARE_REQUEST_FLAG_SWITCH_TO_EXISTING_FIRMWARE;
                }
                firmwareSlot = get_8bit_range_uint32(nvmeIoCtx->cmd.adminCmd.cdw10, 2, 0);
                // send activate command API
                ret = internal_Intel_FWDL_Function_Activate(nvmeIoCtx->device, flags, &returnCode, firmwareSlot,
                                                            nvmeIoCtx->timeout);
                // Dummy up output data based on return code.
                dummy_Up_NVM_Status_FWDL(nvmeIoCtx, returnCode);
                if (returnCode == INTEL_FIRMWARE_STATUS_POWER_CYCLE_REQUIRED)
                {
                    ret = POWER_CYCLE_REQUIRED;
                }
            }
            else
            {
                ret = BAD_PARAMETER;
            }
        }
        else
        {
            ret = BAD_PARAMETER;
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
    return ret;
}

eReturnValues send_Intel_NVM_Command(nvmeCmdCtx* nvmeIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
#    if defined(INTRST_DEBUG)
    printf("Intel: NVM passthrough request\n");
#    endif // INTRST_DEBUG
    DISABLE_NONNULL_COMPARE
    if (nvmeIoCtx != M_NULLPTR)
    {
        if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
        {
            switch (nvmeIoCtx->cmd.adminCmd.opcode)
            {
            case NVME_ADMIN_CMD_DOWNLOAD_FW:
            case NVME_ADMIN_CMD_ACTIVATE_FW:
#    if defined(INTRST_DEBUG)
                printf("Intel: NVM firmware command\n");
#    endif // INTRST_DEBUG
                ret = send_Intel_NVM_Firmware_Download(nvmeIoCtx);
                break;
            default:
#    if defined(INTRST_DEBUG)
                printf("Intel: NVM generic passthrough command\n");
#    endif // INTRST_DEBUG
                ret = send_Intel_NVM_Passthrough_Command(nvmeIoCtx);
                break;
            }
        }
        else // IO command
        {
            ret = send_Intel_NVM_Passthrough_Command(nvmeIoCtx);
        }
    }
    else
    {
        ret = BAD_PARAMETER;
    }
#    if defined(INTRST_DEBUG)
    printf("Intel: NVM result: %d\n", ret);
#    endif // INTRST_DEBUG
    RESTORE_NONNULL_COMPARE
    return ret;
}

eReturnValues send_Intel_NVM_SCSI_Command(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = OS_PASSTHROUGH_FAILURE;
#    if defined(INTRST_DEBUG)
    printf("Intel: Received SCSI command for translation\n");
#    endif // INTRST_DEBUG
    DISABLE_NONNULL_COMPARE
    if (scsiIoCtx != M_NULLPTR)
    {
        ret = sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
    }
    else
    {
        ret = BAD_PARAMETER;
    }
    RESTORE_NONNULL_COMPARE
#    if defined(INTRST_DEBUG)
    printf("Intel: Translated command result: %d\n", ret);
#    endif // INTRST_DEBUG
    return ret;
}

#endif // ENABLE_INTEL_RST
