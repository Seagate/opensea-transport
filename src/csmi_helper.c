//
// Do NOT modify or remove this copyright and license
//
// Copyright (c) 2012 - 2018 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//
// This software is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ******************************************************************************************
// 
#if defined (ENABLE_CSMI)

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <tchar.h>
#else
#include <sys/ioctl.h>
#endif
#include "csmi_helper.h"
#include "csmi_helper_func.h"
#include "cmds.h"
#include "sat_helper_func.h"
#include "ata_helper_func.h"
#include "scsi_helper_func.h"

extern bool validate_Device_Struct(versionBlock);

void print_IOCTL_Return_Code(uint32_t returnCode)
{
    printf("IOCTL Status: ");
    switch (returnCode)
    {
    case CSMI_SAS_STATUS_SUCCESS:
        printf("CSMI SAS STATUS SUCCESS\n");
        break;
    case CSMI_SAS_STATUS_FAILED:
        printf("CSMI SAS STATUS FAILED\n");
        break;
    case CSMI_SAS_STATUS_BAD_CNTL_CODE:
        printf("CSMI SAS BAD CNTL CODE\n");
        break;
    case CSMI_SAS_STATUS_INVALID_PARAMETER:
        printf("CSMI SAS INVALID PARAMETER\n");
        break;
    case CSMI_SAS_STATUS_WRITE_ATTEMPTED:
        printf("CSMI SAS WRITE ATTEMPTED\n");
        break;
    case CSMI_SAS_RAID_SET_OUT_OF_RANGE:
        printf("CSMI SAS RAID SET OUT OF RANGE\n");
        break;
    case CSMI_SAS_RAID_SET_BUFFER_TOO_SMALL:
        printf("CSMI SAS RAID SET BUFFER TOO SMALL\n");
        break;
    case CSMI_SAS_RAID_SET_DATA_CHANGED:
        printf("CSMI SAS RAID SET DATA CHANGED\n");
        break;
    case CSMI_SAS_PHY_INFO_NOT_CHANGEABLE:
        printf("CSMI SAS PHY INFO NOT CHANGEABLE\n");
        break;
    case CSMI_SAS_LINK_RATE_OUT_OF_RANGE:
        printf("CSMI SAS LINK RATE OUT OF RANGE\n");
        break;
    case CSMI_SAS_PHY_DOES_NOT_EXIST:
        printf("CSMI SAS PHY DOES NOT EXIST\n");
        break;
    case CSMI_SAS_PHY_DOES_NOT_MATCH_PORT:
        printf("CSMI SAS PHY DOES NOT MATCH PORT\n");
        break;
    case CSMI_SAS_PHY_CANNOT_BE_SELECTED:
        printf("CSMI SAS PHY CANNOT BE SELECTED\n");
        break;
    case CSMI_SAS_SELECT_PHY_OR_PORT:
        printf("CSMI SAS SELECT PHY OR PORT\n");
        break;
    case CSMI_SAS_PORT_DOES_NOT_EXIST:
        printf("CSMI SAS PORT_DOES_NOT_EXIST\n");
        break;
    case CSMI_SAS_PORT_CANNOT_BE_SELECTED:
        printf("CSMI SAS PORT CANNOT BE SELECTED\n");
        break;
    case CSMI_SAS_CONNECTION_FAILED:
        printf("CSMI SAS CONNECTION FAILED\n");
        break;
    case CSMI_SAS_NO_SATA_DEVICE:
        printf("CSMI SAS NO SATA DEVICE\n");
        break;
    case CSMI_SAS_NO_SATA_SIGNATURE:
        printf("CSMI SAS NO SATA SIGNATURE\n");
        break;
    case CSMI_SAS_SCSI_EMULATION:
        printf("CSMI SAS SCSI EMULATION\n");
        break;
    case CSMI_SAS_NOT_AN_END_DEVICE:
        printf("CSMI SAS NOT_AN_END_DEVICE\n");
        break;
    case CSMI_SAS_NO_SCSI_ADDRESS:
        printf("CSMI SAS NO_SCSI_ADDRESS\n");
        break;
    case CSMI_SAS_NO_DEVICE_ADDRESS:
        printf("CSMI SAS NO_DEVICE_ADDRESS\n");
        break;
    default:
        printf("Unknown error code %"PRIu32"\n", returnCode);
        break;
    }
    return;
}
#if defined (_WIN32)
//If the below code doesn't work (from stack overflow), look here: https://msdn.microsoft.com/en-us/library/ms680582(VS.85).aspx
void print_Last_Error(DWORD lastError)
{
    LPSTR messageBuffer = NULL;
    /*size_t size = */
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    printf("Last Error Code was %"PRIu32": %s\n", lastError, messageBuffer);

    //Free the buffer.
    LocalFree(messageBuffer);

}
#else
void print_Last_Error(int lastError)
{
    printf("Last Error Code was %"PRIu32": %s\n", lastError, strerror(lastError));
}
#endif

int get_CSMI_Phy_Info(tDevice *device, PCSMI_SAS_PHY_INFO_BUFFER PhyInfo)
{
    int retval = FAILURE;
#if defined(_WIN32)
    DWORD bytesReturned = 0;
#endif
    memset(PhyInfo, 0, sizeof(CSMI_SAS_PHY_INFO_BUFFER));
    ptrCSMIDevice csmiDev = (ptrCSMIDevice)device->raid_device;
    PhyInfo->IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
    PhyInfo->IoctlHeader.Length = sizeof(CSMI_SAS_PHY_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    PhyInfo->IoctlHeader.Timeout = CSMI_ALL_TIMEOUT;    
    PhyInfo->IoctlHeader.ControlCode = CC_CSMI_SAS_GET_PHY_INFO;

    memcpy(PhyInfo->IoctlHeader.Signature, CSMI_SAS_SIGNATURE,sizeof(CSMI_SAS_SIGNATURE));

#if defined (_WIN32)
    BOOL success = DeviceIoControl(device->os_info.fd,
        IOCTL_SCSI_MINIPORT,
        PhyInfo,
        sizeof(CSMI_SAS_PHY_INFO_BUFFER),
        PhyInfo,
        sizeof(CSMI_SAS_PHY_INFO_BUFFER),
        &bytesReturned,
        NULL);
    device->os_info.last_error = GetLastError();
    if (TRUE != success && bytesReturned > 0)
    {
#else
    int success = ioctl(dh, CC_CSMI_SAS_GET_PHY_INFO, &PhyInfo);
    device->os_info.last_error = errno;
    if(0 != success)
    {
#endif
        retval = FAILURE;
    }
    else
    {
        retval = SUCCESS;
    }
    if (csmiDev->csmiVerbose)
    {
        printf("\n===CSMI SAS GET PHY INFO===\n");
        print_Last_Error(device->os_info.last_error);
        print_IOCTL_Return_Code(PhyInfo->IoctlHeader.ReturnCode);
    }
    return retval;
}
//This is the same above...BUT this only needs a handle and it is only used by the get_Device_Count and get_Device_List calls
int issue_Get_Phy_Info(HANDLE fd, PCSMI_SAS_PHY_INFO_BUFFER PhyInfo)
{
    int retval = FAILURE;
#if defined(_WIN32)
    DWORD bytesReturned = 0;
#endif
    memset(PhyInfo, 0, sizeof(CSMI_SAS_PHY_INFO_BUFFER));

    PhyInfo->IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
    PhyInfo->IoctlHeader.Length = sizeof(CSMI_SAS_PHY_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    PhyInfo->IoctlHeader.Timeout = CSMI_ALL_TIMEOUT;
    PhyInfo->IoctlHeader.ControlCode = CC_CSMI_SAS_GET_PHY_INFO;

    memcpy(PhyInfo->IoctlHeader.Signature, CSMI_SAS_SIGNATURE, sizeof(CSMI_SAS_SIGNATURE));

#if defined (_WIN32)
    if (TRUE != DeviceIoControl(fd,
        IOCTL_SCSI_MINIPORT,
        PhyInfo,
        sizeof(CSMI_SAS_PHY_INFO_BUFFER),
        PhyInfo,
        sizeof(CSMI_SAS_PHY_INFO_BUFFER),
        &bytesReturned,
        NULL) && bytesReturned > 0)
    {
        //device->os_info.last_error = GetLastError();
#else
    if (0 != ioctl(dh, CC_CSMI_SAS_GET_PHY_INFO, &PhyInfo))
    {
        //device->os_info.last_error = errno;
#endif
        retval = FAILURE;
    }
    else
    {
        retval = SUCCESS;
    }
    return retval;
}

int get_CSMI_Controller_Info(tDevice *device, PCSMI_SAS_CNTLR_CONFIG controllerInfo)
{
    int retval = FAILURE;
#if defined (_WIN32)
    DWORD bytesReturned = 0;
#endif
    ptrCSMIDevice csmiDev = (ptrCSMIDevice)device->raid_device;
    CSMI_SAS_CNTLR_CONFIG_BUFFER controllerConfigBuffer;
    
    memset(&controllerConfigBuffer, 0, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER));

    controllerConfigBuffer.IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
    controllerConfigBuffer.IoctlHeader.Length = sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER) - sizeof(IOCTL_HEADER);
    controllerConfigBuffer.IoctlHeader.Timeout = CSMI_ALL_TIMEOUT;
    controllerConfigBuffer.IoctlHeader.ControlCode = CC_CSMI_SAS_GET_CNTLR_CONFIG;

    memcpy(controllerConfigBuffer.IoctlHeader.Signature, CSMI_ALL_SIGNATURE, sizeof(CSMI_ALL_SIGNATURE));

#if defined (_WIN32)
    BOOL success = DeviceIoControl(device->os_info.fd,
        IOCTL_SCSI_MINIPORT,
        &controllerConfigBuffer,
        sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER),
        &controllerConfigBuffer,
        sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER),
        &bytesReturned,
        NULL);
    device->os_info.last_error = GetLastError();
    if (TRUE != success && bytesReturned > 0)
    {
#else
    int success = ioctl(dh, CC_CSMI_SAS_GET_CNTLR_CONFIG, &controllerConfigBuffer);
    device->os_info.last_error = errno;
    if (0 != success)
    {
#endif
        retval = FAILURE;
    }
    else
    {
        retval = SUCCESS;
        memcpy(controllerInfo, &controllerConfigBuffer.Configuration, sizeof(CSMI_SAS_CNTLR_CONFIG));
    }
    if (csmiDev->csmiVerbose)
    {
        printf("\n===CSMI SAS GET CNTLR CONFIG===\n");
        print_Last_Error(device->os_info.last_error);
        print_IOCTL_Return_Code(controllerConfigBuffer.IoctlHeader.ReturnCode);
    }
    return retval;
}


int get_CSMI_Driver_Info(tDevice *device, PCSMI_SAS_DRIVER_INFO driverInfo)
{
    int retval = FAILURE;
#if defined (_WIN32)
    DWORD bytesReturned = 0;
#endif
    ptrCSMIDevice csmiDev = (ptrCSMIDevice)device->raid_device;
    CSMI_SAS_DRIVER_INFO_BUFFER driverInfoBuffer;
    memset(&driverInfoBuffer, 0, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER));

    driverInfoBuffer.IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
    driverInfoBuffer.IoctlHeader.Length = sizeof(CSMI_SAS_DRIVER_INFO_BUFFER) - sizeof(IOCTL_HEADER);
    driverInfoBuffer.IoctlHeader.Timeout = CSMI_ALL_TIMEOUT;
    driverInfoBuffer.IoctlHeader.ControlCode = CC_CSMI_SAS_GET_DRIVER_INFO;

    memcpy(driverInfoBuffer.IoctlHeader.Signature, CSMI_ALL_SIGNATURE, sizeof(CSMI_ALL_SIGNATURE));

#if defined (_WIN32)
    BOOL success = DeviceIoControl(device->os_info.fd,
        IOCTL_SCSI_MINIPORT,
        &driverInfoBuffer,
        sizeof(CSMI_SAS_DRIVER_INFO_BUFFER),
        &driverInfoBuffer,
        sizeof(CSMI_SAS_DRIVER_INFO_BUFFER),
        &bytesReturned,
        NULL);
    device->os_info.last_error = GetLastError();
    if (TRUE != success && bytesReturned > 0)
    {
#else
    int success = ioctl(dh, CC_CSMI_SAS_GET_DRIVER_INFO, pDrvInfo);
    device->os_info.last_error = errno;
    if (0 != success)
    {
#endif
        retval = FAILURE;
    }
    else
    {
        retval = SUCCESS;
        memcpy(driverInfo, &driverInfoBuffer.Information, sizeof(CSMI_SAS_DRIVER_INFO));
    }
    if (csmiDev->csmiVerbose)
    {
        printf("\n===CSMI SAS GET DRIVER INFO===\n");
        print_Last_Error(device->os_info.last_error);
        print_IOCTL_Return_Code(driverInfoBuffer.IoctlHeader.ReturnCode);
    }
    return retval;
}


int get_CSMI_SATA_Signature(tDevice *device, PCSMI_SAS_SATA_SIGNATURE signature)
{
    int retval = FAILURE;
#if defined (_WIN32)
    DWORD bytesReturned = 0;
#endif
    CSMI_SAS_SATA_SIGNATURE_BUFFER signatureBuffer;
    memset(&signatureBuffer, 0, sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER));

    signatureBuffer.IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
    signatureBuffer.IoctlHeader.Length = sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER) - sizeof(IOCTL_HEADER);
    signatureBuffer.IoctlHeader.Timeout = CSMI_ALL_TIMEOUT;
    signatureBuffer.IoctlHeader.ControlCode = CC_CSMI_SAS_GET_SATA_SIGNATURE;
    memcpy(signatureBuffer.IoctlHeader.Signature, CSMI_SAS_SIGNATURE, sizeof(CSMI_SAS_SIGNATURE));

    //set the phy identifier
    ptrCSMIDevice csmiDev = device->raid_device;
    signatureBuffer.Signature.bPhyIdentifier = csmiDev->phyIdentifier;

