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
#if !defined(DISABLE_NVME_PASSTHROUGH)

#include "platform_helper.h"
#include <stdio.h>
#include "nvme_helper.h"
#include "nvme_helper_func.h"
#include "common_public.h"


// \file nvme_cmds.c   Implementation for NVM Express helper functions
//                     The intention of the file is to be generic & not OS specific

// \fn fill_In_NVMe_Device_Info(device device)
// \brief Sends a set Identify etc commands & fills in the device information
// \param device device struture
// \return SUCCESS - pass, !SUCCESS fail or something went wrong
int fill_In_NVMe_Device_Info(tDevice *device)
{
    int ret = UNKNOWN;
    nvmeIDCtrl * ctrlData = &device->drive_info.IdentifyData.nvme.ctrl; //Conroller information data structure
    nvmeIDNameSpaces * nsData = &device->drive_info.IdentifyData.nvme.ns; //Name Space Data structure 
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif

    ret = nvme_Identify(device,(uint8_t *)ctrlData,0,NVME_IDENTIFY_CTRL);

#ifdef _DEBUG
printf("fill NVMe info ret = %d\n", ret);
#endif

    if (ret == SUCCESS)
    {
        memcpy(device->drive_info.serialNumber,ctrlData->sn,SERIAL_NUM_LEN);
        device->drive_info.serialNumber[20] = '\0';
        remove_Leading_And_Trailing_Whitespace(device->drive_info.serialNumber);
        memcpy(device->drive_info.product_revision, ctrlData->fr,8); //8 is the NVMe spec length of this
        device->drive_info.product_revision[8] = '\0';
        remove_Leading_And_Trailing_Whitespace(device->drive_info.product_revision);
        memcpy(device->drive_info.product_identification, ctrlData->mn,MODEL_NUM_LEN); 
        device->drive_info.product_identification[40] = '\0';
        remove_Leading_And_Trailing_Whitespace(device->drive_info.product_identification);
        device->drive_info.bridge_info.vendorID = ctrlData->vid;

        //set the IEEE OUI into the WWN since we use the WWN for detecting if the drive is a Seagate drive.
        //TODO: currently we set NAA to 5, but we should probably at least follow the SCSI-NVMe translation specification!
        device->drive_info.worldWideName = M_BytesTo8ByteValue(0x05, ctrlData->ieee[2], ctrlData->ieee[1], ctrlData->ieee[0], 0, 0, 0, 0) << 4;

        ret = nvme_Identify(device,(uint8_t *)nsData,device->drive_info.lunOrNSID,NVME_IDENTIFY_NS);

        if (ret == SUCCESS) 
        {

            device->drive_info.deviceBlockSize = 2 << (nsData->lbaf[nsData->flbas].lbaDS - 1);//removed math.h pow() function - TJE
            device->drive_info.devicePhyBlockSize = device->drive_info.deviceBlockSize; //True for NVMe?

            device->drive_info.deviceMaxLba = nsData->nsze; //* device->drive_info.deviceBlockSize;
            

            //TODO: Add support if more than one Namespace. 
            /*
            for (ns=2; ns <= ctrlData.nn; ns++) 
            {
                

            }*/

        }
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif

    return ret;
}

void nvme_Print_Feature_Identifiers_Help ( )
{
    printf("\n====================================================\n");
    printf(" Feature\t O/M \tPersistent\tDescription\n");
    printf("Identifier\t   \tAcross Power\t      \n");
    printf("          \t   \t  & Reset   \t      \n");
    printf("====================================================\n");
    printf("00h       \t   \t            \tReserved\n");
    printf("01h       \t M \t   NO       \tArbitration\n");
    printf("02h       \t M \t   NO       \tPower Management\n");
    printf("03h       \t O \t   YES      \tLBA Range Type\n");
    printf("04h       \t M \t   NO       \tTemprature Threshold\n");
    printf("05h       \t M \t   NO       \tError Recovery\n");
    printf("06h       \t O \t   NO       \tVolatile Write Cache\n");
    printf("07h       \t M \t   NO       \tNumber of Queues\n");
    printf("08h       \t M \t   NO       \tInterrupt Coalescing\n");
    printf("09h       \t M \t   NO       \tInterrupt Vector Configuration\n");
    printf("0Ah       \t M \t   NO       \tWrite Atomicity Normal\n");
    printf("0Bh       \t M \t   NO       \tAsynchronous Event Configuration\n");
    printf("0Ch       \t O \t   NO       \tAutonomous Power State Transition\n");
    printf("0Dh       \t O \t   NO       \tHost Memory Buffer\n");
    printf("0Eh-77h   \t   \t            \tReserved          \n");
    printf("78h-7Fh   \t   \t            \tRefer to NVMe Management Spec\n");
    printf("80h-BFh   \t   \t            \tCommand Set Specific (Reserved)\n");
    printf("C0h-FFh   \t   \t            \tVendor Specific\n");
    printf("====================================================\n");
}

int nvme_Print_All_Feature_Identifiers (tDevice *device, eNvmeFeaturesSelectValue selectType, bool listOnlySupportedFeatures)
{
    int ret = UNKNOWN;
    uint8_t featureID;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    printf(" Feature ID\tRaw Value\n");
    printf("===============================\n");
    for (featureID = 1; featureID < 0x0C; featureID++)
    {
        if (featureID == NVME_FEAT_LBA_RANGE_)
        {
            continue;
        }
        memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
        featureCmd.fid = featureID;
        featureCmd.sel = selectType;
        if(nvme_Get_Features(device, &featureCmd) == SUCCESS)
        {
            printf("    %02Xh    \t0x%08X\n",featureID, featureCmd.featSetGetValue);
        }
    }
    printf("===============================\n");

#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Print_Arbitration_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_ARBITRATION_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tArbitration & Command Processing Feature\n");
        printf("=============================================\n");
        printf("Hi  Priority Weight (HPW) :\t\t0x%02X\n",( (featureCmd.featSetGetValue & 0xFF000000)>>24) );
        printf("Med Priority Weight (MPW) :\t\t0x%02X\n",( (featureCmd.featSetGetValue & 0x00FF0000)>>16) );
        printf("Low Priority Weight (LPW) :\t\t0x%02X\n",( (featureCmd.featSetGetValue & 0x0000FF00)>>8 )  );
        printf("Arbitration Burst    (AB) :\t\t0x%02X\n",featureCmd.featSetGetValue & 0x00000003);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}


//Temperature Threshold 
int nvme_Print_Temperature_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
    uint8_t   TMPSEL = 0; //0=Composite, 1=Sensor 1, 2=Sensor 2, ...
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_TEMP_THRESH_;
    featureCmd.sel = selectType;
    printf("\n\tTemperature Threshold Feature\n");
    printf("=============================================\n");
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("Composite Temperature : \t0x%04X\tOver  Temp. Threshold\n",\
                                (featureCmd.featSetGetValue & 0x000000FF));
    }
    featureCmd.featSetGetValue = BIT20;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("Composite Temperature : \t0x%04X\tUnder Temp. Threshold\n",\
                                (featureCmd.featSetGetValue & 0x000000FF));
    }
    
    for (TMPSEL=1; TMPSEL <= 8; TMPSEL++)
    {
        featureCmd.featSetGetValue = (TMPSEL << 16);
        ret = nvme_Get_Features(device, &featureCmd);
        if(ret == SUCCESS)
        {
            printf("Temperature Sensor %d  : \t0x%04X\tOver  Temp. Threshold\n",\
                   TMPSEL, (featureCmd.featSetGetValue & 0x000000FF));
        }
        //Not get Under Temprature 
        // BIT20 = THSEL 0=Over Temperature Thresh. 1=Under Temperature Thresh. 
        featureCmd.featSetGetValue = (BIT20 | (uint32_t)((uint32_t)TMPSEL << 16));
        ret = nvme_Get_Features(device, &featureCmd);
        if(ret == SUCCESS)
        {
            printf("Temperature Sensor %d  : \t0x%04X\tUnder Temp. Threshold\n",\
                   TMPSEL, (featureCmd.featSetGetValue & 0x000000FF));
        }    
    }
    //WARNING: This is just sending back the last sensor ret 
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Power Management
int nvme_Print_PM_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_POWER_MGMT_FEAT;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tPower Management Feature Details\n");
        printf("=============================================\n");
        printf("Workload Hint  (WH) :\t\t0x%02X\n",( (featureCmd.featSetGetValue & 0x000000E0)>>5 )  );
        printf("Power State    (PS) :\t\t0x%02X\n",featureCmd.featSetGetValue & 0x0000001F);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Error Recovery
