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
        //set the t10 vendor id to NVMe
        sprintf(device->drive_info.T10_vendor_ident, "NVMe");

        //Set the other device fields we need.
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

            device->drive_info.deviceBlockSize = power_Of_Two(nsData->lbaf[nsData->flbas].lbaDS); //removed math.h pow() function - TJE
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

void print_NVMe_Cmd_Verbose(const nvmeCmdCtx * cmdCtx)
{
    if (VERBOSITY_COMMAND_VERBOSE <= g_verbosity)
    {
        printf("Sending NVM Command:\n");
        printf("\tType: ");
        switch (cmdCtx->commandType)
        {
        case NVM_ADMIN_CMD:
            printf("Admin");
            break;
        case NVM_CMD:
            printf("NVM");
            break;
        case NVM_UNKNOWN_CMD_SET:
        default:
            printf("Unknown");
            break;
        }
        printf("\n");
        printf("\tData Direction: ");
        //Data Direction:
        switch (cmdCtx->commandDirection)
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
        //printf("Cmd result 0x%02X\n", cmdCtx->result);
        printf("Command Bytes:\n");
        switch (cmdCtx->commandType)
        {
        case NVM_ADMIN_CMD:
            printf("\tOpcode (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.adminCmd.opcode);
            printf("\tFlags (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.adminCmd.flags);
            printf("\tReserved (CDW0) = %" PRIu16 "\n", cmdCtx->cmd.adminCmd.rsvd1);
            printf("\tNSID = %" PRIu32 "\n", cmdCtx->cmd.adminCmd.nsid);
            printf("\tCDW2 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw2);
            printf("\tCDW3 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw3);
            printf("\tMetadata = %" PRIu64 "\n", cmdCtx->cmd.adminCmd.metadata);
            printf("\tMetadata Length = %" PRIu32 "\n", cmdCtx->cmd.adminCmd.metadataLen);
            printf("\tAddress = %" PRIu64 "\n", cmdCtx->cmd.adminCmd.addr);
            printf("\tData Length = %" PRIu32 "\n", cmdCtx->cmd.adminCmd.dataLen);
            printf("\tCDW10 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw10);
            printf("\tCDW11 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw11);
            printf("\tCDW12 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw12);
            printf("\tCDW13 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw13);
            printf("\tCDW14 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw14);
            printf("\tCDW15 = %08" PRIX32 "h\n", cmdCtx->cmd.adminCmd.cdw15);
            break;
        case NVM_CMD:
            printf("\tOpcode (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.nvmCmd.opcode);
            printf("\tFlags (CDW0) = %" PRIu8 "\n", cmdCtx->cmd.nvmCmd.flags);
            printf("\tCommand ID (CDW0) = %" PRIu16 "\n", cmdCtx->cmd.nvmCmd.commandId);
            printf("\tNSID = %" PRIu32 "\n", cmdCtx->cmd.nvmCmd.nsid);
            printf("\tCDW2 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw2);
            printf("\tCDW3 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw3);
            printf("\tMetadata (CDW4 & 5) = %" PRIu64 "\n", cmdCtx->cmd.nvmCmd.metadata);
            printf("\tPRP1 (CDW6 & 7) = %" PRIu64 "\n", cmdCtx->cmd.nvmCmd.prp1);
            printf("\tPRP2 (CDW8 & 9) = %" PRIu64 "\n", cmdCtx->cmd.nvmCmd.prp2);
            printf("\tCDW10 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw10);
            printf("\tCDW11 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw11);
            printf("\tCDW12 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw12);
            printf("\tCDW13 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw13);
            printf("\tCDW14 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw14);
            printf("\tCDW15 = %08" PRIX32 "h\n", cmdCtx->cmd.nvmCmd.cdw15);
            break;
        default:
            printf("\tCDW0  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw0);
            printf("\tCDW1  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw1);
            printf("\tCDW2  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw2);
            printf("\tCDW3  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw3);
            printf("\tCDW4  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw4);
            printf("\tCDW5  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw5);
            printf("\tCDW6  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw6);
            printf("\tCDW7  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw7);
            printf("\tCDW8  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw8);
            printf("\tCDW9  = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw9);
            printf("\tCDW10 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw10);
            printf("\tCDW11 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw11);
            printf("\tCDW12 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw12);
            printf("\tCDW13 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw13);
            printf("\tCDW14 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw14);
            printf("\tCDW15 = %08" PRIX32 "h\n", cmdCtx->cmd.dwords.cdw15);
            break;
        }
    }
}

void print_NVMe_Cmd_Result_Verbose(const nvmeCmdCtx * cmdCtx)
{
    //TODO: Print out the result/error information!
}


char *nvme_cmd_to_string(int admin, uint8_t opcode)
{
	if (admin) {
		switch (opcode) {
		case NVME_ADMIN_CMD_DELETE_SQ:	return "Delete I/O Submission Queue";
		case NVME_ADMIN_CMD_CREATE_SQ:	return "Create I/O Submission Queue";
		case NVME_ADMIN_CMD_GET_LOG_PAGE:	return "Get Log Page";
		case NVME_ADMIN_CMD_DELETE_CQ:	return "Delete I/O Completion Queue";
		case NVME_ADMIN_CMD_CREATE_CQ:	return "Create I/O Completion Queue";
		case NVME_ADMIN_CMD_IDENTIFY:	return "Identify";
		case NVME_ADMIN_CMD_ABORT_CMD:	return "Abort";
		case NVME_ADMIN_CMD_SET_FEATURES:	return "Set Features";
		case NVME_ADMIN_CMD_GET_FEATURES:	return "Get Features";
		case NVME_ADMIN_CMD_ASYNC_EVENT:	return "Asynchronous Event Request";
		case NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT:	return "Namespace Management";
		case NVME_ADMIN_CMD_ACTIVATE_FW:	return "Firmware Commit";
		case NVME_ADMIN_CMD_DOWNLOAD_FW:	return "Firmware Image Download";
		case NVME_ADMIN_CMD_DEVICE_SELF_TEST:	return "Device Self-test";
		case NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT:	return "Namespace Attachment";
		case NVME_ADMIN_CMD_KEEP_ALIVE:	return "Keep Alive";
		case NVME_ADMIN_CMD_DIRECTIVE_SEND:	return "Directive Send";
		case NVME_ADMIN_CMD_DIRECTIVE_RECEIVE:	return "Directive Receive";
		case NVME_ADMIN_CMD_VIRTUALIZATION_MANAGEMENT:	return "Virtualization Management";
		case NVME_ADMIN_CMD_NVME_MI_SEND:	return "NVMEe-MI Send";
		case NVME_ADMIN_CMD_NVME_MI_RECEIVE:	return "NVMEe-MI Receive";
        case NVME_ADMIN_CMD_DOORBELL_BUFFER_CONFIG:		return "Doorbell Buffer Config";
        case NVME_ADMIN_CMD_NVME_OVER_FABRICS:      return "NVMe Over Fabric";
		case NVME_ADMIN_CMD_FORMAT_NVM:	return "Format NVM";
		case NVME_ADMIN_CMD_SECURITY_SEND:	return "Security Send";
		case NVME_ADMIN_CMD_SECURITY_RECV:	return "Security Receive";
		case NVME_ADMIN_CMD_SANITIZE:	return "Sanitize";
		}
	} else {
		switch (opcode) {
		case NVME_CMD_FLUSH:		return "Flush";
		case NVME_CMD_WRITE:		return "Write";
		case NVME_CMD_READ:		return "Read";
		case NVME_CMD_WRITE_UNCOR:	return "Write Uncorrectable";
		case NVME_CMD_COMPARE:		return "Compare";
		case NVME_CMD_WRITE_ZEROS:	return "Write Zeroes";
		case NVME_CMD_DATA_SET_MANAGEMENT:		return "Dataset Management";
		case NVME_CMD_RESERVATION_REGISTER:	return "Reservation Register";
		case NVME_CMD_RESERVATION_REPORT:	return "Reservation Report";
		case NVME_CMD_RESERVATION_ACQUIRE:	return "Reservation Acquire";
		case NVME_CMD_RESERVATION_RELEASE:	return "Reservation Release";
		}
	}

	return "Unknown";
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
    case NVME_LOG_CMD_SPT_EFET_ID:
        *logSize = sizeof(nvmeEffectsLog);
        break;
    case NVME_LOG_DEV_SELF_TEST:
        *logSize = sizeof(nvmeSelfTestLog);
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
    //cmdOpts.addr = (uint64_t)smartLog;
    cmdOpts.addr = smartLog;
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

int nvme_Get_CmdSptEfft_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
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
    cmdOpts.lid = NVME_LOG_CMD_SPT_EFET_ID;
    
    ret = nvme_Get_Log_Page(device, &cmdOpts);
#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}

int nvme_Get_DevSelfTest_Log_Page(tDevice *device, uint8_t * pData, uint32_t dataLen)
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
    cmdOpts.lid = NVME_LOG_DEV_SELF_TEST;
    
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

void show_effects_log_human(uint32_t effect)
{
	const char *set = "+";
	const char *clr = "-";

	printf("  CSUPP+");
	printf("  LBCC%s", (effect & NVME_CMD_EFFECTS_LBCC) ? set : clr);
	printf("  NCC%s", (effect & NVME_CMD_EFFECTS_NCC) ? set : clr);
	printf("  NIC%s", (effect & NVME_CMD_EFFECTS_NIC) ? set : clr);
	printf("  CCC%s", (effect & NVME_CMD_EFFECTS_CCC) ? set : clr);

	if ((effect & NVME_CMD_EFFECTS_CSE_MASK) >> 16 == 0)
		printf("  No command restriction\n");
	else if ((effect & NVME_CMD_EFFECTS_CSE_MASK) >> 16 == 1)
		printf("  No other command for same namespace\n");
	else if ((effect & NVME_CMD_EFFECTS_CSE_MASK) >> 16 == 2)
		printf("  No other command for any namespace\n");
	else
		printf("  Reserved CSE\n");
}

int nvme_Print_CmdSptEfft_Log_Page(tDevice *device)
{
    int ret = UNKNOWN;
    nvmeEffectsLog effectsLogInfo;
    int i;
    int effect;

#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif

    memset(&effectsLogInfo,0,sizeof(nvmeEffectsLog));
    ret = nvme_Get_CmdSptEfft_Log_Page(device, (uint8_t*)&effectsLogInfo, sizeof(nvmeEffectsLog) );
    if (ret == SUCCESS)
    {
    	printf("Admin Command Set\n");
    	for (i = 0; i < 256; i++) 
        {
    		effect = effectsLogInfo.acs[i];
    		if (effect & NVME_CMD_EFFECTS_CSUPP) 
            {
    			printf("ACS%-6d[%-32s] %08x", i, nvme_cmd_to_string(1, i), effect);
    			show_effects_log_human(effect);
    		}
    	}
    	printf("\nNVM Command Set\n");
    	for (i = 0; i < 256; i++) 
        {
    		effect = effectsLogInfo.iocs[i];
    		if (effect & NVME_CMD_EFFECTS_CSUPP) 
            {
    			printf("IOCS%-5d[%-32s] %08x", i, nvme_cmd_to_string(0, i), effect);
    			show_effects_log_human(effect);
    		}
    	}
    }

#ifdef _DEBUG
    printf("<--%s (%d)\n",__FUNCTION__, ret);
#endif
    return ret;
}


int nvme_Print_DevSelfTest_Log_Page(tDevice *device)
{
    int ret = UNKNOWN;
    nvmeSelfTestLog selfTestLogInfo;
    int effect;
	int i, temp;
	const char *test_code_res;
	const char *test_res[10] = {
		"Operation completed without error",
		"Operation was aborted by a Device Self-test command",
		"Operation was aborted by a Controller Level Reset",
		"Operation was aborted due to a removal of a namespace from the namespace inventory",
		"Operation was aborted due to the processing of a Format NVM command",
		"A fatal error or unknown test error occurred while the controller was executing the"\
		" device self-test operation andthe operation did not complete",
		"Operation completed with a segment that failed and the segment that failed is not known",
		"Operation completed with one or more failed segments and the first segment that failed "\
		"is indicated in the SegmentNumber field",
		"Operation was aborted for unknown reason",
		"Reserved"
	};


#ifdef _DEBUG
    printf("-->%s\n",__FUNCTION__);
#endif

    memset(&selfTestLogInfo,0,sizeof(nvmeSelfTestLog));
    ret = nvme_Get_DevSelfTest_Log_Page(device, (uint8_t*)&selfTestLogInfo, sizeof(nvmeSelfTestLog) );
    if (ret == SUCCESS)
    {
    	printf("Current operation : %#x\n", selfTestLogInfo.crntDevSelftestOprn);
    	printf("Current Completion : %u%%\n", selfTestLogInfo.crntDevSelftestCompln);
    	for (i = 0; i < NVME_SELF_TEST_REPORTS; i++) 
        {
    		temp = selfTestLogInfo.result[i].deviceSelfTestStatus & 0xf;
    		if (temp == 0xf)
    			continue;
    
    		printf("Result[%d]:\n", i);
    		printf("  Test Result                  : %#x %s\n", temp,
    			test_res[temp > 9 ? 9 : temp]);
    
    		temp = selfTestLogInfo.result[i].deviceSelfTestStatus >> 4;
    		switch (temp) {
    		case 1:
    			test_code_res = "Short device self-test operation";
    			break;
    		case 2:
    			test_code_res = "Extended device self-test operation";
    			break;
    		case 0xe:
    			test_code_res = "Vendor specific";
    			break;
    		default :
    			test_code_res = "Reserved";
    			break;
    		}
    		printf("  Test Code                    : %#x %s\n", temp,
    			test_code_res);
    		if (temp == 7)
    			printf("  Segment number               : %#x\n",
    				selfTestLogInfo.result[i].segmentNum);
    
    		temp = selfTestLogInfo.result[i].validDiagnosticInfo;
    		printf("  Valid Diagnostic Information : %#x\n", temp);
    		printf("  Power on hours (POH)         : %#"PRIx64"\n", selfTestLogInfo.result[i].powerOnHours);
    
    		if (temp & NVME_SELF_TEST_VALID_NSID)
    			printf("  Namespace Identifier         : %#x\n", selfTestLogInfo.result[i].nsid);

    		if (temp & NVME_SELF_TEST_VALID_FLBA)
    			printf("  Failing LBA                  : %#"PRIx64"\n", selfTestLogInfo.result[i].failingLba);

            if (temp & NVME_SELF_TEST_VALID_SCT)
    			printf("  Status Code Type             : %#x\n", selfTestLogInfo.result[i].statusCodeType);

    		if (temp & NVME_SELF_TEST_VALID_SC)
    			printf("  Status Code                  : %#x\n", selfTestLogInfo.result[i].statusCode);

    		printf("  Vendor Specific                      : %x %x\n", selfTestLogInfo.result[i].vendorSpecific[0], selfTestLogInfo.result[i].vendorSpecific[1]);
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

#if !defined(FORMAT_NVME_NO_SECURE_ERASE)
#define FORMAT_NVME_NO_SECURE_ERASE (0)
#endif
#if !defined(FORMAT_NVME_ERASE_USER_DATA)
#define FORMAT_NVME_ERASE_USER_DATA (1)
#endif
#if !defined(FORMAT_NVME_CRYPTO_ERASE)
#define FORMAT_NVME_CRYPTO_ERASE    (2)
#endif
#if !defined(FORMAT_NVME_PI_FIRST_BYTES)
#define FORMAT_NVME_PI_FIRST_BYTES  (4)
#endif
#if !defined(FORMAT_NVME_PI_TYPE_I)
#define FORMAT_NVME_PI_TYPE_I       (8)
#endif
#if !defined(FORMAT_NVME_PI_TYPE_II)
#define FORMAT_NVME_PI_TYPE_II      (16)
#endif
#if !defined(FORMAT_NVME_PI_TYPE_III)
#define FORMAT_NVME_PI_TYPE_III     (32)
#endif
#if !defined(FORMAT_NVME_XFER_METADATA)
#define FORMAT_NVME_XFER_METADATA   (64)
#endif

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


/***************************************
* Extended-SMART Information
***************************************/
char* print_ext_smart_id(uint8_t attrId)
{
	switch(attrId) {
		case VS_ATTR_ID_SOFT_READ_ERROR_RATE:
		return "Soft ECC error count";
			break;
		case VS_ATTR_ID_REALLOCATED_SECTOR_COUNT:
		return "Bad NAND block count";
			break;
		case VS_ATTR_ID_POWER_ON_HOURS:
		return "Power On Hours";
			break;
		case VS_ATTR_ID_POWER_FAIL_EVENT_COUNT:
		return "Power Fail Event Count";
			break;
		case VS_ATTR_ID_DEVICE_POWER_CYCLE_COUNT:
		return "Device Power Cycle Count";
			break;
		case VS_ATTR_ID_RAW_READ_ERROR_RATE:
		return "Uncorrectable read error count";
			break;
/**********************************************
		case 30:
            return "LIFETIME_WRITES0_TO_FLASH";
			break;
		case 31:
            return "LIFETIME_WRITES1_TO_FLASH";
			break;
		case 32:
            return "LIFETIME_WRITES0_FROM_HOST";
			break;
		case 33:
            return "LIFETIME_WRITES1_FROM_HOST";
			break;
		case 34:
            return "LIFETIME_READ0_FROM_HOST";
			break;
		case 35:
            return "LIFETIME_READ1_FROM_HOST";
			break;
		case 36:
            return "PCIE_PHY_CRC_ERROR";
			break;
		case 37:
            return "BAD_BLOCK_COUNT_SYSTEM";
			break;
		case 38:
            return "BAD_BLOCK_COUNT_USER";
			break;
		case 39:
            return "THERMAL_THROTTLING_STATUS";
			break;
**********************************************/
		case VS_ATTR_ID_GROWN_BAD_BLOCK_COUNT:
		return "Bad NAND block count";
			break;
		case VS_ATTR_ID_END_2_END_CORRECTION_COUNT:
		return "SSD End to end correction counts";
			break;
		case VS_ATTR_ID_MIN_MAX_WEAR_RANGE_COUNT:
		return "User data erase counts";
            break;
		case VS_ATTR_ID_REFRESH_COUNT:
		return "Refresh count";
			break;
		case VS_ATTR_ID_BAD_BLOCK_COUNT_USER:
		return "User data erase fail count";
			break;
		case VS_ATTR_ID_BAD_BLOCK_COUNT_SYSTEM:
		return "System area erase fail count";
			break;
		case VS_ATTR_ID_THERMAL_THROTTLING_STATUS:
		return "Thermal throttling status and count";
			break;
		case VS_ATTR_ID_ALL_PCIE_CORRECTABLE_ERROR_COUNT:
            return "PCIe Correctable Error count";
            break;
        case VS_ATTR_ID_ALL_PCIE_UNCORRECTABLE_ERROR_COUNT:
            return "PCIe Uncorrectable Error count";
            break;
        case VS_ATTR_ID_INCOMPLETE_SHUTDOWN_COUNT:
		return "Incomplete shutdowns";
			break;
		case VS_ATTR_ID_GB_ERASED_LSB:
		return "LSB of Flash GB erased";
			break;
		case VS_ATTR_ID_GB_ERASED_MSB:
            return "MSB of Flash GB erased";
			break;
		case VS_ATTR_ID_LIFETIME_DEVSLEEP_EXIT_COUNT:
		return "LIFETIME_DEV_SLEEP_EXIT_COUNT";
			break;
		case VS_ATTR_ID_LIFETIME_ENTERING_PS4_COUNT:
		return "LIFETIME_ENTERING_PS4_COUNT";
			break;
		case VS_ATTR_ID_LIFETIME_ENTERING_PS3_COUNT:
		return "LIFETIME_ENTERING_PS3_COUNT";
			break;
		case VS_ATTR_ID_RETIRED_BLOCK_COUNT:
		return "Retired block count"; /*VS_ATTR_ID_RETIRED_BLOCK_COUNT*/
			break;
		case VS_ATTR_ID_PROGRAM_FAILURE_COUNT:
		return "Program fail count";
			break;
		case VS_ATTR_ID_ERASE_FAIL_COUNT:
		return "Erase Fail Count";
			break;
		case VS_ATTR_ID_AVG_ERASE_COUNT:
		return "System data % used";
			break;
		case VS_ATTR_ID_UNEXPECTED_POWER_LOSS_COUNT:
		return "Unexpected power loss count";
			break;
		case VS_ATTR_ID_WEAR_RANGE_DELTA:
		return "Wear range delta";
			break;
		case VS_ATTR_ID_SATA_INTERFACE_DOWNSHIFT_COUNT:
		return "PCIE_INTF_DOWNSHIFT_COUNT";
			break;
		case VS_ATTR_ID_END_TO_END_CRC_ERROR_COUNT:
		return "E2E_CRC_ERROR_COUNT";
			break;
		case VS_ATTR_ID_UNCORRECTABLE_ECC_ERRORS:
		return "Soft ECC error count";
			break;
		case VS_ATTR_ID_MAX_LIFE_TEMPERATURE:
		return "Max lifetime temperature";/*VS_ATTR_ID_MAX_LIFE_TEMPERATURE for extended*/
			break;
		case VS_ATTR_ID_RAISE_ECC_CORRECTABLE_ERROR_COUNT:
		return "RAIS_ECC_CORRECT_ERR_COUNT";
			break;
		case VS_ATTR_ID_UNCORRECTABLE_RAISE_ERRORS:
		return "Uncorrectable read error count";/*VS_ATTR_ID_UNCORRECTABLE_RAISE_ERRORS*/
			break;
		case VS_ATTR_ID_DRIVE_LIFE_PROTECTION_STATUS:
		return "DRIVE_LIFE_PROTECTION_STATUS";
			break;
		case VS_ATTR_ID_REMAINING_SSD_LIFE:
		return "Remaining SSD life";
			break;
		case VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB:
		return "LSB of Physical (NAND) bytes written";
			break;
		case VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB:
		return "MSB of Physical (NAND) bytes written";
			break;
		case VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB:
		return "LSB of Physical (HOST) bytes written";
			break;
		case VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB:
            return "MSB of Physical (HOST) bytes written";
			break;
		case VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB:
            return "LSB of Physical (NAND) bytes read";
			break;
		case VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB:
            return "MSB of Physical (NAND) bytes read";
			break;
		case VS_ATTR_ID_FREE_SPACE:
		return "Free Space";
			break;
		case VS_ATTR_ID_TRIM_COUNT_LSB:
		return "LSB of Trim count";
			break;
		case VS_ATTR_ID_TRIM_COUNT_MSB:
		return "MSB of Trim count";
			break;
		case VS_ATTR_ID_OP_PERCENTAGE:
		return "OP percentage";
			break;
		case VS_ATTR_ID_MAX_SOC_LIFE_TEMPERATURE:
		return "Max lifetime SOC temperature";
			break;
		default:
			return "Un-Known";
	}
}



uint64_t smart_attribute_vs(uint16_t verNo, SmartVendorSpecific attr)
{	
	uint64_t val = 0;
	fb_smart_attribute_data *attrFb;

    /**
     * These are all FaceBook specific attributes.
     */
    if(verNo >= EXTENDED_SMART_VERSION_FB) {
        attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
	}

/******************************************************************
	if(attr.AttributeNumber == VS_ATTR_POWER_CONSUMPTION) {
		attrFb = (fb_smart_attribute_data *)&attr;
		return attrFb->LSDword;
	} 
	else if(attr.AttributeNumber == VS_ATTR_THERMAL_THROTTLING_STATUS) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		return attrFb->LSDword;
	}
	else if(attr.AttributeNumber == VS_ATTR_PCIE_PHY_CRC_ERROR) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		return attrFb->LSDword;
	}
	else if(attr.AttributeNumber == VS_ATTR_BAD_BLOCK_COUNT_USER) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		return attrFb->LSDword;
	}
	else if(attr.AttributeNumber == VS_ATTR_BAD_BLOCK_COUNT_SYSTEM) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		return attrFb->LSDword;
	}
	else if(attr.AttributeNumber == VS_ATTR_LIFETIME_READ1_FROM_HOST) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
	}
	else if(attr.AttributeNumber == VS_ATTR_LIFETIME_READ0_FROM_HOST) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
	}
	else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES1_FROM_HOST) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
	}
	else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES0_FROM_HOST) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
	}
	else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES1_TO_FLASH) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
	}
	else if(attr.AttributeNumber == VS_ATTR_LIFETIME_WRITES0_TO_FLASH) {
		fb_smart_attribute_data *attrFb;
		attrFb = (fb_smart_attribute_data *)&attr;
		val = attrFb->MSDword;
		val = (val << 32) | attrFb->LSDword ;
		return val;
    }
******************************************************************/

	else
		return attr.Raw0_3;
}



void print_smart_log(uint16_t verNo, SmartVendorSpecific attr, int lastAttr) 
{
    static uint64_t lsbGbErased = 0, msbGbErased = 0, lsbLifWrtToFlash = 0, msbLifWrtToFlash = 0, lsbLifWrtFrmHost = 0, msbLifWrtFrmHost = 0, lsbLifRdToHost = 0, msbLifRdToHost = 0, lsbTrimCnt = 0, msbTrimCnt = 0;
    char buf[40] = {0};
    char strBuf[35] = {0};
    int hideAttr = 0;

	if(attr.AttributeNumber == VS_ATTR_ID_GB_ERASED_LSB)
    {
        lsbGbErased = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_GB_ERASED_MSB)
    {
        msbGbErased = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB)
    {
        lsbLifWrtToFlash = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB)
    {
        msbLifWrtToFlash = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB)
    {
        lsbLifWrtFrmHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB) {
        msbLifWrtFrmHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB) {
        lsbLifRdToHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
        }

    if(attr.AttributeNumber == VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB)
    {
        msbLifRdToHost = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_TRIM_COUNT_LSB)
    {
        lsbTrimCnt = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if(attr.AttributeNumber == VS_ATTR_ID_TRIM_COUNT_MSB)
    {
        msbTrimCnt = smart_attribute_vs(verNo, attr);
        hideAttr = 1;
    }

    if((attr.AttributeNumber != 0) &&(hideAttr != 1)) {
        printf("%-40s", print_ext_smart_id(attr.AttributeNumber));
        printf("%-15d", attr.AttributeNumber  );
        printf(" 0x%016" PRIX64 "", smart_attribute_vs(verNo, attr));
        printf("\n");
    }

    if( lastAttr == 1 ) {

        sprintf(strBuf, "%s", (print_ext_smart_id(VS_ATTR_ID_GB_ERASED_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_GB_ERASED_MSB << 8 | VS_ATTR_ID_GB_ERASED_LSB);

        sprintf(buf, "0x%016" PRIX64 "%016" PRIX64 "", msbGbErased, lsbGbErased);
        printf(" %s", buf);
        printf("\n");

        sprintf(strBuf, "%s", (print_ext_smart_id(VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_MSB << 8 | VS_ATTR_ID_LIFETIME_WRITES_TO_FLASH_LSB);

        sprintf(buf, "0x%016" PRIX64 "%016" PRIX64 , msbLifWrtToFlash, lsbLifWrtToFlash);
    		printf(" %s", buf);
		printf("\n");

        sprintf(strBuf, "%s", (print_ext_smart_id(VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_MSB << 8 | VS_ATTR_ID_LIFETIME_WRITES_FROM_HOST_LSB);

        sprintf(buf, "0x%016" PRIX64 "%016" PRIX64 , msbLifWrtFrmHost, lsbLifWrtFrmHost);
        printf(" %s", buf);
        printf("\n");

        sprintf(strBuf, "%s", (print_ext_smart_id(VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB) + 7));
        printf("%-40s", strBuf);

        printf("%-15d", VS_ATTR_ID_LIFETIME_READS_TO_HOST_MSB << 8 | VS_ATTR_ID_LIFETIME_READS_TO_HOST_LSB);

        sprintf(buf, "0x%016" PRIX64 "%016" PRIX64, msbLifRdToHost, lsbLifRdToHost);
        printf(" %s", buf);
        printf("\n");

        sprintf(strBuf, "%s", (print_ext_smart_id(VS_ATTR_ID_TRIM_COUNT_LSB) + 7));
            printf("%-40s", strBuf);
            printf("%-15d", VS_ATTR_ID_TRIM_COUNT_MSB << 8 | VS_ATTR_ID_TRIM_COUNT_LSB);

        sprintf(buf, "0x%016" PRIX64 "%016" PRIX64, msbTrimCnt, lsbTrimCnt);
        printf(" %s", buf);
            printf("\n");
	}
}


void print_smart_log_CF(fb_log_page_CF *pLogPageCF)
{
    uint64_t currentTemp, maxTemp;
    printf("\n\nSeagate DRAM Supercap SMART Attributes :\n");
    printf("%-39s %-19s \n", "Description", "Supercap Attributes");

    printf("%-40s", "Super-cap current temperature");
    currentTemp = pLogPageCF->AttrCF.SuperCapCurrentTemperature;
    /*currentTemp = currentTemp ? currentTemp - 273 : 0;*/
    printf(" 0x%016" PRIX64 "", currentTemp);
    printf("\n");		

    maxTemp = pLogPageCF->AttrCF.SuperCapMaximumTemperature;
    /*maxTemp = maxTemp ? maxTemp - 273 : 0;*/
    printf("%-40s", "Super-cap maximum temperature");
    printf(" 0x%016" PRIX64 "", maxTemp);
    printf("\n");

    printf("%-40s", "Super-cap status");
    printf(" 0x%016" PRIX64 "", (uint64_t)pLogPageCF->AttrCF.SuperCapStatus);
    printf("\n");

    printf("%-40s", "Data units read to DRAM namespace");
    printf(" 0x%016" PRIX64 "%016" PRIX64 "", pLogPageCF->AttrCF.DataUnitsReadToDramNamespace.MSU64,
           pLogPageCF->AttrCF.DataUnitsReadToDramNamespace.LSU64);
    printf("\n");

    printf("%-40s", "Data units written to DRAM namespace");
    printf(" 0x%016" PRIX64 "%016" PRIX64 "", pLogPageCF->AttrCF.DataUnitsWrittenToDramNamespace.MSU64, 
           pLogPageCF->AttrCF.DataUnitsWrittenToDramNamespace.LSU64);
    printf("\n");

    printf("%-40s", "DRAM correctable error count");
    printf(" 0x%016" PRIX64 "", pLogPageCF->AttrCF.DramCorrectableErrorCount);
    printf("\n");

    printf("%-40s", "DRAM uncorrectable error count");
    printf(" 0x%016" PRIX64 "", pLogPageCF->AttrCF.DramUncorrectableErrorCount);
    printf("\n");

}
int get_Ext_Smrt_Log(tDevice *device)//, nvmeGetLogPageCmdOpts * getLogPageCmdOpts)
{
    #ifdef _DEBUG
        printf("-->%s\n",__FUNCTION__);
    #endif
	int ret = 0, index = 0;
	EXTENDED_SMART_INFO_T ExtdSMARTInfo;
	memset( &ExtdSMARTInfo, 0x00, sizeof(ExtdSMARTInfo));
	ret = nvme_Read_Ext_Smt_Log(device, &ExtdSMARTInfo);
	if (!ret) {
        printf("%-39s %-15s %-19s \n", "Description", "Ext-Smart-Id", "Ext-Smart-Value");
        for(index=0; index<80; index++)
            printf("-");
        printf("\n");
        for(index =0; index < NUMBER_EXTENDED_SMART_ATTRIBUTES; index++)
            print_smart_log(ExtdSMARTInfo.Version, ExtdSMARTInfo.vendorData[index], index == (NUMBER_EXTENDED_SMART_ATTRIBUTES - 1));

    }
	return 0;

}
int cler_Pcie_correctble_errs (tDevice *device)
{
    #ifdef _DEBUG
        printf("-->%s\n",__FUNCTION__);
    #endif
	return 0;

}



#endif