#if defined (_WIN32)
    BOOL success = DeviceIoControl(device->os_info.fd,
        IOCTL_SCSI_MINIPORT,
        &signatureBuffer,
        sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER),
        &signatureBuffer,
        sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER),
        &bytesReturned,
        NULL);
    device->os_info.last_error = GetLastError();
    if(TRUE != success && bytesReturned > 0)
    {
#else
    int success = ioctl(dh, CC_CSMI_SAS_GET_DRIVER_INFO, pDrvInfo);
    device->os_info.last_error = errno;
    if (0 != success)
    {
#endif
    }
    else
    {
        retval = SUCCESS;
        memcpy(signature, &signatureBuffer.Signature, sizeof(CSMI_SAS_SATA_SIGNATURE));
    }
    if (csmiDev->csmiVerbose)
    {
        printf("\n===CSMI SAS GET SATA SIGNATURE===\n");
        print_Last_Error(device->os_info.last_error);
        print_IOCTL_Return_Code(signatureBuffer.IoctlHeader.ReturnCode);
    }
    return retval;
}


int get_CSMI_SCSI_Address(tDevice *device, ptrCSMISCSIAddress scsiAddress)
{
    int retval = FAILURE;
#if defined (_WIN32)
    DWORD bytesReturned = 0;
#endif
    CSMI_SAS_GET_SCSI_ADDRESS_BUFFER signatureBuffer;
    memset(&signatureBuffer, 0, sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER));

    signatureBuffer.IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
    signatureBuffer.IoctlHeader.Length = sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER) - sizeof(IOCTL_HEADER);
    signatureBuffer.IoctlHeader.Timeout = CSMI_ALL_TIMEOUT;
    signatureBuffer.IoctlHeader.ControlCode = CC_CSMI_SAS_GET_SCSI_ADDRESS;
    memcpy(signatureBuffer.IoctlHeader.Signature, CSMI_SAS_SIGNATURE, sizeof(CSMI_SAS_SIGNATURE));

    //set the phy identifier
    ptrCSMIDevice csmiDev = device->raid_device;
    memcpy(signatureBuffer.bSASAddress, csmiDev->sasAddress, 8);
    //TODO: SAS Lun? Not sure where to get this from...-TJE

#if defined (_WIN32)
    BOOL success = DeviceIoControl(device->os_info.fd,
        IOCTL_SCSI_MINIPORT,
        &signatureBuffer,
        sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER),
        &signatureBuffer,
        sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER),
        &bytesReturned,
        NULL);
    device->os_info.last_error = GetLastError();
    if(TRUE != success && bytesReturned > 0)
    {
#else
    int success = ioctl(dh, CC_CSMI_SAS_GET_DRIVER_INFO, pDrvInfo);
    device->os_info.last_error = errno;
    if (0 != success)
    {
#endif
        retval = FAILURE;
    }
    else
    {
        retval = SUCCESS;
        //copy back the fields we want
        scsiAddress->hostIndex = signatureBuffer.bHostIndex;
        scsiAddress->pathId = signatureBuffer.bPathId;
        scsiAddress->targetId = signatureBuffer.bTargetId;
        scsiAddress->lun = signatureBuffer.bLun;
    }
    if (csmiDev->csmiVerbose)
    {
        printf("\n===CSMI SAS GET SCSI ADDRESS===\n");
        print_Last_Error(device->os_info.last_error);
        print_IOCTL_Return_Code(signatureBuffer.IoctlHeader.ReturnCode);
    }
    return retval;
}

//will check if an atapi drive is attached by checking the signature and sets the drive type for this device to ATAPI drive.
void check_If_ATAPI_Drive(tDevice *device)
{
    ptrCSMIDevice csmiDevice = device->raid_device;
    if (csmiDevice)
    {
        //TODO: Do we need to do more checking before trying to issue this command to get the SATA signature? Most SAS HBAs don't support ATAPI so it may not matter.
        CSMI_SAS_SATA_SIGNATURE signature;
        memset(&signature, 0, sizeof(CSMI_SAS_SATA_SIGNATURE));
        if (get_CSMI_SATA_Signature(device, &signature) == 0)
        {
            pFIS_REG_D2H signatureFIS = (pFIS_REG_D2H)signature.bSignatureFIS;
            if (signatureFIS->lbaMid == 0x14 && signatureFIS->lbaHi == 0xEB)
            {
                //atapi device found
                device->drive_info.drive_type = ATAPI_DRIVE;
            }
        }
    }
}

void print_FIS(uint8_t fis[20])
{
    switch (fis[0])
    {
    case FIS_TYPE_REG_H2D:
    {
        pFIS_REG_H2D h2dFis = (pFIS_REG_H2D)fis;
        printf("\tFisType:\t%02"PRIX8"\n", h2dFis->fisType);
        printf("\tCRRR_PORT:\t%02"PRIX8"\n", h2dFis->byte1);
        //show the byte 1 bitfields independently
        printf("\t\tCommand:\t\t%"PRIX8"\n", h2dFis->crrr_port.c);
        printf("\t\tReserved:\t\t%"PRIX8"\n", h2dFis->crrr_port.rsv0);
        printf("\t\tPort Multiplier Port:\t%"PRIu8"\n", h2dFis->crrr_port.pmport);
        //
        printf("\tCommand:\t\t%02"PRIX8"\n", h2dFis->command);
        printf("\tFeature (7:0):\t\t%02"PRIX8"\n", h2dFis->feature);
        printf("\tLBA (7:0):\t%02"PRIX8"\n", h2dFis->lbaLow);
        printf("\tLBA (15:8):\t%02"PRIX8"\n", h2dFis->lbaMid);
        printf("\tLBA (23:16):\t%02"PRIX8"\n", h2dFis->lbaHi);
        printf("\tDevice:\t\t%02"PRIX8"\n", h2dFis->device);
        printf("\tLBA (31:24):\t%02"PRIX8"\n", h2dFis->lbaLowExt);
        printf("\tLBA (39:32):\t%02"PRIX8"\n", h2dFis->lbaMidExt);
        printf("\tLBA (47:40):\t%02"PRIX8"\n", h2dFis->lbaHiExt);
        printf("\tFeature (15:8):\t%02"PRIX8"\n", h2dFis->featureExt);
        printf("\tCount (7:0):\t%02"PRIX8"\n", h2dFis->sectorCount);
        printf("\tCount (15:8):\t%02"PRIX8"\n", h2dFis->sectorCountExt);
        printf("\tICC:\t\t%02"PRIX8"\n", h2dFis->icc);
        printf("\tControl:\t%02"PRIX8"\n", h2dFis->control);
        printf("\tAux (7:0):\t%02"PRIX8"\n", h2dFis->aux1);
        printf("\tAux (15:8):\t%02"PRIX8"\n", h2dFis->aux2);
        printf("\tAux (23:16):\t%02"PRIX8"\n", h2dFis->aux3);
        printf("\tAux (31:24):\t%02"PRIX8"\n", h2dFis->aux4);
    }
    break;
    case FIS_TYPE_REG_D2H:
    {
        pFIS_REG_D2H d2hFis = (pFIS_REG_D2H)fis;
        printf("\tFisType:\t%02"PRIX8"\n", d2hFis->fisType);
        printf("\tRIRR_PORT:\t%02"PRIX8"\n", d2hFis->byte1);
        //show the byte 1 bitfields independently
        printf("\t\tReserved:\t\t%"PRIX8"\n", d2hFis->rirr_port.rsv1);
        printf("\t\tInterupt:\t\t%"PRIX8"\n", d2hFis->rirr_port.interupt);
        printf("\t\tReserved:\t\t%"PRIX8"\n", d2hFis->rirr_port.rsv0);
        printf("\t\tPort Multiplier Port:\t%"PRIu8"\n", d2hFis->rirr_port.pmport);
        //
        printf("\tStatus:\t\t%02"PRIX8"\n", d2hFis->status);
        printf("\tError:\t\t%02"PRIX8"\n", d2hFis->error);
        printf("\tLBA (7:0):\t%02"PRIX8"\n", d2hFis->lbaLow);
        printf("\tLBA (15:8):\t%02"PRIX8"\n", d2hFis->lbaMid);
        printf("\tLBA (23:16):\t%02"PRIX8"\n", d2hFis->lbaHi);
        printf("\tDevice:\t\t%02"PRIX8"\n", d2hFis->device);
        printf("\tLBA (31:24):\t%02"PRIX8"\n", d2hFis->lbaLowExt);
        printf("\tLBA (39:32):\t%02"PRIX8"\n", d2hFis->lbaMidExt);
        printf("\tLBA (47:40):\t%02"PRIX8"\n", d2hFis->lbaHiExt);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved0);
        printf("\tCount (7:0):\t%02"PRIX8"\n", d2hFis->sectorCount);
        printf("\tCount (15:8):\t%02"PRIX8"\n", d2hFis->sectorCountExt);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved1);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved2);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved3);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved4);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved5);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved6);
    }
    break;
    case FIS_TYPE_PIO_SETUP:
    {
        pFIS_REG_PIO_SETUP d2hFis = (pFIS_REG_PIO_SETUP)fis;
        printf("\tFisType:\t%02"PRIX8"\n", d2hFis->fisType);
        printf("\tRIDR_PORT:\t%02"PRIX8"\n", d2hFis->byte1);
        //show the byte 1 bitfields independently
        printf("\t\tReserved:\t\t%"PRIX8"\n", d2hFis->ridr_port.rsv1);
        printf("\t\tInterupt:\t\t%"PRIX8"\n", d2hFis->ridr_port.interupt);
        printf("\t\tData Direction:\t%"PRIX8"\n", d2hFis->ridr_port.dataDir);
        printf("\t\tReserved:\t\t%"PRIX8"\n", d2hFis->ridr_port.rsv0);
        printf("\t\tPort Multiplier Port:\t%"PRIu8"\n", d2hFis->ridr_port.pmport);
        //
        printf("\tStatus:\t\t%02"PRIX8"\n", d2hFis->status);
        printf("\tError:\t\t%02"PRIX8"\n", d2hFis->error);
        printf("\tLBA (7:0):\t%02"PRIX8"\n", d2hFis->lbaLow);
        printf("\tLBA (15:8):\t%02"PRIX8"\n", d2hFis->lbaMid);
        printf("\tLBA (23:16):\t%02"PRIX8"\n", d2hFis->lbaHi);
        printf("\tDevice:\t\t%02"PRIX8"\n", d2hFis->device);
        printf("\tLBA (31:24):\t%02"PRIX8"\n", d2hFis->lbaLowExt);
        printf("\tLBA (39:32):\t%02"PRIX8"\n", d2hFis->lbaMidExt);
        printf("\tLBA (47:40):\t%02"PRIX8"\n", d2hFis->lbaHiExt);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved0);
        printf("\tCount (7:0):\t%02"PRIX8"\n", d2hFis->sectorCount);
        printf("\tCount (15:8):\t%02"PRIX8"\n", d2hFis->sectorCountExt);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved1);
        printf("\tE_Status:\t%02"PRIX8"\n", d2hFis->eStatus);
        printf("\tTransfer Count (7:0):\t%02"PRIX8"\n", d2hFis->transferCount);
        printf("\tTransfer Count (15:8):\t%02"PRIX8"\n", d2hFis->transferCountHi);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved2);
        printf("\tReserved:\t%02"PRIX8"\n", d2hFis->reserved3);
    }
    break;
    default:
        for (uint8_t fisIter = 0; fisIter < 20; ++fisIter)
        {
            printf("\tFIS[%"PRIu8"]:\t%02"PRIX8"\n", fisIter, fis[fisIter]);
        }
        printf("\n");
        break;
    }
    return;
}