int nvme_Print_Error_Recovery_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_ERR_RECOVERY_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tError Recovery Feature Details\n");
        printf("=============================================\n");
        printf("Deallocated Logical Block Error (DULBE) :\t\t%s\n", (featureCmd.featSetGetValue & BIT16) ? "Enabled" : "Disabled" );
        printf("Time Limited Error Recovery     (TLER)  :\t\t0x%04X\n",featureCmd.featSetGetValue & 0x0000FFFF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Volatile Write Cache Feature. 
int nvme_Print_WCE_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_VOLATILE_WC_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tVolatile Write Cache Feature Details\n");
        printf("=============================================\n");
        printf("Volatile Write Cache (WCE) :\t\t%s\n", (featureCmd.featSetGetValue & BIT0) ? "Enabled" : "Disabled" );
        
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Number of Queues Feature 
int nvme_Print_NumberOfQueues_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_NUM_QUEUES_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tNumber of Queues Feature Details\n");
        printf("=============================================\n");
        printf("# of I/O Completion Queues Requested (NCQR)  :\t\t0x%04X\n",(featureCmd.featSetGetValue & 0xFFFF0000) >> 16 );
        printf("# of I/O Submission Queues Requested (NSQR)  :\t\t0x%04X\n",featureCmd.featSetGetValue & 0x0000FFFF);

        //TODO: How to get NCQA??? Seems like Linux driver limitation? -X
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Interrupt Coalescing (08h Feature)
int nvme_Print_Intr_Coalescing_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_IRQ_COALESCE_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tInterrupt Coalescing Feature Details\n");
        printf("=============================================\n");
        printf("Aggregation Time     (TIME)  :\t\t0x%02X\n",(featureCmd.featSetGetValue & 0x0000FF00) >> 8 );
        printf("Aggregation Threshold (THR)  :\t\t0x%02X\n",featureCmd.featSetGetValue & 0x000000FF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Interrupt Vector Configuration (09h Feature)
int nvme_Print_Intr_Config_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_IRQ_CONFIG_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tInterrupt Vector Configuration Feature Details\n");
        printf("=============================================\n");
        printf("Coalescing Disable (CD) :\t%s\n", (featureCmd.featSetGetValue & BIT16) ? "Enabled" : "Disabled" );
        printf("Interrupt Vector   (IV) :\t0x%02X\n",featureCmd.featSetGetValue & 0x000000FF);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Write Atomicity Normal (0Ah Feature)
int nvme_Print_Write_Atomicity_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_WRITE_ATOMIC_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tWrite Atomicity Normal Feature Details\n");
        printf("=============================================\n");
        printf("Disable Normal (DN) :\t%s\n\n", (featureCmd.featSetGetValue & BIT0) ? "Enabled" : "Disabled" );
        if (featureCmd.featSetGetValue & BIT0)
        {
            printf(" Host specifies that AWUN & NAWUN are not required\n");
            printf(" & controller shall only honor AWUPF & NAWUPF\n");
        }
        else
        {
            printf("Controller honors AWUN, NAWUN, AWUPF & NAWUPF\n");
        }
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

//Asynchronous Event Configuration (0Bh Feature)
int nvme_Print_Async_Config_Feature_Details(tDevice *device, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
    nvmeFeaturesCmdOpt featureCmd;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&featureCmd, 0, sizeof(nvmeFeaturesCmdOpt));
    featureCmd.fid = NVME_FEAT_ASYNC_EVENT_;
    featureCmd.sel = selectType;
    ret = nvme_Get_Features(device, &featureCmd);
    if(ret == SUCCESS)
    {
        printf("\n\tAsync Event Configuration\n");
        printf("=============================================\n");
        printf("Firmware Activation Notices     :\t%s\n", (featureCmd.featSetGetValue & BIT9) ? "Enabled" : "Disabled" );
        printf("Namespace Attribute Notices     :\t%s\n", (featureCmd.featSetGetValue & BIT8) ? "Enabled" : "Disabled" );
        printf("SMART/Health Critical Warnings  :\t0x%02X\n", featureCmd.featSetGetValue & 0x000000FF );
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Print_Feature_Details(tDevice *device, uint8_t featureID, eNvmeFeaturesSelectValue selectType)
{
    int ret = UNKNOWN;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    switch (featureID)
    {
    case NVME_FEAT_ARBITRATION_:
        ret = nvme_Print_Arbitration_Feature_Details(device, selectType);
        break;
    case NVME_POWER_MGMT_FEAT:
        ret = nvme_Print_PM_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_TEMP_THRESH_:
        ret = nvme_Print_Temperature_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_ERR_RECOVERY_:
        ret = nvme_Print_Error_Recovery_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_VOLATILE_WC_:
        ret = nvme_Print_WCE_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_NUM_QUEUES_:
        ret = nvme_Print_NumberOfQueues_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_IRQ_COALESCE_:
        ret = nvme_Print_Intr_Coalescing_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_IRQ_CONFIG_:
        ret = nvme_Print_Intr_Config_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_WRITE_ATOMIC_:
        ret = nvme_Print_Write_Atomicity_Feature_Details(device, selectType);
        break;
    case NVME_FEAT_ASYNC_EVENT_:
        ret = nvme_Print_Async_Config_Feature_Details(device, selectType);
        break;
    default:
        ret = NOT_SUPPORTED;
        break;
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Get_Log_Size(uint8_t logPageId, uint64_t * logSize)
{
    switch (logPageId)
    {
    case NVME_LOG_ERROR_ID:
        *logSize = sizeof(nvmeErrLogEntry);
        break;
    case NVME_LOG_SMART_ID:
    case NVME_LOG_FW_SLOT_ID: //Same size as Health
        *logSize = NVME_SMART_HEALTH_LOG_LEN;
        break;
    default:
        *logSize = 0;
        break;
    }
    return SUCCESS; // Can be used later to tell if the log is unavailable or we can't get size. 
}

int nvme_Get_SMART_Log_Page(tDevice *device, uint32_t nsid, uint8_t * pData, uint32_t dataLen)
{
    int ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
    nvmeSmartLog * smartLog; // in case we need to align memory
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    if ( (pData == NULL) || (dataLen < NVME_SMART_HEALTH_LOG_LEN) )
    {
        return ret;
    }

    memset(&cmdOpts,0,sizeof(nvmeGetLogPageCmdOpts));
    smartLog = (nvmeSmartLog *)pData;

    cmdOpts.nsid = nsid;
    cmdOpts.addr = (uint64_t)smartLog;
    cmdOpts.dataLen = NVME_SMART_HEALTH_LOG_LEN;
    cmdOpts.lid = NVME_LOG_SMART_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Get_ERROR_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
{
    int ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    //Should be able to pull at least one entry. 
    if ( (pData == NULL) || (dataLen < sizeof(nvmeErrLogEntry)) )
    {
        return ret;
    }
   
    memset(&cmdOpts,0,sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr = (uint64_t)pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid = NVME_LOG_ERROR_ID;

    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Get_FWSLOTS_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
{
    int ret = UNKNOWN;
    nvmeGetLogPageCmdOpts cmdOpts;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    //Should be able to pull at least one entry. 
    if ( (pData == NULL) || (dataLen < sizeof(nvmeFirmwareSlotInfo)) )
    {
        return ret;
    }
   
    memset(&cmdOpts,0,sizeof(nvmeGetLogPageCmdOpts));
    cmdOpts.addr = (uint64_t)pData;
    cmdOpts.dataLen = dataLen;
    cmdOpts.lid = NVME_LOG_FW_SLOT_ID;
    
    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Print_FWSLOTS_Log_Page(tDevice *device)
{
    int ret = UNKNOWN;
    int slot = 0;
    nvmeFirmwareSlotInfo fwSlotsLogInfo;
    char fwRev[9]; // 8 bytes for the FSR + 1 byte '\0'
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    memset(&fwSlotsLogInfo,0,sizeof(nvmeFirmwareSlotInfo));
    ret = nvme_Get_FWSLOTS_Log_Page(device, (uint8_t*)&fwSlotsLogInfo, sizeof(nvmeFirmwareSlotInfo) );
    if (ret == SUCCESS)
    {
#ifdef _DEBUG
        printf("AFI: 0x%X\n",fwSlotsLogInfo.afi);
#endif
        printf("\nFirmware slot actively running firmware: %d\n",fwSlotsLogInfo.afi & 0x03);
        printf("Firmware slot to be activated at next reset: %d\n\n",((fwSlotsLogInfo.afi & 0x30) >> 4));

        for (slot=1; slot <= NVME_MAX_FW_SLOTS; slot++ )
        {
            memcpy(fwRev,(char *)&fwSlotsLogInfo.FSR[slot-1],8);
            fwRev[8] = '\0';
            printf(" Slot %d : %s\n", slot,fwRev);
        }
    }

#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Print_ERROR_Log_Page(tDevice *device, uint64_t numOfErrToPrint)
{
    int ret = UNKNOWN;
    int err = 0;
    nvmeErrLogEntry * pErrLogBuf = NULL;
#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    //TODO: If this is not specified get the value. 
    if (!numOfErrToPrint)
    {
        numOfErrToPrint = 32;
    }
    pErrLogBuf = (nvmeErrLogEntry *) calloc((size_t)numOfErrToPrint, sizeof(nvmeErrLogEntry));
    if (pErrLogBuf != NULL)
    {
        ret = nvme_Get_ERROR_Log_Page(device, (uint8_t*)pErrLogBuf, (uint32_t)(numOfErrToPrint * sizeof(nvmeErrLogEntry)) );
        if (ret == SUCCESS)
        {
            printf("Err #\tLBA\t\tSQ ID\tCMD ID\tStatus\tLocation\n");
            printf("=======================================================\n");
            for (err = 0; err < (int)numOfErrToPrint; err++)
            {
                if (pErrLogBuf[err].errorCount)
                {

                    printf("%"PRIu64"\t%"PRIu64"\t%"PRIu16"\t%"PRIu16"\t0x%02"PRIX16"\t0x%02"PRIX16"\n", \
                          pErrLogBuf[err].errorCount,
                          pErrLogBuf[err].lba,
                          pErrLogBuf[err].subQueueID,
                          pErrLogBuf[err].cmdID,
                          pErrLogBuf[err].statusField,
                          pErrLogBuf[err].paramErrLocation);
                }
            }
        }
    }
    safe_Free(pErrLogBuf);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int print_Nvme_Ctrl_Regs(tDevice * device)
{
    int ret = UNKNOWN; 

    nvmeBarCtrlRegisters ctrlRegs;

    memset(&ctrlRegs,0,sizeof(nvmeBarCtrlRegisters));

    printf("\n=====CONTROLLER REGISTERS=====\n");

    ret = nvme_Read_Ctrl_Reg(device, &ctrlRegs);

    if (ret == SUCCESS) 
    {
        printf("Controller Capabilities (CAP)\t:\t%"PRIx64"\n", ctrlRegs.cap);
        printf("Version (VS)\t:\t0x%x\n",ctrlRegs.vs);
        printf("Interrupt Mask Set (INTMS)\t:\t0x%x\n",ctrlRegs.intms);
        printf("Interrupt Mask Clear (INTMC)\t:\t0x%x\n",ctrlRegs.intmc);
        printf("Controller Configuration (CC)\t:\t0x%x\n",ctrlRegs.cc);
        printf("Controller Status (CSTS)\t:\t0x%x\n",ctrlRegs.csts);
        //What about NSSR?
        printf("Admin Queue Attributes (AQA)\t:\t0x%x\n",ctrlRegs.aqa);
        printf("Admin Submission Queue Base Address (ASQ)\t:\t%"PRIx64"\n",ctrlRegs.asq);
        printf("Admin Completion Queue Base Address (ACQ)\t:\t%"PRIx64"\n",ctrlRegs.acq);
    }
    else
    {
        printf("Couldn't read Controller register for dev %s\n",device->os_info.name);
    }
    return ret;
}

int run_NVMe_Format(tDevice * device, uint32_t newLBASize, uint64_t flags)
{
    int ret = SUCCESS; 
    uint8_t c=0; 
    uint8_t sizeSupported = 0;
    nvmeFormatCmdOpts formatCmdOptions; 
    nvmeIDCtrl * ctrlData = &device->drive_info.IdentifyData.nvme.ctrl; //Controller information data structure
    nvmeIDNameSpaces * nsData = &device->drive_info.IdentifyData.nvme.ns; //Name Space Data structure 

#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif
    //Check if the newLBASize is supported by the device. 
    for (c=0; c <= nsData->nlbaf; c++) 
    {
        if ( (2 << (nsData->lbaf[c].lbaDS -1))  == newLBASize)
        {
            //printf("Formatting LBA Size %d Supported\n",newLBASize);
            sizeSupported = 1;
            break;
        }
    }
    if (!sizeSupported)
    {
        if (g_verbosity > VERBOSITY_QUIET)
        {
            printf("ERROR: UnSupported LBA Size %d\n",newLBASize);
        }
        ret = NOT_SUPPORTED;
    }
    
    if ( (flags & FORMAT_NVME_CRYPTO_ERASE) && (!(ctrlData->fna & BIT2)) )
    {
        if (g_verbosity > VERBOSITY_QUIET)
        {
            printf("ERROR: Crypto Erase not supported %d\n",newLBASize);
        }
        ret = NOT_SUPPORTED;        
    }
    
    //If everything checks out perform the Format. 
    if (ret == SUCCESS)
    {
        memset(&formatCmdOptions, 0, sizeof(nvmeFormatCmdOpts));
        formatCmdOptions.lbaf = c;
        formatCmdOptions.nsid = device->drive_info.lunOrNSID ;

        if (flags & FORMAT_NVME_CRYPTO_ERASE)
        {
            formatCmdOptions.ses = FORMAT_NVME_CRYPTO_ERASE;
        }
        else if (flags & FORMAT_NVME_ERASE_USER_DATA)
        {
            formatCmdOptions.ses = FORMAT_NVME_ERASE_USER_DATA;
        }
        ret = nvme_Format(device, &formatCmdOptions);
    }
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

#endif