//This is an E0 command, much like SAT to issue a generic command through STP by making the drive look like SCSI...This is likely not even used since SAT does this...
//From section 7.1 of CSMI spec
#define CSMI_ATA_PASSTHROUGH_OP_CODE 0xE0
#define CSMI_PROTOCOL_NON_DATA 0
#define CSMI_PROTOCOL_PIO_IN 1
#define CSMI_PROTOCOL_PIO_OUT 2
#define CSMI_PROTOCOL_DMA_IN 3
#define CSMI_PROTOCOL_DMA_OUT 4
#define CSMI_PROTOCOL_PACKET_IN 5
#define CSMI_PROTOCOL_PACKET_OUT 6
#define CSMI_PROTOCOL_DMA_QUEUED_IN 7
#define CSMI_PROTOCOL_DMA_QUEUED_OUT 8
int build_CSMI_Passthrough_CDB(uint8_t cdb[16], ataPassthroughCommand * ataPtCmd)
{
    int ret = FAILURE;
    if (cdb && ataPtCmd)
    {
        ret = SUCCESS;
        cdb[0] = CSMI_ATA_PASSTHROUGH_OP_CODE;
        switch (ataPtCmd->commadProtocol)
        {
        case ATA_PROTOCOL_PIO:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_PIO_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_PIO_OUT;
            }
            break;
        case ATA_PROTOCOL_DMA:
        case ATA_PROTOCOL_UDMA:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_DMA_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_DMA_OUT;
            }
            break;
        case ATA_PROTOCOL_NO_DATA:
            cdb[1] = CSMI_PROTOCOL_NON_DATA;
            break;
        case ATA_PROTOCOL_DMA_QUE:
        case ATA_PROTOCOL_DMA_FPDMA:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_DMA_QUEUED_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_DMA_QUEUED_OUT;
            }
            break;
        case ATA_PROTOCOL_PACKET:
            if (ataPtCmd->commandDirection == XFER_DATA_IN)
            {
                cdb[1] = CSMI_PROTOCOL_PACKET_IN;
            }
            else
            {
                cdb[1] = CSMI_PROTOCOL_PACKET_OUT;
            }
            break;
        default:
            return FAILURE;
        }
        //check if the command is doing a transfer number in words...otherwise set blocks
        switch (ataPtCmd->tfr.CommandStatus)
        {
        default:
            cdb[1] |= BIT7;//set the blocks bit
            //set block count (OLD spec says this is in terms of 512...might be different for new 4k drives though...not like this is even used though)
            cdb[14] = M_Byte1(ataPtCmd->dataSize / LEGACY_DRIVE_SEC_SIZE);
            cdb[15] = M_Byte0(ataPtCmd->dataSize / LEGACY_DRIVE_SEC_SIZE);
            break;
        case ATA_READ_MULTIPLE:
        case ATA_WRITE_MULTIPLE:
        case ATA_READ_READ_MULTIPLE_EXT:
        case ATA_WRITE_MULTIPLE_EXT:
            cdb[14] = M_Byte1(ataPtCmd->dataSize / sizeof(__u16));
            cdb[15] = M_Byte0(ataPtCmd->dataSize / sizeof(__u16));
            break;
        }
        //set registers
        cdb[2] = ataPtCmd->tfr.CommandStatus;
        cdb[3] = ataPtCmd->tfr.Feature48;
        cdb[4] = ataPtCmd->tfr.ErrorFeature;
        cdb[5] = ataPtCmd->tfr.SectorCount48;
        cdb[6] = ataPtCmd->tfr.SectorCount;
        cdb[7] = ataPtCmd->tfr.LbaHi48;
        cdb[8] = ataPtCmd->tfr.LbaMid48;
        cdb[9] = ataPtCmd->tfr.LbaLow48;
        cdb[10] = ataPtCmd->tfr.LbaHi;
        cdb[11] = ataPtCmd->tfr.LbaMid;
        cdb[12] = ataPtCmd->tfr.LbaLow;
        cdb[13] = ataPtCmd->tfr.DeviceHead;
    }
    return ret;
}
//Only use this if STP passthrough returns CSMI_SAS_SCSI_EMULATION AND SAT commands don't work
//NOTE: Recommend trying to issue a SAT command instead since that standard was adopted and is MORE likely supported than the CDB built in the function above
int send_Vendor_Unique_ATA_Passthrough(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    uint8_t passThroughCDB[CDB_LEN_16] = { 0 };
    if (SUCCESS == build_CSMI_Passthrough_CDB(passThroughCDB, scsiIoCtx->pAtaCmdOpts))
    {
        //build new local context so as not to mess up with anything created above...
        ScsiIoCtx vuPT;
        ptrCSMIDevice csmiDevice = (ptrCSMIDevice)scsiIoCtx->device->raid_device;
        memcpy(vuPT.cdb, passThroughCDB, CDB_LEN_16);
        vuPT.cdbLength = CDB_LEN_16;
        vuPT.dataLength = scsiIoCtx->dataLength;
        vuPT.pdata = scsiIoCtx->pdata;
        vuPT.psense = scsiIoCtx->psense;
        vuPT.senseDataSize = scsiIoCtx->senseDataSize;
        vuPT.device = scsiIoCtx->device;
        vuPT.direction = scsiIoCtx->direction;
        if (csmiDevice->csmiVerbose)
        {
            printf("\n===Attempting Legacy (pre-SAT) ATA Passthrough CDB with SSP===\n");
        }
        //Issue SSP passthrough
        ret = send_SSP_Passthrough_Command(&vuPT);
        //No idea how command status would be returned by the controller though...it's not defined in SPEC...which is why SAT is recommended
    }
    else
    {
        return ret;
    }
    return ret;
}

int send_SSP_Passthrough_Command(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (scsiIoCtx)
    {
        seatimer_t commandTimer;
        memset(&commandTimer, 0, sizeof(seatimer_t));
        PCSMI_SAS_SSP_PASSTHRU_BUFFER ptrSSPPassthrough = NULL;
        size_t allocatedSize = sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER) + scsiIoCtx->dataLength;
        ptrSSPPassthrough = (PCSMI_SAS_SSP_PASSTHRU_BUFFER)malloc(allocatedSize);
        if (!ptrSSPPassthrough)
        {
            return MEMORY_FAILURE;
        }
        memset(ptrSSPPassthrough, 0, allocatedSize);
        //setup the IOCTL header
        ptrSSPPassthrough->IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
        ptrSSPPassthrough->IoctlHeader.Length = (ULONG)(allocatedSize - sizeof(IOCTL_HEADER));
        ptrSSPPassthrough->IoctlHeader.Timeout = scsiIoCtx->timeout;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            ptrSSPPassthrough->IoctlHeader.Timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        }
        else
        {
            if (scsiIoCtx->timeout != 0)
            {
                ptrSSPPassthrough->IoctlHeader.Timeout = scsiIoCtx->timeout;
            }
            else
            {
                ptrSSPPassthrough->IoctlHeader.Timeout = 15;
            }
        }
        ptrSSPPassthrough->IoctlHeader.ControlCode = CC_CSMI_SAS_SSP_PASSTHRU;
        memcpy(ptrSSPPassthrough->IoctlHeader.Signature, CSMI_SAS_SIGNATURE, sizeof(CSMI_SAS_SIGNATURE));
        //setup the parameters
        ptrSSPPassthrough->Parameters.bConnectionRate = CSMI_SAS_LINK_RATE_NEGOTIATED; //use whatever was negotiated. We don't want or need to change this.
        if (scsiIoCtx->cdbLength > 16)
        {
            //split lengths between CDB and additional CDB
            ptrSSPPassthrough->Parameters.bCDBLength = 16;
            memcpy(ptrSSPPassthrough->Parameters.bCDB, scsiIoCtx->cdb, 16);
            ptrSSPPassthrough->Parameters.bAdditionalCDBLength = (scsiIoCtx->cdbLength - 16) / sizeof(uint32_t);//Spec says this can be from 0 to 6 for valid DWORDs!
            memcpy(ptrSSPPassthrough->Parameters.bAdditionalCDB, &scsiIoCtx[16], scsiIoCtx->cdbLength - 16);
        }
        else
        {
            ptrSSPPassthrough->Parameters.bCDBLength = scsiIoCtx->cdbLength;
            memcpy(ptrSSPPassthrough->Parameters.bCDB, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
        }
        ptrSSPPassthrough->Parameters.uDataLength = scsiIoCtx->dataLength;
        switch (scsiIoCtx->direction)
        {
        case XFER_DATA_IN:
            ptrSSPPassthrough->Parameters.uFlags = CSMI_SAS_SSP_READ;
            break;
        case XFER_DATA_OUT:
            memcpy(ptrSSPPassthrough->bDataBuffer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            ptrSSPPassthrough->Parameters.uFlags = CSMI_SAS_SSP_WRITE;
            break;
        case XFER_NO_DATA:
            ptrSSPPassthrough->Parameters.uFlags = CSMI_SAS_SSP_UNSPECIFIED;
            break;
        default:
            safe_Free(ptrSSPPassthrough);
            return NOT_SUPPORTED;
        }
        ptrSSPPassthrough->Parameters.uFlags |= CSMI_SAS_SSP_TASK_ATTRIBUTE_SIMPLE;//not sure if this is needed....-TJE
        //SAS address
        ptrCSMIDevice csmiDevice = (ptrCSMIDevice)scsiIoCtx->device->raid_device;
        memcpy(ptrSSPPassthrough->Parameters.bDestinationSASAddress, csmiDevice->sasAddress, 8);
        //Phy/Port info
        //Line below needs to be tested and debug whether we should use it or what I have now...TJE
        //ptrSSPPassthrough->Parameters.bPortIdentifier = CSMI_SAS_IGNORE_PORT; //phy->bPortIdentifier;
        ptrSSPPassthrough->Parameters.bPhyIdentifier = csmiDevice->phyIdentifier;
        ptrSSPPassthrough->Parameters.bPortIdentifier = csmiDevice->portIdentifier;

        if (csmiDevice->csmiVerbose)
        {
            printf("\n\n===SSP Passthrough===\n");
            printf("Sending CDB:\n");
            print_Data_Buffer(ptrSSPPassthrough->Parameters.bCDB, ptrSSPPassthrough->Parameters.bCDBLength, false);
            printf("\n");
            if (ptrSSPPassthrough->Parameters.bAdditionalCDBLength > 0)
            {
                printf("Additional CDB:\n");
                print_Data_Buffer(ptrSSPPassthrough->Parameters.bAdditionalCDB, ptrSSPPassthrough->Parameters.bAdditionalCDBLength, false);
                printf("\n");
            }
        }

        //Issue the command
#if defined (_WIN32)
        DWORD bytesReturned = 0;
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (overlappedStruct.hEvent == NULL)
        {
            safe_Free(ptrSSPPassthrough);
            return OS_PASSTHROUGH_FAILURE;
        }
        start_Timer(&commandTimer);
        BOOL success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_SCSI_MINIPORT,
            ptrSSPPassthrough,
            (DWORD)allocatedSize,
            ptrSSPPassthrough,
            (DWORD)allocatedSize,
            &bytesReturned,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &bytesReturned, TRUE);
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        if (!success)
        {
            ret = FAILURE;
        }
        else
        {
            ret = SUCCESS;
        }
#else
        start_Timer(&commandTimer);
        ret = ioctl(scsiIoCtx->device->os_info.fd, CC_CSMI_SAS_SSP_PASSTHRU, p_ssp_passthru);
        scsiIoCtx->device->os_info.last_error = errno;
        stop_Timer(&commandTimer);
        scsiIoCtx->device->os_info.last_error = errno;
#endif
        if (csmiDevice->csmiVerbose)
        {
            print_Last_Error(scsiIoCtx->device->os_info.last_error);
            print_IOCTL_Return_Code(ptrSSPPassthrough->IoctlHeader.ReturnCode);
            if (ptrSSPPassthrough->Status.bResponseLength > 0)
            {
                printf("Response Data:\n");
                print_Data_Buffer(ptrSSPPassthrough->Status.bResponse, M_Min(M_BytesTo2ByteValue(ptrSSPPassthrough->Status.bResponseLength[0], ptrSSPPassthrough->Status.bResponseLength[1]), 256), false);
            }
            printf("\n===END SSP Passthrough===\n");
        }
        //TODO: more CSMI error code checks?
        //TODO: need to test and check if we need to make more changes that match
        //copy back result data if there is any
        if (ptrSSPPassthrough->Status.bDataPresent == CSMI_SAS_SSP_RESPONSE_DATA_PRESENT && ptrSSPPassthrough->Status.uDataBytes > 0)
        {
            memcpy(scsiIoCtx->pdata, ptrSSPPassthrough->bDataBuffer, ptrSSPPassthrough->Status.uDataBytes);
        }
        //copy sense data back
        if (ptrSSPPassthrough->Status.bDataPresent == CSMI_SAS_SSP_SENSE_DATA_PRESENT && scsiIoCtx->psense)
        {
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
            memcpy(scsiIoCtx->psense, ptrSSPPassthrough->Status.bResponse, scsiIoCtx->senseDataSize);
        }
        else if (scsiIoCtx->psense)
        {
            memset(scsiIoCtx->psense, 0, scsiIoCtx->senseDataSize);
        }
        safe_Free(ptrSSPPassthrough);
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    }
    return ret;
}

void build_H2D_fis(FIS_REG_H2D *fis, ataTFRBlock *tfr)
{
    fis->fisType = FIS_TYPE_REG_H2D;
    fis->byte1 = COMMAND_BIT_MASK;
    
    fis->command = tfr->CommandStatus;
    
    fis->feature = tfr->ErrorFeature;
    fis->featureExt = tfr->Feature48;
    
    fis->lbaHi = tfr->LbaHi;
    fis->lbaHiExt = tfr->LbaHi48;
    fis->lbaLow = tfr->LbaLow;
    fis->lbaLowExt = tfr->LbaLow48;
    fis->lbaMid = tfr->LbaMid;
    fis->lbaMidExt = tfr->LbaMid48;

    fis->sectorCount = tfr->SectorCount;
    fis->sectorCountExt = tfr->SectorCount48;

    fis->device = tfr->DeviceHead;

    fis->control = tfr->DeviceControl;

    fis->icc = tfr->icc;

    fis->aux1 = tfr->aux1;
    fis->aux2 = tfr->aux2;
    fis->aux3 = tfr->aux3;
    fis->aux4 = tfr->aux4;
}

int send_STP_Passthrough_Command(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    if (scsiIoCtx)
    {
        seatimer_t commandTimer;
        memset(&commandTimer, 0, sizeof(seatimer_t));
        FIS_REG_H2D fisToSend;
        PCSMI_SAS_STP_PASSTHRU_BUFFER pSTPPassthrough = NULL;
        size_t allocatedSize = sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER) + scsiIoCtx->dataLength;
        memset(&fisToSend, 0, sizeof(FIS_REG_H2D));
        pSTPPassthrough = (PCSMI_SAS_STP_PASSTHRU_BUFFER)malloc(allocatedSize);
        if (!pSTPPassthrough)
        {
            return MEMORY_FAILURE;
        }
        memset(pSTPPassthrough, 0, allocatedSize);
        // Set up IOCTL HEADER
        pSTPPassthrough->IoctlHeader.HeaderLength = sizeof(IOCTL_HEADER);
        pSTPPassthrough->IoctlHeader.Length = (ULONG)(allocatedSize - sizeof(IOCTL_HEADER));
        pSTPPassthrough->IoctlHeader.Timeout = scsiIoCtx->timeout;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 && scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
        {
            pSTPPassthrough->IoctlHeader.Timeout = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        }
        else
        {
            if (scsiIoCtx->timeout != 0)
            {
                pSTPPassthrough->IoctlHeader.Timeout = scsiIoCtx->timeout;
            }
            else
            {
                pSTPPassthrough->IoctlHeader.Timeout = 15;
            }
        }
        pSTPPassthrough->IoctlHeader.ControlCode = CC_CSMI_SAS_STP_PASSTHRU;
        memcpy(pSTPPassthrough->IoctlHeader.Signature, CSMI_SAS_SIGNATURE, sizeof(CSMI_SAS_SIGNATURE));
        //set up the parameters
        pSTPPassthrough->Parameters.bConnectionRate = CSMI_SAS_LINK_RATE_NEGOTIATED; //use whatever was negotiated. We don't want or need to change this.
        //SAS address
        ptrCSMIDevice csmiDevice = (ptrCSMIDevice)scsiIoCtx->device->raid_device;
        memcpy(pSTPPassthrough->Parameters.bDestinationSASAddress, csmiDevice->sasAddress, 8);
        //Phy/Port info
        pSTPPassthrough->Parameters.bPhyIdentifier = csmiDevice->phyIdentifier;
        pSTPPassthrough->Parameters.bPortIdentifier = csmiDevice->portIdentifier;
        //create/fill in the FIS
        build_H2D_fis(&fisToSend, &scsiIoCtx->pAtaCmdOpts->tfr);
        memcpy(pSTPPassthrough->Parameters.bCommandFIS, &fisToSend, sizeof(FIS_REG_H2D));
        pSTPPassthrough->Parameters.uDataLength = scsiIoCtx->dataLength;
        //set direction flag
        switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
        {
        case XFER_DATA_IN:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_READ;
            break;
        case XFER_DATA_OUT:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_WRITE;
            //copy data to the output buffer
            memcpy(pSTPPassthrough->bDataBuffer, scsiIoCtx->pdata, scsiIoCtx->dataLength);
            break;
        case XFER_NO_DATA:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_UNSPECIFIED;
            break;
        default:
            return BAD_PARAMETER;
        }
        //set protocol flag
        switch (scsiIoCtx->pAtaCmdOpts->commadProtocol)
        {
        case ATA_PROTOCOL_NO_DATA:
            //no flags to set
            break;
        case ATA_PROTOCOL_PIO:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_PIO;
            break;
        case ATA_PROTOCOL_DMA:
        case ATA_PROTOCOL_UDMA:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_DMA;
            break;
        case ATA_PROTOCOL_PACKET:
        case ATA_PROTOCOL_PACKET_DMA:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_PACKET;
            break;
        case ATA_PROTOCOL_DEV_DIAG:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_EXECUTE_DIAG;
            break;
        case ATA_PROTOCOL_DMA_QUE:
        case ATA_PROTOCOL_DMA_FPDMA:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_DMA_QUEUED;
            break;
        case ATA_PROTOCOL_SOFT_RESET:
            pSTPPassthrough->Parameters.uFlags |= CSMI_SAS_STP_RESET_DEVICE;
            break;
        default:
            safe_Free(pSTPPassthrough);
            return NOT_SUPPORTED;
        }
        if (csmiDevice->csmiVerbose)
        {
            printf("\n\n===STP Passthrough===\n");
            printf("Sending FIS:\n");
            print_FIS(pSTPPassthrough->Parameters.bCommandFIS);
            printf("\n");
        }
        //issue the IO
#if defined (_WIN32)
        DWORD bytesReturned = 0;
        OVERLAPPED overlappedStruct;
        memset(&overlappedStruct, 0, sizeof(OVERLAPPED));
        overlappedStruct.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (overlappedStruct.hEvent == NULL)
        {
            safe_Free(pSTPPassthrough);
            return OS_PASSTHROUGH_FAILURE;
        }
        start_Timer(&commandTimer);
        BOOL success = DeviceIoControl(scsiIoCtx->device->os_info.fd,
            IOCTL_SCSI_MINIPORT,
            pSTPPassthrough,
            (DWORD)allocatedSize,
            pSTPPassthrough,
            (DWORD)allocatedSize,
            &bytesReturned,
            &overlappedStruct);
        scsiIoCtx->device->os_info.last_error = GetLastError();
        if (ERROR_IO_PENDING == scsiIoCtx->device->os_info.last_error)//This will only happen for overlapped commands. If the drive is opened without the overlapped flag, everything will work like old synchronous code.-TJE
        {
            success = GetOverlappedResult(scsiIoCtx->device->os_info.fd, &overlappedStruct, &bytesReturned, TRUE);
        }
        else if (scsiIoCtx->device->os_info.last_error != ERROR_SUCCESS)
        {
            ret = OS_PASSTHROUGH_FAILURE;
        }
        stop_Timer(&commandTimer);
        CloseHandle(overlappedStruct.hEvent);//close the overlapped handle since it isn't needed any more...-TJE
        overlappedStruct.hEvent = NULL;
        if (!success)
        {
            ret = FAILURE;
        }
        else
        {
            scsiIoCtx->device->os_info.last_error = 0;
            ret = SUCCESS;
        }
#else
        start_Timer(&commandTimer);
        ret = ioctl(scsiIoCtx->device->os_info.fd, CC_CSMI_SAS_STP_PASSTHRU, pSTPPassthrough);
        scsiIoCtx->device->os_info.last_error = errno;
        stop_Timer(&commandTimer);
        scsiIoCtx->device->os_info.last_error = errno;
#endif
        scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
        if (csmiDevice->csmiVerbose)
        {
            print_Last_Error(scsiIoCtx->device->os_info.last_error);
            print_IOCTL_Return_Code(pSTPPassthrough->IoctlHeader.ReturnCode);
            printf("Returned FIS (Assuming IOCTL was successful and did send something):\n");
            print_FIS(pSTPPassthrough->Status.bStatusFIS);
            printf("\n===END STP Passthrough===\n");
        }
        /*
        //revisit this check. According to the spec this can be set to send SSP commands instead, BUT on my system this failed. It shows up as a SATA controller reporting this and the IO WAS successful
        if (pSTPPassthrough->IoctlHeader.ReturnCode = CSMI_SAS_SCSI_EMULATION)
        {
            ptrCSMIDevice csmiDev = (ptrCSMIDevice)scsiIoCtx->device->raid_device;
            csmiDev->useSSPInsteadOfSTP = true;
            return send_SSP_Passthrough_Command(scsiIoCtx);
        }
        */
        //TODO: more CSMI error code checks?
        //IO complete. Now let's copy back any results (mask into sense data for above layers like we do with ATA passthrough in Windows and FreeBSD)
        //TODO? Check Status.uSCR? These are the status control registers. We may or maynot need to check these too
        ataReturnTFRs rtfrs;//create this temporarily to save fis output results, then we'll pack it into sense data
        memset(&rtfrs, 0, sizeof(ataReturnTFRs));
        switch (pSTPPassthrough->Status.bStatusFIS[0])
        {
        case FIS_TYPE_REG_D2H://Spec says we'll get this and we should mostly get this...but we'll handle some others below this case too.
        {
            pFIS_REG_D2H fisD2H = (pFIS_REG_D2H)&pSTPPassthrough->Status.bStatusFIS[0];
            rtfrs.status = fisD2H->status;
            rtfrs.error = fisD2H->error;
            rtfrs.lbaHi = fisD2H->lbaHi;
            rtfrs.lbaHiExt = fisD2H->lbaHiExt;
            rtfrs.lbaMid = fisD2H->lbaMid;
            rtfrs.lbaMidExt = fisD2H->lbaMidExt;
            rtfrs.lbaLow = fisD2H->lbaLow;
            rtfrs.lbaLowExt = fisD2H->lbaLowExt;
            rtfrs.secCnt = fisD2H->sectorCount;
            rtfrs.secCntExt = fisD2H->sectorCountExt;
            rtfrs.device = fisD2H->device;
        }
            break;
        case FIS_TYPE_PIO_SETUP://for when we get a PIO command status back (PIO in only as out will send back a D2H)
        {
            pFIS_REG_PIO_SETUP fisPIO = (pFIS_REG_PIO_SETUP)&pSTPPassthrough->Status.bStatusFIS[0];
            rtfrs.status = fisPIO->eStatus;//Get the E_Status! This is the ACTUAL command completion. This is what we really want in layers above.
            rtfrs.error = fisPIO->error;
            rtfrs.lbaHi = fisPIO->lbaHi;
            rtfrs.lbaHiExt = fisPIO->lbaHiExt;
            rtfrs.lbaMid = fisPIO->lbaMid;
            rtfrs.lbaMidExt = fisPIO->lbaMidExt;
            rtfrs.lbaLow = fisPIO->lbaLow;
            rtfrs.lbaLowExt = fisPIO->lbaLowExt;
            rtfrs.secCnt = fisPIO->sectorCount;
            rtfrs.secCntExt = fisPIO->sectorCountExt;
            rtfrs.device = fisPIO->device;
        }
            break;
        case FIS_TYPE_DEV_BITS:
            //TODO: handle this fis for async commands
            //This may not be even needed since I think async commands will send back a fis 34 which we handle above.
            break;
        }
        //copy rtfrs to sense data that will be parsed above.
        if (scsiIoCtx->psense)//check that the pointer is valid
        {
            if (scsiIoCtx->senseDataSize >= 22)//check that the sense data buffer is big enough to fill in our rtfrs using descriptor format
            {
                scsiIoCtx->returnStatus.format = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->returnStatus.senseKey = 0x01;//check condition
                //setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->returnStatus.asc = 0x00;
                scsiIoCtx->returnStatus.ascq = 0x1D;
                //now fill in the sens buffer
                scsiIoCtx->psense[0] = SCSI_SENSE_CUR_INFO_DESC;
                scsiIoCtx->psense[1] = 0x01;//recovered error
                //setting ASC/ASCQ to ATA Passthrough Information Available
                scsiIoCtx->psense[2] = 0x00;//ASC
                scsiIoCtx->psense[3] = 0x1D;//ASCQ
                scsiIoCtx->psense[4] = 0;
                scsiIoCtx->psense[5] = 0;
                scsiIoCtx->psense[6] = 0;
                scsiIoCtx->psense[7] = 0x0E;//additional sense length
                scsiIoCtx->psense[8] = 0x09;//descriptor code
                scsiIoCtx->psense[9] = 0x0C;//additional descriptor length
                scsiIoCtx->psense[10] = 0;
                if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
                {
                    scsiIoCtx->psense[10] |= 0x01;//set the extend bit
                    //fill in the ext registers while we're in this if...no need for another one
                    scsiIoCtx->psense[12] = rtfrs.secCntExt;// Sector Count Ext
                    scsiIoCtx->psense[14] = rtfrs.lbaLowExt;// LBA Lo Ext
                    scsiIoCtx->psense[16] = rtfrs.lbaMidExt;// LBA Mid Ext
                    scsiIoCtx->psense[18] = rtfrs.lbaHiExt;// LBA Hi
                }
                //fill in the returned 28bit registers
                scsiIoCtx->psense[11] = rtfrs.error;// Error
                scsiIoCtx->psense[13] = rtfrs.secCnt;// Sector Count
                scsiIoCtx->psense[15] = rtfrs.lbaLow;// LBA Lo
                scsiIoCtx->psense[17] = rtfrs.lbaMid;// LBA Mid
                scsiIoCtx->psense[19] = rtfrs.lbaHi;// LBA Hi
                scsiIoCtx->psense[20] = rtfrs.device;// Device/Head
                scsiIoCtx->psense[21] = rtfrs.status;// Status
            }
        }
        if (scsiIoCtx->pdata && scsiIoCtx->pAtaCmdOpts->commandDirection == XFER_DATA_IN && pSTPPassthrough->Status.uDataBytes > 0)
        {
            //copy the received data back to the scsiIoCtx.
            memcpy(scsiIoCtx->pdata, pSTPPassthrough->bDataBuffer, M_Min(scsiIoCtx->dataLength, pSTPPassthrough->Status.uDataBytes));
        }
        safe_Free(pSTPPassthrough);
    }
    return ret;
}

//-----------------------------------------------------------------------------
//
//  close_Device()
//
//! \brief   Description:  Given a device, close it's handle.
//
//  Entry:
//!   \param[in] device = device stuct that holds device information.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int close_CSMI_Device(tDevice *device)
{
    //int retValue = 0;
    if (device)
    {
        // We don't actually call the CloseHandle function since we opened \\.\SCSIx: which can point to multiple devices since this layer can be used to talk to 
        // multiple devices and we don't want to break any other device that is being used.
        // Instead we'll just change the handle to an invalid value and free the memory we allocated in the device structure for talking to the drive (allocated in get_CSMI_Device)
        //retValue = CloseHandle(device->os_info.fd);
        //device->os_info.last_error = GetLastError();
        safe_Free(device->raid_device);
        device->os_info.last_error = 0;
#if defined (_WIN32)
        device->os_info.fd = INVALID_HANDLE_VALUE;
#else
        device->os_info.fd = -1;
#endif
        return SUCCESS;
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

int get_CSMI_Device(const char *filename, tDevice *device)
{
    int ret = FAILURE;
    bool handleHasAddressInsteadOfPort = false;
    uint8_t givenSASAddress[8] = { 0 };//only valid if above bool is set to true
    //Need to open this handle and setup some information then fill in the device information.
    //printf("%s -->\n Opening Device %s\n",__FUNCTION__, filename);
    if (!(validate_Device_Struct(device->sanity)))
    {
        return LIBRARY_MISMATCH;
    }
    //set the handle name first...since the tokenizing below will break it apart
    memcpy(device->os_info.name, filename, strlen(filename));
#if defined (_WIN32)
    uint64_t controllerNum = 0, portNum = 0;
    int sscanfret = sscanf(filename, "\\\\.\\SCSI%"SCNu64":%"SCNu64"", &controllerNum, &portNum);
    if (sscanfret != 0 && sscanfret != EOF)
    {
        sprintf(device->os_info.friendlyName, "SCSI%" PRIu64 ":%" PRIu64, controllerNum, portNum);
    }
    else
    {
        //TODO: what should we do when we fail to parse this from the handle???
    }
#else
    //TODO: handle non-Windows OS with CSMI
#endif

    uint8_t portNumber = 0xFF;//something crazy and likely not used.
    //need to copy the incoming file name into - \\.\SCSI1: and a separate port
    char handle[40] = { 0 };
    char *token = strtok((char*)filename, ":");
    int count = 0;
    while (token)
    {
        switch (count)
        {
        case 0:// \\.\SCSI,ctrl#>:
            strcpy(handle, token);
            strcat(handle, ":");//add the colon since the strtok strips it off.
            break;
        case 1:// port number
            if (strlen(token) == 16)
            {
                //sas address given
                handleHasAddressInsteadOfPort = true;
#if !defined (_MSC_VER)
                //The sscanf code below SHOULD work, but it appears that I am getting stack corruption. looking at the memory debugger, it seems to treat them as 32bit numbers instead of 8 as specified. Found in VS2013. Not sure if this affects newer versions.
                sscanf(token, "%"PRIX8"%"PRIX8"%"PRIX8"%"PRIX8"%"PRIX8"%"PRIX8"%"PRIX8"%"PRIX8"", &givenSASAddress[0], &givenSASAddress[1], &givenSASAddress[2], &givenSASAddress[3], &givenSASAddress[4], &givenSASAddress[5], &givenSASAddress[6], &givenSASAddress[7]);
#else
                //This workaround code is annoying but works...
                char address[17] = { 0 };
                strcpy(address, token);
                for (int8_t vsIsStupidIter = 7; vsIsStupidIter >= 0; --vsIsStupidIter)
                {
                    size_t tokenLength = strlen(address);
                    uint32_t temp = 0;
                    if (EOF != sscanf(address, "%"PRIX32, &temp))
                    {
                        givenSASAddress[vsIsStupidIter] = (uint8_t)temp;
                        //now remove the 2 end characters from token for next iteration through the loop
                        address[tokenLength - 2] = '\0';
                        address[tokenLength - 1] = '\0';
                    }
                }
#endif
            }
            else
            {
                portNumber = atoi(token);
            }
            break;
        default:
            break;
        }
        token = strtok(NULL, ":");
        ++count;
    }

#if defined(_WIN32)
    TCHAR device_name[40] = { 0 };
    CONST TCHAR *ptrDeviceName = &device_name[0];
    _stprintf_s(device_name, 40, TEXT("%hs"), handle);

    //lets try to open the device.
    device->os_info.fd = CreateFile(ptrDeviceName,
        GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS, 
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
        FILE_FLAG_OVERLAPPED,
#else
        0,
#endif
        NULL);
    //DWORD lastError = GetLastError();
    if (device->os_info.fd != INVALID_HANDLE_VALUE)
#else
    if ((device->os_info.fd = open(filename, O_RDWR | O_NONBLOCK)) < 0)
#endif
    {
        device->os_info.minimumAlignment = sizeof(void *);//setting alignment this way to be compatible across OSs since CSMI doesn't really dictate an alignment, but we should set something. - TJE
        device->raid_device = (ptrCSMIDevice)calloc(1, sizeof(csmiDevice));
        device->issue_io = (issue_io_func)send_CSMI_IO;
        ptrCSMIDevice csmiDevice = (ptrCSMIDevice)device->raid_device;
        if (!csmiDevice)
        {
            return MEMORY_FAILURE;
        }
        if (device->dFlags & CSMI_FLAG_VERBOSE)
        {
            csmiDevice->csmiVerbose = true;
        }
        //we were able to open the requested handle...now it's time to collect some information we'll need to save for this device so we can talk to it later.
        //get some controller/driver into then start checking for connected ports and increment the counter.
        get_CSMI_Controller_Info(device, &csmiDevice->controllerConfig);
        //TODO: check controller info
        get_CSMI_Driver_Info(device, &csmiDevice->driverInfo);
        //TODO: check driver info
        CSMI_SAS_PHY_INFO_BUFFER phyInfo;
        get_CSMI_Phy_Info(device, &phyInfo);
        //Using the data we've already gotten, we need to save phyidentifier, port identifier, port protocol, and SAS address.
        //TODO: Check if we should be using the Identify or Attached structure information to populate the support fields.
        //Identify appears to contain initiator data, and attached seems to include target data...
        if (portNumber > 31)
        {
            //return this or some other error?
            return FAILURE;
        }
        if (!handleHasAddressInsteadOfPort)
        {
            csmiDevice->portIdentifier = phyInfo.Information.Phy[portNumber].bPortIdentifier;
            csmiDevice->phyIdentifier = phyInfo.Information.Phy[portNumber].Attached.bPhyIdentifier;
            csmiDevice->portProtocol = phyInfo.Information.Phy[portNumber].Attached.bTargetPortProtocol;
            memcpy(csmiDevice->sasAddress, phyInfo.Information.Phy[portNumber].Attached.bSASAddress, 8);
            //save the phy info.
            memcpy(&csmiDevice->phyInfo, &phyInfo.Information.Phy[portNumber], sizeof(CSMI_SAS_PHY_ENTITY));
        }
        else
        {
            //hard coded 32 in this loop is from the phyInfo structure that holds a maximum of 32 phys.
            for (uint8_t portIter = 0; portIter < 32 && portIter < phyInfo.Information.bNumberOfPhys; ++portIter)
            {
                if (memcmp(givenSASAddress, phyInfo.Information.Phy[portIter].Attached.bSASAddress, 8) == 0)
                {
                    //Found the matching PHY!
                    csmiDevice->portIdentifier = phyInfo.Information.Phy[portIter].bPortIdentifier;
                    csmiDevice->phyIdentifier = phyInfo.Information.Phy[portIter].Attached.bPhyIdentifier;
                    csmiDevice->portProtocol = phyInfo.Information.Phy[portIter].Attached.bTargetPortProtocol;
                    memcpy(&csmiDevice->phyInfo, &phyInfo.Information.Phy[portIter], sizeof(CSMI_SAS_PHY_ENTITY));
                    break;
                }
            }
        }
        device->drive_info.interface_type = RAID_INTERFACE;

        //Get SCSI address
        if (SUCCESS == get_CSMI_SCSI_Address(device, &csmiDevice->scsiAddress))
        {
            csmiDevice->scsiAddressValid = true;
        }
        //Get SATA signature
        if (SUCCESS == get_CSMI_SATA_Signature(device, &csmiDevice->sataSignature))
        {
            csmiDevice->sataSignatureValid = true;
        }

        //setup remaining flags before we issue commands
        if (device->dFlags & CSMI_FLAG_FORCE_STP)
        {
            csmiDevice->portProtocol = CSMI_SAS_PROTOCOL_STP;
        }
        if (device->dFlags & CSMI_FLAG_FORCE_SSP)
        {
            csmiDevice->portProtocol = CSMI_SAS_PROTOCOL_SSP;
        }
        if (device->dFlags & CSMI_FLAG_FORCE_PRE_SAT_VU_PASSTHROUGH)
        {
            csmiDevice->portProtocol = CSMI_SAS_PROTOCOL_SSP;
            csmiDevice->ataVendorUniquePT = true;
        }
        //these flags may help some weird drivers/controllers...default behaviour should be to use whatever we can get back from the driver...I mean...if it told us something it makes sense to keep using it.-TJE
        if (device->dFlags & CSMI_FLAG_USE_PORT)
        {
            csmiDevice->phyIdentifier = CSMI_SAS_USE_PORT_IDENTIFIER;
        }
        if (device->dFlags & CSMI_FLAG_IGNORE_PORT)
        {
            csmiDevice->portIdentifier = CSMI_SAS_IGNORE_PORT;
        }
        
        switch (csmiDevice->portProtocol)
        {
        case CSMI_SAS_PROTOCOL_SATA:
        case CSMI_SAS_PROTOCOL_STP:
            ret = fill_In_ATA_Drive_Info(device);
            break;
        case CSMI_SAS_PROTOCOL_SSP:
            if (csmiDevice->ataVendorUniquePT)
            {
                ret = fill_In_ATA_Drive_Info(device);
            }
            else
            {
                ret = fill_In_Device_Info(device);
            }
            break;
        default:
            ret = fill_Drive_Info_Data(device);
            break;
        }
    }
    return ret;
}

//-----------------------------------------------------------------------------
//
//  get_Device_Count()
//
//! \brief   Description:  Get the count of devices in the system that this library
//!                        can talk to. This function is used in conjunction with
//!                        get_Device_List, so that enough memory is allocated.
//
//  Entry:
//!   \param[out] numberOfDevices = integer to hold the number of devices found.
//!   \param[in] flags = eScanFlags based mask to let application control.
//!                      NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_CSMI_Device_Count(uint32_t * numberOfDevices, uint64_t flags)
{
#if defined (_WIN32)
    HANDLE fd = NULL;
#if defined (UNICODE)
    wchar_t deviceName[40] = { 0 };
#else
    char deviceName[40] = { 0 };
#endif
#else
    int fd = -1;
#endif
    int  controllerNumber = 0, driveNumber = 0, found = 0;
    for (controllerNumber = 0; controllerNumber < OPENSEA_MAX_CONTROLLERS; ++controllerNumber)
    {
#if defined (_WIN32)
#if defined (UNICODE)
        wsprintf(deviceName, L"\\\\.\\SCSI%d:", controllerNumber);
#else
        snprintf(deviceName, sizeof(deviceName), "\\\\.\\SCSI%d:", controllerNumber);
#endif
        //lets try to open the controller.
        fd = CreateFile(deviceName,
            GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS, 
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
            FILE_FLAG_OVERLAPPED,
#else
            0,
#endif
            NULL);
        if (fd != INVALID_HANDLE_VALUE)
#else
        if ((fd = open(filename, O_RDWR | O_NONBLOCK)) < 0)
#endif
        {
            CSMI_SAS_PHY_INFO_BUFFER phyInfo;
            issue_Get_Phy_Info(fd, &phyInfo);
            //check phy info for valid devices. expanders will be ignored for now...if we add them in, we'll need to also add SMP passthrough
            for (driveNumber = 0; ((driveNumber <= phyInfo.Information.bNumberOfPhys) && (driveNumber < MAX_DEVICES_PER_CONTROLLER)); ++driveNumber)
            {
                switch(phyInfo.Information.Phy[driveNumber].Attached.bDeviceType)
                {
                case CSMI_SAS_END_DEVICE:
                    ++found;
                    break;
                case CSMI_SAS_EDGE_EXPANDER_DEVICE:
                case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
                case CSMI_SAS_NO_DEVICE_ATTACHED:
                default:
                    break;
                }
            }
        }
        //close handle to the controller
#if defined (_WIN32)
        if (fd != INVALID_HANDLE_VALUE)
        {
            CloseHandle(fd);
        }
#else
        if (fd < 0)
        {
            close(fd);
        }
#endif
        
    }
    *numberOfDevices = found;

    return SUCCESS;
}

//-----------------------------------------------------------------------------
//
//  get_Device_List()
//
//! \brief   Description:  Get a list of devices that the library supports.
//!                        Use get_Device_Count to figure out how much memory is
//!                        needed to be allocated for the device list. The memory
//!                        allocated must be the multiple of device structure.
//!                        The application can pass in less memory than needed
//!                        for all devices in the system, in which case the library
//!                        will fill the provided memory with how ever many device
//!                        structures it can hold.
//  Entry:
//!   \param[out] ptrToDeviceList = pointer to the allocated memory for the device list
//!   \param[in]  sizeInBytes = size of the entire list in bytes.
//!   \param[in]  versionBlock = versionBlock structure filled in by application for
//!                              sanity check by library.
//!   \param[in] flags = eScanFlags based mask to let application control.
//!                      NOTE: currently flags param is not being used.
//!
//  Exit:
//!   \return SUCCESS - pass, WARN_NOT_ALL_DEVICES_ENUMERATED - some deviec had trouble being enumerated. 
//!                     Validate that it's drive_type is not UNKNOWN_DRIVE, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
int get_CSMI_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    int returnValue = SUCCESS;
    int numberOfDevices = 0;
    int controllerNumber = 0, driveNumber = 0, found = 0, failedGetDeviceCount = 0;
#if defined (_WIN32)
#if !defined (UNICODE)
    char deviceName[40] = { 0 };
#else
    wchar_t deviceName[40] = { 0 };
#endif
    char    name[80] = { 0 }; //Because get device needs char
    HANDLE fd = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
    char deviceName[40] = { 0 };
#endif
    tDevice * d = NULL;

    //TODO: Check if sizeInBytes is a multiple of
    if (!(ptrToDeviceList) || (!sizeInBytes))
    {
        returnValue = BAD_PARAMETER;
    }
    else if ((!(validate_Device_Struct(ver))))
    {
        returnValue = LIBRARY_MISMATCH;
    }
    else
    {
        numberOfDevices = sizeInBytes / sizeof(tDevice);
        d = ptrToDeviceList;
        for (controllerNumber = 0; controllerNumber < OPENSEA_MAX_CONTROLLERS && (found < numberOfDevices); ++controllerNumber)
        {
            //TODO: get controller info and only try to go further when we have a phy/port with an attached device.
#if defined(_WIN32)
#if !defined (UNICODE)
            snprintf(deviceName, sizeof(deviceName), "\\\\.\\SCSI%d:", controllerNumber);
#else
            wsprintf(deviceName, L"\\\\.\\SCSI%d:", controllerNumber);
#endif
            //lets try to open the device.
            fd = CreateFile((LPCTSTR)deviceName,
                GENERIC_WRITE | GENERIC_READ, //FILE_ALL_ACCESS, 
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
#if !defined(WINDOWS_DISABLE_OVERLAPPED)
                FILE_FLAG_OVERLAPPED,
#else
                0,
#endif
                NULL);
            if (fd != INVALID_HANDLE_VALUE)
#else
            if ((fd = open(filename, O_RDWR | O_NONBLOCK)) < 0)
#endif
            {
                //get some controller/driver into then start checking for connected ports and increment the counter.
                CSMI_SAS_PHY_INFO_BUFFER phyInfo;
                issue_Get_Phy_Info(fd, &phyInfo);
                //check phy info for valid devices. expanders will be ignored for now...if we add them in, we'll need to also add SMP passthrough-TJE
                for (driveNumber = 0; ((driveNumber <= phyInfo.Information.bNumberOfPhys) && (driveNumber < MAX_DEVICES_PER_CONTROLLER)); ++driveNumber)
                {
                    switch (phyInfo.Information.Phy[driveNumber].Attached.bDeviceType)
                    {
                    case CSMI_SAS_END_DEVICE:
                        //call get_CSMI_Device to fill in the drive information and setup all the remaining flags
#if defined(_WIN32)
                        _snprintf(name, 80, "%s%d:%d", WIN_CSMI_DRIVE, controllerNumber, driveNumber);
#else
                        snprintf(name, 80, "%s%d:%d", LIN_CSMI_DRIVE, controllerNumber, driveNumber);
#endif
                        memset(d, 0, sizeof(tDevice));
                        d->sanity.size = ver.size;
                        d->sanity.version = ver.version;
                        d->dFlags = flags;
                        returnValue = get_CSMI_Device(name, d);
                        if (returnValue != SUCCESS)
                        {
                            failedGetDeviceCount++;
                        }
                        found++;
                        d++;
                        break;
                    case CSMI_SAS_EDGE_EXPANDER_DEVICE://move this case above to support this device type when SMP passthrough support is added
                    case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
                    case CSMI_SAS_NO_DEVICE_ATTACHED:
                    default:
                        break;
                    }
                }
                //close the handle to the controller once we're done since we will have opened the drive specific handle for each specific drive. (\\.\SCSI<controller>:<port>
#if defined(_WIN32)
                CloseHandle(fd);
#else
                close(fd);
#endif
            }
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (failedGetDeviceCount)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    return returnValue;
}

int send_CSMI_IO(ScsiIoCtx *scsiIoCtx)
{
    int ret = OS_PASSTHROUGH_FAILURE;
    ptrCSMIDevice csmiDev = (ptrCSMIDevice)scsiIoCtx->device->raid_device;
    switch (csmiDev->portProtocol)
    {
    case CSMI_SAS_PROTOCOL_SATA:
        if (csmiDev->useSSPInsteadOfSTP)
        {
            ret = send_SSP_Passthrough_Command(scsiIoCtx);
        }
        else if (scsiIoCtx->pAtaCmdOpts)
        {
            ret = send_STP_Passthrough_Command(scsiIoCtx);
        }
        else
        {
            //Software SAT translation
            ret = translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        }
        break;
    case CSMI_SAS_PROTOCOL_STP:
        if (scsiIoCtx->pAtaCmdOpts && !csmiDev->useSSPInsteadOfSTP)
        {
            ret = send_STP_Passthrough_Command(scsiIoCtx);
        }
        else
        {
            ret = send_SSP_Passthrough_Command(scsiIoCtx);
        }
        break;
    case CSMI_SAS_PROTOCOL_SSP:
        if (csmiDev->ataVendorUniquePT)//This will likely never be used other than debugging unless we talk to a REALLY old driver.-TJE
        {
            ret = send_Vendor_Unique_ATA_Passthrough(scsiIoCtx);
        }
        else
        {
            ret = send_SSP_Passthrough_Command(scsiIoCtx);
        }
        break;
    case CSMI_SAS_PROTOCOL_SMP://NOT Supported. Just return OS_PASS_THROUGH_FAILURE
    default:
        break;
    }
    return ret;
}

void print_CSMI_Device_Info(tDevice * device)
{
    if (device && device->raid_device)
    {
        ptrCSMIDevice csmiDevice = (ptrCSMIDevice)device->raid_device;
        printf("\n======================\n");
        printf("     CSMI Device Info    \n");
        printf("======================\n");
        printf("\n===Driver Info===\n");
        printf("Name: %s\n", csmiDevice->driverInfo.szName);
        printf("Description: %s\n", csmiDevice->driverInfo.szDescription);
        printf("Driver version (maj.min.bld.rel): %"PRIu16".%"PRIu16".%"PRIu16".%"PRIu16"\n", csmiDevice->driverInfo.usMajorRevision, csmiDevice->driverInfo.usMinorRevision, csmiDevice->driverInfo.usBuildRevision, csmiDevice->driverInfo.usReleaseRevision);
        printf("CSMI Version (maj.min): %"PRIu16".%"PRIu16"\n", csmiDevice->driverInfo.usCSMIMajorRevision, csmiDevice->driverInfo.usCSMIMinorRevision);
        
        printf("\n===Controller Config===\n");
        printf("Base IO Address: %"PRIX32"h\n", csmiDevice->controllerConfig.uBaseIoAddress);
        printf("Board ID: %"PRIX32"h\n", csmiDevice->controllerConfig.uBoardID);
        printf("Slot Number: %"PRIX16"h\n", csmiDevice->controllerConfig.usSlotNumber);
        printf("Controller Class: %"PRIX8"h\n", csmiDevice->controllerConfig.bControllerClass);//CSMI_SAS_CNTLR_CLASS_HBA = 5
        if (csmiDevice->controllerConfig.bControllerClass == CSMI_SAS_CNTLR_CLASS_HBA)
        {
            printf("\tHBA Class\n");
        }
        else
        {
            printf("\tUnknown controller class\n");
        }
        printf("IO Bus Type: ");
        switch (csmiDevice->controllerConfig.bIoBusType)
        {
        case CSMI_SAS_BUS_TYPE_PCI:
            printf("PCI\n");
            break;
        case CSMI_SAS_BUS_TYPE_PCMCIA:
            printf("PCMCIA\n");
            break;
        default:
            printf("Unknown %" PRIX8 "h\n", csmiDevice->controllerConfig.bIoBusType);
            break;
        }
        printf("PCI Bus Address: \n");
        printf("\tBus Number: %" PRIX8 "h\n", csmiDevice->controllerConfig.BusAddress.PciAddress.bBusNumber);
        printf("\tDevice Number: %" PRIX8 "h\n", csmiDevice->controllerConfig.BusAddress.PciAddress.bDeviceNumber);
        printf("\tFunction Number: %" PRIX8 "h\n", csmiDevice->controllerConfig.BusAddress.PciAddress.bFunctionNumber);
        printf("Serial Number: %s\n", csmiDevice->controllerConfig.szSerialNumber);
        printf("Version (maj.min.bld.rel): %" PRIu16 ".%" PRIu16 ".%" PRIu16 ".%" PRIu16 "\n", csmiDevice->controllerConfig.usMajorRevision, csmiDevice->controllerConfig.usMinorRevision, csmiDevice->controllerConfig.usBuildRevision, csmiDevice->controllerConfig.usReleaseRevision);
        printf("BIOS Version (maj.min.bld.rel): %" PRIu16 ".%" PRIu16 ".%" PRIu16 ".%" PRIu16 "\n", csmiDevice->controllerConfig.usBIOSMajorRevision, csmiDevice->controllerConfig.usBIOSMinorRevision, csmiDevice->controllerConfig.usBIOSBuildRevision, csmiDevice->controllerConfig.usBIOSReleaseRevision);
        printf("Controller Flags: %" PRIX32 "\n", csmiDevice->controllerConfig.uControllerFlags);
        //check the flags and print them out
        if (CSMI_SAS_CNTLR_SAS_HBA & csmiDevice->controllerConfig.uControllerFlags)
        {
            printf("\tSAS HBA\n");
        }
        if (CSMI_SAS_CNTLR_SAS_RAID & csmiDevice->controllerConfig.uControllerFlags)
        {
            printf("\tSAS RAID\n");
        }
        if (CSMI_SAS_CNTLR_SATA_HBA & csmiDevice->controllerConfig.uControllerFlags)
        {
            printf("\tSATA HBA\n");
        }
        if (CSMI_SAS_CNTLR_SATA_RAID & csmiDevice->controllerConfig.uControllerFlags)
        {
            printf("\tSATA RAID\n");
        }
        if (CSMI_SAS_CNTLR_SMART_ARRAY & csmiDevice->controllerConfig.uControllerFlags)
        {
            printf("\tSMART Array\n");
        }
        printf("ROM Version (maj.min.bld.rel): %"PRIu16".%"PRIu16".%"PRIu16".%"PRIu16"\n", csmiDevice->controllerConfig.usRromMajorRevision, csmiDevice->controllerConfig.usRromMinorRevision, csmiDevice->controllerConfig.usRromBuildRevision, csmiDevice->controllerConfig.usRromReleaseRevision);
        printf("ROM BIOS Version (maj.min.bld.rel): %"PRIu16".%"PRIu16".%"PRIu16".%"PRIu16"\n", csmiDevice->controllerConfig.usRromBIOSMajorRevision, csmiDevice->controllerConfig.usRromBIOSMinorRevision, csmiDevice->controllerConfig.usRromBIOSBuildRevision, csmiDevice->controllerConfig.usRromBIOSReleaseRevision);

        printf("\n===PHY Info for this device===\n");
        printf("Identify:\n");
        printf("\tDevice Type: ");
        switch (csmiDevice->phyInfo.Identify.bDeviceType)
        {
        case CSMI_SAS_NO_DEVICE_ATTACHED:
            printf("No device attached\n");
            break;
        case CSMI_SAS_END_DEVICE:
            printf("End Device\n");
            break;
        case CSMI_SAS_EDGE_EXPANDER_DEVICE:
            printf("Edge Expanded Device\n");
            break;
        case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
            printf("Fannout Expander Device\n");
            break;
        default:
            printf("Unknown device type %"PRIX8"h\n", csmiDevice->phyInfo.Identify.bDeviceType);
            break;
        }
        printf("\tInitiator Port Protocol: ");
        switch (csmiDevice->phyInfo.Identify.bInitiatorPortProtocol)
        {
        case CSMI_SAS_PROTOCOL_SATA:
            printf("SATA\n");
            break;
        case CSMI_SAS_PROTOCOL_SMP:
            printf("SMP\n");
            break;
        case CSMI_SAS_PROTOCOL_STP:
            printf("STP\n");
            break;
        case CSMI_SAS_PROTOCOL_SSP:
            printf("SSP\n");
            break;
        default:
            printf("Unknown protocol %"PRIX8"h\n", csmiDevice->phyInfo.Identify.bInitiatorPortProtocol);
            break;
        }
        printf("\tTarget Port Protocol: ");
        switch (csmiDevice->phyInfo.Identify.bTargetPortProtocol)
        {
        case CSMI_SAS_PROTOCOL_SATA:
            printf("SATA\n");
            break;
        case CSMI_SAS_PROTOCOL_SMP:
            printf("SMP\n");
            break;
        case CSMI_SAS_PROTOCOL_STP:
            printf("STP\n");
            break;
        case CSMI_SAS_PROTOCOL_SSP:
            printf("SSP\n");
            break;
        default:
            printf("Unknown protocol %"PRIX8"h\n", csmiDevice->phyInfo.Identify.bTargetPortProtocol);
            break;
        }
        printf("\tSAS Address: ");
        for (uint8_t iter = 0; iter < 8; ++iter)
        {
            printf("%02"PRIX8, csmiDevice->phyInfo.Identify.bSASAddress[iter]);
        }
        printf("\n");
        printf("\tPHY Identifier: ");
        if (csmiDevice->phyInfo.Identify.bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER)
        {
            printf("Use port Identifier\n");
        }
        else
        {
            printf("%"PRIX8"\n", csmiDevice->phyInfo.Identify.bPhyIdentifier);
        }
        printf("\tSignal Class: ");
        switch (csmiDevice->phyInfo.Identify.bSignalClass)
        {
        case CSMI_SAS_SIGNAL_CLASS_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_DIRECT:
            printf("Direct\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_SERVER:
            printf("Server\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_ENCLOSURE:
            printf("Enclosure\n");
            break;
        default:
            printf("Unkown signal class %"PRIX8"\n", csmiDevice->phyInfo.Identify.bSignalClass);
            break;
        }
        printf("Port Identifier: ");
        if (csmiDevice->phyInfo.bPortIdentifier == CSMI_SAS_IGNORE_PORT)
        {
            printf("Ignore Port\n");
        }
        else
        {
            printf("%"PRIX8"h\n", csmiDevice->phyInfo.bPortIdentifier);
        }
        printf("Negotiated Link Rate: ");
        switch (csmiDevice->phyInfo.bNegotiatedLinkRate)
        {
        case CSMI_SAS_LINK_RATE_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_SAS_PHY_DISABLED:
            printf("Phy disabled\n");
            break;
        case CSMI_SAS_LINK_RATE_FAILED:
            printf("Failed\n");
            break;
        case CSMI_SAS_SATA_SPINUP_HOLD:
            printf("SATA Spinup Hold\n");
            break;
        case CSMI_SAS_SATA_PORT_SELECTOR:
            printf("SATA Port Selector\n");
            break;
        case CSMI_SAS_LINK_RATE_1_5_GBPS:
            printf("1.5Gb/s\n");
            break;
        case CSMI_SAS_LINK_RATE_3_0_GBPS:
            printf("3.0Gb/s\n");
            break;
        case CSMI_SAS_LINK_RATE_6_0_GBPS:
            printf("6.0Gb/s\n");
            break;
        case CSMI_SAS_LINK_VIRTUAL:
            printf("Virtual\n");
            break;
        default:
            printf("Unknown rate %"PRIX8"h\n", csmiDevice->phyInfo.bNegotiatedLinkRate);
            break;
        }
        printf("Minimum Link Rate: ");
        switch (csmiDevice->phyInfo.bMinimumLinkRate)
        {
        case CSMI_SAS_LINK_RATE_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_SAS_PHY_DISABLED:
            printf("Phy disabled\n");
            break;
        case CSMI_SAS_LINK_RATE_FAILED:
            printf("Failed\n");
            break;
        case CSMI_SAS_SATA_SPINUP_HOLD:
            printf("SATA Spinup Hold\n");
            break;
        case CSMI_SAS_SATA_PORT_SELECTOR:
            printf("SATA Port Selector\n");
            break;
        case CSMI_SAS_LINK_RATE_1_5_GBPS:
            printf("1.5Gb/s\n");
            break;
        case CSMI_SAS_LINK_RATE_3_0_GBPS:
            printf("3.0Gb/s\n");
            break;
        case CSMI_SAS_LINK_RATE_6_0_GBPS:
            printf("6.0Gb/s\n");
            break;
        case CSMI_SAS_LINK_VIRTUAL:
            printf("Virtual\n");
            break;
        default:
            printf("Unknown rate %"PRIX8"h\n", csmiDevice->phyInfo.bMinimumLinkRate);
            break;
        }
        printf("Maximum Link Rate: ");
        switch (csmiDevice->phyInfo.bMaximumLinkRate)
        {
        case CSMI_SAS_LINK_RATE_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_SAS_PHY_DISABLED:
            printf("Phy disabled\n");
            break;
        case CSMI_SAS_LINK_RATE_FAILED:
            printf("Failed\n");
            break;
        case CSMI_SAS_SATA_SPINUP_HOLD:
            printf("SATA Spinup Hold\n");
            break;
        case CSMI_SAS_SATA_PORT_SELECTOR:
            printf("SATA Port Selector\n");
            break;
        case CSMI_SAS_LINK_RATE_1_5_GBPS:
            printf("1.5Gb/s\n");
            break;
        case CSMI_SAS_LINK_RATE_3_0_GBPS:
            printf("3.0Gb/s\n");
            break;
        case CSMI_SAS_LINK_RATE_6_0_GBPS:
            printf("6.0Gb/s\n");
            break;
        case CSMI_SAS_LINK_VIRTUAL:
            printf("Virtual\n");
            break;
        default:
            printf("Unknown rate %"PRIX8"h\n", csmiDevice->phyInfo.bMaximumLinkRate);
            break;
        }
        printf("PHY Change Count: %"PRIu8"\n", csmiDevice->phyInfo.bPhyChangeCount);
        //bAutoDiscover
        printf("Auto Discover: ");
        switch (csmiDevice->phyInfo.bAutoDiscover)
        {
        case CSMI_SAS_DISCOVER_NOT_SUPPORTED:
            printf("Not Supported\n");
            break;
        case CSMI_SAS_DISCOVER_NOT_STARTED:
            printf("Not Started\n");
            break;
        case CSMI_SAS_DISCOVER_IN_PROGRESS:
            printf("In Progress\n");
            break;
        case CSMI_SAS_DISCOVER_COMPLETE:
            printf("Complete\n");
            break;
        case CSMI_SAS_DISCOVER_ERROR:
            printf("Discover Error\n");
            break;
        default:
            printf("Unknown value %"PRIX8"h\n", csmiDevice->phyInfo.bAutoDiscover);
        }
        //bPhyFeatures
        printf("PHY Features: %"PRIX8"h\n", csmiDevice->phyInfo.bPhyFeatures);
        printf("Attached:\n");
        printf("\tDevice Type: ");
        switch (csmiDevice->phyInfo.Attached.bDeviceType)
        {
        case CSMI_SAS_NO_DEVICE_ATTACHED:
            printf("No device attached\n");
            break;
        case CSMI_SAS_END_DEVICE:
            printf("End Device\n");
            break;
        case CSMI_SAS_EDGE_EXPANDER_DEVICE:
            printf("Edge Expanded Device\n");
            break;
        case CSMI_SAS_FANOUT_EXPANDER_DEVICE:
            printf("Fannout Expander Device\n");
            break;
        default:
            printf("Unknown device type %"PRIX8"h\n", csmiDevice->phyInfo.Attached.bDeviceType);
            break;
        }
        printf("\tInitiator Port Protocol: ");
        switch (csmiDevice->phyInfo.Attached.bInitiatorPortProtocol)
        {
        case CSMI_SAS_PROTOCOL_SATA:
            printf("SATA\n");
            break;
        case CSMI_SAS_PROTOCOL_SMP:
            printf("SMP\n");
            break;
        case CSMI_SAS_PROTOCOL_STP:
            printf("STP\n");
            break;
        case CSMI_SAS_PROTOCOL_SSP:
            printf("SSP\n");
            break;
        default:
            printf("Unknown protocol %"PRIX8"h\n", csmiDevice->phyInfo.Attached.bInitiatorPortProtocol);
            break;
        }
        printf("\tTarget Port Protocol: ");
        switch (csmiDevice->phyInfo.Attached.bTargetPortProtocol)
        {
        case CSMI_SAS_PROTOCOL_SATA:
            printf("SATA\n");
            break;
        case CSMI_SAS_PROTOCOL_SMP:
            printf("SMP\n");
            break;
        case CSMI_SAS_PROTOCOL_STP:
            printf("STP\n");
            break;
        case CSMI_SAS_PROTOCOL_SSP:
            printf("SSP\n");
            break;
        default:
            printf("Unknown protocol %"PRIX8"h\n", csmiDevice->phyInfo.Attached.bTargetPortProtocol);
            break;
        }
        printf("\tSAS Address: ");
        for (uint8_t iter = 0; iter < 8; ++iter)
        {
            printf("%02"PRIX8, csmiDevice->phyInfo.Attached.bSASAddress[iter]);
        }
        printf("\n");
        printf("\tPHY Identifier: ");
        if (csmiDevice->phyInfo.Attached.bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER)
        {
            printf("Use port Identifier\n");
        }
        else
        {
            printf("%"PRIX8"\n", csmiDevice->phyInfo.Attached.bPhyIdentifier);
        }
        printf("\tSignal Class: ");
        switch (csmiDevice->phyInfo.Attached.bSignalClass)
        {
        case CSMI_SAS_SIGNAL_CLASS_UNKNOWN:
            printf("Unknown\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_DIRECT:
            printf("Direct\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_SERVER:
            printf("Server\n");
            break;
        case CSMI_SAS_SIGNAL_CLASS_ENCLOSURE:
            printf("Enclosure\n");
            break;
        default:
            printf("Unkown signal class %"PRIX8"\n", csmiDevice->phyInfo.Attached.bSignalClass);
            break;
        }
        if (csmiDevice->scsiAddressValid)
        {
            printf("\n===SCSI Address===\n");
            printf("Host Index:\t%"PRIu8"\n", csmiDevice->scsiAddress.hostIndex);
            printf("Path ID:\t%"PRIu8"\n", csmiDevice->scsiAddress.pathId);
            printf("Target ID:\t%"PRIu8"\n", csmiDevice->scsiAddress.targetId);
            printf("LUN:\t\t%"PRIu8"\n", csmiDevice->scsiAddress.lun);
        }
        if (csmiDevice->sataSignatureValid)
        {
            printf("\n===SATA Signature===\n");
            printf("Phy Identifier: %"PRIX8"h\n", csmiDevice->sataSignature.bPhyIdentifier);
            //Do a quick check on the FIS to see the device type:
            printf("SATA Device Type: ");
            pFIS_REG_D2H signatureFIS = (pFIS_REG_D2H)csmiDevice->sataSignature.bSignatureFIS;
            if (signatureFIS->lbaMid == 0x14 && signatureFIS->lbaHi == 0xEB)
            {
                //atapi device found
                printf("ATAPI\n");
            }
            else if (signatureFIS->lbaMid == 0 && signatureFIS->lbaHi == 0)
            {
                printf("ATA\n");
            }
            else if (signatureFIS->lbaMid == 0xAB && signatureFIS->lbaHi == 0xCD)
            {
                printf("Host Managed ATA\n");
            }
            else
            {
                printf("Unknown\n");
            }
            printf("Signature FIS:\n");
            print_FIS(csmiDevice->sataSignature.bSignatureFIS);
        }
    }
}

#endif
